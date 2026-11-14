"""Shared helpers for the point, colour and matrix tests.

A test compares one function of the exe with the reconstructed function from
build/libbc_emutest_p53 or build/libbc_emutest_p24. The 53-bit test runs the original at x87
control word 0x027F. The 24-bit test runs it at 0x007F. Both must match bit for bit.
NaN results are compared as "both NaN": the host default NaN is 0x7FC00000 and the x87
default NaN is 0xFFC00000.
"""
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from emuharness import (CW_24BIT, DEFAULT_CW, Buf, F32, F64, I32, Machine, Native, Original, Stub,  # noqa: E402
                        U32, inputs, register)
from emuharness import types as T  # noqa: E402
from emuharness.case import EXE_PATH, REPO_ROOT, _format_inputs, original_fields  # noqa: E402
from emuharness.compare import buffer_fields, Field  # noqa: E402
from emuharness.machine import SCRATCH  # noqa: E402
from emuharness.native import BoundNative  # noqa: E402
from emuharness.stubs import HEAP_BASE, malloc_stub  # noqa: E402
from emuharness import convert  # noqa: E402

BUILD_DIR = os.path.join(REPO_ROOT, "build")
PRECISIONS = (53, 24)
CONTROL_WORDS = {53: DEFAULT_CW, 24: CW_24BIT}
SUFFIX = ".dylib" if sys.platform == "darwin" else ".so"


def lib_path(precision):
    return os.path.join(BUILD_DIR, "libbc_emutest_p%d%s" % (precision, SUFFIX))
RESULTS_PATH = os.path.join(os.path.dirname(HERE), "build", "math_results.json")
CANARY = b"\xA5"


def ensure_built(precision):
    sources = []
    for folder in ("src/engine", "src/emutest"):
        for name in os.listdir(os.path.join(REPO_ROOT, folder)):
            sources.append(os.path.join(REPO_ROOT, folder, name))
    sources.append(os.path.join(REPO_ROOT, "CMakeLists.txt"))
    libs = [lib_path(p) for p in PRECISIONS]
    if all(os.path.exists(l) for l in libs) and min(os.path.getmtime(l) for l in libs) >= max(
            os.path.getmtime(s) for s in sources):
        return lib_path(precision)
    subprocess.run(["cmake", "-S", REPO_ROOT, "-B", BUILD_DIR], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", BUILD_DIR], check=True, capture_output=True)
    return lib_path(precision)


# Records ---------------------------------------------------------------------------------

def record(name, **fields):
    try:
        with open(RESULTS_PATH) as f:
            data = json.load(f)
    except (OSError, ValueError):
        data = {}
    data[name] = fields
    os.makedirs(os.path.dirname(RESULTS_PATH), exist_ok=True)
    with open(RESULTS_PATH, "w") as f:
        json.dump(data, f, indent=1, sort_keys=True)


# Machine with preset globals ---------------------------------------------------------------

class PresetMachine(Machine):
    """A machine that writes preset bytes into the image before each call."""

    presets = ()

    def _restore_writable(self):
        super()._restore_writable()
        for addr, data in self.presets:
            self.write(addr, data)


# Extras: data outside the argument list --------------------------------------------------

class Globals:
    """Globals in the exe that the function reads. The input supplies the bytes.

    segments is a list of (address, size). The native shim gets one buffer with the segments in order.
    """
    where = "before"
    takes_input = True

    def __init__(self, segments, name="globals"):
        self.segments = segments
        self.name = name
        self.size = sum(s for _, s in segments)

    def native_arg(self):
        return Buf(size=self.size, io="in")

    def presets(self, value):
        out, offset = [], 0
        for addr, size in self.segments:
            out.append((addr, bytes(value[offset:offset + size])))
            offset += size
        return out


class Fixed:
    """Fixed bytes in the exe. The native side has no argument for them."""
    where = "none"
    takes_input = False

    def __init__(self, presets):
        self._presets = presets

    def presets(self, value=None):
        return list(self._presets)


class ReadBack:
    """Memory in the exe that the function writes. The native shim fills one buffer."""
    where = "after"
    takes_input = False

    def __init__(self, addr, size, layout, name="global"):
        self.addr, self.size, self.layout, self.name = addr, size, layout, name

    def native_arg(self):
        return Buf(size=self.size, io="out")


SEED_BLOCK = SCRATCH + 0x5000
SEED_ADDR = SEED_BLOCK + 0x14


class Seed:
    """The rand seed in the per-thread block (the block lookup 0x8613e1 is stubbed)."""
    where = "before"
    takes_input = True
    name = "seed"

    def native_arg(self):
        return Buf(size=4, layout="<I", io="inout")

    def presets(self, value):
        return [(SEED_ADDR, struct.pack("<I", value))]


def seed_stubs():
    return {0x8613E1: Stub(lambda ctx: ctx.returns(SEED_BLOCK), nargs=0)}


# Input generation ------------------------------------------------------------------------

def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def mixed_floats(seed, count):
    """Floats from several magnitude ranges and random bit patterns."""
    r = inputs.rng(seed)
    out = []
    for i in range(count):
        kind = i % 6
        if kind == 0:
            v = r.uniform(-100.0, 100.0)
        elif kind == 1:
            v = r.uniform(-1.0, 1.0)
        elif kind == 2:
            v = r.uniform(-1e-3, 1e-3)
        elif kind == 3:
            v = r.choice((-1, 1)) * 10 ** r.uniform(-20, 20)
        elif kind == 4:
            v = inputs.f32_from_bits(r.getrandbits(32))
            if v != v or abs(v) == float("inf"):
                v = 0.5
        else:
            v = r.choice((-1, 1)) * 10 ** r.uniform(-7, 3)
        out.append(f32(v))
    return out


def edge_floats():
    return inputs.finite_edges(32) + [2.0, -2.0, 0.5, 1e-6, 9.9999e-7, 1.0000001e-6, 3.0, 1e19, -1e19, 1e-19]


def special_floats():
    return [inputs.f32_from_bits(b) for b in (0x7FC00000, 0x7F800000, 0xFF800000)]


def vectors(seed, count=600, edge=True, special=True):
    """Lists of 3 floats."""
    flat = mixed_floats(seed, count * 3)
    out = [flat[3 * i:3 * i + 3] for i in range(count)]
    if edge:
        for e in edge_floats():
            out += [[e, 0.0, 0.0], [0.0, e, 0.0], [0.0, 0.0, e], [e, e, e], [e, 1.0, -e], [1.0, e, 2.0]]
    out += [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [1.0, 1.0, 1.0],
            [3.0, 4.0, 0.0], [-0.0, -0.0, -0.0], [1e-3, 1e-3, 1e-3], [5e-7, 0, 0], [1e-6, 0, 0]]
    if special:
        sp = special_floats()
        r = inputs.rng(seed + 1)
        for _ in range(120):
            v = [r.choice(sp + [0.0, 1.0, -1.0, 2.5]) for _ in range(3)]
            out.append(v)
    return out


def pack(values, fmt=None):
    return struct.pack("<%df" % len(values), *values)


def vec_inputs(seed, count=600, arity=1, **kw):
    """Tuples of `arity` 12-byte vectors."""
    sets = [vectors(seed + 17 * k, count, **kw) for k in range(arity)]
    n = min(len(s) for s in sets)
    rows = []
    for i in range(n):
        rows.append(tuple(pack(s[i]) for s in sets))
    # pair edge vectors with random ones too
    if arity > 1:
        base = sets[0]
        for i, vec in enumerate(vectors(seed + 999, 60, special=False)):
            rows.append(tuple([pack(base[(i * 7) % len(base)])] + [pack(vec)] * (arity - 1)))
    return rows


def matrices(seed, count):
    flat = mixed_floats(seed, count * 9)
    rows = [struct.pack("<9f", *flat[9 * i:9 * i + 9]) for i in range(count)]
    ident = struct.pack("<9f", 1, 0, 0, 0, 1, 0, 0, 0, 1)
    rows += [ident, bytes(36), struct.pack("<9f", *([2.0] * 9)), struct.pack("<9f", *([1e19] * 9))]
    return rows


def canary(n):
    return CANARY * n


# Running ---------------------------------------------------------------------------------

def _expected_eax(args, values):
    """Address that the harness gives to each Buf argument (bump allocation from HEAP_BASE)."""
    nxt, addrs = HEAP_BASE, {}
    for index, (atype, value) in enumerate(zip(args, values)):
        if isinstance(atype, T.Buf):
            size = atype.size if atype.size is not None else len(bytes(value))
            start = (nxt + 15) & ~15
            nxt = start + atype.offset + size + 16
            addrs[index] = start + atype.offset
    return addrs


class Spec:
    def __init__(self, name, original, symbol, make_inputs, extras=(), stubs=None, eax_arg=None,
                 native_ret=None, note="", check_global_ret=None):
        self.name = name
        self.original = original
        self.symbol = symbol
        self.make_inputs = make_inputs
        self.extras = list(extras)
        self.stubs = stubs or {}
        self.eax_arg = eax_arg
        self.native_ret = native_ret
        self.note = note


def _float_ret(spec):
    r = spec.original.ret
    return r if isinstance(r, T.Scalar) else None


def _build_native(spec, precision):
    before = [e for e in spec.extras if e.where == "before"]
    after = [e for e in spec.extras if e.where == "after"]
    args = [e.native_arg() for e in before] + list(spec.original.args) + [e.native_arg() for e in after]
    ret = _float_ret(spec)
    return BoundNative(Native(ensure_built(precision), spec.symbol), args, ret), before, after


def _compare(fields_o, fields_n):
    """Return (differences, nan_loose_count)."""
    diffs, loose = [], 0
    for a, b in zip(fields_o, fields_n):
        if a.raw == b.raw:
            continue
        s = a.scalar
        if s is not None and s.is_float:
            nbits = 8 * s.size
            if convert.is_nan(a.bits(), nbits) and convert.is_nan(b.bits(), nbits):
                loose += 1
                continue
        diffs.append("      %s: original=%s  native=%s" % (a.name, a.text(), b.text()))
    return diffs, loose


def run_spec(spec, precision, rows=None):
    """Return dict(total, failures, loose, lines)."""
    func = spec.original
    cw = CONTROL_WORDS[precision]
    bound, before, after = _build_native(spec, precision)
    machine = PresetMachine(EXE_PATH, spec.stubs, 5_000_000)
    rows = rows if rows is not None else spec.make_inputs()
    failures, loose_total = [], 0
    n = len(func.args)
    for index, row in enumerate(rows):
        orig_vals = list(row[:n])
        extra_vals = list(row[n:])
        presets, native_before, ex_i = [], [], 0
        for e in spec.extras:
            value = extra_vals[ex_i] if e.takes_input else None
            ex_i += 1 if e.takes_input else 0
            if e.where in ("before", "none"):
                presets += e.presets(value)
            if e.where == "before":
                native_before.append(struct.pack("<I", value) if isinstance(e, Seed) else value)
        machine.presets = presets
        try:
            out = machine.call(func, orig_vals, cw)
        except Exception as exc:  # harness errors become failures with context
            failures.append((index, row, ["      original run stopped: %s: %s" % (type(exc).__name__, exc)]))
            break
        fields_o = original_fields(func, out)
        nvals = native_before + orig_vals + [bytes(e.size) if hasattr(e, "size") else b"" for e in after]
        value, buffers = bound.call(nvals)
        # native fields in the same order as the original fields
        fields_n = []
        ret = _float_ret(spec)
        if ret is not None:
            fields_n.append(Field("ret", ret, T.encode(ret, value)))
        shift = len(native_before)
        for i, atype in enumerate(func.args):
            if isinstance(atype, T.Buf) and atype.io != "in":
                fields_n += buffer_fields(i, atype, buffers[i + shift])
        extra_diffs = []
        for k, e in enumerate(after):
            data_o = machine.read(e.addr, e.size)
            idx = len(before) + n + k
            fo = buffer_fields(100 + k, Buf(size=e.size, layout=e.layout), data_o)
            fn = buffer_fields(100 + k, Buf(size=e.size, layout=e.layout), buffers[idx])
            fields_o += fo
            fields_n += fn
        for k, e in enumerate(before):
            if isinstance(e, Seed):
                fo = buffer_fields(200, Buf(size=4, layout="<I"), machine.read(SEED_ADDR, 4))
                fields_o += fo
                fields_n += buffer_fields(200, Buf(size=4, layout="<I"), buffers[k])
        diffs, loose = _compare(fields_o, fields_n)
        loose_total += loose
        if spec.eax_arg is not None:
            want = _expected_eax(func.args, orig_vals)[spec.eax_arg]
            if out.eax != want:
                diffs.append("      eax=0x%08x, address of argument %d is 0x%08x" % (out.eax, spec.eax_arg, want))
        if diffs:
            failures.append((index, row, diffs))
    shown = []
    for index, row, diffs in failures[:3]:
        shown.append("    input #%d %s" % (index, _format_row(spec, row)))
        shown += diffs
    return dict(total=len(rows), failures=len(failures), loose=loose_total, lines=shown)


def _format_row(spec, row):
    n = len(spec.original.args)
    text = _format_inputs(spec.original, row[:n])
    if len(row) > n:
        text += " extras=" + repr(tuple(row[n:]))
    return text


def check_spec(spec, precision, must_differ=False):
    """Run the spec at the control word of the precision against the library of that precision.

    The test is green when no input differs. With must_differ it is green when at least one input differs.
    """
    rows = spec.make_inputs()
    started = time.time()
    result = run_spec(spec, precision, rows)
    record("%s@p%d" % (spec.name, precision), inputs=result["total"], differing=result["failures"],
           nan_loose=result["loose"], control_word="0x%04X" % CONTROL_WORDS[precision],
           seconds=round(time.time() - started, 2))
    print("  %s at %d-bit (CW 0x%04X): %d inputs; differing %d (NaN-only matches %d)"
          % (spec.name, precision, CONTROL_WORDS[precision], result["total"], result["failures"], result["loose"]))
    for line in result["lines"]:
        print(line)
    if must_differ:
        assert result["failures"] > 0, "no input differs at %d-bit" % precision
    else:
        assert result["failures"] == 0, "%d of %d inputs differ at %d-bit" % (result["failures"], result["total"],
                                                                              precision)
    return result["failures"]


def define(spec, defect_symbol=None, defect_precisions=PRECISIONS):
    """Register the test of spec for each precision.

    A planted defect is a wrong native function. Its test is expected to fail at the precisions in
    defect_precisions.
    """
    for precision in PRECISIONS:
        @register("math.%s@p%d" % (spec.name, precision))
        def _test(precision=precision):
            check_spec(spec, precision)

    if defect_symbol:
        defect = Spec(spec.name + "_defect", spec.original, defect_symbol, spec.make_inputs, spec.extras,
                      spec.stubs, spec.eax_arg)
        for precision in defect_precisions:
            @register("math.%s_planted_defect@p%d" % (spec.name, precision), expect_fail=True)
            def _defect_test(precision=precision):
                check_spec(defect, precision)
    return spec


def define_precision_defect(spec, defect_symbol):
    """Register one test for a defect that only the 24-bit run can find.

    The defective function matches the original at 53 bits and differs at 24 bits. The test is green
    only when both are true, and it prints both results.
    """
    defect = Spec(spec.name + "_defect_p24", spec.original, defect_symbol, spec.make_inputs, spec.extras,
                  spec.stubs, spec.eax_arg)

    @register("math.%s_planted_defect_p24_only" % spec.name)
    def _test():
        check_spec(defect, 53)
        differing = check_spec(defect, 24, must_differ=True)
        print("  planted defect: passes at 53-bit, fails at 24-bit (%d inputs differ)" % differing)


def func_size(addr, last):
    """Size of a function whose last byte is at `last`."""
    return last - addr + 1


# The fast square root table that the original builder (0x811a60) makes -----------------------

TABLE_ADDR = SCRATCH + 0x4000
_table_cache = []


def original_table():
    if not _table_cache:
        machine = Machine(EXE_PATH, {0x718CB0: malloc_stub()})
        machine.call(Original(0x811A60, func_size(0x811A60, 0x811AD5), "cdecl", [], None), [], DEFAULT_CW)
        _table_cache.append(machine.read(machine.read_u32(0x9A284C), 0x400))
    return _table_cache[0]


class TableFixed(Fixed):
    """Puts the table and the table pointer into the image."""

    def __init__(self):
        pass

    def presets(self, value=None):
        return [(TABLE_ADDR, original_table()), (0x9A284C, struct.pack("<I", TABLE_ADDR))]
