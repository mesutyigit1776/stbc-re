"""Tests of the 32-bit long of the interpreter of stbc.exe.

The original side runs functions of the Python 1.5.2 in the exe (int and long number methods, the hash functions,
PyOS_strtol and PyOS_strtoul, the dictionary insert code) in the emulator. The exe allocator (0x718d60, 0x718dc0)
and the two functions that set an exception (0x752e80, 0x752e40) are stubs; all other code of the exe runs.
The native side is build/libbc_pylong_probe, the interpreter of this tree. It builds the same objects from the same
byte description and writes the result in that description. The tests compare the results exactly.
build/libbc_pylong_probe_wide is the same interpreter with the long of the host. The tests with a name that ends in
_wide_long must fail on it.

Objects are written as tuples: ('i', value) int, ('s', bytes) string, ('f', bits) float, ('l', digit count, digits)
long, ('x', real bits, imaginary bits) complex, ('n',) None, ('t', items) tuple, ('E', name, message) exception and
('T', name) a result that is no object of these kinds. The x87 control word is 0x027F: the interpreter uses plain
doubles, so the 24-bit precision of the renderer is not reproduced by the float tests (see re/obligations.tsv).
"""
import ast
import ctypes
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from emuharness import F64, I32, PTR, Machine, Original, Stub, convert, register  # noqa: E402
from emuharness.case import EXE_PATH, REPO_ROOT  # noqa: E402
from emuharness.errors import HarnessError, RunawayExecution  # noqa: E402
from emuharness.stubs import free_stub, malloc_stub  # noqa: E402

PYTHON_RANGE = (0x748000, 0x4D000)
CRT_RANGE = (0x850000, 0x30000)
MALLOC, FREE = 0x718D60, 0x718DC0
REALLOC_A, REALLOC_B = 0x718D80, 0x718DA0
THREAD_STATE_POINTER = 0x99F914  # _PyThreadState_Current (read by 0x752db0 PyErr_Restore and 0x77c130)
INT_TYPE, LONG_TYPE, FLOAT_TYPE = 0x95DF00, 0x96F500, 0x95E798
STRING_TYPE, TUPLE_TYPE, COMPLEX_TYPE = 0x95E960, 0x95E200, 0x974F50
LIST_TYPE = 0x95D4D8
BUILTIN_TABLE = 0x9614A0  # the methods of the module __builtin__: name, function, flags, doc
NONE = 0x95DD20
EXCEPTION_TABLE = 0x9617E0
ERRNO = 0x85CBD7
ERRNO_SLOT = 0x30003000
THREAD_DATA = 0x30004000
GET_THREAD_DATA = 0x8613E1
CFLTCVT_INIT = 0x859971
BUILD_DIR = os.path.join(REPO_ROOT, "build")
SUFFIX = ".dylib" if sys.platform == "darwin" else ".so"
CW = 0x027F

# TYPE.SLOT -> (address, argument count, result kind). Kind "obj": the function returns an object, "int": a C int.
SLOTS = {}
for _name, _addr, _n in (
        ("nb_add", 0x74C8C0, 2), ("nb_subtract", 0x74C920, 2), ("nb_multiply", 0x74C960, 2),
        ("nb_divide", 0x74CA50, 2), ("nb_remainder", 0x74CAF0, 2), ("nb_divmod", 0x74CB30, 2),
        ("nb_power", 0x74CB70, 3), ("nb_negative", 0x74CCF0, 1), ("nb_positive", 0x74CD20, 1),
        ("nb_absolute", 0x74CD30, 1), ("nb_invert", 0x74CD60, 1), ("nb_lshift", 0x74CD80, 2),
        ("nb_rshift", 0x74CDD0, 2), ("nb_and", 0x74CE40, 2), ("nb_xor", 0x74CE60, 2), ("nb_or", 0x74CE80, 2),
        ("nb_int", 0x74CEA0, 1), ("nb_long", 0x74CEB0, 1), ("nb_float", 0x74CED0, 1), ("nb_oct", 0x74CEF0, 1),
        ("nb_hex", 0x74CF40, 1), ("tp_repr", 0x74C850, 1)):
    SLOTS["int." + _name] = (_addr, _n, "obj")
SLOTS["int.nb_nonzero"] = (0x74CD50, 1, "int")
SLOTS["int.tp_hash"] = (0x74C8A0, 1, "int")
SLOTS["int.tp_compare"] = (0x74C880, 2, "int")
for _name, _addr, _n in (
        ("nb_add", 0x7788C0, 2), ("nb_subtract", 0x778BB0, 2), ("nb_multiply", 0x778C00, 2),
        ("nb_divide", 0x778DB0, 2), ("nb_remainder", 0x7794E0, 2), ("nb_divmod", 0x779530, 2),
        ("nb_power", 0x7795D0, 3), ("nb_negative", 0x779870, 1), ("nb_positive", 0x779860, 1),
        ("nb_absolute", 0x7798D0, 1), ("nb_invert", 0x779810, 1), ("nb_lshift", 0x779AD0, 2),
        ("nb_rshift", 0x779910, 2), ("nb_and", 0x779C10, 2), ("nb_xor", 0x779E80, 2), ("nb_or", 0x779EA0, 2),
        ("nb_int", 0x779F00, 1), ("nb_long", 0x779F30, 1), ("nb_float", 0x779F40, 1), ("nb_oct", 0x779F70, 1),
        ("nb_hex", 0x779F80, 1), ("tp_repr", 0x778420, 1)):
    SLOTS["long." + _name] = (_addr, _n, "obj")
SLOTS["long.nb_nonzero"] = (0x7798F0, 1, "int")
SLOTS["long.tp_hash"] = (0x778870, 1, "int")
SLOTS["long.tp_compare"] = (0x7787E0, 2, "int")
SLOTS["float.nb_int"] = (0x7503A0, 1, "obj")
SLOTS["float.nb_long"] = (0x750440, 1, "obj")
SLOTS["float.tp_hash"] = (0x74FCC0, 1, "int")
SLOTS["string.tp_hash"] = (0x750C40, 1, "int")
SLOTS["tuple.tp_hash"] = (0x74DE60, 1, "int")
SLOTS["complex.tp_hash"] = (0x784990, 1, "int")

# Functions of the object API: name -> (address, number of arguments, kind of the result).
API = {
    "PyInt_AsLong": (0x74C7C0, 1, "long_error"),
    "PyLong_AsLong": (0x777CB0, 1, "long_error"),
    "PyLong_AsUnsignedLong": (0x777D40, 1, "ulong_error"),
    "PyLong_FromLong": (0x777AD0, 1, "from_int"),
    "PyLong_FromUnsignedLong": (0x777B60, 1, "from_int"),
    "PyLong_FromDouble": (0x777BA0, 1, "from_double"),
    "PyLong_AsDouble": (0x777DD0, 1, "double_error"),
    "PyObject_Hash": (0x74C100, 1, "long_error"),
    "PyNumber_Int": (0x759F90, 1, "object"),
    "PyNumber_Long": (0x75A160, 1, "object"),
    "PyString_Format": (0x750E60, 2, "object"),
    "PyLong_FromString": (0x778190, 2, "from_string"),
}
PYOS_STRTOL, PYOS_STRTOUL = 0x787190, 0x786FC0
CRT_STRTOL = 0x85C337
PYDICT_NEW, PYDICT_SETITEM, PYDICT_DELITEM = 0x751CA0, 0x751E60, 0x7520C0

INT_MIN, INT_MAX = -2 ** 31, 2 ** 31 - 1


# Descriptions of objects ------------------------------------------------------------------------------------------

def double_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def bits_double(b):
    return struct.unpack("<d", struct.pack("<Q", b))[0]


def i(v):
    return ("i", v)


def s(data):
    return ("s", data)


def f(x):
    return ("f", double_bits(x))


def long_spec(value):
    digits = []
    magnitude = abs(value)
    while magnitude:
        digits.append(magnitude & 0x7FFF)
        magnitude >>= 15
    return ("l", -len(digits) if value < 0 else len(digits), tuple(digits))


def raw_long(count, digits):
    return ("l", count, tuple(digits))


NONE_SPEC = ("n",)


def tup(*items):
    return ("t", tuple(items))


def encode(spec):
    kind = spec[0]
    if kind == "i":
        return b"i" + struct.pack("<i", spec[1])
    if kind == "s":
        return b"s" + struct.pack("<I", len(spec[1])) + spec[1]
    if kind == "f":
        return b"f" + struct.pack("<Q", spec[1])
    if kind == "x":
        return b"x" + struct.pack("<QQ", spec[1], spec[2])
    if kind == "l":
        return b"l" + struct.pack("<i", spec[1]) + b"".join(struct.pack("<H", d) for d in spec[2])
    if kind == "n":
        return b"n"
    if kind == "t":
        return b"t" + struct.pack("<I", len(spec[1])) + b"".join(encode(item) for item in spec[1])
    raise ValueError(kind)


def decode(data, pos=0):
    """Read one object description. Returns (spec, next position)."""
    kind = chr(data[pos])
    pos += 1
    if kind == "i":
        return ("i", struct.unpack_from("<i", data, pos)[0]), pos + 4
    if kind == "q":
        return ("q", struct.unpack_from("<q", data, pos)[0]), pos + 8
    if kind in "sET":
        parts = []
        for _ in range(2 if kind == "E" else 1):
            n = struct.unpack_from("<I", data, pos)[0]
            parts.append(bytes(data[pos + 4:pos + 4 + n]))
            pos += 4 + n
        if kind == "s":
            return ("s", parts[0]), pos
        return (kind,) + tuple(p.decode("latin-1") for p in parts), pos
    if kind == "f":
        return ("f", struct.unpack_from("<Q", data, pos)[0]), pos + 8
    if kind == "l":
        n = struct.unpack_from("<i", data, pos)[0]
        digits = struct.unpack_from("<%dH" % abs(n), data, pos + 4)
        return ("l", n, tuple(digits)), pos + 4 + 2 * abs(n)
    if kind == "n":
        return ("n",), pos
    if kind in "tL":
        n = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        items = []
        for _ in range(n):
            item, pos = decode(data, pos)
            items.append(item)
        return (kind, tuple(items)), pos
    raise ValueError(kind)


# The original side ------------------------------------------------------------------------------------------------

def _realloc(ctx):
    """realloc(block, size): a new tracked block with the old bytes."""
    pointer, size = ctx.arg(0), ctx.arg(1)
    new = ctx.heap.new_block(size)
    old = ctx.heap.blocks.get(pointer)
    if old is not None:
        ctx.write(new, ctx.read(pointer, min(old.size, size)))
        ctx.heap.release(pointer)
    ctx.returns(new)


class Exe:
    """The interpreter functions of the exe in one emulator."""

    def __init__(self):
        self.machine = Machine(EXE_PATH, stubs={
            MALLOC: malloc_stub(), FREE: free_stub(),
            REALLOC_A: Stub(_realloc, nargs=2), REALLOC_B: Stub(_realloc, nargs=2),
            GET_THREAD_DATA: Stub(lambda ctx: ctx.returns(THREAD_DATA)),
        })
        self.thread_state = 0

    @property
    def error(self):
        """The exception that the interpreter code of the exe has set: (name, message), or None."""
        m = self.machine
        kind, value = m.read_u32(self.thread_state + 0x20), m.read_u32(self.thread_state + 0x24)
        if kind == 0:
            return None
        message = ""
        if value != 0 and m.read_u32(value + 4) == STRING_TYPE:
            message = m.read(value + 0x14, m.read_u32(value + 8)).decode("latin-1")
        return self._exception_name(kind), message

    @error.setter
    def error(self, _value):
        self.machine.write(self.thread_state + 0x20, bytes(12))

    def _exception_name(self, pointer):
        if pointer == 0:
            return "0"
        size = struct.unpack("<I", self.machine.read(pointer + 8, 4))[0]
        return self.machine.read(pointer + 0x14, size).decode("latin-1")

    def reset(self):
        """Restore the data of the exe and empty the heap, then give each exception variable its name string."""
        m = self.machine
        m.reset_state()
        m.write(ERRNO_SLOT, bytes(4))
        self.thread_state = m.heap.alloc(0x100)
        m.write(self.thread_state, bytes(0x100))
        m.write(THREAD_STATE_POINTER, struct.pack("<I", self.thread_state))
        for index in range(26):
            entry = EXCEPTION_TABLE + 12 * index
            name = m.read_u32(entry)
            variable = m.read_u32(entry + 4)
            text = bytearray()
            while m.read(name + len(text), 1) != b"\0":
                text += m.read(name + len(text), 1)
            m.write(variable, struct.pack("<I", self.make_string(bytes(text))))
        m.call(Original(CFLTCVT_INIT, 0x38, "cdecl", [], None, allow=[(CFLTCVT_INIT, 0x38)]), [], cw=CW, fresh=False)

    # Objects in guest memory.
    def _alloc(self, data):
        address = self.machine.heap.alloc(len(data), 8)
        self.machine.write(address, data)
        return address

    def make_string(self, data):
        return self._alloc(struct.pack("<IIIII", 1, STRING_TYPE, len(data), 0xFFFFFFFF, 0) + data + b"\0")

    def build(self, spec):
        kind = spec[0]
        if kind == "i":
            return self._alloc(struct.pack("<IIi", 1, INT_TYPE, spec[1]))
        if kind == "s":
            return self.make_string(spec[1])
        if kind == "f":
            return self._alloc(struct.pack("<IIQ", 1, FLOAT_TYPE, spec[1]))
        if kind == "x":
            return self._alloc(struct.pack("<IIQQ", 1, COMPLEX_TYPE, spec[1], spec[2]))
        if kind == "l":
            return self._alloc(struct.pack("<IIi", 1, LONG_TYPE, spec[1])
                               + b"".join(struct.pack("<H", d) for d in spec[2]) + bytes(2))
        if kind == "n":
            return NONE
        if kind == "t":
            items = [self.build(item) for item in spec[1]]
            return self._alloc(struct.pack("<III", 1, TUPLE_TYPE, len(items)) + b"".join(struct.pack("<I", p) for p in items))
        raise ValueError(kind)

    def read_object(self, pointer):
        m = self.machine
        if pointer == 0:
            if self.error is not None:
                return ("E", self.error[0], self.error[1])
            return ("T", "NULL")
        if pointer == NONE:
            return ("n",)
        kind = m.read_u32(pointer + 4)
        if kind == INT_TYPE:
            return ("i", struct.unpack("<i", m.read(pointer + 8, 4))[0])
        if kind == LONG_TYPE:
            n = struct.unpack("<i", m.read(pointer + 8, 4))[0]
            return ("l", n, struct.unpack("<%dH" % abs(n), m.read(pointer + 12, 2 * abs(n))))
        if kind == FLOAT_TYPE:
            return ("f", struct.unpack("<Q", m.read(pointer + 8, 8))[0])
        if kind == STRING_TYPE:
            return ("s", m.read(pointer + 0x14, m.read_u32(pointer + 8)))
        if kind == TUPLE_TYPE:
            n = m.read_u32(pointer + 8)
            return ("t", tuple(self.read_object(m.read_u32(pointer + 12 + 4 * j)) for j in range(n)))
        if kind == LIST_TYPE:
            n, items = m.read_u32(pointer + 8), m.read_u32(pointer + 12)
            return ("L", tuple(self.read_object(m.read_u32(items + 4 * j)) for j in range(n)))
        return ("T", "type 0x%x" % kind)

    def call(self, address, args, returns_object=True):
        """Call a cdecl function whose arguments are pointers or ints. Returns the EAX value."""
        original = Original(address, 1, "cdecl", [PTR] * len(args), I32, allow=[PYTHON_RANGE, CRT_RANGE])
        self.error = None
        out = self.machine.call(original, args, cw=CW, fresh=False)
        return out.eax

    def _stopped_at_idiv(self):
        """Unicorn does not deliver the divide error of idiv. It stops with the instruction pointer at the idiv."""
        eip = self.machine.read_reg("eip")
        code = self.machine.read(eip, 2)
        return code[0] == 0xF7 and (code[1] >> 3) & 7 == 7

    def slot(self, name, specs):
        address, nargs, kind = SLOTS[name]
        pointers = [self.build(spec) for spec in specs]
        try:
            eax = self.call(address, pointers)
        except (RunawayExecution, HarnessError):
            if self._stopped_at_idiv():
                return ("T", "trap")
            raise
        if kind == "int":
            return ("i", struct.unpack("<i", struct.pack("<I", eax))[0])
        return self.read_object(eax)

    def api(self, name, specs):
        if name.startswith("builtin:"):
            return self.builtin(name[8:], specs[0])
        address, _nargs, kind = API[name]
        if kind == "from_int":
            value = specs[0][1] & 0xFFFFFFFF if name == "PyLong_FromUnsignedLong" else specs[0][1]
            self.error = None
            original = Original(address, 1, "cdecl", [I32], I32, allow=[PYTHON_RANGE, CRT_RANGE])
            return self.read_object(self.machine.call(original, [value], cw=CW, fresh=False).eax)
        if kind == "from_double":
            self.error = None
            original = Original(address, 1, "cdecl", [F64], I32, allow=[PYTHON_RANGE, CRT_RANGE])
            return self.read_object(self.machine.call(original, [bits_double(specs[0][1])], cw=CW, fresh=False).eax)
        if kind == "from_string":
            text = self.build(specs[0])
            end = self.machine.heap.alloc(16)
            original = Original(address, 1, "cdecl", [PTR, PTR, I32], I32, allow=[PYTHON_RANGE, CRT_RANGE])
            self.error = None
            number = self.machine.call(original, [text + 0x14, end, specs[1][1]], cw=CW, fresh=False).eax
            result = self.read_object(number)
            if result[0] == "E" or number == 0:
                return result
            return ("t", (result, ("i", self.machine.read_u32(end) - (text + 0x14))))
        pointers = [self.build(spec) for spec in specs]
        if kind == "object":
            return self.read_object(self.call(address, pointers))
        if kind == "double_error":
            return self.call_double(address, pointers)
        eax = self.call(address, pointers)
        value = struct.unpack("<i", struct.pack("<I", eax))[0]
        if self.error is not None and eax == 0xFFFFFFFF:
            return ("E", self.error[0], self.error[1])
        return ("i", value)

    def call_double(self, address, pointers):
        original = Original(address, 1, "cdecl", [PTR] * len(pointers), F64, allow=[PYTHON_RANGE, CRT_RANGE])
        self.error = None
        out = self.machine.call(original, pointers, cw=CW, fresh=False)
        mantissa, sign_exp = struct.unpack("<QH", out.st0_raw)
        if mantissa == 0 and sign_exp & 0x7FFF == 0:
            bits = (sign_exp >> 15) << 63
        else:
            bits = convert.float80_to_bits(out.st0_raw, 52, 11)
        if self.error is not None and bits_double(bits) == -1.0:
            return ("E", self.error[0], self.error[1])
        return ("f", bits)

    def builtin(self, name, args):
        """Call the C function of a built-in function with the tuple args."""
        m = self.machine
        entry = BUILTIN_TABLE
        while m.read_u32(entry) != 0:
            pointer = m.read_u32(entry)
            if m.read(pointer, len(name) + 1) == name.encode() + b"\0":
                function = m.read_u32(entry + 4)
                break
            entry += 16
        else:
            raise KeyError(name)
        self.error = None
        pointers = [0, self.build(args)]
        return self.read_object(self.call(function, pointers))

    def strtol(self, text, base, unsigned, function=None):
        m = self.machine
        address = self._alloc(text + b"\0")
        end = self.machine.heap.alloc(16)
        m.write(ERRNO_SLOT, bytes(4))
        original = Original(function or (PYOS_STRTOUL if unsigned else PYOS_STRTOL), 1, "cdecl", [PTR, PTR, I32], I32,
                            allow=[PYTHON_RANGE, CRT_RANGE])
        stub = Stub(lambda ctx: ctx.returns(ERRNO_SLOT))
        if ERRNO not in m.stub_at:
            m.add_stub(ERRNO, stub)
        out = m.call(original, [address, end, base], cw=CW, fresh=False)
        offset = m.read_u32(end) - address
        error = m.read_u32(ERRNO_SLOT)
        return out.eax, offset, error

    def key_order(self, ops):
        """Run the operations on a new dictionary. Returns the keys in table order, which is the order of keys()."""
        m = self.machine
        d = self.call(PYDICT_NEW, [])
        for op, spec in ops:
            key = self.build(spec)
            if op == "S":
                self.call(PYDICT_SETITEM, [d, key, self.build(("i", 0))])
            else:
                self.call(PYDICT_DELITEM, [d, key])
        _fill, _used, size, _poly, table = struct.unpack("<IIIII", m.read(d + 8, 20))
        keys = []
        for slot in range(size):
            _hash, key, value = struct.unpack("<iII", m.read(table + 12 * slot, 12))
            if key != 0 and value != 0:
                keys.append(self.read_object(key))
        return keys

    def dict_order(self, ops):
        """Run the operations on a new dictionary. Returns the same list as bc_py_probe_dict."""
        m = self.machine
        d = self.call(PYDICT_NEW, [])
        errors = 0
        for number, (op, spec) in enumerate(ops):
            key = self.build(spec)
            if op == "S":
                value = self.build(("i", number))
                if struct.unpack("<i", struct.pack("<I", self.call(PYDICT_SETITEM, [d, key, value])))[0] != 0:
                    errors += 1
            else:
                if struct.unpack("<i", struct.pack("<I", self.call(PYDICT_DELITEM, [d, key])))[0] != 0:
                    errors += 1
        fill, used, size, _poly, table = struct.unpack("<IIIII", m.read(d + 8, 20))
        out = [size, used, fill, errors]
        for slot in range(size):
            entry_hash, key, value = struct.unpack("<iII", m.read(table + 12 * slot, 12))
            key_state = -1 if key == 0 else (-2 if value == 0 else 1)
            out += [entry_hash, key_state, -1 if value == 0 else struct.unpack("<i", m.read(value + 8, 4))[0]]
        return out


# The native side --------------------------------------------------------------------------------------------------

def build_probe(wide):
    target = "bc_pylong_probe_wide" if wide else "bc_pylong_probe"
    path = os.path.join(BUILD_DIR, "lib" + target + SUFFIX)
    sources = [os.path.join(REPO_ROOT, "src", "pyhost", "long32_probe.c"), os.path.join(REPO_ROOT, "CMakeLists.txt")]
    for root, _dirs, files in os.walk(os.path.join(REPO_ROOT, "third_party", "Python-1.5.2")):
        sources += [os.path.join(root, name) for name in files if name.endswith((".c", ".h"))]
    if not os.path.exists(path) or os.path.getmtime(path) < max(os.path.getmtime(p) for p in sources):
        subprocess.run(["cmake", "-S", REPO_ROOT, "-B", BUILD_DIR], check=True, capture_output=True)
        subprocess.run(["cmake", "--build", BUILD_DIR, "--target", target], check=True, capture_output=True)
    return path


class Probe:
    def __init__(self, wide=False):
        self.lib = ctypes.CDLL(build_probe(wide))
        self.lib.bc_py_probe_slot.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
                                              ctypes.c_int]
        self.lib.bc_py_probe_api.argtypes = list(self.lib.bc_py_probe_slot.argtypes)
        self.lib.bc_py_probe_strtol.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                                ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_int),
                                                ctypes.POINTER(ctypes.c_int)]
        self.lib.bc_py_probe_crt_strtol.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int),
                                                    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        self.lib.bc_py_probe_dict.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int),
                                              ctypes.c_int]
        self.out = ctypes.create_string_buffer(1 << 20)

    def _result(self, size):
        assert size <= len(self.out), "result too large"
        return decode(self.out.raw[:size])[0]

    def _stopped_at_idiv(self):
        """Unicorn does not deliver the divide error of idiv. It stops with the instruction pointer at the idiv."""
        eip = self.machine.read_reg("eip")
        code = self.machine.read(eip, 2)
        return code[0] == 0xF7 and (code[1] >> 3) & 7 == 7

    def slot(self, name, specs):
        data = b"".join(encode(spec) for spec in specs)
        size = self.lib.bc_py_probe_slot(name.encode(), len(specs), data, self.out, len(self.out))
        result = self._result(size)
        if SLOTS[name][2] == "int" and result[0] == "i":
            return result
        return result

    def api(self, name, specs):
        data = b"".join(encode(spec) for spec in specs)
        size = self.lib.bc_py_probe_api(name.encode(), len(specs), data, self.out, len(self.out))
        return self._result(size)

    def strtol(self, text, base, unsigned):
        value, end, error = ctypes.c_uint(), ctypes.c_int(), ctypes.c_int()
        self.lib.bc_py_probe_strtol(text, base, 1 if unsigned else 0, ctypes.byref(value), ctypes.byref(end),
                                    ctypes.byref(error))
        return value.value, end.value, error.value

    def crt_strtol(self, text, base):
        value, end, error = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        self.lib.bc_py_probe_crt_strtol(text, base, ctypes.byref(value), ctypes.byref(end), ctypes.byref(error))
        return value.value & 0xFFFFFFFF, end.value, error.value

    def key_order(self, ops):
        """Run the operations on a new dictionary. Returns the keys in table order, which is the order of keys()."""
        m = self.machine
        d = self.call(PYDICT_NEW, [])
        for op, spec in ops:
            key = self.build(spec)
            if op == "S":
                self.call(PYDICT_SETITEM, [d, key, self.build(("i", 0))])
            else:
                self.call(PYDICT_DELITEM, [d, key])
        _fill, _used, size, _poly, table = struct.unpack("<IIIII", m.read(d + 8, 20))
        keys = []
        for slot in range(size):
            _hash, key, value = struct.unpack("<iII", m.read(table + 12 * slot, 12))
            if key != 0 and value != 0:
                keys.append(self.read_object(key))
        return keys

    def dict_order(self, ops):
        data = b"".join(op.encode() + encode(spec) for op, spec in ops)
        out = (ctypes.c_int * 70000)()
        n = self.lib.bc_py_probe_dict(data, len(ops), out, 70000)
        assert n > 0, "dictionary too large"
        return list(out[:n])


# Inputs -----------------------------------------------------------------------------------------------------------

EDGE = [0, 1, -1, 2, -2, 3, -3, 7, 10, 31, 32, 33, 100, 127, 128, 255, 256, 1000, 32767, 32768, 46340, 46341, 65535,
        65536, 65537, -65535, -65536, -65537, 0x7FFF, 0x10000, 0x7FFE0001, 0x3FFFFFFF, 0x40000000, 0x40000001,
        0x55555555, 0x2AAAAAAA, 0x7FFFFFFE, 0x7FFFFFFF, -0x7FFFFFFF, -0x7FFFFFFE, -0x40000000, -0x40000001,
        -0x3FFFFFFF, -0x2AAAAAAB, INT_MIN, INT_MIN + 1, 12345678, -12345678, 1000000007, -1000000007]


def to_int32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def random_ints(rnd, count):
    values = []
    for _ in range(count):
        mode = rnd.randrange(6)
        if mode == 0:
            v = rnd.randrange(INT_MIN, INT_MAX + 1)
        elif mode == 1:
            v = rnd.randrange(-100, 101)
        elif mode == 2:
            v = to_int32((1 << rnd.randrange(0, 32)) + rnd.randrange(-2, 3))
        elif mode == 3:
            v = rnd.randrange(-65536, 65537)
        elif mode == 4:
            v = rnd.randrange(-3000000, 3000001)
        else:
            v = to_int32(-(1 << rnd.randrange(0, 32)) + rnd.randrange(-2, 3))
        values.append(v)
    return values


def int_pairs(seed, random_count):
    rnd = random.Random(seed)
    pairs = [(a, b) for a in EDGE for b in EDGE]
    values = random_ints(rnd, 2 * random_count)
    pairs += list(zip(values[:random_count], values[random_count:]))
    return pairs


def shift_pairs(seed, random_count):
    rnd = random.Random(seed)
    counts = list(range(-2, 40)) + [255, 0x7FFFFFFF, INT_MIN]
    pairs = [(a, c) for a in EDGE for c in counts]
    pairs += [(a, rnd.randrange(0, 36)) for a in random_ints(rnd, random_count)]
    return pairs


class Comparison:
    def __init__(self, limit=6):
        self.total = 0
        self.bad = 0
        self.shown = []
        self.limit = limit

    def check(self, label, exe, native):
        self.total += 1
        if exe != native:
            self.bad += 1
            if len(self.shown) < self.limit:
                self.shown.append("%s: exe %r, native %r" % (label, exe, native))

    def finish(self, name):
        assert self.bad == 0, "%s: %d of %d differ. %s" % (name, self.bad, self.total, "; ".join(self.shown))
        print("    %s: %d comparisons equal" % (name, self.total))


def run_slots(wide, cases):
    """cases: list of (slot name, [specs]). Compares the exe and the probe."""
    exe, probe, result = Exe(), Probe(wide), Comparison()
    for index, (name, specs) in enumerate(cases):
        if index % 64 == 0:
            exe.reset()
        result.check("%s %r" % (name, specs), exe.slot(name, specs), probe.slot(name, specs))
    return result


def binary_cases(pair_count=1200):
    cases = []
    for op in ("nb_add", "nb_subtract", "nb_multiply", "nb_divide", "nb_remainder", "nb_divmod", "nb_and", "nb_xor",
               "nb_or"):
        for a, b in int_pairs(sum(op.encode()), pair_count):
            cases.append(("int." + op, [i(a), i(b)]))
    for op in ("nb_lshift", "nb_rshift"):
        for a, c in shift_pairs(len(op), pair_count):
            cases.append(("int." + op, [i(a), i(c)]))
    return cases


def divide_trap_free(cases):
    """The exe traps on -2**31 divided by -1 (B0019). The probe has a hook for that case, so no case is left out."""
    return cases


@register("py_long.int_binary_matches_exe")
def int_binary():
    run_slots(False, binary_cases()).finish("int binary operations")


@register("py_long.int_binary_wide_long", expect_fail=True)
def int_binary_wide():
    run_slots(True, binary_cases(300)).finish("int binary operations, long of the host")


def pow_cases(count=3000):
    rnd = random.Random(77)
    cases = []
    small = [0, 1, -1, 2, -2, 3, -3, 5, 10, 46340, 46341, -46341, 65536, 0x7FFFFFFF, INT_MIN, 1000, -1000]
    exponents = [0, 1, 2, 3, 4, 5, 8, 15, 16, 17, 30, 31, 32, 33, 62, 63, 64, 100, 0x7FFFFFFF, -1, INT_MIN]
    for a in small:
        for e in exponents:
            cases.append(("int.nb_power", [i(a), i(e), NONE_SPEC]))
    moduli = [1, -1, 2, -2, 3, 7, -7, 1000, 65536, 0x7FFFFFFF, INT_MIN + 1, 0]
    for a in small[:12]:
        for e in exponents[:12]:
            for z in moduli:
                cases.append(("int.nb_power", [i(a), i(e), i(z)]))
    for _ in range(count):
        a = random_ints(rnd, 1)[0]
        e = rnd.randrange(0, 70) if rnd.random() < 0.8 else rnd.randrange(0, 2 ** 31)
        z = random_ints(rnd, 1)[0] if rnd.random() < 0.5 else None
        cases.append(("int.nb_power", [i(a), i(e), i(z) if z is not None else NONE_SPEC]))
    return cases


@register("py_long.int_power_matches_exe")
def int_power():
    run_slots(False, pow_cases()).finish("int power")


@register("py_long.int_power_wide_long", expect_fail=True)
def int_power_wide():
    run_slots(True, pow_cases(100)).finish("int power, long of the host")


def unary_cases(count=1500):
    rnd = random.Random(5)
    values = EDGE + random_ints(rnd, count)
    cases = []
    for op in ("nb_negative", "nb_positive", "nb_absolute", "nb_nonzero", "nb_invert", "nb_int", "nb_long", "nb_float",
               "nb_oct", "nb_hex", "tp_repr", "tp_hash"):
        for v in values:
            cases.append(("int." + op, [i(v)]))
    for a, b in int_pairs(3, 600):
        cases.append(("int.tp_compare", [i(a), i(b)]))
    return cases


@register("py_long.int_unary_matches_exe")
def int_unary():
    run_slots(False, unary_cases()).finish("int unary operations, text and compare")


@register("py_long.int_unary_wide_long", expect_fail=True)
def int_unary_wide():
    run_slots(True, unary_cases(100)).finish("int unary operations, long of the host")


def random_long(rnd):
    digits = rnd.randrange(0, 9)
    value = 0
    for _ in range(digits):
        value = (value << 15) | rnd.randrange(0, 0x8000)
    if rnd.random() < 0.15:
        value = (1 << (15 * rnd.randrange(1, 5))) - rnd.randrange(0, 3)
    return long_spec(-value if rnd.random() < 0.5 else value)


def long_cases(count=300):
    rnd = random.Random(9)
    fixed = [0, 1, -1, 2 ** 31 - 1, 2 ** 31, -2 ** 31, -2 ** 31 - 1, 2 ** 32 - 1, 2 ** 32, 2 ** 45, -2 ** 45,
             2 ** 15, 2 ** 30, 2 ** 31 + 1, 2 ** 62, 10 ** 20, -10 ** 20, 0x7FFF, 0x8000]
    values = [long_spec(v) for v in fixed] + [random_long(rnd) for _ in range(count)]
    cases = []
    for op in ("nb_negative", "nb_positive", "nb_absolute", "nb_nonzero", "nb_invert", "nb_int", "nb_long", "nb_float",
               "nb_oct", "nb_hex", "tp_repr", "tp_hash"):
        for v in values:
            cases.append(("long." + op, [v]))
    for op in ("nb_add", "nb_subtract", "nb_multiply", "nb_divide", "nb_remainder", "nb_divmod", "nb_and", "nb_xor",
               "nb_or"):
        for _ in range(count):
            a, b = rnd.choice(values), rnd.choice(values)
            cases.append(("long." + op, [a, b]))
    for _ in range(count):
        cases.append(("long.nb_lshift", [rnd.choice(values), long_spec(rnd.choice([0, 1, 5, 14, 15, 16, 31, 32, 100]))]))
        cases.append(("long.nb_rshift", [rnd.choice(values), long_spec(rnd.choice([0, 1, 5, 14, 15, 16, 31, 32, 100]))]))
        cases.append(("long.tp_compare", [rnd.choice(values), rnd.choice(values)]))
    for _ in range(60):
        cases.append(("long.nb_power", [long_spec(rnd.randrange(-9, 10)), long_spec(rnd.randrange(0, 12)), NONE_SPEC]))
    return cases


@register("py_long.long_matches_exe")
def long_ops():
    run_slots(False, long_cases()).finish("long operations")


@register("py_long.long_wide_long", expect_fail=True)
def long_wide():
    run_slots(True, long_cases(40)).finish("long operations, long of the host")


# Conversions between the number types -----------------------------------------------------------------------------

def float_values():
    rnd = random.Random(12)
    values = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.5, 2.5, 0.1, 1e10, -1e10, 2147483647.0, 2147483648.0, -2147483648.0,
              -2147483649.0, 2147483647.5, 2147483647.9, -2147483648.5, -2147483648.9, 4294967295.0, 4294967296.0,
              1e15, 1e16, 1e22, 1e30, 1e100, 1e300, 1.7976931348623157e308, 5e-324, 1e-300, 3.14159, 123456.789,
              0.3, 1e9, 2 ** 31 + 0.5, 2.0 ** 53, 2.0 ** 62, 2.0 ** 63, 2.0 ** 64, 9999999999.0, 0.999999999]
    values += [rnd.uniform(-1e6, 1e6) for _ in range(150)]
    values += [rnd.uniform(-1e10, 1e10) for _ in range(150)]
    values += [rnd.randrange(-2 ** 33, 2 ** 33) + rnd.choice([0, 0.25, 0.5, 0.75]) for _ in range(150)]
    values += [bits_double(rnd.getrandbits(63) | (rnd.getrandbits(1) << 63)) for _ in range(100)]
    values += [float("inf"), float("-inf")]
    return [x for x in values if x == x]


def nan_values():
    return [bits_double(0x7FF8000000000000), bits_double(0xFFF8000000000000)]


def conversion_cases():
    rnd = random.Random(31)
    cases = []
    for x in float_values() + nan_values():
        cases.append(("float.nb_int", [f(x)]))
        cases.append(("float.nb_long", [f(x)]))
        cases.append(("float.tp_hash", [f(x)]))
    return cases


@register("py_long.float_conversions_match_exe")
def float_conversions():
    exe, probe, result = Exe(), Probe(False), Comparison()
    for index, (name, specs) in enumerate(conversion_cases()):
        if index % 64 == 0:
            exe.reset()
        spec_name = name
        exe_result = exe.slot(spec_name, specs)
        native = probe.slot(spec_name, specs)
        if exe_result[0] == "E" and native[0] == "E":
            exe_result, native = exe_result[:2], native[:2]
        result.check("%s %r" % (name, specs), exe_result, native)
    result.finish("float to int, float to long, float hash")


@register("py_long.float_conversions_wide_long", expect_fail=True)
def float_conversions_wide():
    exe, probe, result = Exe(), Probe(True), Comparison()
    for index, (name, specs) in enumerate(conversion_cases()[:600]):
        if index % 64 == 0:
            exe.reset()
        result.check("%s %r" % (name, specs), exe.slot(name, specs), probe.slot(name, specs))
    result.finish("float conversions, long of the host")


def api_cases():
    rnd = random.Random(44)
    cases = []
    numbers = [i(v) for v in EDGE] + [f(x) for x in float_values()[:80]]
    numbers += [long_spec(v) for v in (0, 1, -1, 2 ** 31 - 1, 2 ** 31, -2 ** 31, -2 ** 31 - 1, 2 ** 32 - 1, 2 ** 32, 2 ** 50,
                                      -2 ** 50, 10 ** 12)]
    numbers += [random_long(rnd) for _ in range(150)]
    for spec in numbers:
        cases.append(("PyInt_AsLong", [spec]))
    for spec in [n for n in numbers if n[0] == "l"]:
        cases.append(("PyLong_AsLong", [spec]))
        cases.append(("PyLong_AsUnsignedLong", [spec]))
        cases.append(("PyLong_AsDouble", [spec]))
    for v in EDGE + random_ints(rnd, 600):
        cases.append(("PyLong_FromLong", [i(v)]))
        cases.append(("PyLong_FromUnsignedLong", [i(v)]))
    for x in float_values():
        cases.append(("PyLong_FromDouble", [f(x)]))
    return cases


def text_conversion_cases():
    rnd = random.Random(61)
    cases = []
    texts = list(strtol_texts())[:700] + [b"12\x0034", b"1 2", b"5 ", b" 5", b"5\xa0", b"\xa05", b"5\x85", b"1" * 205,
                                         b"-" + b"9" * 205, b"2147483647", b"2147483648", b"-2147483648",
                                         b"-2147483649", b"007", b"0x1f", b"1L", b"1l", b"12abc"]
    for text in texts:
        cases.append(("PyNumber_Int", [s(text)]))
        cases.append(("PyNumber_Long", [s(text)]))
    for base in (0, 2, 8, 10, 16, 36, 1, 37, -1):
        for text in texts[:400]:
            cases.append(("PyLong_FromString", [s(text), i(base)]))
    others = [i(5), i(-1), f(1.5), f(-2.5), f(1e10), long_spec(2 ** 40), long_spec(7), NONE_SPEC, tup(i(1)), f(float("inf"))]
    for spec in others:
        cases.append(("PyNumber_Int", [spec]))
        cases.append(("PyNumber_Long", [spec]))
    return cases


def format_cases():
    rnd = random.Random(62)
    formats = [b"%d", b"%i", b"%u", b"%x", b"%X", b"%o", b"%#x", b"%#X", b"%#o", b"%08x", b"%-10x|", b"%+d", b"% d",
               b"%.5d", b"%5.3d", b"%#010x", b"%c", b"%s", b"%5d%%", b"%ld", b"%hx", b"%.0x", b"%#.0x", b"%+x"]
    values = [i(v) for v in EDGE] + [i(v) for v in random_ints(rnd, 120)] + [f(1.5), f(-2.5), f(1e10), f(-1.0),
                                                                              long_spec(5), long_spec(2 ** 31),
                                                                              long_spec(-2 ** 31), long_spec(2 ** 40),
                                                                              s(b"text"), NONE_SPEC]
    cases = []
    for fmt in formats:
        for value in values:
            cases.append(("PyString_Format", [s(fmt), value]))
            cases.append(("PyString_Format", [s(fmt), tup(value)]))
    return cases


def builtin_cases():
    rnd = random.Random(73)
    cases = []
    small = [0, 1, -1, 2, 3, 5, 10, -5, 100, 31, 32]
    edge = [INT_MIN, INT_MIN + 1, INT_MAX, INT_MAX - 1, 0x40000000, -0x40000000, 65536, 46341]

    def pair_list(values):
        return [(a, b) for a in values for b in values]

    for hi in small + [-3, 7, 50, 1000, 3000]:
        cases.append(("builtin:range", [tup(i(hi))]))
    for lo, hi in pair_list(small + [-7, 20]):
        cases.append(("builtin:range", [tup(i(lo), i(hi))]))
    for lo, hi in pair_list([0, 5, -5, 100]):
        for step in (1, 2, 3, -1, -2, -7, 0, 100, 1000):
            cases.append(("builtin:range", [tup(i(lo), i(hi), i(step))]))
    # Only ranges with a few items or with too many items for an int: a longer list needs a host memory that the exe
    # does not have (size_t has 32 bits there).
    huge = [(INT_MIN, INT_MAX, 1), (INT_MAX, INT_MIN, -1), (INT_MIN, INT_MAX, 2), (INT_MAX, INT_MIN, -2),
            (-5, INT_MAX, 1), (INT_MIN, 5, 1), (INT_MIN, 0, 1), (5, INT_MIN, -1), (0, INT_MIN, 1),
            (INT_MIN, INT_MAX, INT_MAX), (INT_MIN, INT_MAX, 0x40000000), (INT_MIN, INT_MAX, 1 << 20),
            (INT_MAX, INT_MIN, -INT_MAX), (INT_MAX, INT_MIN, -0x40000000), (INT_MAX, INT_MIN, -(1 << 20)),
            (0, INT_MAX, INT_MAX), (0, INT_MAX, 0x40000000), (-0x40000000, 0x40000001, 0x40000000),
            (INT_MIN, 0, 1 << 30), (INT_MIN, 0, INT_MAX), (0, INT_MIN, -(1 << 30)), (0, INT_MIN, INT_MIN),
            (INT_MIN, INT_MAX, INT_MIN), (5, 0, INT_MIN), (0, 5, INT_MIN), (INT_MAX - 5, INT_MAX, 1),
            (INT_MAX, INT_MAX - 5, -1), (INT_MIN, INT_MIN + 3, 1), (INT_MIN + 3, INT_MIN, -1), (1 << 30, -(1 << 30), -1),
            (-(1 << 30), 1 << 30, 1), (-(1 << 30), (1 << 30) + 1, 1)]
    for lo, hi, step in huge:
        cases.append(("builtin:range", [tup(i(lo), i(hi), i(step))]))
    cases += [("builtin:range", [tup(f(2.5))]), ("builtin:range", [tup(long_spec(5))]), ("builtin:range", [tup()]),
              ("builtin:range", [tup(i(1), i(2), i(3), i(4))]), ("builtin:range", [tup(s(b"x"))]),
              ("builtin:range", [tup(long_spec(2 ** 31))])]
    for a, b in int_pairs(5, 300):
        if not (a == INT_MIN and b == -1):  # the trap of B0019 ends the process of the probe
            cases.append(("builtin:divmod", [tup(i(a), i(b))]))
        cases.append(("builtin:cmp", [tup(i(a), i(b))]))
    for a in EDGE + random_ints(rnd, 200):
        cases.append(("builtin:abs", [tup(i(a))]))
        cases.append(("builtin:hex", [tup(i(a))]))
        cases.append(("builtin:oct", [tup(i(a))]))
        cases.append(("builtin:int", [tup(i(a))]))
        cases.append(("builtin:long", [tup(i(a))]))
        cases.append(("builtin:float", [tup(i(a))]))
        cases.append(("builtin:chr", [tup(i(a))]))
        cases.append(("builtin:hash", [tup(i(a))]))
    for a, e in [(a, e) for a in (2, -2, 3, 10, 7, 0, 1, -1, 46340, 46341) for e in (0, 1, 2, 10, 30, 31, 32, 62, 100)]:
        cases.append(("builtin:pow", [tup(i(a), i(e))]))
        cases.append(("builtin:pow", [tup(i(a), i(e), i(1000))]))
    for x in float_values()[:120]:
        cases.append(("builtin:int", [tup(f(x))]))
        cases.append(("builtin:round", [tup(f(x))]))
        cases.append(("builtin:round", [tup(f(x), i(2))]))
        cases.append(("builtin:long", [tup(f(x))]))
        cases.append(("builtin:abs", [tup(f(x))]))
    return cases


def run_api(wide, cases):
    exe, probe, result = Exe(), Probe(wide), Comparison()
    for index, (name, specs) in enumerate(cases):
        if index % 64 == 0:
            exe.reset()
        result.check("%s %r" % (name, specs), exe.api(name, specs), probe.api(name, specs))
    return result


@register("py_long.object_api_matches_exe")
def object_api():
    run_api(False, api_cases()).finish("PyInt_AsLong, PyLong_AsLong and the conversions of long")


@register("py_long.text_conversions_match_exe")
def text_conversions():
    run_api(False, text_conversion_cases()).finish("int(), long() and PyLong_FromString on text")


@register("py_long.text_conversions_wide_long", expect_fail=True)
def text_conversions_wide():
    run_api(True, text_conversion_cases()[:600]).finish("int() and long() on text, long of the host")


@register("py_long.format_matches_exe")
def format_conversions():
    run_api(False, format_cases()).finish("the integer conversions of the % operator")


@register("py_long.format_wide_long", expect_fail=True)
def format_wide():
    run_api(True, format_cases()[:600]).finish("the % operator, long of the host")


@register("py_long.builtins_match_exe")
def builtins():
    run_api(False, builtin_cases()).finish("range, divmod, pow, hex, oct, int, long, chr, hash and round")


@register("py_long.builtins_wide_long", expect_fail=True)
def builtins_wide():
    # A range with a huge item count would need all the memory of the host without the 32-bit overflow check.
    run_api(True, [c for c in builtin_cases() if c[0] != "builtin:range"][:700]).finish(
        "built-in functions, long of the host")


@register("py_long.object_api_wide_long", expect_fail=True)
def object_api_wide():
    run_api(True, api_cases()[:500]).finish("object API, long of the host")


# Hash values ------------------------------------------------------------------------------------------------------

def random_bytes(rnd, length):
    mode = rnd.randrange(3)
    if mode == 0:
        return bytes(rnd.randrange(256) for _ in range(length))
    if mode == 1:
        return bytes(rnd.randrange(128, 256) for _ in range(length))
    return bytes(rnd.choice(b"abcdefghijklmnopqrstuvwxyz_0123456789") for _ in range(length))


def hash_cases():
    rnd = random.Random(2025)
    cases = []
    strings = [b"", b"a", b"abc", b"\0", b"\xff", b"\x80\x80", b"Hello, World", b"x" * 300] + [
        random_bytes(rnd, n) for n in range(0, 301)] + [random_bytes(rnd, rnd.randrange(0, 301)) for _ in range(1500)]
    for data in strings:
        cases.append(("string.tp_hash", [s(data)]))
    for v in EDGE + random_ints(rnd, 500):
        cases.append(("int.tp_hash", [i(v)]))
    for _ in range(300):
        cases.append(("long.tp_hash", [random_long(rnd)]))
    for x in float_values():
        cases.append(("float.tp_hash", [f(x)]))
    leafs = [i(v) for v in EDGE[:20]] + [s(b"key"), s(b""), s(b"\xff\xfe"), f(1.5), f(0.1), long_spec(2 ** 40)]
    for _ in range(400):
        count = rnd.randrange(0, 6)
        items = [rnd.choice(leafs) for _ in range(count)]
        if rnd.random() < 0.3:
            items.append(tup(*[rnd.choice(leafs) for _ in range(rnd.randrange(0, 3))]))
        cases.append(("tuple.tp_hash", [tup(*items)]))
    for _ in range(300):
        re_, im = rnd.choice(float_values()), rnd.choice(float_values())
        cases.append(("complex.tp_hash", [("x", double_bits(re_), double_bits(im))]))
    return cases


@register("py_long.hash_matches_exe")
def hashes():
    run_slots(False, hash_cases()).finish("hash of string, int, long, float, tuple and complex")


@register("py_long.hash_wide_long", expect_fail=True)
def hashes_wide():
    run_slots(True, hash_cases()[:700]).finish("hash, long of the host")


@register("py_long.cached_string_hash_matches_exe")
def cached_string_hash():
    """The string hash reads and writes the cache at +0xc and reads the interned string at +0x10 (0x750c40)."""
    exe = Exe()
    exe.reset()
    for cached in (0, 5, -7, 0x7FFFFFFF, INT_MIN, -1):
        pointer = exe.make_string(b"cache")
        exe.machine.write(pointer + 0xC, struct.pack("<i", cached))
        value = struct.unpack("<i", struct.pack("<I", exe.call(0x750C40, [pointer])))[0]
        stored = struct.unpack("<i", exe.machine.read(pointer + 0xC, 4))[0]
        assert value == (cached if cached != -1 else stored), "cached %d gave %d" % (cached, value)
    base = exe.make_string(b"base")
    value = struct.unpack("<i", struct.pack("<I", exe.call(0x750C40, [base])))[0]
    other = exe.make_string(b"other")
    exe.machine.write(other + 0x10, struct.pack("<I", base))
    assert struct.unpack("<i", struct.pack("<I", exe.call(0x750C40, [other])))[0] == value
    assert struct.unpack("<i", exe.machine.read(other + 0xC, 4))[0] == value


# PyOS_strtol and PyOS_strtoul -------------------------------------------------------------------------------------

def strtol_texts():
    rnd = random.Random(808)
    texts = [b"", b"0", b"1", b"-1", b"+1", b"  12", b"\t\n\x0b\x0c\r 7", b"0x", b"0x0", b"0xff", b"0XFF", b"0xFFFFFFFF",
             b"0x7fffffff", b"0x80000000", b"0x100000000", b"-0x80000000", b"-0x80000001", b"2147483647",
             b"2147483648", b"-2147483648", b"-2147483649", b"4294967295", b"4294967296", b"-4294967295",
             b"4294967297", b"99999999999", b"0777", b"0778", b"00", b"-", b"+", b"- 5", b"--5", b"12abc", b"abc",
             b"zz", b"ZZ", b"1_0", b"0b101", b"\xa0 5", b"\x85" + b"5", b"1e5", b"0x1g", b"-0", b"-00", b"000000000001",
             b"3000000000", b"-3000000000", b"1" * 40, b"0x" + b"f" * 20, b"7" * 12, b"-" + b"1" * 11, b"\x00 5",
             b"2147483647x", b"-2147483648x", b"4294967295 ", b"0xfffffffg"]
    for _ in range(400):
        base = rnd.choice([0, 0, 10, 16, 8, 2, 36, 7, 3])
        n = rnd.randrange(1, 14)
        digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"[:rnd.choice([10, 16, 36, 62])]
        body = "".join(rnd.choice(digits) for _ in range(n))
        prefix = rnd.choice(["", "", "-", "+", " ", "0x", "-0x", "0", "  -"])
        texts.append((prefix + body + rnd.choice(["", "", "x", " ", "."])).encode())
    return texts


def strtol_cases():
    texts = strtol_texts()
    bases = [0, 2, 8, 10, 16, 36, 1, 37, -1, 3, 7, 11, 20, 35]
    return [(t, b) for t in texts for b in bases]


def run_strtol(wide, unsigned, limit=None):
    exe, probe, result = Exe(), Probe(wide), Comparison()
    for index, (text, base) in enumerate(strtol_cases()[:limit]):
        if index % 64 == 0:
            exe.reset()
        label = "%s(%r, %d)" % ("PyOS_strtoul" if unsigned else "PyOS_strtol", text, base)
        a = exe.strtol(text, base, unsigned)
        b = probe.strtol(text, base, unsigned)
        result.check(label, (a[0], a[1], 1 if a[2] else 0), (b[0], b[1], 1 if b[2] else 0))
    return result


@register("py_long.strtol_matches_exe")
def strtol():
    run_strtol(False, False).finish("PyOS_strtol")


@register("py_long.strtoul_matches_exe")
def strtoul():
    run_strtol(False, True).finish("PyOS_strtoul")


@register("py_long.strtol_wide_long", expect_fail=True)
def strtol_wide():
    run_strtol(True, False, 3000).finish("PyOS_strtol, long of the host")


@register("py_long.strtoul_wide_long", expect_fail=True)
def strtoul_wide():
    run_strtol(True, True, 3000).finish("PyOS_strtoul, long of the host")


INTS_SCRIPT = os.path.join(REPO_ROOT, "tests", "py152", "ints.py")
HASH_SLOTS = {"s": "string.tp_hash", "rep": "string.tp_hash", "i": "int.tp_hash", "f": "float.tp_hash",
              "l": "long.tp_hash", "t": "tuple.tp_hash"}


def script_data(name):
    """The data block NAME of tests/py152/ints.py (between the lines BEGIN NAME and END NAME)."""
    text = open(INTS_SCRIPT).read()
    block = text[text.index("# BEGIN " + name):text.index("# END " + name)]
    body = block[block.index(name + " = ["):].split("=", 1)[1]
    return ast.literal_eval(body.replace("MIN", "-2147483648").strip())


def script_spec(entry):
    kind = entry[0]
    if kind == "s":
        return s(entry[1].encode("latin-1"))
    if kind == "rep":
        return s((entry[1] * entry[2]).encode("latin-1"))
    if kind == "i":
        return i(entry[1])
    if kind == "f":
        return f(float(entry[1]))
    if kind == "l":
        return long_spec(int(entry[1]))
    return tup(*[script_spec(item) for item in entry[1]])


def check_expectations(planted_defect=None):
    exe = Exe()
    exe.reset()
    hashes = script_data("HASHES")
    orders = script_data("ORDERS")
    if planted_defect == "hash":
        hashes = [(hashes[0][0], hashes[0][1] + 1)] + hashes[1:]
    if planted_defect == "order":
        first_ops, first_keys = orders[0]
        orders = [(first_ops, [first_keys[1], first_keys[0]] + first_keys[2:])] + orders[1:]
    for entry, expected in hashes:
        got = exe.slot(HASH_SLOTS[entry[0]], [script_spec(entry)])
        assert got == ("i", expected), "hash of %r: exe %r, script %r" % (entry, got, expected)
    for ops, expected in orders:
        exe.reset()
        got = exe.key_order([(op, script_spec(key)) for op, key in ops])
        assert got == [script_spec(key) for key in expected], "dictionary order: exe %r" % (got,)
    print("    %d hash values and %d dictionary orders equal" % (len(hashes), len(orders)))


@register("py_long.expectations_match_exe")
def expectations():
    """The hash values and the dictionary orders that tests/py152/ints.py expects are the results of the exe."""
    check_expectations()


@register("py_long.expectations_wrong_hash", expect_fail=True)
def expectations_wrong_hash():
    check_expectations("hash")


@register("py_long.expectations_wrong_order", expect_fail=True)
def expectations_wrong_order():
    check_expectations("order")


@register("py_long.crt_strtol_matches_exe")
def crt_strtol():
    """The strtol of the C run time, which cPickle uses for the opcode INT, against the replacement."""
    exe, probe, result = Exe(), Probe(False), Comparison()
    for index, (text, base) in enumerate(strtol_cases()):
        if index % 64 == 0:
            exe.reset()
        a = exe.strtol(text, base, False, function=CRT_STRTOL)
        b = probe.crt_strtol(text, base)
        result.check("strtol(%r, %d)" % (text, base), (a[0], a[1], 1 if a[2] else 0), (b[0], b[1], 1 if b[2] else 0))
    result.finish("strtol of the C run time")


@register("py_long.crt_strtol_host_strtol_wide_long", expect_fail=True)
def crt_strtol_host():
    """The planted defect: the strtol of the host, which has a 64-bit long, in place of the replacement."""
    libc = ctypes.CDLL(None, use_errno=True)
    libc.strtol.restype = ctypes.c_long
    libc.strtol.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]
    exe, result = Exe(), Comparison()
    for index, (text, base) in enumerate(strtol_cases()[:3000]):
        if index % 64 == 0:
            exe.reset()
        a = exe.strtol(text, base, False, function=CRT_STRTOL)
        buffer = ctypes.create_string_buffer(text)
        end = ctypes.c_char_p()
        ctypes.set_errno(0)
        value = libc.strtol(buffer, ctypes.byref(end), base)
        offset = ctypes.cast(end, ctypes.c_void_p).value - ctypes.addressof(buffer)
        result.check("strtol(%r, %d)" % (text, base), (a[0], a[1], 1 if a[2] else 0),
                     (value & 0xFFFFFFFF, offset, 1 if ctypes.get_errno() else 0))
    result.finish("strtol of the host")


# Dictionary order -------------------------------------------------------------------------------------------------

def dict_programs():
    rnd = random.Random(1999)
    programs = []
    words = [("name%d" % n).encode() for n in range(40)] + [b"", b"a", b"Position", b"Type", b"Mission", b"Ship"]
    programs.append([("S", s(w)) for w in words])
    programs.append([("S", i(v)) for v in range(0, 60)])
    programs.append([("S", i(v)) for v in range(60, 0, -1)])
    programs.append([("S", i(v * 1000)) for v in range(40)])
    programs.append([("S", i(-v)) for v in range(1, 40)])
    programs.append([("S", i(v)) for v in EDGE])
    programs.append([("S", s(b"k%d" % n)) for n in range(200)])
    programs.append([("S", f(v)) for v in (0.5, 1.0, 1.5, 2.0, -1.5, 100.25, 1e10, 1e100, 0.1)])
    programs.append([("S", tup(i(a), i(b))) for a in range(6) for b in range(6)])
    programs.append([("S", tup(s(b"x"), i(a))) for a in range(30)])
    programs.append([("S", long_spec(v)) for v in (0, 1, 2 ** 31, 2 ** 40, -2 ** 40, 10 ** 15)])
    for length in (5, 20, 60):
        ops = []
        keys = []
        for _ in range(length * 3):
            roll = rnd.random()
            if roll < 0.6 or not keys:
                key = rnd.choice([s(random_bytes(rnd, rnd.randrange(0, 12))), i(rnd.randrange(INT_MIN, INT_MAX + 1)),
                                  i(rnd.randrange(-50, 400)), s(b"id%d" % rnd.randrange(0, 80))])
                keys.append(key)
                ops.append(("S", key))
            else:
                ops.append(("D", rnd.choice(keys)))
        programs.append(ops)
    return programs


def run_dicts(wide, limit=None):
    exe, probe, result = Exe(), Probe(wide), Comparison()
    for number, ops in enumerate(dict_programs()[:limit]):
        exe.reset()
        result.check("dictionary program %d" % number, exe.dict_order(ops), probe.dict_order(ops))
    return result


@register("py_long.dict_order_matches_exe")
def dict_order():
    run_dicts(False).finish("dictionary tables")


@register("py_long.dict_order_wide_long", expect_fail=True)
def dict_order_wide():
    run_dicts(True, 12).finish("dictionary tables, long of the host")


# The trap of the exe ----------------------------------------------------------------------------------------------

@register("py_long.exe_traps_on_int_min_divided_by_minus_one")
def idiv_trap():
    """B0019: the exe divides with idiv, so -2**31 / -1 raises a hardware exception, and the probe takes the hook."""
    exe, probe = Exe(), Probe(False)
    exe.reset()
    for name in ("int.nb_divide", "int.nb_remainder", "int.nb_divmod"):
        specs = [i(INT_MIN), i(-1)]
        assert exe.slot(name, specs) == ("T", "trap"), "the exe did not trap in %s" % name
        assert probe.slot(name, specs) == ("T", "trap"), "the probe did not trap in %s" % name
    for specs in ([i(INT_MIN), i(1)], [i(INT_MIN), i(2)], [i(INT_MIN), i(INT_MIN)], [i(5), i(-1)]):
        assert exe.slot("int.nb_divide", specs) == probe.slot("int.nb_divide", specs)
