"""Read the SWIG type tables of stbc.exe and write target/appc_swig_types.tsv.

The Appc module init code registers the tables listed at 0x900a94 and stores the results at 0x991068.
Each table entry has 20 bytes: name, converter, string, next, prev.
Run with: python3 -I re/extract_appc_swig.py
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import api_surface  # noqa: E402

TABLE_LIST = 0x900A94
RESULT_ARRAY = 0x991068
ENTRY_SIZE = 20
EVIDENCE = "E0136"
IDENTITY = bytes.fromhex("8b442404c3")
OFFSET_PREFIX = bytes.fromhex("8b44240485c074")
HEADER = ["type_index", "variable_address", "table_address", "entry_index", "name", "converter_address",
          "converter_kind", "converter_offset", "type_string", "evidence_id"]


def word(exe, address):
    return exe.u32(address)[0]


def converter_kind(exe, address):
    """Classify a cast function: identity, or add N to a non-null pointer."""
    code = exe.data
    o = exe.offset(address)
    if code[o:o + len(IDENTITY)] == IDENTITY:
        return "identity", ""
    if code[o:o + len(OFFSET_PREFIX)] == OFFSET_PREFIX:
        skip = code[o + len(OFFSET_PREFIX)]
        tail = code[o + len(OFFSET_PREFIX) + 1:o + len(OFFSET_PREFIX) + 1 + skip]
        if skip == 4 and tail[0:2] == b"\x83\xc0":
            return "offset", str(tail[2])
        if skip == 6 and tail[0] == 0x05:
            return "offset", str(struct.unpack_from("<I", tail, 1)[0])
    return "other", ""


def main():
    exe = api_surface.Pe(os.path.join(ROOT, "game", "stbc.exe"))
    tables = []
    while True:
        pointer = word(exe, TABLE_LIST + 4 * len(tables))
        if pointer == 0:
            break
        tables.append(pointer)
    rows = []
    for index, table in enumerate(tables):
        entry = table
        entry_index = 0
        while word(exe, entry) != 0:
            name = exe.cstring(word(exe, entry))
            converter = word(exe, entry + 4)
            text_address = word(exe, entry + 8)
            kind, offset = ("none", "") if converter == 0 else converter_kind(exe, converter)
            text = exe.cstring(text_address) if text_address else ""
            rows.append([str(index), "0x%x" % (RESULT_ARRAY + 4 * index), "0x%x" % table, str(entry_index), name,
                         "0x%x" % converter if converter else "", kind, offset, text, EVIDENCE])
            entry += ENTRY_SIZE
            entry_index += 1
    path = os.path.join(ROOT, "target", "appc_swig_types.tsv")
    with open(path, "w") as f:
        f.write("\t".join(HEADER) + "\n")
        for row in rows:
            f.write("\t".join(row) + "\n")
    print("types: %d, entries: %d" % (len(tables), len(rows)))


if __name__ == "__main__":
    main()
