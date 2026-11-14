"""Tests of the engine functions of the point, colour and matrix types and of the CRT rand."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from math_common import (Buf, CONTROL_WORDS, PRECISIONS, F32, F64, Fixed, Globals, I32, Machine, Original, ReadBack, Seed,  # noqa: E402
                          Spec, U32, EXE_PATH, define_precision_defect, SCRATCH, canary, define, func_size, inputs, malloc_stub,
                          matrices, mixed_floats, edge_floats, pack, record, register, seed_stubs, vec_inputs,
                          vectors, f32, Stub, ensure_built, TableFixed)

IN12 = Buf(size=12, layout="fff", io="in")
OUT12 = Buf(size=12, layout="fff", io="out")
IN36 = Buf(size=36, layout="f" * 9, io="in")


def size(addr, last):
    return func_size(addr, last)


def scalars(seed, count):
    return mixed_floats(seed, count) + edge_floats() + [0.0, -0.0, 1.0, -1.0, 2.0, 0.5, 3.0, 7.0]


def zip_rows(*columns):
    n = min(len(c) for c in columns)
    return [tuple(c[i] for c in columns) for i in range(n)]


def vec_rows(seed, arity, extra=()):
    rows = vec_inputs(seed, 600, arity)
    return [row + tuple(extra) for row in rows]


def out_rows(seed, arity, out_index):
    """Vector rows with a canary-filled out buffer inserted at out_index."""
    rows = vec_inputs(seed, 600, arity)
    return [row[:out_index] + (canary(12),) + row[out_index:] for row in rows]


# NiPoint3 ------------------------------------------------------------------------------------

# At 24 bits the correct result also rounds the sum to float before the root, so this defect is
# the correct behavior there. It is a defect at 53 bits only.
NI_LENGTH = define(Spec("NiPoint3_Length", Original(0x4145B0, size(0x4145B0, 0x4145D0), "thiscall", [IN12], F64),
                        "bc_NiPoint3_Length", lambda: vec_inputs(11, 600, 1)),
                   defect_symbol="bc_NiPoint3_Length_defect", defect_precisions=(53,))

define_precision_defect(NI_LENGTH, "bc_NiPoint3_Length_defect_p24")

define(Spec("NiPoint3_NiPoint3", Original(0x414540, size(0x414540, 0x414558), "thiscall",
                                          [Buf(size=12, layout="fff", io="out"), F32, F32, F32], None),
            "bc_NiPoint3_NiPoint3",
            lambda: [(canary(12), a, b, c) for a, b, c in zip(mixed_floats(1, 600), mixed_floats(2, 600),
                                                              mixed_floats(3, 600))]
            + [(canary(12), a, 0.0, -a) for a in edge_floats()],
            eax_arg=0))

define(Spec("NiPoint3_DivideByScalar", Original(0x414580, size(0x414580, 0x4145AC), "thiscall",
                                                [IN12, Buf(size=12, layout="fff", io="out"), F32], None),
            "bc_NiPoint3_DivideByScalar",
            lambda: [(v[0], canary(12), s) for v, s in zip(vec_inputs(12, 700, 1),
                                                          scalars(13, 700) * 2)]
            + [(pack([1.0, 2.0, 3.0]), canary(12), s) for s in scalars(14, 100)],
            eax_arg=1))

define(Spec("NiPoint3_UnitCross", Original(0x422770, size(0x422770, 0x422807), "thiscall",
                                           [IN12, Buf(size=12, layout="fff", io="out"), IN12], None),
            "bc_NiPoint3_UnitCross", lambda: out_rows(15, 2, 1), eax_arg=1))

# TGMatrix3 -----------------------------------------------------------------------------------

define(Spec("TGMatrix3_MatrixTimesPoint", Original(0x459CB0, size(0x459CB0, 0x459D0A), "thiscall",
                                                    [IN36, Buf(size=12, layout="fff", io="out"), IN12], None),
            "bc_TGMatrix3_MatrixTimesPoint",
            lambda: [(m, canary(12), v[0]) for m, v in zip(matrices(21, 700) * 2, vec_inputs(22, 700, 1))],
            eax_arg=1))

define(Spec("TGMatrix3_PointTimesMatrix", Original(0x459D10, size(0x459D10, 0x459D6C), "cdecl",
                                                    [Buf(size=12, layout="fff", io="out"), IN12, IN36], None),
            "bc_TGMatrix3_PointTimesMatrix",
            lambda: [(canary(12), v[0], m) for m, v in zip(matrices(23, 700) * 2, vec_inputs(24, 700, 1))],
            eax_arg=0))

# TGPoint3 ------------------------------------------------------------------------------------

define(Spec("TGPoint3_Cross", Original(0x45C1A0, size(0x45C1A0, 0x45C1F0), "thiscall",
                                       [IN12, Buf(size=12, layout="fff", io="out"), IN12], None),
            "bc_TGPoint3_Cross", lambda: out_rows(31, 2, 1), eax_arg=1),
       defect_symbol="bc_TGPoint3_Cross_defect")

define(Spec("TGPoint3_UnitCross", Original(0x581E60, size(0x581E60, 0x581F1B), "thiscall",
                                           [IN12, Buf(size=12, layout="fff", io="out"), IN12], None),
            "bc_TGPoint3_UnitCross", lambda: out_rows(32, 2, 1), eax_arg=1))


def zero_vectors(count, seed):
    return [pack(v) for v in vectors(seed, count, edge=False, special=False)[:count]]


ZERO_VECTOR = Globals([(0x980F48, 12)], "zero vector")


def aligned_rows(seed):
    rows = out_rows(seed, 2, 1)
    zeros = zero_vectors(len(rows), seed + 5)
    return [row + (zeros[i % len(zeros)],) for i, row in enumerate(rows)]


define(Spec("TGPoint3_GetAlignedComponent", Original(0x45E980, size(0x45E980, 0x45EA43), "thiscall",
                                                     [IN12, Buf(size=12, layout="fff", io="out"), IN12], None),
            "bc_TGPoint3_GetAlignedComponent", lambda: aligned_rows(33), extras=[ZERO_VECTOR], eax_arg=1))

define(Spec("TGPoint3_GetPerpendicularComponent", Original(0x45EA50, size(0x45EA50, 0x45EA9B), "thiscall",
                                                           [IN12, Buf(size=12, layout="fff", io="out"), IN12],
                                                           None, allow=[(0x45E980, 0xC4)]),
            "bc_TGPoint3_GetPerpendicularComponent", lambda: aligned_rows(34), extras=[ZERO_VECTOR], eax_arg=1))

define(Spec("TGPoint3_MultMatrix", Original(0x45E8D0, size(0x45E8D0, 0x45E94F), "thiscall",
                                            [Buf(size=12, layout="fff", io="inout"), IN36], None,
                                            allow=[(0x459D10, 0x5D)]),
            "bc_TGPoint3_MultMatrix",
            lambda: [(v[0], m) for m, v in zip(matrices(35, 700) * 2, vec_inputs(36, 700, 1))]))

define(Spec("TGPoint3_MultMatrixLeft", Original(0x45E950, size(0x45E950, 0x45E97B), "thiscall",
                                                [Buf(size=12, layout="fff", io="inout"), IN36], None,
                                                allow=[(0x459CB0, 0x5B)]),
            "bc_TGPoint3_MultMatrixLeft",
            lambda: [(v[0], m) for m, v in zip(matrices(37, 700) * 2, vec_inputs(38, 700, 1))]))

define(Spec("TGPoint3_Set", Original(0x5EE360, size(0x5EE360, 0x5EE376), "thiscall",
                                     [Buf(size=12, layout="fff", io="inout"), F32, F32, F32], None),
            "bc_TGPoint3_Set",
            lambda: [(pack([1.5, 2.5, 3.5]), a, b, c) for a, b, c in zip(mixed_floats(41, 600),
                                                                       mixed_floats(42, 600),
                                                                       mixed_floats(43, 600))]
            + [(pack([1.5, 2.5, 3.5]), a, a, -a) for a in edge_floats()]))


def random_seeds(count, seed):
    r = inputs.rng(seed)
    return [0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 12345] + [r.getrandbits(32) for _ in range(count)]


RAND_SIZE = size(0x859DF3, 0x859E14)

define(Spec("rand", Original(0x859DF3, RAND_SIZE, "cdecl", [], I32), "bc_crt_rand",
            lambda: [(s,) for s in random_seeds(700, 51)], extras=[Seed()], stubs=seed_stubs()))

define(Spec("TGPoint3_GetRandomUnitVector", Original(0x45E7B0, size(0x45E7B0, 0x45E8C9), "cdecl",
                                                     [Buf(size=12, layout="fff", io="out")], None,
                                                     allow=[(0x859DF3, RAND_SIZE)]),
            "bc_TGPoint3_GetRandomUnitVector",
            lambda: [(canary(12), s) for s in random_seeds(700, 52)], extras=[Seed()], stubs=seed_stubs(),
            eax_arg=0))

MODELS = [("Forward", 0x434CD0, 0x434CF8, 0x980DF0, 0x45E460), ("Backward", 0x5ED6D0, 0x5ED6F8, 0x980F20, 0x45E4B0),
          ("Up", 0x5022E0, 0x502308, 0x980E30, 0x45E500), ("Down", 0x5ED7C0, 0x5ED7E8, 0x980EE8, 0x45E550),
          ("Right", 0x502310, 0x502338, 0x980E50, 0x45E5A0), ("Left", 0x5ED8B0, 0x5ED8D8, 0x980E70, 0x45E5F0)]


def model_rows(seed):
    return [(canary(12), pack(v)) for v in vectors(seed, 600, edge=True, special=False)]


for name, getter, last, global_addr, init in MODELS:
    define(Spec("TGPoint3_GetModel" + name, Original(getter, size(getter, last), "cdecl",
                                                     [Buf(size=12, layout="fff", io="out")], None),
                "bc_TGPoint3_GetModel" + name, (lambda s=60 + getter % 7: model_rows(s)),
                extras=[Globals([(global_addr, 12)], "model " + name.lower())], eax_arg=0))
    define(Spec("TGPoint3_InitModel" + name, Original(init, 0x1F, "cdecl", [], None), "bc_TGPoint3_InitModel" + name,
                lambda: [()], extras=[ReadBack(global_addr, 12, "fff", "model " + name.lower())]))

define(Spec("TGPoint3_InitZero", Original(0x45E640, 0x1F, "cdecl", [], None), "bc_TGPoint3_InitZero",
            lambda: [()], extras=[ReadBack(0x980F48, 12, "fff", "zero vector")]))

# The static initializers of the zero points that SubsystemProperty copies. The data starts as 9.0 so that a missing
# store shows.
define(Spec("NiPoint3_InitZero", Original(0x8119E0, size(0x8119E0, 0x8119FE), "cdecl", [], None), "bc_NiPoint3_InitZero",
            lambda: [()], extras=[Fixed([(0x9A2878, pack([9.0, 9.0, 9.0]))]), ReadBack(0x9A2878, 12, "fff", "zero point")]))
define(Spec("NiPoint2_InitZero", Original(0x820010, size(0x820010, 0x820024), "cdecl", [], None), "bc_NiPoint2_InitZero",
            lambda: [()], extras=[Fixed([(0x9A2BB8, pack([9.0, 9.0]))]), ReadBack(0x9A2BB8, 8, "ff", "zero 2D point")]))

for axis, addr, last, glob in (("X", 0x811A00, 0x811A1E, 0x9A2840), ("Y", 0x811A20, 0x811A3E, 0x9A2858),
                               ("Z", 0x811A40, 0x811A5E, 0x9A2868)):
    define(Spec("NiPoint3_InitUnit" + axis, Original(addr, size(addr, last), "cdecl", [], None),
                "bc_NiPoint3_InitUnit" + axis, lambda: [()], extras=[ReadBack(glob, 12, "fff", "unit " + axis)]))

# UnitizeVectors with the table that the original builder makes -------------------------------

def unitize_rows(seed):
    r = inputs.rng(seed)
    vecs = vectors(seed, 400, special=True)
    rows = []
    for i in range(0, len(vecs) - 5):
        count = r.randrange(0, 6)
        flat = [c for v in vecs[i:i + 5] for c in v]
        rows.append((pack(flat), count))
    return rows


define(Spec("TGPoint3_UnitizeVectors", Original(0x821830, size(0x821830, 0x8218D7), "cdecl",
                                                [Buf(size=60, layout="f" * 15, io="inout"), U32], None),
            "bc_TGPoint3_UnitizeVectors", lambda: unitize_rows(71), extras=[TableFixed()]))

# TGColorA::Copy ------------------------------------------------------------------------------

define(Spec("TGColorA_Copy", Original(0x5EED10, size(0x5EED10, 0x5EED2D), "thiscall",
                                      [Buf(size=16, layout="ffff", io="inout"), F32, F32, F32, F32], None),
            "bc_TGColorA_Copy",
            lambda: [(pack([9.0, 8.0, 7.0, 6.0]), a, b, c, d)
                     for a, b, c, d in zip(mixed_floats(81, 600), mixed_floats(82, 600), mixed_floats(83, 600),
                                           mixed_floats(84, 600))]))


# Static initializers that allocate -----------------------------------------------------------

def init_fast_sqrt_table(precision):
    import ctypes
    calls = []

    def alloc(ctx):
        calls.append(ctx.arg(0))
        ctx.returns(ctx.heap.alloc(ctx.arg(0)))

    machine = Machine(EXE_PATH, {0x718CB0: Stub(alloc, nargs=1)})
    machine.call(Original(0x811A60, size(0x811A60, 0x811AD5), "cdecl", [], None), [], CONTROL_WORDS[precision])
    assert calls == [0x400], calls
    original = machine.read(machine.read_u32(0x9A284C), 0x400)
    lib = ctypes.CDLL(ensure_built(precision))
    buffer = (ctypes.c_uint32 * 256)()
    lib.bc_NiPoint3_InitFastSqrtTable(buffer)
    native = bytes(buffer)
    assert native == original, "table differs at entry %d" % next(i for i in range(256) if native[4 * i:4 * i + 4]
                                                                   != original[4 * i:4 * i + 4])
    record("NiPoint3_InitFastSqrtTable@p%d" % precision, inputs=1, entries=256, differing=0)
    print("  fast square root table at %d-bit: 256 entries equal; allocation size 0x400" % precision)


def free_fast_sqrt_table(precision):
    import ctypes
    freed = []
    machine = Machine(EXE_PATH, {0x718CF0: Stub(lambda ctx: freed.append(ctx.arg(0)), nargs=1)})
    machine.uc.mem_write(0x9A284C, struct.pack("<I", 0x30001234))
    machine.call(Original(0x811AF0, size(0x811AF0, 0x811B08), "cdecl", [], None), [], CONTROL_WORDS[precision])
    assert freed == [0x30001234], freed
    assert machine.read_u32(0x9A284C) == 0
    lib = ctypes.CDLL(ensure_built(precision))
    assert lib.bc_NiPoint3_FreeFastSqrtTable() == 1
    record("NiPoint3_FreeFastSqrtTable@p%d" % precision, inputs=1, differing=0)
    print("  free at %d-bit: original passes the stored pointer to operator_delete and zeroes the pointer; "
          "native zeroes it" % precision)


for _precision in PRECISIONS:
    register("math.NiPoint3_InitFastSqrtTable@p%d" % _precision)(lambda p=_precision: init_fast_sqrt_table(p))
    register("math.NiPoint3_FreeFastSqrtTable@p%d" % _precision)(lambda p=_precision: free_fast_sqrt_table(p))
