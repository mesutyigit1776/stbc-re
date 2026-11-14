"""Write re/out/seeds.tsv for REA_GHIDRA_SEED_FILE from re/names.tsv and re/function_starts.tsv.

Run with: python3 -I re/make_seeds.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = os.path.join(ROOT, "re", "names.tsv")
STARTS = os.path.join(ROOT, "re", "function_starts.tsv")
OUT = os.path.join(ROOT, "re", "out", "seeds.tsv")
ADDRESS = re.compile(r"^0x[0-9a-f]+$")
NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::~?[A-Za-z_][A-Za-z0-9_]*)*$")


def read_rows(path):
    with open(path) as f:
        header = f.readline().rstrip("\n").split("\t")
        for number, line in enumerate(f, 2):
            line = line.rstrip("\n")
            if line:
                yield number, dict(zip(header, line.split("\t")))


def main():
    names = {}
    renamed = []
    for number, row in read_rows(NAMES):
        address, name = row["address"], row["name"]
        if not ADDRESS.match(address) or not NAME.match(name):
            sys.exit("%s:%d: bad address or name" % (NAMES, number))
        if address in names:
            renamed.append((address, names[address], name))
        else:
            names[address] = name

    starts = {}
    if os.path.exists(STARTS):
        for number, row in read_rows(STARTS):
            if not ADDRESS.match(row["address"]):
                sys.exit("%s:%d: bad address" % (STARTS, number))
            starts[row["address"]] = None

    seeds = dict.fromkeys(starts)
    seeds.update(names)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        for address in sorted(seeds, key=lambda a: int(a, 16)):
            name = seeds[address]
            f.write("%s\tfunction%s\n" % (address, "\t" + name if name else ""))
    print("seeds: %d (%d named, %d unnamed starts)"
          % (len(seeds), len(names), len(set(starts) - set(names))))
    for address, kept, dropped in renamed:
        print("kept %s for %s, dropped %s" % (kept, address, dropped))


if __name__ == "__main__":
    main()
