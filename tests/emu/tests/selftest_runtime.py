"""Self-tests on small C runtime functions of the exe.

0x860440 strlen, 0x859d47 wcslen, 0x859d20 _ftol. Each was identified from its
instructions: strlen is the dword-at-a-time zero-byte scan, wcslen is the 16-bit
scan that returns the length in characters, _ftol saves the x87 control word,
sets round-to-zero, runs FISTP and returns EDX:EAX.
"""
import struct

from emuharness import (Buf, Case, F64, I64, Native, Original, St0, U32, build_fixture, inputs,
                        register)

STRLEN = 0x860440
STRLEN_SIZE = 0x7B
WCSLEN = 0x859D47
WCSLEN_SIZE = 29
FTOL = 0x859D20
FTOL_SIZE = 39


def fixture():
    return build_fixture("selftest.c")


def strings():
    out = [b"a" * n + b"\0" + bytes([0x41, 0x42, 0x43]) * 3 for n in range(0, 70)]
    out.append(b"\0\0\0\0\0\0\0\0")
    out.append(b"ab\0cd\0")
    return inputs.single(out)


def strlen_cases(symbol, offsets=range(4)):
    return [Case(Original(STRLEN, STRLEN_SIZE, "cdecl", [Buf(offset=offset, io="in")], U32),
                 Native(fixture(), symbol), strings(),
                 label="pointer offset %d" % offset, max_shown=1)
            for offset in offsets]


@register("selftest.strlen")
def strlen_matches_c():
    return strlen_cases("fx_strlen")


@register("selftest.strlen_planted_defect", expect_fail=True)
def strlen_off_by_one_is_detected():
    return strlen_cases("fx_strlen_off_by_one", offsets=(0, 1))


@register("selftest.wcslen")
def wcslen_matches_c():
    values = [struct.pack("<%dH" % (n + 3), *([0x41 + n % 7] * n + [0, 0x4141, 0x4242]))
              for n in range(0, 40)]
    return Case(Original(WCSLEN, WCSLEN_SIZE, "cdecl", [Buf(io="in")], U32),
                Native(fixture(), "fx_wcslen"), inputs.single(values))


def ftol_inputs():
    values = inputs.finite_edges(64) + [0.5, -0.5, 0.9999999999999999, -0.9999999999999999, 1.5, -1.5,
                                         2.5, -2.5, 4294967295.5, -4294967296.5, 9.2e18, -9.2e18,
                                         9.3e18, -9.3e18]
    values += inputs.random_floats(20260101, 300, 64, magnitude=1e6)
    values += inputs.random_floats(20260102, 100, 64, magnitude=1e15)
    values += [float("inf"), float("-inf"), float("nan")]
    return inputs.single(values)


def ftol_case(symbol, cw):
    return Case(Original(FTOL, FTOL_SIZE, "stdcall", [St0(F64)], I64), Native(fixture(), symbol),
                ftol_inputs(), cw=cw, max_shown=2)


@register("selftest.ftol")
def ftol_matches_truncation():
    return ftol_case("fx_ftol", (0x027F, 0x007F))


@register("selftest.ftol_planted_defect", expect_fail=True)
def ftol_rounding_is_detected():
    return ftol_case("fx_ftol_round", 0x027F)
