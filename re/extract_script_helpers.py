"""Read the helper registrations of the application start-up function 0x43ad70 of stbc.exe.

The function calls 0x6f9bc0 33 times. Each call pushes the method name, the class name and the code address.
Writes target/script_helpers.tsv.
Run with: python3 -I re/extract_script_helpers.py
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import api_surface  # noqa: E402

START, END = 0x43AD70, 0x43B300
REGISTER = 0x6F9BC0
EVIDENCE = "E0147"
HEADER = ["index", "class", "method", "code_address", "call_address", "evidence_id"]


def main():
    exe = api_surface.Pe(os.path.join(ROOT, "game", "stbc.exe"))
    code = exe.data
    rows = []
    address = START
    while address < END:
        o = exe.offset(address)
        if (code[o] == 0x68 and code[o + 5] == 0x68 and code[o + 10] == 0x68 and code[o + 15] == 0xE8):
            method, cls, target = struct.unpack_from("<III", bytes([code[o + 1 + 5 * i + j] for i in range(3) for j in range(4)]))
            rel = struct.unpack_from("<i", code, o + 16)[0]
            if (address + 20 + rel) & 0xFFFFFFFF == REGISTER:
                rows.append([str(len(rows)), exe.cstring(cls), exe.cstring(method), "0x%x" % target,
                             "0x%x" % (address + 15), EVIDENCE])
                address += 20
                continue
        address += 1
    path = os.path.join(ROOT, "target", "script_helpers.tsv")
    with open(path, "w") as f:
        f.write("\t".join(HEADER) + "\n")
        for row in rows:
            f.write("\t".join(row) + "\n")
    print("helpers: %d" % len(rows))


if __name__ == "__main__":
    main()
