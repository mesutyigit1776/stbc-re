"""Layout checks of the font classes: the field maps of the tests, the layout files in re/layouts, the exe offset tables of
the native headers and the order of the virtual functions must agree. The compile-time check of the headers must reject
a wrong table. The trap slots and the stack buffer of the group name must reach the exe address that they stand for.
"""
import ctypes
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from font_common import *  # noqa: F401,F403,E402
from font_common import FONT_MODEL, layout_file, native_table, vtable_rows  # noqa: E402
from prop_common import LIB  # noqa: E402
from emuharness.native import load_library  # noqa: E402

FILES = {"FontManager": "TGFontManager", "FontFace": "TGFontFace", "FontRegistration": "TGFontRegistration"}


@register("font.layout.field_maps_layout_files_and_native_tables_agree")
def layouts_agree():
    for model_name, file_name in FILES.items():
        file_fields, file_size = layout_file(file_name)
        assert native_table(file_name) == file_fields, (file_name, native_table(file_name), file_fields)
        assert FONT_MODEL.cls(model_name).size == file_size, (model_name, FONT_MODEL.cls(model_name).size, file_size)
        covered = set()
        for member in FONT_MODEL.members(model_name):
            field = "vtable" if member.name == "vtbl" else member.name
            assert file_fields.get(field) == member.offset, (model_name, field, member.offset, file_fields.get(field))
            covered.add(field)
        assert set(file_fields) <= covered, (model_name, set(file_fields) - covered)


@register("font.layout.virtual_functions_follow_the_exe_slot_order")
def vtable_order():
    lib = load_library(LIB)
    for kind, name in ((0, "TGFontManager"), (1, "TGFontRegistration")):
        rows = vtable_rows(name)
        out = (ctypes.c_int32 * 16)()
        count = lib.bc_font_virtual_slots(kind, out)
        assert [out[i] for i in range(count)] == [int(r[2]) for r in rows], (name, list(out[:count]))
        assert all(r[0] == hex(0x897E5C if kind == 0 else 0x897E64) for r in rows)


CHECK_SOURCE = """
#include "TGFontManager.h"
constexpr exe::Field kTable[] = {
    EXE_VPTR(),
    EXE_FIELD_ALIGNED(TGFontManager, families, 0x04, 0x18, 4),
    EXE_FIELD(TGFontManager, defaultName, %s),
    EXE_FIELD(TGFontManager, defaultSize, %s),
};
static_assert(exe::LayoutHolds(kTable, 0x24), "layout table rejected");
"""


def compiles(name_offset, size_offset):
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "check.cpp")
        with open(path, "w") as f:
            f.write(CHECK_SOURCE % (name_offset, size_offset))
        run = subprocess.run(["clang++", "-std=c++17", "-fsyntax-only", "-Wno-invalid-offsetof", "-I",
                              os.path.join(REPO_ROOT, "src", "engine"), path], capture_output=True, text=True)
    return run.returncode == 0, run.stderr


@register("font.layout.header_check_accepts_the_exe_table_and_rejects_wrong_tables")
def header_check():
    ok, text = compiles("0x1c", "0x20")
    assert ok, "the correct table was rejected: " + text
    ok, text = compiles("0x20", "0x20")
    assert not ok and "layout table rejected" in text, "a table with a wrong offset was accepted"
    ok, text = compiles("0x1c", "0x24")
    assert not ok and "layout table rejected" in text, "a table with a wrong offset was accepted"


@register("font.traps.stream_slots_reach_their_exe_address")
def traps_reach_addresses():
    lib = load_library(LIB)
    lib.bc_trap_address.restype = ctypes.c_uint32
    for function, expected in ((lib.bc_TGFontManager_CallTrap, [0x735410, 0x735500]),
                               (lib.bc_TGFontRegistration_CallTrap, [0x7354D0, 0x7358E0])):
        function.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        for which, address in enumerate(expected):
            lib.bc_trap_clear()
            function(None, which)
            assert [lib.bc_trap_address(i) for i in range(lib.bc_trap_count())] == [address], (which, address)


@register("font.traps.a_group_name_of_128_characters_or_more_stops_at_the_exe_address_of_the_sprintf_call")
def long_name_stops():
    lib = load_library(LIB)
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_init_global_FontManager.restype = ctypes.c_void_p
    lib.bc_init_global_IconManager.restype = ctypes.c_void_p
    lib.bc_TGFontManager_RegisterFont.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_float, ctypes.c_char_p,
                                                  ctypes.c_char_p]
    # the name and the size text "5.00" are 128 characters or more: the exe writes past the buffer of 0x80 bytes (B0040)
    for length, stops in ((122, False), (123, False), (124, True), (127, True), (200, True)):
        lib.bc_init_global_IconManager()
        manager = lib.bc_init_global_FontManager()
        lib.bc_trap_clear()
        lib.bc_TGFontManager_RegisterFont(manager, b"x" * length, 5.0, b"M", b"F")
        found = [lib.bc_trap_address(i) for i in range(lib.bc_trap_count())]
        assert found == ([0x734D68] if stops else []), (length, found)
