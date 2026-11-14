"""Tests of the chain that gives an image its size: the constructors of the image classes, the creation of an image from a
file (0x7e0c50), the renderer (0x7c2a10), the texture manager (0x7d6f30), the search for a surface (0x7c6370), the load of
the file (0x7c5c60) and the loader of the files (0x7e15a0, 0x819000).

The exe code runs in the emulator as it is. The device functions (0x7c5200, 0x7c5330, 0x7c6080, 0x7c59e0), the COM calls of
DirectDraw, the file layer and the C run time are stubs that write one line each into a log, like the test functions of the
native side. The graphs of the objects, the log, the counters and the error code are compared after each call.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_image_common import *  # noqa: F401,F403,E402
from emuharness import Inner  # noqa: E402
from icon_image_common import (CHAIN_SETUP, chain_case, counters_probe, dd_image, device, image_list_global,  # noqa: E402
                               image_scenario, log_probe, raw_bytes_probe, raw_image, renderer_list_global,
                               scratch_raw_global, slot_array, surface, surface_record, texture_format, tga_state_probe)
from icon_image_scenarios import *  # noqa: F401,F403,E402


@register("icon.image.NiImage_constructor")
def image_constructor_test():
    scenarios = [image_scenario({"image": Obj("NiImage")}, [Root("image")], [], [], "counter %d, objects %d" % (c, n),
                                image_counter=c, object_count=n)
                 for c in (1, 2, 40, 0xFFFFFFFF) for n in (0, 7)]
    return chain_case(0x7E12F0, 0x7E1338, "thiscall", [PTR], PTR, "bc_NiImage_NiImage", scenarios,
                      setup_calls=CHAIN_SETUP)


@register("icon.image.NiDDImage_Create")
def dd_create_test():
    scenarios = []
    for before in range(0, 6):
        for count in (0, 3):
            pre = [Pre("create", []) for _ in range(before)]
            scenarios.append(image_scenario({}, [], pre, [image_list_global()],
                                            "%d images before, count %d" % (before, count), dd_count=count,
                                            image_counter=5))
    return chain_case(0x7C6490, 0x7C65ED, "cdecl", [], PTR, "bc_NiDDImage_Create", scenarios, ret_class="NiDDImage",
                      setup_calls=CHAIN_SETUP)


@register("icon.image.NiDDImage_GetSurface_search")
def get_surface_search_test():
    return get_surface_case("bc_NiDDImage_GetSurface", search_scenarios())


@register("icon.image.NiDDImage_GetSurface_creation")
def get_surface_creation_test():
    return get_surface_case("bc_NiDDImage_GetSurface", creation_scenarios())


@register("icon.image.NiDDImage_GetSurface_errors")
def get_surface_errors_test():
    return get_surface_case("bc_NiDDImage_GetSurface", error_scenarios())


@register("icon.image.NiDDImage_LoadFile")
def load_file_test():
    return load_file_case("bc_NiDDImage_LoadFile", load_file_scenarios())


@register("icon.image.NiD3DRender_AcceptImage")
def accept_renderer_test():
    return chain_case(0x7C2A10, 0x7C2A61, "thiscall", [PTR, PTR], U8, "bc_NiD3DRender_AcceptImage", accept_scenarios("renderer"),
                      setup_calls=CHAIN_SETUP)


@register("icon.image.TextureManager_AcceptImage")
def accept_manager_test():
    return chain_case(0x7D6F30, 0x7D6F6C, "thiscall", [PTR, PTR], U8, "bc_TextureManager_AcceptImage", accept_scenarios("manager"),
                      setup_calls=CHAIN_SETUP)


@register("icon.image.NiImage_CreateFromFile")
def create_from_file_test():
    scenarios = [s for s in create_scenarios() if s.args[0] != 0]
    return chain_case(0x7E0C50, 0x7E0D5C, "cdecl", [Buf(io="in"), U32, U32, U32], PTR, "bc_NiImage_CreateFromFile", scenarios,
                      ret_class="NiDDImage", setup_calls=CHAIN_SETUP)


@register("icon.image.NiImage_CreateFromFile_without_a_path")
def create_from_file_null_test():
    scenarios = [s for s in create_scenarios() if s.args[0] == 0]
    return chain_case(0x7E0C50, 0x7E0D5C, "cdecl", [PTR, U32, U32, U32], PTR, "bc_NiImage_CreateFromFile", scenarios,
                      ret_class="NiDDImage", setup_calls=CHAIN_SETUP)


@register("icon.image.loadTextureFileByExtension")
def load_by_extension_test():
    return chain_case(0x819000, 0x81926A, "cdecl", [Buf(io="in"), PTR], U8, "bc_LoadTextureFileByExtension",
                      loader_scenarios(False), setup_calls=CHAIN_SETUP,
                      probes=[log_probe(), trap_probe(), counters_probe(), raw_bytes_probe("raw"), tga_state_probe()])


@register("icon.image.loadTextureFileByExtension_without_a_path")
def load_by_extension_null_test():
    return chain_case(0x819000, 0x81926A, "cdecl", [PTR, PTR], U8, "bc_LoadTextureFileByExtension", loader_scenarios(True),
                      setup_calls=CHAIN_SETUP)


@register("icon.image.loadRawImageFile")
def load_raw_image_file_test():
    scenarios = [raw_file_scenario(name, name[:40], file=tga(4, 4, 24, 2)) for name in
                 ("Data/a.tga", "Data/a.bmp", "Data/a.rgb", "Data/a.xyz", "a.nif")]
    scenarios.append(raw_file_scenario("Data/a.tga", "a file that does not open", stream_open=0))
    scenarios.append(raw_file_scenario("Data/a.bmp", "a bmp file that the loader refuses", bmp_ok=0))
    scenarios.append(raw_file_scenario("Data/a.tga", "a tga file with a second picture", file=tga(7, 2, 32, 4)))
    return chain_case(0x7E15A0, 0x7E15BF, "cdecl", [Buf(io="in")], PTR, "bc_LoadRawImageFile", scenarios, ret_class="NiRawImageData",
                      setup_calls=CHAIN_SETUP)


@register("icon.image.NiRawImageData_deleting_destructor_0x7e0900")
def raw_destroy_test():
    return chain_case(0x7E0900, 0x7E091B, "thiscall", [PTR, U32], PTR, "bc_NiRawImageData_DeletingDestructor",
                      raw_destroy_scenarios(), setup_calls=CHAIN_SETUP, allow_leaks=True,
                      model=destroyed_model("NiRawImageData"))


@register("icon.image.NiImage_deleting_destructor_0x7e1f80")
def image_destroy_test():
    return chain_case(0x7E1F80, 0x7E1F9B, "thiscall", [PTR, U32], PTR, "bc_NiImage_DeletingDestructor",
                      image_destroy_scenarios(), setup_calls=CHAIN_SETUP, allow_leaks=True, model=destroyed_model("NiImage"))


@register("icon.image.NiObjectNET_constructor_0x7dac80")
def object_net_ctor_test():
    return chain_case(0x7DAC80, 0x7DAD0B, "thiscall", [PTR], PTR, "bc_NiObjectNET_NiObjectNET",
                      property_constructor_scenarios("NiObjectNET"), setup_calls=CHAIN_SETUP)


@register("icon.image.NiTextureProperty_constructor_0x809fe0")
def property_ctor_test():
    return chain_case(0x809FE0, 0x80A04B, "thiscall", [PTR], PTR, "bc_NiTextureProperty_NiTextureProperty",
                      property_constructor_scenarios("NiTextureProperty"), setup_calls=CHAIN_SETUP)


@register("icon.image.NiObjectNET_destructor_body_0x7dad10")
def object_net_destroy_test():
    return chain_case(0x7DAD10, 0x7DADF9, "thiscall", [PTR], None, "bc_NiObjectNET_Destroy",
                      property_destroy_scenarios("NiObjectNET", False), setup_calls=CHAIN_SETUP, allow_leaks=True,
                      model=destroyed_model("NiObjectNET"))


@register("icon.image.NiTextureProperty_destructor_body_0x80a1d0")
def property_destroy_test():
    return chain_case(0x80A1D0, 0x80A263, "thiscall", [PTR], None, "bc_NiTextureProperty_Destroy",
                      property_destroy_scenarios("NiTextureProperty", False), setup_calls=CHAIN_SETUP, allow_leaks=True,
                      model=destroyed_model("NiTextureProperty"))


@register("icon.image.NiTextureProperty_deleting_destructor_0x80a8a0")
def property_deleting_test():
    return chain_case(0x80A8A0, 0x80A8BB, "thiscall", [PTR, U32], PTR, "bc_NiTextureProperty_DeletingDestructor",
                      property_destroy_scenarios("NiTextureProperty", True), setup_calls=CHAIN_SETUP, allow_leaks=True,
                      model=destroyed_model("NiTextureProperty"))
