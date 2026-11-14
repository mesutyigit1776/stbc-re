"""Build the game-engine API surface that the scripts use and map it to stbc.exe.

Writes target/appc_method_table.tsv, target/api_surface.tsv,
target/api_unresolved.tsv, target/api_methods.tsv,
target/api_methods_resolved.tsv and target/wrapper_use.tsv.
Run with: python3 -I tools/api_surface.py
Run the checks with: python3 -I tools/api_surface.py --check
"""
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pyc152  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "game", "stbc.exe")
SHADOW = os.path.join(ROOT, "game", "scripts", "App.pyc")
SOURCES = {
    "stock": os.path.join(ROOT, "game"),
    "remastered": os.path.join(ROOT, "mods", "remastered_1.2"),
    "orion": os.path.join(ROOT, "mods", "orion_delta_rc4"),
}
TABLE_EVIDENCE_ID = "E0002"
METHOD_TABLE_ADDRESS = 0x8E6438
INIT_MODULE_STRING_ADDRESS = 0x8D9C78


# ---------------------------------------------------------------- exe table

class Pe:
    """Minimal PE reader for virtual-address lookups."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        count = struct.unpack_from("<H", d, pe + 6)[0]
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 0x34)[0]
        self.sections = []
        off = pe + 24 + opt_size
        for _ in range(count):
            name = d[off:off + 8].rstrip(b"\0").decode()
            vsize, rva, rsize, raw = struct.unpack_from("<IIII", d, off + 8)
            self.sections.append((name, self.base + rva, max(vsize, rsize), raw))
            off += 40

    def section(self, va):
        for name, start, size, _ in self.sections:
            if start <= va < start + size:
                return name
        return None

    def offset(self, va):
        for _, start, size, raw in self.sections:
            if start <= va < start + size:
                return raw + va - start
        return None

    def u32(self, va, n=1):
        return struct.unpack_from("<%dI" % n, self.data, self.offset(va))

    def cstring(self, va):
        off = self.offset(va)
        return self.data[off:self.data.index(b"\0", off)].decode("latin-1")


def read_method_table(exe, start=METHOD_TABLE_ADDRESS):
    """Return PyMethodDef entries (index, entry, name, function, flags) up to the zero entry."""
    entries = []
    addr = start
    while True:
        name_ptr, func, flags, doc = exe.u32(addr, 4)
        if name_ptr == 0 and func == 0:
            return entries, addr
        entries.append((len(entries), addr, exe.cstring(name_ptr), func, flags))
        addr += 16


# ------------------------------------------------------------ shadow module

def is_class_body(co):
    ins = list(pyc152.disassemble(co))
    return len(ins) >= 2 and ins[-2][1] == "LOAD_LOCALS"


def shadow_scope(co, qual, found):
    """Collect (App name -> Appc functions) from one scope of App.pyc."""
    ins = list(pyc152.disassemble(co))
    for k, (_, name, _, text) in enumerate(ins):
        if name != "LOAD_ATTR" or k == 0:
            continue
        prev = ins[k - 1]
        if prev[1] not in pyc152.LOAD_VAR_OPS or prev[3] != "Appc":
            continue
        if text == "globals":
            member = ins[k + 1][3]
            for j in range(k + 2, min(k + 6, len(ins))):
                if ins[j][1] == "STORE_NAME":
                    found[ins[j][3]].append("globals." + member)
                    break
        elif k >= 3 and ins[k - 3][3] == "new" and ins[k - 2][3] == "instancemethod":
            for j in range(k + 1, min(k + 10, len(ins))):
                if ins[j][1] == "STORE_ATTR":
                    owner = ins[j - 1][3]
                    found[owner + "." + ins[j][3]].append(text)
                    break
        elif k + 1 < len(ins) and ins[k + 1][1] == "STORE_NAME":
            found[(qual + "." if qual else "") + ins[k + 1][3]].append(text)
        elif co.name != "?" and not is_class_body(co):
            found[(qual + "." if qual else "") + co.name].append(text)


def shadow_map(path=SHADOW):
    """Map each App name to its Appc functions, from the SWIG shadow module."""
    top = pyc152.load_pyc(path)
    found = collections.defaultdict(list)
    shadow_scope(top, "", found)
    for child in top.consts:
        if not isinstance(child, pyc152.Code):
            continue
        if is_class_body(child):
            shadow_scope(child, child.name, found)
            for grand in child.consts:
                if isinstance(grand, pyc152.Code):
                    shadow_scope(grand, child.name, found)
        else:
            shadow_scope(child, "", found)
    result = {}
    top_ins = list(pyc152.disassemble(top))
    for k, (_, name, _, text) in enumerate(top_ins):
        if name == "BUILD_CLASS":
            result[top_ins[k + 1][3]] = []
    for key, funcs in found.items():
        unique = list(dict.fromkeys(funcs))
        result[key] = unique
        if key.endswith(".__init__"):
            cls = key[:-len(".__init__")]
            if not result.get(cls):
                result[cls] = unique
    return result


# ------------------------------------------------------------- class table

SKIPPED_METHODS = {"__init__", "__del__", "__repr__", "__setattr__", "__getattr__",
                   "<lambda>", "this", "thisown"}
CONSTRUCTOR_SUFFIXES = ("Cast", "Create", "GetObject", "GetObjectByID")


class Shadow:
    """Classes and module functions of the SWIG shadow module App.pyc."""

    def __init__(self):
        self.bases = {}
        self.methods = {}
        self.functions = {}
        self.returns = {}
        self.global_class = {}
        self.pointer_of = {}
        self.constructible = set()
        self.owner_mismatch = 0

    def is_pointer(self, cls):
        return cls in self.pointer_of

    def canonical(self, cls):
        return self.pointer_of.get(cls, cls)

    def lookup_order(self, cls):
        """Classes in Python 1.5.2 attribute lookup order: depth first, left to right."""
        order = [cls]
        for parent in self.bases.get(cls, ()):
            order.extend(self.lookup_order(parent))
        return order

    def resolve(self, cls, method):
        """Return (defining class, Appc functions) for a method of a class, or None."""
        for owner in self.lookup_order(cls):
            if method in self.methods.get(owner, {}):
                return owner, self.methods[owner][method]
        return None

    def depth(self, cls):
        parents = self.bases.get(cls, ())
        return 1 + max((self.depth(p) for p in parents), default=0)


def appc_attributes(co):
    """Appc attribute names read in one code object, in order."""
    ins = list(pyc152.disassemble(co))
    return [text for k, (_, name, _, text) in enumerate(ins)
            if name == "LOAD_ATTR" and k > 0 and ins[k - 1][3] == "Appc"
            and ins[k - 1][1] in pyc152.LOAD_VAR_OPS]


def wrapped_pointer_class(co, pointer_names):
    """Pointer class that a wrapper function applies to its result, or None."""
    for _, name, _, text in pyc152.disassemble(co):
        if name == "LOAD_GLOBAL" and text in pointer_names:
            return text
    return None


def read_shadow(path=SHADOW):
    """Read the class table, module functions and typed globals from App.pyc."""
    top = pyc152.load_pyc(path)
    ins = list(pyc152.disassemble(top))
    shadow = Shadow()
    for k, (_, name, _, text) in enumerate(ins):
        if name != "BUILD_CLASS":
            continue
        count = ins[k - 4][2]
        body = top.consts[ins[k - 3][2]]
        cls = ins[k + 1][3]
        shadow.bases[cls] = [ins[k - 4 - n][3] for n in range(count, 0, -1)]
        methods = {}
        for child in body.consts:
            if isinstance(child, pyc152.Code) and child.name == "__init__" and appc_attributes(child):
                shadow.constructible.add(cls)
            if isinstance(child, pyc152.Code) and child.name not in SKIPPED_METHODS:
                funcs = list(dict.fromkeys(appc_attributes(child)))
                if funcs:
                    methods[child.name] = funcs
        shadow.methods[cls] = methods
    for cls, bases in list(shadow.bases.items()):
        if cls.endswith("Ptr") and len(bases) == 1 and bases[0] == cls[:-3]:
            shadow.pointer_of[cls] = bases[0]
    for k, (_, name, _, text) in enumerate(ins):
        if name == "LOAD_ATTR" and text == "instancemethod" and k + 7 < len(ins):
            window = ins[k + 1:k + 8]
            if [w[1] for w in window] != ["LOAD_NAME", "LOAD_ATTR", "LOAD_NAME", "LOAD_NAME",
                                          "CALL_FUNCTION", "LOAD_NAME", "STORE_ATTR"]:
                continue
            function, bound, owner, method = window[1][3], window[3][3], window[5][3], window[6][3]
            if bound != owner:
                shadow.owner_mismatch += 1
            funcs = shadow.methods.setdefault(owner, {}).setdefault(method, [])
            if function not in funcs:
                funcs.append(function)
    pointer_names = set(shadow.pointer_of)
    for k, (_, name, _, text) in enumerate(ins):
        if name == "LOAD_CONST" and isinstance(top.consts[ins[k][2]], pyc152.Code) \
                and ins[k + 1][1] == "MAKE_FUNCTION" and ins[k + 2][1] == "STORE_NAME":
            co = top.consts[ins[k][2]]
            funcs = appc_attributes(co)
            if funcs:
                shadow.functions[ins[k + 2][3]] = funcs[0]
                pointer = wrapped_pointer_class(co, pointer_names)
                if pointer:
                    shadow.returns[ins[k + 2][3]] = shadow.pointer_of[pointer]
        elif name == "LOAD_NAME" and text == "Appc" and k + 2 < len(ins) \
                and ins[k + 1][1] == "LOAD_ATTR" and ins[k + 2][1] == "STORE_NAME" \
                and ins[k + 1][3] != "globals":
            shadow.functions.setdefault(ins[k + 2][3], ins[k + 1][3])
        elif name == "LOAD_NAME" and text in pointer_names and k + 5 < len(ins) \
                and ins[k + 1][3] == "Appc" and ins[k + 2][3] == "globals":
            shadow.global_class[ins[k + 5][3]] = shadow.pointer_of[text]
    return shadow


def constructor_classes(shadow):
    """Map each App function that returns an object of a known class to that class.

    The rule: `<Class>_Cast`, `<Class>_Create`, `<Class>_GetObject` and
    `<Class>_GetObjectByID`, and the class name itself. A function whose wrapper
    in App.pyc returns a different class is left out.
    """
    result, disagree = {}, []
    for cls in shadow.bases:
        if shadow.is_pointer(cls):
            continue
        for suffix in CONSTRUCTOR_SUFFIXES:
            name = "%s_%s" % (cls, suffix)
            if name in shadow.functions:
                returned = shadow.returns.get(name)
                if returned is None or returned == cls:
                    result[name] = cls
                else:
                    disagree.append((name, cls, returned))
        if cls in shadow.constructible:
            result[cls] = cls
    return result, disagree


# ------------------------------------------------------------- script usage

def strip_python(text):
    """Remove comments and string literal bodies from Python source."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "#":
            while i < n and text[i] != "\n":
                i += 1
        elif c in "'\"":
            quote = text[i:i + 3] if text[i:i + 3] in ('"""', "'''") else c
            i += len(quote)
            while i < n and not text.startswith(quote, i):
                if text[i] == "\\":
                    i += 1
                if len(quote) == 1 and i < n and text[i] == "\n":
                    break
                i += 1
            i += len(quote)
            out.append('""')
        else:
            out.append(c)
            i += 1
    return "".join(out)


ATTR_RE = re.compile(r"(?<![\w.])(App|Appc)\s*\.\s*([A-Za-z_]\w*)")
FROM_RE = re.compile(r"^[ \t]*from[ \t]+(App|Appc)[ \t]+import[ \t]+(.+)$", re.M)
ALIAS_RE = re.compile(r"^[ \t]*\w+[ \t]*=[ \t]*(App|Appc)[ \t]*$", re.M)


class FileUse:
    def __init__(self):
        self.attrs = {"App": collections.Counter(), "Appc": collections.Counter()}
        self.stars = set()
        self.aliases = 0


def scan_py(path):
    with open(path, "rb") as f:
        text = strip_python(f.read().decode("latin-1"))
    use = FileUse()
    for mod, attr in ATTR_RE.findall(text):
        use.attrs[mod][attr] += 1
    for mod, names in FROM_RE.findall(text):
        for item in names.replace("(", "").replace(")", "").split(","):
            item = item.split()[0] if item.split() else ""
            if item == "*":
                use.stars.add(mod)
            elif item:
                use.attrs[mod][item] += 1
    use.aliases = len(ALIAS_RE.findall(text))
    return use


def scan_pyc(path):
    attrs, froms, stars, aliases = pyc152.module_uses(pyc152.load_pyc(path))
    use = FileUse()
    for mod in attrs:
        use.attrs[mod].update(attrs[mod])
        use.attrs[mod].update(froms[mod])
    use.stars = set(stars)
    use.aliases = len(aliases)
    return use


def merge(a, b):
    """Union of two readings of one script: keep the larger count per name."""
    use = FileUse()
    for mod in use.attrs:
        for name in set(a.attrs[mod]) | set(b.attrs[mod]):
            use.attrs[mod][name] = max(a.attrs[mod][name], b.attrs[mod][name])
    use.stars = a.stars | b.stars
    use.aliases = max(a.aliases, b.aliases)
    return use


def scan_source(root, skip):
    """Return ({script key: FileUse}, error list) for one source tree."""
    files = {}
    for dirpath, _, names in os.walk(root):
        for name in names:
            stem, ext = os.path.splitext(name)
            if ext in (".py", ".pyc"):
                path = os.path.join(dirpath, name)
                if os.path.abspath(path) not in skip:
                    files.setdefault(os.path.join(dirpath, stem), {})[ext] = path
    uses, errors = {}, []
    for key, paths in files.items():
        readings = []
        for ext, reader in ((".py", scan_py), (".pyc", scan_pyc)):
            if ext in paths:
                try:
                    readings.append(reader(paths[ext]))
                except (ValueError, IndexError, struct.error) as err:
                    errors.append((paths[ext], str(err)))
        if readings:
            uses[key] = readings[0] if len(readings) == 1 else merge(*readings)
    return uses, errors


# ------------------------------------------------------- source method calls

COMPOUND_STARTS = {"if", "elif", "else", "for", "while", "try", "except", "finally"}
SIMPLE_STARTS = {"print", "return", "raise", "assert", "exec", "global", "pass",
                 "break", "continue"}
OPERATORS = ("==", "<=", ">=", "!=", "<>", "**", "<<", ">>", "+=", "-=", "*=", "/=",
             "%=", "&=", "|=", "^=")
ASSIGN_OPERATORS = {"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="}
OPEN, CLOSE = "([{", ")]}"
TRIPLE_QUOTES = ('"' * 3, "'" * 3)
RAW_PREFIXES = ("r", "R", "ur", "UR", "Ur", "uR")


def tokenize_logical_lines(text):
    """Split Python 1.5.2 source into logical lines.

    Returns a list of (indent, tokens, line number). A token is (kind, text)
    with kind n (name), o (operator), s (string) or d (number).
    """
    lines = []
    i, n = 0, len(text)
    lineno = 1
    tokens, depth, indent, start_line = [], 0, 0, 1
    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start and depth == 0:
            width, j = 0, i
            while j < n and text[j] in " \t\f":
                width = (width // 8 + 1) * 8 if text[j] == "\t" else width + 1
                j += 1
            if j >= n or text[j] in "\r\n#":
                while j < n and text[j] != "\n":
                    j += 1
                i = j + 1
                lineno += 1
                continue
            indent, start_line, i, at_line_start = width, lineno, j, False
            continue
        if c == "\n":
            lineno += 1
            i += 1
            if depth == 0:
                if tokens:
                    lines.append((indent, tokens, start_line))
                tokens, at_line_start = [], True
        elif c in " \t\r\f":
            i += 1
        elif c == "\\" and text[i + 1:i + 2] in ("\n", "\r"):
            i += 3 if text[i + 1:i + 3] == "\r\n" else 2
            lineno += 1
        elif c == "#":
            while i < n and text[i] != "\n":
                i += 1
        elif c in "'\"":
            raw = bool(tokens) and tokens[-1][0] == "n" and tokens[-1][1] in RAW_PREFIXES \
                and text[i - 1].isalpha()
            if text[i - 1].isalpha() and tokens and tokens[-1][0] == "n" \
                    and tokens[-1][1] in RAW_PREFIXES + ("u", "U"):
                tokens.pop()
            quote = text[i:i + 3] if text[i:i + 3] in TRIPLE_QUOTES else c
            i += len(quote)
            while i < n and not text.startswith(quote, i):
                if text[i] == "\\" and not raw:
                    i += 1
                if i < n and text[i] == "\n":
                    lineno += 1
                    if len(quote) == 1:
                        break
                i += 1
            i += len(quote)
            tokens.append(("s", ""))
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            tokens.append(("n", text[i:j]))
            i = j
        elif c.isdigit() or (c == "." and text[i + 1:i + 2].isdigit()):
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in "._" or
                             (text[j] in "+-" and text[j - 1] in "eE")):
                j += 1
            tokens.append(("d", text[i:j]))
            i = j
        else:
            op = text[i:i + 2] if text[i:i + 2] in OPERATORS else c
            if op in OPEN:
                depth += 1
            elif op in CLOSE:
                depth = max(0, depth - 1)
            tokens.append(("o", op))
            i += len(op)
    if tokens:
        lines.append((indent, tokens, start_line))
    return lines


def split_depth0(tokens, separator):
    """Split a token list at depth-0 operators equal to `separator`."""
    parts, current, depth = [], [], 0
    for tok in tokens:
        if tok[0] == "o" and tok[1] in OPEN:
            depth += 1
        elif tok[0] == "o" and tok[1] in CLOSE:
            depth -= 1
        if depth == 0 and tok == ("o", separator):
            parts.append(current)
            current = []
        else:
            current.append(tok)
    parts.append(current)
    return parts


def split_statements(indent, tokens, lineno):
    """Split a logical line into simple statements and compound headers.

    Yields (indent, tokens, line number). The body of a one-line compound
    statement gets indent + 1.
    """
    head = tokens[0][1] if tokens[0][0] == "n" else ""
    if head in COMPOUND_STARTS or head in ("def", "class"):
        depth, lambdas, colon = 0, 0, None
        for k, tok in enumerate(tokens):
            if tok[0] == "o" and tok[1] in OPEN:
                depth += 1
            elif tok[0] == "o" and tok[1] in CLOSE:
                depth -= 1
            elif depth == 0 and tok == ("n", "lambda"):
                lambdas += 1
            elif depth == 0 and tok == ("o", ":"):
                if lambdas:
                    lambdas -= 1
                else:
                    colon = k
                    break
        if colon is not None:
            yield indent, tokens[:colon + 1], lineno
            for part in split_depth0(tokens[colon + 1:], ";"):
                if part:
                    yield indent + 1, part, lineno
            return
    for part in split_depth0(tokens, ";"):
        if part:
            yield indent, part, lineno


def matching_open(tokens, close_index):
    depth = 0
    for k in range(close_index, -1, -1):
        tok = tokens[k]
        if tok[0] == "o" and tok[1] in CLOSE:
            depth += 1
        elif tok[0] == "o" and tok[1] in OPEN:
            depth -= 1
            if depth == 0:
                return k
    return None


def constructor_call(tokens, constructors):
    """Constructor name when the tokens are exactly `App.<name>(...)`."""
    if len(tokens) >= 5 and tokens[0] == ("n", "App") and tokens[1] == ("o", ".") \
            and tokens[2][0] == "n" and tokens[2][1] in constructors \
            and tokens[3] == ("o", "(") and tokens[-1] == ("o", ")") \
            and matching_open(tokens, len(tokens) - 1) == 3:
        return tokens[2][1]
    return None


def call_events(tokens, constructors):
    """Yield (method, kind, root name, depth, constructor) for each `.name(` call.

    kind is "chain" when the object is a name or a dotted name, "call" when it
    is the result of a call, and "other" for anything else.
    """
    for i in range(1, len(tokens) - 2):
        if tokens[i] != ("o", ".") or tokens[i + 1][0] != "n" or tokens[i + 2] != ("o", "("):
            continue
        method = tokens[i + 1][1]
        j = i - 1
        if tokens[j][0] == "n":
            while j >= 2 and tokens[j - 1] == ("o", ".") and tokens[j - 2][0] == "n":
                j -= 2
            if j >= 1 and tokens[j - 1] == ("o", "."):
                yield method, "other", None, 0, None
            else:
                yield method, "chain", tokens[j][1], (i - 1 - j) // 2, None
        elif tokens[j] == ("o", ")"):
            k = matching_open(tokens, j)
            constructor = None
            if k is not None and k >= 3 and (k < 4 or tokens[k - 4] != ("o", ".")):
                constructor = constructor_call(tokens[k - 3:j + 1], constructors)
            yield method, "call", None, 0, constructor
        else:
            yield method, "other", None, 0, None


class SourceScope:
    """Statements and events of one def, class body or module level."""

    def __init__(self):
        self.statements = []
        self.events = []


def names_in(tokens):
    return [t[1] for t in tokens if t[0] == "n"]


def scan_statement(tokens, constructors, scope, index, line, imports):
    """Record the call and store events of one statement in its scope."""
    events = scope.events
    head = tokens[0][1] if tokens[0][0] == "n" else ""

    def add_calls(part):
        for method, kind, root, depth, constructor in call_events(part, constructors):
            events.append(("call", index, line, method, kind, root, depth, constructor))

    def add_store(name, constructor=None):
        events.append(("store", index, line, name, constructor))

    if head == "import":
        for part in split_depth0(tokens[1:], ","):
            if part and part[0][0] == "n":
                if part[0][1] not in ("App", "Appc"):
                    imports.add(part[0][1])
                add_store(part[0][1])
    elif head == "from" and len(tokens) > 3 and tokens[2] == ("n", "import"):
        for part in split_depth0([t for t in tokens[3:] if t[1] not in ("(", ")")], ","):
            if part and part[0][0] == "n":
                if tokens[1][1] not in ("App", "Appc"):
                    imports.add(part[0][1])
                add_store(part[0][1])
    elif head == "for":
        split = next((k for k, t in enumerate(tokens) if t == ("n", "in")), 1)
        add_calls(tokens[split:])
        for name in names_in(tokens[1:split]):
            add_store(name)
    elif head == "except":
        add_calls(tokens)
        if len(tokens) >= 4 and tokens[-3] == ("o", ",") and tokens[-2][0] == "n":
            add_store(tokens[-2][1])
    elif head == "del":
        for name in names_in(tokens[1:]):
            add_store(name)
    elif head in COMPOUND_STARTS or head in SIMPLE_STARTS:
        add_calls(tokens)
    else:
        parts = split_depth0(tokens, "=")
        add_calls(parts[-1])
        for target in parts[:-1]:
            add_calls(target)
            if len(target) == 1 and target[0][0] == "n":
                add_store(target[0][1], constructor_call(parts[-1], constructors))
            elif not any(t[0] == "o" and t[1] in (".", "[") for t in target):
                for name in names_in(target):
                    add_store(name)
        if len(parts) == 1 and tokens[0][0] == "n" \
                and any(t[0] == "o" and t[1] in ASSIGN_OPERATORS for t in tokens):
            add_store(tokens[0][1])


def scan_source_text(text, constructors):
    """Method calls of one script, read from the source text.

    Returns a list of pyc152.CallSite. Scopes follow def and class headers by
    indentation. A name keeps the class of its latest constructor assignment
    while every later statement stays at or inside the indentation of that
    assignment, and no loop around the use stores the name again.
    """
    top = SourceScope()
    scopes, stack = [top], [(-1, top)]
    imports = set()
    for indent, tokens, lineno in tokenize_logical_lines(text):
        for sindent, stoks, sline in split_statements(indent, tokens, lineno):
            while len(stack) > 1 and sindent <= stack[-1][0]:
                stack.pop()
            scope = stack[-1][1]
            head = stoks[0][1] if stoks[0][0] == "n" else ""
            scope.statements.append((sindent, head in ("for", "while")))
            index = len(scope.statements) - 1
            if head in ("def", "class") and len(stoks) > 1:
                scope.events.append(("store", index, sline, stoks[1][1], None))
                scope = SourceScope()
                scopes.append(scope)
                stack.append((sindent, scope))
            else:
                scan_statement(stoks, constructors, scope, index, sline, imports)
    sites = []
    for scope in scopes:
        sites.extend(resolve_scope(scope, imports))
    return sites


def resolve_scope(scope, imports):
    """Turn the call events of one scope into pyc152.CallSite objects."""
    stores = {}
    for k, ev in enumerate(scope.events):
        if ev[0] == "store":
            stores.setdefault(ev[3], []).append((k, ev[1], ev[4]))
    spans = loop_spans(scope.statements)
    sites = []
    for k, ev in enumerate(scope.events):
        if ev[0] != "call":
            continue
        _, index, line, method, kind, root, depth, constructor = ev
        receiver = "object"
        if kind == "chain":
            if root in ("App", "Appc") and depth == 0:
                receiver = "app"
            elif root in imports:
                receiver = "module"
            elif root == "self" and depth == 0:
                receiver = "self"
            elif depth == 0:
                constructor = typed_by_text(root, k, index, stores, scope.statements, spans)
        sites.append(pyc152.CallSite(method, receiver, constructor, line, ""))
    return sites


def loop_spans(statements):
    """(first, last) statement indices of the body of each for and while header."""
    spans = []
    for first, (indent, is_loop) in enumerate(statements):
        if is_loop:
            last = first
            while last + 1 < len(statements) and statements[last + 1][0] > indent:
                last += 1
            spans.append((first, last))
    return spans


def typed_by_text(name, event_index, use_stmt, stores, statements, spans):
    """Constructor that gave `name` its value at a call event, or None."""
    previous = [s for s in stores.get(name, ()) if s[0] < event_index]
    if not previous:
        return None
    _, store_stmt, constructor = previous[-1]
    if constructor is None:
        return None
    store_indent = statements[store_stmt][0]
    if any(statements[i][0] < store_indent for i in range(store_stmt + 1, use_stmt + 1)):
        return None
    for first, last in spans:
        if store_stmt < first <= use_stmt <= last:
            if any(first <= s[1] <= last for s in stores[name]):
                return None
    return constructor


# --------------------------------------------------------- method call stats

class MethodIndex:
    """Method names of the shadow classes and the Appc functions behind them."""

    def __init__(self, shadow):
        self.shadow = shadow
        self.constructor_class, self.constructor_disagree = constructor_classes(shadow)
        self.candidates = collections.defaultdict(set)
        for cls, methods in shadow.methods.items():
            for name, funcs in methods.items():
                self.candidates[name].update(funcs)
        self.candidates = {k: sorted(v) for k, v in self.candidates.items()}

    def resolve_typed(self, constructor, method):
        """Appc function for a method called on the result of a constructor, or None."""
        cls = self.constructor_class.get(constructor)
        found = self.shadow.resolve(cls, method) if cls else None
        if found and len(found[1]) == 1:
            return found[1][0]
        return None


class UnitCalls:
    """Call counts of one script: by method name, and by resolution."""

    def __init__(self):
        self.calls = collections.Counter()
        self.typed = collections.Counter()
        self.unique = collections.Counter()
        self.ambiguous = collections.Counter()
        self.kinds = collections.Counter()
        self.typed_miss = 0

    def add(self, sites, index):
        for site in sites:
            self.kinds[site.receiver] += 1
            if site.receiver != "object" or site.method not in index.candidates:
                continue
            self.calls[site.method] += 1
            appc = index.resolve_typed(site.constructor, site.method) if site.constructor else None
            if site.constructor and appc is None:
                self.typed_miss += 1
            candidates = index.candidates[site.method]
            if appc:
                self.typed[appc] += 1
            elif len(candidates) == 1:
                self.unique[candidates[0]] += 1
            else:
                for func in candidates:
                    self.ambiguous[func] += 1

    def merge(self, other):
        """Keep the larger count per key (a .py and its .pyc are one script)."""
        for name in ("calls", "typed", "unique", "ambiguous", "kinds"):
            mine, theirs = getattr(self, name), getattr(other, name)
            for key in set(mine) | set(theirs):
                mine[key] = max(mine[key], theirs[key])
        self.typed_miss = max(self.typed_miss, other.typed_miss)


def read_unit_calls(paths, index):
    constructors = set(index.constructor_class)
    unit = None
    for ext in (".py", ".pyc"):
        if ext not in paths:
            continue
        if ext == ".py":
            with open(paths[ext], "rb") as f:
                sites = scan_source_text(f.read().decode("latin-1"), constructors)
        else:
            sites = pyc152.scan_method_calls(pyc152.load_pyc(paths[ext]), constructors)
        reading = UnitCalls()
        reading.add(sites, index)
        if unit is None:
            unit = reading
        else:
            unit.merge(reading)
    return unit


class SourceCalls:
    """Method call totals of one source tree."""

    def __init__(self):
        self.calls = collections.Counter()
        self.files = collections.Counter()
        self.typed = collections.Counter()
        self.unique = collections.Counter()
        self.ambiguous = collections.Counter()
        self.kinds = collections.Counter()
        self.typed_miss = 0
        self.errors = []
        self.units = 0
        self.py_only = 0
        self.file_sets = collections.defaultdict(set)

    def add(self, unit, key):
        self.units += 1
        for function in set(unit.typed) | set(unit.unique):
            self.file_sets[function].add(key)
        self.calls.update(unit.calls)
        self.files.update(unit.calls.keys())
        self.typed.update(unit.typed)
        self.unique.update(unit.unique)
        self.ambiguous.update(unit.ambiguous)
        self.kinds.update(unit.kinds)
        self.typed_miss += unit.typed_miss


def script_units(root, skip):
    """Map each script to its .py and .pyc paths, like scan_source."""
    files = {}
    for dirpath, _, names in os.walk(root):
        for name in names:
            stem, ext = os.path.splitext(name)
            if ext in (".py", ".pyc"):
                path = os.path.join(dirpath, name)
                if os.path.abspath(path) not in skip:
                    files.setdefault(os.path.join(dirpath, stem), {})[ext] = path
    return files


def scan_method_calls_in_tree(root, skip, index):
    totals = SourceCalls()
    for key, paths in sorted(script_units(root, skip).items()):
        try:
            unit = read_unit_calls(paths, index)
        except (ValueError, IndexError, struct.error, KeyError) as err:
            totals.errors.append((paths.get(".pyc") or paths[".py"], str(err)))
            continue
        totals.add(unit, key)
        if ".pyc" not in paths:
            totals.py_only += 1
    return totals


# ------------------------------------------------------------------- output

def read_data_addresses(out_dir):
    """Map Appc constant and global names to (data address, evidence id) from the init tables."""
    found = {}
    for file_name, column, prefix in (("appc_constants.tsv", "source_address", ""),
                                      ("appc_globals.tsv", "variable_address", "globals.")):
        path = os.path.join(out_dir, file_name)
        if not os.path.exists(path):
            continue
        with open(path) as f:
            header = f.readline().rstrip("\n").split("\t")
            for line in f:
                row = dict(zip(header, line.rstrip("\n").split("\t")))
                found[prefix + row["name"]] = (row[column], row["evidence_id"])
    return found


def write_tsv(path, header, rows):
    with open(path, "w") as f:
        f.write("\t".join(header) + "\n")
        for row in rows:
            f.write("\t".join(str(x) for x in row) + "\n")


# ------------------------------------------------------------------- checks

def rename_in_code(code, old, new):
    """Rename an attribute or variable name in every code object of a tree (in memory)."""
    for co in pyc152.walk_code(code):
        co.names = tuple(new if name == old else name for name in co.names)


def adjacent_call_count(code, method):
    """Count `LOAD_ATTR method` directly followed by a zero-argument call."""
    count = 0
    for co in pyc152.walk_code(code):
        ins = list(pyc152.disassemble(co))
        count += sum(1 for k in range(len(ins) - 1)
                     if ins[k][1] == "LOAD_ATTR" and ins[k][3] == method
                     and ins[k + 1][1] == "CALL_FUNCTION" and ins[k + 1][2] == 0)
    return count


def count_method(code, method, constructors):
    sites = pyc152.scan_method_calls(code, constructors)
    return sum(1 for s in sites if s.method == method and s.receiver == "object"), \
        sum(1 for s in sites if s.method == method and s.constructor)


TEXT_CASES = [
    ("typed name", "def f(o):\n\tp = App.ShipClass_Cast(o)\n\tp.GetName()\n", "ShipClass_Cast"),
    ("reassigned", "def f(o):\n\tp = App.ShipClass_Cast(o)\n\tp = o\n\tp.GetName()\n", None),
    ("store in branch", "def f(o):\n\tif o:\n\t\tp = App.ShipClass_Cast(o)\n\tp.GetName()\n", None),
    ("store in loop after use",
     "def f(o):\n\tp = App.ShipClass_Cast(o)\n\tfor i in o:\n\t\tp.GetName()\n\t\tp = i\n", None),
    ("other module", "import string\ndef f(o):\n\tstring.GetName()\n", "module"),
    ("direct call", "def f(o):\n\tApp.ShipClass_Cast(o).GetName()\n", "ShipClass_Cast"),
]


def run_checks():
    shadow = read_shadow()
    index = MethodIndex(shadow)
    constructors = set(index.constructor_class)
    failures = 0

    print("class table")
    print("  ShipClassPtr lookup order:", " > ".join(shadow.lookup_order("ShipClassPtr")))
    print("  resolve(ShipClassPtr, GetName):", shadow.resolve("ShipClassPtr", "GetName"))

    print("mutation of a stock script")
    for path in sorted(script_units(os.path.join(ROOT, "game"), set())):
        pyc_path = path + ".pyc"
        code = pyc152.load_pyc(pyc_path)
        total, _ = count_method(code, "GetName", constructors)
        expected = adjacent_call_count(code, "GetName")
        if total >= 3 and total == expected:
            break
    rename_in_code(code, "GetName", "GetNamX")
    after, _ = count_method(code, "GetName", constructors)
    print("  file: %s" % os.path.relpath(pyc_path, ROOT))
    print("  GetName calls before: %d, after renaming GetName to GetNamX: %d, dropped: %d, "
          "expected (independent adjacent-instruction count): %d"
          % (total, after, total - after, expected))
    failures += (total - after) != expected

    code = pyc152.load_pyc(pyc_path)
    by_constructor = collections.Counter(
        s.constructor for s in pyc152.scan_method_calls(code, constructors) if s.constructor)
    name, expected = by_constructor.most_common(1)[0]
    rename_in_code(code, name, name + "X")
    remaining = sum(1 for s in pyc152.scan_method_calls(code, constructors) if s.constructor)
    print("  typed calls before: %d, after renaming %s: %d, dropped: %d, expected: %d"
          % (sum(by_constructor.values()), name, remaining,
             sum(by_constructor.values()) - remaining, expected))
    failures += sum(by_constructor.values()) - remaining != expected

    print("type inference on source text")
    for label, text, expected in TEXT_CASES:
        sites = [s for s in scan_source_text(text, constructors) if s.method == "GetName"]
        got = sites[0].constructor or sites[0].receiver if sites else None
        if expected is None:
            got = sites[0].constructor if sites else None
        ok = got == expected
        failures += not ok
        print("  %-26s expected %-15s got %-15s %s" % (label, expected, got, "ok" if ok else "FAIL"))

    print("hand-checked example")
    py = os.path.join(ROOT, "mods", "remastered_1.2", "scripts", "Maelstrom", "Maelstrom.py")
    with open(py, "rb") as f:
        source = f.read().decode("latin-1")
    lines = source.split("\n")
    for reading, sites in (("source text", scan_source_text(source, constructors)),
                           (".pyc", pyc152.scan_method_calls(pyc152.load_pyc(py + "c"), constructors))):
        for site in sites:
            if site.line == 188 and site.method == "GetName":
                cls = index.constructor_class[site.constructor]
                print("  %s: line %d `%s` -> constructor %s, class %s, Appc %s"
                      % (reading, site.line, lines[site.line - 1].strip(), site.constructor, cls,
                         index.resolve_typed(site.constructor, site.method)))
    print("failures: %d" % failures)
    return failures


def method_rows(index, totals, address):
    """Rows of api_methods.tsv, sorted by Orion Delta calls then Remastered calls."""
    names = set()
    for source in totals.values():
        names.update(source.calls)
    rows = []
    for name in names:
        candidates = index.candidates[name]
        rows.append((name, totals["stock"].calls[name], totals["remastered"].calls[name],
                     totals["orion"].calls[name], totals["orion"].files[name],
                     len(candidates), ",".join(candidates),
                     ",".join("0x%x" % address[f] if f in address else "-" for f in candidates)))
    rows.sort(key=lambda r: (-r[3], -r[2], -r[1], r[0]))
    return rows


def resolved_rows(totals, address):
    """Rows of api_methods_resolved.tsv: calls resolved by the class of the object."""
    functions = set()
    for source in totals.values():
        functions.update(source.typed)
    rows = [(f, "0x%x" % address[f] if f in address else "",
             totals["stock"].typed[f], totals["remastered"].typed[f], totals["orion"].typed[f])
            for f in functions]
    rows.sort(key=lambda r: (-r[4], -r[3], -r[2], r[0]))
    return rows


def wrapper_use_rows(entries, surface_rows, totals):
    """Rows of wrapper_use.tsv: one per Appc method table entry."""
    direct = collections.defaultdict(lambda: [0, 0, 0])
    for row in surface_rows:
        if row[1]:
            for i in range(3):
                direct[row[1]][i] += row[2 + i]
    rows = []
    for _, _, name, function, _ in entries:
        d = direct.get(name, [0, 0, 0])
        resolved = [totals[k].typed[name] + totals[k].unique[name] for k in SOURCES]
        ambiguous = [totals[k].ambiguous[name] for k in SOURCES]
        mods_sure = d[1] + d[2] + resolved[1] + resolved[2]
        if mods_sure:
            used = "yes"
        elif ambiguous[1] + ambiguous[2]:
            used = "maybe"
        else:
            used = "no"
        rows.append((name, "0x%x" % function, sum(d), sum(resolved), sum(ambiguous), used,
                     d[2], resolved[2], ambiguous[2], d[1], resolved[1], ambiguous[1]))
    rows.sort(key=lambda r: (-(r[6] + r[7]), -r[8], -(r[9] + r[10]), -r[11], r[0]))
    return rows


def wrapper_file_rows(entries, surface_rows, usage, shadow, totals):
    """Rows of wrapper_files.tsv: the number of distinct scripts that use each wrapper for sure.

    A script counts when it calls the wrapper directly (Appc.NAME or App.NAME) or calls a method that the
    class of the object or the unique method name resolves to the wrapper (the same rule as wrapper_use.tsv,
    without the ambiguous calls)."""
    files = {label: collections.defaultdict(set) for label in SOURCES}
    for label, uses in usage.items():
        for key, use in uses.items():
            for mod, counter in use.attrs.items():
                for name in counter:
                    if mod == "Appc":
                        files[label][name].add(key)
                    else:
                        found = shadow.get(name, [])
                        if found:
                            files[label][found[0]].add(key)
        for function, keys in totals[label].file_sets.items():
            files[label][function] |= keys
    rows = []
    for _, _, name, function, _ in entries:
        rows.append((name, "0x%x" % function, len(files["stock"][name]), len(files["remastered"][name]),
                     len(files["orion"][name])))
    extra = sorted(set().union(*[set(f) for f in files.values()]) - {r[0] for r in rows})
    for name in extra:
        if name.startswith("globals."):
            rows.append((name, "", len(files["stock"][name]), len(files["remastered"][name]),
                         len(files["orion"][name])))
    rows.sort(key=lambda r: (-(r[3] + r[4]), -r[2], r[0]))
    return rows


def report_shadow(shadow, index):
    classes = [c for c in shadow.bases if not shadow.is_pointer(c)]
    depths = collections.Counter(shadow.depth(c) for c in classes)
    print("shadow classes: %d (+%d pointer classes), methods: %d, max depth %d, "
          "classes with 2 bases: %d, constructor wrappers: %d, return-class disagreements: %d"
          % (len(classes), len(shadow.pointer_of), sum(len(m) for m in shadow.methods.values()),
             max(depths), sum(1 for c in classes if len(shadow.bases[c]) > 1),
             len(index.constructor_class), len(index.constructor_disagree)))



def main():
    out_dir = os.path.join(ROOT, "target")
    os.makedirs(out_dir, exist_ok=True)

    exe = Pe(EXE)
    entries, terminator = read_method_table(exe)
    write_tsv(os.path.join(out_dir, "appc_method_table.tsv"),
              ["index", "entry_address", "name", "function_address", "flags"],
              [(i, "0x%x" % e, n, "0x%x" % f, fl) for i, e, n, f, fl in entries])
    table = {n: f for _, _, n, f, _ in entries}
    data = read_data_addresses(out_dir)
    print("method table: %d entries, terminator 0x%x" % (len(entries), terminator))

    shadow = shadow_map()
    skip = {os.path.abspath(os.path.join(r, "scripts", "App.pyc"))
            for r in SOURCES.values()}
    skip.add(os.path.abspath(SHADOW))

    usage = {}
    notes = {}
    for label, root in SOURCES.items():
        uses, errors = scan_source(root, skip)
        usage[label] = uses
        notes[label] = (errors, sum(1 for u in uses.values() if u.stars),
                        sum(1 for u in uses.values() if u.aliases))
        print("%s: %d scripts, %d unreadable, %d with star import, %d with alias"
              % (label, len(uses), len(errors), notes[label][1], notes[label][2]))
        for path, err in errors[:5]:
            print("  error:", path, err)

    stats = {}
    for label, uses in usage.items():
        for key, use in uses.items():
            for mod, counter in use.attrs.items():
                for name, n in counter.items():
                    row = stats.setdefault((mod, name), {})
                    cell = row.setdefault(label, [0, 0])
                    cell[0] += n
                    cell[1] += 1

    rows, unresolved = [], []
    for (mod, name), by_source in stats.items():
        if mod == "App":
            app_name = name
            funcs = shadow.get(name, [])
            appc_name = funcs[0] if funcs else ""
        else:
            app_name, appc_name = "", name
        address = table.get(appc_name, "")
        data_address, data_evidence = data.get(appc_name, ("", ""))
        if address == "" and data_address == "":
            if mod == "App" and name not in shadow:
                reason = "not defined in App.pyc"
            elif mod == "App" and not appc_name:
                reason = "class without constructor wrapper"
            elif appc_name.startswith("globals."):
                reason = "global variable of Appc.globals, not a method"
            else:
                reason = "constant or variable of Appc, not a method"
            unresolved.append((app_name, appc_name, reason))
        get = lambda label, i: by_source.get(label, [0, 0])[i]
        rows.append((app_name, appc_name, get("stock", 0), get("remastered", 0),
                     get("orion", 0), get("remastered", 1), get("orion", 1),
                     "0x%x" % address if address != "" else "", data_address,
                     TABLE_EVIDENCE_ID if address != "" else data_evidence))
    rows.sort(key=lambda r: (-r[4], -r[3], -r[2], r[0], r[1]))
    write_tsv(os.path.join(out_dir, "api_surface.tsv"),
              ["app_name", "appc_function", "uses_stock", "uses_remastered",
               "uses_orion_delta", "files_remastered", "files_orion_delta",
               "exe_wrapper_address", "exe_data_address", "evidence_id"], rows)
    unresolved.sort()
    write_tsv(os.path.join(out_dir, "api_unresolved.tsv"),
              ["app_name", "appc_function", "reason"], unresolved)
    print("rows: %d, unresolved: %d" % (len(rows), len(unresolved)))

    class_table = read_shadow()
    index = MethodIndex(class_table)
    report_shadow(class_table, index)
    totals = {}
    for label, root in SOURCES.items():
        totals[label] = scan_method_calls_in_tree(root, skip, index)
        t = totals[label]
        counted = sum(t.calls.values())
        print("%s method calls: %d counted = %d typed + %d unique-name + %d ambiguous; "
              "%d typed but method missing; %d scripts (%d without .pyc); %d unreadable; "
              "calls not counted: %s"
              % (label, counted, sum(t.typed.values()), sum(t.unique.values()),
                 counted - sum(t.typed.values()) - sum(t.unique.values()), t.typed_miss,
                 t.units, t.py_only, len(t.errors),
                 {k: v for k, v in sorted(t.kinds.items()) if k != "object"}))
        for path, err in t.errors[:5]:
            print("  error:", path, err)
    address = {n: f for _, _, n, f, _ in entries}
    write_tsv(os.path.join(out_dir, "api_methods.tsv"),
              ["method_name", "calls_stock", "calls_remastered", "calls_orion_delta",
               "files_orion_delta", "candidate_count", "candidate_appc_functions",
               "candidate_exe_addresses"], method_rows(index, totals, address))
    write_tsv(os.path.join(out_dir, "api_methods_resolved.tsv"),
              ["appc_function", "exe_address", "calls_stock", "calls_remastered",
               "calls_orion_delta"], resolved_rows(totals, address))
    write_tsv(os.path.join(out_dir, "wrapper_use.tsv"),
              ["appc_function", "function_address", "direct_uses", "resolved_method_calls",
               "ambiguous_method_calls", "used_by_mods", "direct_uses_orion_delta",
               "resolved_orion_delta", "ambiguous_orion_delta", "direct_uses_remastered",
               "resolved_remastered", "ambiguous_remastered"],
              wrapper_use_rows(entries, rows, totals))
    write_tsv(os.path.join(out_dir, "wrapper_files.tsv"),
              ["appc_function", "function_address", "files_stock", "files_remastered", "files_orion_delta"],
              wrapper_file_rows(entries, rows, usage, shadow, totals))


if __name__ == "__main__":
    if "--check" in sys.argv[1:]:
        sys.exit(1 if run_checks() else 0)
    main()
