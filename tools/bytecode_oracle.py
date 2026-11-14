"""Bytecode oracle: compare the compiled files that ship with the game with the output of our compiler.

For each source `.py` that has a `.pyc` next to it, the host `bc_pyhost --compile-oracle` compiles the source text
the way the import code of the exe does (the file is read as the Windows CRT text mode does, then parsed and compiled
by the interpreter of the build). The marshal bytes are compared with the body of the shipped `.pyc`.

Ignored: the 4-byte time stamp of the `.pyc` header (bytes 4 to 7), the number of digits of a float exponent in
the text of a float constant, and the width of an integer constant (see below). The exe writes `6.7e-005` (three digit exponent, C run time of the exe, E0155). Most of the
shipped `.pyc` files were made by a compiler with another C library and have `6.7e-05`. Such a file counts as equal. Its
class is "exponent spelling" and the report counts these files. A shipped integer constant from 2**31 to 2**32-1 is
written as TYPE_INT64 (`I`), because the compiler of the shipped file had a 64-bit long. The exe has a 32-bit long: it
writes TYPE_INT with the same 32 bits, and its reader takes the low word of an `I` constant with a zero high word
(E0164). Such a file counts as equal, with the class "integer width". The magic number must be equal. The code file name
inside the marshal data is not ignored: it is taken from the shipped `.pyc` and given to the compiler, so that it
cannot cause a difference. The first lines of the output say so.

Run: tools/.venv/bin/python -I tools/bytecode_oracle.py --host build/bc_pyhost [--source stock --source remastered ...]
"""
import argparse
import collections
import os
import re
import struct
import subprocess
import sys

MAGIC_152 = b"\x99\x4e\x0d\x0a"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = {
    "stock": os.path.join(ROOT, "game"),
    "remastered": os.path.join(ROOT, "mods", "remastered_1.2"),
    "orion": os.path.join(ROOT, "mods", "orion_delta_rc4"),
}
CODE_FIELDS = ("argcount", "nlocals", "stacksize", "flags", "code", "consts", "names", "varnames", "filename",
               "name", "firstlineno", "lnotab")
FIELD_CLASS = {"code": "code bytes", "consts": "constant", "names": "name", "varnames": "name",
               "lnotab": "line table"}
CHUNK = 150


class Node:
    def __init__(self, tag, start):
        self.tag, self.start, self.end = tag, start, start
        self.value = None
        self.fields = {}
        self.items = []


class Reader:
    """Reads a marshal stream of Python 1.5.2 and keeps the byte range of each object."""

    def __init__(self, data):
        self.data, self.pos, self.interned = data, 0, []

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
        node = Node(chr(self.take(1)[0]), self.pos - 1)
        t = node.tag
        if t in "0N.":
            pass
        elif t == "i":
            node.value = self.int32()
        elif t == "I":
            node.value = struct.unpack("<q", self.take(8))[0]
        elif t == "l":
            n = self.int32()
            value = 0
            for digit in reversed([struct.unpack("<H", self.take(2))[0] for _ in range(abs(n))]):
                value = (value << 15) | digit
            node.value = -value if n < 0 else value
        elif t == "f":
            node.value = self.take(self.take(1)[0])
        elif t == "x":
            node.value = (self.take(self.take(1)[0]), self.take(self.take(1)[0]))
        elif t in "st":
            node.value = self.take(self.int32())
            if t == "t":
                self.interned.append(node.value)
        elif t == "R":
            node.value = self.interned[self.int32()]
        elif t in "([":
            node.items = [self.load() for _ in range(self.int32())]
        elif t == "{":
            while self.data[self.pos:self.pos + 1] != b"0":
                node.items.append(self.load())
                node.items.append(self.load())
            self.pos += 1
        elif t == "c":
            for field in CODE_FIELDS:
                if field in ("argcount", "nlocals", "stacksize", "flags", "firstlineno"):
                    start = self.pos
                    child = Node("h", start)
                    child.value = self.int16()
                    child.end = self.pos
                else:
                    child = self.load()
                node.fields[field] = child
        else:
            raise ValueError("unknown marshal type %r at %d" % (t, self.pos - 1))
        node.end = self.pos
        return node


def parse(body):
    reader = Reader(body)
    node = reader.load()
    return node


def innermost(node, offset, path):
    """Find the code object and the field that hold the byte at offset."""
    if node.tag != "c":
        for child in node.items:
            if child.start <= offset < child.end:
                return innermost(child, offset, path)
        return node, path
    for field in CODE_FIELDS:
        child = node.fields[field]
        if child.start <= offset < child.end:
            name = ""
            if node.fields["name"].value is not None:
                name = node.fields["name"].value.decode("latin-1")
            if field == "consts":
                for item in child.items:
                    if item.start <= offset < item.end:
                        if item.tag == "c":
                            return innermost(item, offset, path + [name])
                        return item, path + [name, field]
            return child, path + [name, field]
    return node, path


def int_kind(a_node, b_node):
    """Classify a difference of two integer constants."""
    def is_int(n):
        return n is not None and n.tag in "il" and isinstance(n.value, int)
    if not (is_int(a_node) or is_int(b_node)):
        return None
    if a_node is not None and b_node is not None and a_node.tag != b_node.tag and {a_node.tag, b_node.tag} <= set("ilI"):
        return "integer width"
    for n in (a_node, b_node):
        if is_int(n) and not -2**31 <= n.value < 2**31:
            return "integer width"
    return "integer other"


def classify(shipped, ours):
    """Return (class, detail) of the first difference of two marshal bodies."""
    limit = min(len(shipped), len(ours))
    offset = next((i for i in range(limit) if shipped[i] != ours[i]), limit)
    try:
        a_tree, b_tree = parse(shipped), None
    except (ValueError, IndexError, struct.error) as error:
        return "unreadable", "shipped body: %s" % error
    if offset >= len(shipped):
        return "length", "ours is longer, offset %d" % offset
    a_node, a_path = innermost(a_tree, offset, [])
    field = a_path[-1] if a_path else ""
    where = ".".join(p for p in a_path if p != field) + " / " + field
    try:
        b_tree = parse(ours)
        b_node, _ = innermost(b_tree, offset, []) if offset < len(ours) else (None, [])
    except (ValueError, IndexError, struct.error):
        b_node = None
    klass = FIELD_CLASS.get(field, "other")
    if field == "consts":
        kind = int_kind(a_node, b_node)
        if kind:
            klass = kind
        elif a_node is not None and b_node is not None and a_node.tag == "f" and b_node.tag == "f":
            klass = "float text"
    detail = "%s, offset %d" % (where, offset)
    if klass.startswith("integer"):
        detail += ", shipped %s %r, ours %s %r" % (a_node.tag, a_node.value, b_node.tag if b_node else "-",
                                                    b_node.value if b_node else None)
    elif klass == "float text":
        detail += ", shipped %s, ours %s" % (a_node.value.decode(), b_node.value.decode())
    elif klass == "constant" and a_node is not None:
        detail += ", shipped constant tag %s" % a_node.tag
    return klass, detail


def float_spans(node, spans):
    if node.tag in "fx":
        spans.append(node)
    for child in node.items:
        float_spans(child, spans)
    for child in node.fields.values():
        float_spans(child, spans)


def normalize_exponents(text):
    return re.sub(rb"e([+-])0(\d\d)", rb"e\1\2", text)


def normalize_floats(body):
    """Return the body with the exponents of the float texts in the two digit style: `e-005` becomes `e-05`."""
    spans = []
    float_spans(parse(body), spans)
    out, pos = bytearray(), 0
    for node in sorted(spans, key=lambda n: n.start):
        out += body[pos:node.start]
        texts = [node.value] if node.tag == "f" else list(node.value)
        out += node.tag.encode()
        for text in texts:
            text = normalize_exponents(text)
            out += bytes([len(text)]) + text
        pos = node.end
    out += body[pos:]
    return bytes(out)


def integer_spans(node, spans):
    if node.tag == "I":
        spans.append(node)
    for child in node.items:
        integer_spans(child, spans)
    for child in node.fields.values():
        integer_spans(child, spans)


def normalize_integers(body):
    """Return the body with each 64-bit integer constant that has a zero high word written as a 32-bit constant."""
    spans = []
    integer_spans(parse(body), spans)
    out, pos = bytearray(), 0
    for node in sorted(spans, key=lambda n: n.start):
        if not 0 <= node.value < 2 ** 32:
            continue
        out += body[pos:node.start] + b"i" + struct.pack("<I", node.value)
        pos = node.end
    out += body[pos:]
    return bytes(out)


def find_pairs(root):
    pairs = []
    for folder, _, names in os.walk(root):
        for name in sorted(names):
            if name.endswith(".py") and os.path.exists(os.path.join(folder, name + "c")):
                pairs.append(os.path.join(folder, name))
    return sorted(pairs)


def shipped_code_name(pyc):
    """The file name inside the code object of a .pyc (read from its marshal data)."""
    return parse(pyc[8:]).fields["filename"].value.decode("latin-1")


def run_host(host, jobs, read_mode):
    """Compile the (source, code name) jobs in chunks. Returns a list of (status, bytes).

    After a crash of the host, the file that was running gets the status CRASH and the rest starts again."""
    results = []
    args = [host, "--compile-oracle"] + (["--host-text"] if read_mode == "host" else [])
    while len(results) < len(jobs):
        chunk = jobs[len(results):len(results) + CHUNK]
        text = "".join("%s\t%s\n" % job for job in chunk)
        proc = subprocess.run(args, input=text.encode("latin-1"), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        data, pos, got = proc.stdout, 0, []
        while pos < len(data):
            end = data.index(b"\n", pos)
            tag, size = data[pos:end].decode().split()
            pos = end + 1
            got.append((tag, data[pos:pos + int(size)]))
            pos += int(size)
        if len(got) < len(chunk):
            got.append(("CRASH", b"host exit code %d" % proc.returncode))
        results.extend(got)
    return results


def compare_all(host, sources, read_mode, defect, stale_listed, limit_files=None):
    rows = []
    for label in sources:
        root = SOURCES[label]
        paths = find_pairs(root) if limit_files is None else [p for p in limit_files if p.startswith(root + os.sep)]
        jobs, pycs = [], {}
        for path in paths:
            pyc = open(path + "c", "rb").read()
            pycs[path] = pyc
            name = shipped_code_name(pyc) if pyc[:4] == MAGIC_152 else os.path.basename(path)
            jobs.append((path, name))
        results = run_host(host, jobs, read_mode)
        for (path, _), (tag, body) in zip(jobs, results):
            pyc = pycs[path]
            shipped = pyc[8:]
            if defect and path == paths[0]:
                index = parse(shipped).fields["code"].start + 5
                shipped = shipped[:index] + bytes([shipped[index] ^ 1]) + shipped[index + 1:]
            relative = os.path.relpath(path, root)
            if pyc[:4] != MAGIC_152:
                status, klass, detail = "different", "magic", "magic %r" % pyc[:4]
            elif tag != "OK":
                status, klass, detail = "different", "compile " + tag.lower(), body.decode("latin-1")[:200]
            elif body == shipped:
                status, klass, detail = "equal", "", ""
            else:
                try:
                    shipped_n, body_n = normalize_floats(shipped), normalize_floats(body)
                    shipped_i = normalize_integers(shipped_n)
                except (ValueError, IndexError, struct.error):
                    shipped_n, body_n, shipped_i = shipped, body, shipped
                if shipped_n == body_n:
                    status, klass, detail = "equal", SPELLING, "shipped float text has the two digit exponent style"
                elif shipped_i == body_n:
                    status, klass, detail = "equal", INTEGER_SPELLING, "shipped integer constant has a 64-bit type"
                else:
                    klass, detail = classify(shipped_n, body_n)
                    status = "different"
            if status == "different" and relative in stale_listed.get(label, ()):
                status = "stale-listed"
            rows.append((label, relative, status, klass, detail))
    return rows


NUMBER_FORMAT_CLASSES = ("integer width", "float text")
SPELLING = "exponent spelling"
INTEGER_SPELLING = "integer width"


def write_stale(path, rows):
    """Write the list of stale files: the shipped .pyc was made from another text than the .py next to it.

    The rule: a file is stale when it differs and the first difference is not a number format difference
    (integer width, float text). Such a difference cannot come from the compiler of the exe, because the code,
    the names or the constants of the source are not the ones of the .pyc."""
    with open(path, "w") as handle:
        handle.write("source\tfile\tclass\tdetail\n")
        for r in rows:
            if r[2] == "different" and r[3] not in NUMBER_FORMAT_CLASSES:
                handle.write("%s\t%s\t%s\t%s\n" % (r[0], r[1], r[3], r[4]))


def read_stale(path):
    listed = collections.defaultdict(set)
    if path and os.path.exists(path):
        with open(path) as handle:
            next(handle, None)
            for line in handle:
                fields = line.rstrip("\n").split("\t")
                if len(fields) >= 2:
                    listed[fields[0]].add(fields[1])
    return listed


def report(rows, sources, out):
    print("Bytecode oracle. Ignored: the 4-byte time stamp (bytes 4 to 7) of each .pyc header. "
          "The code file name comes from the shipped .pyc.")
    failed = False
    for label in sources:
        mine = [r for r in rows if r[0] == label]
        counts = collections.Counter(r[2] for r in mine)
        print("%s: files compared %d, equal %d, different %d, stale-listed %d"
              % (label, len(mine), counts["equal"], counts["different"], counts["stale-listed"]))
        spelled = [r for r in mine if r[2] == "equal" and r[3] == SPELLING]
        if spelled:
            print("  equal with a different spelling of the float exponent: %d files" % len(spelled))
        wide = [r for r in mine if r[2] == "equal" and r[3] == INTEGER_SPELLING]
        if wide:
            print("  equal with a 64-bit integer constant in the shipped file: %d files" % len(wide))
        classes = collections.Counter(r[3] for r in mine if r[2] == "different")
        for klass, count in classes.most_common():
            print("  class %-18s %d" % (klass, count))
            for r in [r for r in mine if r[2] == "different" and r[3] == klass][:5]:
                print("    %s: %s" % (r[1], r[4]))
        failed = failed or counts["different"] > 0
    print("FAIL oracle: all compared files equal" if failed else "PASS oracle: all compared files equal")
    if out:
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "oracle_results.tsv"), "w") as handle:
            handle.write("source\tfile\tstatus\tclass\tdetail\n")
            for r in rows:
                handle.write("\t".join(r) + "\n")
    return failed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--source", action="append", choices=sorted(SOURCES))
    parser.add_argument("--files", help="a list of source paths relative to the repository root (a sample)")
    parser.add_argument("--read-mode", choices=("crt", "host"), default="crt")
    parser.add_argument("--defect", action="store_true", help="plant a one-byte change in the first shipped file")
    parser.add_argument("--stale", default=os.path.join(ROOT, "target", "stale_pyc.tsv"))
    parser.add_argument("--write-stale", help="write the list of stale files to this path")
    parser.add_argument("--out", help="folder for oracle_results.tsv")
    args = parser.parse_args()
    sources = args.source or sorted(SOURCES)
    limit = None
    if args.files:
        with open(args.files) as handle:
            limit = [os.path.join(ROOT, line.strip()) for line in handle if line.strip()]
        sources = [s for s in sources if any(p.startswith(SOURCES[s] + os.sep) for p in limit)]
    rows = compare_all(args.host, sources, args.read_mode, args.defect, read_stale(args.stale), limit)
    if args.write_stale:
        write_stale(args.write_stale, rows)
    sys.exit(1 if report(rows, sources, args.out) else 0)


if __name__ == "__main__":
    main()
