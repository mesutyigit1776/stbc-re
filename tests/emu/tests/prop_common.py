"""Shared definitions for the tests of TGString, ObjectRegistry, ClassFactoryTable, TGList and TGObject.

The original side is the exe in the emulator. The native side is build/libbc_emutest_p53: these functions are
integer and pointer code, so they run once. Both sides build equal object graphs from one description (a field
map per class, see emuharness/layout.py). Memory that no field map names is not compared: padding, the
allocator byte of TGString, the allocation slack behind the end of a string, and the initial contents of an
allocation (both heaps give zeroed memory).
"""
import ctypes
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)

from emuharness import (CSTR, F32, I32, PTR, SCRATCH, U8, U16, U32, VTBL, Accessor, Array, Buf, Case, ClassDef, Global,  # noqa: E402
                        GuestSide, Inline, Member, Model, Native, NativeApi, Obj, ObjectCase, Original, Pre,
                        Probe, Ptr, Root, Scenario, SetupCall, Stub, expect_error, pool_stubs, register)
from emuharness.case import REPO_ROOT  # noqa: E402
from emuharness.native import load_library  # noqa: E402
import math_common as mc  # noqa: E402

LIB = mc.ensure_built(53)
LIB24 = mc.ensure_built(24)

VT_TGOBJECT, VT_REGISTRY, VT_FACTORY = 0x896278, 0x896264, 0x89623C
VT_PROPERTY, VT_SUBSYSTEM = 0x8961AC, 0x895384
VT_SET, VT_LIST, VT_GROUP, VT_INSTANCE = 0x893D58, 0x896204, 0x8961A8, 0x8961F0
VT_MANAGER = 0x8961A0
VT_NINODE = SCRATCH + 0xE000  # a vtable of hand-written stubs for the scene node
REGISTRY_POINTER = 0x99A67C
NEXT_ID, KEEP_REGISTRY, ADVANCE_NEXT_ID = 0x95B078, 0x95B07C, 0x95B07D
CREATOR_BASE, CREATOR_STRIDE = 0x3000D000, 0x10


def span(first, last):
    """(start, size) of the code from first to the last byte, inclusive."""
    return first, last - first + 1


def orig(first, last, conv, args, ret, allow=()):
    return Original(first, last - first + 1, conv, args, ret, allow=[tuple(a) for a in allow])


def narrow(text):
    """A C string argument: the text in latin-1 and a terminating zero."""
    return text.encode("latin-1") + b"\0"


def wide(text):
    """A 16 bit string argument: UTF-16 units and a terminating zero."""
    return text.encode("utf-16-le") + b"\0\0"


# Field maps ----------------------------------------------------------------------------------------------

def buckets_member(cls):
    return Member("buckets", 0x0C, Array(Ptr(cls + "Node"), "bucketCount"),
                  native=Accessor("bc_get_%s_buckets" % cls, "bc_set_%s_buckets" % cls))


CLASSES = [
    ClassDef("TGObject", 8, [Member("vtbl", 0, VTBL), Member("id", 4, I32)], vtable=VT_TGOBJECT, counted=False),
    ClassDef("ObjectRegistryNode", 12, [
        Member("key", 0, U32), Member("value", 4, Ptr("TGObject")), Member("next", 8, Ptr("ObjectRegistryNode")),
    ], counted=False),
    ClassDef("ObjectRegistry", 0x10, [
        Member("vtbl", 0, VTBL), Member("count", 4, U32), Member("bucketCount", 8, U32),
        buckets_member("ObjectRegistry")], vtable=VT_REGISTRY, counted=False),
    ClassDef("RegistryPointer", 4, [Member("registry", 0, Ptr("ObjectRegistry"))], counted=False),
    ClassDef("ClassFactoryNode", 12, [
        Member("key", 0, U32),
        Member("value", 4, U32, native=Accessor("bc_get_ClassFactoryNode_value", "bc_set_ClassFactoryNode_value")),
        Member("next", 8, Ptr("ClassFactoryNode")),
    ], counted=False),
    ClassDef("ClassFactoryTable", 0x10, [
        Member("vtbl", 0, VTBL), Member("count", 4, U32), Member("bucketCount", 8, U32),
        Member("buckets", 0x0C, Array(Ptr("ClassFactoryNode"), "bucketCount"),
               native=Accessor("bc_get_ClassFactoryTable_buckets", "bc_set_ClassFactoryTable_buckets"))],
        vtable=VT_FACTORY, counted=False),
    ClassDef("TGString", 0x10, [
        Member("buffer", 4, Array(U16, "length")), Member("length", 8, U32), Member("capacity", 0x0C, U32),
    ], counted=False),
    ClassDef("TGListNode", 12, [
        Member("data", 0, Ptr("TGObject")), Member("next", 4, Ptr("TGListNode")), Member("prev", 8, Ptr("TGListNode")),
    ], counted=False),
    ClassDef("TGListBlock", 8, [Member("nextBlock", 0, Ptr("TGListBlock")), Member("nodes", 4, Ptr("TGListNode"))],
             counted=False),
    ClassDef("TGList", 0x18, [
        Member("count", 0, I32), Member("head", 4, Ptr("TGListNode")), Member("tail", 8, Ptr("TGListNode")),
        Member("freeNodes", 0x0C, Ptr("TGListNode")), Member("blocks", 0x10, Ptr("TGListBlock")),
        Member("blockSize", 0x14, U32)], counted=False),
    ClassDef("NodeCell", 4, [Member("node", 0, Ptr("TGListNode"))], counted=False),
    ClassDef("PropPoint3", 12, [Member("x", 0, F32), Member("y", 4, F32), Member("z", 8, F32)], counted=False),
    ClassDef("PropPoint2", 8, [Member("x", 0, F32), Member("y", 4, F32)], counted=False),
    ClassDef("NiNode", 0x1C, [Member("vtbl", 0, VTBL), Member("refCount", 4, I32), Member("parent", 0x18, Ptr("NiNode"))],
             vtable=VT_NINODE, counted=False),
    ClassDef("TGModelProperty", 0x1C, [
        Member("vtbl", 0, VTBL), Member("id", 4, I32), Member("refCount", 8, I32),
        Member("name", 0x0C, Inline("TGString"))], vtable=VT_PROPERTY),
    ClassDef("SubsystemProperty", 0x48, [
        Member("displayNode", 0x1C, Ptr("NiNode")), Member("maxCondition", 0x20, F32), Member("critical", 0x24, U8),
        Member("targetable", 0x25, U8), Member("primary", 0x26, U8), Member("position", 0x28, Inline("PropPoint3")),
        Member("position2D", 0x34, Inline("PropPoint2")), Member("repairComplexity", 0x3C, F32),
        Member("disabledPercentage", 0x40, F32), Member("radius", 0x44, F32)],
        vtable=VT_SUBSYSTEM, base="TGModelProperty"),
    ClassDef("GroupNode", 12, [
        Member("data", 0, Ptr("TGModelPropertyGroup")), Member("next", 4, Ptr("GroupNode")),
        Member("prev", 8, Ptr("GroupNode"))], counted=False),
    ClassDef("GroupBlock", 8, [Member("nextBlock", 0, Ptr("GroupBlock")), Member("nodes", 4, Ptr("GroupNode"))],
             counted=False),
    ClassDef("GroupList", 0x18, [
        Member("count", 0, I32), Member("head", 4, Ptr("GroupNode")), Member("tail", 8, Ptr("GroupNode")),
        Member("freeNodes", 0x0C, Ptr("GroupNode")), Member("blocks", 0x10, Ptr("GroupBlock")),
        Member("blockSize", 0x14, U32)], counted=False),
    ClassDef("InstanceNode", 12, [
        Member("data", 0, Ptr("TGModelPropertyInstance")), Member("next", 4, Ptr("InstanceNode")),
        Member("prev", 8, Ptr("InstanceNode"))], counted=False),
    ClassDef("InstanceBlock", 8, [Member("nextBlock", 0, Ptr("InstanceBlock")),
                                  Member("nodes", 4, Ptr("InstanceNode"))], counted=False),
    ClassDef("InstanceList", 0x18, [
        Member("count", 0, I32), Member("head", 4, Ptr("InstanceNode")), Member("tail", 8, Ptr("InstanceNode")),
        Member("freeNodes", 0x0C, Ptr("InstanceNode")), Member("blocks", 0x10, Ptr("InstanceBlock")),
        Member("blockSize", 0x14, U32)], counted=False),
    ClassDef("TGModelPropertyList", 0x20, [
        Member("vtbl", 0, VTBL), Member("items", 4, Inline("InstanceList")), Member("unused", 0x1C, U32)],
        vtable=VT_LIST, counted=False),
    ClassDef("TGModelPropertyGroup", 0x24, [Member("hardpointName", 0x20, CSTR)], vtable=VT_GROUP,
             base="TGModelPropertyList", counted=False),
    ClassDef("TGModelPropertyInstance", 0x10, [
        Member("vtbl", 0, VTBL), Member("originalTemplate", 4, Ptr("TGModelProperty")),
        Member("hardpointName", 8, CSTR), Member("property", 0x0C, Ptr("TGModelProperty"))],
        vtable=VT_INSTANCE, counted=False),
    ClassDef("TGModelPropertySet", 0x0C, [Member("groups", 8, Ptr("GroupList"))], vtable=VT_SET, base="TGObject"),
    ClassDef("TemplateNode", 12, [
        Member("data", 0, Ptr("TGModelProperty")), Member("next", 4, Ptr("TemplateNode")),
        Member("prev", 8, Ptr("TemplateNode"))], counted=False),
    ClassDef("TemplateBlock", 8, [Member("nextBlock", 0, Ptr("TemplateBlock")), Member("nodes", 4, Ptr("TemplateNode"))],
             counted=False),
    ClassDef("TemplateList", 0x18, [
        Member("count", 0, I32), Member("head", 4, Ptr("TemplateNode")), Member("tail", 8, Ptr("TemplateNode")),
        Member("freeNodes", 0x0C, Ptr("TemplateNode")), Member("blocks", 0x10, Ptr("TemplateBlock")),
        Member("blockSize", 0x14, U32)], counted=False),
    ClassDef("TGModelPropertyManager", 0x64, [
        Member("vtbl", 0, VTBL), Member("registeredFilters", 0x04, Inline("TemplateList")),
        Member("currentFilters", 0x1C, Inline("TemplateList")), Member("globalTemplates", 0x34, Inline("TemplateList")),
        Member("localTemplates", 0x4C, Inline("TemplateList"))], vtable=VT_MANAGER, counted=False),
    ClassDef("FloatCell", 4, [Member("value", 0, F32)], counted=False),
    ClassDef("WrapperPointers", 12, [
        Member("object", 0, Ptr("SubsystemProperty")), Member("x", 4, Ptr("FloatCell")),
        Member("y", 8, Ptr("FloatCell"))], counted=False),
]

def native_api(lib):
    return NativeApi(lib, class_with_declared=True, heap_allocations="bc_heap_allocations",
                     heap_frees="bc_heap_frees", heap_mark="bc_heap_mark", heap_overruns="bc_heap_overruns")


MODEL = Model(CLASSES, native_api(LIB))
MODEL24 = Model(CLASSES, native_api(LIB24))


def native_lib():
    return load_library(LIB)


# Probes --------------------------------------------------------------------------------------------------

def _pointer_at(side, address, cls, member):
    m = side.model.member(cls, member)
    size = side.pointer_size
    return int.from_bytes(side.read(address + side.offset(cls, m), size), "little")


def _u32_at(side, address, cls, member):
    m = side.model.member(cls, member)
    return struct.unpack("<I", side.read(address + side.offset(cls, m), 4))[0]


def string_state(*names):
    """Per string: the share count byte, which strings share one buffer, and the character after the end."""
    def read(side, addresses, ret):
        groups, out = {}, []
        for name in names:
            if name not in addresses:
                continue
            address = addresses[name]
            buffer = _pointer_at(side, address, "TGString", "buffer")
            if not buffer:
                out.append(None)
                continue
            length = _u32_at(side, address, "TGString", "length")
            share = side.read(buffer - 1, 1)[0]
            terminator = struct.unpack("<H", side.read(buffer + 2 * length, 2))[0]
            out.append((share, groups.setdefault(buffer, len(groups)), terminator))
        return out
    return Probe("string state of " + ", ".join(names), read)


def narrow_text():
    """The zero terminated text that the returned pointer points to."""
    def read(side, addresses, ret):
        if not ret:
            return None
        data = bytearray()
        while len(data) < 4096:
            byte = side.read(ret + len(data), 1)
            if byte == b"\0":
                break
            data += byte
        return bytes(data)
    return Probe("returned narrow text", read)


def next_id_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return struct.unpack("<i", side.machine.read(NEXT_ID, 4))[0]
        return side.lib.bc_TGObject_nextId()
    return Probe("object ID counter", read)


# Trap log: the exe side stubs the function at 0x6d6320, the native side records the trap -----------------

GUEST_TRAPS = []


def trap_probe():
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return list(GUEST_TRAPS)
        lib = side.lib
        lib.bc_trap_address.restype = ctypes.c_uint32
        lib.bc_trap_registry_count.restype = ctypes.c_uint32
        return [(lib.bc_trap_address(i), lib.bc_trap_registry_count(i)) for i in range(lib.bc_trap_count())]
    return Probe("trap log (exe address, registry count)", read)


def remove_from_event_tables_stub():
    def record(ctx):
        pointer = ctx.machine.read_u32(REGISTRY_POINTER)
        count = ctx.machine.read_u32(pointer + 4) if pointer else 0
        GUEST_TRAPS.append((0x6D6320, count))
        ctx.returns(0)
    return Stub(record, nargs=1)


def alloc_stub():
    """operator_new (0x718cb0) for the emulated code: a tracked block of zeroed memory, like the native heap."""
    def allocate(ctx):
        size = ctx.arg(0)
        address = ctx.heap.new_block(size)
        ctx.write(address, bytes(size))
        ctx.returns(address)
    return Stub(allocate, nargs=1)


def pool_stubs_zeroed():
    stubs = pool_stubs()
    stubs[0x718CB0] = alloc_stub()
    return stubs


def sized_free_stubs():
    """The sized pool free that the deleting destructor of TGObject uses: 0x717b20 finds the pool, 0x718180 frees."""
    return {
        0x717B20: Stub(lambda ctx: ctx.returns(1), nargs=2, conv="thiscall"),
        0x718180: Stub(lambda ctx: (ctx.heap.release(ctx.arg(1)), ctx.returns(0)), nargs=2, conv="thiscall"),
    }


# Scenario state -------------------------------------------------------------------------------------------

def object_state(next_id=1, keep=1, advance=1):
    """Set the TGObject statics on both sides and clear the logs."""
    def setup(machine, lib):
        lib.bc_TGObject_ResetState()
        lib.bc_TGObject_SetState.argtypes = [ctypes.c_int32, ctypes.c_uint8, ctypes.c_uint8]
        lib.bc_TGObject_SetState(next_id, keep, advance)
        machine.write(NEXT_ID, struct.pack("<i", next_id))
        machine.write(KEEP_REGISTRY, bytes([keep]))
        machine.write(ADVANCE_NEXT_ID, bytes([advance]))
        GUEST_TRAPS.clear()
        lib.bc_trap_clear()
    return setup


def registry_global(registry):
    """The pointer 0x99a67c as a global object. registry is an Obj of class ObjectRegistry or None."""
    return Global("registry_pointer", REGISTRY_POINTER, Obj("RegistryPointer", registry=registry),
                  "bc_init_global_registry_pointer")


def make_registry(keys, bucket_count=0x7F7):
    """A registry that holds one TGObject per key, entered in this order. Returns (registry, {key: TGObject})."""
    objects = {k: Obj("TGObject", id=k) for k in keys}
    heads = [None] * bucket_count
    for key in keys:
        bucket = key % bucket_count
        heads[bucket] = Obj("ObjectRegistryNode", key=key, value=objects[key], next=heads[bucket])
    registry = Obj("ObjectRegistry", count=len(keys), bucketCount=bucket_count, buckets=heads)
    return registry, objects


def failing_text(case):
    """Run a case that must find a planted defect. Returns the report text."""
    results = case.run()
    assert not all(r.ok for r in results), "the planted defect was not found"
    return "\n".join(line for r in results for line in r.lines)


# Shared by the tests of the object foundation and of the property classes -----------------------------------

REGISTRY_SLOTS = [span(0x6F1960, 0x6F196D), span(0x6F1970, 0x6F1981), span(0x6F1990, 0x6F19A3),
                  span(0x6F19B0, 0x6F19B2), span(0x6F1AC0, 0x6F1B37)]
FACTORY_SLOTS = [span(0x6F1890, 0x6F189E), span(0x6F18A0, 0x6F18B1), span(0x6F18C0, 0x6F18D3),
                 span(0x6F18E0, 0x6F18E2), span(0x6F19C0, 0x6F1A37)]
OBJECT_CODE = [span(0x6F0EE0, 0x6F0F21), span(0x6F0F30, 0x6F0FBE), span(0x6F0FC0, 0x6F1076), span(0x6F0A70, 0x6F0B54),
               span(0x6F0BA0, 0x6F0BB9), span(0x6F0B70, 0x6F0B98), span(0x6F0B60, 0x6F0B65), span(0x518AB0, 0x518ABA),
               span(0x6F13E0, 0x6F1431), span(0x4468F0, 0x446975), span(0x6F08B0, 0x6F08EF), span(0x6F0900, 0x6F0988)]
ALLOW = REGISTRY_SLOTS + FACTORY_SLOTS + OBJECT_CODE


def with_registry(keys, buckets=0x7F7):
    registry, objects = make_registry(keys, buckets)
    return registry_global(registry), objects


def destructor_stubs():
    stubs = pool_stubs_zeroed()
    stubs[0x6D6320] = remove_from_event_tables_stub()
    stubs.update(sized_free_stubs())
    return stubs


def creator_stub(index):
    def make(ctx):
        identity = ctx.arg(0)
        address = ctx.heap.new_block(8, "TGObject")
        ctx.write(address, struct.pack("<II", VT_TGOBJECT, (identity + 0x100 * index) & 0xFFFFFFFF))
        ctx.returns(address)
    return Stub(make, nargs=1)


def creator_stubs():
    return {CREATOR_BASE + CREATOR_STRIDE * i: creator_stub(i) for i in range(4)}


def creator(index):
    return CREATOR_BASE + CREATOR_STRIDE * index


def factory_global(entries, buckets=0x25):
    """The global class factory table with the entries (type, creator index), entered in this order."""
    heads = [None] * buckets
    for key, index in entries:
        heads[key % buckets] = Obj("ClassFactoryNode", key=key, value=creator(index), next=heads[key % buckets])
    table = Obj("ClassFactoryTable", count=len(entries), bucketCount=buckets, buckets=heads)
    return Global("factory", 0x99A578, table, "bc_init_global_ClassFactoryTable")




STRING_CODE = [
    span(0x6F3E90, 0x6F3EE1), span(0x6F3EF0, 0x6F3F2E), span(0x6F47C0, 0x6F4966), span(0x6F4CC0, 0x6F4CD0),
    span(0x6F4EE0, 0x6F5019), span(0x6F4450, 0x6F44AD), span(0x6F43E0, 0x6F444B), span(0x6F56F0, 0x6F5737),
    span(0x4166D0, 0x416718), span(0x416720, 0x416732), span(0x416740, 0x416861), span(0x416870, 0x41697F),
    span(0x416980, 0x416A55), span(0x6F5820, 0x6F58C1), span(0x40E360, 0x40E3AC), span(0x40E330, 0x40E355),
    span(0x6F41A0, 0x6F41D1), span(0x416DD0, 0x416DD5), span(0x859D47, 0x859D63), span(0x85BE82, 0x85BEB6),
    span(0x86F8E0, 0x86F9BE),
]


