"""Argument and return types shared by the original side and the native side."""
import ctypes
import struct
from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class Scalar:
    name: str
    size: int
    kind: str
    fmt: str
    ctype: object

    @property
    def is_float(self) -> bool:
        return self.kind == "float"

    @property
    def signed(self) -> bool:
        return self.kind == "sint"


I8 = Scalar("i8", 1, "sint", "<b", ctypes.c_int8)
U8 = Scalar("u8", 1, "uint", "<B", ctypes.c_uint8)
I16 = Scalar("i16", 2, "sint", "<h", ctypes.c_int16)
U16 = Scalar("u16", 2, "uint", "<H", ctypes.c_uint16)
I32 = Scalar("i32", 4, "sint", "<i", ctypes.c_int32)
U32 = Scalar("u32", 4, "uint", "<I", ctypes.c_uint32)
I64 = Scalar("i64", 8, "sint", "<q", ctypes.c_int64)
U64 = Scalar("u64", 8, "uint", "<Q", ctypes.c_uint64)
F32 = Scalar("f32", 4, "float", "<f", ctypes.c_float)
F64 = Scalar("f64", 8, "float", "<d", ctypes.c_double)


@dataclass(frozen=True)
class Buf:
    """Pointer to a buffer. The test gives the initial bytes; the harness reads them back.

    layout is a struct format such as "<3f". With a layout, each field is
    compared on its own and may have a ULP tolerance. Without it the buffer is
    compared as bytes. size pads the input bytes with zeros. offset moves the
    pointer that is passed away from the 16-byte aligned start, to reach
    alignment paths. io is "in", "out" or "inout"; "in" buffers are not compared.
    """
    size: Optional[int] = None
    layout: Optional[str] = None
    io: str = "inout"
    offset: int = 0


@dataclass(frozen=True)
class St0:
    """Argument passed in ST0 (x87 register) instead of on the stack."""
    scalar: Scalar


@dataclass(frozen=True)
class Raw:
    """Raw bit pattern for an argument, for example a NaN with a given payload."""
    bits: int


PTR = Scalar("ptr", 4, "uint", "<I", ctypes.c_void_p)
"""Pointer to an object: 4 bytes in the exe, a full pointer natively."""

VOID = None


def encode(scalar: Scalar, value) -> bytes:
    if isinstance(value, Raw):
        return (value.bits & ((1 << (8 * scalar.size)) - 1)).to_bytes(scalar.size, "little")
    if scalar.is_float:
        return struct.pack(scalar.fmt, value)
    return (int(value) & ((1 << (8 * scalar.size)) - 1)).to_bytes(scalar.size, "little")


def decode(scalar: Scalar, raw: bytes):
    return struct.unpack(scalar.fmt, raw[:scalar.size])[0]


def native_value(scalar: Scalar, value):
    if isinstance(value, Raw):
        return decode(scalar, encode(scalar, value))
    return value
