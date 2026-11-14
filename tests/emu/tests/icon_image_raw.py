"""Tests of the raw image: the constructor 0x7de3e0 and Allocate 0x7de4a0, which makes the pixel block and the palette.

The tests bring the image into a state with Allocate itself (a setup call on both sides), so that the second call
frees the blocks of the first.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_image_common import *  # noqa: F401,F403,E402
from icon_image_common import (SETUP_CALLS, allocate_scenarios, image_case, image_scenario, mip_scenarios,  # noqa: E402
                               raw_bytes_probe, reallocation_scenarios)

@register("icon.image.NiRawImageData_constructor")
def constructor_test():
    scenarios = [image_scenario({"raw": Obj("NiRawImageData")}, [Root("raw")], [], [], "a blank object", object_count=n)
                 for n in (0, 5, 100)]
    return image_case(0x7DE3E0, 0x7DE410, "thiscall", [PTR], PTR, "bc_NiRawImageData_NiRawImageData", scenarios,
                      setup_calls=SETUP_CALLS)


@register("icon.image.NiRawImageData_Allocate_from_a_new_image")
def allocate_new_test():
    return image_case(0x7DE4A0, 0x7DE631, "thiscall", [PTR, U32, U32, U32], U8, "bc_NiRawImageData_Allocate",
                      allocate_scenarios(), setup_calls=SETUP_CALLS,
                      probes=[log_probe(), trap_probe(), counters_probe(), raw_bytes_probe("raw")])


@register("icon.image.NiRawImageData_Allocate_again")
def allocate_again_test():
    return image_case(0x7DE4A0, 0x7DE631, "thiscall", [PTR, U32, U32, U32], U8, "bc_NiRawImageData_Allocate",
                      reallocation_scenarios(), setup_calls=SETUP_CALLS,
                      probes=[log_probe(), trap_probe(), counters_probe(), raw_bytes_probe("raw")])


@register("icon.image.getMipLevelCount")
def mip_level_test():
    return image_case(0x7E15C0, 0x7E1604, "cdecl", [U32, U32], U32, "bc_GetMipLevelCount", mip_scenarios(), code=[])
