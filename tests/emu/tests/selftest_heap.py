"""Self-tests of the guard bytes around heap blocks on both sides.

The original side is hand-written x86 code in the scratch region. It writes a dword just behind the block of a
TGObject (8 bytes) or just in front of it. The native side is a function of the test library that writes behind or
in front of the block of its TGObject. Each side must report the block, its class and the offset.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from prop_common import LIB, MODEL, failing_text  # noqa: E402
from emuharness import SCRATCH  # noqa: E402

CODE_OVERRUN = SCRATCH + 0x2700
OVERRUN = bytes.fromhex("8B442404 C74008 44332211 31C0 C3")
CODE_UNDERRUN = SCRATCH + 0x2740
UNDERRUN = bytes.fromhex("8B442404 C740FC 44332211 31C0 C3")
CODE_UNTOUCHED = SCRATCH + 0x2780
UNTOUCHED = bytes.fromhex("31C0C3")
MEMORY = {CODE_OVERRUN: OVERRUN, CODE_UNDERRUN: UNDERRUN, CODE_UNTOUCHED: UNTOUCHED}


def heap_case(code, size, symbol):
    original = Original(code, size, "cdecl", [PTR], U32)
    scenario = Scenario({"object": Obj("TGObject", id=5)}, [Root("object")], label="one TGObject")
    return ObjectCase(MODEL, original, Native(LIB, symbol), [scenario], memory=MEMORY)


@register("selftest.heap.code_that_stays_inside_its_blocks_is_clean")
def clean():
    return heap_case(CODE_UNTOUCHED, len(UNTOUCHED), "bc_heap_untouched")


@register("selftest.heap.planted_overrun_on_the_original_side_names_block_class_and_offset")
def original_overrun():
    text = failing_text(heap_case(CODE_OVERRUN, len(OVERRUN), "bc_heap_untouched"))
    assert "original heap fault: heap block overrun: block of 8 bytes at 0x" in text, text
    assert "(TGObject): the byte at offset 8 (1 past the end) was changed" in text, text
    assert "native heap block overrun" not in text, text


@register("selftest.heap.planted_overrun_on_the_native_side_names_block_class_and_offset")
def native_overrun():
    text = failing_text(heap_case(CODE_UNTOUCHED, len(UNTOUCHED), "bc_heap_overrun_defect"))
    assert "native heap block overrun: block of 16 bytes at 0x" in text, text
    assert "(TGObject): the byte at offset 16 (1 past the end) was changed" in text, text
    assert "original heap fault" not in text, text


@register("selftest.heap.planted_write_before_the_start_is_found_on_both_sides")
def underrun():
    text = failing_text(heap_case(CODE_UNDERRUN, len(UNDERRUN), "bc_heap_underrun_defect"))
    assert "original heap fault: heap block overrun: block of 8 bytes at 0x" in text, text
    assert "native heap block overrun: block of 16 bytes at 0x" in text, text
    assert text.count("offset -4 (before the start) was changed") == 2, text
