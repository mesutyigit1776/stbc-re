"""Tests of the image file loaders: the TGA loader 0x824c70 and the pixel reader 0x8254e0 that it calls.

The stream is a stub on both sides: slot 2 of the stream and the function 0x86e6a0 write one line for each call, so the
reads of the loader (the number of bytes and the order) are compared too. The globals of the loader (the colour map,
the last pixel and the packet counters) and the pixels are compared after each call.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_image_common import *  # noqa: F401,F403,E402
from icon_image_common import (tga_case, tga_colour_map_scenarios, tga_files, tga_leftover_scenarios,  # noqa: E402
                               tga_real_scenarios, tga_scenario, tga_scenarios)


@register("icon.image.loadTgaFile")
def tga_test():
    return tga_case("bc_LoadTgaFile", tga_scenarios())


@register("icon.image.loadTgaFile_state_left_by_an_earlier_file")
def tga_leftover_test():
    return tga_case("bc_LoadTgaFile", tga_leftover_scenarios())


@register("icon.image.loadTgaFile_colour_map_with_alpha")
def tga_colour_map_test():
    return tga_case("bc_LoadTgaFile", tga_colour_map_scenarios())


@register("icon.image.loadTgaFile_files_of_the_game_and_the_mods")
def tga_real_test():
    return tga_case("bc_LoadTgaFile", tga_real_scenarios())
