"""Layout checks of the icon classes: the field maps of the tests, the layout files in re/layouts, the exe offset tables
of the native headers and the order of the virtual functions must agree. The compile-time check of the headers must
reject a wrong table. The trap slots and the boundary functions must reach the exe address that they stand for.
"""
import ctypes
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import ICON_MODEL  # noqa: E402
from prop_common import LIB  # noqa: E402
from emuharness.native import load_library  # noqa: E402

LAYOUTS = os.path.join(REPO_ROOT, "re", "layouts")
# Model class -> layout file. The members of a file that the model groups: the corners and the serial number.
FILES = {"IconManager": "TGIconManager", "IconGroup": "TGIconGroup", "IconLocation": "TGIconLocation",
         "IconLocationNode": "TGIconLocationNode", "IconLocationTable": "TGIconLocationTable",
         "IconRegistration": "TGIconRegistration", "IconTextureArray": "TGIconTextureArray",
         "TextureCache": "TGIconTextureCache", "TextureCacheNode": "TGIconTextureNode",
         "NiTextureProperty": "NiTextureProperty", "NiImage": "NiImage", "NiCamera": "NiCamera"}
GROUPED = {"IconLocation": {"c%d" % i: ("corners", 4 * i) for i in range(8)},
           "NiTextureProperty": {"serial": ("name", 0)}, "NiImage": {"serial": ("name", 0)},
           "NiCamera": {"serial": ("reserved", 0)}}
# Classes of which the source tree declares only the members that the icon code uses.
PARTIAL = ("NiImage", "NiCamera", "NiTextureProperty")
NATIVE_TABLES = {name for name in FILES.values()}


def layout_file(name):
    fields, size = {}, None
    for line in open(os.path.join(LAYOUTS, name + ".tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[0] == "sizeof":
            size = int(cells[1])
        elif cells[3] != "padding":
            fields.setdefault(cells[3], int(cells[0], 16))
    return fields, size


def native_table(name):
    lib = load_library(LIB)
    out = ctypes.create_string_buffer(2048)
    length = lib.bc_exe_layout(name.encode(), out, 2048)
    assert length >= 0, "no native exe table for " + name
    return {row.split()[0]: int(row.split()[1]) for row in out.value.decode().splitlines()}


@register("icon.layout.field_maps_layout_files_and_native_tables_agree")
def layouts_agree():
    for model_name, file_name in FILES.items():
        file_fields, file_size = layout_file(file_name)
        assert native_table(file_name) == file_fields, (file_name, native_table(file_name), file_fields)
        if model_name not in PARTIAL:
            assert ICON_MODEL.cls(model_name).size == file_size, (model_name, ICON_MODEL.cls(model_name).size, file_size)
        grouped = GROUPED.get(model_name, {})
        covered = set()
        for member in ICON_MODEL.members(model_name):
            field = "vtable" if member.name == "vtbl" else member.name
            if field in grouped:
                base, delta = grouped[field]
                assert file_fields[base] + delta == member.offset, (model_name, field, member.offset)
                covered.add(base)
            else:
                assert file_fields.get(field) == member.offset, (model_name, field, member.offset, file_fields.get(field))
                covered.add(field)
        if model_name not in PARTIAL:
            assert set(file_fields) <= covered, (model_name, "fields of the layout file that the field map lacks",
                                                 set(file_fields) - covered)


# The layout files of the image classes: model class -> layout file, with the fields that the model groups.
IMAGE_FILES = {"NiRawImageData": "NiRawImageData", "NiImage": "NiImage", "NiDDImage": "NiDDImage",
               "TextureFormat": "TextureFormat", "ImageSurfaceRecord": "ImageSurfaceRecord",
               "TextureManager": "TextureManager", "NiD3DRender": "NiD3DRender", "NiObjectNET": "NiObjectNET",
               "NiTextureProperty": "NiTextureProperty"}
IMAGE_GROUPED = {"TextureManager": {"slot%d" % i: ("slots", 4 * i) for i in range(7)}}
IMAGE_PARTIAL = ("TextureFormat", "TextureManager", "NiD3DRender")


@register("icon.layout.image_classes_field_maps_layout_files_and_native_tables_agree")
def image_layouts_agree():
    from icon_image_common import IMAGE_MODEL
    for model_name, file_name in IMAGE_FILES.items():
        file_fields, file_size = layout_file(file_name)
        assert native_table(file_name) == file_fields, (file_name, native_table(file_name), file_fields)
        if model_name not in IMAGE_PARTIAL:
            assert IMAGE_MODEL.cls(model_name).size == file_size, (model_name, IMAGE_MODEL.cls(model_name).size, file_size)
        grouped = IMAGE_GROUPED.get(model_name, {})
        covered = set()
        for member in IMAGE_MODEL.members(model_name):
            field = "vtable" if member.name == "vtbl" else member.name
            if field in grouped:
                base, delta = grouped[field]
                assert file_fields[base] + delta == member.offset, (model_name, field, member.offset)
                covered.add(base)
            else:
                assert file_fields.get(field) == member.offset, (model_name, field, member.offset, file_fields.get(field))
                covered.add(field)
        if model_name not in IMAGE_PARTIAL:
            assert set(file_fields) <= covered, (model_name, set(file_fields) - covered)


def vtable_rows(name):
    rows = [line.split("\t") for line in open(os.path.join(LAYOUTS, name + ".vtable.tsv")).read().splitlines()[1:]]
    return rows


@register("icon.layout.virtual_functions_follow_the_exe_slot_order")
def vtable_order():
    lib = load_library(LIB)
    # kind, layout file, has a virtual destructor, slots after the destructor in the file
    cases = [(0, "TGIconGroup", True), (1, "TGIconLocation", False), (2, "TGIconRegistration", False),
             (3, "TGIconLocationTable", True)]
    for kind, name, has_destructor in cases:
        rows = vtable_rows(name)
        expected = [int(r[2]) for r in rows if int(r[2]) > 0 or not has_destructor]
        out = (ctypes.c_int32 * 16)()
        count = lib.bc_icon_virtual_slots(kind, out)
        shift = 1 if has_destructor else 0
        assert [out[i] - shift for i in range(count)] == expected, (name, list(out[:count]), expected)
        if has_destructor:
            assert int(rows[0][2]) == 0 and "Destructor" in rows[0][4], rows[0]
    cache = vtable_rows("TGIconTextureArray")
    assert [int(r[2]) for r in cache] == [0]
    assert [int(r[2]) for r in vtable_rows("TGIconManager")] == [0]


CHECK_SOURCE = """
#include "TGIconGroup.h"
constexpr exe::Field kTable[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconGroup, name, 0x04),
    EXE_FIELD(TGIconGroup, lockCount, %s),
    EXE_FIELD_ALIGNED(TGIconGroup, locations, 0x0c, 0x10, 4),
    EXE_FIELD_ALIGNED(TGIconGroup, textures, %s, 0x18, 4),
};
static_assert(exe::LayoutHolds(kTable, 0x34), "layout table rejected");
"""


def compiles(lock_offset, textures_offset):
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "check.cpp")
        with open(path, "w") as f:
            f.write(CHECK_SOURCE % (lock_offset, textures_offset))
        run = subprocess.run(["clang++", "-std=c++17", "-fsyntax-only", "-Wno-invalid-offsetof", "-I",
                              os.path.join(REPO_ROOT, "src", "engine"), path], capture_output=True, text=True)
    return run.returncode == 0, run.stderr


@register("icon.layout.header_check_accepts_the_exe_table_and_rejects_wrong_tables")
def header_check():
    ok, text = compiles("0x08", "0x1c")
    assert ok, "the correct table was rejected: " + text
    ok, text = compiles("0x0c", "0x1c")
    assert not ok and "layout table rejected" in text, "a table with a wrong offset was accepted"
    ok, text = compiles("0x08", "0x20")
    assert not ok and "layout table rejected" in text, "a table with a wrong offset was accepted"


@register("icon.traps.slots_and_boundary_functions_reach_their_exe_address")
def traps_reach_addresses():
    lib = load_library(LIB)
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_TGIconGroup_CallTrap.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.bc_icon_call_default_boundary.argtypes = [ctypes.c_int32]
    expected = {0: [0x72CB70], 1: [0x72CC50], 2: [0x72C480, 0x72C4E0], 3: [0x747AA0, 0x747AD0]}
    for which, addresses in expected.items():
        lib.bc_trap_clear()
        lib.bc_TGIconGroup_CallTrap(None, which)
        assert [lib.bc_trap_address(i) for i in range(lib.bc_trap_count())] == addresses, (which, addresses)
    # The creation of an image and of a texture property are reconstructed: with no renderer (no factory function) it gives null and stops nowhere.
    for which, addresses in enumerate(([], [], [0x6F8AB0], [0x6F9920])):
        lib.bc_trap_clear()
        lib.bc_icon_call_default_boundary(which)
        assert [lib.bc_trap_address(i) for i in range(lib.bc_trap_count())] == addresses, (which, addresses)
