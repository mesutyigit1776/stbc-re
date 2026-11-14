"""Compare the original result with the native result and report differences."""
import struct
from dataclasses import dataclass
from typing import Dict, List, Optional

from . import convert
from . import types as T

CW_PRECISION = {0: "24-bit", 2: "53-bit", 3: "64-bit"}


def describe_cw(cw: int) -> str:
    return "x87 CW=0x%04X (%s precision)" % (cw, CW_PRECISION.get((cw >> 8) & 3, "reserved"))


@dataclass
class Field:
    """One output value: a scalar, or raw bytes when scalar is None."""
    name: str
    scalar: Optional[T.Scalar]
    raw: bytes
    note: str = ""

    def bits(self) -> int:
        return int.from_bytes(self.raw, "little")

    def numeric(self) -> str:
        if self.scalar is None:
            return repr(self.raw)
        value = T.decode(self.scalar, self.raw)
        return repr(value)

    def text(self) -> str:
        width = len(self.raw) * 2
        text = "0x%0*x (%s %s)" % (width, self.bits(), self.scalar.name if self.scalar else "bytes",
                                  self.numeric())
        return text + (" " + self.note if self.note else "")


def buffer_fields(index: int, arg: T.Buf, data: bytes) -> List[Field]:
    name = "arg%d" % index
    if arg.layout is None:
        return [Field(name, None, data)]
    fields, offset = [], 0
    for n, ch in enumerate(arg.layout.lstrip("<>=!@")):
        size = struct.calcsize("<" + ch)
        scalar = {"f": T.F32, "d": T.F64, "i": T.I32, "I": T.U32, "h": T.I16, "H": T.U16,
                  "b": T.I8, "B": T.U8, "q": T.I64, "Q": T.U64}[ch]
        fields.append(Field("%s[%d]" % (name, n), scalar, data[offset:offset + size]))
        offset += size
    if offset > len(data):
        raise ValueError("buffer layout %r needs %d bytes, buffer has %d" % (arg.layout, offset, len(data)))
    if offset < len(data):
        fields.append(Field("%s[rest]" % name, None, data[offset:]))
    return fields


@dataclass
class Difference:
    field: str
    original: Field
    native: Field
    ulps: Optional[int]

    def text(self) -> str:
        line = "      %s: original=%s  native=%s" % (self.field, self.original.text(), self.native.text())
        if self.ulps is not None:
            line += "  diff=%d ulp" % self.ulps
        elif self.original.scalar is not None and self.original.scalar.is_float:
            line += "  diff=NaN involved"
        return line


def compare_fields(original: List[Field], native: List[Field], ulps: Dict[str, int]) -> List[Difference]:
    out = []
    for a, b in zip(original, native):
        tolerance = ulps.get(a.name, ulps.get("*", 0))
        if a.raw == b.raw:
            continue
        scalar = a.scalar
        if scalar is not None and scalar.is_float:
            nbits = 8 * scalar.size
            dist = convert.ulp_distance(a.bits(), b.bits(), nbits)
            if tolerance and dist is None and convert.is_nan(a.bits(), nbits) \
                    and convert.is_nan(b.bits(), nbits):
                continue
            if tolerance and dist is not None and dist <= tolerance:
                continue
            out.append(Difference(a.name, a, b, dist))
        else:
            out.append(Difference(a.name, a, b, None))
    return out
