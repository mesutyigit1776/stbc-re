"""Tests of TGModelProperty (0x6ec640 and the functions around it).

Each object is built with the constructor of the exe on both sides, then brought into a state with setup calls (a
name, a reference count, an unshared or shared name buffer). The original side runs the real code; the allocator,
the event tables and the Python proxy lookup are stubs. The trap log shows the calls into code that the source tree
does not have yet. A probe reads the share count of the name buffers, because the graph shows only the characters.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_property_common import *  # noqa: F401,F403,E402
from prop_property_common import (CODE_SET_NAME_STRING, CREATOR_PROPERTY, CREATOR_SUBSYSTEM, ENVS, FACTORY_TABLES,  # noqa: E402
                                  PROPERTY_ALLOW, SET_NAME_STRING, TYPES, build_case, copy_probe, duplicate_probe, real_factory,
                                  name_state, new_property, one, poke, prop_original, registry_for, state_setup,
                                  with_constructors)
from prop_common import failing_text, narrow, string_state, with_registry  # noqa: E402

NAMES = [None, "", "a", "Phaser 1", "caf\xe9", "\x80\xff\x01\xa0", "n" * 31, "n" * 32, "long " * 20]
COUNTS = [-3, -1, 0, 1, 2, 3, 1000]
NAME_STATES = ["frozen", "unshared", "shared"]
SHARE_COUNTS = ["share:%d" % n for n in (1, 2, 0xFD, 0xFE)]


def named(kind, text):
    """Setup calls that give the property q a name buffer in a chosen state. The name of the property p is
    frozen. The subject is q for the states unshared and shared, else p."""
    if kind == "frozen":
        return new_property("p", name=text), "p"
    pre = new_property("p", name=text) + new_property("q") + [Pre("copyfrom", [Root("q"), Root("p")])]
    if kind == "unshared":
        return pre, "q"
    if kind.startswith("share:"):
        return pre + [Pre("setshare", [Root("q"), int(kind[6:])])], "q"
    if kind == "shared":
        return pre + [Pre("getname", [Root("q"), Root("h1")]), Pre("getname", [Root("q"), Root("h2")])], "q"
    raise ValueError(kind)


def roots(*extra, props=()):
    """The roots of a scenario: two properties, helper strings, the named extra strings and the extra properties."""
    result = {"p": Obj("TGModelProperty"), "q": Obj("TGModelProperty"), "h1": Obj("TGString"), "h2": Obj("TGString"),
              "out": Obj("TGString")}
    result.update({name: Obj("TGString") for name in extra})
    result.update({name: Obj("TGModelProperty") for name in props})
    return result


def thiscall(first, last, args, ret):
    return prop_original(first, last, "thiscall", args, ret)


PROBES = [name_state("p", "q"), string_state("out", "h1", "h2"), trap_probe()]


def single(first, last, symbol, args, ret, arg_lists, ret_class=None, probes=PROBES, **kw):
    """A case with one scenario for each list of arguments. The property is p, constructed with a name."""
    scenarios = [one(roots(), a, new_property(name="Phaser 1"), "args %s" % (a,)) for a in arg_lists]
    return build_case(ENVS[53], thiscall(first, last, args, ret), symbol, scenarios, ret_class=ret_class,
                      probes=probes, **kw)


# Construction ---------------------------------------------------------------------------------------------

@register("prop.property.TGModelProperty_TGModelProperty")
def ctor_test():
    scenarios = []
    for next_id, keep, advance in ((1, 1, 1), (50, 1, 1), (5, 1, 0)):
        for requested in (0, 1, 3, 49, 50, 100, 0x7FFFFFFF):
            for keys in (None, [], [2, 3]):
                glob = registry_global(None) if keys is None else registry_for(keys)
                scenarios.append(Scenario({"p": Obj("TGModelProperty")}, [Root("p"), requested], globals=[glob],
                                          setup=state_setup(next_id, keep, advance),
                                          label="counter %d advance %d id %d registry %s" % (
                                              next_id, advance, requested, "none" if keys is None else keys)))
    return build_case(ENVS[53], thiscall(0x6EC640, 0x6EC6B1, [PTR, I32], PTR), "bc_TGModelProperty_TGModelProperty",
                      scenarios, ret_class="TGModelProperty", setup_calls={}, allow_leaks=True,  # an equal ID drops an object
                      probes=[next_id_probe(), name_state("p"), trap_probe()])


# Type functions and constant strings -------------------------------------------------------------------------

@register("prop.property.TGModelProperty_GetObjType")
def get_obj_type_test():
    return single(0x6ED220, 0x6ED225, "bc_TGModelProperty_GetObjType", [PTR], I32, [[Root("p")]])


@register("prop.property.TGModelProperty_IsTypeOf")
def is_type_of_test():
    return single(0x6ED230, 0x6ED248, "bc_TGModelProperty_IsTypeOf", [PTR, I32], U8, [[Root("p"), t] for t in TYPES])


@register("prop.property.TGModelProperty_class_name_functions")
def class_names_test():
    return [single(first, last, symbol, [PTR], U32, [[Root("p")]]) for first, last, symbol in (
        (0x6ED250, 0x6ED255, "bc_TGModelProperty_GetClassName"),
        (0x6ED260, 0x6ED265, "bc_TGModelProperty_GetSwigPointerName"),
        (0x6ED270, 0x6ED275, "bc_TGModelProperty_GetShadowClassName"))]


EXE_TEXTS = {"TGModelProperty": (0x95B00C, 0x914088, 0x95B01C), "SubsystemProperty": (0x959590, 0x914050, 0x9595A4)}


def exe_text(machine, address):
    text = bytearray()
    while machine.read(address + len(text), 1) != b"\0":
        text += machine.read(address + len(text), 1)
    return bytes(text)


@register("prop.property.class_name_text_matches_the_exe_strings")
def class_name_text_test():
    from emuharness.case import EXE_PATH
    from emuharness.machine import Machine
    machine = Machine(EXE_PATH)
    lib = native_lib()
    for cls, addresses in EXE_TEXTS.items():
        lib.bc_new_TGModelProperty.restype = ctypes.c_void_p
        lib.bc_new_SubsystemProperty.restype = ctypes.c_void_p
        lib.bc_property_text.restype = ctypes.c_char_p
        lib.bc_property_text.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        lib.bc_object_construct.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
        lib.bc_object_construct.restype = ctypes.c_void_p
        obj = getattr(lib, "bc_new_" + cls)()
        lib.bc_object_construct(obj, 0 if cls == "TGModelProperty" else 1, 0)
        for which, address in enumerate(addresses):
            assert lib.bc_property_text(obj, which) == exe_text(machine, address), (cls, which)


@register("prop.property.TGModelProperty_CheckLinks")
def check_links_test():
    return single(0x67C560, 0x67C562, "bc_TGModelProperty_CheckLinks", [PTR], U8, [[Root("p")]])


@register("prop.property.TGModelProperty_FixLinks")
def fix_links_test():
    return single(0x67C570, 0x67C570, "bc_TGModelProperty_FixLinks", [PTR], None, [[Root("p")]])


@register("prop.property.TGModelProperty_WriteInit")
def write_init_test():
    return single(0x67DDD0, 0x67DDD2, "bc_TGModelProperty_WriteInit", [PTR, U32, U32], None,
                  [[Root("p"), a, b] for a in (0, 0x1234) for b in (0, 0xFFFFFFFF)])


@register("prop.property.TGModelProperty_Edit")
def edit_test():
    return single(0x6EC7D0, 0x6EC7D4, "bc_TGModelProperty_Edit", [PTR, U32], U8,
                  [[Root("p"), w] for w in (0, 1, 0xFFFFFFFF)])


@register("prop.property.TGModelProperty_Attach")
def attach_test():
    return single(0x6EC770, 0x6EC772, "bc_TGModelProperty_Attach", [PTR, I32, I32, I32], None,
                  [[Root("p"), a, b, c] for a in (0, 1) for b in (0, 5) for c in (0, -1)])


# Reference counting and destruction ----------------------------------------------------------------------------

@register("prop.property.TGModelProperty_IncRefCount")
def inc_ref_test():
    scenarios = [one(roots(), [Root("p")], new_property(name="x") + [poke("p", 8, count)], "count %d" % count)
                 for count in COUNTS + [0x7FFFFFFF]]
    return build_case(ENVS[53], thiscall(0x6EC780, 0x6EC783, [PTR], None), "bc_TGModelProperty_IncRefCount",
                      scenarios, probes=PROBES)


def destruction_scenarios(args_for, names=(None, "Phaser 1", "n" * 40), keys=((), (5,), (5, 6, 5 + 0x7F7))):
    scenarios = []
    for keep in (0, 1):
        for name in names:
            for key_set in keys:
                scenarios.append(one(roots(), args_for, new_property(name=name), "keep=%d name %r registry %s" % (
                    keep, name if name is None or len(name) < 10 else name[:8] + "...", list(key_set)),
                                     keys=key_set, setup=state_setup(keep=keep)))
    return scenarios


@register("prop.property.TGModelProperty_DecRefCount")
def dec_ref_test():
    scenarios = []
    for count in COUNTS:
        for name in (None, "Phaser 1"):
            scenarios.append(one(roots(), [Root("p")], new_property(name=name) + [poke("p", 8, count)],
                                 "count %d name %r" % (count, name), keys=[7]))
    return build_case(ENVS[53], thiscall(0x6EC790, 0x6EC7B0, [PTR], None), "bc_TGModelProperty_DecRefCount",
                      scenarios, probes=PROBES)


@register("prop.property.TGModelProperty_destructor")
def destructor_test():
    scenarios = destruction_scenarios([Root("p")])
    scenarios += [one(roots(), [Root("q")], named(kind, "Phaser 1")[0], "name state %s" % kind)
                  for kind in ("unshared", "shared")]
    return build_case(ENVS[53], thiscall(0x6EC6F0, 0x6EC763, [PTR], None), "bc_TGModelProperty_DestructorBody",
                      scenarios, probes=PROBES)


@register("prop.property.TGModelProperty_DeletingDestructor")
def deleting_destructor_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        for s in destruction_scenarios([Root("p"), flags], names=(None, "Phaser 1"), keys=((5,),)):
            s.label = "flags %d, %s" % (flags, s.label)
            scenarios.append(s)
    return build_case(ENVS[53], thiscall(0x6EC6C0, 0x6EC6E8, [PTR, U32], PTR), "bc_TGModelProperty_DeletingDestructor",
                      scenarios, ret_class="TGModelProperty", probes=PROBES)


# Names ------------------------------------------------------------------------------------------------------

@register("prop.property.TGModelProperty_GetName")
def get_name_test():
    scenarios = []
    for kind in NAME_STATES + SHARE_COUNTS:
        for text in ("", "a", "Phaser 1", "caf\xe9", "n" * 40):
            if kind.startswith("share:") and not text:
                continue  # an empty name has no buffer
            pre, subject = named(kind, text)
            scenarios.append(one(roots(), [Root(subject), Root("out")], pre, "%s name %r" % (kind, text[:8])))
    scenarios.append(one(roots(), [Root("p"), Root("out")], new_property(), "name after the constructor"))
    return build_case(ENVS[53], thiscall(0x5378D0, 0x5379F5, [PTR, PTR], PTR), "bc_TGModelProperty_GetName",
                      scenarios, ret_class="TGString", probes=PROBES)


@register("prop.property.TGModelProperty_SetName")
def set_name_test():
    scenarios = []
    for kind in NAME_STATES:
        for old in ("", "Phaser 1", "n" * 40):
            for text in NAMES[1:]:
                pre, subject = named(kind, old)
                scenarios.append(one(roots(), [Root(subject), narrow(text)], pre,
                                     "%s %r <- %r" % (kind, old[:8], text[:8])))
    cases = [build_case(ENVS[53], thiscall(0x5DDC10, 0x5DDC32, [PTR, Buf(io="in")], None),
                        "bc_TGModelProperty_SetName", scenarios, probes=PROBES)]
    null = [one(roots(), [Root("p"), 0], new_property(name="Phaser 1"), "null text")]
    cases.append(build_case(ENVS[53], thiscall(0x5DDC10, 0x5DDC32, [PTR, PTR], None), "bc_TGModelProperty_SetName",
                            null, probes=PROBES))
    return cases


def string_pre(kind, text):
    """Setup calls that build the string t in a state, from the name of a helper property w."""
    if kind == "frozen":
        return [Pre("strctor", [Root("t"), narrow(text)])]
    base = new_property("w", name=text)
    if kind == "unshared":
        return base + [Pre("getname", [Root("w"), Root("t")])]
    if kind == "shared":
        return base + [Pre("getname", [Root("w"), Root("h1")]), Pre("getname", [Root("w"), Root("t")])]
    raise ValueError(kind)


@register("prop.property.TGModelProperty_SetNameString")
def set_name_string_test():
    scenarios = []
    for old_kind in NAME_STATES + SHARE_COUNTS[2:]:
        for kind in NAME_STATES:
            for text in ("", "Phaser 1", "n" * 40, "caf\xe9"):
                old, subject = named(old_kind, "old name")
                pre = old + string_pre(kind, text)
                scenarios.append(one(roots("t", props=("w",)), [Root(subject), Root("t")], pre,
                                     "old %s, new %s %r" % (old_kind, kind, text[:8])))
    thunk = Original(CODE_SET_NAME_STRING, len(SET_NAME_STRING), "cdecl", [PTR, PTR], None,
                     allow=[tuple(a) for a in PROPERTY_ALLOW])
    return build_case(ENVS[53], thunk, "bc_TGModelProperty_SetNameString", scenarios, allow_leaks=True,  # a made-up share count leaves a block
                      probes=PROBES + [string_state("t")])


@register("prop.property.TGModelProperty_CopyFrom")
def copy_from_test():
    scenarios = []
    for kind in NAME_STATES:
        for target in (None, "", "target name", "n" * 40):
            for source in ("", "Phaser 1", "caf\xe9", "n" * 40):
                pre, subject = named(kind, source)
                other = new_property("r", name=target)
                scenarios.append(one(roots(props=("r",)), [Root("r"), Root(subject)], pre + other,
                                     "source %s %r, target %r" % (kind, source[:8], target)))
    return build_case(ENVS[53], thiscall(0x6EC810, 0x6EC834, [PTR, PTR], U8), "bc_TGModelProperty_CopyFrom",
                      scenarios, probes=PROBES + [name_state("r")])


# The factory and Duplicate ------------------------------------------------------------------------------------

@register("prop.property.TGModelProperty_CreateForFactory")
def create_for_factory_test():
    scenarios = [one(roots(), [ident], [], "id %d registry %s" % (ident, keys), keys=keys, setup=state_setup(500))
                 for ident in (0, 1, 100, 0x7FFFFFFF) for keys in ([], [3, 4])]
    original = prop_original(0x6E94B0, 0x6E94DD, "cdecl", [I32], PTR)
    return build_case(ENVS[53], original, "bc_TGModelProperty_CreateForFactory", scenarios, ret_class="TGModelProperty",
                      probes=[next_id_probe(), name_state("ret"), trap_probe()])


@register("prop.property.TGModelProperty_Duplicate")
def duplicate_test():
    scenarios = []
    for table in FACTORY_TABLES:
        for name in (None, "", "Phaser 1", "caf\xe9", "n" * 40):
            for refs in (0, 2):
                glob_set = [registry_for([4]), real_factory(FACTORY_TABLES[table])]
                scenarios.append(Scenario(roots(), [Root("p")], globals=glob_set, setup=state_setup(next_id=60),
                                          pre=with_constructors(roots(), new_property(name=name, refs=refs)),
                                          label="table %s name %r count %d" % (table, name, refs + 1)))
    scenarios += [Scenario(roots(), [Root("p")], globals=[registry_for([4]), real_factory(FACTORY_TABLES["both"])],
                           setup=state_setup(), pre=with_constructors(roots(), named(kind, "Phaser 1")[0]),
                           label="source name %s" % kind)
                  for kind in ("unshared", "shared")]
    return build_case(ENVS[53], thiscall(0x6EC7E0, 0x6EC80D, [PTR], PTR), "bc_TGModelProperty_Duplicate", scenarios,
                      ret_class="TGModelProperty",
                      probes=[name_state("p", "q", "ret"), string_state("h1", "h2"), trap_probe(), next_id_probe(),
                              copy_probe(), duplicate_probe("TGModelProperty")])


def registration_thunk():
    """The registrations of the exe in its order (0x43c5e6, then 0x6e89de and 0x6e8a8b), through Insert (0x4468f0)."""
    address = SCRATCH + 0x2900
    code = bytearray()
    for creator, key in ((CREATOR_SUBSYSTEM, 0x812B), (CREATOR_PROPERTY, 0x601), (0x6E9500, 0x602)):
        code += b"\x68" + struct.pack("<I", creator) + b"\x68" + struct.pack("<I", key) + b"\xB9" + struct.pack("<I", 0x99A578)
        code += b"\xE8" + struct.pack("<i", 0x4468F0 - (address + len(code) + 5))
    code += b"\xC3"
    return address, bytes(code)


@register("prop.property.RegisterClassCreators_enters_the_pairs_that_the_exe_registers")
def registration_test():
    address, code = registration_thunk()
    scenarios = [Scenario({}, [], globals=[real_factory(entries)], setup=state_setup(), label="table %s" % entries)
                 for entries in ([], [(0x602, 0x6E9500)], [(0x601 + 0x25, CREATOR_SUBSYSTEM)],
                                 [(0x601, CREATOR_SUBSYSTEM)])]
    thunk = Original(address, len(code), "cdecl", [], None, allow=[tuple(a) for a in PROPERTY_ALLOW])
    return build_case(ENVS[53], thunk, "bc_RegisterClassCreators", scenarios, memory={address: code}, stubs=property_stubs(),
                      allow_leaks=True)


# Slots that are not reconstructed ------------------------------------------------------------------------------

TRAP_SLOTS = {"TGModelProperty": {4: 0x6EC840, 5: 0x6EC880, 12: 0x6ED280},
              "SubsystemProperty": {4: 0x6933B0, 5: 0x693460, 12: 0x692550, 13: 0x6927E0, 15: 0x6929A0}}


def vtable_rows(name):
    rows = {}
    for line in open(os.path.join(REPO_ROOT, "re", "layouts", name + ".vtable.tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[6] == "trap":
            rows[int(cells[2])] = int(cells[3], 16)
    return rows


@register("prop.property.slots_that_are_not_reconstructed_reach_their_trap")
def trap_slots_test():
    lib = native_lib()
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_new_TGModelProperty.restype = ctypes.c_void_p
    lib.bc_new_SubsystemProperty.restype = ctypes.c_void_p
    lib.bc_object_construct.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
    for cls, slots in TRAP_SLOTS.items():
        assert vtable_rows(cls) == slots, (cls, vtable_rows(cls), slots)
        obj = getattr(lib, "bc_new_" + cls)()
        lib.bc_object_construct(obj, 0 if cls == "TGModelProperty" else 1, 0)
        call = getattr(lib, "bc_%s_CallSlot" % cls)
        call.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        for slot, address in sorted(slots.items()):
            lib.bc_trap_clear()
            call(obj, slot)
            assert lib.bc_trap_count() == 1 and lib.bc_trap_address(0) == address, (cls, slot, lib.bc_trap_count())


# Planted defects ----------------------------------------------------------------------------------------------

@register("prop.property.planted_defect_DecRefCount_does_not_destroy_at_zero")
def planted_dec_ref():
    scenarios = [one(roots(), [Root("p")], new_property(name="Phaser 1") + [poke("p", 8, 1)], "count 1", keys=[7])]
    text = failing_text(build_case(ENVS[53], thiscall(0x6EC790, 0x6EC7B0, [PTR], None),
                                   "bc_TGModelProperty_DecRefCount_defect", scenarios, probes=PROBES))
    assert "p: class original=TGObject native=TGModelProperty" in text, text
    assert "live objects of TGModelProperty: original=1 native=2" in text, text
    assert "heap blocks freed by the call: original=3 native=0" in text, text


@register("prop.property.planted_defect_Duplicate_shares_the_name_buffer")
def planted_duplicate():
    scenarios = [Scenario(roots(), [Root("p")], globals=[registry_for([4]), real_factory(FACTORY_TABLES["both"])],
                          setup=state_setup(next_id=60), pre=with_constructors(roots(), new_property(name="Phaser 1")),
                          label="named property")]
    text = failing_text(build_case(ENVS[53], thiscall(0x6EC7E0, 0x6EC80D, [PTR], PTR),
                                   "bc_TGModelProperty_Duplicate_defect", scenarios, ret_class="TGModelProperty",
                                   probes=[name_state("p", "ret"), copy_probe()]))
    assert "probe name state of p, ret: original=[(255, 0, 0), (0, 1, 0)] native=[(255, 0, 0), (255, 0, 0)]" in text, text
    assert "heap blocks freed by the call: original=1 native=2" in text, text
