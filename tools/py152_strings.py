"""Compare the strings of the Python 1.5.2 inside stbc.exe with the pristine CPython 1.5.2 source.

Run with: tools/.venv/bin/python -I tools/py152_strings.py [--tarball PATH] [--out DIR]
It reads game/stbc.exe and the pristine sources. With --tarball (py152.tgz) it reads all of
Include, Objects, Python, Parser, Modules and PC from the tarball. Without it, it reads
third_party/Python-1.5.2/ only, which lacks some modules and PC/.
It writes exe_only.tsv, pristine_missing.tsv and per_file.tsv and prints the Python data region.

The region is the largest cluster of exe strings (20 characters or more) that also occur in
a pristine source file that is mostly present in the exe. The strings of a source file are
literals only: macro text and preprocessor conditions are not evaluated, so every
pristine_missing row needs a look at its source line.

Ignored data: strings shorter than 3 characters, strings with bytes outside printable ASCII,
text in .text and .rsrc, and (for exe_only inside the region) strings that are mostly
not letters, for example pointer bytes. Strings that macros build (#x, ##) are not found in
the sources.
"""
import argparse
import collections
import hashlib
import os
import re
import struct
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "game", "stbc.exe")
SRC = os.path.join(ROOT, "third_party", "Python-1.5.2")
SOURCE_DIRS = ["Include", "Objects", "Python", "Parser", "Modules"]
MIN_LEN = 3
ANCHOR_LEN = 20
CLUSTER_GAP = 0x4000

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", "'": "'", '"': '"',
           "a": "\a", "b": "\b", "f": "\f", "v": "\v", "?": "?"}


def read_pe(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sections = []
    for i in range(count):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, pe + 24 + opt_size + 40 * i)
        sections.append((name.rstrip(b"\0").decode(), base + va, vsize, raw, rsize))
    return data, sections


def exe_strings(data, sections):
    """Return [(address, bytes)] for each NUL-terminated printable run that follows a NUL byte."""
    found = []
    for name, va, vsize, raw, rsize in sections:
        if name not in (".data", ".rdata"):
            continue
        blob = data[raw:raw + min(vsize, rsize)]
        for m in re.finditer(rb"(?<=\0)[\x09\x0a\x0d\x20-\x7e]{%d,}(?=\0)" % MIN_LEN, blob):
            found.append((va + m.start(), m.group()))
    return found


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def unescape(body):
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        c = body[i]
        if c in "01234567":
            j = i
            while j < len(body) and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(chr(int(body[i:j], 8) & 0xFF))
            i = j
        elif c == "x":
            j = i + 1
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(body[i + 1:j], 16) & 0xFF))
            i = j
        elif c == "\n":
            i += 1
        else:
            out.append(ESCAPES.get(c, c))
            i += 1
    return "".join(out)


def c_literals(text):
    """Return the string literals of C text. Adjacent literals join as one."""
    text = strip_comments(text)
    text = re.sub(r"\\\n", "", text)
    result = []
    for m in re.finditer(r'(?:"(?:[^"\\\n]|\\.)*"\s*)+', text):
        parts = re.findall(r'"((?:[^"\\\n]|\\.)*)"', m.group())
        result.append(unescape("".join(parts)))
    return result


def pristine_strings(tarball):
    """Return {string: set(file)}.

    With a tarball, all of SOURCE_DIRS and PC/ come from it (the tree in third_party lacks
    some modules). Without a tarball, the tree in third_party is used and PC/ is missing.
    """
    table = collections.defaultdict(set)

    def add(rel, text):
        for s in c_literals(text):
            table[s].add(rel)

    if tarball and os.path.exists(tarball):
        with tarfile.open(tarball) as tar:
            for member in tar.getmembers():
                parts = member.name.split("/")
                if len(parts) >= 3 and parts[1] in SOURCE_DIRS + ["PC"] and member.name.endswith((".c", ".h")):
                    add("/".join(parts[1:]), tar.extractfile(member).read().decode("latin1"))
        return table
    for sub in SOURCE_DIRS:
        for dirpath, _, names in os.walk(os.path.join(SRC, sub)):
            for name in sorted(names):
                if name.endswith((".c", ".h")):
                    path = os.path.join(dirpath, name)
                    add(os.path.relpath(path, SRC), open(path, encoding="latin1").read())
    return table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tarball", default=None, help="py152.tgz; gives Modules/ and PC/ in full")
    ap.add_argument("--out", default=os.path.join(ROOT, "re", "out", "py152_strings"))
    ap.add_argument("--selftest", action="store_true", help="plant two defects and check that they are reported")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if args.tarball:
        expected = open(os.path.join(ROOT, "third_party", "Python-1.5.2.sha256")).readline().split()[0]
        if hashlib.sha256(open(args.tarball, "rb").read()).hexdigest() != expected:
            sys.exit("tarball does not match third_party/Python-1.5.2.sha256")

    data, sections = read_pe(EXE)
    exe = exe_strings(data, sections)
    pristine = pristine_strings(args.tarball)
    if args.selftest:
        # Defect 1: a string only in the exe. Defect 2: a pristine string that the exe lacks.
        exe.append((0x1, b"PLANTED EXE ONLY STRING"))
        pristine["PLANTED PRISTINE ONLY STRING"] = {"Python/planted.c"}
    lit = {s.encode("latin1", "replace"): files for s, files in pristine.items()}
    image = b"".join(data[raw:raw + min(vsize, rsize)] for name, va, vsize, raw, rsize in sections
                     if name in (".data", ".rdata"))

    def printable(b):
        return len(b) >= MIN_LEN and all(32 <= c < 127 or c in (9, 10, 13) for c in b)

    # A source file counts as compiled into the exe when most of its strings are in the image.
    per_file = collections.defaultdict(lambda: [0, 0])
    missing = []
    for s, files in sorted(pristine.items()):
        b = s.encode("latin1", "replace")
        if not printable(b):
            continue
        found = b in image
        for fl in files:
            per_file[fl][1] += 1
            per_file[fl][0] += found
        if not found:
            missing.append((s, files))
    compiled = {fl for fl, (a, b) in per_file.items() if b >= 3 and a * 2 >= b}

    matched = [(a, s) for a, s in exe if s in lit]
    anchors = sorted(a for a, s in matched if len(s) >= ANCHOR_LEN and lit[s] & compiled
                     and lit[s] != {"Modules/errnomodule.c"})
    clusters = []
    for a in anchors:
        if clusters and a - clusters[-1][-1] <= CLUSTER_GAP:
            clusters[-1].append(a)
        else:
            clusters.append([a])
    region = max(clusters, key=len)
    low, high = region[0], region[-1]
    print("exe strings: %d, pristine literals: %d, matched: %d" % (len(exe), len(pristine), len(matched)))
    print("python region (largest anchor cluster, gap <= 0x%x): 0x%x - 0x%x, %d anchors"
          % (CLUSTER_GAP, low, high, len(region)))

    def noise(text):
        letters = sum(chr(c).isalpha() or c == 32 for c in text)
        return len(text) <= 4 or letters * 2 < len(text)

    with open(os.path.join(args.out, "exe_only.tsv"), "w") as f:
        f.write("address\tin_region\tstring\n")
        for a, s in exe:
            if s not in lit and not (low <= a <= high and noise(s)):
                f.write("0x%x\t%d\t%r\n" % (a, low <= a <= high, s.decode("latin1")))
    with open(os.path.join(args.out, "pristine_missing.tsv"), "w") as f:
        f.write("string\tfiles\tcompiled\n")
        for s, files in missing:
            if s.endswith((".h", '.h"')) or len(s) < MIN_LEN:
                continue
            f.write("%r\t%s\t%d\n" % (s, ",".join(sorted(files)), bool(files & compiled)))
    with open(os.path.join(args.out, "per_file.tsv"), "w") as f:
        f.write("file\tfound\ttotal\tcompiled\n")
        for fl, (a, b) in sorted(per_file.items()):
            f.write("%s\t%d\t%d\t%d\n" % (fl, a, b, fl in compiled))
    print("written to", args.out)
    if args.selftest:
        exe_only = open(os.path.join(args.out, "exe_only.tsv")).read()
        missing_text = open(os.path.join(args.out, "pristine_missing.tsv")).read()
        found = "PLANTED EXE ONLY STRING" in exe_only and "PLANTED PRISTINE ONLY STRING" in missing_text
        print("selftest:", "both planted defects reported" if found else "FAILED: a planted defect is missing")
        return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
