"""Tests of TGModelPropertySet (0x6eb040 and the functions around it) and of its list, group and instance classes.

Sets are built with the code of the exe on both sides: the constructor and AddToSet run as setup calls on blank
objects. The tested call then runs on equal graphs (set, groups, instances, properties, names). The allocator, the
event tables and the Python proxy lookup are stubs. The class factory table holds the creators of the exe, because
AddToSet copies properties through Duplicate. The lists and groups of the destructor tests are also built by hand,
so that states that AddToSet never makes are covered (empty groups, several node blocks).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_property_common import *  # noqa: F401,F403,E402
from prop_property_common import (CODE_POKE, ENVS, FACTORY_TABLES, PROPERTY_ALLOW, SCRATCH_CODE, TYPES, build_case,  # noqa: E402
                                  name_state, new_property, property_stubs, real_factory, registered_both,
                                  registered_once, registry_for, state_setup, with_constructors)
from prop_common import _pointer_at, failing_text, narrow, span, orig  # noqa: E402
from prop_property_common import Env  # noqa: E402

SET_CODE = [
    span(0x6EB040, 0x6EB0C6), span(0x5DEA10, 0x5DEA8E), span(0x6EB0D0, 0x6EB219), span(0x596400, 0x596428),
    span(0x5963A0, 0x5963A5), span(0x5963B0, 0x5963C8), span(0x5963D0, 0x5963D5), span(0x5963E0, 0x5963E5),
    span(0x5963F0, 0x5963F5), span(0x6EB4F0, 0x6EB556), span(0x6EB590, 0x6EB727), span(0x6EB220, 0x6EB4D2),
    span(0x6EB560, 0x6EB58D), span(0x6E9500, 0x6E952D), span(0x6EDFF0, 0x6EE013), span(0x6EE020, 0x6EE03D),
    span(0x6EE040, 0x6EE08E), span(0x6EC570, 0x6EC58D), span(0x6EC590, 0x6EC5B8), span(0x6ED380, 0x6ED3FC),
    span(0x6ED400, 0x6ED41D), span(0x6ED440, 0x6ED475), span(0x6ED4A0, 0x6ED4C2), span(0x6ED4D0, 0x6ED4F2),
    span(0x6EBCA0, 0x6EBD59),
]
SET_ALLOW = PROPERTY_ALLOW + SET_CODE
CODE_NOP = SCRATCH_CODE + 0xC0
VT_LIST, VT_GROUP = 0x896204, 0x8961A8


def setcall(first, last, conv, args, ret):
    return orig(first, last, conv, args, ret, allow=SET_ALLOW)


def make_env(precision):
    base = ENVS[precision]
    lib = base.lib
    calls = dict(base.setup_calls)
    calls["setctor"] = SetupCall(setcall(0x6EB040, 0x6EB0C6, "thiscall", [PTR, I32], PTR),
                                 Native(lib, "bc_TGModelPropertySet_TGModelPropertySet"))
    calls["add"] = SetupCall(setcall(0x6EB220, 0x6EB4D2, "thiscall", [PTR, Buf(io="in"), PTR], U8),
                             Native(lib, "bc_TGModelPropertySet_AddToSet"))
    calls["ictor"] = SetupCall(setcall(0x6ED380, 0x6ED3FC, "thiscall", [PTR, PTR, Buf(io="in")], PTR),
                               Native(lib, "bc_TGModelPropertyInstance_TGModelPropertyInstance"))
    calls["ictor_null"] = SetupCall(setcall(0x6ED380, 0x6ED3FC, "thiscall", [PTR, PTR, PTR], PTR),
                                    Native(lib, "bc_TGModelPropertyInstance_TGModelPropertyInstance"))
    calls["vptr"] = SetupCall(Original(CODE_NOP, 1, "cdecl", [PTR, U32], None), Native(lib, "bc_set_vptr"))
    return Env(precision, lib, base.model, base.cw, calls)


SENVS = {53: make_env(53), 24: make_env(24)}
MEMORY = {CODE_NOP: b"\xC3"}


def case(env, first, last, conv, args, ret, symbol, scenarios, **kw):
    kw.setdefault("memory", dict(MEMORY))
    from prop_property_common import MEMORY as BASE_MEMORY
    kw["memory"].update(BASE_MEMORY)
    kw.setdefault("stubs", property_stubs())
    return build_case(env, setcall(first, last, conv, args, ret), symbol, scenarios, **kw)


# Scenario building ------------------------------------------------------------------------------------------

def set_roots(props=("a", "b", "c", "d", "e"), subsystems=(), extra=None):
    roots = {"s": Obj("TGModelPropertySet")}
    roots.update({n: Obj("TGModelProperty") for n in props})
    roots.update({n: Obj("SubsystemProperty") for n in subsystems})
    roots.update(extra or {})
    return roots


def build(entries, kinds=None):
    """Setup calls: construct the set s and the properties, then add the entries (hardpoint, root, name)."""
    kinds = kinds or {}
    pre = [Pre("setctor", [Root("s"), 0])]
    seen = set()
    for _, root, name in entries:
        if root not in seen:
            pre += new_property(root, 0, name, cls=kinds.get(root, "ctor"))
            seen.add(root)
    pre += [Pre("add", [Root("s"), narrow(hp), Root(root)]) for hp, root, _ in entries]
    return pre


def scenario(roots, args, pre, label, factory="both", next_id=100):
    tables = [registry_for([]), real_factory(FACTORY_TABLES[factory])]
    kinds = []
    return Scenario(roots, args, globals=tables, pre=with_constructors(roots, pre),
                    setup=state_setup(next_id=next_id), label=label)


SUB = {"d": "sctor", "e": "sctor"}


def sub_roots():
    return set_roots(props=("a", "b", "c"), subsystems=("d", "e"))


SETS = {
    "empty": [],
    "one": [("Hull", "a", "Phaser 1")],
    "two names, one hardpoint": [("Hull", "a", "Phaser 1"), ("Hull", "b", "Phaser 2")],
    "two hardpoints": [("Hull", "a", "Phaser 1"), ("Engine", "b", "Phaser 2")],
    "five on one hardpoint": [("Hull", n, "P%s" % n) for n in "abc"],
    "mixed types": [("Hull", "a", "Phaser 1"), ("Engine", "d", "Impulse"), ("Hull", "e", "Shield"),
                    ("Aft", "b", "Phaser 2")],
}


def sets_with_subsystems():
    return {k: v for k, v in SETS.items()}


PROBES = [trap_probe(), next_id_probe()]
CAPTURED = []


def copy_count_probe():
    """The reference counts of the original template and of the stored copy of every instance, in list order."""
    def read(side, addresses, ret):
        model = side.model

        def ptr(addr, cls, name):
            return _pointer_at(side, addr, cls, name)

        def inline(addr, cls, name):
            return addr + side.offset(cls, model.member(cls, name))

        def count(addr):
            return 0 if not addr else struct.unpack("<i", side.read(addr + side.offset(
                "TGModelProperty", model.member("TGModelProperty", "refCount")), 4))[0]

        found = []
        header = ptr(addresses["s"], "TGModelPropertySet", "groups") if addresses.get("s") else 0
        node = ptr(header, "GroupList", "head") if header else 0
        while node:
            group = ptr(node, "GroupNode", "data")
            inst = ptr(inline(group, "TGModelPropertyGroup", "items"), "InstanceList", "head")
            while inst:
                instance = ptr(inst, "InstanceNode", "data")
                found.append((count(ptr(instance, "TGModelPropertyInstance", "originalTemplate")),
                              count(ptr(instance, "TGModelPropertyInstance", "property"))))
                inst = ptr(inst, "InstanceNode", "next")
            node = ptr(node, "GroupNode", "next")
        CAPTURED.append((type(side).__name__, found))
        return found
    return Probe("reference counts (template, copy) of the instances", read)


# Construction and constant functions -------------------------------------------------------------------------

@register("prop.set.TGModelPropertySet_TGModelPropertySet")
def ctor_test():
    scenarios = []
    for next_id, keep, advance in ((1, 1, 1), (50, 1, 1), (5, 1, 0)):
        for requested in (0, 1, 3, 49, 50, 100, 0x7FFFFFFF):
            for keys in (None, [], [2, 3]):
                glob = registry_global(None) if keys is None else registry_for(keys)
                scenarios.append(Scenario({"s": Obj("TGModelPropertySet")}, [Root("s"), requested], globals=[glob],
                                          setup=state_setup(next_id, keep, advance),
                                          label="counter %d advance %d id %d registry %s" % (
                                              next_id, advance, requested, "none" if keys is None else keys)))
    return case(SENVS[53], 0x6EB040, 0x6EB0C6, "thiscall", [PTR, I32], PTR, "bc_TGModelPropertySet_TGModelPropertySet",
                scenarios, ret_class="TGModelPropertySet", setup_calls={}, allow_leaks=True,  # an equal ID drops an object
                probes=PROBES)


@register("prop.set.TGModelPropertySet_TGModelPropertySet_default")
def ctor_default_test():
    scenarios = [Scenario({"s": Obj("TGModelPropertySet")}, [Root("s")], globals=[registry_for(keys)],
                          setup=state_setup(next_id), label="counter %d registry %s" % (next_id, keys))
                 for next_id in (1, 7, 500) for keys in ([], [2, 3], [7])]
    return case(SENVS[53], 0x5DEA10, 0x5DEA8E, "thiscall", [PTR], PTR, "bc_TGModelPropertySet_TGModelPropertySet_default",
                scenarios, ret_class="TGModelPropertySet", setup_calls={}, allow_leaks=True, probes=PROBES)


def constant(name, first, last, symbol, args, ret, arg_lists):
    def build_test():
        scenarios = [scenario(set_roots(), a, build(SETS["one"]), "args %s" % (a,)) for a in arg_lists]
        return case(SENVS[53], first, last, "thiscall", args, ret, symbol, scenarios, probes=PROBES)
    register("prop.set.TGModelPropertySet_" + name)(build_test)


constant("GetObjType", 0x5963A0, 0x5963A5, "bc_TGModelPropertySet_GetObjType", [PTR], I32, [[Root("s")]])
constant("IsTypeOf", 0x5963B0, 0x5963C8, "bc_TGModelPropertySet_IsTypeOf", [PTR, I32], U8,
         [[Root("s"), t] for t in TYPES + [0x602, 2, 0x601]])
constant("GetClassName", 0x5963D0, 0x5963D5, "bc_TGModelPropertySet_GetClassName", [PTR], U32, [[Root("s")]])
constant("GetSwigPointerName", 0x5963E0, 0x5963E5, "bc_TGModelPropertySet_GetSwigPointerName", [PTR], U32, [[Root("s")]])
constant("GetShadowClassName", 0x5963F0, 0x5963F5, "bc_TGModelPropertySet_GetShadowClassName", [PTR], U32, [[Root("s")]])


# FindGroup -------------------------------------------------------------------------------------------------

@register("prop.set.TGModelPropertySet_FindGroup")
def find_group_test():
    entries = [("Hull", "a", "Phaser 1"), ("hull", "b", "Phaser 2"), ("Engine", "c", "Impulse"), ("", "d", "Empty")]
    scenarios = []
    for label, content in (("empty set", []), ("four groups", entries)):
        for query in ("Hull", "hull", "HULL", "Hul", "Hulls", "Engine", "", "Aft", "Engine ", "\xe9"):
            scenarios.append(scenario(set_roots(), [Root("s"), narrow(query)], build(content), "%s, query %r" % (label, query)))
    return case(SENVS[53], 0x6EB4F0, 0x6EB556, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGModelPropertySet_FindGroup",
                scenarios, ret_class="TGModelPropertyGroup", probes=PROBES)


# AddToSet ---------------------------------------------------------------------------------------------------

def add_scenarios():
    out = []

    def add(label, content, hp, root, name, kinds=None, factory="both", roots=None):
        roots = roots or sub_roots()
        pre = build(content, kinds)
        # the added property is built after the content
        if root not in {r for _, r, _ in content}:
            pre += new_property(root, 0, name, cls=(kinds or {}).get(root, "ctor"))
        out.append(scenario(roots, [Root("s"), narrow(hp), Root(root)], pre, label, factory))

    add("new set, first property", [], "Hull", "a", "Phaser 1")
    add("new name on a known hardpoint", SETS["one"], "Hull", "b", "Phaser 2")
    add("new hardpoint", SETS["one"], "Engine", "b", "Phaser 1")
    add("hardpoint that differs in case only", SETS["one"], "hull", "b", "Phaser 1")
    add("same name, same hardpoint (replace)", SETS["one"], "Hull", "b", "Phaser 1")
    add("name that differs in case only (replace, B0002)", SETS["one"], "Hull", "b", "PHASER 1")
    add("name with an accent that differs in case (no replace)", [("Hull", "a", "caf\xe9")], "Hull", "b", "CAF\xc9")
    add("empty names (replace)", [("Hull", "a", "")], "Hull", "b", "")
    add("replace the second of three", SETS["five on one hardpoint"], "Hull", "d", "Pb", kinds=SUB)
    add("replace the last of three", SETS["five on one hardpoint"], "Hull", "e", "PC", kinds=SUB)
    add("fourth property on a hardpoint", SETS["five on one hardpoint"], "Hull", "d", "Pd", kinds=SUB)
    add("subsystem property on a new hardpoint", SETS["one"], "Aft", "d", "Impulse", kinds=SUB)
    add("subsystem property replaces a property", SETS["one"], "Hull", "d", "Phaser 1", kinds=SUB)
    add("property replaces a subsystem property", [("Hull", "d", "Phaser 1")], "Hull", "a", "phaser 1", kinds=SUB)
    add("subsystem property replaces a subsystem property", [("Hull", "d", "Impulse")], "Hull", "e", "IMPULSE",
        kinds=SUB)
    add("mixed set, replace in the third group", SETS["mixed types"], "Aft", "c", "phaser 2", kinds=SUB)
    add("same property again", SETS["one"], "Hull", "a", "Phaser 1")
    add("no creators: new instance has no copy", SETS["one"], "Engine", "b", "Phaser 2", factory="empty")
    add("no creators: replace stores no copy", SETS["one"], "Hull", "b", "PHASER 1", factory="empty")
    add("long names", [("Hull", "a", "n" * 40)], "Hull", "b", "N" * 40)
    add("long hardpoint names", [("h" * 70, "a", "x")], "h" * 70, "b", "y")
    return out


def add_builder(env):
    return case(env, 0x6EB220, 0x6EB4D2, "thiscall", [PTR, Buf(io="in"), PTR], U8, "bc_TGModelPropertySet_AddToSet",
                add_scenarios(), probes=PROBES + [copy_count_probe()], allow_leaks=True)  # B0002 leaves a copy


registered_both("prop.set.TGModelPropertySet_AddToSet", lambda env: add_builder(SENVS[env.precision]))


@register("prop.set.B0002_AddToSet_replace_leaves_the_copy_with_count_2")
def b0002_test():
    del CAPTURED[:]
    case_ = add_builder(SENVS[53])
    case_.scenarios = [s for s in case_.scenarios if s.label.startswith("name that differs in case only")]
    results = case_.run()
    assert all(r.ok for r in results), results[0].lines
    counts = {side: found for side, found in CAPTURED}
    assert counts == {"GuestSide": [(2, 2)], "NativeSide": [(2, 2)]}, counts


# GetPropertiesByType ------------------------------------------------------------------------------------------

@register("prop.set.TGModelPropertySet_GetPropertiesByType")
def by_type_test():
    scenarios = []
    for name, content in (("empty", []), ("mixed", SETS["mixed types"]), ("one hardpoint", SETS["five on one hardpoint"]),
                          ("plain only", SETS["two hardpoints"])):
        for t in (0x601, 0x812B, 0x602, 2, 0, 0x1234, -1, 0x7FFFFFFF):
            scenarios.append(scenario(sub_roots(), [Root("s"), t], build(content, SUB), "%s, type %#x" % (name, t)))
    return case(SENVS[53], 0x6EB590, 0x6EB727, "thiscall", [PTR, I32], PTR, "bc_TGModelPropertySet_GetPropertiesByType",
                scenarios, ret_class="TGModelPropertyList", probes=PROBES, allow_leaks=True)  # the caller owns the list


# Destruction -------------------------------------------------------------------------------------------------

def destruction_scenarios(args, with_replace=False):
    out = []
    for label, content in SETS.items():
        out.append(scenario(sub_roots(), args, build(content, SUB), label))
    out.append(scenario(sub_roots(), args, build(SETS["mixed types"] + [("Hull", "c", "phaser 1")], SUB),
                        "mixed with a second hardpoint entry"))
    big = [("H%d" % (i % 3), n, "P%d" % i) for i, n in enumerate("abcde")]
    out.append(scenario(sub_roots(), args, build(big, SUB), "five properties on three hardpoints"))
    return out


@register("prop.set.TGModelPropertySet_destructor")
def destructor_test():
    return case(SENVS[53], 0x6EB0D0, 0x6EB219, "thiscall", [PTR], None, "bc_TGModelPropertySet_DestructorBody",
                destruction_scenarios([Root("s")]), probes=PROBES)


@register("prop.set.TGModelPropertySet_DeletingDestructor")
def deleting_destructor_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        for s in destruction_scenarios([Root("s"), flags])[:4]:
            s.label = "flags %d, %s" % (flags, s.label)
            scenarios.append(s)
    return case(SENVS[53], 0x596400, 0x596428, "thiscall", [PTR, U32], PTR, "bc_TGModelPropertySet_DeletingDestructor",
                scenarios, ret_class="TGModelPropertySet", probes=PROBES)


@register("prop.set.destruction_after_a_replace_keeps_the_copy_alive_B0002")
def destructor_replace_test():
    content = [("Hull", "a", "Phaser 1"), ("Hull", "b", "PHASER 1")]
    scenarios = [scenario(sub_roots(), [Root("s")], build(content), "replace, then destroy")]
    return case(SENVS[53], 0x6EB0D0, 0x6EB219, "thiscall", [PTR], None, "bc_TGModelPropertySet_DestructorBody",
                scenarios, probes=PROBES, allow_leaks=True)  # the copy keeps the count 1 and is never freed


# Creators ----------------------------------------------------------------------------------------------------

def creator_case(first, last, symbol):
    scenarios = [Scenario({}, [i], globals=[registry_for(keys)], setup=state_setup(500),
                          label="id %d registry %s" % (i, keys)) for i in (0, 1, 100, 0x7FFFFFFF) for keys in ([], [3, 4])]
    return case(SENVS[53], first, last, "cdecl", [I32], PTR, symbol, scenarios, ret_class="TGModelPropertySet",
                probes=PROBES)


register("prop.set.TGModelPropertySet_CreateForFactory")(
    lambda: creator_case(0x6E9500, 0x6E952D, "bc_TGModelPropertySet_CreateForFactory"))
register("prop.set.TGModelPropertySet_Create")(lambda: creator_case(0x6EB560, 0x6EB58D, "bc_TGModelPropertySet_Create"))


@register("prop.set.slots_that_are_not_reconstructed_reach_their_trap")
def trap_slots_test():
    lib = native_lib()
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_new_TGModelPropertySet.restype = ctypes.c_void_p
    lib.bc_TGModelPropertySet_TGModelPropertySet.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.bc_TGModelPropertySet_CallSlot.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    rows = {}
    for line in open(os.path.join(REPO_ROOT, "re", "layouts", "TGModelPropertySet.vtable.tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[6] == "trap":
            rows[int(cells[2])] = int(cells[3], 16)
    assert rows == {4: 0x6EB730, 5: 0x6EB780, 6: 0x6EB8F0, 7: 0x6EB940}, rows
    obj = lib.bc_new_TGModelPropertySet()
    lib.bc_TGModelPropertySet_TGModelPropertySet(obj, 0)
    for slot, address in sorted(rows.items()):
        lib.bc_trap_clear()
        lib.bc_TGModelPropertySet_CallSlot(obj, slot)
        assert lib.bc_trap_count() == 1 and lib.bc_trap_address(0) == address, slot
    lib.bc_new_TGModelPropertyInstance.restype = ctypes.c_void_p
    instance_rows = {}
    for line in open(os.path.join(REPO_ROOT, "re", "layouts", "TGModelPropertyInstance.vtable.tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[6] == "out":
            instance_rows[int(cells[2])] = int(cells[3], 16)
    assert instance_rows == {1: 0x6ED500, 2: 0x6ED5C0, 3: 0x6ED630, 4: 0x6ED650}, instance_rows


# List and group ----------------------------------------------------------------------------------------------

def hand_list(kind, blocks, name=None, free=0):
    """A list or group (kind 'list' or 'group') with `blocks` node blocks of one node each, `free` free nodes."""
    nodes = [Obj("InstanceNode") for _ in range(blocks + free)]
    chain = None
    for i in range(blocks):
        chain = Obj("InstanceBlock", nextBlock=chain, nodes=nodes[i])
    items = Obj("InstanceList", count=0, head=None, tail=None, freeNodes=nodes[blocks] if free else None, blocks=chain,
                blockSize=2)
    values = {"items": items, "unused": 0}
    if kind == "group":
        values["hardpointName"] = name
    return Obj("TGModelPropertyGroup" if kind == "group" else "TGModelPropertyList", **values)


def list_scenarios(kind, args):
    out = []
    for blocks in (0, 1, 2, 4):
        for free in (0, 1):
            for name in ((None, "Hull", "h" * 40) if kind == "group" else (None,)):
                root = "g" if kind == "group" else "l"
                obj = hand_list(kind, blocks, name, free)
                pre = [Pre("vptr", [Root(root), 1 if kind == "group" else 0])]
                out.append(Scenario({root: obj}, [Root(root)] + list(args), globals=[registry_for([])], pre=pre,
                                    setup=state_setup(), label="%d blocks, %d free nodes, name %r" % (blocks, free, name and name[:6])))
    return out


@register("prop.set.TGModelPropertyList_TGModelPropertyList")
def list_ctor_test():
    scenarios = [Scenario({"l": Obj("TGModelPropertyList", unused=0x55, items=Obj(
        "InstanceList", count=v, head=None, tail=None, freeNodes=None, blocks=None, blockSize=v))},
                          [Root("l")], globals=[registry_for([])], setup=state_setup(), label="dirty header %d" % v)
                 for v in (0, 3, 9)]
    return case(SENVS[53], 0x6EDFF0, 0x6EE013, "thiscall", [PTR], PTR, "bc_TGModelPropertyList_TGModelPropertyList",
                scenarios, ret_class="TGModelPropertyList", probes=PROBES, allow_leaks=True)


@register("prop.set.TGModelPropertyList_destructor")
def list_destructor_test():
    return case(SENVS[53], 0x6EE040, 0x6EE08E, "thiscall", [PTR], None, "bc_TGModelPropertyList_DestructorBody",
                list_scenarios("list", []) + list_scenarios("group", []), probes=PROBES, allow_leaks=True)


@register("prop.set.TGModelPropertyList_DeletingDestructor")
def list_deleting_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        for s in list_scenarios("list", [flags])[:4]:
            s.label = "flags %d, %s" % (flags, s.label)
            scenarios.append(s)
    return case(SENVS[53], 0x6EE020, 0x6EE03D, "thiscall", [PTR, U32], PTR, "bc_TGModelPropertyList_DeletingDestructor",
                scenarios, ret_class="TGModelPropertyList", probes=PROBES, allow_leaks=True)


@register("prop.set.TGModelPropertyGroup_destructor")
def group_destructor_test():
    return case(SENVS[53], 0x6EC590, 0x6EC5B8, "thiscall", [PTR], None, "bc_TGModelPropertyGroup_DestructorBody",
                list_scenarios("group", []), probes=PROBES, allow_leaks=True)


@register("prop.set.TGModelPropertyGroup_DeletingDestructor")
def group_deleting_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        for s in list_scenarios("group", [flags])[:6]:
            s.label = "flags %d, %s" % (flags, s.label)
            scenarios.append(s)
    return case(SENVS[53], 0x6EC570, 0x6EC58D, "thiscall", [PTR, U32], PTR, "bc_TGModelPropertyGroup_DeletingDestructor",
                scenarios, ret_class="TGModelPropertyGroup", probes=PROBES, allow_leaks=True)


# Instance ----------------------------------------------------------------------------------------------------

def instance_roots():
    return {"i": Obj("TGModelPropertyInstance"), "a": Obj("TGModelProperty"), "b": Obj("TGModelProperty"),
            "d": Obj("SubsystemProperty")}


def instance_scenario(args, label, hardpoint="Hull", template="a", factory="both", more=()):
    pre = (new_property("a", 0, "Phaser 1") + new_property("b", 0, "Phaser 2") + new_property("d", 0, "Impulse", cls="sctor")
           + [Pre("ictor", [Root("i"), Root(template), narrow(hardpoint)])] + list(more))
    return scenario(instance_roots(), args, pre, label, factory)


@register("prop.set.TGModelPropertyInstance_TGModelPropertyInstance")
def instance_ctor_test():
    scenarios = []
    for template in ("a", "d"):
        for hardpoint in ("", "Hull", "h" * 40):
            for factory in ("both", "empty"):
                pre = new_property("a", 0, "Phaser 1") + new_property("d", 0, "Impulse", cls="sctor")
                scenarios.append(scenario(instance_roots(), [Root("i"), Root(template), narrow(hardpoint)], pre,
                                          "template %s, hardpoint %r, factory %s" % (template, hardpoint[:6], factory),
                                          factory))
    return case(SENVS[53], 0x6ED380, 0x6ED3FC, "thiscall", [PTR, PTR, Buf(io="in")], PTR,
                "bc_TGModelPropertyInstance_TGModelPropertyInstance", scenarios, ret_class="TGModelPropertyInstance",
                probes=PROBES, setup_calls=dict(SENVS[53].setup_calls))


@register("prop.set.TGModelPropertyInstance_destructor")
def instance_destructor_test():
    scenarios = [instance_scenario([Root("i")], "template %s" % t, template=t) for t in ("a", "d")]
    scenarios.append(instance_scenario([Root("i")], "template with count 3", more=[Pre("incref", [Root("a")])] * 2))
    return case(SENVS[53], 0x6ED440, 0x6ED475, "thiscall", [PTR], None, "bc_TGModelPropertyInstance_DestructorBody",
                scenarios, probes=PROBES)


@register("prop.set.TGModelPropertyInstance_DeletingDestructor")
def instance_deleting_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        scenarios.append(instance_scenario([Root("i"), flags], "flags %d" % flags))
    return case(SENVS[53], 0x6ED400, 0x6ED41D, "thiscall", [PTR, U32], PTR,
                "bc_TGModelPropertyInstance_DeletingDestructor", scenarios, ret_class="TGModelPropertyInstance",
                probes=PROBES)


def reference_scenarios(slot):
    out = []
    for new in ("a", "b", "d", None):
        for extra in (0, 2):
            more = [Pre("incref", [Root("b")])] * extra
            out.append(instance_scenario([Root("i"), Root(new) if new else 0],
                                         "%s <- %s, extra references %d" % (slot, new, extra), more=more))
    out.append(instance_scenario([Root("i"), Root("a")], "%s <- the stored one" % slot))
    return out


@register("prop.set.TGModelPropertyInstance_SetProperty")
def set_property_test():
    scenarios = reference_scenarios("copy")
    scenarios.append(instance_scenario([Root("i"), Root("a")], "own copy", more=[]))
    return case(SENVS[53], 0x6ED4A0, 0x6ED4C2, "thiscall", [PTR, PTR], None, "bc_TGModelPropertyInstance_SetProperty",
                scenarios, probes=PROBES)


@register("prop.set.TGModelPropertyInstance_SetOriginalTemplate")
def set_template_test():
    return case(SENVS[53], 0x6ED4D0, 0x6ED4F2, "thiscall", [PTR, PTR], None,
                "bc_TGModelPropertyInstance_SetOriginalTemplate", reference_scenarios("template"), probes=PROBES)


# Start-up registration ---------------------------------------------------------------------------------------

def startup_thunk():
    """The registrations of the exe in the order of the modules: 0x812b (0x43c5e6), then 0x601 and 0x602 (0x6e89de, 0x6e8a8b)."""
    address = SCRATCH + 0x2900
    code = bytearray()
    for creator, key in ((0x43FF30, 0x812B), (0x6E94B0, 0x601), (0x6E9500, 0x602)):
        code += b"\x68" + struct.pack("<I", creator) + b"\x68" + struct.pack("<I", key) + b"\xB9" + struct.pack("<I", 0x99A578)
        code += b"\xE8" + struct.pack("<i", 0x4468F0 - (address + len(code) + 5))
    code += b"\xC3"
    return address, bytes(code)


@register("prop.set.module_initializers_enter_the_creators_in_the_order_of_the_exe")
def module_initializers_test():
    address, code = startup_thunk()
    tables = [[], [(0x602, 0x6E9500)], [(0x601 + 0x25, 0x43FF30)], [(0x601, 0x43FF30)], [(0x812B + 0x25 * 0, 0x6E94B0)]]
    scenarios = [Scenario({}, [], globals=[real_factory(entries)], setup=state_setup(), label="table %s" % entries)
                 for entries in tables]
    thunk = Original(address, len(code), "cdecl", [], None, allow=[tuple(a) for a in SET_ALLOW])
    return build_case(SENVS[53], thunk, "bc_RunModuleInitializers", scenarios, memory={address: code},
                      stubs=property_stubs(), allow_leaks=True)


@register("prop.set.the_exe_runs_the_subsystem_creator_module_before_the_property_creator_module")
def module_order_test():
    from emuharness.case import EXE_PATH
    from emuharness.machine import Machine
    machine = Machine(EXE_PATH)

    def word(address):
        return struct.unpack("<I", machine.read(address, 4))[0]
    # The static initializer list holds the two module constructors; the first entry runs first.
    assert word(0x8BCB10) == 0x43AA70 and word(0x8D0EC4) == 0x6E8910 and 0x8BCB10 < 0x8D0EC4
    # Both module constructors pass the priority 0x20000000 to the module base constructor.
    assert machine.read(0x6E8963, 5) == b"\x68\x00\x00\x00\x20" and machine.read(0x43AB51, 5) == b"\x68\x00\x00\x00\x20"
    # The module list insert goes behind modules of equal priority: JA at 0x71e31c.
    assert machine.read(0x71E319, 5) == b"\x3B\x50\x10\x77\x09"
    # Slot 0 of both module tables holds the registration code.
    assert word(0x896180) == 0x6E8980 and word(0x889B84) == 0x43AD70


# Planted defects -------------------------------------------------------------------------------------------------

@register("prop.set.planted_defect_AddToSet_compares_the_names_with_case")
def planted_add_case():
    scenarios = [s for s in add_scenarios() if s.label.startswith("name that differs in case only")]
    text = failing_text(case(SENVS[53], 0x6EB220, 0x6EB4D2, "thiscall", [PTR, Buf(io="in"), PTR], U8,
                             "bc_TGModelPropertySet_AddToSet_defect", scenarios,
                             probes=PROBES + [copy_count_probe()], allow_leaks=True))
    assert "s.groups.head.data.items.count: original=1 native=2" in text, text
    assert "probe reference counts (template, copy) of the instances: original=[(2, 2)] native=[(2, 1), (2, 1)]" in text, text


@register("prop.set.planted_defect_GetPropertiesByType_returns_the_wrong_order")
def planted_by_type_order():
    scenarios = [scenario(sub_roots(), [Root("s"), 0x601], build(SETS["mixed types"], SUB), "mixed set")]
    text = failing_text(case(SENVS[53], 0x6EB590, 0x6EB727, "thiscall", [PTR, I32], PTR,
                             "bc_TGModelPropertySet_GetPropertiesByType_defect", scenarios,
                             ret_class="TGModelPropertyList", probes=PROBES, allow_leaks=True))
    assert 'ret.items.head.data.hardpointName: original="Hull" native="Aft"' in text, text


@register("prop.set.planted_defect_destructor_leaks_a_group")
def planted_destructor_leak():
    scenarios = [scenario(sub_roots(), [Root("s")], build(SETS["two hardpoints"]), "two groups")]
    text = failing_text(case(SENVS[53], 0x6EB0D0, 0x6EB219, "thiscall", [PTR], None,
                             "bc_TGModelPropertySet_DestructorBody_defect", scenarios, probes=PROBES))
    assert "heap blocks freed by the call: original=22 native=20" in text, text


@register("prop.set.B0005_destroying_an_instance_without_a_copy_faults_in_the_exe")
def b0005_test():
    scenario_ = scenario(sub_roots(), [Root("s")], build(SETS["one"]), "no creators", factory="empty")
    text = failing_text(case(SENVS[53], 0x6EB0D0, 0x6EB219, "thiscall", [PTR], None,
                             "bc_TGModelPropertySet_DestructorBody", [scenario_], probes=PROBES))
    assert "original run stopped: UnmappedAccess: read from unmapped memory at eip 0x006ec790" in text, text
