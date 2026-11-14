"""Shared definitions for the tests of the image code: the raw image, the loaders of the image formats, the image classes
and the chain from the renderer to the width and the height of an image (0x7de3e0, 0x7de4a0, 0x7e15c0, 0x824c70, 0x7e0c50
and the functions around them).

The code is integer and pointer code, so each test runs once. The exe calls into the file layer and the Direct3D layer
are stubs; the native side has test functions that do the same. Both write one text line per call into a log, and the log
is compared after the tested call.
"""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from prop_common import (LIB, native_api, orig, pool_stubs_zeroed, span, trap_probe, GUEST_TRAPS)  # noqa: E402
from emuharness import Probe  # noqa: E402

OBJECT_COUNT = 0x9A1478
IMAGE_COUNTER = 0x9783D0
VT_RAW_IMAGE = 0x898D44

# The exe code that the tests of the raw image run: the base constructor, the constructor and Allocate of the raw
# image, and the two functions that Allocate calls for the pixel block (0x8221f0 allocates, 0x822210 frees).
RAW_CODE = [span(0x7D87A0, 0x7D87B5), span(0x7DE3E0, 0x7DE410), span(0x7DE4A0, 0x7DE631), span(0x8221F0, 0x822204),
            span(0x822210, 0x82221A), span(0x7DE680, 0x7DE6DC), span(0x7D87F0, 0x7D8803)]

IMAGE_LOG = []


VT_STREAM = SCRATCH + 0xF000
STUB_READ = SCRATCH + 0xF100
SEEK = 0x86E6A0
TGA_STATE = 0x9A2E30
TGA_STATE_SIZE = 0x10024

# The exe code that the TGA loader runs: the loader, the pixel reader without alpha and the thunk that reads from a stream.
TGA_CODE = RAW_CODE + [span(0x824C70, 0x825419), span(0x8254E0, 0x825715), span(0x803650, 0x803663)]

FILE = {"data": b""}

# Exe addresses and vtables of the image chain.
VT_NIIMAGE, VT_NIDDIMAGE, VT_TEXTURE_MANAGER, VT_RENDERER = 0x898D78, 0x898A7C, 0x898B58, 0x898984
VT_DDSURFACE, VT_DDDEVICE = SCRATCH + 0xF200, SCRATCH + 0xF280
RENDERER_LIST, IMAGE_LIST, DD_COUNT, IMAGE_ERROR = 0x9A1660, 0x9A11E8, 0x9A123C, 0x9A168C
FLAG2, FLAG4, FACTORY, SCRATCH_RAW = 0x9A1689, 0x9A168A, 0x9A1694, 0x9A165C
STUB = {name: SCRATCH + 0xF400 + 0x10 * i for i, name in enumerate(
    ("delete_image", "is_open", "release", "set_palette", "create_palette", "delete_raw"))}
ATEXIT = 0x8594E4
ATEXIT_LOG = []
GUARD_IMAGE_LIST, GUARD_RAW_RENDERER = 0x9A11E5, 0x9A163D
DEVICE = {}


def list_classes(prefix, data_class):
    """A pointer list with its node and block classes (the shape of TGList)."""
    return [
        ClassDef(prefix + "Node", 12, [Member("data", 0, Ptr(data_class)), Member("next", 4, Ptr(prefix + "Node")),
                                       Member("prev", 8, Ptr(prefix + "Node"))], counted=False),
        ClassDef(prefix + "Block", 8, [Member("nextBlock", 0, Ptr(prefix + "Block")),
                                       Member("nodes", 4, Ptr(prefix + "Node"))], counted=False),
        ClassDef(prefix + "List", 0x18, [
            Member("count", 0, I32), Member("head", 4, Ptr(prefix + "Node")), Member("tail", 8, Ptr(prefix + "Node")),
            Member("freeNodes", 0x0C, Ptr(prefix + "Node")), Member("blocks", 0x10, Ptr(prefix + "Block")),
            Member("blockSize", 0x14, U32)], counted=False),
    ]


def image_classes():
    image_members = [
        Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("name", 8, CSTR),
        Member("rawImage", 0x0C, Ptr("NiRawImageData")), Member("width", 0x10, U32), Member("height", 0x14, U32),
        Member("mipLevels", 0x18, U32), Member("id", 0x1C, U32), Member("reserved20", 0x20, U32),
        Member("flags", 0x24, U32), Member("mask", 0x28, U32), Member("scale", 0x2C, F32)]
    return list_classes("ImageSurface", "ImageSurfaceRecord") + list_classes("Renderer", "NiD3DRender") + \
        list_classes("ImageAll", "NiDDImage") + [
        ClassDef("NiImage", 0x30, image_members, vtable=VT_NIIMAGE, counted=False),
        ClassDef("NiDDImage", 0x5C, [
            Member("surfaces", 0x30, Inline("ImageSurfaceList")), Member("reserved48", 0x48, U32),
            Member("reserved4c", 0x4C, U32), Member("reserved50", 0x50, U32), Member("reserved54", 0x54, U32),
            Member("streamData", 0x58, Ptr("PixelBlock"))], vtable=VT_NIDDIMAGE, base="NiImage", counted=False),
        ClassDef("NiObjectNET", 0x14, [
            Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("name", 8, CSTR),
            Member("extraData", 0x0C, Ptr("NiRawImageData")), Member("controller", 0x10, Ptr("NiRawImageData"))],
            vtable=0x898C48, counted=False),
        ClassDef("NiTextureProperty", 0x1C, [
            Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("name", 8, CSTR),
            Member("extraData", 0x0C, Ptr("NiRawImageData")), Member("controller", 0x10, Ptr("NiRawImageData")),
            Member("flags", 0x14, U16), Member("texture", 0x18, Ptr("NiDDImage"))], vtable=0x89A288, counted=False),
        ClassDef("TextureFormat", 0x28, [Member("paletted", 0, U8), Member("id", 0x24, U32)], counted=False),
        ClassDef("ImageSurfaceRecord", 0x10, [
            Member("surface", 0, Ptr("DdSurface")), Member("formatId", 4, U32), Member("device", 8, Ptr("DdDevice")),
            Member("dirty", 0x0C, U8)], counted=False),
        ClassDef("DdSurface", 8, [Member("vtbl", 0, VTBL), Member("serial", 4, U32)], vtable=VT_DDSURFACE, counted=False),
        ClassDef("DdDevice", 8, [Member("vtbl", 0, VTBL), Member("serial", 4, U32)], vtable=VT_DDDEVICE, counted=False),
        ClassDef("SlotArray", 0x1C, [Member("slot%d" % i, 4 * i, Ptr("TextureFormat")) for i in range(7)], counted=False),
        ClassDef("SurfaceCell", 4, [Member("surface", 0, Ptr("DdSurface"))], counted=False),
        ClassDef("FormatCell", 4, [Member("format", 0, Ptr("TextureFormat"))], counted=False),
        ClassDef("RawImagePointer", 4, [Member("image", 0, Ptr("NiRawImageData"))], counted=False),
        ClassDef("TextureManager", 0xB0, [Member("vtbl", 0, VTBL)] + [Member("slot%d" % i, 8 + 4 * i, Ptr("TextureFormat"))
                                                                      for i in range(7)] + [
            Member("flags", 0xA8, U32), Member("device", 0xAC, Ptr("DdDevice"))], vtable=VT_TEXTURE_MANAGER, counted=False),
        ClassDef("NiD3DRender", 0xC8, [Member("vtbl", 0, VTBL), Member("textureManager", 0xC4, Ptr("TextureManager"))],
                 vtable=VT_RENDERER, counted=False),
        ClassDef("ImageStream", 8, [Member("vtbl", 0, VTBL), Member("position", 4, U32)], vtable=VT_STREAM, counted=False),
        ClassDef("PixelBlock", 1, [], counted=False),
        ClassDef("NiRawImageData", 0x24, [
            Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("format", 8, U32), Member("width", 0x0C, U32),
            Member("height", 0x10, U32), Member("bytesPerPixel", 0x14, U32), Member("stride", 0x18, U32),
            Member("palette", 0x1C, Ptr("PixelBlock")), Member("pixels", 0x20, Ptr("PixelBlock"))],
            vtable=VT_RAW_IMAGE, counted=False),
    ]


IMAGE_MODEL = Model(image_classes(), native_api(LIB))

STREAM_MEMORY = {VT_STREAM: struct.pack("<III", 0, STUB["is_open"], STUB_READ), STUB_READ: b"\xC3"}


def stream_stubs():
    """The stream that the loaders read: slot 2 reads, 0x86e6a0 moves the position. Both write a line into the log."""
    def read(ctx):
        stream, destination, count = ctx.arg(0), ctx.arg(1), ctx.arg(2)
        position = struct.unpack("<I", ctx.read(stream + 4, 4))[0]
        data = FILE["data"]
        ctx.write(destination, bytes(data[position + i] if position + i < len(data) else 0 for i in range(count)))
        ctx.write(stream + 4, struct.pack("<I", (position + count) & 0xFFFFFFFF))
        IMAGE_LOG.append("read %d" % count)
        ctx.returns(0)

    def seek(ctx):
        stream, offset, mode = ctx.arg(0), ctx.arg(1), ctx.arg(2)
        position = struct.unpack("<I", ctx.read(stream + 4, 4))[0]
        ctx.write(stream + 4, struct.pack("<I", (position + offset) & 0xFFFFFFFF))
        IMAGE_LOG.append("seek %d %d" % (struct.unpack("<i", struct.pack("<I", offset))[0], mode))
        ctx.returns(0)

    stubs = pool_stubs_zeroed()
    stubs[STUB_READ] = Stub(read, nargs=3, conv="thiscall")
    stubs[SEEK] = Stub(seek, nargs=3, conv="thiscall")
    return stubs


DEVICE_DEFAULTS = dict(create_ok=1, fill_ok=1, refill_ok=1, stream_data_ok=1, palette_result=0, set_palette_result=0,
                       nif_ok=1, sgi_ok=1, bmp_ok=1, stream_open=1)


def image_setup(object_count=100, image_counter=1, extra=None, file=b"", tga_state=None, device=None, factory=False,
                error=0, flag2=0, flag4=0, dd_count=0, guards=(0, 0)):
    """Sets the statics of both sides: the log is empty, the count of the objects and the image counter have the values
    given, the stream reads the bytes of file, the state of the TGA loader is the bytes given (zero if none), the test device
    gives the results of device (see DEVICE_DEFAULTS), the factory function of the images is set when factory is true, and
    the error code, the two flag bytes and the count of the NiDDImages have the values given."""
    config = dict(DEVICE_DEFAULTS)
    config.update(device or {})

    def setup(machine, lib):
        FILE["data"] = file
        DEVICE.clear()
        DEVICE.update(config)
        DEVICE.update(surface_serial=0, palette_serial=0)
        del IMAGE_LOG[:]
        GUEST_TRAPS.clear()
        lib.bc_trap_clear()
        machine.write(OBJECT_COUNT, struct.pack("<i", object_count))
        machine.write(IMAGE_COUNTER, struct.pack("<I", image_counter))
        lib.bc_image_reset.argtypes = [ctypes.c_int32, ctypes.c_uint32]
        lib.bc_image_reset(object_count, image_counter)
        lib.bc_image_set_file.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
        lib.bc_image_set_file(file, len(file))
        if tga_state is not None:
            machine.write(TGA_STATE, tga_state)
            lib.bc_tga_state_set.argtypes = [ctypes.c_char_p]
            lib.bc_tga_state_set(tga_state)
        lib.bc_image_configure.argtypes = [ctypes.c_int32] * 10
        lib.bc_image_configure(*[int(config[k]) for k in ("create_ok", "fill_ok", "refill_ok", "stream_data_ok",
                                                          "palette_result", "set_palette_result", "nif_ok", "sgi_ok",
                                                          "bmp_ok", "stream_open")])
        lib.bc_image_set_factory.argtypes = [ctypes.c_int32]
        lib.bc_image_set_factory(1 if factory else 0)
        machine.write(FACTORY, struct.pack("<I", 0x7C6490 if factory else 0))
        lib.bc_image_set_error_and_flags.argtypes = [ctypes.c_uint32] * 3
        lib.bc_image_set_error_and_flags(error, flag2, flag4)
        machine.write(IMAGE_ERROR, struct.pack("<I", error))
        machine.write(FLAG2, bytes([flag2]))
        machine.write(FLAG4, bytes([flag4]))
        del ATEXIT_LOG[:]
        lib.bc_atexit_clear()
        machine.write(GUARD_IMAGE_LIST, bytes([guards[0]]))
        machine.write(GUARD_RAW_RENDERER, bytes([guards[1]]))
        lib.bc_image_set_guards.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        lib.bc_image_set_guards(*guards)
        lib.bc_image_set_dd_count.argtypes = [ctypes.c_uint32]
        lib.bc_image_set_dd_count(dd_count)
        machine.write(DD_COUNT, struct.pack("<I", dd_count))
        if extra:
            extra(machine, lib)
    return setup


def image_scenario(roots, args, pre=(), glob=(), label="", extra=None, **statics):
    return Scenario(roots, args, globals=list(glob), pre=list(pre), label=label, setup=image_setup(extra=extra, **statics))


def condensed(lines):
    """The lines of a log, with a count for each run of equal lines, so that a long log of equal reads stays short."""
    out = []
    for line in lines:
        if out and out[-1][0] == line:
            out[-1][1] += 1
        else:
            out.append([line, 1])
    return "; ".join(line if count == 1 else "%s x%d" % (line, count) for line, count in out)


def log_probe():
    """The calls that the image code made outside the tested classes, as text."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return condensed(IMAGE_LOG)
        buffer = ctypes.create_string_buffer(1 << 23)
        side.lib.bc_image_log_dump.restype = ctypes.c_int
        length = side.lib.bc_image_log_dump(buffer, len(buffer))
        assert length >= 0, "the log does not fit"
        return condensed(buffer.value.decode("latin-1").splitlines())
    return Probe("calls outside the image classes", read)


def counters_probe():
    """The count of the objects at 0x9a1478, the counter at 0x9783d0, the count of the NiDDImages at 0x9a123c and the error
    code at 0x9a168c."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return (struct.unpack("<i", side.machine.read(OBJECT_COUNT, 4))[0],
                    struct.unpack("<I", side.machine.read(IMAGE_COUNTER, 4))[0],
                    struct.unpack("<I", side.machine.read(DD_COUNT, 4))[0],
                    struct.unpack("<I", side.machine.read(IMAGE_ERROR, 4))[0])
        side.lib.bc_image_object_count.restype = ctypes.c_int32
        side.lib.bc_image_counter.restype = ctypes.c_uint32
        side.lib.bc_image_dd_count.restype = ctypes.c_uint32
        side.lib.bc_image_error.restype = ctypes.c_uint32
        return (side.lib.bc_image_object_count(), side.lib.bc_image_counter(), side.lib.bc_image_dd_count(),
                side.lib.bc_image_error())
    return Probe("object count, image counter, count of NiDDImages and error code", read)


def _pointer(side, address, member):
    m = side.model.member("NiRawImageData", member)
    return int.from_bytes(side.read(address + side.offset("NiRawImageData", m), side.pointer_size), "little")


def _u32(side, address, member):
    m = side.model.member("NiRawImageData", member)
    return struct.unpack("<I", side.read(address + side.offset("NiRawImageData", m), 4))[0]


def exit_probe():
    """The functions that were registered with atexit and the guard bytes of the functions that run at the end."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return (list(ATEXIT_LOG), side.machine.read(GUARD_IMAGE_LIST, 1)[0], side.machine.read(GUARD_RAW_RENDERER, 1)[0])
        lib = side.lib
        lib.bc_atexit_address.restype = ctypes.c_uint32
        lib.bc_atexit_address.argtypes = [ctypes.c_int32]
        lib.bc_image_guard_image_list.restype = ctypes.c_uint32
        lib.bc_image_guard_raw_renderer.restype = ctypes.c_uint32
        return ([lib.bc_atexit_address(i) for i in range(lib.bc_atexit_count())], lib.bc_image_guard_image_list(),
                lib.bc_image_guard_raw_renderer())
    return Probe("functions registered with atexit and guard bytes", read)


def tga_state_probe():
    """The globals of the TGA loader (0x9a2e30 to 0x9b2e53): the colour map, the last pixel and the packet counters."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return side.machine.read(TGA_STATE, TGA_STATE_SIZE)
        buffer = ctypes.create_string_buffer(TGA_STATE_SIZE)
        side.lib.bc_tga_state_get(buffer)
        return buffer.raw
    return Probe("state of the TGA loader", read)


def raw_bytes_probe(root):
    """The bytes of the palette (0x400) and of the pixel block (stride * height) of the raw image root."""
    def read(side, addresses, ret):
        address = addresses[root]
        out = []
        palette = _pointer(side, address, "palette")
        out.append(side.read(palette, 0x400) if palette else None)
        pixels = _pointer(side, address, "pixels")
        size = _u32(side, address, "stride") * _u32(side, address, "height")
        out.append(side.read(pixels, size) if pixels and size < (1 << 20) else None)
        return out
    return Probe("palette and pixels of " + root, read)


def image_case(first, last, conv, args, ret, symbol, scenarios, code=RAW_CODE, model=IMAGE_MODEL, **kw):
    """An ObjectCase of the exe function against the native shim of the library."""
    kw.setdefault("stubs", pool_stubs_zeroed())
    kw.setdefault("probes", [log_probe(), trap_probe(), counters_probe()])
    kw.setdefault("label", "p53")
    original = orig(first, last, conv, args, ret, allow=code)
    return ObjectCase(model, original, Native(LIB, symbol), scenarios, **kw)


# Scenarios shared by the tests of the raw image and the planted defects ------------------------------------------------

CTOR = orig(0x7DE3E0, 0x7DE410, "thiscall", [PTR], PTR, allow=RAW_CODE)
ALLOCATE = orig(0x7DE4A0, 0x7DE631, "thiscall", [PTR, U32, U32, U32], U8, allow=RAW_CODE)

SETUP_CALLS = {
    "ctor": SetupCall(CTOR, Native(LIB, "bc_NiRawImageData_NiRawImageData")),
    "allocate": SetupCall(ALLOCATE, Native(LIB, "bc_NiRawImageData_Allocate")),
}

FORMATS = (0, 1, 2, 3, 7, 0xFFFFFFFF)
SIZES = ((0, 0), (1, 1), (4, 4), (3, 5), (16, 8), (64, 32), (0, 5), (5, 0), (2, 1000))


def allocate_scenarios():
    scenarios = []
    for fmt in FORMATS:
        for width, height in SIZES:
            roots = {"raw": Obj("NiRawImageData")}
            pre = [Pre("ctor", [Root("raw")])]
            scenarios.append(image_scenario(roots, [Root("raw"), fmt, width, height], pre, [],
                                            "format %d, size %d by %d" % (fmt, width, height)))
    return scenarios


def reallocation_scenarios():
    scenarios = []
    states = [(0, 8, 4), (1, 8, 4), (2, 8, 4), (5, 8, 4), (0, 0, 0)]
    wanted = [(0, 8, 4), (1, 8, 4), (2, 8, 4), (0, 16, 4), (1, 8, 8), (2, 4, 4), (5, 8, 4), (5, 3, 3), (0, 0, 0), (1, 0, 0)]
    for state in states:
        for fmt, width, height in wanted:
            roots = {"raw": Obj("NiRawImageData")}
            pre = [Pre("ctor", [Root("raw")]), Pre("allocate", [Root("raw")] + list(state))]
            scenarios.append(image_scenario(roots, [Root("raw"), fmt, width, height], pre, [],
                                            "state %s, then format %d, size %d by %d" % (state, fmt, width, height)))
    return scenarios




MIP_SIZES = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 31, 32, 33, 64, 100, 128, 255, 256, 512, 1000, 1024, 4096, 0x80000000,
             0x80000001, 0xFFFFFFFF)


def mip_scenarios():
    return [image_scenario({}, [width, height], [], [], "%d by %d" % (width, height))
            for width in MIP_SIZES for height in MIP_SIZES]


# The TGA loader --------------------------------------------------------------------------------------------------------

def tga_case(symbol, scenarios, **kw):
    kw.setdefault("stubs", stream_stubs())
    kw.setdefault("memory", STREAM_MEMORY)
    kw.setdefault("setup_calls", SETUP_CALLS)
    kw.setdefault("probes", [log_probe(), trap_probe(), counters_probe(), raw_bytes_probe("raw"), tga_state_probe()])
    return image_case(0x824C70, 0x825419, "cdecl", [PTR, PTR], U8, symbol, scenarios, code=TGA_CODE, **kw)


def tga_scenario(data, label, state=None):
    roots = {"stream": Obj("ImageStream", position=0), "raw": Obj("NiRawImageData")}
    return image_scenario(roots, [Root("stream"), Root("raw")], [Pre("ctor", [Root("raw")])], [], label, file=data,
                          tga_state=state)


def tga_files():
    """(label, file) pairs: every image type, depth, colour map, packet kind and descriptor that the loader reads, and the
    files that it refuses."""
    from icon_image_files import make_tga, pattern
    files = []
    for depth in (8, 15, 16, 24, 32):
        for width, height in ((1, 1), (3, 2), (4, 4), (5, 7), (1, 8), (8, 1)):
            files.append(("truecolor %d bits %dx%d" % (depth, width, height),
                          make_tga(2 if depth != 8 else 3, width, height, depth, seed=depth * 10 + width)))
    for descriptor in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x08, 0x10, 0x30):
        for depth in (24, 32):
            files.append(("descriptor 0x%02x, %d bits, 6x9" % (descriptor, depth),
                          make_tga(2, 6, 9, depth, seed=descriptor + depth, descriptor=descriptor)))
    files.append(("an id field", make_tga(2, 3, 3, 24, id_text=b"abc", seed=5)))
    files.append(("a long id field", make_tga(2, 3, 3, 32, id_text=b"x" * 255, seed=6)))
    for image_type, depth in ((10, 8), (10, 15), (10, 16), (10, 24), (10, 32), (11, 8), (11, 16)):
        for width, height in ((4, 4), (7, 3), (16, 2), (1, 1)):
            files.append(("run length type %d, %d bits %dx%d" % (image_type, depth, width, height),
                          make_tga(image_type, width, height, depth, seed=depth + width * 7, rle=True)))
    for entry in (8, 15, 16, 24, 32):
        for image_type, rle in ((1, False), (9, True)):
            for first, count in ((0, 16), (3, 9), (0, 256), (250, 6)):
                files.append(("colour map entries of %d bits, type %d, %d from %d" % (entry, image_type, count, first),
                              make_tga(image_type, 5, 4, 8, seed=entry + first, color_map=count, map_first=first, entry=entry,
                                       rle=rle)))
    files.append(("a colour map in a truecolor file", make_tga(2, 3, 3, 24, color_map=4, entry=24, seed=9)))
    files.append(("a colour map in a truecolor file with alpha", make_tga(2, 3, 3, 32, color_map=4, entry=16, seed=9)))
    files.append(("a gray file with a colour map", make_tga(3, 3, 3, 8, color_map=2, entry=32, seed=10)))
    indices = b"".join(struct.pack("<H", (i * 37) % 300) for i in range(9))
    files.append(("indices of 16 bits", make_tga(1, 3, 3, 16, color_map=300, entry=24, seed=12, pixels=indices)))
    # files that the loader refuses
    for image_type in (0, 4, 5, 6, 7, 8, 12, 13, 32, 33, 255):
        files.append(("image type %d" % image_type, make_tga(image_type, 3, 3, 24, seed=image_type)))
    for depth in (0, 1, 4, 12, 17, 23, 25, 33, 64):
        files.append(("depth %d" % depth, make_tga(2, 3, 3, depth, seed=depth)))
    files.append(("colour mapped without a map", make_tga(1, 3, 3, 8, seed=1, color_map_type=0)))
    files.append(("colour mapped with a bad entry size", make_tga(1, 3, 3, 8, color_map=4, entry=12, seed=2)))
    files.append(("a colour map that is too large", make_tga(1, 3, 3, 8, color_map=0x4000, entry=24, seed=3)))
    files.append(("a colour map that just fits", make_tga(1, 3, 3, 8, color_map=0x3FFE, entry=8, seed=3)))
    files.append(("a colour map that does not fit by one", make_tga(1, 3, 3, 8, color_map=0x3FFF, entry=8, seed=3)))
    files.append(("a colour map past the end", make_tga(1, 3, 3, 8, color_map=100, map_first=0x3FFF, entry=8, seed=3)))
    files.append(("width 0", make_tga(2, 0, 5, 24, seed=4)))
    files.append(("height 0", make_tga(2, 5, 0, 24, seed=4)))
    files.append(("width and height 0", make_tga(2, 0, 0, 32, seed=4)))
    files.append(("a large size", make_tga(2, 300, 40, 24, seed=14)))
    files.append(("a large size with alpha", make_tga(2, 257, 33, 32, seed=15)))
    return files


def tga_scenarios():
    return [tga_scenario(data, label) for label, data in tga_files()]


def tga_state_bytes(**fields):
    """A state of the TGA globals as the bytes of the exe memory 0x9a2e30 to 0x9b2e53. Fields: index_b, red, green, blue_b,
    red_b, alpha, green_b, index_a, blue, count, run."""
    data = bytearray(TGA_STATE_SIZE)
    layout = {"index_b": (0x00, 4), "red": (0x04, 1), "green": (0x06, 1), "blue_b": (0x09, 1), "red_b": (0x0A, 1),
              "alpha": (0x0B, 1), "green_b": (0x0D, 1), "index_a": (0x10, 4), "blue": (0x10018, 1), "count": (0x1001C, 4),
              "run": (0x10020, 4)}
    for name, value in fields.items():
        offset, size = layout[name]
        data[offset:offset + size] = value.to_bytes(size, "little")
    return bytes(data)


def tga_leftover_scenarios():
    """The counters of the run length packets are not reset at the start of a file (B0032): a file of the types 9, 10 and
    11 starts with the packet that an earlier file left."""
    from icon_image_files import make_tga
    scenarios = []
    for depth in (24, 32):
        for count, run in ((5, 0x80), (3, 0), (1, 0x80), (200, 0x80)):
            state = tga_state_bytes(count=count, run=run, red=11, green=22, blue=33, alpha=44, red_b=55, green_b=66, blue_b=77,
                                    index_a=2, index_b=3)
            data = make_tga(10, 4, 3, depth, seed=count, rle=True)
            scenarios.append(tga_scenario(data, "left over %d pixels of a %s packet, %d bits" % (
                count, "run" if run else "raw", depth), state))
    return scenarios


def tga_colour_map_scenarios():
    """Colour mapped files with alpha: the exe copies the first entry of the map for every pixel (B0031)."""
    from icon_image_files import make_tga
    scenarios = []
    for rle in (False, True):
        data = make_tga(9 if rle else 1, 4, 4, 8, color_map=16, entry=32, seed=21, rle=rle)
        scenarios.append(tga_scenario(data, "a map with alpha, run length %s" % rle))
    return scenarios


# The image chain -----------------------------------------------------------------------------------------------------

# The exe code of the chain: the renderer, the texture manager, the search and the creation of a surface, the creation of
# an image, the file loader and the code that these call, without the TGA code (TGA_CODE) and the raw image code (RAW_CODE).
CHAIN_CODE = TGA_CODE + [span(0x7C2A10, 0x7C2A61), span(0x7D6F30, 0x7D6F6C), span(0x7C6370, 0x7C6482),
                         span(0x7C5C60, 0x7C6079), span(0x7DAC80, 0x7DAD0B), span(0x7DAD10, 0x7DADF9), span(0x809FE0, 0x80A04B),
                         span(0x80A1D0, 0x80A263), span(0x80A8A0, 0x80A8BB), span(0x7E0C50, 0x7E0D5C), span(0x7E15A0, 0x7E15BF),
                         span(0x7E15C0, 0x7E1604), span(0x7C6490, 0x7C65ED), span(0x7E12F0, 0x7E1338),
                         span(0x819000, 0x81926A), span(0x40CFE0, 0x40CFFC), span(0x7C7690, 0x7C7747), span(0x7C75C0, 0x7C75C5), span(0x7E0B00, 0x7E0B55), span(0x7E1340, 0x7E13B8)]


def _word(data):
    return struct.unpack("<I", data)[0]


def _cstring(ctx, address):
    out = bytearray()
    while True:
        byte = ctx.read(address + len(out), 1)
        if byte == b"\0":
            return bytes(out)
        out += byte


def chain_stubs():
    """The device, the file layer and the C run time functions that the chain calls. The functions write the same lines as the
    test functions of the native side."""
    stubs = stream_stubs()

    def returns_bool(ctx, value):
        ctx.returns(1 if value else 0)

    def delete_image(ctx):
        image = ctx.arg(0)
        IMAGE_LOG.append("delete_image %d" % _word(ctx.read(image + 0x1C, 4)))
        ctx.heap.release(image)
        ctx.returns(0)

    def create_surface(ctx):
        device, fmt, out, count = ctx.arg(1), ctx.arg(2), ctx.arg(3), ctx.arg(4)
        serial = _word(ctx.read(device + 4, 4)) if device else 0xFFFF
        IMAGE_LOG.append("create_surface %d %d %d" % (serial, _word(ctx.read(fmt + 0x24, 4)), count))
        if not DEVICE["create_ok"]:
            return returns_bool(ctx, False)
        DEVICE["surface_serial"] += 1
        block = ctx.heap.new_block(8, "DdSurface")
        ctx.write(block, struct.pack("<II", VT_DDSURFACE, DEVICE["surface_serial"]))
        ctx.write(out, struct.pack("<I", block))
        returns_bool(ctx, True)

    def fill_surface(ctx):
        raw, fmt, surface = ctx.arg(1), ctx.arg(2), ctx.arg(3)
        IMAGE_LOG.append("fill_surface %d %d %d %d" % (_word(ctx.read(surface + 4, 4)), _word(ctx.read(fmt + 0x24, 4)),
                                                       _word(ctx.read(raw + 0x0C, 4)), _word(ctx.read(raw + 0x10, 4))))
        returns_bool(ctx, DEVICE["fill_ok"])

    def refill_surface(ctx):
        fmt, surface, flag = ctx.arg(1), ctx.arg(2), ctx.arg(3) & 0xFF
        IMAGE_LOG.append("refill_surface %d %d %d" % (_word(ctx.read(surface + 4, 4)), _word(ctx.read(fmt + 0x24, 4)),
                                                      1 if flag else 0))
        returns_bool(ctx, DEVICE["refill_ok"])

    def stream_data(ctx):
        IMAGE_LOG.append("stream_data %d" % ctx.arg(3))
        returns_bool(ctx, DEVICE["stream_data_ok"])

    def release(ctx):
        surface = ctx.arg(0)
        IMAGE_LOG.append("surface_release %d" % _word(ctx.read(surface + 4, 4)))
        ctx.heap.release(surface)
        ctx.returns(0)

    def set_palette(ctx):
        surface, palette = ctx.arg(0), ctx.arg(1)
        IMAGE_LOG.append("set_palette %d %d" % (_word(ctx.read(surface + 4, 4)), palette & 0xFFFF))
        ctx.returns(DEVICE["set_palette_result"])

    def create_palette(ctx):
        device, flags, out = ctx.arg(0), ctx.arg(1), ctx.arg(3)
        IMAGE_LOG.append("create_palette %d 0x%x" % (_word(ctx.read(device + 4, 4)), flags))
        if DEVICE["palette_result"] == 0:
            DEVICE["palette_serial"] += 1
            ctx.write(out, struct.pack("<I", 0xBEEF0000 + DEVICE["palette_serial"]))
        ctx.returns(DEVICE["palette_result"])

    def is_open(ctx):
        returns_bool(ctx, DEVICE["stream_open"])

    def open_stream(ctx):
        stream, name = ctx.arg(0), _cstring(ctx, ctx.arg(1))
        IMAGE_LOG.append("open " + name.decode("latin-1"))
        ctx.write(stream, struct.pack("<II", VT_STREAM, 0))
        ctx.returns(stream)

    def close_stream(ctx):
        IMAGE_LOG.append("close")
        ctx.returns(0)

    def delete_raw(ctx):
        raw = ctx.arg(0)
        IMAGE_LOG.append("delete raw_image")
        ctx.heap.release(raw)
        ctx.returns(0)

    def at_exit(ctx):
        ATEXIT_LOG.append(ctx.arg(0))
        ctx.returns(0)

    def nif_load(ctx):
        IMAGE_LOG.append("nif " + _cstring(ctx, ctx.arg(1)).decode("latin-1"))
        ctx.write(ctx.arg(0) + 0x4C, struct.pack("<II", 0, 0))
        ctx.returns(0)

    def nif_close(ctx):
        IMAGE_LOG.append("nif_close")
        ctx.returns(0)

    def sgi(ctx):
        IMAGE_LOG.append("sgi")
        returns_bool(ctx, DEVICE["sgi_ok"])

    def bmp(ctx):
        IMAGE_LOG.append("bmp")
        returns_bool(ctx, DEVICE["bmp_ok"])

    def strncmp(ctx):
        a, b, n = _cstring(ctx, ctx.arg(0))[:ctx.arg(2)], _cstring(ctx, ctx.arg(1))[:ctx.arg(2)], ctx.arg(2)
        ctx.returns(0 if a == b else (1 if a > b else 0xFFFFFFFF))

    def strrchr(ctx):
        text, char = ctx.arg(0), ctx.arg(1) & 0xFF
        data = _cstring(ctx, text)
        index = data.rfind(bytes([char]))
        ctx.returns(0 if index < 0 else text + index)

    def stricmp(ctx):
        a, b = _cstring(ctx, ctx.arg(0)).lower(), _cstring(ctx, ctx.arg(1)).lower()
        ctx.returns(0 if a == b else (1 if a > b else 0xFFFFFFFF))

    stubs.update({
        STUB["delete_image"]: Stub(delete_image, nargs=2, conv="thiscall"),
        STUB["delete_raw"]: Stub(delete_raw, nargs=2, conv="thiscall"),
        ATEXIT: Stub(at_exit, nargs=1),
        STUB["is_open"]: Stub(is_open, nargs=1, conv="thiscall"),
        STUB["release"]: Stub(release, nargs=1, conv="stdcall"),
        STUB["set_palette"]: Stub(set_palette, nargs=2, conv="stdcall"),
        STUB["create_palette"]: Stub(create_palette, nargs=5, conv="stdcall"),
        0x7C5200: Stub(create_surface, nargs=5, conv="thiscall"),
        0x7C5330: Stub(fill_surface, nargs=4, conv="thiscall"),
        0x7C6080: Stub(refill_surface, nargs=4, conv="thiscall"),
        0x7C59E0: Stub(stream_data, nargs=7, conv="thiscall"),
        0x86E550: Stub(open_stream, nargs=4, conv="thiscall"),
        0x86E620: Stub(close_stream, nargs=1, conv="thiscall"),
        0x816C60: Stub(lambda ctx: ctx.returns(ctx.arg(0)), nargs=1, conv="thiscall"),
        0x817A40: Stub(nif_load, nargs=2, conv="thiscall"),
        0x816DA0: Stub(nif_close, nargs=1, conv="thiscall"),
        0x823F60: Stub(sgi, nargs=2),
        0x824710: Stub(bmp, nargs=2),
        0x86E490: Stub(lambda ctx: ctx.returns(0), nargs=1),
        0x86E420: Stub(strncmp, nargs=3),
        0x859AF0: Stub(strrchr, nargs=2),
        0x86E400: Stub(stricmp, nargs=2),
    })
    return stubs


def chain_memory():
    memory = dict(STREAM_MEMORY)
    surface = bytearray(0x80)
    struct.pack_into("<I", surface, 0x08, STUB["release"])
    struct.pack_into("<I", surface, 0x7C, STUB["set_palette"])
    memory[VT_DDSURFACE] = bytes(surface)
    device = bytearray(0x20)
    struct.pack_into("<I", device, 0x14, STUB["create_palette"])
    memory[VT_DDDEVICE] = bytes(device)
    memory[0x898A7C + 0x28] = struct.pack("<I", STUB["delete_image"])
    memory[VT_RAW_IMAGE + 0x28] = struct.pack("<I", STUB["delete_raw"])
    for address in STUB.values():
        memory[address] = b"\xC3"
    return memory


def chain_case(first, last, conv, args, ret, symbol, scenarios, **kw):
    kw.setdefault("stubs", chain_stubs())
    kw.setdefault("memory", chain_memory())
    kw.setdefault("setup_calls", SETUP_CALLS)
    kw.setdefault("probes", [log_probe(), trap_probe(), counters_probe()])
    return image_case(first, last, conv, args, ret, symbol, scenarios, code=CHAIN_CODE, **kw)


# Objects of the chain ----------------------------------------------------------------------------------------------------

def texture_format(format_id, paletted=0):
    return Obj("TextureFormat", paletted=paletted, id=format_id)


def surface(serial):
    return Obj("DdSurface", serial=serial)


def device(serial=1):
    return Obj("DdDevice", serial=serial)


def surface_record(surf, format_id, dev, dirty=0):
    return Obj("ImageSurfaceRecord", surface=surf, formatId=format_id, device=dev, dirty=dirty)


def surface_list(records):
    """The list of the surface records of an image, with nodes that are separate blocks."""
    nodes = []
    for record in records:
        nodes.append(Obj("ImageSurfaceNode", data=record, next=None, prev=nodes[-1] if nodes else None))
    for before, after in zip(nodes, nodes[1:]):
        before.values["next"] = after
    return Obj("ImageSurfaceList", count=len(nodes), head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
               freeNodes=None, blocks=None, blockSize=2)


def raw_image(width=0, height=0, fmt=1, palette=False, count=1):
    """A raw image in the state after a successful loader; the pixels are not described (the pointer is null)."""
    return Obj("NiRawImageData", refCount=count, format=fmt, width=width, height=height, bytesPerPixel={0: 1, 1: 3, 2: 4}.get(fmt, 0),
               stride=width * {0: 1, 1: 3, 2: 4}.get(fmt, 0), palette=Obj("PixelBlock") if palette else None, pixels=None)


def dd_image(name="Data/a.tga", raw=None, flags=0, mask=7, records=(), image_id=7, stream_data=False, count=0, width=0,
             height=0):
    return Obj("NiDDImage", refCount=count, name=name, rawImage=raw, width=width, height=height, mipLevels=0, id=image_id,
               reserved20=0, flags=flags, mask=mask, scale=128.5, surfaces=surface_list(records), reserved48=0, reserved4c=0,
               reserved50=0, reserved54=0, streamData=Obj("PixelBlock") if stream_data else None)


def slot_array(ids):
    """Seven slots; an entry is None for an empty slot, a number for a format with that id, or a tuple (id, paletted)."""
    roots = {}
    for index in range(7):
        entry = ids[index] if index < len(ids) else None
        if entry is None:
            roots["slot%d" % index] = None
        elif isinstance(entry, tuple):
            roots["slot%d" % index] = texture_format(entry[0], entry[1])
        else:
            roots["slot%d" % index] = texture_format(entry)
    return Obj("SlotArray", **roots)


def scratch_raw_global(raw=None):
    """The raw image at 0x9a165c that the loaders fill."""
    return Global("scratch", SCRATCH_RAW, Obj("RawImagePointer", image=raw or raw_image(0, 0, 1)),
                  "bc_init_global_RawImagePointer")


def renderer_list_global(renderers):
    nodes = []
    for renderer in renderers:
        nodes.append(Obj("RendererNode", data=renderer, next=None, prev=nodes[-1] if nodes else None))
    for before, after in zip(nodes, nodes[1:]):
        before.values["next"] = after
    lst = Obj("RendererList", count=len(nodes), head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
              freeNodes=None, blocks=None, blockSize=2)
    return Global("rendererList", RENDERER_LIST, lst, "bc_init_global_RendererList")


def image_list_global(images=()):
    nodes = []
    for image in images:
        nodes.append(Obj("ImageAllNode", data=image, next=None, prev=nodes[-1] if nodes else None))
    for before, after in zip(nodes, nodes[1:]):
        before.values["next"] = after
    lst = Obj("ImageAllList", count=len(nodes), head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
              freeNodes=None, blocks=None, blockSize=2)
    return Global("imageList", IMAGE_LIST, lst, "bc_init_global_ImageAllList")


CREATE_CALL = orig(0x7C6490, 0x7C65ED, "cdecl", [], PTR, allow=CHAIN_CODE)
CHAIN_SETUP = dict(SETUP_CALLS)
CHAIN_SETUP["create"] = SetupCall(CREATE_CALL, Native(LIB, "bc_NiDDImage_Create"))


def real_tga_files(per_kind=4, largest=20000):
    """Small .tga files of the game and of the mods, from every kind of header that the data has (the image type, the pixel
    depth and the row order). The files are read from the repository."""
    import glob
    kinds = {}
    for pattern in ("game/data/**/*.tga", "mods/**/*.tga"):
        for path in sorted(glob.glob(os.path.join(REPO_ROOT, pattern), recursive=True)):
            size = os.path.getsize(path)
            if size > largest or size < 18:
                continue
            with open(path, "rb") as f:
                header = f.read(18)
            kind = (header[2], header[16], (header[17] >> 5) & 1)
            kinds.setdefault(kind, []).append((size, path))
    chosen = []
    for kind, files in sorted(kinds.items()):
        for size, path in sorted(files)[:per_kind]:
            chosen.append((kind, path))
    assert len(chosen) >= 20, "the game files are not in the repository"
    return chosen


def tga_real_scenarios():
    scenarios = []
    for kind, path in real_tga_files():
        with open(path, "rb") as f:
            data = f.read()
        scenarios.append(tga_scenario(data, "type %d, %d bits, top-to-bottom %d: %s" % (kind + (os.path.relpath(path, REPO_ROOT),))))
    return scenarios
