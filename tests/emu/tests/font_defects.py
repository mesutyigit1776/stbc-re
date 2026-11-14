"""Planted defects of the font tests: each native variant in src/emutest/FontDefects.cpp is wrong in one way, and the
test of the function must find it. A test that passes on a defect would not show a mistake in the real source.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from font_common import *  # noqa: F401,F403,E402
from font_common import (FONT_ENVS, SCRIPT_FONTS, atexit_probe, atexit_stubs, f32, font_case, font_global, font_scenario,  # noqa: E402
                         font_setup, log_probe, register_pre, trap_probe, face_with)
from prop_common import failing_text  # noqa: E402

E53, E24 = FONT_ENVS[53], FONT_ENVS[24]
REGISTER = [PTR, Buf(io="in"), F32, Buf(io="in"), Buf(io="in")]
ADD = [PTR, F32, Buf(io="in"), Buf(io="in")]


def fails(case, fragment):
    text = failing_text(case)
    assert fragment in text, "expected %r in the report:\n%s" % (fragment, text)


def register_case(symbol, scenarios, env=E53):
    return font_case(env, 0x734A30, 0x734C1F, "thiscall", REGISTER, None, symbol, scenarios)


def register_scenario(name, size, before, label, **kw):
    return font_scenario({}, [Root("fm"), narrow(name), size, narrow("M"), narrow("F")], register_pre(before), label, **kw)


@register("font.defect.RegisterFont_sets_the_default_font_on_every_call")
def defect_default_every_call():
    fails(register_case("bc_TGFontManager_RegisterFont_defect_default_every_call",
                        [register_scenario("Arial", 8.0, SCRIPT_FONTS[:1], "the second font")]), "defaultName")


@register("font.defect.RegisterFont_compares_the_family_name_without_case")
def defect_case():
    fails(register_case("bc_TGFontManager_RegisterFont_defect_case_insensitive",
                        [register_scenario("tahoma", 8.0, [("Tahoma", 8, "M", "F")], "another case")]), "families")


@register("font.defect.RegisterFont_makes_a_new_family_for_every_call")
def defect_new_family():
    fails(register_case("bc_TGFontManager_RegisterFont_defect_new_family_every_call",
                        [register_scenario("Crillee", 6.0, SCRIPT_FONTS[:1], "a second size")]), "families")


@register("font.defect.RegisterFont_does_not_store_the_default_size")
def defect_no_default_size():
    fails(register_case("bc_TGFontManager_RegisterFont_defect_no_default_size",
                        [register_scenario("Crillee", 5.0, [], "the first font")]), "defaultSize")


@register("font.defect.RegisterFont_puts_a_new_family_at_the_head_of_the_list")
def defect_family_at_head():
    fails(register_case("bc_TGFontManager_RegisterFont_defect_family_at_head",
                        [register_scenario("Arial", 8.0, SCRIPT_FONTS[:6], "a new family")]), "families")


def add_case(symbol, scenarios, env=E53):
    return font_case(env, 0x734C20, 0x734DAF, "thiscall", ADD, None, symbol, scenarios)


def add_scenario(size, label, pre=None):
    return font_scenario({"f": Obj("FontFace")}, [Root("f"), size, narrow("Mod"), narrow("Fn")],
                         pre or [Pre("fface", [Root("f"), narrow("Crillee")])], label)


@register("font.defect.AddRegistration_names_the_icon_group_with_one_decimal")
def defect_one_decimal():
    fails(add_case("bc_TGFontFace_AddRegistration_defect_one_decimal", [add_scenario(5.0, "size 5")]), "groupName")


@register("font.defect.AddRegistration_swaps_the_module_and_the_function")
def defect_swapped():
    fails(add_case("bc_TGFontFace_AddRegistration_defect_swapped_texts", [add_scenario(5.0, "size 5")]), "moduleName")


@register("font.defect.AddRegistration_stores_the_size_as_an_integer")
def defect_integer_size():
    fails(add_case("bc_TGFontFace_AddRegistration_defect_integer_size", [add_scenario(9.5, "size 9.5")]), "size")


@register("font.defect.AddRegistration_names_the_icon_group_with_the_format_of_the_host")
def defect_host_format():
    fails(add_case("bc_TGFontFace_AddRegistration_defect_host_format", [add_scenario(0.125, "a tie in the second decimal")]),
          "groupName")


@register("font.defect.TGFontFace_destructor_frees_the_registrations")
def defect_face_destructor():
    case = font_case(E53, 0x7349B0, 0x734A2F, "thiscall", [PTR], None, "bc_TGFontFace_Destructor_defect_frees_registrations",
                     [font_scenario({"f": Obj("FontFace")}, [Root("f")], face_with([5, 6]), "two registrations")],
                     allow_leaks=True)
    fails(case, "heap")


@register("font.defect.TGFontManager_destructor_keeps_the_default_name")
def defect_manager_destructor():
    case = font_case(E53, 0x734840, 0x7348E6, "thiscall", [PTR], None, "bc_TGFontManager_DestructorBody_defect_keeps_default_name",
                     [font_scenario({}, [Root("fm")], register_pre(SCRIPT_FONTS[:1]), "a default font")], allow_leaks=True)
    fails(case, "heap")


@register("font.defect.TGFontManager_constructor_sets_the_block_size_3")
def defect_block_size():
    case = font_case(E53, 0x734810, 0x734837, "thiscall", [PTR], PTR, "bc_TGFontManager_TGFontManager_defect_block_size",
                     [Scenario({"m": Obj("FontManager")}, [Root("m")], label="a blank object", setup=font_setup())])
    fails(case, "blockSize")


@register("font.defect.static_initializer_does_not_register_the_destructor")
def defect_no_cleanup():
    case = font_case(E53, 0x7347D0, 0x7347DF, "cdecl", [], None, "bc_TGFontManager_InitGlobal_defect_no_cleanup",
                     [font_scenario({}, [], label="the global is blank")], stubs=atexit_stubs(),
                     probes=[log_probe(), trap_probe(), atexit_probe()])
    fails(case, "atexit")
