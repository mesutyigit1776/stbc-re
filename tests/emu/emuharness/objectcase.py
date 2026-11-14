"""Cases that call a function on real objects: one graph per side, compared field by field."""
from bisect import bisect_left
from collections import Counter
from dataclasses import dataclass, field, replace
from typing import Callable, Dict, List, Optional, Sequence, Tuple, Union

from . import compare
from . import types as T
from .case import EXE_PATH, CaseResult, native_fields, original_fields
from .errors import HarnessError
from .graph import compare_graphs
from .layout import Model
from .machine import DEFAULT_CW, Machine, Original
from .native import Native, load_library
from .objects import Global, GraphReader, Inner, Obj, ObjectBuilder, Root
from .sides import GuestSide, NativeSide
from .stubs import Stub

NOT_COMPARED_OBJECTS = ("members outside the field map, padding, addresses (pointers compare by graph "
                        "identity), memory that no root reaches, native strings and arrays, the return "
                        "values of setup calls, and anything a probe does not read")
MAX_LINES = 12


@dataclass(frozen=True)
class Pre:
    """A call that runs on both sides before the tested call. call names an entry of ObjectCase.setup_calls.

    Use it to bring objects into a state with the code of the exe, for example to run a constructor.
    Root(name) in args passes the address of a root object.
    """
    call: str
    args: Sequence = ()


@dataclass(frozen=True)
class SetupCall:
    original: Original
    native: Native


@dataclass(frozen=True)
class Probe:
    """Reads extra state on one side. read(side, addresses, ret) gets the Side (GuestSide or NativeSide),
    the root addresses by name (also pointer roots) and the return value (an address for a pointer
    return); the two results must be equal."""
    name: str
    read: Callable


@dataclass
class Scenario:
    """One input: the objects to build, and the call arguments. Root(name) passes an object address.

    setup(machine, lib) runs after the state reset and before the build, on both sides: it writes
    global state (the machine is the guest, lib the native ctypes library).
    """
    roots: Dict[str, Optional[Obj]]
    args: Sequence = ()
    globals: Sequence[Global] = ()
    label: str = ""
    pre: Sequence[Pre] = ()
    setup: Optional[Callable] = None


@dataclass
class HeapReport:
    live: Dict[str, int] = field(default_factory=dict)
    leaks: List[str] = field(default_factory=list)
    faults: List[str] = field(default_factory=list)


@dataclass
class ObjectCase:
    """The original function and the native function run on equal objects.

    ret_class names the class of a returned object pointer (the original returns PTR). Without ret_class the
    returned pointer is opaque: the case compares only null against not null, and a probe can read the memory.
    memory maps guest addresses to bytes that are written before the first call, for
    example hand-written code or vtables in the scratch region.
    """
    model: Model
    original: Original
    native: Native
    scenarios: Sequence[Scenario]
    cw: Union[int, Sequence[int]] = DEFAULT_CW
    ulps: Dict[str, int] = field(default_factory=dict)
    stubs: Dict[Union[int, str], Stub] = field(default_factory=dict)
    memory: Dict[int, bytes] = field(default_factory=dict)
    ret_class: Optional[str] = None
    setup_calls: Dict[str, SetupCall] = field(default_factory=dict)
    probes: Sequence[Probe] = ()
    allow_leaks: bool = False
    max_shown: int = 5
    label: str = ""
    max_insns: int = 5_000_000

    def run(self) -> List[CaseResult]:
        cws = [self.cw] if isinstance(self.cw, int) else list(self.cw)
        machine = Machine(EXE_PATH, self.stubs, self.max_insns)
        machine.vtables = dict(self.model.by_vtable)
        for addr, data in self.memory.items():
            machine.write(addr, data)
        pointer_ret = self.original.ret is T.PTR
        ret = T.PTR if pointer_ret else (self.original.ret if isinstance(self.original.ret, T.Scalar) else None)
        bound = self.native.bind(self.original.args, ret)
        self._bound_setup = {name: _bind_setup(setup) for name, setup in self.setup_calls.items()}
        return [self._run_cw(machine, bound, cw) for cw in cws]

    def _run_cw(self, machine: Machine, bound, cw: int) -> CaseResult:
        failures = []
        for index, scenario in enumerate(self.scenarios):
            try:
                problems = self._run_scenario(machine, bound, scenario, cw)
            except HarnessError as exc:
                problems = ["original run stopped: %s: %s" % (type(exc).__name__, exc)]
            if problems:
                failures.append((index, scenario, problems))
        return self._result(cw, failures)

    def _run_scenario(self, machine: Machine, bound, scenario: Scenario, cw: int) -> List[str]:
        machine.reset_state()
        guest, native = GuestSide(machine, self.model), NativeSide(self.model)
        if scenario.setup is not None:
            scenario.setup(machine, load_library(self.model.native.lib))
        before = native.live_counts()
        guest_built = ObjectBuilder(guest).build(scenario.roots, scenario.globals)
        native_built = ObjectBuilder(native).build(scenario.roots, scenario.globals)
        guest_addr = _with_inner(scenario, guest, guest_built)
        native_addr = _with_inner(scenario, native, native_built)
        problems = self._run_pre(machine, scenario, guest_addr, native_addr, native, cw)
        heap_before = _heap_counts(machine, native)
        out = machine.call(self.original, _resolve(scenario.args, guest_addr), cw, fresh=False)
        value, buffers = bound.call(_resolve(scenario.args, native_addr))
        heap_after = _heap_counts(machine, native)

        guest_roots, native_roots = guest_built.roots(), native_built.roots()
        opaque = self.original.ret is T.PTR and self.ret_class is None
        if self.original.ret is T.PTR and not opaque:
            guest_roots["ret"] = (out.eax, self.ret_class)
            native_roots["ret"] = (value or 0, self.ret_class)
        guest_reader, native_reader = GraphReader(guest), GraphReader(native)
        problems += compare_graphs(guest_reader.read(guest_roots), native_reader.read(native_roots))
        if opaque:
            guest_reader.visited.add(out.eax)
            if bool(out.eax) != bool(value):
                problems.append("ret: original=%s native=%s" % ("null" if not out.eax else "a pointer",
                                                                 "null" if not value else "a pointer"))
        problems += ["native " + t for t in native.heap_overruns(
            {addr: cls for (addr, cls) in native_reader.objects})]
        problems += self._probe_problems(guest, native, guest_roots, native_roots, out.eax, value)

        scalar = replace(self.original, ret=None) if self.original.ret is T.PTR else self.original
        problems += [d.text().strip() for d in compare.compare_fields(
            original_fields(scalar, out), native_fields(scalar, value, buffers), self.ulps)]

        globals_at = set(native_built.addr[g.name] for g in scenario.globals)
        problems += self._heap_problems(
            guest_heap_report(machine, self.model, guest_reader.visited),
            native_heap_report(native, before, native_reader, globals_at))
        return problems + _heap_count_problems(heap_before, heap_after)

    def _run_pre(self, machine: Machine, scenario: Scenario, guest_addr, native_addr, native: NativeSide, cw: int) -> List[str]:
        problems = []
        for index, pre in enumerate(scenario.pre):
            if pre.call not in self.setup_calls:
                raise HarnessError("setup call %r is not in the case" % pre.call)
            setup, bound = self.setup_calls[pre.call], self._bound_setup[pre.call]
            out = machine.call(setup.original, _resolve(pre.args, guest_addr), cw, fresh=False)
            value, buffers = bound.call(_resolve(pre.args, native_addr))
            problems += ["native " + t for t in native.heap_overruns()]
            ret = setup.original.ret
            if isinstance(ret, T.Scalar) and ret is not T.PTR:
                diffs = compare.compare_fields(original_fields(setup.original, out),
                                               native_fields(setup.original, value, buffers), {})
                problems += ["setup call #%d %s: %s" % (index, pre.call, d.text().strip()) for d in diffs]
        return problems

    def _probe_problems(self, guest, native, guest_roots, native_roots, guest_ret, native_ret) -> List[str]:
        problems = []
        guest_addr = {n: a for n, (a, _) in guest_roots.items()}
        native_addr = {n: a for n, (a, _) in native_roots.items()}
        for probe in self.probes:
            one = probe.read(guest, guest_addr, guest_ret)
            two = probe.read(native, native_addr, native_ret)
            if one != two:
                problems.append("probe %s: original=%r native=%r" % (probe.name, one, two))
        return problems

    def _heap_problems(self, guest: HeapReport, native: HeapReport) -> List[str]:
        problems = ["original heap fault: " + f for f in guest.faults]
        if not self.allow_leaks:
            problems += guest.leaks + native.leaks
        for cls in sorted(native.live):
            if guest.live.get(cls, 0) != native.live[cls]:
                problems.append("live objects of %s: original=%d native=%d"
                                % (cls, guest.live.get(cls, 0), native.live[cls]))
        return problems

    def _result(self, cw: int, failures) -> CaseResult:
        func = self.original
        total = len(self.scenarios)
        lines = ["  %s%s; original 0x%08x; %d scenarios; tolerance %s" % (
            self.label + ": " if self.label else "", compare.describe_cw(cw), func.addr, total,
            self.ulps or "none (bit-exact)"),
            "    not compared: " + NOT_COMPARED_OBJECTS]
        if failures:
            lines.append("    %d of %d scenarios differ" % (len(failures), total))
        for index, scenario, problems in failures[:self.max_shown]:
            lines.append("    scenario #%d%s" % (index, " " + scenario.label if scenario.label else ""))
            lines += ["      " + p for p in problems[:MAX_LINES]]
            if len(problems) > MAX_LINES:
                lines.append("      ... %d more differences" % (len(problems) - MAX_LINES))
        if len(failures) > self.max_shown:
            lines.append("    ... %d more differing scenarios" % (len(failures) - self.max_shown))
        return CaseResult(not failures, cw, total, lines)


def _bind_setup(setup: SetupCall):
    ret = setup.original.ret
    return setup.native.bind(setup.original.args, ret if isinstance(ret, T.Scalar) else None)


def _heap_counts(machine: Machine, native: NativeSide) -> Tuple:
    blocks = machine.heap.blocks.values()
    return ((len(machine.heap.blocks), sum(1 for b in blocks if b.freed)), native.heap_counts())


def _heap_count_problems(before: Tuple, after: Tuple) -> List[str]:
    """Compare the blocks that the tested call allocated and freed, on both sides."""
    (g_before, n_before), (g_after, n_after) = before, after
    if n_before is None:
        return []
    problems = []
    for index, what in enumerate(("allocated", "freed")):
        guest, native = g_after[index] - g_before[index], n_after[index] - n_before[index]
        if guest != native:
            problems.append("heap blocks %s by the call: original=%d native=%d" % (what, guest, native))
    return problems


def _resolve(args: Sequence, addr: Dict[str, int]) -> list:
    return [addr[a.name] if isinstance(a, Root) else addr[a.key] if isinstance(a, Inner) else a for a in args]


def _with_inner(scenario: Scenario, side, built) -> Dict[str, int]:
    """The root addresses of a side and the addresses of the members that the arguments name (Inner)."""
    addr = dict(built.addr)
    arguments = list(scenario.args) + [a for pre in scenario.pre for a in pre.args]
    for arg in arguments:
        if isinstance(arg, Inner):
            cls = built.classes[arg.root]
            addr[arg.key] = built.addr[arg.root] + side.offset(cls, side.model.member(cls, arg.member))
    return addr


def guest_heap_report(machine: Machine, model: Model, visited) -> HeapReport:
    """Live objects per class, blocks that no root reaches, and the faults of the stubbed allocator."""
    report = HeapReport(faults=list(machine.heap.faults))
    reached = sorted(visited)
    for addr, block in sorted(machine.heap.blocks.items()):
        if block.freed:
            continue
        cls = _class_at(machine, model, addr, block.size) or block.cls
        if cls:
            report.live[cls] = report.live.get(cls, 0) + 1
        i = bisect_left(reached, addr)
        if not (i < len(reached) and reached[i] < addr + max(block.size, 1)):
            report.leaks.append("original leak: %s of %d bytes at 0x%08x is not freed and no root reaches it"
                                % (cls or "block", block.size, addr))
    return report


def _class_at(machine: Machine, model: Model, addr: int, size: int) -> Optional[str]:
    if size < 4:
        return None
    return model.by_vtable.get(int.from_bytes(machine.read(addr, 4), "little"))


def native_heap_report(side: NativeSide, before: Dict[str, int], reader: GraphReader,
                       global_addrs) -> HeapReport:
    """Live objects per class since the build, and objects that no root reaches."""
    after = side.live_counts()
    report = HeapReport(live={c: n - before.get(c, 0) for c, n in after.items()})
    reached = Counter(o.cls for (addr, _), o in reader.objects.items() if addr not in global_addrs)
    for cls, live in sorted(report.live.items()):
        if live > reached[cls]:
            report.leaks.append("native leak: %d object(s) of %s are alive and no root reaches them"
                                % (live - reached[cls], cls))
    return report
