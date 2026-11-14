"""Tests of the static initializers of the image code (0x7c4d40, 0x7e0ba0, 0x7e0ae0 with 0x7e0b00) and of the functions that
they register with atexit (0x7c4d80, 0x7e0be0, 0x7e0b60)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_image_common import *  # noqa: F401,F403,E402
from icon_image_scenarios import (image_list_init_scenarios, init_case, release_image_list_scenarios,  # noqa: E402
                                  release_renderer_list_scenarios, release_scratch_scenarios, renderer_list_init_scenarios,
                                  scratch_raw_init_scenarios)


@register("icon.image.static_initializer_0x7c4d40")
def image_list_init_test():
    return init_case(0x7C4D40, 0x7C4D70, "bc_NiImage_InitImageList", image_list_init_scenarios(), allow_leaks=True)


@register("icon.image.static_initializer_0x7e0ba0")
def renderer_list_init_test():
    return init_case(0x7E0BA0, 0x7E0BD0, "bc_NiImage_InitRendererList", renderer_list_init_scenarios(), allow_leaks=True)


@register("icon.image.static_initializer_0x7e0ae0")
def scratch_raw_init_test():
    return init_case(0x7E0AE0, 0x7E0AF0, "bc_NiImage_InitScratchRawImage", scratch_raw_init_scenarios())


@register("icon.image.release_image_list_0x7c4d80")
def release_image_list_test():
    return init_case(0x7C4D80, 0x7C4DE2, "bc_NiImage_ReleaseImageList", release_image_list_scenarios(), allow_leaks=True)


@register("icon.image.release_renderer_list_0x7e0be0")
def release_renderer_list_test():
    return init_case(0x7E0BE0, 0x7E0C42, "bc_NiImage_ReleaseRendererList", release_renderer_list_scenarios(), allow_leaks=True)


@register("icon.image.release_scratch_raw_image_0x7e0b60")
def release_scratch_test():
    return init_case(0x7E0B60, 0x7E0B94, "bc_NiImage_ReleaseScratchRawImage", release_scratch_scenarios())


@register("icon.traps.image_boundaries_and_unreproduced_cases_reach_their_exe_address")
def image_traps_test():
    import ctypes
    from emuharness.native import load_library
    lib = load_library(LIB)
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_image_call_trap.argtypes = [ctypes.c_int32]
    expected = [0x7C4F30, 0x7C5200, 0x7C5330, 0x7C6080, 0x7C59E0, 0x86E550, 0x86E620, 0x817A40, 0x823F60, 0x824710, None,
                None, 0x8256DC, 0x824E80, 0x819047, 0x7C6370, 0x7C5D3E]
    for which, address in enumerate(expected):
        if address is None:
            continue
        lib.bc_trap_clear()
        lib.bc_image_call_trap(which)
        found = [lib.bc_trap_address(i) for i in range(lib.bc_trap_count())]
        assert found == [address], (which, hex(address), [hex(a) for a in found])
