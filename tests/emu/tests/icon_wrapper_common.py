"""Shared parts of the tests of the Appc wrappers of the icon classes.

The Python interpreter is a set of stubs: the argument parser writes the objects of the argument tuple and the test
values, the pointer conversion gives the pointer back, and the result helpers keep what the wrapper hands to Python.
The native side runs a function with the same effect on the engine class.
"""
import ctypes
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import (ICON_ENVS, build_group, camera, icon_case, icon_setup, icon_stubs, manager_global,  # noqa: E402
                         registered_once, texture)
from emuharness import types as T  # noqa: E402

PARSE, CONVERT, FLOAT_FROM_DOUBLE, INT_FROM_LONG, STRING_FROM_STRING, NEW_POINTER = (
    0x74E310, 0x5BAE00, 0x74FA70, 0x74C6E0, 0x7506C0, 0x5BB0E0)
HANDLES = {"float": 0xF1000000, "int": 0x1A000000, "string": 0x5C000000, "pointer": 0xB1000000}
NONE = 0x95DD20
WSTATE = {"values": (), "double": None, "integer": None, "pointer": None, "text": None}


def wrapper_stubs():
    """Stubs for the interpreter and the SWIG helpers that the icon wrappers call."""
    def parse(ctx):
        arguments = ctx.arg(0)
        fmt = bytearray()
        address = ctx.arg(1)
        while True:
            byte = ctx.read(address + len(fmt), 1)
            if byte in (b"\0", b":"):
                break
            fmt += byte
        values = list(WSTATE["values"])
        objects = 0
        for index, letter in enumerate(bytes(fmt)):
            out = ctx.arg(2 + index)
            if letter == ord("O"):
                ctx.write(out, ctx.read(arguments + 4 * objects, 4))
                objects += 1
            elif letter == ord("f"):
                ctx.write(out, T.encode(F32, values.pop(0)))
            elif letter == ord("i"):
                ctx.write(out, T.encode(I32, values.pop(0)))
            else:
                raise AssertionError("unexpected format character %r" % chr(letter))
        ctx.returns(1)

    def convert(ctx):
        ctx.write(ctx.arg(1), struct.pack("<I", ctx.arg(0)))
        ctx.returns(0)

    def to_double(ctx):
        WSTATE["double"] = struct.pack("<II", ctx.stack_arg(0), ctx.stack_arg(1))
        ctx.returns(HANDLES["float"])

    def to_int(ctx):
        WSTATE["integer"] = struct.unpack("<i", struct.pack("<I", ctx.stack_arg(0)))[0]
        ctx.returns(HANDLES["int"])

    def to_string(ctx):
        data = bytearray()
        address = ctx.stack_arg(0)
        while True:
            byte = ctx.read(address + len(data), 1)
            if byte == b"\0":
                break
            data += byte
        WSTATE["text"] = bytes(data)
        ctx.returns(HANDLES["string"])

    def new_pointer(ctx):
        WSTATE["pointer"] = ctx.stack_arg(0)
        ctx.returns(HANDLES["pointer"])

    stubs = icon_stubs()
    stubs.update({PARSE: Stub(parse, nargs=0), CONVERT: Stub(convert, nargs=4), FLOAT_FROM_DOUBLE: Stub(to_double, nargs=2),
                  INT_FROM_LONG: Stub(to_int, nargs=1), STRING_FROM_STRING: Stub(to_string, nargs=1),
                  NEW_POINTER: Stub(new_pointer, nargs=2)})
    return stubs


def wrapper_setup(values=(), textures=None):
    def reset(machine, lib):
        WSTATE.update(values=tuple(values), double=None, integer=None, pointer=None, text=None)
        lib.bc_capture_reset()
        lib.bc_icon_captured_text_clear()
    return icon_setup(textures, "C:\\BC\\", 100, extra=reset)


def double_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return WSTATE["double"]
        side.lib.bc_captured_double.restype = ctypes.c_double
        value = side.lib.bc_captured_double()
        return struct.pack("<d", value)
    return Probe("float handed to Python", read)


def integer_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return WSTATE["integer"]
        side.lib.bc_captured_integer.restype = ctypes.c_int64
        return side.lib.bc_captured_integer()
    return Probe("integer handed to Python", read)


def text_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return WSTATE["text"]
        buffer = ctypes.create_string_buffer(300)
        side.lib.bc_icon_captured_text.restype = ctypes.c_int
        length = side.lib.bc_icon_captured_text(buffer, 300)
        return None if length < 0 else buffer.value
    return Probe("text handed to Python", read)


def pointer_probe(root=None):
    """The pointer that the wrapper hands to SWIG: for a camera pointer the camera that it holds."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            pointer = WSTATE["pointer"]
            if pointer is None or root is None:
                return pointer
            camera_address = struct.unpack("<I", side.read(pointer, 4))[0]
            return camera_address == addresses.get(root, 0)
        side.lib.bc_captured_pointer.restype = ctypes.c_void_p
        pointer = side.lib.bc_captured_pointer()
        if root is None:
            return pointer
        return (ctypes.c_void_p.from_address(pointer).value or 0) == addresses.get(root, 0)
    return Probe("pointer handed to Python", read)


def wrapper_case(env, first, last, symbol, scenarios, extra=(), probes=(), **kw):
    kw.setdefault("stubs", wrapper_stubs())
    kw.setdefault("probes", [log_probe(), trap_probe()] + list(probes))
    return icon_case(env, first, last, "cdecl", [U32, PTR] + list(extra), U32, symbol, scenarios, **kw)


def manager_arguments(manager, new_camera=None):
    return Obj("ManagerArgs", m=manager, c=new_camera)


def manager_scenario(values, label, new_camera=None, extra_roots=None, **members):
    manager = manager_global(**members)
    roots = {"a": manager_arguments(manager, new_camera)}
    roots.update(extra_roots or {})
    return Scenario(roots, [0, Root("a")] + list(values), globals=[manager], label=label, setup=wrapper_setup(values))


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


VALUES = [0.0, 1.0, -1.0, 640.0, f32(0.1), 3.0e38, 1.0e-40, -0.0, 1920.0, f32(1.0 / 3.0)]


def group_scenario(pre, label, group_name="G", textures=None):
    group = Obj("IconGroup")
    arguments = Obj("GroupArgs", g=group)
    return Scenario({"g": group, "a": arguments}, [0, Root("a")], globals=[manager_global(scale=1.0, width=640.0, height=480.0)],
                    pre=pre, label=label, setup=wrapper_setup((), {"C:\\BC\\Data/a.tga": (64, 32)}))


