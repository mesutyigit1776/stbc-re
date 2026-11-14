"""Shared definitions for the tests of TGModelProperty and SubsystemProperty.

Each property object of a scenario is built with the constructor of the exe (a setup call on both sides), because
the native object needs its vtable and its registry entry. Other state is set by small setup calls: a thunk in the
scratch region of the emulator writes a value at an exe offset, and the native library writes the same member.
The functions that the tested code calls outside the property classes are stubs: the pool allocator, the event
tables (0x6d6320), the Python proxy lookup (0x6f15c0) and the scene node functions. A test runs against the library
of each precision when it computes with floats.
"""
import ctypes
import os
import struct
import sys
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from emuharness import CW_24BIT, DEFAULT_CW  # noqa: E402
from prop_common import (ALLOW, GUEST_TRAPS, LIB, LIB24, MODEL, MODEL24, STRING_CODE,  # noqa: E402
                         VT_NINODE, _pointer_at, _u32_at, alloc_stub, object_state, orig, pool_stubs_zeroed,
                         registry_global, remove_from_event_tables_stub, sized_free_stubs, span, trap_probe,
                         with_registry)

NONE_OBJECT = 0x95DD20
PROXY_LOOKUP = 0x6F15C0
SCRATCH_CODE = 0x30002800

PROPERTY_CODE = [
    span(0x6EC640, 0x6EC6B1), span(0x6EC6C0, 0x6EC6E8), span(0x6EC6F0, 0x6EC763), span(0x6EC770, 0x6EC772),
    span(0x6EC780, 0x6EC783), span(0x6EC790, 0x6EC7B0), span(0x6EC7D0, 0x6EC7D4), span(0x6EC7E0, 0x6EC80D),
    span(0x6EC810, 0x6EC834), span(0x5DDC10, 0x5DDC32), span(0x5DDC40, 0x5DDC8B), span(0x5378D0, 0x5379F5),
    span(0x6ED220, 0x6ED225), span(0x6ED230, 0x6ED248), span(0x6ED250, 0x6ED255), span(0x6ED260, 0x6ED265),
    span(0x6ED270, 0x6ED275), span(0x67C560, 0x67C562), span(0x67C570, 0x67C570), span(0x67DDD0, 0x67DDD2),
    span(0x6E94B0, 0x6E94DD), span(0x43FF30, 0x43FF5D), span(0x692470, 0x6924DF), span(0x692650, 0x692678),
    span(0x692680, 0x69270E), span(0x692710, 0x692798), span(0x6923F0, 0x692410), span(0x6924E0, 0x6924E5),
    span(0x6924F0, 0x692514), span(0x692520, 0x692525), span(0x692530, 0x692535), span(0x692540, 0x692545),
    span(0x6927A0, 0x6927B7), span(0x6927C0, 0x6927D0), span(0x63AE20, 0x63AE41), span(0x63AE50, 0x63AF27),
]
PROPERTY_ALLOW = ALLOW + STRING_CODE + PROPERTY_CODE

# Code that the tests place in the scratch region of the emulator.
CODE_POKE = SCRATCH_CODE
# poke(object, exe offset, value, size): stores 4 bytes, or 1 byte when size is 1:
#   mov eax,[esp+4]; mov ecx,[esp+8]; mov edx,[esp+0xc]; cmp dword [esp+0x10],1; je byte;
#   mov [eax+ecx],edx; ret; byte: mov [eax+ecx],dl; ret
def _poke_code():
    return bytes([0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x0C, 0x83, 0x7C, 0x24, 0x10, 0x01,
                  0x74, 0x04, 0x89, 0x14, 0x08, 0xC3, 0x88, 0x14, 0x08, 0xC3])


POKE = _poke_code()
CODE_SET_NAME_STRING = SCRATCH_CODE + 0x40


def _set_name_string_code():
    # mov ecx,[esp+4]; mov eax,[esp+8]; push [eax+0xc]; push [eax+8]; push [eax+4]; push [eax]; call 0x5ddc40; ret
    head = bytes([0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x44, 0x24, 0x08, 0xFF, 0x70, 0x0C, 0xFF, 0x70, 0x08, 0xFF, 0x70, 0x04,
                  0xFF, 0x30, 0xE8])
    call_end = CODE_SET_NAME_STRING + len(head) + 4
    return head + struct.pack("<i", 0x5DDC40 - call_end) + b"\xC3"


SET_NAME_STRING = _set_name_string_code()

# poke_name_share(object, value): stores the byte value before the first character of the name buffer.
CODE_POKE_SHARE = SCRATCH_CODE + 0x80
POKE_SHARE = bytes([0x8B, 0x44, 0x24, 0x04, 0x8B, 0x40, 0x10, 0x8B, 0x4C, 0x24, 0x08, 0x88, 0x48, 0xFF, 0xC3])

# The scene node vtable: slot 10 (offset 0x28) deletes, slot 40 (offset 0xa0) detaches a child.
NODE_DELETE, NODE_DETACH = VT_NINODE + 0x100, VT_NINODE + 0x110
NODE_VTABLE = bytearray(0xA4 + 4)
struct.pack_into("<I", NODE_VTABLE, 0x28, NODE_DELETE)
struct.pack_into("<I", NODE_VTABLE, 0xA0, NODE_DETACH)
MEMORY = {CODE_POKE: POKE, CODE_POKE_SHARE: POKE_SHARE, CODE_SET_NAME_STRING: SET_NAME_STRING, VT_NINODE: bytes(NODE_VTABLE),
          NODE_DELETE: b"\xC3", NODE_DETACH: b"\xC3"}

NODE_LOG = []
NODE_MODE = {"detach_returns_nothing": False}
PROXY = {"returns": NONE_OBJECT}


def proxy_stub():
    """The Python proxy lookup: records the call and returns None, like an object without a proxy."""
    def lookup(ctx):
        pointer = ctx.machine.read_u32(0x99A67C)
        count = ctx.machine.read_u32(pointer + 4) if pointer else 0
        GUEST_TRAPS.append((PROXY_LOOKUP, count))
        ctx.returns(NONE_OBJECT)
    return Stub(lookup, nargs=1, conv="thiscall")


def sized_alloc_stubs():
    """The inline pool allocation for a known size: 0x717b70 finds the pool, 0x718010 takes a block."""
    def find(ctx):
        ctx.returns(ctx.arg(1))

    def take(ctx):
        size = ctx.arg(0)
        address = ctx.heap.new_block(size)
        ctx.write(address, bytes(size))
        ctx.returns(address)
    return {0x717B70: Stub(find, nargs=4, conv="thiscall"), 0x718010: Stub(take, nargs=1, conv="thiscall")}


def node_stubs():
    def delete(ctx):
        NODE_LOG.append(1)
        ctx.returns(0)

    def detach(ctx):
        NODE_LOG.append(2)
        out, child = ctx.arg(1), ctx.arg(2)
        ctx.write(out, struct.pack("<I", 0 if NODE_MODE["detach_returns_nothing"] else child))
        ctx.returns(0)
    return {NODE_DELETE: Stub(delete, nargs=2, conv="thiscall"), NODE_DETACH: Stub(detach, nargs=3, conv="thiscall")}


def property_stubs():
    stubs = pool_stubs_zeroed()
    stubs[0x6D6320] = remove_from_event_tables_stub()
    stubs[PROXY_LOOKUP] = proxy_stub()
    stubs.update(sized_free_stubs())
    stubs.update(sized_alloc_stubs())
    stubs.update(node_stubs())
    return stubs


def state_setup(next_id=1, keep=1, advance=1, positions=((0, 0, 0), (0, 0)), detach_returns_nothing=False,
                extra=None):
    """Set the statics of both sides: the ID counter and flags, the default positions that the constructor of
    SubsystemProperty copies (bits at 0x9a2878 and 0x9a2bb8), and the scene node log."""
    base = object_state(next_id, keep, advance)

    def setup(machine, lib):
        base(machine, lib)
        three, two = positions
        machine.write(0x9A2878, struct.pack("<3I", *three))
        machine.write(0x9A2BB8, struct.pack("<2I", *two))
        lib.bc_SubsystemProperty_SetDefaultPositions.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        lib.bc_SubsystemProperty_SetDefaultPositions((ctypes.c_uint32 * 3)(*three), (ctypes.c_uint32 * 2)(*two))
        NODE_LOG.clear()
        lib.bc_node_log_clear()
        NODE_MODE["detach_returns_nothing"] = detach_returns_nothing
        lib.bc_node_detach_returns_nothing.argtypes = [ctypes.c_uint8]
        lib.bc_node_detach_returns_nothing(1 if detach_returns_nothing else 0)
        lib.bc_capture_reset()
        if extra:
            extra(machine, lib)
    return setup


def node_log_probe():
    """The calls that the destructor made on the scene node: 2 detach, 1 delete."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return list(NODE_LOG)
        lib = side.lib
        lib.bc_node_log_entry.restype = ctypes.c_uint8
        return [lib.bc_node_log_entry(i) for i in range(lib.bc_node_log_size())]
    return Probe("scene node calls (2 detach, 1 delete)", read)


def name_state(*roots):
    """Per property: the share count byte of the name buffer, which names share one buffer, and the terminator."""
    def read(side, addresses, ret):
        groups, out = {}, []
        for name in roots:
            if name not in addresses or not addresses[name]:
                continue
            member = side.model.member("TGModelProperty", "name")
            base = addresses[name] + side.offset("TGModelProperty", member)
            buffer = _pointer_at(side, base, "TGString", "buffer")
            if not buffer:
                out.append(None)
                continue
            length = _u32_at(side, base, "TGString", "length")
            share = side.read(buffer - 1, 1)[0]
            terminator = struct.unpack("<H", side.read(buffer + 2 * length, 2))[0]
            out.append((share, groups.setdefault(buffer, len(groups)), terminator))
        return out
    return Probe("name state of " + ", ".join(roots), read)


Env = namedtuple("Env", "precision lib model cw setup_calls")


def make_env(precision):
    lib = LIB if precision == 53 else LIB24
    model = MODEL if precision == 53 else MODEL24
    cw = DEFAULT_CW if precision == 53 else CW_24BIT

    def call(first, last, conv, args, ret):
        return orig(first, last, conv, args, ret, allow=PROPERTY_ALLOW)

    def thunk(address, size, args, ret):
        return Original(address, size, "cdecl", args, ret, allow=[tuple(a) for a in PROPERTY_ALLOW])

    calls = {
        "ctor": SetupCall(call(0x6EC640, 0x6EC6B1, "thiscall", [PTR, I32], PTR),
                          Native(lib, "bc_TGModelProperty_TGModelProperty")),
        "sctor": SetupCall(call(0x692470, 0x6924DF, "thiscall", [PTR, I32], PTR),
                           Native(lib, "bc_SubsystemProperty_SubsystemProperty")),
        "setname": SetupCall(call(0x5DDC10, 0x5DDC32, "thiscall", [PTR, Buf(io="in")], None),
                             Native(lib, "bc_TGModelProperty_SetName")),
        "incref": SetupCall(call(0x6EC780, 0x6EC783, "thiscall", [PTR], None),
                            Native(lib, "bc_TGModelProperty_IncRefCount")),
        "poke": SetupCall(thunk(CODE_POKE, len(POKE), [PTR, U32, U32, U32], None), Native(lib, "bc_poke_pre")),
        "setshare": SetupCall(thunk(CODE_POKE_SHARE, len(POKE_SHARE), [PTR, U32], None),
                              Native(lib, "bc_poke_name_share")),
        "setnode": SetupCall(thunk(CODE_POKE, len(POKE), [PTR, U32, PTR, U32], None),
                             Native(lib, "bc_poke_pointer")),
        "copyfrom": SetupCall(call(0x6EC810, 0x6EC834, "thiscall", [PTR, PTR], U8),
                              Native(lib, "bc_TGModelProperty_CopyFrom")),
        "getname": SetupCall(call(0x5378D0, 0x5379F5, "thiscall", [PTR, PTR], PTR),
                             Native(lib, "bc_TGModelProperty_GetName")),
        "strctor": SetupCall(call(0x6F3E90, 0x6F3EE1, "thiscall", [PTR, Buf(io="in")], PTR),
                             Native(lib, "bc_TGString_TGString")),
    }
    return Env(precision, lib, model, cw, calls)


ENVS = {53: make_env(53), 24: make_env(24)}


def build_case(env, original, symbol, scenarios, **kw):
    """An ObjectCase against the library of the environment. symbol is the native function of the p53 library; the
    p24 library exports the same name."""
    kw.setdefault("stubs", property_stubs())
    kw.setdefault("setup_calls", env.setup_calls)
    kw.setdefault("memory", MEMORY)
    kw.setdefault("label", "p%d" % env.precision)
    return ObjectCase(env.model, original, Native(env.lib, symbol), scenarios, cw=env.cw, **kw)


def prop_original(first, last, conv, args, ret):
    return orig(first, last, conv, args, ret, allow=PROPERTY_ALLOW)


def registered_both(name, builder):
    """Register a test for each precision. builder(env) returns the cases."""
    for precision in (53, 24):
        register("%s@p%d" % (name, precision))(lambda precision=precision: builder(ENVS[precision]))


def registered_once(name, builder):
    register(name)(lambda: builder(ENVS[53]))


# Scenario helpers ---------------------------------------------------------------------------------------------

def registry_for(keys=()):
    glob, _ = with_registry(list(keys))
    return glob


def new_property(root="p", ident=0, name=None, refs=0, cls="ctor"):
    """The setup calls that bring a blank object into the state of a constructed property."""
    pre = [Pre(cls, [Root(root), ident])]
    if name is not None:
        pre.append(Pre("setname", [Root(root), narrow(name)]))
    pre += [Pre("incref", [Root(root)]) for _ in range(refs)]
    return pre


def poke(root, offset, value, size=4):
    return Pre("poke", [Root(root), offset, value & 0xFFFFFFFF, size])


def with_constructors(roots_, pre):
    """Adds the constructor call for each property root that the setup calls do not construct, so that every
    object has its vtable on the native side before the graph is read."""
    built = {c.args[0].name for c in pre if c.call in ("ctor", "sctor")}
    missing = [Pre("ctor" if o.cls == "TGModelProperty" else "sctor", [Root(n), 0]) for n, o in roots_.items()
               if o.cls in ("TGModelProperty", "SubsystemProperty") and n not in built]
    return missing + list(pre)


def one(roots_, args, pre, label, keys=(), setup=None):
    return Scenario(roots_, args, globals=[registry_for(keys)], pre=with_constructors(roots_, pre),
                    setup=setup or state_setup(), label=label)




TYPES = [0, 1, 2, 3, 0x601, 0x602, 0x812B, 0x8001, 0x10601, -1, 0x7FFFFFFF, -2147483648]


def real_factory(entries, buckets=0x25):
    """The global class factory table with the creators of the exe, entered in this order. entries: (type, address)."""
    heads = [None] * buckets
    for key, address in entries:
        heads[key % buckets] = Obj("ClassFactoryNode", key=key, value=address, next=heads[key % buckets])
    table = Obj("ClassFactoryTable", count=len(entries), bucketCount=buckets, buckets=heads)
    return Global("factory", 0x99A578, table, "bc_init_global_ClassFactoryTable")


CREATOR_PROPERTY, CREATOR_SUBSYSTEM = 0x6E94B0, 0x43FF30
FACTORY_TABLES = {"both": [(0x601, CREATOR_PROPERTY), (0x812B, CREATOR_SUBSYSTEM)], "property": [(0x601, CREATOR_PROPERTY)],
                  "empty": [], "other": [(0x812B, CREATOR_SUBSYSTEM)],
                  "chain": [(0x601 + 0x25, CREATOR_SUBSYSTEM), (0x601, CREATOR_PROPERTY)]}


def copy_probe():
    def read(side, addresses, ret):
        ret = ret or 0
        assert ret == 0 or ret != addresses["p"], "Duplicate returned the object itself"
        return ret != 0
    return Probe("the copy is another object", read)




def duplicate_probe(cls, fields=(), node=False):
    """The copy is another object with count 1, a new ID, equal fields and no scene node."""
    def read(side, addresses, ret):
        ret = ret or 0
        if not ret:
            return None
        source, copy = addresses["p"], ret
        assert copy != source, "the copy is the source object"

        def raw(address, name, size):
            return side.read(address + side.offset(cls, side.model.member(cls, name)), size)

        assert raw(copy, "refCount", 4) == struct.pack("<i", 1), "the count of the copy is not 1"
        assert raw(copy, "id", 4) != raw(source, "id", 4), "the copy has the ID of the source"
        for field, size in fields:
            assert raw(copy, field, size) == raw(source, field, size), field
        if node:
            assert raw(copy, "displayNode", side.pointer_size) == bytes(side.pointer_size), "the copy has a scene node"
        return True
    return Probe("the copy is separate, with equal fields and count 1", read)
