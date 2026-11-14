"""Native side: call a C-ABI function in a shared library through ctypes."""
import ctypes
import os
import subprocess
import sys
from dataclasses import dataclass
from typing import Dict, Sequence

from . import types as T

EMU_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(EMU_DIR, "build")
FIXTURE_DIR = os.path.join(EMU_DIR, "fixtures")


def build_fixture(source: str, extra_flags: Sequence[str] = ()) -> str:
    """Compile tests/emu/fixtures/<source> (.c or .cpp) into tests/emu/build/ and return the library path."""
    src = os.path.join(FIXTURE_DIR, source)
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    out = os.path.join(BUILD_DIR, os.path.splitext(source)[0] + suffix)
    os.makedirs(BUILD_DIR, exist_ok=True)
    newest = max(os.path.getmtime(os.path.join(FIXTURE_DIR, f)) for f in os.listdir(FIXTURE_DIR)
                 if f == source or f.endswith(".h"))
    if not os.path.exists(out) or os.path.getmtime(out) < newest:
        cplusplus = src.endswith(".cpp")
        cmd = ["clang++" if cplusplus else "clang", "-shared", "-fPIC", "-O1", "-ffp-contract=off",
               "-fno-fast-math", "-I", FIXTURE_DIR, "-o", out, src]
        if cplusplus:
            cmd += ["-std=c++17", "-Wno-invalid-offsetof"]
        cmd += list(extra_flags)
        subprocess.run(cmd, check=True)
    return out


@dataclass
class Native:
    """A function in a shared library. It takes the same logical arguments as the original."""
    lib: str
    symbol: str

    def bind(self, args: Sequence, ret) -> "BoundNative":
        return BoundNative(self, args, ret)


_libs: Dict[str, ctypes.CDLL] = {}


def load_library(path: str) -> ctypes.CDLL:
    if path not in _libs:
        _libs[path] = ctypes.CDLL(path)
    return _libs[path]


class BoundNative:
    def __init__(self, native: Native, args: Sequence, ret):
        self.fn = getattr(load_library(native.lib), native.symbol)
        self.args = args
        self.fn.argtypes = [self._ctype(a) for a in args]
        self.fn.restype = None if ret is None else ret.ctype

    @staticmethod
    def _ctype(arg):
        if isinstance(arg, T.Buf):
            return ctypes.c_void_p
        if isinstance(arg, T.St0):
            return arg.scalar.ctype
        return arg.ctype

    def call(self, values: Sequence):
        """Call the function. Return (return value, {arg index: buffer bytes})."""
        buffers, call_args = {}, []
        for index, (arg, value) in enumerate(zip(self.args, values)):
            if isinstance(arg, T.Buf):
                data = bytes(value)
                size = arg.size if arg.size is not None else len(data)
                body = bytes(arg.offset) + data.ljust(size, b"\0")[:size] + bytes(16)
                buf = ctypes.create_string_buffer(body, len(body))
                buffers[index] = (buf, arg.offset, size)
                call_args.append(ctypes.addressof(buf) + arg.offset)
            else:
                scalar = arg.scalar if isinstance(arg, T.St0) else arg
                call_args.append(T.native_value(scalar, value))
        result = self.fn(*call_args)
        return result, {i: buf.raw[off:off + size] for i, (buf, off, size) in buffers.items()}
