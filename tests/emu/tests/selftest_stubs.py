"""Self-tests for stubs, imports and the stop on an unstubbed target.

0x85AEAD is a string duplicate function (CRT). It tests its argument for null,
calls strlen (0x860440), calls an allocator (0x85B028) with length + 1, and
calls a copy function (0x863840) with the new block and the source.
0x78FA52 is a six-byte function that calls the import mss32.dll!_AIL_sample_playback_rate@4.
"""
from emuharness import (Buf, HarnessError, Machine, Original, Stub, UnstubbedTarget, U32,
                        expect_error, malloc_stub, register)
from emuharness.case import EXE_PATH
from emuharness.stubs import HEAP_BASE, HEAP_SIZE

STRDUP = 0x85AEAD
STRDUP_SIZE = 0x2B
STRLEN = (0x860440, 0x7B)
ALLOC = 0x85B028
COPY = 0x863840
IMPORT_CALLER = 0x78FA52
IMPORT_NAME = "mss32.dll!_AIL_sample_playback_rate@4"


def strdup_original(allow):
    return Original(STRDUP, STRDUP_SIZE, "cdecl", [Buf(io="in")], U32, allow=allow)


def copy_stub():
    def copy(ctx):
        dest, src = ctx.arg(0), ctx.arg(1)
        data = bytearray()
        while True:
            byte = ctx.read(src + len(data), 1)
            data += byte
            if byte == b"\0":
                break
        ctx.write(dest, bytes(data))
        ctx.returns(dest)
    return Stub(copy, nargs=2, conv="cdecl")


@register("selftest.unstubbed_call_stops_with_site_and_target")
def unstubbed_call():
    machine = Machine(EXE_PATH)
    with expect_error(UnstubbedTarget, "0x%08x" % ALLOC, "call returns to") as box:
        machine.call(strdup_original([STRLEN]), [b"hello\0"])
    assert "0x0085aec3" in str(box[0]), str(box[0])


@register("selftest.unstubbed_import_stops_with_import_name")
def unstubbed_import():
    machine = Machine(EXE_PATH)
    func = Original(IMPORT_CALLER, 7, "cdecl", [], U32)
    with expect_error(UnstubbedTarget, IMPORT_NAME):
        machine.call(func, [])


@register("selftest.import_stub_sets_return_value")
def stubbed_import():
    calls = []

    def playback_rate(ctx):
        calls.append(ctx.reg("esp"))
        ctx.returns(0x1234)

    machine = Machine(EXE_PATH, {IMPORT_NAME: Stub(playback_rate, nargs=0)})
    out = machine.call(Original(IMPORT_CALLER, 7, "cdecl", [], U32), [])
    assert out.eax == 0x1234, hex(out.eax)
    assert len(calls) == 1, calls


@register("selftest.stubs_for_allocator_and_copy_run_strdup")
def strdup_with_stubs():
    machine = Machine(EXE_PATH, {ALLOC: malloc_stub(), COPY: copy_stub()})
    for text in (b"", b"a", b"hello world", b"x" * 100):
        out = machine.call(strdup_original([STRLEN]), [text + b"\0"])
        assert HEAP_BASE <= out.eax < HEAP_BASE + HEAP_SIZE, hex(out.eax)
        assert machine.read(out.eax, len(text) + 1) == text + b"\0"


@register("selftest.stack_imbalance_is_reported")
def wrong_convention():
    machine = Machine(EXE_PATH)
    wrong = Original(0x469620, 27, "thiscall", [Buf(io="in"), Buf(io="in"), U32], None)
    with expect_error(HarnessError, "stack pointer after return"):
        machine.call(wrong, [bytes(12), bytes(12), 0])
