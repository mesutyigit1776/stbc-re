"""Layout checks of the foundation classes: the field maps of the tests, the layout files in re/layouts and the
exe offset tables of the native headers must agree. The compile-time check of the headers must reject a wrong table.
"""
import ctypes
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from prop_common import native_lib  # noqa: E402

LAYOUTS = os.path.join(REPO_ROOT, "re", "layouts")
# Fields that the field map leaves out on purpose, with the reason.
NOT_IN_FIELD_MAP = {"TGString": {"allocator": "the constructor stores the low byte of a pointer"},
                    "NiNode": {"reserved": "not used by the property classes"}}
# Classes of which the source tree declares only the members that other classes use.
PARTIAL = ("NiNode",)


def layout_file(name):
    """(fields {name: offset}, size) from a layout file. The members of an embedded object (name.buffer, position.x)
    are folded into the member (name, position) at the offset of its first row. Padding rows are left out."""
    fields, size = {}, None
    for line in open(os.path.join(LAYOUTS, name + ".tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[0] == "sizeof":
            size = int(cells[1])
        elif cells[3] != "padding" and not cells[3].endswith(".padding"):
            fields.setdefault(cells[3].split(".")[0], int(cells[0], 16))
    return fields, size


def native_table(name):
    lib = native_lib()
    out = ctypes.create_string_buffer(2048)
    length = lib.bc_exe_layout(name.encode(), out, 2048)
    assert length >= 0, "no native exe table for " + name
    rows = [line.split() for line in out.value.decode().splitlines()]
    return {row[0]: int(row[1]) for row in rows}


@register("prop.layout.field_maps_layout_files_and_native_tables_agree")
def layouts_agree():
    for name in ("TGString", "TGObject", "ObjectRegistry", "ObjectRegistryNode", "ClassFactoryTable",
                 "ClassFactoryNode", "TGList", "TGListNode", "TGListBlock", "TGModelProperty", "SubsystemProperty",
                 "NiNode", "TGModelPropertySet", "TGModelPropertyList", "TGModelPropertyGroup",
                 "TGModelPropertyInstance", "TGModelPropertyManager"):
        file_fields, file_size = layout_file(name)
        native = native_table(name)
        assert native == file_fields, (name, "native table", native, "layout file", file_fields)
        cls = MODEL.cls(name)
        if name not in PARTIAL:
            assert cls.size == file_size, (name, cls.size, file_size)
        mapped = {("vtable" if m.name == "vtbl" else m.name): m.offset for m in MODEL.members(name)}
        for field, offset in mapped.items():
            assert file_fields.get(field) == offset, (name, field, offset, file_fields.get(field))
        missing = set(file_fields) - set(mapped) - set(NOT_IN_FIELD_MAP.get(name, {}))
        assert not missing, (name, "fields of the layout file that the field map lacks", missing)


@register("prop.layout.virtual_functions_follow_the_exe_slot_order")
def vtable_order():
    lib = native_lib()
    expected = {0: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], 1: [1, 2, 3, 4], 2: [1, 2, 3, 4], 3: list(range(1, 17)),
                4: list(range(1, 17)), 5: list(range(1, 12)), 6: [1, 2, 3, 4]}
    for kind, files in ((0, "TGObject.vtable"), (1, "ObjectRegistry.vtable"), (2, "ClassFactoryTable.vtable"),
                        (3, "TGModelProperty.vtable"), (4, "SubsystemProperty.vtable"),
                        (5, "TGModelPropertySet.vtable"), (6, "TGModelPropertyInstance.vtable")):
        rows = [line.split("\t") for line in open(os.path.join(LAYOUTS, files + ".tsv")).read().splitlines()[1:]]
        assert [int(r[2]) for r in rows if int(r[2]) > 0] == expected[kind], files
        out = (ctypes.c_int32 * 16)()
        count = lib.bc_virtual_slots(kind, out)
        # The Itanium ABI puts the complete and the deleting destructor in slots 0 and 1.
        assert [out[i] - 1 for i in range(count)] == expected[kind], (files, list(out[:count]))


CHECK_SOURCE = """
#include "TGString.h"
constexpr exe::Field kTable[] = {
    EXE_FIELD(TGString, allocator, 0x00),
    EXE_FIELD(TGString, buffer, %s),
    EXE_FIELD(TGString, length, %s),
    EXE_FIELD(TGString, capacity, 0x0c),
};
static_assert(exe::LayoutHolds(kTable, 0x10), "layout table rejected");
"""


def compiles(buffer_offset, length_offset):
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "check.cpp")
        with open(path, "w") as f:
            f.write(CHECK_SOURCE % (buffer_offset, length_offset))
        run = subprocess.run(["clang++", "-std=c++17", "-fsyntax-only", "-Wno-invalid-offsetof", "-I",
                              os.path.join(REPO_ROOT, "src", "engine"), path], capture_output=True, text=True)
    return run.returncode == 0, run.stderr


@register("prop.layout.header_check_accepts_the_exe_table_and_rejects_wrong_tables")
def header_check():
    ok, text = compiles("0x04", "0x08")
    assert ok, "the correct table was rejected: " + text
    ok, text = compiles("0x08", "0x08")
    assert not ok and "layout table rejected" in text, "a table with a wrong offset was accepted"
    ok, text = compiles("0x08", "0x04")
    assert not ok and "layout table rejected" in text, "a table with swapped fields was accepted"
