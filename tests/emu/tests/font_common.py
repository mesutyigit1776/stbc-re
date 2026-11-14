"""Shared definitions for the tests of the font manager, the font family record and the font registration.

The font code registers each font size with the icon manager, so every scenario builds the global font manager at
0x99d8e0 and the global icon manager at 0x99d1f0 and compares both graphs. The name of the icon group is made by
sprintf (0x8599b9) of the exe; the tables of the float conversion code are set by the setup of the scenario, as the
start-up code of the exe does.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import (ICON_CLASSES, ICON_CODE, ICON_ENVS, ICON_MEMORY, IconEnv, atexit_probe, atexit_stubs,  # noqa: E402
                         icon_setup, icon_stubs, list_classes, log_probe, manager_global, narrow, registration, span)
from prop_common import LIB, LIB24, native_api, orig, trap_probe  # noqa: E402

FONT_MANAGER = 0x99D8E0
VT_FONT_MANAGER, VT_FONT_REGISTRATION = 0x897E5C, 0x897E64
CRT_CODE = span(0x850000, 0x87FFFF)
FONT_CODE = ICON_CODE + [span(0x7347D0, 0x734DAF), CRT_CODE]
# The pointers that the start-up code of the exe stores for the float conversion (0x859971).
FLOAT_TABLE = {0x97A858: 0x861ADA, 0x97A85C: 0x861784, 0x97A860: 0x8617EA, 0x97A864: 0x86172A, 0x97A868: 0x8617D2,
               0x97A86C: 0x861ADA}

FONT_CLASSES = list_classes("FontFace", "FontFace") + list_classes("FontReg", "FontRegistration") + \
    list_classes("FontGroup", None) + [
    ClassDef("FontRegistration", 0x14, [
        Member("vtbl", 0, VTBL), Member("groupName", 4, CSTR), Member("moduleName", 8, CSTR),
        Member("functionName", 0x0C, CSTR), Member("size", 0x10, F32)], vtable=VT_FONT_REGISTRATION, counted=False),
    ClassDef("FontFace", 0x34, [
        Member("name", 0, CSTR), Member("registrations", 4, Inline("FontRegList")),
        Member("loadedGroups", 0x1C, Inline("FontGroupList"))], counted=False),
    ClassDef("FontManager", 0x24, [
        Member("vtbl", 0, VTBL), Member("families", 4, Inline("FontFaceList")), Member("defaultName", 0x1C, CSTR),
        Member("defaultSize", 0x20, F32)], vtable=VT_FONT_MANAGER, counted=False),
]
FONT_MODEL = Model(ICON_CLASSES + FONT_CLASSES, native_api(LIB))
FONT_MODEL24 = Model(ICON_CLASSES + FONT_CLASSES, native_api(LIB24))


def font_setup(serial=100, extra=None):
    """Sets the statics of both sides and the tables of the float conversion code of the guest."""
    def tables(machine, lib):
        for address, value in FLOAT_TABLE.items():
            machine.write(address, struct.pack("<I", value))
        if extra:
            extra(machine, lib)
    return icon_setup(None, "", serial, extra=tables)


def make_font_env(precision):
    base = ICON_ENVS[precision]
    lib, model = base.lib, FONT_MODEL if precision == 53 else FONT_MODEL24

    def setup(first, last, args, ret, symbol):
        return SetupCall(orig(first, last, "thiscall", args, ret, allow=FONT_CODE), Native(lib, symbol))

    calls = dict(base.setup_calls)
    calls.update({
        "fmctor": setup(0x734810, 0x734837, [PTR], PTR, "bc_TGFontManager_TGFontManager"),
        "fregister": setup(0x734A30, 0x734C1F, [PTR, Buf(io="in"), F32, Buf(io="in"), Buf(io="in")], None,
                           "bc_TGFontManager_RegisterFont"),
        "fface": setup(0x7348F0, 0x7349AF, [PTR, Buf(io="in")], PTR, "bc_TGFontFace_TGFontFace"),
        "faddreg": setup(0x734C20, 0x734DAF, [PTR, F32, Buf(io="in"), Buf(io="in")], None,
                         "bc_TGFontFace_AddRegistration"),
    })
    return IconEnv(precision, lib, model, base.cw, calls)


FONT_ENVS = {53: make_font_env(53), 24: make_font_env(24)}


def font_case(env, first, last, conv, args, ret, symbol, scenarios, **kw):
    """An ObjectCase of the exe function against the native shim of the library of the environment."""
    kw.setdefault("stubs", icon_stubs())
    kw.setdefault("setup_calls", env.setup_calls)
    kw.setdefault("memory", ICON_MEMORY)
    kw.setdefault("label", "p%d" % env.precision)
    kw.setdefault("probes", [log_probe(), trap_probe()])
    original = orig(first, last, conv, args, ret, allow=FONT_CODE)
    return ObjectCase(env.model, original, Native(env.lib, symbol), scenarios, cw=env.cw, **kw)


def font_pair(name):
    """A decorator that registers the test for each precision."""
    def decorate(builder):
        for precision in (53, 24):
            register("%s@p%d" % (name, precision))(lambda precision=precision: builder(FONT_ENVS[precision]))
        return builder
    return decorate


def font_once(name):
    def decorate(builder):
        register(name)(lambda: builder(FONT_ENVS[53]))
        return builder
    return decorate


# Scenario building blocks -----------------------------------------------------------------------------------------------

def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def empty_font_list(prefix):
    return Obj(prefix + "List", count=0, head=None, tail=None, freeNodes=None, blocks=None, blockSize=2)


def font_manager_obj(default_name=None, default_size=0.0):
    """The state of the manager after its constructor, with the default font given."""
    return Obj("FontManager", families=empty_font_list("FontFace"), defaultName=default_name, defaultSize=default_size)


def font_global(default_name=None, default_size=0.0):
    return Global("fm", FONT_MANAGER, font_manager_obj(default_name, default_size), "bc_init_global_FontManager")


def font_scenario(roots, args, pre=(), label="", icon=None, font=None, serial=100, extra=None):
    """A scenario with the global font manager and the global icon manager."""
    return Scenario(roots, args, globals=[font or font_global(), icon or manager_global()], pre=list(pre), label=label,
                    setup=font_setup(serial, extra))


# The fonts that Icons/FontsAndIcons.py registers, in the order of the script.
SCRIPT_FONTS = [("Crillee", 5, "Crillee5", "LoadCrillee5"), ("Crillee", 6, "Crillee6", "LoadCrillee6"),
                ("Crillee", 9, "Crillee9", "LoadCrillee9"), ("Crillee", 12, "Crillee12", "LoadCrillee12"),
                ("Crillee", 15, "Crillee15", "LoadCrillee15"), ("LCARSText", 5, "LCARSText5", "LoadLCARSText5"),
                ("LCARSText", 6, "LCARSText6", "LoadLCARSText6"), ("LCARSText", 9, "LCARSText9", "LoadLCARSText9"),
                ("LCARSText", 12, "LCARSText12", "LoadLCARSText12"), ("LCARSText", 15, "LCARSText15", "LoadLCARSText15"),
                ("Tahoma", 8, "Tahoma", "LoadTahoma"), ("Tahoma", 14, "TahomaLarge", "LoadTahomaLarge"),
                ("Arial", 8, "Arial", "LoadArial"), ("Serpentine", 12, "Serpentine12", "LoadSerpentine12")]


def register_pre(fonts):
    """The setup calls that register the fonts with the code of each side."""
    return [Pre("fregister", [Root("fm"), narrow(n), float(s), narrow(m), narrow(f)]) for n, s, m, f in fonts]


def face_with(sizes, name="Crillee"):
    """The setup calls that make a family with the sizes, by the code of each side."""
    pre = [Pre("fface", [Root("f"), narrow(name)])]
    for size in sizes:
        pre.append(Pre("faddreg", [Root("f"), float(size), narrow("Mod%d" % size), narrow("Fn%d" % size)]))
    return pre


# Layout files -----------------------------------------------------------------------------------------------------------

LAYOUTS = os.path.join(REPO_ROOT, "re", "layouts")


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
    import ctypes
    from emuharness.native import load_library
    lib = load_library(LIB)
    out = ctypes.create_string_buffer(2048)
    length = lib.bc_exe_layout(name.encode(), out, 2048)
    assert length >= 0, "no native exe table for " + name
    return {row.split()[0]: int(row.split()[1]) for row in out.value.decode().splitlines()}


def vtable_rows(name):
    return [line.split("\t") for line in open(os.path.join(LAYOUTS, name + ".vtable.tsv")).read().splitlines()[1:]]
