"""Compare the generated Appc wrappers with stbc.exe and with the recorded wrapper tables.

Usage: python3 -I tools/appcgen/check.py [--out DIR] [--selftest]

Reads: generated code in --out (made by appcgen.py), target/math_wrappers.tsv,
target/property_wrappers.tsv, target/icon_wrappers.tsv, target/font_wrappers.tsv (made by tools/wrapper_table.py), target/appc_method_table.tsv, target/appc_globals.tsv, re/names.tsv,
the vtable and layout files in re/layouts and the bytes of game/stbc.exe.
Exit status is 1 on any mismatch.

Compared for each method wrapper: the format string, the pointer types, the number of converted
pointers and string arguments, the return kind and type, the type globals in load order, the
table name and flags, and the engine call (the method name of a single named engine function,
or the vtable slot of a virtual call). Compared for each module attribute: the registration (name,
getter and setter), the type, the object address and the copied ranges.

Data that this check ignores: the C++ call or inline expression of each wrapper (only the
method name of a single named engine call is compared), the order of float arguments, the
order of the string arguments of one call, the default value of optional arguments, reference counting, and the bodies of the engine functions.
"""

import argparse
import csv
import hashlib
import os
import re
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appcgen  # noqa: E402

REPO = appcgen.REPO
EXE = os.path.join(REPO, "game", "stbc.exe")

PARSE_TUPLE = 0x74E310
CONVERT_PTR = 0x5BAE00
NEW_POINTER_OBJ = 0x5BB0E0
FLOAT_FROM_DOUBLE = 0x74FA70
PY_INT_FROM_LONG = 0x74C6E0
PY_NONE = 0x95DD20
ALLOCATORS = {0x718CB0, 0x718CF0, 0x717B70, 0x718010}  # pool allocation: sized find and take, free
STRING_AS_STRING = 0x7507D0
PY_STRING_FROM_STRING = 0x7506C0
ADD_VAR_LINK = 0x5BAD80
INIT_START, INIT_END = 0x65A250, 0x65B13A

TYPE_NAME_LIST = 0x900A94
TYPE_SLOT_ARRAY = 0x991068
TYPE_LOOP_ADDRESS = 0x65A27B
TYPE_LOOP_PARTS = ("a1940a9000", "898668109900", "8b86980a9000")

CLASS_PATTERN = re.compile(
    r"^(?:new_|delete_)?(NiPoint3|TGPoint3|NiColorA|TGColorA|TGObject|TGModelProperty|TGModelPropertyManager"
    r"|TGModelPropertySet|SubsystemProperty|TGIconGroup|TGIconManager)(?:_|$)")
# Wrappers of a class that is described in part: the names of the wrappers that exist.
PARTIAL_NAMES = {"TGFontManager_RegisterFont"}
WRAPPER_FILES = ("math_wrappers.tsv", "property_wrappers.tsv", "icon_wrappers.tsv", "font_wrappers.tsv")
COLUMN_NAMES = {"address": "wrapper_address", "engine call": "engine_call", "inline range": "inline_range",
                "return kind": "return_kind", "return helper": "return_helper", "evidence id": "evidence_id"}
VARIABLE_PATTERN = re.compile(r"^(\S+) \((get|set)\)$")


class Exe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            _, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", self.data, pe + 24 + optional_size + 40 * i)
            self.sections.append((base + va, vsize, raw, rsize))

    def offset(self, address):
        for va, vsize, raw, rsize in self.sections:
            if va <= address < va + vsize:
                return raw + address - va if address - va < rsize else None
        return None

    def read(self, address, length):
        start = self.offset(address)
        return self.data[start:start + length] if start is not None else None

    def u32(self, address):
        raw = self.read(address, 4)
        return struct.unpack("<I", raw)[0] if raw else None

    def cstring(self, address, limit=96):
        raw = self.read(address, limit)
        if raw is None:
            return None
        return raw.split(b"\0")[0].decode("latin1")




def read_tsv(path, renames=None):
    """Rows of a tab separated file. A column in renames gets its new name; spaces in names become underscores."""
    renames = renames or {}
    with open(path, newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    return [{renames.get(key, key).replace(" ", "_"): value for key, value in row.items()} for row in rows]


def sha256_ok():
    expected = open(os.path.join(REPO, "checksums", "stbc.exe.sha256")).read().split()[0]
    return hashlib.sha256(open(EXE, "rb").read()).hexdigest() == expected


def read_type_slots(exe):
    """Map each type global to its SWIG type name, as the loop in initAppc does."""
    loop = exe.read(TYPE_LOOP_ADDRESS, 0x28)
    if loop is None or not all(bytes.fromhex(part) in loop for part in TYPE_LOOP_PARTS):
        raise SystemExit("check: the type register loop at 0x65a27b is not the expected code")
    slots = {}
    index = 0
    while True:
        descriptor = exe.u32(TYPE_NAME_LIST + 4 * index)
        if not descriptor:
            break
        name = exe.cstring(exe.u32(descriptor))
        slots[TYPE_SLOT_ARRAY + 4 * index] = name
        index += 1
    return slots


def function_bodies(text, pattern):
    return {m.group(1): m.group(2) for m in re.finditer(pattern, text, re.S | re.M)}


def parse_generated(out_dir):
    """Return {function name: info}, the method table rows (name, function, flags) and the variable rows."""
    functions, setters, getters = {}, {}, {}
    for name in sorted(os.listdir(out_dir)):
        if not (name.startswith("appc_") and name.endswith(".cpp")):
            continue
        text = open(os.path.join(out_dir, name)).read()
        for function, body in function_bodies(text, r"^PyObject \*(Appc_\w+)\(PyObject \*, PyObject \*args\)\n\{\n(.*?)^\}").items():
            parse = re.search(r'PyArg_ParseTuple\(args, APPC_CSTR\("([^"]*)"\)', body)
            converts = re.findall(r"SWIG_ConvertPtr\(\w+, \(void \*\*\)&\w+, SWIGTYPE(\w+),", body)
            new_object = re.search(r"SWIG_NewPointerObj\(result, SWIGTYPE(\w+)\)", body)
            message = re.search(r'PyErr_Format\(PyExc_TypeError, APPC_CSTR\("([^"]*)"\)', body)
            if "PyString_FromString" in body:
                kind, result_type = "string", None
            elif "return Py_None;" in body:
                kind, result_type = "None", None
            elif "PyFloat_FromDouble" in body:
                kind, result_type = "float", None
            elif "PyInt_FromLong" in body:
                kind, result_type = "int", None
            elif new_object:
                kind, result_type = "pointer", new_object.group(1)
            else:
                kind, result_type = "unknown", None
            functions[function] = {
                "format": parse.group(1) if parse else None,
                "arg_types": converts,
                "texts": body.count("PyString_AsString("),
                "text_message": message.group(1) if message else None,
                "return_kind": kind,
                "return_type": result_type,
                "body": body,
            }
        setters.update(function_bodies(text, r"^int (Appc_set_\w+)\(PyObject \*value\)\n\{\n(.*?)^\}"))
        getters.update(function_bodies(text, r"^PyObject \*(Appc_get_\w+)\(void\)\n\{\n(.*?)^\}"))
    rows = []
    inc = open(os.path.join(out_dir, "appc_methods.inc")).read()
    for match in re.finditer(r'\{ APPC_CSTR\("(\w+)"\), (Appc_\w+), (\d+), NULL \},', inc):
        rows.append((match.group(1), match.group(2), int(match.group(3))))
    variables = []
    inc = open(os.path.join(out_dir, "appc_variables.inc")).read()
    for match in re.finditer(r'\{ APPC_CSTR\("(\w+)"\), (Appc_get_\w+), (Appc_set_\w+) \},', inc):
        variables.append({"name": match.group(1), "getter": getters.get(match.group(2)),
                          "setter": setters.get(match.group(3))})
    return functions, rows, variables


TEXT_START, TEXT_END = 0x401000, 0x888000


def call_targets(address, code):
    """The CALL rel32 sites of the code. A byte 0xE8 inside an operand gives a target outside the code and is dropped."""
    sites = [(i, (address + i + 5 + struct.unpack_from("<i", code, i + 1)[0]) & 0xFFFFFFFF)
             for i in range(len(code) - 4) if code[i] == 0xE8]
    return [(i, target) for i, target in sites if TEXT_START <= target < TEXT_END]


def type_slot_loads(code, slots):
    """The type globals that the code loads into a register, in order of first use."""
    found = []
    for i in range(len(code) - 5):
        value = None
        if code[i] == 0xA1:
            value = struct.unpack_from("<I", code, i + 1)[0]
        elif code[i] == 0x8B and code[i + 1] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            value = struct.unpack_from("<I", code, i + 2)[0]
        if value in slots and slots[value] not in found:
            found.append(slots[value])
    return found


def exe_wrapper_facts(exe, address, end, slots):
    """Read format, converter count, return kind, string arguments and type globals from the wrapper bytes."""
    code = exe.read(address, end - address)
    facts = {"format": None, "converts": 0, "texts": 0, "text_message": None, "return_kind": None, "types": [], "calls": [],
             "code": code}
    if code is None:
        return facts
    call_sites = call_targets(address, code)
    parse_call = next((i for i, t in call_sites if t == PARSE_TUPLE), None)
    if parse_call is not None:
        for i in range(parse_call - 1, -1, -1):
            if i + 5 <= parse_call + 1 and code[i] == 0x68:
                text = exe.cstring(struct.unpack_from("<I", code, i + 1)[0])
                if text and ":" in text and text.isprintable():
                    facts["format"] = text
                    break
    targets = [t for _, t in call_sites]
    facts["converts"] = targets.count(CONVERT_PTR)
    facts["texts"] = targets.count(STRING_AS_STRING)
    for i in range(len(code) - 4):
        if code[i] == 0x68:
            text = exe.cstring(struct.unpack_from("<I", code, i + 1)[0])
            if text and text.startswith("Expected None or string"):
                facts["text_message"] = text
    if NEW_POINTER_OBJ in targets:
        facts["return_kind"] = "pointer"
    elif PY_STRING_FROM_STRING in targets:
        facts["return_kind"] = "string"
    elif FLOAT_FROM_DOUBLE in targets:
        facts["return_kind"] = "float"
    elif PY_INT_FROM_LONG in targets:
        facts["return_kind"] = "int"
    elif bytes([0xB8]) + struct.pack("<I", PY_NONE) in code:
        facts["return_kind"] = "None"
    facts["types"] = type_slot_loads(code, slots)
    facts["calls"] = targets
    return facts


def tsv_arguments(args_text, slots):
    """(format characters, pointer type names, number of string arguments) from the args column."""
    chars, types, texts = "", [], 0
    for part in args_text.split(","):
        part = part.strip().rstrip("?")
        if part == "none" or not part:
            continue
        if part.startswith("ptr@"):
            chars += "O"
            types.append(slots.get(int(part[4:], 16)))
        elif part.startswith("str"):
            chars += "O"
            texts += 1
        elif part.startswith("float"):
            chars += "f"
        elif part.startswith("int"):
            chars += "i"
        elif part.startswith("long"):
            chars += "l"
        else:
            chars += "?"
    return chars, types, texts


def format_chars(fmt):
    return fmt.split(":")[0].replace("|", "")


def normalise_kind(kind):
    if kind.startswith("float"):
        return "float"
    if kind.startswith("pointer"):
        return "pointer"
    return kind


def vtable_method(class_name, slot):
    """The method name of a vtable slot from the layout file of the class, or None."""
    path = os.path.join(REPO, "re", "layouts", class_name + ".vtable.tsv")
    if not os.path.exists(path):
        return None
    for row in read_tsv(path):
        if int(row["slot"]) == slot:
            return row["name"].split("::")[-1]
    return None


def has_virtual_call(code, offset):
    """True when the code has a CALL through a register plus the offset (FF /2 with a byte displacement)."""
    for i in range(len(code) - 2):
        if code[i] != 0xFF:
            continue
        mod, reg, rm = code[i + 1] >> 6, (code[i + 1] >> 3) & 7, code[i + 1] & 7
        if reg != 2 or rm == 4 or rm == 5 and mod == 0:
            continue
        if mod == 0 and offset == 0:
            return True
        if mod == 1 and i + 2 < len(code) and code[i + 2] == offset:
            return True
    return False


def check_engine_call(row, names, info, facts, slots, arg_types):
    """Compare the engine call of the tsv with the exe bytes and with the generated call."""
    text = row["engine_call"]
    virtual = re.search(r"virtual: (?:slot (\d+) \(vtable\+0x[0-9a-f]+\)|vtable\+0x([0-9a-f]+))", text)
    if virtual:
        slot = int(virtual.group(1)) if virtual.group(1) else int(virtual.group(2), 16) // 4
        if not has_virtual_call(facts["code"], 4 * slot):
            return "MISMATCH no virtual call at vtable+0x%x in the exe bytes" % (4 * slot)
        class_name = (arg_types[0] or "")[3:] if arg_types else ""
        method = vtable_method(class_name, slot)
        if method is None:
            return "MISMATCH no vtable row for slot %d of %s" % (slot, class_name)
        if method == "DeletingDestructor":
            return "virtual slot %d (delete)" % slot if "delete a_this" in info["body"] else "MISMATCH slot 0 without delete"
        if not re.search(r"\b%s\(" % re.escape(method), info["body"]):
            return "MISMATCH virtual slot %d is %s, not found in generated code" % (slot, method)
        return "virtual slot %d, %s" % (slot, method)
    calls = [int(a, 16) for a in text.split(",") if re.fullmatch(r"\s*0x[0-9a-fA-F]+\s*", a)]
    named = [a for a in calls if a not in ALLOCATORS]
    if not named:
        return "no named engine call"
    missing = [hex(a) for a in named if a not in facts["calls"] and not row["inline_range"].strip()]
    if missing:
        return "MISMATCH engine call %s is not called in the exe bytes" % ", ".join(missing)
    if row["inline_range"].strip():
        return "skipped (calls and inline code)"
    if len(named) != 1 or "::" not in names.get(named[0], ""):
        return "skipped (engine call unnamed)"
    cls, method = names[named[0]].split("::")
    body = info["body"]
    if method == cls or method.startswith(cls + "_"):
        if not re.search(r"\bnew\b[^;]*\b%s\(" % re.escape(cls), body):
            return "MISMATCH constructor of %s not found in generated code" % cls
        return "constructs %s" % cls
    call_line = [line for line in body.splitlines() if re.search(r"\b%s\(" % re.escape(method), line)]
    if not call_line:
        return "MISMATCH engine call %s not found in generated code" % names[named[0]]
    if cls + "::" not in call_line[0] and "a_this->" not in call_line[0] and "(a_this" not in call_line[0]:
        return "MISMATCH class of %s" % names[named[0]]
    return "calls %s" % names[named[0]]


def check_method_wrapper(row, name, info, facts, slots, names, method_row):
    """Return the list of problems and the table cells for one wrapper row."""
    problems = []
    address = int(row["wrapper_address"], 16)
    if int(method_row["function_address"], 16) != address:
        problems.append("tsv address differs from appc_method_table.tsv")
    if method_row["flags"] != "1":
        problems.append("method table flags are not 1")
    expected_format = row["format"]
    if info["format"] != expected_format:
        problems.append("format generated %r, tsv %r" % (info["format"], expected_format))
    if facts["format"] != expected_format:
        problems.append("format in exe bytes %r, tsv %r" % (facts["format"], expected_format))

    chars, tsv_types, tsv_texts = tsv_arguments(row["args"], slots)
    if format_chars(expected_format) != chars:
        problems.append("tsv args %r do not match format %r" % (chars, expected_format))
    if info["arg_types"] != tsv_types:
        problems.append("argument types generated %s, exe %s" % (info["arg_types"], tsv_types))
    if facts["converts"] != len(tsv_types):
        problems.append("exe converts %d pointers, tsv lists %d" % (facts["converts"], len(tsv_types)))
    if info["texts"] != tsv_texts or facts["texts"] != tsv_texts:
        problems.append("string arguments generated %d, exe %d, tsv %d" % (info["texts"], facts["texts"], tsv_texts))
    if tsv_texts and (info["text_message"] != appcgen.TEXT_ERROR_MESSAGE or facts["text_message"] != appcgen.TEXT_ERROR_MESSAGE):
        problems.append("error text generated %r, exe %r" % (info["text_message"], facts["text_message"]))

    kind = normalise_kind(row["return_kind"])
    if info["return_kind"] != kind:
        problems.append("return kind generated %s, tsv %s" % (info["return_kind"], kind))
    if facts["return_kind"] != kind:
        problems.append("return kind in exe bytes %s, tsv %s" % (facts["return_kind"], kind))
    return_type = None
    if kind == "pointer":
        match = re.search(r"type global (0x[0-9a-f]+)", row["return_helper"])
        return_type = slots.get(int(match.group(1), 16)) if match else None
        if info["return_type"] != return_type:
            problems.append("return type generated %s, exe %s" % (info["return_type"], return_type))

    expected_types = []
    for type_name in tsv_types + ([return_type] if return_type else []):
        if type_name not in expected_types:
            expected_types.append(type_name)
    if facts["types"] != expected_types:
        problems.append("type globals in exe bytes %s, expected %s" % (facts["types"], expected_types))

    call_note = check_engine_call(row, names, info, facts, slots, tsv_types)
    if call_note.startswith("MISMATCH"):
        problems.append(call_note)
    cells = (name, row["wrapper_address"], expected_format, kind + (" " + return_type if return_type else ""), call_note)
    return problems, cells


def find_registration(exe, name):
    """The (getter, setter) pair that the init code of the exe registers for a module attribute, or None."""
    code = exe.read(INIT_START, INIT_END - INIT_START + 1)
    for i in range(16, len(code) - 5):
        if code[i] != 0xE8 or (INIT_START + i + 5 + struct.unpack_from("<i", code, i + 1)[0]) & 0xFFFFFFFF != ADD_VAR_LINK:
            continue
        if not (0x50 <= code[i - 1] <= 0x57 and code[i - 6] == 0x68 and code[i - 11] == 0x68 and code[i - 16] == 0x68):
            continue
        pushed = [struct.unpack_from("<I", code, i - offset + 1)[0] for offset in (6, 11, 16)]
        if exe.cstring(pushed[0]) == name:
            return pushed[1], pushed[2]
    return None


def layout_ranges(class_name):
    """(offset, size) of every field of the class except the vtable pointer, from the layout file."""
    ranges = []
    for row in read_tsv(os.path.join(REPO, "re", "layouts", class_name + ".tsv")):
        if row["offset"] != "sizeof" and row["field"] != "vtable":
            ranges.append((int(row["offset"], 16), int(row["size"])))
    return ranges


def function_extent(address, all_addresses, cap):
    """The end of the function: the next known function start, but not more than cap bytes."""
    later = [a for a in all_addresses if a > address]
    return min(later[0] if later else address + cap, address + cap)


def check_variable(generated, globals_row, exe, slots, type_name, class_name, all_addresses):
    """Compare one module attribute with the exe. Returns (problems, cells)."""
    name = generated["name"]
    problems = []
    getter, setter = int(globals_row["getter_address"], 16), int(globals_row["setter_address"], 16)
    variable = int(globals_row["variable_address"], 16)
    if find_registration(exe, name) != (getter, setter):
        problems.append("registration in the init code differs from the getter %#x and setter %#x" % (getter, setter))
    get_code = exe.read(getter, function_extent(getter, all_addresses, 0x100) - getter)
    set_code = exe.read(setter, function_extent(setter, all_addresses, 0x100) - setter)
    if generated["getter"] is None or generated["setter"] is None:
        return ["no generated getter or setter"], (name, "-", "-", "-", "-")
    get_targets = [t for _, t in call_targets(getter, get_code)]
    set_targets = [t for _, t in call_targets(setter, set_code)]
    if get_targets != [NEW_POINTER_OBJ]:
        problems.append("getter in the exe calls %s, expected only SWIG_NewPointerObj" % [hex(t) for t in get_targets])
    if set_targets != [CONVERT_PTR]:
        problems.append("setter in the exe calls %s, expected only SWIG_ConvertPtr" % [hex(t) for t in set_targets])
    if b"\x68" + struct.pack("<I", variable) not in get_code:
        problems.append("getter in the exe does not push the object address %#x" % variable)
    for label, code, body in (("getter", get_code, generated["getter"]), ("setter", set_code, generated["setter"])):
        if type_slot_loads(code, slots) != [type_name]:
            problems.append("%s type global in exe bytes %s, expected %s" % (label, type_slot_loads(code, slots), type_name))
        if "SWIGTYPE%s" % type_name not in body:
            problems.append("%s type generated differs from %s" % (label, type_name))
    assigns = "assign: none" not in generated["setter"]
    if assigns and b"\xB8\x01\x00\x00\x00\xC3" not in set_code:
        problems.append("setter in the exe does not return 1 for a failed conversion")
    if not assigns and b"\x83\xF8\xFF\x0F\x94\xC1\x8B\xC1\xC3" not in set_code:
        problems.append("setter in the exe does not return 1 for a failed conversion and 0 else")
    if assigns:
        copies = sorted(struct.unpack_from("<I", set_code, i + 1)[0] for i in range(len(set_code) - 4) if set_code[i] == 0xBF)
        counts = sorted(struct.unpack_from("<I", set_code, i + 1)[0] for i in range(len(set_code) - 4) if set_code[i] == 0xB9)
        expected = layout_ranges(class_name)
        if copies != sorted(variable + offset for offset, _ in expected) or counts != sorted(size // 4 for _, size in expected):
            problems.append("setter in the exe copies %s (dword counts %s), layout file of %s gives %s" % (
                [hex(c) for c in copies], counts, class_name, [(hex(variable + o), s // 4) for o, s in expected]))
    elif b"\xF3\xA5" in set_code or b"\xF3\xA4" in set_code:
        problems.append("setter in the exe copies memory, the description says assign none")
    if assigns and "*(" not in generated["setter"]:
        problems.append("generated setter does not assign the object")
    if not assigns and "*(" in generated["setter"]:
        problems.append("generated setter assigns the object, the setter of the exe does not")
    cells = (name, hex(setter) + "/" + hex(getter), "(module attribute)", "pointer " + type_name,
             "copies %d ranges" % len(layout_ranges(class_name)) if assigns else "assigns nothing")
    return problems, cells


def print_table(rows):
    header = ("wrapper", "address", "format", "return", "engine call", "result")
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(6)]
    for row in [header] + rows:
        print("  ".join(str(cell).ljust(widths[i]) for i, cell in enumerate(row)))


def run_checks(out_dir):
    errors = []
    exe = Exe(EXE)
    slots = read_type_slots(exe)
    functions, table_rows, variables = parse_generated(out_dir)
    wrappers = []
    for name in WRAPPER_FILES:
        wrappers += read_tsv(os.path.join(REPO, "target", name), COLUMN_NAMES)
    method_table = read_tsv(os.path.join(REPO, "target", "appc_method_table.tsv"))
    globals_table = {r["name"]: r for r in read_tsv(os.path.join(REPO, "target", "appc_globals.tsv"))}
    names = {int(row["address"], 16): row["name"] for row in read_tsv(os.path.join(REPO, "re", "names.tsv"))}

    all_addresses = sorted({int(r["function_address"], 16) for r in method_table})
    global_addresses = sorted({int(r[key], 16) for r in globals_table.values() for key in ("getter_address", "setter_address")})
    method_by_name = {r["name"]: r for r in method_table}

    generated_names = [name for name, _, _ in table_rows]
    table_function = {name: function for name, function, _ in table_rows}
    if len(set(generated_names)) != len(generated_names):
        errors.append("generated table has a repeated name")
    for name, _, flags in table_rows:
        if flags != 1:
            errors.append("%s: table flags %d, expected 1" % (name, flags))

    expected_names = {r["name"] for r in method_table if CLASS_PATTERN.match(r["name"]) or r["name"] in PARTIAL_NAMES}
    for name in sorted(expected_names - set(generated_names)):
        errors.append("table name missing in generated code: %s" % name)
    for name in sorted(set(generated_names) - expected_names):
        errors.append("table name not in appc_method_table.tsv: %s" % name)

    address_to_function = {}
    exe_format_reads = set()
    result_rows = []
    method_wrappers = [r for r in wrappers if not VARIABLE_PATTERN.match(r["wrapper"])]
    for row in method_wrappers:
        name = row["wrapper"]
        address = int(row["wrapper_address"], 16)
        function = table_function.get(name)
        info = functions.get(function) if function else None
        if info is None or name not in method_by_name:
            errors.append("%s: no generated wrapper" % name)
            result_rows.append((name, row["wrapper_address"], "-", "-", "-", "MISSING"))
            continue
        problems = []
        previous = address_to_function.setdefault(address, function)
        if previous != function:
            problems.append("address shared with another generated function")
        facts = exe_wrapper_facts(exe, address, function_extent(address, all_addresses, 0x400), slots)
        more, cells = check_method_wrapper(row, name, info, facts, slots, names, method_by_name[name])
        problems += more
        if facts["format"] == row["format"]:
            exe_format_reads.add(address)
        errors += ["%s: %s" % (name, problem) for problem in problems]
        result_rows.append(cells + ("ok" if not problems else "FAIL",))

    variable_rows = {VARIABLE_PATTERN.match(r["wrapper"]).group(1) for r in wrappers if VARIABLE_PATTERN.match(r["wrapper"])}
    for generated in variables:
        name = generated["name"]
        row = globals_table.get(name)
        if row is None or name not in variable_rows:
            errors.append("%s: module attribute not in appc_globals.tsv or in the wrapper table" % name)
            continue
        if name in method_by_name:
            errors.append("%s: module attribute is also a method table name" % name)
        match = re.search(r"SWIGTYPE(\w+)\b", generated["getter"] or "")
        type_name = match.group(1) if match else None
        class_name = type_name[3:] if type_name and type_name.startswith("_p_") else type_name
        problems, cells = check_variable(generated, row, exe, slots, type_name, class_name, global_addresses)
        errors += ["%s: %s" % (name, problem) for problem in problems]
        result_rows.append(cells + ("ok" if not problems else "FAIL",))
    for name in sorted(variable_rows - {v["name"] for v in variables}):
        errors.append("%s: module attribute missing in generated code" % name)

    distinct_functions = {table_function[r["wrapper"]] for r in method_wrappers if r["wrapper"] in table_function}
    distinct_addresses = {r["wrapper_address"] for r in method_wrappers}
    if len(distinct_functions) != len(distinct_addresses):
        errors.append("generated functions %d, distinct exe addresses %d" % (len(distinct_functions), len(distinct_addresses)))
    for function in sorted(set(functions) - set(table_function.values())):
        errors.append("generated function without table row: %s" % function)
    summary = {
        "rows": len(method_wrappers),
        "functions": len(distinct_functions),
        "addresses": len(distinct_addresses),
        "table names": len(generated_names),
        "expected names": len(expected_names),
        "formats read from exe bytes": len(exe_format_reads),
        "module attributes": len(variables),
    }
    return result_rows, errors, summary


def mutate(text, header, old, new):
    start = text.index(header + "\n")
    end = text.find("\nfunc ", start + 1)
    end2 = text.find("\nglobal ", start + 1)
    candidates = [e for e in (end, end2) if e >= 0]
    end = min(candidates) if candidates else len(text)
    block = text[start:end]
    if old not in block:
        raise SystemExit("selftest: pattern %r not in %s" % (old, header))
    return text[:start] + block.replace(old, new, 1) + text[end:]


SELFTEST_MUTATIONS = [
    ("float argument becomes int in TGPoint3_SetXYZ", "math.appc", "func TGPoint3_SetXYZ", "arg f float y", "arg i int y"),
    ("optional marker lost in new_NiColorA", "color.appc", "func new_NiColorA", "opt f float r", "arg f float r"),
    ("pointer class changed in TGPoint3_Add", "math.appc", "func TGPoint3_Add", "arg O TGPoint3 other", "arg O NiPoint3 other"),
    ("return kind changed in TGPoint3_VectorLength", "math.appc", "func TGPoint3_VectorLength", "return float", "return none"),
    ("alias dropped in NiPoint3_x_get", "math.appc", "func NiPoint3_x_get", "    alias TGPoint3_x_get\n", ""),
    ("engine call method renamed in TGPoint3_Set", "math.appc", "func TGPoint3_Set", "$this->Set(", "$this->Assign("),
    ("string argument becomes an int in TGModelProperty_SetName", "modelproperty.appc", "func TGModelProperty_SetName",
     "arg S string name", "arg i int name"),
    ("string argument lost in TGModelProperty_Create", "modelproperty.appc", "func TGModelProperty_Create",
     "arg S string name", "arg O TGString name"),
    ("long argument becomes int in TGObject_GetTGObjectPtr", "object.appc", "func TGObject_GetTGObjectPtr",
     "arg l long id", "arg i int id"),
    ("string return becomes int in TGObject_GetIndentString", "object.appc", "func TGObject_GetIndentString",
     "return string", "return int"),
    ("returned pointer class changed in TGModelProperty_Duplicate", "modelproperty.appc", "func TGModelProperty_Duplicate",
     "return ptr TGModelProperty", "return ptr TGString"),
    ("function pointer type changed in TGModelPropertyManager_AddFilter", "propertymanager.appc",
     "func TGModelPropertyManager_AddFilter", "arg O ModelPropertyFilter filter", "arg O TGModelProperty filter"),
    ("optional marker lost in TGModelPropertyManager_FindByName", "propertymanager.appc",
     "func TGModelPropertyManager_FindByName", "opt i int local", "arg i int local"),
    ("virtual call renamed in TGObject_Print", "object.appc", "func TGObject_Print", "$this->Print(", "$this->Show("),
    ("virtual slot 0 call lost in TGObject_Destroy", "object.appc", "func TGObject_Destroy", "call  delete $this",
     "call  $this->GetObjType()"),
    ("engine function renamed in TGModelProperty_Create", "modelproperty.appc", "func TGModelProperty_Create",
     "TGModelProperty::Create(", "TGModelProperty::Make("),
    ("module attribute type changed", "propertymanager.appc", "global g_kModelPropertyManager TGModelPropertyManager",
     "global g_kModelPropertyManager TGModelPropertyManager", "global g_kModelPropertyManager TGModelPropertySet"),
    ("module attribute renamed", "propertymanager.appc", "global g_kModelPropertyManager TGModelPropertyManager",
     "global g_kModelPropertyManager", "global g_kModelPropertyManagerX"),
    ("float argument becomes int in TGIconManager_SetScale", "iconmanager.appc", "func TGIconManager_SetScale",
     "arg f float scale", "arg i int scale"),
    ("optional marker lost in TGIconManager_SetDisplayInfo", "iconmanager.appc", "func TGIconManager_SetDisplayInfo",
     "opt f float scale 1.0f", "arg f float scale"),
    ("string argument becomes an int in TGIconManager_GetIconGroup", "iconmanager.appc",
     "func TGIconManager_GetIconGroup", "arg S string name", "arg i int name"),
    ("pointer class changed in TGIconManager_AddIconGroup", "iconmanager.appc", "func TGIconManager_AddIconGroup",
     "arg O TGIconGroup group", "arg O NiCamera group"),
    ("returned pointer class changed in TGIconManager_CreateIconGroup", "iconmanager.appc",
     "func TGIconManager_CreateIconGroup", "return ptr TGIconGroup", "return ptr NiCamera"),
    ("return kind changed in TGIconGroup_GetCount", "icongroup.appc", "func TGIconGroup_GetCount", "return int",
     "return float"),
    ("long argument becomes int in TGIconGroup_SetIconLocation", "icongroup.appc", "func TGIconGroup_SetIconLocation",
     "arg l long handle", "arg i int handle"),
    ("virtual call renamed in TGIconGroup_SetIconLocation", "icongroup.appc", "func TGIconGroup_SetIconLocation",
     "$this->SetIconLocation(", "$this->PlaceIcon("),
    ("optional marker lost in TGIconGroup_LoadIconTexture", "icongroup.appc", "func TGIconGroup_LoadIconTexture",
     "opt i int flag", "arg i int flag"),
    ("module attribute assigns in g_kIconManager", "iconmanager.appc", "global g_kIconManager TGIconManager",
     "    assign none", "    assign copy"),
    ("float argument becomes int in TGFontManager_RegisterFont", "fontmanager.appc", "func TGFontManager_RegisterFont",
     "arg f float size", "arg i int size"),
    ("string argument becomes an int in TGFontManager_RegisterFont", "fontmanager.appc", "func TGFontManager_RegisterFont",
     "arg S string name", "arg i int name"),
    ("module attribute assigns in g_kFontManager", "fontmanager.appc", "global g_kFontManager TGFontManager",
     "    assign none", "    assign copy"),
]


def selftest():
    work = tempfile.mkdtemp(prefix="appcgen_selftest_")
    failures = 0
    try:
        interface = os.path.join(REPO, "interface")
        baseline_out = os.path.join(work, "baseline")
        appcgen.generate(interface, baseline_out)
        if run_checks(baseline_out)[1]:
            print("selftest: the unchanged description does not pass")
            return 1
        for label, file_name, header, old, new in SELFTEST_MUTATIONS:
            copy = os.path.join(work, "interface")
            shutil.rmtree(copy, ignore_errors=True)
            shutil.copytree(interface, copy)
            path = os.path.join(copy, file_name)
            text = open(path).read()
            open(path, "w").write(mutate(text, header, old, new))
            out = os.path.join(work, "out")
            shutil.rmtree(out, ignore_errors=True)
            try:
                appcgen.generate(copy, out)
            except appcgen.appcdesc.DescriptionError as error:
                print("selftest: detected  %-60s (description rejected: %s)" % (label, error))
                continue
            errors = run_checks(out)[1]
            if errors:
                print("selftest: detected  %-60s (%d errors, first: %s)" % (label, len(errors), errors[0]))
            else:
                print("selftest: NOT DETECTED  %s" % label)
                failures += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=os.path.join(REPO, "build", "appc"))
    parser.add_argument("--selftest", action="store_true", help="plant defects in a copy of the description")
    options = parser.parse_args()
    if not sha256_ok():
        sys.exit("check: game/stbc.exe does not match checksums/stbc.exe.sha256")
    if options.selftest:
        sys.exit(selftest())
    appcgen.generate(os.path.join(REPO, "interface"), options.out)
    rows, errors, summary = run_checks(options.out)
    print_table(rows)
    print()
    print(", ".join("%s %s" % (key, value) for key, value in summary.items()))
    for error in errors:
        print("MISMATCH " + error)
    print("%d mismatches" % len(errors))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
