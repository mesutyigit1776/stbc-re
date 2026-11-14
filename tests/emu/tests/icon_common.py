"""Shared definitions for the tests of the icon manager, the icon group and their containers.

Each scenario builds objects on both sides: a group comes from the constructor of the exe (a setup call on both sides),
because the native object needs its vtable; the global icon manager is built from a description of all members. The
exe calls into the renderer, NetImmerse and the script layer are stubs; the native side has test functions that do the
same: both write one text line per call into a log, and the log is compared after the tested call. Textures, texture
properties and cameras have a serial number in the part of the object that the icon code does not use, so that the log
names them.
"""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from emuharness import CW_24BIT, DEFAULT_CW, F64, Inner  # noqa: E402
from prop_common import (GUEST_TRAPS, LIB, LIB24, REGISTRY_POINTER, alloc_stub, native_api, narrow, object_state, orig,  # noqa: E402
                         pool_stubs_zeroed, span, trap_probe)

TEXTURES_SIZE = 0x18
MANAGER = 0x99D1F0
RENDER_OBJECT = 0x99D040
WORKING_DIRECTORY = 0x997BC0
ITERATOR = 0x99D48C
ATEXIT = 0x8594E4
WRAPPER_HANDLE = 0xBEEF0001

VT_ICON_MANAGER, VT_ICON_GROUP, VT_ICON_ARRAY, VT_ICON_TABLE = 0x897BD4, 0x897BD8, 0x897BEC, 0x897BF0
VT_ICON_REGISTRATION, VT_ICON_LOCATION, VT_TEXTURE_CACHE = 0x897C04, 0x897C0C, 0x88B6A0
VT_PROPERTY_BASE = SCRATCH + 0xF000
VT_TEXTURE_BASE = SCRATCH + 0xF040
VT_CAMERA_BASE = SCRATCH + 0xF080
VT_RENDERER_BASE = SCRATCH + 0xF100
STUB_BASE = SCRATCH + 0xF400
STUBS = {name: STUB_BASE + 0x10 * i for i, name in enumerate(
    ("delete_property", "delete_texture", "delete_camera", "width", "height", "slot_98", "slot_9c", "slot_ac",
     "slot_b0"))}

# The exe code that the icon tests run: group and manager code with the array and table code between, the registration,
# the texture cache, the smart pointer helpers and the vector functions of the C run time.
ICON_CODE = [span(0x7299C0, 0x72D35F), span(0x747990, 0x747A9F), span(0x44EBD0, 0x44F16F), span(0x45D9A0, 0x45D9D3),
             span(0x40CFE0, 0x40CFFC), span(0x401FB0, 0x402040), span(0x42E6C0, 0x42E6DA), span(0x859865, 0x859904),
             span(0x859D64, 0x859DF3), span(0x4D7290, 0x4D72B1)]

ICON = {"log": [], "serial": 0, "textures": {}}


# Field maps -----------------------------------------------------------------------------------------------------------

def list_classes(prefix, data_class):
    """A pointer list with its node and block classes (the shape of TGList)."""
    return [
        ClassDef(prefix + "Node", 12, [Member("data", 0, Ptr(data_class) if data_class else U32),
                                       Member("next", 4, Ptr(prefix + "Node")),
                                       Member("prev", 8, Ptr(prefix + "Node"))], counted=False),
        ClassDef(prefix + "Block", 8, [Member("nextBlock", 0, Ptr(prefix + "Block")),
                                       Member("nodes", 4, Ptr(prefix + "Node"))], counted=False),
        ClassDef(prefix + "List", 0x18, [
            Member("count", 0, I32), Member("head", 4, Ptr(prefix + "Node")), Member("tail", 8, Ptr(prefix + "Node")),
            Member("freeNodes", 0x0C, Ptr(prefix + "Node")), Member("blocks", 0x10, Ptr(prefix + "Block")),
            Member("blockSize", 0x14, U32)], counted=False),
    ]


ICON_CLASSES = list_classes("IconGroup", "IconGroup") + list_classes("IconOther", None) + [
    ClassDef("NiTextureProperty", 0x1C, [
        Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("serial", 8, U32),
        Member("texture", 0x18, Ptr("NiImage"))], vtable=VT_PROPERTY_BASE, counted=False),
    ClassDef("NiImage", 0x18, [
        Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("serial", 8, U32), Member("width", 0x10, U32),
        Member("height", 0x14, U32)], vtable=VT_TEXTURE_BASE, counted=False),
    ClassDef("NiD3DRender", 0x10, [Member("vtbl", 0, VTBL), Member("width", 4, U32), Member("height", 8, U32)],
             vtable=VT_RENDERER_BASE, counted=False),
    ClassDef("NiCamera", 0x18C, [
        Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("serial", 8, U32),
        Member("renderer", 0x188, Ptr("NiD3DRender"))], vtable=VT_CAMERA_BASE, counted=False),
    ClassDef("ManagerArgs", 8, [Member("m", 0, Ptr("IconManager")), Member("c", 4, Ptr("NiCamera"))], counted=False),
    ClassDef("GroupArgs", 4, [Member("g", 0, Ptr("IconGroup"))], counted=False),
    ClassDef("NodeCell", 4, [Member("node", 0, Ptr("IconGroupNode"))], counted=False),
    ClassDef("PropertyCellObject", 4, [Member("value", 0, Ptr("NiTextureProperty"))], counted=False),
    ClassDef("CameraCellObject", 4, [Member("camera", 0, Ptr("NiCamera"))], counted=False),
    ClassDef("IconLocation", 0x30, [
        Member("vtbl", 0, VTBL), Member("texture", 4, Ptr("NiTextureProperty"))]
        + [Member("c%d" % i, 8 + 4 * i, F32) for i in range(8)]
        + [Member("pixelWidth", 0x28, F32), Member("pixelHeight", 0x2C, F32)], vtable=VT_ICON_LOCATION, counted=False),
    ClassDef("IconLocationNode", 12, [Member("key", 0, U32), Member("value", 4, Ptr("IconLocation")),
                                      Member("next", 8, Ptr("IconLocationNode"))], counted=False),
    ClassDef("IconLocationTable", 0x10, [
        Member("vtbl", 0, VTBL), Member("count", 4, U32), Member("bucketCount", 8, U32),
        Member("buckets", 0x0C, Array(Ptr("IconLocationNode"), "bucketCount"))], vtable=VT_ICON_TABLE, counted=False),
    ClassDef("IconTextureArray", 0x18, [
        Member("vtbl", 0, VTBL), Member("data", 4, Array(Ptr("NiTextureProperty"), "capacity")),
        Member("capacity", 8, U32), Member("used", 0x0C, U32), Member("count", 0x10, I32),
        Member("growBy", 0x14, U32)], vtable=VT_ICON_ARRAY, counted=False),
    ClassDef("IconGroup", 0x34, [
        Member("vtbl", 0, VTBL), Member("name", 4, CSTR), Member("lockCount", 8, I32),
        Member("locations", 0x0C, Inline("IconLocationTable")),
        Member("textures", 0x1C, Inline("IconTextureArray"))], vtable=VT_ICON_GROUP, counted=False),
    ClassDef("IconRegistration", 0x10, [
        Member("vtbl", 0, VTBL), Member("groupName", 4, CSTR), Member("moduleName", 8, CSTR),
        Member("functionName", 0x0C, CSTR)], vtable=VT_ICON_REGISTRATION, counted=False),
    ClassDef("TextureCacheNode", 12, [Member("key", 0, CSTR), Member("value", 4, Ptr("NiImage")),
                                      Member("next", 8, Ptr("TextureCacheNode"))], counted=False),
    ClassDef("TextureCache", 0x10, [
        Member("vtbl", 0, VTBL), Member("count", 4, U32), Member("bucketCount", 8, U32),
        Member("buckets", 0x0C, Array(Ptr("TextureCacheNode"), "bucketCount"))], vtable=VT_TEXTURE_CACHE,
        counted=False),
    ClassDef("IconManager", 0x7C, [
        Member("vtbl", 0, VTBL), Member("data", 4, Array(Ptr("IconRegistration"), "capacity")),
        Member("capacity", 8, U32), Member("used", 0x0C, U32), Member("count", 0x10, I32), Member("growBy", 0x14, U32),
        Member("groups", 0x18, Inline("IconGroupList")), Member("otherList", 0x30, Inline("IconOtherList")),
        Member("unused48", 0x48, U32), Member("unused4c", 0x4C, U32), Member("camera", 0x50, Ptr("NiCamera")),
        Member("scale", 0x54, F32), Member("screenWidth", 0x58, F32), Member("screenHeight", 0x5C, F32),
        Member("pixelCenterAdj", 0x60, F32), Member("pixelCenterAdjX", 0x64, F32),
        Member("pixelCenterAdjY", 0x68, F32), Member("textureCache", 0x6C, Inline("TextureCache"))],
        vtable=VT_ICON_MANAGER, counted=False),
]

ICON_MODEL = Model(ICON_CLASSES, native_api(LIB))
ICON_MODEL24 = Model(ICON_CLASSES, native_api(LIB24))


# Exe memory of the test objects: vtables with the slots that the icon code calls ------------------------------------------

def _vtable(size, slots):
    data = bytearray(size)
    for offset, name in slots.items():
        struct.pack_into("<I", data, offset, STUBS[name])
    return bytes(data)


CODE_SET_LOCK = SCRATCH + 0xF200
# set_lock(group, value): mov eax,[esp+4]; mov ecx,[esp+8]; mov [eax+8],ecx; ret
SET_LOCK = bytes([0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x89, 0x48, 0x08, 0xC3])

ICON_MEMORY = {
    CODE_SET_LOCK: SET_LOCK,
    VT_PROPERTY_BASE: _vtable(0x30, {0x28: "delete_property"}),
    VT_TEXTURE_BASE: _vtable(0x30, {0x28: "delete_texture"}),
    VT_CAMERA_BASE: _vtable(0x30, {0x28: "delete_camera"}),
    VT_RENDERER_BASE: _vtable(0xB4, {0x4C: "width", 0x50: "height", 0x98: "slot_98", 0x9C: "slot_9c", 0xAC: "slot_ac",
                                     0xB0: "slot_b0"}),
}
for _name, _address in STUBS.items():
    ICON_MEMORY[_address] = b"\xC3"


# The exe stubs ---------------------------------------------------------------------------------------------------------------

def _read_text(ctx, address):
    data = bytearray()
    while True:
        byte = ctx.read(address + len(data), 1)
        if byte == b"\0":
            return bytes(data).decode("latin-1")
        data += byte


def _log(text):
    ICON["log"].append(text)


def _new_serial():
    ICON["serial"] += 1
    return ICON["serial"]


def icon_stubs():
    def new_property(ctx):
        block = ctx.arg(0)
        serial = _new_serial()
        ctx.write(block, struct.pack("<IiI", VT_PROPERTY_BASE, 0, serial))
        ctx.write(block + 0x18, struct.pack("<I", 0))
        _log("new_property %d" % serial)
        ctx.returns(block)

    def create_texture(ctx):
        path = _read_text(ctx, ctx.stack_arg(0))
        flag = ctx.stack_arg(1) & 0xFF
        size = ICON["textures"].get(path)
        if size is None:
            _log("create_texture %s %d 0" % (path, 1 if flag else 0))
            ctx.returns(0)
            return
        block = ctx.heap.new_block(TEXTURES_SIZE)
        serial = _new_serial()
        ctx.write(block, struct.pack("<IiI", VT_TEXTURE_BASE, 0, serial))
        ctx.write(block + 0x10, struct.pack("<II", size[0], size[1]))
        _log("create_texture %s %d %d" % (path, 1 if flag else 0, serial))
        ctx.returns(block)

    def flush(ctx):
        _log("flush")
        ctx.returns(0)

    def module_call(ctx):
        _log("module_call %s %s" % (_read_text(ctx, ctx.stack_arg(0)), _read_text(ctx, ctx.stack_arg(1))))
        ctx.returns(0)

    def python_wrapper(ctx):
        _log("pywrapper")
        ctx.returns(WRAPPER_HANDLE)

    def delete(kind):
        def run(ctx):
            _log("delete %s %d" % (kind, struct.unpack("<I", ctx.read(ctx.arg(0) + 8, 4))[0]))
            ctx.heap.release(ctx.arg(0))
            ctx.returns(0)
        return Stub(run, nargs=2, conv="thiscall")

    def read_field(offset):
        def run(ctx):
            ctx.returns(struct.unpack("<I", ctx.read(ctx.arg(0) + offset, 4))[0])
        return Stub(run, nargs=1, conv="thiscall")

    def renderer_slot(name):
        def run(ctx):
            if name == "slot_98":
                pointer = ctx.machine.read_u32(REGISTRY_POINTER)
                count = ctx.machine.read_u32(pointer + 4) if pointer else 0
                GUEST_TRAPS.append((0x72A690, count))
            ctx.returns(0)
        return Stub(run, nargs=2 if name in ("slot_98", "slot_ac") else 1, conv="thiscall")

    stubs = pool_stubs_zeroed()
    stubs.update({
        0x809FE0: Stub(new_property, nargs=1, conv="thiscall"),
        0x7E0C50: Stub(create_texture, nargs=4),
        0x728270: Stub(flush, nargs=1, conv="thiscall"),
        0x6F8AB0: Stub(module_call, nargs=6),
        0x6F9920: Stub(python_wrapper, nargs=2),
        0x728420: Stub(lambda ctx: ctx.returns(0), nargs=2, conv="thiscall"),
        STUBS["delete_property"]: delete("property"),
        STUBS["delete_texture"]: delete("texture"),
        STUBS["delete_camera"]: delete("camera"),
        STUBS["width"]: read_field(4),
        STUBS["height"]: read_field(8),
    })
    for name in ("slot_98", "slot_9c", "slot_ac", "slot_b0"):
        stubs[STUBS[name]] = renderer_slot(name)
    return stubs


ATEXIT_LOG = []


def atexit_stubs():
    def record(ctx):
        ATEXIT_LOG.append(ctx.arg(0))
        ctx.returns(0)
    stubs = icon_stubs()
    stubs[ATEXIT] = Stub(record, nargs=1)
    return stubs


# Scenario state --------------------------------------------------------------------------------------------------------------

def icon_setup(textures=None, workdir="", serial=100, state=None, extra=None):
    """Sets the statics of both sides: the log is empty, the serial counter and the texture files are those given, and
    the working directory text is at 0x997bc0."""
    base = object_state()
    files = dict(textures or {})

    def setup(machine, lib):
        base(machine, lib)
        del ICON["log"][:]
        del ATEXIT_LOG[:]
        lib.bc_atexit_clear()
        ICON["serial"] = serial
        ICON["textures"] = files
        text = workdir.encode("latin-1") + b"\0"
        machine.write(WORKING_DIRECTORY, text.ljust(0x104, b"\0"))
        machine.write(ITERATOR, struct.pack("<I", 0))
        lib.bc_icon_reset.argtypes = [ctypes.c_uint32, ctypes.c_char_p]
        lib.bc_icon_reset(serial, workdir.encode("latin-1"))
        lib.bc_icon_add_texture.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
        for path, size in files.items():
            lib.bc_icon_add_texture(path.encode("latin-1"), size[0], size[1])
        if extra:
            extra(machine, lib)
    return setup


def log_probe():
    """The calls that the icon code made outside the icon classes, as text."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return "\n".join(ICON["log"])
        buffer = ctypes.create_string_buffer(1 << 16)
        side.lib.bc_icon_log_dump.restype = ctypes.c_int
        length = side.lib.bc_icon_log_dump(buffer, len(buffer))
        assert length >= 0, "the log does not fit"
        return buffer.value.decode("latin-1").rstrip("\n")
    return Probe("calls outside the icon classes", read)


def iterator_probe():
    """The number of the node that GoToFirstPos and GetNext move (-1 for no node)."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            node = struct.unpack("<I", side.machine.read(ITERATOR, 4))[0]
            return struct.unpack("<i", side.read(node, 4))[0] if node else -1
        side.lib.bc_TGIconGroup_iteratorKey.restype = ctypes.c_int32
        return side.lib.bc_TGIconGroup_iteratorKey()
    return Probe("node of the iterator", read)


def atexit_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return list(ATEXIT_LOG)
        lib = side.lib
        lib.bc_atexit_address.restype = ctypes.c_uint32
        lib.bc_atexit_address.argtypes = [ctypes.c_int32]
        return [lib.bc_atexit_address(i) for i in range(lib.bc_atexit_count())]
    return Probe("functions registered with atexit", read)


# Environments ----------------------------------------------------------------------------------------------------------------

from collections import namedtuple  # noqa: E402

IconEnv = namedtuple("IconEnv", "precision lib model cw setup_calls")


def make_icon_env(precision):
    lib = LIB if precision == 53 else LIB24
    model = ICON_MODEL if precision == 53 else ICON_MODEL24
    cw = DEFAULT_CW if precision == 53 else CW_24BIT

    def call(first, last, conv, args, ret):
        return orig(first, last, conv, args, ret, allow=ICON_CODE)

    def setup(name, first, last, conv, args, ret, symbol):
        return SetupCall(call(first, last, conv, args, ret), Native(lib, symbol))

    calls = {
        "gctor": setup("gctor", 0x72BB20, 0x72BCDF, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGIconGroup_TGIconGroup"),
        "gctor0": setup("gctor0", 0x72BB20, 0x72BCDF, "thiscall", [PTR, PTR], PTR, "bc_TGIconGroup_TGIconGroup"),
        "setname": setup("setname", 0x72BF70, 0x72BFDF, "thiscall", [PTR, Buf(io="in")], None, "bc_TGIconGroup_SetName"),
        "setname0": setup("setname0", 0x72BF70, 0x72BFDF, "thiscall", [PTR, PTR], None, "bc_TGIconGroup_SetName"),
        "load": setup("load", 0x72BFE0, 0x72C24F, "thiscall", [PTR, Buf(io="in"), U32], I32,
                      "bc_TGIconGroup_LoadIconTexture"),
        "setloc": setup("setloc", 0x72C260, 0x72C47F, "thiscall",
                        [PTR, I32, I32, F32, F32, F32, F32, I32, I32], None, "bc_TGIconGroup_SetIconLocation"),
        "mctor": setup("mctor", 0x729B30, 0x729C3F, "thiscall", [PTR], PTR, "bc_TGIconManager_TGIconManager"),
        "register": setup("register", 0x729E70, 0x729F9F, "thiscall", [PTR, Buf(io="in"), Buf(io="in"), Buf(io="in")],
                          None, "bc_TGIconManager_RegisterIconGroup"),
        "addgroup": setup("addgroup", 0x72A000, 0x72A07F, "thiscall", [PTR, PTR], None, "bc_TGIconManager_AddIconGroup"),
        "reset": setup("reset", 0x72BDE0, 0x72BF5F, "thiscall", [PTR], None, "bc_TGIconGroup_Reset"),
        "gdestroy": setup("gdestroy", 0x72ACB0, 0x72ACCF, "thiscall", [PTR, U32], PTR,
                          "bc_TGIconGroup_DeletingDestructor"),
        "isnum": setup("isnum", 0x72CD80, 0x72CDCF, "thiscall", [PTR, I32], U8, "bc_TGIconGroup_IsNumValid"),
        "gofirst": setup("gofirst", 0x72CDD0, 0x72CDFF, "thiscall", [PTR], None, "bc_TGIconGroup_GoToFirstPos"),
        "getnext": setup("getnext", 0x72CE00, 0x72CE5F, "thiscall", [PTR], I32, "bc_TGIconGroup_GetNext"),
        "setcamera": setup("setcamera", 0x729DC0, 0x729E6F, "thiscall", [PTR, PTR, F32], None,
                           "bc_TGIconManager_SetDisplayInfo"),
        "tcinsert": setup("tcinsert", 0x44EBD0, 0x44ECDF, "thiscall", [PTR, Buf(io="in"), PTR], None,
                          "bc_TextureCache_Insert"),
        "setlock": SetupCall(Original(CODE_SET_LOCK, len(SET_LOCK), "cdecl", [PTR, I32], None,
                                      allow=[tuple(a) for a in ICON_CODE]), Native(lib, "bc_TGIconGroup_setLock")),
        "purge": setup("purge", 0x72A080, 0x72A14F, "thiscall", [PTR], None, "bc_TGIconManager_Purge"),
        "delete": setup("delete", 0x72A210, 0x72A3FF, "thiscall", [PTR, Buf(io="in")], U8, "bc_TGIconManager_DeleteIconGroup"),
        "arrstore": setup("arrstore", 0x72D1F0, 0x72D2FF, "thiscall", [PTR, U32, PTR], None, "bc_IconTextureArray_StoreAt"),
        "arradd": setup("arradd", 0x72CED0, 0x72CF3F, "thiscall", [PTR, PTR], None, "bc_IconTextureArray_Add"),
    }
    return IconEnv(precision, lib, model, cw, calls)


ICON_ENVS = {53: make_icon_env(53), 24: make_icon_env(24)}


def icon_case(env, first, last, conv, args, ret, symbol, scenarios, **kw):
    """An ObjectCase of the exe function against the native shim of the library of the environment."""
    kw.setdefault("stubs", icon_stubs())
    kw.setdefault("setup_calls", env.setup_calls)
    kw.setdefault("memory", ICON_MEMORY)
    kw.setdefault("label", "p%d" % env.precision)
    kw.setdefault("probes", [log_probe(), trap_probe()])
    original = orig(first, last, conv, args, ret, allow=ICON_CODE)
    return ObjectCase(env.model, original, Native(env.lib, symbol), scenarios, cw=env.cw, **kw)


def registered_pair(name):
    """A decorator that registers the test for each precision. The function takes the environment and gives the case."""
    def decorate(builder):
        for precision in (53, 24):
            register("%s@p%d" % (name, precision))(lambda precision=precision: builder(ICON_ENVS[precision]))
        return builder
    return decorate


def registered_once(name):
    """A decorator that registers the test for the precision 53 only (integer and pointer code)."""
    def decorate(builder):
        register(name)(lambda: builder(ICON_ENVS[53]))
        return builder
    return decorate


# Scenario building blocks ----------------------------------------------------------------------------------------------------

def empty_list(prefix, block_size=2):
    return Obj(prefix + "List", count=0, head=None, tail=None, freeNodes=None, blocks=None, blockSize=block_size)


def empty_cache(buckets=0x25):
    return Obj("TextureCache", count=0, bucketCount=buckets, buckets=[None] * buckets)


def manager_obj(scale=0.0, width=0.0, height=0.0, adjustment=-0.5, adjustment_x=0.0, adjustment_y=0.0, camera=None,
                registrations=(), capacity=None, groups=None, cache=None, grow_by=1):
    """The state of the manager after its constructor, with the members given."""
    records = list(registrations)
    capacity = capacity if capacity is not None else max(1, len(records))
    data = records + [None] * (capacity - len(records))
    return Obj("IconManager", data=data, capacity=capacity, used=len(records), count=len(records), growBy=grow_by,
               groups=groups or empty_list("IconGroup"), otherList=empty_list("IconOther"), unused48=0, unused4c=0,
               camera=camera, scale=scale, screenWidth=width, screenHeight=height, pixelCenterAdj=adjustment,
               pixelCenterAdjX=adjustment_x, pixelCenterAdjY=adjustment_y, textureCache=cache or empty_cache())


def manager_global(**members):
    return Global("m", MANAGER, manager_obj(**members), "bc_init_global_IconManager")


def registration(group, module, function):
    return Obj("IconRegistration", groupName=group, moduleName=module, functionName=function)


def renderer(width, height):
    return Obj("NiD3DRender", width=width, height=height)


def camera(serial, width=640, height=480, count=0, drawing=True):
    return Obj("NiCamera", refCount=count, serial=serial, renderer=renderer(width, height) if drawing else None)


def texture(serial, width, height, count=0):
    return Obj("NiImage", refCount=count, serial=serial, width=width, height=height)


def holder(serial, tex=None, count=0):
    return Obj("NiTextureProperty", refCount=count, serial=serial, texture=tex)


def build_group(root, name, textures=(), locations=(), lock=None):
    """The setup calls that bring a blank group root into a state: the constructor, the textures and the locations.
    textures: file names; locations: (number, handle, x, y, width, height, rotation, mirror)."""
    pre = [Pre("gctor", [Root(root), narrow(name)]) if name is not None else Pre("gctor0", [Root(root), 0])]
    for path in textures:
        pre.append(Pre("load", [Root(root), narrow(path), 0]))
    for location in locations:
        pre.append(Pre("setloc", [Root(root)] + list(location)))
    if lock is not None:
        pre.append(Pre("setlock", [Root(root), lock]))
    return pre


def scenario(roots, args, pre=(), glob=(), label="", textures=None, workdir="", serial=100, extra=None):
    return Scenario(roots, args, globals=list(glob), pre=list(pre), label=label,
                    setup=icon_setup(textures, workdir, serial, extra=extra))


def cache_hash(text):
    """The bucket of a key text in the texture cache: h * 33 plus the signed character, modulo 0x25."""
    value = 0
    for byte in text.encode("latin-1"):
        value = (value * 33 + (byte if byte < 128 else byte - 256)) & 0xFFFFFFFF
    return value % 0x25
