"""Shared parts of the tests that run a whole Appc wrapper of the exe.

The Python interpreter is a set of stubs: the argument parser (0x74e310) writes the test values, the SWIG pointer
conversion (0x5bae00) gives the object, and the result helpers keep what the wrapper hands to Python (a float, an
integer or a pointer). The first argument of the wrapper is not used. The second is the object, which the argument
parser stub takes as the Python argument tuple.
"""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_property_common import *  # noqa: F401,F403,E402
from prop_property_common import property_stubs, state_setup  # noqa: E402
from emuharness import types as T  # noqa: E402

PARSE, CONVERT, FLOAT_FROM_DOUBLE, INT_FROM_LONG, NEW_POINTER = 0x74E310, 0x5BAE00, 0x74FA70, 0x74C6E0, 0x5BB0E0
HANDLES = {"float": 0xF1000000, "int": 0x1A000000, "pointer": 0xB1000000}
WSTATE = {"values": (), "object": 0, "double": None, "integer": None, "pointer": None}


def wrapper_stubs():
    """Stubs for the Python interpreter and the SWIG helpers that the wrappers call."""
    def parse(ctx):
        fmt = bytearray()
        address = ctx.arg(1)
        while True:
            byte = ctx.read(address + len(fmt), 1)
            if byte in (b"\0", b":"):
                break
            fmt += byte
        WSTATE["object"] = ctx.arg(0)
        values = list(WSTATE["values"])
        for index, letter in enumerate(bytes(fmt)):
            out = ctx.arg(2 + index)
            if letter == ord("O"):
                ctx.write(out, struct.pack("<I", 0x1000))
            elif letter == ord("f"):
                ctx.write(out, T.encode(F32, values.pop(0)))
            elif letter == ord("i"):
                ctx.write(out, T.encode(I32, values.pop(0)))
            else:
                raise AssertionError("unexpected format character %r" % chr(letter))
        ctx.returns(1)

    def convert(ctx):
        ctx.write(ctx.arg(1), struct.pack("<I", WSTATE["object"]))
        ctx.returns(0)

    def to_double(ctx):
        WSTATE["double"] = struct.pack("<II", ctx.stack_arg(0), ctx.stack_arg(1))
        ctx.returns(HANDLES["float"])

    def to_int(ctx):
        WSTATE["integer"] = struct.unpack("<i", struct.pack("<I", ctx.stack_arg(0)))[0]
        ctx.returns(HANDLES["int"])

    def new_pointer(ctx):
        WSTATE["pointer"] = ctx.stack_arg(0)
        ctx.returns(HANDLES["pointer"])

    stubs = property_stubs()
    stubs.update({PARSE: Stub(parse, nargs=0), CONVERT: Stub(convert, nargs=4), FLOAT_FROM_DOUBLE: Stub(to_double, nargs=2),
                  INT_FROM_LONG: Stub(to_int, nargs=1), NEW_POINTER: Stub(new_pointer, nargs=2)})
    return stubs


def wrapper_setup(values=()):
    def reset(machine, lib):
        WSTATE.update(values=tuple(values), object=0, double=None, integer=None, pointer=None)
    return state_setup(extra=reset)


def double_probe():
    """The float that the wrapper hands to PyFloat_FromDouble, as 8 bytes."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return WSTATE["double"]
        side.lib.bc_captured_double.restype = ctypes.c_double
        return struct.pack("<d", side.lib.bc_captured_double())
    return Probe("float handed to Python", read)


def integer_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return WSTATE["integer"]
        side.lib.bc_captured_integer.restype = ctypes.c_int64
        return side.lib.bc_captured_integer()
    return Probe("integer handed to Python", read)


def pointer_probe():
    """The 12 bytes of the point that GetPosition hands to Python."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return bytes(side.read(WSTATE["pointer"], 12))
        side.lib.bc_captured_pointer.restype = ctypes.c_void_p
        return bytes(side.read(side.lib.bc_captured_pointer(), 12))
    return Probe("point handed to Python", read)
