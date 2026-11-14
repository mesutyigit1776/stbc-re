"""Planted defects of the image tests: each native variant in src/emutest/IconImageDefects.cpp is wrong in one way, and the
test of the function must find it. A test that passes on a defect would not show a mistake in the real source.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_image_common import *  # noqa: F401,F403,E402
from icon_image_common import (SETUP_CALLS, tga_case, tga_scenarios, allocate_scenarios, counters_probe, image_case, log_probe,  # noqa: E402
                               mip_scenarios, raw_bytes_probe, reallocation_scenarios)
from prop_common import failing_text, trap_probe  # noqa: E402

RAW_PROBES = [log_probe(), trap_probe(), counters_probe(), raw_bytes_probe("raw")]


def passes(case):
    results = case.run()
    assert all(r.ok for r in results), "the case with the real function must pass:\n" + "\n".join(
        line for r in results for line in r.lines)


def fails(case, fragment):
    text = failing_text(case)
    assert fragment in text, "expected %r in the report:\n%s" % (fragment, text)


def allocate_case(symbol, scenarios):
    return image_case(0x7DE4A0, 0x7DE631, "thiscall", [PTR, U32, U32, U32], U8, symbol, scenarios,
                      setup_calls=SETUP_CALLS, probes=RAW_PROBES)


@register("icon.defect.Allocate_rounds_the_size_to_a_power_of_two")
def defect_allocate_round():
    passes(allocate_case("bc_NiRawImageData_Allocate", allocate_scenarios()))
    fails(allocate_case("bc_NiRawImageData_Allocate_defect_round", allocate_scenarios()), ".width")


@register("icon.defect.Allocate_swaps_the_width_and_the_height")
def defect_allocate_swap():
    fails(allocate_case("bc_NiRawImageData_Allocate_defect_swap", allocate_scenarios()), ".width")


@register("icon.defect.Allocate_gives_the_format_1_four_bytes_for_each_pixel")
def defect_allocate_four_bytes():
    fails(allocate_case("bc_NiRawImageData_Allocate_defect_four_bytes", allocate_scenarios()), ".bytesPerPixel")


@register("icon.defect.Allocate_makes_an_image_of_the_same_size_again")
def defect_allocate_always():
    fails(allocate_case("bc_NiRawImageData_Allocate_defect_always", reallocation_scenarios()), "heap blocks")


@register("icon.defect.getMipLevelCount_takes_the_floor_of_the_logarithm")
def defect_mip_floor():
    def case(symbol):
        return image_case(0x7E15C0, 0x7E1604, "cdecl", [U32, U32], U32, symbol, mip_scenarios(), code=[])
    passes(case("bc_GetMipLevelCount"))
    fails(case("bc_GetMipLevelCount_defect_floor"), "ret")


TGA_DEFECTS = (("swaps_the_width_and_the_height_fields", "swap", ".width"),
               ("rounds_the_width_up_to_a_power_of_two", "round", ".width"),
               ("ignores_the_bit_of_the_row_order", "row_order", "pixels"),
               ("ignores_the_interleave", "interleave", "pixels"),
               ("swaps_red_and_blue", "red_blue", "pixels"))


def _tga_defect(name, fragment):
    def run():
        fails(tga_case("bc_LoadTgaFile_defect_" + name, tga_scenarios()), fragment)
    return run


for _title, _name, _fragment in TGA_DEFECTS:
    register("icon.defect.loadTgaFile_" + _title)(_tga_defect(_name, _fragment))


# The image chain -------------------------------------------------------------------------------------------------------------

from icon_image_scenarios import (GET_SURFACE_ARGS, LOAD_FILE_ARGS, accept_scenarios, create_renderers,  # noqa: E402
                                  create_scenario, create_scenarios, creation_scenarios, get_surface_case, load_file_case,
                                  load_file_scenarios, loader_scenarios, search_scenarios, tga)
from icon_image_common import CHAIN_SETUP, chain_case, image_scenario  # noqa: E402


def chain_defect(title, builder, fragment):
    register("icon.defect." + title)(lambda: fails(builder(), fragment))


chain_defect("NiImage_constructor_stores_the_scale_128",
             lambda: chain_case(0x7E12F0, 0x7E1338, "thiscall", [PTR], PTR, "bc_NiImage_NiImage_defect_scale",
                                [image_scenario({"image": Obj("NiImage")}, [Root("image")], [], [], "a blank image")],
                                setup_calls=CHAIN_SETUP), ".scale")

chain_defect("NiDDImage_Create_does_not_enter_the_image_into_the_list",
             lambda: chain_case(0x7C6490, 0x7C65ED, "cdecl", [], PTR, "bc_NiDDImage_Create_defect_list",
                                [image_scenario({}, [], [Pre("create", [])], [image_list_global()], "an image after another image")],
                                ret_class="NiDDImage", setup_calls=CHAIN_SETUP), "imageList")


def create_defect_case(symbol, scenario_filter):
    scenarios = [s for s in create_scenarios() if s.args[0] != 0 and scenario_filter(s)]
    return chain_case(0x7E0C50, 0x7E0D5C, "cdecl", [Buf(io="in"), U32, U32, U32], PTR, symbol, scenarios, ret_class="NiDDImage",
                      setup_calls=CHAIN_SETUP)


chain_defect("CreateFromFile_sets_the_flag_2_for_the_mode_2_without_the_enable_byte",
             lambda: create_defect_case("bc_NiImage_CreateFromFile_defect_flags", lambda s: True), ".flags")
chain_defect("CreateFromFile_stores_the_mask_only_for_the_mode_1",
             lambda: create_defect_case("bc_NiImage_CreateFromFile_defect_mask", lambda s: True), ".mask")


def accept_defect_case(first, last, symbol, root):
    return chain_case(first, last, "thiscall", [PTR, PTR], U8, symbol, accept_scenarios(root), setup_calls=CHAIN_SETUP)


chain_defect("AcceptImage_clears_the_flag_1_of_the_image",
             lambda: accept_defect_case(0x7C2A10, 0x7C2A61, "bc_NiD3DRender_AcceptImage_defect", "renderer"), ".flags")
chain_defect("TextureManager_AcceptImage_takes_the_bit_1_of_the_flags",
             lambda: accept_defect_case(0x7D6F30, 0x7D6F6C, "bc_TextureManager_AcceptImage_defect", "manager"), "")
chain_defect("GetSurface_takes_a_record_of_another_device",
             lambda: get_surface_case("bc_NiDDImage_GetSurface_defect_device", search_scenarios()), "")
chain_defect("GetSurface_does_not_fill_a_dirty_record_again",
             lambda: get_surface_case("bc_NiDDImage_GetSurface_defect_dirty", search_scenarios()), "")
chain_defect("LoadFile_swaps_the_width_and_the_height",
             lambda: load_file_case("bc_NiDDImage_LoadFile_defect_swap", load_file_scenarios()), ".width")
chain_defect("LoadFile_leaves_the_mip_level_count_at_0",
             lambda: load_file_case("bc_NiDDImage_LoadFile_defect_mips", load_file_scenarios()), ".mipLevels")
def slot_order_scenarios():
    from icon_image_scenarios import SLOT_SETS, dd_image, load_file_scenario
    return [load_file_scenario(dd_image(mask=mask), "%s slots, mask %d" % (slots, mask), slots=SLOT_SETS[slots],
                               bit2=bit2, file=tga(4, 4, 24, mask))
            for slots in ("all", "sparse", "last") for mask in (5, 7, 8) for bit2 in (0, 1)]


chain_defect("LoadFile_searches_the_slots_from_the_other_end",
             lambda: load_file_case("bc_NiDDImage_LoadFile_defect_slots", slot_order_scenarios()), "")


def loader_defect_case(symbol):
    return chain_case(0x819000, 0x81926A, "cdecl", [Buf(io="in"), PTR], U8, symbol, loader_scenarios(False), setup_calls=CHAIN_SETUP)


chain_defect("loadTextureFileByExtension_compares_the_extension_with_the_case",
             lambda: loader_defect_case("bc_LoadTextureFileByExtension_defect_case"), "ret")
chain_defect("loadTextureFileByExtension_does_not_know_the_extension_int",
             lambda: loader_defect_case("bc_LoadTextureFileByExtension_defect_int"), "ret")



from icon_image_scenarios import (image_list_init_scenarios, init_case, release_image_list_scenarios,  # noqa: E402
                                  scratch_raw_init_scenarios)

chain_defect("InitImageList_gives_the_list_the_block_size_1",
             lambda: init_case(0x7C4D40, 0x7C4D70, "bc_NiImage_InitImageList_defect", image_list_init_scenarios(),
                               allow_leaks=True), "blockSize")
chain_defect("ReleaseImageList_does_not_look_at_the_guard_bit",
             lambda: init_case(0x7C4D80, 0x7C4DE2, "bc_NiImage_ReleaseImageList_defect", release_image_list_scenarios(),
                               allow_leaks=True), "")
chain_defect("InitScratchRawImage_takes_no_reference",
             lambda: init_case(0x7E0AE0, 0x7E0AF0, "bc_NiImage_InitScratchRawImage_defect", scratch_raw_init_scenarios()),
             "refCount")


from icon_image_scenarios import destroyed_model, image_destroy_scenarios, raw_destroy_scenarios  # noqa: E402

chain_defect("RawImage_destructor_does_not_free_the_palette",
             lambda: chain_case(0x7E0900, 0x7E091B, "thiscall", [PTR, U32], PTR, "bc_NiRawImageData_DeletingDestructor_defect",
                                raw_destroy_scenarios(), setup_calls=CHAIN_SETUP, allow_leaks=True,
                                model=destroyed_model("NiRawImageData")), "")
chain_defect("NiImage_destructor_does_not_release_the_raw_image",
             lambda: chain_case(0x7E1F80, 0x7E1F9B, "thiscall", [PTR, U32], PTR, "bc_NiImage_DeletingDestructor_defect",
                                image_destroy_scenarios(), setup_calls=CHAIN_SETUP, allow_leaks=True,
                                model=destroyed_model("NiImage")), "")


from icon_image_scenarios import property_constructor_scenarios, property_destroy_scenarios  # noqa: E402

chain_defect("NiTextureProperty_constructor_sets_the_flags_to_1",
             lambda: chain_case(0x809FE0, 0x80A04B, "thiscall", [PTR], PTR, "bc_NiTextureProperty_NiTextureProperty_defect",
                                property_constructor_scenarios("NiTextureProperty"), setup_calls=CHAIN_SETUP), ".flags")
chain_defect("NiObjectNET_destructor_body_does_not_release_the_controller",
             lambda: chain_case(0x7DAD10, 0x7DADF9, "thiscall", [PTR], None, "bc_NiObjectNET_Destroy_defect",
                                property_destroy_scenarios("NiObjectNET", False), setup_calls=CHAIN_SETUP, allow_leaks=True,
                                model=destroyed_model("NiObjectNET")), "")
chain_defect("NiTextureProperty_destructor_body_does_not_release_the_image",
             lambda: chain_case(0x80A1D0, 0x80A263, "thiscall", [PTR], None, "bc_NiTextureProperty_Destroy_defect",
                                property_destroy_scenarios("NiTextureProperty", False), setup_calls=CHAIN_SETUP, allow_leaks=True,
                                model=destroyed_model("NiTextureProperty")), "")
chain_defect("NiTextureProperty_deleting_destructor_frees_the_block_for_the_flag_0",
             lambda: chain_case(0x80A8A0, 0x80A8BB, "thiscall", [PTR, U32], PTR, "bc_NiTextureProperty_DeletingDestructor_defect",
                                property_destroy_scenarios("NiTextureProperty", True), setup_calls=CHAIN_SETUP, allow_leaks=True,
                                model=destroyed_model("NiTextureProperty")), "")
