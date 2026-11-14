"""Write re/out/seeds_python.tsv for REA_GHIDRA_SEED_FILE.

The file holds the rows of re/out/seeds.tsv and one row for each address in the Python
code range that is the target of a CALL or JMP rel32 in .text or a pointer in .data or .rdata.
Ghidra misses these functions, because only tables point at many of them.

Run with: python3 -I re/make_python_seeds.py   (run re/make_seeds.py first)
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "game", "stbc.exe")
BASE_SEEDS = os.path.join(ROOT, "re", "out", "seeds.tsv")
OUT = os.path.join(ROOT, "re", "out", "seeds_python.tsv")
LOW, HIGH = 0x74A000, 0x78E000


def main():
    data = open(EXE, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    targets = set()
    for i in range(count):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, pe + 24 + opt_size + 40 * i)
        name = name.rstrip(b"\0")
        blob = data[raw:raw + min(vsize, rsize)]
        if name == b".text":
            for opcode in (b"\xe8", b"\xe9"):
                at = blob.find(opcode)
                while at >= 0:
                    if at + 5 <= len(blob):
                        rel = struct.unpack_from("<i", blob, at + 1)[0]
                        target = (base + va + at + 5 + rel) & 0xFFFFFFFF
                        if LOW <= target < HIGH:
                            targets.add(target)
                    at = blob.find(opcode, at + 1)
        elif name in (b".data", b".rdata"):
            for at in range(0, len(blob) - 3, 4):
                word = struct.unpack_from("<I", blob, at)[0]
                if LOW <= word < HIGH:
                    targets.add(word)
    known = set()
    rows = []
    if os.path.exists(BASE_SEEDS):
        for line in open(BASE_SEEDS):
            rows.append(line)
            known.add(int(line.split("\t")[0], 16))
    with open(OUT, "w") as f:
        f.writelines(rows)
        for target in sorted(targets - known):
            f.write("0x%x\tfunction\n" % target)
    print("seeds: %d from the base file, %d new" % (len(known), len(targets - known)))


if __name__ == "__main__":
    sys.exit(main())
