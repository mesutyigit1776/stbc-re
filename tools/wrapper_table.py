"""Write a table of Appc wrappers from the bytes of game/stbc.exe.

Usage: python3 -I tools/wrapper_table.py --prefix NAME [--prefix NAME ...] --evidence ID --out FILE

The wrappers are the rows of target/appc_method_table.tsv whose name starts with a prefix. The disassembly comes from
llvm-objdump. For each wrapper the table gives the format string, the argument kinds with the SWIG type
variables, the direct engine calls or the virtual call, the range of the inline code, and the return helper.
The module attributes g_k... have a getter and a setter row each (target/appc_globals.tsv).
"""
import argparse
import csv
import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(REPO, "game", "stbc.exe")
PARSE, CONVERT, NEW_POINTER = 0x74E310, 0x5BAE00, 0x5BB0E0
INT_HELPER, FLOAT_HELPER, STRING_HELPER = 0x74C6E0, 0x74FA70, 0x7506C0
TEXT_HELPER = 0x7507D0
ERROR_FORMAT = 0x753260  # PyErr_Format, called for a text argument of the wrong type
NONE_OBJECT = 0x95DD20
ALLOCATORS = {0x718CB0, 0x718CF0, 0x717B70, 0x718010}
HELPERS = {PARSE, CONVERT, NEW_POINTER, INT_HELPER, FLOAT_HELPER, STRING_HELPER, TEXT_HELPER, ERROR_FORMAT}
TYPE_NAME_LIST, TYPE_SLOT_ARRAY = 0x900A94, 0x991068
COLUMNS = ["wrapper", "address", "format", "args", "engine call", "inline range", "return kind", "return helper",
           "evidence id", "used_by_mods"]


class Image:
    def __init__(self):
        self.data = open(EXE, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional = struct.unpack_from("<H", self.data, pe + 20)[0]
        base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            _, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", self.data, pe + 24 + optional + 40 * i)
            self.sections.append((base + va, vsize, raw, rsize))

    def read(self, address, length):
        for va, vsize, raw, rsize in self.sections:
            if va <= address < va + vsize and address - va < rsize:
                return self.data[raw + address - va:raw + address - va + length]
        return None

    def u32(self, address):
        raw = self.read(address, 4)
        return struct.unpack("<I", raw)[0] if raw else None

    def text(self, address):
        raw = self.read(address, 96)
        return raw.split(b"\0")[0].decode("latin1") if raw else None


def disassemble(start, stop):
    out = subprocess.run(["xcrun", "llvm-objdump", "-d", "--x86-asm-syntax=intel", "--no-show-raw-insn",
                          "--start-address=0x%x" % start, "--stop-address=0x%x" % stop, EXE],
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        match = re.match(r"\s+([0-9a-f]+):\s+(.*)", line)
        if match and not match.group(2).strip().startswith(("nop", "int3")):
            rows.append((int(match.group(1), 16), re.sub(r"\s+", " ", match.group(2).strip())))
    return rows


def type_slots(image):
    slots, index = {}, 0
    while True:
        descriptor = image.u32(TYPE_NAME_LIST + 4 * index)
        if not descriptor:
            break
        slots[TYPE_SLOT_ARRAY + 4 * index] = image.text(image.u32(descriptor))
        index += 1
    return slots


def read_tsv(path):
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def facts(image, address, end, slots):
    code = disassemble(address, end)
    calls = [(a, int(t.split()[1], 16)) for a, t in code if t.startswith("call 0x")]
    fmt = None
    for i, (a, t) in enumerate(code):
        if t.startswith("call 0x") and int(t.split()[1], 16) == PARSE:
            for b, u in reversed(code[:i]):
                m = re.match(r"push\s+(0x[0-9a-f]+)$", u)
                if m and image.text(int(m.group(1), 16)) and ":" in image.text(int(m.group(1), 16)):
                    fmt = image.text(int(m.group(1), 16))
                    break
            break
    types = []
    for i, (a, t) in enumerate(code):
        if t.startswith("call 0x") and int(t.split()[1], 16) == CONVERT:
            for b, u in reversed(code[:i]):
                m = re.search(r"\[(0x99[0-9a-f]{4})\]", u)
                if m and u.startswith("mov e"):
                    types.append(int(m.group(1), 16))
                    break
    returns = []
    for a, t in code:
        m = re.search(r"\[(0x99[0-9a-f]{4})\]", t)
        if m and t.startswith("mov e") and int(m.group(1), 16) in slots and int(m.group(1), 16) not in types:
            returns.append(int(m.group(1), 16))
    return code, calls, fmt, types, returns


def argument_text(fmt, types, texts, slots):
    letters, optional, kinds = fmt.split(":")[0], False, []
    pointer = iter(types)
    string_positions = texts
    position = 0
    for letter in letters:
        if letter == "|":
            optional = True
            continue
        if letter == "O":
            kind = "str|None" if position in string_positions else "ptr@0x%x" % next(pointer)
        else:
            kind = {"i": "int", "l": "long", "f": "float"}[letter]
        kinds.append(kind + ("?" if optional else ""))
        position += 1
    return ",".join(kinds)


def text_positions(code, fmt):
    """The positions of the O arguments that the wrapper reads as text. A text argument is checked against None and
    the string type, then read with PyString_AsString; the n-th such argument is the n-th text call."""
    return None


def payload_range(code, targets):
    """The inline code of a wrapper: from the code behind the last conversion check to the code before the call that
    makes the Python result."""
    start = None
    last_convert = max((i for i, (a, t) in enumerate(code) if t.startswith("call 0x") and int(t.split()[1], 16) == CONVERT),
                       default=-1)
    for address, text in code[last_convert + 1:]:
        match = re.match(r"jne (0x[0-9a-f]+)", text)
        if match:
            start = int(match.group(1), 16)
            break
    end = None
    for index, (address, text) in enumerate(code):
        if text.startswith("call 0x") and int(text.split()[1], 16) in (NEW_POINTER, INT_HELPER, FLOAT_HELPER,
                                                                         STRING_HELPER):
            end = code[index - 1][0] if index > 0 else address
        if text.startswith("mov eax, dword ptr [0x95dd20]"):
            end = code[index - 1][0]
    if start is None or end is None or end < start:
        return ""
    return "0x%x-0x%x" % (start, end)


OVERRIDES = {
    "delete_TGIconGroup": "virtual: slot 0 (vtable+0x0) with argument 1 when this is not null",
    "TGIconGroup_GetName": "inline: reads the pointer at this+0x4; a null pointer gives None",
    "TGIconGroup_GetCount": "inline: reads the int at this+0x10, the count of the location table",
    "TGIconManager_GetScreenWidth": "inline: reads the float at this+0x58",
    "TGIconManager_GetScreenHeight": "inline: reads the float at this+0x5c",
    "TGIconManager_SetScreenWidth": "inline: stores the float at this+0x58",
    "TGIconManager_SetScreenHeight": "inline: stores the float at this+0x5c",
    "TGIconManager_GetScale": "inline: reads the float at this+0x54",
    "TGIconManager_SetScale": "inline: stores the float at this+0x54",
    "TGIconManager_GetPixelCenterAdj": "inline: reads the float at this+0x60",
    "TGIconManager_SetCamera": "inline: replaces the camera at this+0x50 with reference counts; the old camera is "
                               "deleted through its slot 10 (offset 0x28) when its count reaches 0",
}


def row_for(image, name, address, end, slots, evidence, used):
    code, calls, fmt, types, returns = facts(image, address, end, slots)
    targets = [t for _, t in calls]
    letters = fmt.split(":")[0].replace("|", "")
    texts = targets.count(TEXT_HELPER)
    # The O arguments that are not converted as pointers are text arguments: this wrapper converts len(types).
    o_positions = [i for i, c in enumerate(letters) if c == "O"]
    pointer_count = len(types)
    # Pointer arguments are the first ones when `this` is a pointer; text arguments follow in call order.
    string_positions = set(o_positions[pointer_count:]) if texts else set()
    args = argument_text(fmt, types, string_positions, slots)
    engine = [t for t in targets if t not in HELPERS]
    virtual = [(a, t) for a, t in code if re.match(r"call dword ptr \[e[a-d]x( \+ 0x[0-9a-f]+)?\]", t)]
    inline_range = ""
    if engine:
        engine_call = ",".join("0x%x" % t for t in engine)
    else:
        engine_call = None
        inline_range = payload_range(code, targets)
    if virtual and not engine:
        offset_match = re.search(r"\+ (0x[0-9a-f]+)", virtual[0][1])
        offset = int(offset_match.group(1), 16) if offset_match else 0
        engine_call = "virtual: slot %d (vtable+0x%x)" % (offset // 4, offset)
    if name in OVERRIDES:
        engine_call = OVERRIDES[name]
    if engine_call is None:
        engine_call = "inline: see the range"
    if NEW_POINTER in targets:
        kind = "pointer string"
        helper = "SWIG_NewPointerObj 0x5bb0e0 (type global 0x%x)" % returns[-1] if returns else "SWIG_NewPointerObj 0x5bb0e0"
    elif STRING_HELPER in targets:
        kind, helper = "string", "PyString_FromString 0x7506c0 (Py_None 0x95dd20 when the pointer is null)"
    elif FLOAT_HELPER in targets:
        kind, helper = "float", "PyFloat_FromDouble 0x74fa70"
    elif INT_HELPER in targets:
        kind, helper = "int", "PyInt_FromLong 0x74c6e0"
    else:
        kind, helper = "None", "Py_None 0x95dd20 (refcount at +0 incremented)"
    return [name, "0x%x" % address, fmt, args, engine_call, inline_range, kind, helper, evidence, used]


def variable_rows(image, name, slots, evidence):
    """The getter row and the setter row of a module attribute."""
    row = next(r for r in read_tsv(os.path.join(REPO, "target", "appc_globals.tsv")) if r["name"] == name)
    getter, setter, variable = int(row["getter_address"], 16), int(row["setter_address"], 16), int(row["variable_address"], 16)
    set_code = disassemble(setter, setter + 0x80)
    set_end = next(a for a, t in set_code if t == "ret")
    get_code = disassemble(getter, getter + 0x20)
    type_global = int(re.search(r"\[(0x99[0-9a-f]{4})\]", get_code[0][1]).group(1), 16)
    push = next(a for a, t in get_code if t == "push 0x%x" % variable)
    copies = [t for _, t in set_code if t.startswith(("rep movs", "mov edi, 0x", "mov esi, 0x"))]
    note = ("inline: copies the members of the object at +0x4 and later to the global object 0x%x; the first dword "
            "(vtable pointer) is not copied" % variable if copies else
            "inline: converts the argument to the object pointer and stores nothing; the global object 0x%x stays as it "
            "is" % variable)
    return [
        ["%s (set)" % name, "0x%x" % setter, "(none: module attribute setter)", "ptr@0x%x" % type_global, note,
         "0x%x-0x%x" % (setter, set_end), "int (C int 0 on success, 1 on conversion failure; no Python object)", "none",
         evidence, "yes"],
        ["%s (get)" % name, "0x%x" % getter, "(none: module attribute getter)", "",
         "inline: address of the global object 0x%x" % variable, "0x%x-0x%x" % (push, push), "pointer string",
         "SWIG_NewPointerObj 0x5bb0e0 (type global 0x%x)" % type_global, evidence, "yes"],
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--prefix", action="append", required=True)
    parser.add_argument("--variable", action="append", default=[])
    parser.add_argument("--evidence", required=True)
    parser.add_argument("--out", required=True)
    options = parser.parse_args()
    image = Image()
    slots = type_slots(image)
    table = read_tsv(os.path.join(REPO, "target", "appc_method_table.tsv"))
    uses = {r["appc_function"]: r for r in read_tsv(os.path.join(REPO, "target", "wrapper_use.tsv"))} \
        if os.path.exists(os.path.join(REPO, "target", "wrapper_use.tsv")) else {}
    addresses = sorted({int(r["function_address"], 16) for r in table})
    rows = []
    for entry in table:
        name = entry["name"]
        if not any(name.startswith(p) for p in options.prefix):
            continue
        address = int(entry["function_address"], 16)
        later = [a for a in addresses if a > address]
        used = uses.get(name, {}).get("used_by_mods", "yes")
        rows.append(row_for(image, name, address, later[0] if later else address + 0x200, slots, options.evidence, used))
    for variable in options.variable:
        rows.extend(variable_rows(image, variable, slots, options.evidence))
    with open(options.out, "w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(COLUMNS)
        writer.writerows(rows)
    print("wrote %d rows to %s" % (len(rows), options.out))


if __name__ == "__main__":
    sys.exit(main())
