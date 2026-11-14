"""Fixed and seeded input sets for float tests."""
import itertools
import random
import struct
from typing import Iterable, List, Sequence

_log: List[str] = []


def take_log() -> List[str]:
    out, _log[:] = list(_log), []
    return out


def rng(seed: int) -> random.Random:
    _log.append("random inputs use seed %d" % seed)
    return random.Random(seed)


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def f64_from_bits(bits: int) -> float:
    return struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0]


def float_edges(bits: int = 64) -> List[float]:
    """0, -0, tiny, huge, values around 1, NaN and infinity."""
    if bits == 32:
        make = f32_from_bits
        tiny = [0x00000001, 0x00800000, 0x007FFFFF]
        huge = [0x7F7FFFFF, 0x7F000000]
        near_one = [0x3F800000, 0x3F7FFFFF, 0x3F800001, 0xBF800000, 0xBF7FFFFF, 0xBF800001]
        special = [0x7F800000, 0xFF800000, 0x7FC00000]
    else:
        make = f64_from_bits
        tiny = [0x0000000000000001, 0x0010000000000000, 0x000FFFFFFFFFFFFF]
        huge = [0x7FEFFFFFFFFFFFFF, 0x7FE0000000000000]
        near_one = [0x3FF0000000000000, 0x3FEFFFFFFFFFFFFF, 0x3FF0000000000001,
                    0xBFF0000000000000, 0xBFEFFFFFFFFFFFFF, 0xBFF0000000000001]
        special = [0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000]
    sign = 1 << (bits - 1)
    patterns = [0, sign] + tiny + [t | sign for t in tiny] + huge + [h | sign for h in huge] \
        + near_one + special
    return [make(p) for p in patterns]


def finite_edges(bits: int = 64) -> List[float]:
    return [v for v in float_edges(bits) if v == v and abs(v) != float("inf")]


def random_floats(seed: int, count: int, bits: int = 64, finite: bool = True,
                  magnitude: float = None) -> List[float]:
    """Random floats. With magnitude, uniform in [-magnitude, magnitude]; else random bit patterns."""
    r = rng(seed)
    out = []
    while len(out) < count:
        if magnitude is not None:
            v = r.uniform(-magnitude, magnitude)
            if bits == 32:
                v = f32_from_bits(struct.unpack("<I", struct.pack("<f", v))[0])
        else:
            v = (f32_from_bits if bits == 32 else f64_from_bits)(r.getrandbits(bits))
        if finite and (v != v or abs(v) == float("inf")):
            continue
        out.append(v)
    return out


def single(values: Iterable) -> List[tuple]:
    return [(v,) for v in values]


def product(*sets: Sequence) -> List[tuple]:
    return list(itertools.product(*sets))


def zipped(*sets: Sequence) -> List[tuple]:
    return list(zip(*sets))
