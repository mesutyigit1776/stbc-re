"""Tests of the font manager (0x734810 to 0x734c1f) and of the family record and the registration it makes (0x7348f0 to
0x734daf): the constructor and destructor, the static initializer and its three parts, the family record, the
registration of a size and RegisterFont.

The font manager is the global at 0x99d8e0 and the icon manager the global at 0x99d1f0; each scenario builds both from a
description, and the registrations that the font code makes with the icon manager are compared with the exe.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from font_common import *  # noqa: F401,F403,E402
from font_common import (FONT_ENVS, SCRIPT_FONTS, atexit_probe, atexit_stubs, f32, font_case, font_global, font_once,  # noqa: E402
                         font_pair, font_scenario, font_setup, face_with, log_probe, manager_global, register_pre, registration, trap_probe)

NAMES = ["Crillee", "LCARSText", "Tahoma", "Arial", "Serpentine", "A", "", "x" * 40, "Tahoma Large", "Z" * 100]
SIZES = [5.0, 12.0, 0.0, 0.1, 9.995, 14.5, 1.0e10, -3.5, 0.005, 100000.0, 3.0e38, 0.125]


# Constructor, destructor and the static initializer ---------------------------------------------------------------------------

@font_once("font.manager.TGFontManager_constructor")
def manager_constructor(env):
    scenarios = [Scenario({"m": Obj("FontManager")}, [Root("m")], label="a blank object", setup=font_setup())]
    return font_case(env, 0x734810, 0x734837, "thiscall", [PTR], PTR, "bc_TGFontManager_TGFontManager", scenarios)


def populated(fonts, default_name=None, default_size=0.0):
    return register_pre(fonts), default_name


@font_once("font.manager.TGFontManager_destructor_body")
def manager_destructor(env):
    states = [
        ("a new manager", []),
        ("one family with one size", SCRIPT_FONTS[:1]),
        ("one family with five sizes", SCRIPT_FONTS[:5]),
        ("the fonts of the script", SCRIPT_FONTS),
    ]
    scenarios = [font_scenario({}, [Root("fm")], register_pre(fonts), label) for label, fonts in states]
    scenarios.append(font_scenario({}, [Root("fm")], [], "a default font and no family", font=font_global("Arial", 8.0)))
    # the destructor does not free the registrations (B0039)
    return font_case(env, 0x734840, 0x7348E6, "thiscall", [PTR], None, "bc_TGFontManager_DestructorBody", scenarios,
                     allow_leaks=True)


@font_once("font.manager.static_initializer_0x7347d0")
def static_init(env):
    scenarios = [font_scenario({}, [], label="the global is blank")]
    return font_case(env, 0x7347D0, 0x7347DF, "cdecl", [], None, "bc_TGFontManager_InitGlobal", scenarios,
                     stubs=atexit_stubs(), probes=[log_probe(), trap_probe(), atexit_probe()])


@font_once("font.manager.TGFontManager_construct_thunk_0x7347e0")
def construct_thunk(env):
    scenarios = [font_scenario({}, [], label="the global is blank")]
    return font_case(env, 0x7347E0, 0x7347E9, "cdecl", [], None, "bc_TGFontManager_ConstructGlobal", scenarios)


@font_once("font.manager.TGFontManager_register_cleanup_0x7347f0")
def register_cleanup(env):
    scenarios = [font_scenario({}, [], label="no objects")]
    return font_case(env, 0x7347F0, 0x7347FB, "cdecl", [], None, "bc_TGFontManager_RegisterGlobalCleanup", scenarios,
                     stubs=atexit_stubs(), probes=[log_probe(), trap_probe(), atexit_probe()])


@font_once("font.manager.TGFontManager_destruct_thunk_0x734800")
def destruct_thunk(env):
    scenarios = [font_scenario({}, [], register_pre(SCRIPT_FONTS[:6]), "families and registrations")]
    return font_case(env, 0x734800, 0x73480A, "cdecl", [], None, "bc_TGFontManager_DestructGlobal", scenarios,
                     allow_leaks=True)


# The family record ----------------------------------------------------------------------------------------------------------

@font_once("font.face.TGFontFace_constructor")
def face_constructor(env):
    scenarios = [font_scenario({"f": Obj("FontFace")}, [Root("f"), narrow(n)], label="name %r" % n[:6]) for n in NAMES]
    return font_case(env, 0x7348F0, 0x7349AF, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGFontFace_TGFontFace", scenarios)


@font_once("font.face.TGFontFace_constructor_with_a_null_name")
def face_constructor_null(env):
    scenarios = [font_scenario({"f": Obj("FontFace")}, [Root("f"), 0], label="a null name")]
    return font_case(env, 0x7348F0, 0x7349AF, "thiscall", [PTR, PTR], PTR, "bc_TGFontFace_TGFontFace_null", scenarios)


@font_once("font.face.TGFontFace_destructor")
def face_destructor(env):
    scenarios = [font_scenario({"f": Obj("FontFace")}, [Root("f")], face_with(sizes), label)
                 for label, sizes in (("no registration", []), ("one registration", [5]), ("three registrations", [5, 6, 9]),
                                      ("five registrations", [5, 6, 9, 12, 15]))]
    # the registrations are not freed (B0039)
    return font_case(env, 0x7349B0, 0x734A2F, "thiscall", [PTR], None, "bc_TGFontFace_Destructor", scenarios, allow_leaks=True)


# The registration of a size -------------------------------------------------------------------------------------------------

def add_registration_scenarios():
    scenarios = []
    for name in ("Crillee", "A", "", "Q" * 100, "x" * 123):
        # the name and the size text must fit into 127 characters (B0040)
        for size in (5.0, 0.1, f32(9.995), 12345.678, 0.0, -2.5, 0.125, 0.375):
            if len(name) == 123 and size not in (5.0, 0.1, 0.0):
                continue
            scenarios.append(font_scenario({"f": Obj("FontFace")}, [Root("f"), size, narrow("Mod"), narrow("Fn")],
                                           [Pre("fface", [Root("f"), narrow(name)])], "name of %d characters, size %r" % (len(name), size)))
    for count in (1, 2, 3, 5):
        scenarios.append(font_scenario({"f": Obj("FontFace")}, [Root("f"), 20.0, narrow("M20"), narrow("F20")],
                                       face_with([5, 6, 9, 12, 15][:count]), "%d registrations before" % count))
    scenarios.append(font_scenario({"f": Obj("FontFace")}, [Root("f"), 5.0, narrow(""), narrow("")],
                                   [Pre("fface", [Root("f"), narrow("Crillee")])], "empty module and function"))
    scenarios.append(font_scenario({"f": Obj("FontFace")}, [Root("f"), 5.0, narrow("M"), narrow("F")],
                                   face_with([5]), "the same size twice"))
    return scenarios


@font_pair("font.face.TGFontFace_AddRegistration")
def add_registration(env):
    return font_case(env, 0x734C20, 0x734DAF, "thiscall", [PTR, F32, Buf(io="in"), Buf(io="in")], None,
                     "bc_TGFontFace_AddRegistration", add_registration_scenarios())


# RegisterFont ---------------------------------------------------------------------------------------------------------------

def register_font_scenarios():
    scenarios = []
    for count in range(len(SCRIPT_FONTS)):
        name, size, module, function = SCRIPT_FONTS[count]
        scenarios.append(font_scenario({}, [Root("fm"), narrow(name), float(size), narrow(module), narrow(function)],
                                       register_pre(SCRIPT_FONTS[:count]),
                                       "font %d of the script after %d fonts" % (count + 1, count)))
    for name in NAMES:
        for size in SIZES[:6]:
            scenarios.append(font_scenario({}, [Root("fm"), narrow(name), f32(size), narrow("Mod"), narrow("Fn")], [],
                                           "name %r, size %r, an empty manager" % (name[:8], size)))
    for size in SIZES:
        scenarios.append(font_scenario({}, [Root("fm"), narrow("Crillee"), f32(size), narrow("ModS"), narrow("FnS")],
                                       register_pre(SCRIPT_FONTS[:3]), "size %r after three sizes of the family" % size))
    for first, second in (("Tahoma", "TahomaLarge"), ("TahomaLarge", "Tahoma"), ("Tahoma", "tahoma"), ("a", "ab"), ("ab", "a")):
        scenarios.append(font_scenario({}, [Root("fm"), narrow(second), 8.0, narrow("M"), narrow("F")],
                                       register_pre([(first, 8, "M0", "F0")]), "%r after %r" % (second, first)))
    scenarios.append(font_scenario({}, [Root("fm"), narrow("B"), 7.0, narrow("M"), narrow("F")], [],
                                   "a default font is set", font=font_global("Arial", 3.0)))
    scenarios.append(font_scenario({}, [Root("fm"), narrow("Crillee"), 5.0, narrow("M"), narrow("F")], [],
                                   "an icon manager with registrations",
                                   icon=manager_global(registrations=[registration("A", "mA", "fA"), registration("B", "mB", "fB")],
                                                       capacity=2)))
    scenarios.append(font_scenario({}, [Root("fm"), narrow("Crillee"), 5.0, narrow("M"), narrow("F")], [],
                                   "an icon manager with spare room",
                                   icon=manager_global(registrations=[registration("A", "mA", "fA")], capacity=6, grow_by=3)))
    return scenarios


@font_pair("font.manager.TGFontManager_RegisterFont")
def register_font(env):
    return font_case(env, 0x734A30, 0x734C1F, "thiscall", [PTR, Buf(io="in"), F32, Buf(io="in"), Buf(io="in")], None,
                     "bc_TGFontManager_RegisterFont", register_font_scenarios())
