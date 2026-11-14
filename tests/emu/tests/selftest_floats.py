"""Self-tests for float handling: x87 control word, ULP tolerance, thiscall, ST0 return.

0x469620 is a thiscall method with one pointer argument (RET 4). It reads two
3-float vectors, multiplies pairwise and sums from the last component down,
and returns the sum in ST0. It was identified from its instructions
(FLD/FMUL/FADDP on [eax+8], [ecx+8], [eax+4], [ecx+4], [eax], [ecx]).
"""
import struct

from emuharness import (CW_24BIT, DEFAULT_CW, Buf, Case, F32, Native, Original, build_fixture,
                        inputs, register)

DOT = 0x469620
DOT_SIZE = 27


def vector_pairs(count, seed):
    values = inputs.random_floats(seed, count * 6, bits=32, magnitude=100.0)
    pairs = []
    for i in range(count):
        a = struct.pack("<3f", *values[6 * i:6 * i + 3])
        b = struct.pack("<3f", *values[6 * i + 3:6 * i + 6])
        pairs.append((a, b))
    return pairs


def dot_case(symbol, cw, ulps=None, label=""):
    original = Original(DOT, DOT_SIZE, "thiscall", [Buf(layout="<3f", io="in"), Buf(layout="<3f", io="in")], F32)
    return Case(original, Native(build_fixture("selftest.c"), symbol), vector_pairs(500, 7), cw=cw,
                ulps=ulps or {}, max_shown=2, label=label)


@register("selftest.dot_53bit_matches_double_math")
def dot_53bit():
    return dot_case("fx_dot_double", DEFAULT_CW)


@register("selftest.dot_24bit_matches_float_math")
def dot_24bit():
    return dot_case("fx_dot_float", CW_24BIT)


@register("selftest.dot_24bit_vs_double_math_differs", expect_fail=True)
def dot_24bit_against_double_math():
    return dot_case("fx_dot_double", CW_24BIT)


@register("selftest.dot_53bit_vs_float_math_differs", expect_fail=True)
def dot_53bit_against_float_math():
    return dot_case("fx_dot_float", DEFAULT_CW)


@register("selftest.ulp_tolerance_rejects_when_too_small", expect_fail=True)
def one_ulp_error_without_tolerance():
    return dot_case("fx_dot_double_plus_1ulp", DEFAULT_CW)


@register("selftest.ulp_tolerance_accepts_one_ulp")
def one_ulp_error_with_tolerance():
    return dot_case("fx_dot_double_plus_1ulp", DEFAULT_CW, ulps={"ret": 1})
