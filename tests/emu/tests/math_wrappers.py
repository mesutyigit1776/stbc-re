"""Tests of the inline engine code in the Appc wrappers of the point and colour types.

The original side runs the whole wrapper. PyArg_ParseTuple (0x74e310) writes the chosen values
into its out pointers, SWIG_ConvertPtr (0x5bae00) gives pointers to test buffers,
PyFloat_FromDouble (0x74fa70) and SWIG_NewPointerObj (0x5bb0e0) capture the result and
operator_new (0x718cb0) gives memory. The native side calls the class method through a shim.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import math_common as pc  # noqa: E402
from math_common import (Buf, CANARY, CONTROL_WORDS, PRECISIONS, EXE_PATH, F32, F64, Field, Fixed, Original, Stub,  # noqa: E402
                          T, TableFixed, U32, buffer_fields, ensure_built, func_size, mixed_floats, pack,
                          PresetMachine, record, register, vectors, edge_floats, BoundNative, Native, inputs)

PARSE, CONVERT, FLOAT_FROM_DOUBLE, NEW_POINTER, ALLOC = 0x74E310, 0x5BAE00, 0x74FA70, 0x5BB0E0, 0x718CB0
NONE_OBJECT = 0x95DD20
FLOAT_HANDLE, OBJECT_HANDLE = 0xF1000000, 0xB1000000

SIZES = {"P12": 12, "P16": 16}


class Wrapper:
    def __init__(self, name, addr, last, kinds, symbol, result, units=False, allow=(), table=False, count=None):
        self.name, self.addr, self.size = name, addr, func_size(addr, last)
        self.kinds = kinds.split() if kinds else []
        self.symbol, self.result = symbol, result
        self.units, self.allow, self.table = units, list(allow), table
        self.optional = count


class State:
    def reset(self, row, wrapper):
        self.row, self.wrapper = row, wrapper
        self.buffers = {}
        self.double = None
        self.pointer = None
        self.alloc_sizes = []
        self.parse_format = None


def make_stubs(state):
    def parse(ctx):
        fmt = bytearray()
        addr = ctx.arg(1)
        while True:
            byte = ctx.read(addr + len(fmt), 1)
            if byte == b"\0":
                break
            fmt += byte
        state.parse_format = bytes(fmt)
        spec = state.wrapper.kinds
        given = len(spec) if state.wrapper.optional is None else state.row[0]
        values = state.row[1:] if state.wrapper.optional is not None else state.row
        pointer_index = 0
        item = 0
        for ch in bytes(fmt).split(b":")[0]:
            if ch in b"|":
                continue
            if item >= given:
                break
            out = ctx.arg(2 + item)
            kind = spec[item]
            if ch == ord("O"):
                ctx.write(out, struct.pack("<I", 0x1000 + item))
            elif ch == ord("f"):
                ctx.write(out, struct.pack("<f", values[item]))
            elif ch == ord("i"):
                ctx.write(out, struct.pack("<i", values[item]))
            else:
                raise AssertionError("unexpected format character %r" % chr(ch))
            item += 1
        ctx.returns(1)

    def convert(ctx):
        handle, out = ctx.arg(0), ctx.arg(1)
        item = handle - 0x1000
        kind = state.wrapper.kinds[item]
        values = state.row[1:] if state.wrapper.optional is not None else state.row
        size = SIZES[kind]
        address = ctx.heap.alloc(size + 16)
        ctx.write(address, bytes(values[item]))
        state.buffers[item] = (address, size)
        ctx.write(out, struct.pack("<I", address))
        ctx.returns(0)

    def to_double(ctx):
        state.double = struct.unpack("<Q", struct.pack("<II", ctx.stack_arg(0), ctx.stack_arg(1)))[0]
        ctx.returns(FLOAT_HANDLE)

    def new_pointer(ctx):
        state.pointer = ctx.arg(0)
        ctx.returns(OBJECT_HANDLE)

    def alloc(ctx):
        size = ctx.arg(0)
        address = ctx.heap.alloc(size + 16)
        ctx.write(address, CANARY * size)
        state.alloc_sizes.append((address, size))
        ctx.returns(address)

    return {PARSE: Stub(parse, nargs=0), CONVERT: Stub(convert, nargs=4), FLOAT_FROM_DOUBLE: Stub(to_double, nargs=2),
            NEW_POINTER: Stub(new_pointer, nargs=2), ALLOC: Stub(alloc, nargs=1)}


class UnitsFixed(Fixed):
    def __init__(self):
        pass

    def presets(self, value=None):
        return [(0x9A2840, pack([1, 0, 0])), (0x9A2858, pack([0, 1, 0])), (0x9A2868, pack([0, 0, 1]))]


def native_for(wrapper, precision):
    args = []
    for kind in wrapper.kinds:
        args.append(Buf(size=SIZES[kind], io="inout") if kind in SIZES else F32)
    if wrapper.result in ("obj12", "obj16"):
        args.append(Buf(size=int(wrapper.result[3:]), io="out"))
    ret = {"f32": F32, "f64": F64}.get(wrapper.result)
    return BoundNative(Native(ensure_built(precision), wrapper.symbol), args, ret)


def run_wrapper(wrapper, rows, precision):
    cw = CONTROL_WORDS[precision]
    state = State()
    stubs = make_stubs(state)
    machine = PresetMachine(EXE_PATH, stubs, 2_000_000)
    presets = []
    if wrapper.units:
        presets += UnitsFixed().presets()
    if wrapper.table:
        presets += TableFixed().presets()
    machine.presets = presets
    original = Original(wrapper.addr, wrapper.size, "cdecl", [U32, U32], U32, allow=wrapper.allow)
    bound = native_for(wrapper, precision)
    failures, loose_total = [], 0
    refcount_before = machine.read_u32(NONE_OBJECT)
    for index, row in enumerate(rows):
        state.reset(row, wrapper)
        try:
            out = machine.call(original, [0, 0], cw)
        except Exception as exc:
            failures.append((index, row, ["      original stopped: %s: %s" % (type(exc).__name__, exc)]))
            break
        problems = []
        fields_o, fields_n = [], []
        values = list(row[1:] if wrapper.optional is not None else row)
        given = len(wrapper.kinds) if wrapper.optional is None else row[0]
        native_args = []
        for item, kind in enumerate(wrapper.kinds):
            if kind in SIZES:
                native_args.append(bytes(values[item]))
            else:
                native_args.append(values[item] if item < given else 0.0)
        if wrapper.result in ("obj12", "obj16"):
            native_args.append(bytes(int(wrapper.result[3:])))
        value, buffers = bound.call(native_args)
        # result category
        if wrapper.result in ("none",):
            if out.eax != NONE_OBJECT or machine.read_u32(NONE_OBJECT) != refcount_before + 1:
                problems.append("      None result expected: eax=0x%08x, refcount %d -> %d"
                                % (out.eax, refcount_before, machine.read_u32(NONE_OBJECT)))
        elif wrapper.result in ("f32", "f64"):
            if out.eax != FLOAT_HANDLE or state.double is None:
                problems.append("      float result expected: eax=0x%08x" % out.eax)
            else:
                fields_o.append(Field("result", F64, struct.pack("<Q", state.double)))
                fields_n.append(Field("result", F64, T.encode(F64, value) if wrapper.result == "f64"
                                      else T.encode(F64, float(value))))
        else:
            size = int(wrapper.result[3:])
            if out.eax != OBJECT_HANDLE or state.pointer is None or len(state.alloc_sizes) != 1 \
                    or state.alloc_sizes[0] != (state.pointer, size):
                problems.append("      object result expected: eax=0x%08x pointer=%r allocations=%r"
                                % (out.eax, state.pointer, state.alloc_sizes))
            else:
                data = machine.read(state.pointer, size)
                fields_o += buffer_fields(50, Buf(size=size, layout="f" * (size // 4)), data)
                fields_n += buffer_fields(50, Buf(size=size, layout="f" * (size // 4)), buffers[len(wrapper.kinds)])
        if wrapper.result not in ("obj12", "obj16") and state.alloc_sizes:
            problems.append("      unexpected allocation %r" % state.alloc_sizes)
        for item, kind in enumerate(wrapper.kinds):
            if kind in SIZES:
                address, size = state.buffers[item]
                layout = "f" * (size // 4)
                fields_o += buffer_fields(item, Buf(size=size, layout=layout), machine.read(address, size))
                fields_n += buffer_fields(item, Buf(size=size, layout=layout), buffers[item])
        diffs, loose = pc._compare(fields_o, fields_n)
        loose_total += loose
        problems += diffs
        if problems:
            failures.append((index, row, problems))
    lines = []
    for index, row, problems in failures[:3]:
        lines.append("    input #%d %r" % (index, row))
        lines += problems
    return dict(total=len(rows), failures=len(failures), loose=loose_total, lines=lines)


def check_wrapper(wrapper, rows, precision):
    started = time.time()
    result = run_wrapper(wrapper, rows, precision)
    record("%s@p%d" % (wrapper.name, precision), inputs=result["total"], differing=result["failures"],
           nan_loose=result["loose"], control_word="0x%04X" % CONTROL_WORDS[precision],
           seconds=round(time.time() - started, 2))
    print("  %s at %d-bit (CW 0x%04X): %d inputs; differing %d (NaN-only matches %d)"
          % (wrapper.name, precision, CONTROL_WORDS[precision], result["total"], result["failures"],
             result["loose"]))
    for line in result["lines"]:
        print(line)
    assert result["failures"] == 0, "%d of %d inputs differ at %d-bit" % (result["failures"], result["total"],
                                                                          precision)


# Inputs ----------------------------------------------------------------------------------

def scalar_values(seed, count):
    return mixed_floats(seed, count) + edge_floats() + [0.0, -0.0, 1.0, -1.0, 2.0, 0.5]


def rows_for(wrapper, seed):
    kinds = wrapper.kinds
    columns = []
    for position, kind in enumerate(kinds):
        s = seed + 31 * position
        if kind == "P12":
            columns.append([pack(v) for v in vectors(s, 600)])
        elif kind == "P16":
            flat = mixed_floats(s, 700 * 4) + edge_floats()
            columns.append([pack(flat[4 * i:4 * i + 4]) for i in range(len(flat) // 4)]
                           + [pack([e, 0.0, 1.0, e]) for e in edge_floats()])
        else:
            columns.append(scalar_values(s, 700))
    if not columns:
        return [()]
    n = max(len(c) for c in columns)
    rows = [tuple(c[(i * (3 + position)) % len(c)] for position, c in enumerate(columns)) for i in range(n)]
    return rows


def optional_rows(seed):
    r = inputs.rng(seed)
    rows = []
    values = scalar_values(seed, 700)
    for i in range(700):
        given = i % 5
        rows.append((given,) + tuple(values[(i + 7 * k) % len(values)] for k in range(4)))
    return rows


def register_wrapper(wrapper, seed, defect=None):
    for precision in PRECISIONS:
        @register("math.wrapper.%s@p%d" % (wrapper.name, precision))
        def _test(precision=precision):
            rows = optional_rows(seed) if wrapper.optional is not None else rows_for(wrapper, seed)
            check_wrapper(wrapper, rows, precision)


SET = "none"
F = "P12 f"

WRAPPERS = []


def add(name, addr, last, kinds, symbol, result, **kw):
    WRAPPERS.append(Wrapper(name, addr, last, kinds, symbol, result, **kw))


for letter, set_addr, set_last, get_addr, get_last in (
        ("r", 0x5BD400, 0x5BD471, 0x5BD480, 0x5BD4DF), ("g", 0x5BD4E0, 0x5BD552, 0x5BD560, 0x5BD5C0),
        ("b", 0x5BD5D0, 0x5BD642, 0x5BD650, 0x5BD6B0), ("a", 0x5BD6C0, 0x5BD732, 0x5BD740, 0x5BD7A0)):
    add("Appc_NiColorA_%s_set" % letter, set_addr, set_last, "P16 f", "bc_NiColorA_%s_set" % letter, "none")
    add("Appc_NiColorA_%s_get" % letter, get_addr, get_last, "P16", "bc_NiColorA_%s_get" % letter, "f32")
add("Appc_new_NiColorA", 0x5BD7B0, 0x5BD853, "f f f f", "bc_NiColorA_new", "obj16", count=4)

for letter, set_addr, set_last, get_addr, get_last in (
        ("x", 0x5BDDF0, 0x5BDE61, 0x5BDE70, 0x5BDECF), ("y", 0x5BDED0, 0x5BDF42, 0x5BDF50, 0x5BDFB0),
        ("z", 0x5BDFC0, 0x5BE032, 0x5BE040, 0x5BE0A0)):
    add("Appc_NiPoint3_%s_set" % letter, set_addr, set_last, "P12 f", "bc_NiPoint3_%s_set" % letter, "none")
    add("Appc_NiPoint3_%s_get" % letter, get_addr, get_last, "P12", "bc_NiPoint3_%s_get" % letter, "f32")
add("Appc_new_NiPoint3", 0x5BE0B0, 0x5BE127, "f f f", "bc_NiPoint3_new", "obj12")
add("Appc_NiPoint3_Length", 0x5BE1A0, 0x5BE21D, "P12", "bc_NiPoint3_Length", "f64")
add("Appc_NiPoint3_SqrLength", 0x5BE220, 0x5BE29B, "P12", "bc_NiPoint3_SqrLength", "f64")
add("Appc_NiPoint3_Dot", 0x5BE2A0, 0x5BE348, "P12 P12", "bc_NiPoint3_Dot", "f64")
add("Appc_NiPoint3_Unitize", 0x5BE350, 0x5BE425, "P12", "bc_NiPoint3_Unitize", "f64")
add("Appc_NiPoint3_Cross", 0x5BE430, 0x5BE51E, "P12 P12", "bc_NiPoint3_Cross", "obj12")
add("Appc_NiPoint3_UnitCross", 0x5BE520, 0x5BE673, "P12 P12", "bc_NiPoint3_UnitCross_new", "obj12")
add("Appc_NiPoint3_Perpendicular", 0x5BE680, 0x5BE902, "P12", "bc_NiPoint3_Perpendicular_new", "obj12", units=True,
    allow=[(0x414580, 0x2D), (0x414540, 0x19), (0x4145B0, 0x21), (0x422770, 0x98)])

for axis, get_addr, get_last, set_addr, set_last in (
        ("X", 0x5EDA80, 0x5EDADF, 0x5EDBC0, 0x5EDC31), ("Y", 0x5EDAE0, 0x5EDB40, 0x5EDC40, 0x5EDCB2),
        ("Z", 0x5EDB50, 0x5EDBB0, 0x5EDCC0, 0x5EDD32)):
    add("Appc_TGPoint3_Get" + axis, get_addr, get_last, "P12", "bc_TGPoint3_Get" + axis, "f32")
    add("Appc_TGPoint3_Set" + axis, set_addr, set_last, "P12 f", "bc_TGPoint3_Set" + axis, "none")
add("Appc_TGPoint3_SetXYZ", 0x5EDD40, 0x5EDDCA, "P12 f f f", "bc_TGPoint3_SetXYZ", "none")
add("Appc_TGPoint3_Add", 0x5EE0B0, 0x5EE15E, "P12 P12", "bc_TGPoint3_Add", "none")
add("Appc_TGPoint3_Subtract", 0x5EE160, 0x5EE20E, "P12 P12", "bc_TGPoint3_Subtract", "none")
add("Appc_TGPoint3_Scale", 0x5EE210, 0x5EE295, "P12 f", "bc_TGPoint3_Scale", "none")
add("Appc_TGPoint3_UnitizeVector", 0x5EE540, 0x5EE635, "P12", "bc_TGPoint3_UnitizeVector", "none", table=True)
add("Appc_TGPoint3_VectorLength", 0x5EE640, 0x5EE720, "P12", "bc_TGPoint3_VectorLength", "f64", table=True)

add("Appc_new_TGColorA", 0x5EE730, 0x5EE785, "", "bc_TGColorA_new", "obj16")
for letter, get_addr, get_last, set_addr, set_last in (
        ("R", 0x5EE800, 0x5EE85F, 0x5EE9B0, 0x5EEA21), ("G", 0x5EE860, 0x5EE8C0, 0x5EEA30, 0x5EEAA2),
        ("B", 0x5EE8D0, 0x5EE930, 0x5EEAB0, 0x5EEB22), ("A", 0x5EE940, 0x5EE9A0, 0x5EEB30, 0x5EEBA2)):
    add("Appc_TGColorA_Get" + letter, get_addr, get_last, "P16", "bc_TGColorA_Get" + letter, "f32")
    add("Appc_TGColorA_Set" + letter, set_addr, set_last, "P16 f", "bc_TGColorA_Set" + letter, "none")
add("Appc_TGColorA_SetRGBA", 0x5EEBB0, 0x5EEC45, "P16 f f f f", "bc_TGColorA_SetRGBA", "none")
add("Appc_TGColorA_ScaleRGB", 0x5EED30, 0x5EEDB5, "P16 f", "bc_TGColorA_ScaleRGB", "none")

for number, wrapper in enumerate(WRAPPERS):
    register_wrapper(wrapper, 1000 + 97 * number)
