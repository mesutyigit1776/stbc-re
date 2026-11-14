"""Reader and disassembler for Python 1.5.2 .pyc files (marshal format)."""
import collections
import struct
import sys

MAGIC_152 = b"\x99\x4e\x0d\x0a"
HAVE_ARGUMENT = 90

OPCODES = {
    0: "STOP_CODE", 1: "POP_TOP", 2: "ROT_TWO", 3: "ROT_THREE", 4: "DUP_TOP",
    10: "UNARY_POSITIVE", 11: "UNARY_NEGATIVE", 12: "UNARY_NOT",
    13: "UNARY_CONVERT", 15: "UNARY_INVERT", 19: "BINARY_POWER",
    20: "BINARY_MULTIPLY", 21: "BINARY_DIVIDE", 22: "BINARY_MODULO",
    23: "BINARY_ADD", 24: "BINARY_SUBTRACT", 25: "BINARY_SUBSCR",
    30: "SLICE+0", 31: "SLICE+1", 32: "SLICE+2", 33: "SLICE+3",
    40: "STORE_SLICE+0", 41: "STORE_SLICE+1", 42: "STORE_SLICE+2",
    43: "STORE_SLICE+3", 50: "DELETE_SLICE+0", 51: "DELETE_SLICE+1",
    52: "DELETE_SLICE+2", 53: "DELETE_SLICE+3", 60: "STORE_SUBSCR",
    61: "DELETE_SUBSCR", 62: "BINARY_LSHIFT", 63: "BINARY_RSHIFT",
    64: "BINARY_AND", 65: "BINARY_XOR", 66: "BINARY_OR", 70: "PRINT_EXPR",
    71: "PRINT_ITEM", 72: "PRINT_NEWLINE", 80: "BREAK_LOOP",
    82: "LOAD_LOCALS", 83: "RETURN_VALUE", 84: "IMPORT_STAR",
    85: "EXEC_STMT", 87: "POP_BLOCK", 88: "END_FINALLY", 89: "BUILD_CLASS",
    90: "STORE_NAME", 91: "DELETE_NAME", 92: "UNPACK_TUPLE",
    93: "UNPACK_LIST", 95: "STORE_ATTR", 96: "DELETE_ATTR",
    97: "STORE_GLOBAL", 98: "DELETE_GLOBAL", 100: "LOAD_CONST",
    101: "LOAD_NAME", 102: "BUILD_TUPLE", 103: "BUILD_LIST", 104: "BUILD_MAP",
    105: "LOAD_ATTR", 106: "COMPARE_OP", 107: "IMPORT_NAME",
    108: "IMPORT_FROM", 110: "JUMP_FORWARD", 111: "JUMP_IF_FALSE",
    112: "JUMP_IF_TRUE", 113: "JUMP_ABSOLUTE", 114: "FOR_LOOP",
    116: "LOAD_GLOBAL", 120: "SETUP_LOOP", 121: "SETUP_EXCEPT",
    122: "SETUP_FINALLY", 124: "LOAD_FAST", 125: "STORE_FAST",
    126: "DELETE_FAST", 127: "SET_LINENO", 130: "RAISE_VARARGS",
    131: "CALL_FUNCTION", 132: "MAKE_FUNCTION", 133: "BUILD_SLICE",
}
NAME_OPS = {"STORE_NAME", "DELETE_NAME", "STORE_ATTR", "DELETE_ATTR",
            "STORE_GLOBAL", "DELETE_GLOBAL", "LOAD_NAME", "LOAD_ATTR",
            "IMPORT_NAME", "IMPORT_FROM", "LOAD_GLOBAL"}
LOCAL_OPS = {"LOAD_FAST", "STORE_FAST", "DELETE_FAST"}
LOAD_VAR_OPS = {"LOAD_NAME", "LOAD_GLOBAL", "LOAD_FAST"}
STORE_VAR_OPS = {"STORE_NAME", "STORE_GLOBAL", "STORE_FAST"}


class Code:
    """A code object."""

    def __init__(self):
        self.argcount = self.nlocals = self.stacksize = self.flags = 0
        self.code = b""
        self.consts = ()
        self.names = ()
        self.varnames = ()
        self.filename = self.name = ""
        self.firstlineno = 0
        self.lnotab = b""


class Long(int):
    """A marshal long (Python 3 int subclass, kept for type fidelity)."""


class Unmarshaller:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.interned = []

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError("marshal data truncated")
        chunk = self.data[self.pos:self.pos + n]
        self.pos += n
        return chunk

    def int32(self):
        return struct.unpack("<i", self.take(4))[0]

    def int16(self):
        return struct.unpack("<h", self.take(2))[0]

    def load(self):
        t = chr(self.take(1)[0])
        if t == "0":
            return None
        if t == "N":
            return None
        if t == "i":
            return self.int32()
        if t == "I":
            return struct.unpack("<q", self.take(8))[0]
        if t == "l":
            n = self.int32()
            digits = [struct.unpack("<H", self.take(2))[0] for _ in range(abs(n))]
            v = 0
            for d in reversed(digits):
                v = (v << 15) | d
            return Long(-v if n < 0 else v)
        if t == "f":
            n = self.take(1)[0]
            return float(self.take(n).decode("latin-1"))
        if t == "x":
            n = self.take(1)[0]
            re_ = float(self.take(n).decode("latin-1"))
            n = self.take(1)[0]
            im = float(self.take(n).decode("latin-1"))
            return complex(re_, im)
        if t == "s":
            return self.take(self.int32())
        if t == "t":
            s = self.take(self.int32())
            self.interned.append(s)
            return s
        if t == "R":
            return self.interned[self.int32()]
        if t in "([":
            return tuple(self.load() for _ in range(self.int32()))
        if t == "{":
            d = {}
            while True:
                if self.data[self.pos:self.pos + 1] == b"0":
                    self.pos += 1
                    return d
                k = self.load()
                d[k] = self.load()
        if t == "c":
            return self.load_code()
        if t == ".":
            return Ellipsis
        raise ValueError("unknown marshal type %r at %d" % (t, self.pos - 1))

    def load_code(self):
        c = Code()
        c.argcount = self.int16()
        c.nlocals = self.int16()
        c.stacksize = self.int16()
        c.flags = self.int16()
        c.code = self.load()
        c.consts = self.load()
        c.names = tuple(n.decode("latin-1") for n in self.load())
        c.varnames = tuple(n.decode("latin-1") for n in self.load())
        c.filename = self.load().decode("latin-1")
        c.name = self.load().decode("latin-1")
        c.firstlineno = self.int16()
        c.lnotab = self.load()
        return c


def load_pyc(path):
    """Return the top-level Code object of a Python 1.5.2 .pyc file."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != MAGIC_152:
        raise ValueError("%s: not a Python 1.5.2 pyc" % path)
    return Unmarshaller(data[8:]).load()


def disassemble(code):
    """Yield (offset, opname, arg, argtext) for each instruction."""
    b = code.code
    i = 0
    while i < len(b):
        off = i
        op = b[i]
        i += 1
        name = OPCODES.get(op)
        if name is None:
            raise ValueError("unknown opcode %d in %s at %d" % (op, code.name, off))
        arg = None
        text = ""
        if op >= HAVE_ARGUMENT:
            arg = b[i] | (b[i + 1] << 8)
            i += 2
            if name in NAME_OPS:
                text = code.names[arg]
            elif name in LOCAL_OPS:
                text = code.varnames[arg]
            elif name == "LOAD_CONST":
                text = repr(code.consts[arg])
        yield off, name, arg, text


def walk_code(code):
    """Yield the code object and all nested code objects."""
    yield code
    for c in code.consts:
        if isinstance(c, Code):
            yield from walk_code(c)


def module_uses(code, modules=("App", "Appc")):
    """Scan code for module attribute use.

    Returns (attrs, froms, stars, aliases) where attrs maps module -> Counter of
    attribute names, froms maps module -> Counter of names imported with
    `from M import X`, stars is the set of modules imported with `import *`,
    and aliases is the set of (module, alias) pairs from `A = App`.
    """
    attrs = {m: collections.Counter() for m in modules}
    froms = {m: collections.Counter() for m in modules}
    stars = set()
    aliases = set()
    for co in walk_code(code):
        ins = list(disassemble(co))
        last_import = None
        for k, (off, name, arg, text) in enumerate(ins):
            if name == "LOAD_ATTR" and k > 0:
                pn, ptext = ins[k - 1][1], ins[k - 1][3]
                if pn in LOAD_VAR_OPS and ptext in modules:
                    attrs[ptext][text] += 1
            elif name == "IMPORT_NAME":
                last_import = text
            elif name == "IMPORT_FROM" and last_import in modules:
                froms[last_import][text] += 1
            elif name == "IMPORT_STAR" and last_import in modules:
                stars.add(last_import)
            elif name in STORE_VAR_OPS and k > 0:
                pn, ptext = ins[k - 1][1], ins[k - 1][3]
                if pn in LOAD_VAR_OPS and ptext in modules and text != ptext:
                    aliases.add((ptext, text))
    return attrs, froms, stars, aliases


FIXED_STACK_EFFECT = {
    "STOP_CODE": 0, "POP_TOP": -1, "ROT_TWO": 0, "ROT_THREE": 0, "DUP_TOP": 1,
    "UNARY_POSITIVE": 0, "UNARY_NEGATIVE": 0, "UNARY_NOT": 0, "UNARY_CONVERT": 0,
    "UNARY_INVERT": 0, "BINARY_POWER": -1, "BINARY_MULTIPLY": -1,
    "BINARY_DIVIDE": -1, "BINARY_MODULO": -1, "BINARY_ADD": -1,
    "BINARY_SUBTRACT": -1, "BINARY_SUBSCR": -1, "SLICE+0": 0, "SLICE+1": -1,
    "SLICE+2": -1, "SLICE+3": -2, "STORE_SLICE+0": -2, "STORE_SLICE+1": -3,
    "STORE_SLICE+2": -3, "STORE_SLICE+3": -4, "DELETE_SLICE+0": -1,
    "DELETE_SLICE+1": -2, "DELETE_SLICE+2": -2, "DELETE_SLICE+3": -3,
    "STORE_SUBSCR": -3, "DELETE_SUBSCR": -2, "BINARY_LSHIFT": -1,
    "BINARY_RSHIFT": -1, "BINARY_AND": -1, "BINARY_XOR": -1, "BINARY_OR": -1,
    "PRINT_EXPR": -1, "PRINT_ITEM": -1, "PRINT_NEWLINE": 0, "BREAK_LOOP": 0,
    "LOAD_LOCALS": 1, "RETURN_VALUE": -1, "IMPORT_STAR": -1, "EXEC_STMT": -3,
    "POP_BLOCK": 0, "END_FINALLY": -1, "BUILD_CLASS": -2, "STORE_NAME": -1,
    "DELETE_NAME": 0, "STORE_ATTR": -2, "DELETE_ATTR": -1, "STORE_GLOBAL": -1,
    "DELETE_GLOBAL": 0, "LOAD_CONST": 1, "LOAD_NAME": 1, "BUILD_MAP": 1,
    "LOAD_ATTR": 0, "COMPARE_OP": -1, "IMPORT_NAME": 1, "IMPORT_FROM": 1,
    "JUMP_FORWARD": 0, "JUMP_IF_FALSE": 0, "JUMP_IF_TRUE": 0, "JUMP_ABSOLUTE": 0,
    "FOR_LOOP": 1, "LOAD_GLOBAL": 1, "SETUP_LOOP": 0, "SETUP_EXCEPT": 0,
    "SETUP_FINALLY": 0, "LOAD_FAST": 1, "STORE_FAST": -1, "DELETE_FAST": 0,
    "SET_LINENO": 0,
}
RELATIVE_JUMPS = {"JUMP_FORWARD", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "FOR_LOOP",
                  "SETUP_LOOP", "SETUP_EXCEPT", "SETUP_FINALLY"}
TERMINAL_OPS = {"JUMP_FORWARD", "JUMP_ABSOLUTE", "RETURN_VALUE", "RAISE_VARARGS",
                "BREAK_LOOP"}
HANDLER_PUSH = 3


def stack_pop(stack, n=1):
    """Pop n tags from a producer-tag stack; missing entries read as unknown."""
    popped = [stack.pop() if stack else None for _ in range(n)]
    return popped


def apply_stack_effect(name, arg, stack):
    """Update the producer-tag stack for one instruction (generic effects only)."""
    if name in ("BUILD_TUPLE", "BUILD_LIST"):
        stack_pop(stack, arg)
        stack.append(None)
    elif name in ("UNPACK_TUPLE", "UNPACK_LIST"):
        stack_pop(stack)
        stack.extend([None] * arg)
    elif name == "RAISE_VARARGS":
        stack_pop(stack, arg)
    elif name == "MAKE_FUNCTION":
        stack_pop(stack, arg + 1)
        stack.append(None)
    elif name == "BUILD_SLICE":
        stack_pop(stack, arg)
        stack.append(None)
    else:
        effect = FIXED_STACK_EFFECT[name]
        if effect >= 0:
            stack.extend([None] * effect)
        else:
            stack_pop(stack, -effect)


class CallSite:
    """A method call `receiver.method(...)` found in bytecode.

    `receiver` is one of: "app" (the App or Appc module), "module" (a name bound
    by import), "self", or "object". `constructor` names the App function whose
    result is the receiver, when the code shows it.
    """

    def __init__(self, method, receiver, constructor, line, code_name):
        self.method = method
        self.receiver = receiver
        self.constructor = constructor
        self.line = line
        self.code_name = code_name


def imported_names(code, skip_modules=("App", "Appc")):
    """Names bound by import statements anywhere in a code tree."""
    names = set()
    for co in walk_code(code):
        ins = list(disassemble(co))
        source = None
        for k, (_, name, _, text) in enumerate(ins):
            if name == "IMPORT_NAME":
                source = text
                if k + 1 < len(ins) and ins[k + 1][1] in STORE_VAR_OPS:
                    if text.split(".")[0] not in skip_modules:
                        names.add(ins[k + 1][3])
            elif name == "IMPORT_FROM" and source not in skip_modules:
                if k + 1 < len(ins) and ins[k + 1][1] in STORE_VAR_OPS:
                    names.add(ins[k + 1][3])
    return names


def tag_root(tag):
    """Return (root variable name, attribute depth) of a producer tag, or (None, 0)."""
    depth = 0
    while tag is not None and tag[0] == "attr":
        tag = tag[1]
        depth += 1
    if tag is not None and tag[0] == "var":
        return tag[1], depth
    return None, depth


def code_flow(co, constructors):
    """Simulate the value stack of one code object.

    Returns (calls, stores, jumps). calls holds (offset, method, receiver tag,
    line); stores holds (offset, name, constructor or None); jumps holds
    (source, target) offset pairs.
    """
    calls, stores, jumps = [], [], []
    stack = []
    pending = {}
    line = 0
    after_terminal = False
    for off, name, arg, text in disassemble(co):
        if after_terminal:
            stack = list(pending.get(off, []))
        after_terminal = name in TERMINAL_OPS
        if name == "SET_LINENO":
            line = arg
            continue
        if name in RELATIVE_JUMPS or name == "JUMP_ABSOLUTE":
            target = arg if name == "JUMP_ABSOLUTE" else off + 3 + arg
            jumps.append((off, target))
            if name == "FOR_LOOP":
                pending.setdefault(target, stack[:-2])
            elif name in ("SETUP_EXCEPT", "SETUP_FINALLY"):
                pending.setdefault(target, list(stack) + [None] * HANDLER_PUSH)
            elif name != "JUMP_ABSOLUTE":
                pending.setdefault(target, list(stack))
        if name in LOAD_VAR_OPS:
            stack.append(("var", text, off))
        elif name == "LOAD_ATTR":
            obj = stack_pop(stack)[0]
            stack.append(("attr", obj, text, off))
        elif name == "DUP_TOP":
            stack.append(stack[-1] if stack else None)
        elif name == "ROT_TWO":
            if len(stack) >= 2:
                stack[-1], stack[-2] = stack[-2], stack[-1]
        elif name == "ROT_THREE":
            if len(stack) >= 3:
                top = stack.pop()
                stack.insert(-2, top)
        elif name == "CALL_FUNCTION":
            nargs, nkw = arg & 0xFF, (arg >> 8) & 0xFF
            stack_pop(stack, nargs + 2 * nkw)
            callee = stack_pop(stack)[0]
            result = None
            if callee is not None and callee[0] == "attr":
                calls.append((off, callee[2], callee[1], line))
                if callee[1] and callee[1][:2] == ("var", "App") and callee[2] in constructors:
                    result = ("ctor", callee[2])
            stack.append(result)
        elif name in STORE_VAR_OPS:
            tag = stack_pop(stack)[0]
            stores.append((off, text, tag[1] if tag and tag[0] == "ctor" else None))
        elif name in ("DELETE_NAME", "DELETE_FAST", "DELETE_GLOBAL"):
            stores.append((off, text, None))
        elif name == "FOR_LOOP":
            stack.append(None)
        else:
            apply_stack_effect(name, arg, stack)
    return calls, stores, jumps


def typed_by_flow(name, use, stores, jumps):
    """Constructor that gave `name` its value at offset `use`, or None.

    The latest store before the use counts. It is dropped when a jump from
    before the store lands between the store and the use (a path that skips
    the store), or when a backward jump from after the use returns to that
    range and the name is stored again before the jump.
    """
    previous = [s for s in stores if s[1] == name and s[0] < use]
    if not previous:
        return None
    store_at, _, constructor = max(previous)
    if constructor is None:
        return None
    for source, target in jumps:
        if source < store_at and store_at < target <= use:
            return None
        if source > use and store_at < target <= use:
            if any(s[1] == name and use < s[0] <= source for s in stores):
                return None
    return constructor


def scan_method_calls(code, constructors, modules=None):
    """Return the CallSites in a code tree whose object is not the App module."""
    modules = imported_names(code) if modules is None else modules
    sites = []
    for co in walk_code(code):
        calls, stores, jumps = code_flow(co, constructors)
        for off, method, receiver, line in calls:
            root, depth = tag_root(receiver)
            constructor = None
            if receiver is not None and receiver[0] == "ctor":
                kind, constructor = "object", receiver[1]
            elif root in ("App", "Appc") and depth == 0:
                kind = "app"
            elif root in modules:
                kind = "module"
            elif root == "self" and depth == 0:
                kind = "self"
            else:
                kind = "object"
                if root is not None and depth == 0:
                    constructor = typed_by_flow(root, receiver[2], stores, jumps)
            sites.append(CallSite(method, kind, constructor, line, co.name))
    return sites


def format_listing(code):
    lines = []
    for co in walk_code(code):
        lines.append("== %s (%s)" % (co.name, co.filename))
        for off, name, arg, text in disassemble(co):
            lines.append("%6d %-16s %s %s" % (off, name, "" if arg is None else arg, text))
    return "\n".join(lines)


if __name__ == "__main__":
    print(format_listing(load_pyc(sys.argv[1])))
