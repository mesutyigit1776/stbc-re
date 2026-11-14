"""Cases: one original function, one native function, a set of inputs."""
import hashlib
import struct
import os
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple, Union

from . import compare, convert
from . import types as T
from .compare import Field
from .errors import HarnessError
from .machine import DEFAULT_CW, CallOutput, Machine, Original
from .native import Native
from .stubs import Stub

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
EXE_PATH = os.path.join(REPO_ROOT, "game", "stbc.exe")
CHECKSUM_PATH = os.path.join(REPO_ROOT, "checksums", "stbc.exe.sha256")

NOT_COMPARED = ("registers other than the return registers, flags, x87 status, writes to "
                "globals and the stack, and memory outside the Buf arguments")


@dataclass
class Case:
    original: Original
    native: Native
    inputs: Sequence[tuple]
    cw: Union[int, Sequence[int]] = DEFAULT_CW
    ulps: Dict[str, int] = field(default_factory=dict)
    stubs: Dict[Union[int, str], Stub] = field(default_factory=dict)
    max_shown: int = 5
    label: str = ""
    max_insns: int = 5_000_000


@dataclass
class CaseResult:
    ok: bool
    cw: int
    total: int
    lines: List[str]


def exe_sha256() -> str:
    with open(EXE_PATH, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def check_exe_hash() -> str:
    expected = open(CHECKSUM_PATH).read().split()[0]
    actual = exe_sha256()
    if actual != expected:
        raise HarnessError("stbc.exe hash %s does not match %s" % (actual, expected))
    return actual


def _scalar_ret(ret) -> Optional[T.Scalar]:
    return ret if isinstance(ret, T.Scalar) else None


def original_fields(func: Original, out: CallOutput) -> List[Field]:
    fields = []
    ret = _scalar_ret(func.ret)
    if ret is not None:
        if ret.is_float:
            nbits = 8 * ret.size
            mant, exp = (23, 8) if ret.size == 4 else (52, 11)
            bits = convert.float80_to_bits(out.st0_raw, mant, exp)
            note = "[st0=0x%s]" % out.st0_raw[::-1].hex()
            fields.append(Field("ret", ret, bits.to_bytes(ret.size, "little"), note))
        else:
            value = (out.edx << 32 | out.eax) if ret.size == 8 else out.eax
            fields.append(Field("ret", ret, (value & ((1 << 8 * ret.size) - 1)).to_bytes(ret.size, "little")))
    for index, arg in enumerate(func.args):
        if isinstance(arg, T.Buf) and arg.io != "in":
            fields += compare.buffer_fields(index, arg, out.buffers[index])
    return fields


def native_fields(func: Original, value, buffers) -> List[Field]:
    fields = []
    ret = _scalar_ret(func.ret)
    if ret is not None:
        fields.append(Field("ret", ret, T.encode(ret, value)))
    for index, arg in enumerate(func.args):
        if isinstance(arg, T.Buf) and arg.io != "in":
            fields += compare.buffer_fields(index, arg, buffers[index])
    return fields


def _format_inputs(func: Original, values) -> str:
    parts = []
    for arg, v in zip(func.args, values):
        if isinstance(arg, T.Buf):
            data = bytes(v)
            if arg.layout and struct.calcsize(arg.layout) <= len(data):
                parts.append(repr(struct.unpack_from(arg.layout, data)))
            else:
                parts.append(repr(data))
        elif isinstance(v, T.Raw):
            parts.append("raw(0x%x)" % v.bits)
        else:
            sc = arg.scalar if isinstance(arg, T.St0) else arg
            if sc.is_float:
                bits = int.from_bytes(T.encode(sc, v), "little")
                parts.append("%r [0x%0*x]" % (v, sc.size * 2, bits))
            else:
                parts.append(repr(v))
    return "(" + ", ".join(parts) + ")"


def run_case(case: Case) -> List[CaseResult]:
    cws = [case.cw] if isinstance(case.cw, int) else list(case.cw)
    machine = Machine(EXE_PATH, case.stubs, case.max_insns)
    bound = case.native.bind(case.original.args, _scalar_ret(case.original.ret))
    return [_run_one_cw(case, machine, bound, cw) for cw in cws]


def _run_one_cw(case: Case, machine: Machine, bound, cw: int) -> CaseResult:
    func = case.original
    failures: List[Tuple[int, tuple, List[str]]] = []
    for index, values in enumerate(case.inputs):
        try:
            out = machine.call(func, values, cw)
        except HarnessError as exc:
            lines = ["    original run stopped: %s: %s" % (type(exc).__name__, exc)]
            return _result(case, cw, index + 1, failures + [(index, values, lines)], aborted=True)
        value, buffers = bound.call(values)
        diffs = compare.compare_fields(original_fields(func, out), native_fields(func, value, buffers),
                                       case.ulps)
        if diffs:
            failures.append((index, values, [d.text() for d in diffs]))
    return _result(case, cw, len(case.inputs), failures)


def _result(case, cw, total, failures, aborted=False) -> CaseResult:
    func = case.original
    head = "  %s%s; original 0x%08x; %d inputs; tolerance %s" % (
        case.label + ": " if case.label else "", compare.describe_cw(cw), func.addr, total,
        case.ulps or "none (bit-exact)")
    lines = [head]
    for index, values, detail in failures[:case.max_shown]:
        lines.append("    input #%d %s" % (index, _format_inputs(func, values)))
        lines += detail
    if len(failures) > case.max_shown:
        lines.append("    ... %d more differing inputs" % (len(failures) - case.max_shown))
    if failures:
        lines.insert(1, "    %d of %d inputs %s" % (len(failures), total,
                                                    "stopped the run" if aborted else "differ"))
    return CaseResult(not failures, cw, total, lines)
