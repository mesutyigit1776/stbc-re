"""Tests of ObjectRegistry, ClassFactoryTable and TGObject.

The registry and its objects are built as a graph on both sides. The functions that work on the global registry
read the pointer at 0x99a67c, which the case makes a global object. The statics of TGObject (the ID counter and
the two flags) are set on both sides before each scenario. The virtual calls into the registry slots run the
code of the exe (allowed ranges).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from prop_common import (ALLOW, CREATOR_BASE, CREATOR_STRIDE, FACTORY_SLOTS, OBJECT_CODE, REGISTRY_SLOTS,  # noqa: E402
                         creator, creator_stub, creator_stubs, destructor_stubs, factory_global, make_registry,
                         native_lib, next_id_probe, object_state, orig, failing_text, pool_stubs_zeroed,
                         registry_global, remove_from_event_tables_stub, sized_free_stubs, span, trap_probe,
                         with_registry)

def reg_orig(first, last, conv, args, ret):
    return orig(first, last, conv, args, ret, allow=ALLOW)


def registry_case(original, symbol, scenarios, ret_class=None, stubs=None, probes=(), label="", allow_leaks=False,
                  setup_calls=None):
    return ObjectCase(MODEL, original, Native(LIB, symbol), scenarios, stubs=stubs or pool_stubs_zeroed(),
                      ret_class=ret_class, probes=list(probes), label=label, allow_leaks=allow_leaks,
                      setup_calls=setup_calls or {})


# Slots of the ID registry -----------------------------------------------------------------------------------

def bare_registry(bucket_count):
    return Obj("ObjectRegistry", bucketCount=bucket_count, buckets=[None] * bucket_count)


KEYS = [0, 1, 2, 6, 7, 8, 100, 0x7F6, 0x7F7, 0x7F8, 0x1000, 0xFFFFFFFF, 0x80000000, 123456789]


@register("prop.registry.Hash")
def hash_test():
    scenarios = [Scenario({"r": bare_registry(n)}, [Root("r"), k], label="buckets %d key %#x" % (n, k))
                 for n in (1, 2, 7, 0x25, 0x7F7) for k in KEYS]
    return registry_case(reg_orig(0x6F1960, 0x6F196D, "thiscall", [PTR, U32], U32), "bc_ObjectRegistry_Hash", scenarios)


@register("prop.registry.KeysEqual")
def keys_equal_test():
    pairs = [(a, b) for a in (0, 1, 7, 0xFFFFFFFF, 0x80000000) for b in (0, 1, 7, 0xFFFFFFFF, 0x80000000)]
    scenarios = [Scenario({"r": bare_registry(7)}, [Root("r"), a, b], label="%#x %#x" % (a, b)) for a, b in pairs]
    return registry_case(reg_orig(0x6F1970, 0x6F1981, "thiscall", [PTR, U32, U32], U8), "bc_ObjectRegistry_KeysEqual",
                         scenarios)


@register("prop.registry.SetNode")
def set_node_test():
    scenarios = []
    for key in (0, 5, 0xFFFFFFFF):
        for node_state in ("blank", "used"):
            node = Obj("ObjectRegistryNode") if node_state == "blank" else Obj(
                "ObjectRegistryNode", key=9, value=Obj("TGObject", id=9), next=Obj("ObjectRegistryNode", key=3))
            scenarios.append(Scenario({"r": bare_registry(7), "n": node, "o": Obj("TGObject", id=key)},
                                      [Root("r"), Root("n"), key, Root("o")], label="key %#x, %s node" % (key, node_state)))
    return registry_case(reg_orig(0x6F1990, 0x6F19A3, "thiscall", [PTR, PTR, U32, PTR], None),
                         "bc_ObjectRegistry_SetNode", scenarios, allow_leaks=True)  # the old value of a used node


@register("prop.registry.ClearNode")
def clear_node_test():
    node = lambda: Obj("ObjectRegistryNode", key=9, value=Obj("TGObject", id=9), next=Obj("ObjectRegistryNode", key=3))
    scenarios = [Scenario({"r": bare_registry(7), "n": node()}, [Root("r"), Root("n")], label="used node"),
                 Scenario({"r": bare_registry(7), "n": Obj("ObjectRegistryNode")}, [Root("r"), Root("n")], label="blank node")]
    return registry_case(reg_orig(0x6F19B0, 0x6F19B2, "thiscall", [PTR, PTR], None), "bc_ObjectRegistry_ClearNode",
                         scenarios)


@register("prop.registry.DeletingDestructor")
def registry_destructor_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        for keys, buckets in (([], 7), ([4], 7), ([1, 8, 15, 3], 7), ([5, 6, 7, 12, 13, 19, 26], 7), (list(range(40)), 0x25),
                              ([10, 20, 30], 0x7F7)):
            registry, _ = make_registry(keys, buckets)
            scenarios.append(Scenario({"r": registry}, [Root("r"), flags],
                                      label="flags %d, %d objects, %d buckets" % (flags, len(keys), buckets)))
    return registry_case(reg_orig(0x6F1AC0, 0x6F1B37, "thiscall", [PTR, U32], PTR), "bc_ObjectRegistry_DeletingDestructor",
                         scenarios, ret_class="ObjectRegistry", allow_leaks=True)


# Global functions on the registry --------------------------------------------------------------------------

@register("prop.object.GetTGObjectPtr")
def get_ptr_test():
    scenarios = []
    for keys in ([], [1], [1, 2, 3], [5, 5 + 0x7F7, 5 + 2 * 0x7F7], [0, 0x7F7, 0xFFFFFFFF % 0x7F7]):
        for wanted in (1, 5, 5 + 0x7F7, 5 + 2 * 0x7F7, 0, 99, 0x7F7):
            glob, _ = with_registry(keys)
            scenarios.append(Scenario({}, [wanted], globals=[glob], setup=object_state(),
                                      label="keys %s, id %d" % (keys, wanted)))
    glob, _ = with_registry([4, 11, 18], 7)
    scenarios.append(Scenario({}, [11], globals=[glob], setup=object_state(), label="7 buckets, third in chain"))
    return registry_case(reg_orig(0x6F0EE0, 0x6F0F21, "cdecl", [I32], PTR), "bc_TGObject_GetTGObjectPtr", scenarios,
                         ret_class="TGObject")


@register("prop.object.RegisterID")
def register_id_test():
    scenarios = []
    for keys in ([], [1], [3, 3 + 0x7F7], [10, 20, 30, 10 + 0x7F7]):
        for new_id in (1, 2, 3, 3 + 0x7F7, 3 + 2 * 0x7F7, 10, 99, 0, 0x7FFFFFFF):
            glob, _ = with_registry(keys)
            scenarios.append(Scenario({"o": Obj("TGObject", id=new_id)}, [Root("o")], globals=[glob],
                                      setup=object_state(), label="keys %s, new id %d" % (keys, new_id)))
    glob, _ = with_registry([3, 10], 7)
    scenarios.append(Scenario({"o": Obj("TGObject", id=17)}, [Root("o")], globals=[glob], setup=object_state(),
                              label="7 buckets, same bucket"))
    glob, _ = with_registry([3, 10], 7)
    scenarios.append(Scenario({"o": Obj("TGObject", id=10)}, [Root("o")], globals=[glob], setup=object_state(),
                              label="7 buckets, replaces the entry"))
    return registry_case(reg_orig(0x6F0F30, 0x6F0FBE, "cdecl", [PTR], U8), "bc_TGObject_RegisterID", scenarios,
                         allow_leaks=True)  # an entry with the same ID drops its old object


def unregister_scenarios():
    scenarios = []
    for keep in (0, 1):
        for keys in ([], [4], [4, 4 + 0x7F7, 4 + 2 * 0x7F7], [1, 2, 3]):
            for target in (4, 4 + 0x7F7, 4 + 2 * 0x7F7, 2, 77):
                glob, objects = with_registry(keys)
                victim = objects.get(target) or Obj("TGObject", id=target)
                scenarios.append(Scenario({"victim": victim}, [Root("victim")], globals=[glob],
                                          setup=object_state(keep=keep),
                                          label="keep=%d keys %s remove %d" % (keep, keys, target)))
    return scenarios


UNREGISTER = reg_orig(0x6F0FC0, 0x6F1076, "cdecl", [PTR], U8)


@register("prop.object.UnregisterID")
def unregister_id_test():
    return registry_case(UNREGISTER, "bc_TGObject_UnregisterID", unregister_scenarios(), allow_leaks=True)


def registry_pointer_is_set(side):
    if isinstance(side, GuestSide):
        return side.machine.read_u32(REGISTRY_POINTER) != 0
    side.lib.bc_TGObject_registry.restype = ctypes.c_void_p
    return bool(side.lib.bc_TGObject_registry())


@register("prop.object.UnregisterID_keeps_the_pointer_to_the_freed_registry_B0001")
def unregister_b0001_test():
    seen = {}

    def read(side, addresses, ret):
        seen[type(side).__name__] = registry_pointer_is_set(side)
        return seen[type(side).__name__]

    glob, objects = with_registry([4])
    scenario = Scenario({"victim": objects[4]}, [Root("victim")], globals=[glob], setup=object_state(keep=0),
                        label="last object, keep flag 0")
    case = registry_case(UNREGISTER, "bc_TGObject_UnregisterID", [scenario],
                         probes=[Probe("registry pointer is set after the call", read)], allow_leaks=True)
    results = case.run()
    assert all(r.ok for r in results), "\n".join(line for r in results for line in r.lines)
    assert seen == {"GuestSide": True, "NativeSide": True}, seen


# TGObject ------------------------------------------------------------------------------------------------------

CTOR = reg_orig(0x6F0A70, 0x6F0B54, "thiscall", [PTR, I32], PTR)


@register("prop.object.TGObject_TGObject")
def tgobject_ctor_test():
    scenarios = []
    for next_id, keep, advance in ((1, 1, 1), (5, 1, 1), (1, 1, 0), (10, 0, 1), (0x7FFFFFFE, 1, 1)):
        for requested in (0, 1, 3, 5, 9, 10, 11, 100, 0x7F8):
            for keys in (None, [], [2, 3]):
                if keys is None:
                    glob = registry_global(None)
                else:
                    glob, _ = with_registry(keys)
                scenarios.append(Scenario({"o": Obj("TGObject")}, [Root("o"), requested], globals=[glob],
                                          setup=object_state(next_id, keep, advance),
                                          label="counter %d advance %d id %d registry %s"
                                          % (next_id, advance, requested, "none" if keys is None else keys)))
    return registry_case(CTOR, "bc_TGObject_TGObject", scenarios, ret_class="TGObject", probes=[next_id_probe()],
                         allow_leaks=True)  # an entry with the same ID drops its old object


def destructor_scenarios(arg_flags=None):
    scenarios = []
    for keep in (0, 1):
        for keys in ([5], [5, 6], [5, 5 + 0x7F7]):
            for flags in (arg_flags or [None]):
                glob, objects = with_registry(keys)
                args = [Root("o")] if flags is None else [Root("o"), flags]
                scenarios.append(Scenario({"o": objects[5], "other": Obj("TGObject", id=77)}, args, globals=[glob],
                                          setup=object_state(keep=keep),
                                          label="keep=%d keys %s flags %s" % (keep, keys, flags)))
    glob, _ = with_registry([6])
    scenarios.append(Scenario({"o": Obj("TGObject", id=77)}, [Root("o")] if arg_flags is None else [Root("o"), 1],
                              globals=[glob], setup=object_state(), label="object not in the registry"))
    return scenarios


@register("prop.object.TGObject_destructor")
def tgobject_dtor_test():
    return registry_case(reg_orig(0x6F0BA0, 0x6F0BB9, "thiscall", [PTR], None), "bc_TGObject_DestructorBody",
                         destructor_scenarios(), stubs=destructor_stubs(), probes=[trap_probe()], allow_leaks=True)


@register("prop.object.TGObject_DeletingDestructor")
def tgobject_deleting_dtor_test():
    return registry_case(reg_orig(0x6F0B70, 0x6F0B98, "thiscall", [PTR, U32], PTR), "bc_TGObject_DeletingDestructor",
                         destructor_scenarios([0, 1, 2, 3]), ret_class="TGObject", stubs=destructor_stubs(),
                         probes=[trap_probe()], allow_leaks=True)


@register("prop.object.TGObject_GetObjType")
def get_obj_type_test():
    return registry_case(reg_orig(0x6F0B60, 0x6F0B65, "thiscall", [PTR], I32), "bc_TGObject_GetObjType",
                         [Scenario({"o": Obj("TGObject", id=7)}, [Root("o")])])


@register("prop.object.TGObject_IsTypeOf")
def is_type_of_test():
    types = [0, 1, 2, 3, 0x601, 0x602, 0x812B, -1, 0x7FFFFFFF, -2147483648]
    scenarios = [Scenario({"o": Obj("TGObject", id=7)}, [Root("o"), t], label="type %#x" % (t & 0xFFFFFFFF)) for t in types]
    return registry_case(reg_orig(0x518AB0, 0x518ABA, "thiscall", [PTR, I32], U8), "bc_TGObject_IsTypeOf", scenarios)


@register("prop.object.TGObject_class_name_functions")
def class_name_test():
    cases = []
    for first, last, symbol in ((0x6F1540, 0x6F1545, "bc_TGObject_GetClassName"),
                                (0x6F1550, 0x6F1555, "bc_TGObject_GetSwigPointerName"),
                                (0x6F1560, 0x6F1565, "bc_TGObject_GetShadowClassName")):
        cases.append(registry_case(reg_orig(first, last, "thiscall", [PTR], U32), symbol,
                                   [Scenario({"o": Obj("TGObject", id=7)}, [Root("o")])]))
    return cases


@register("prop.object.TGObject_class_name_text_matches_the_exe_strings")
def class_name_text_test():
    from emuharness.case import EXE_PATH
    from emuharness.machine import Machine
    machine = Machine(EXE_PATH)
    lib = native_lib()
    lib.bc_new_TGObject.restype = ctypes.c_void_p
    obj = lib.bc_new_TGObject()
    for address, symbol in ((0x95B05C, "bc_TGObject_GetClassNameText"), (0x9142B0, "bc_TGObject_GetSwigPointerNameText"),
                            (0x95B270, "bc_TGObject_GetShadowClassNameText")):
        text = bytearray()
        while machine.read(address + len(text), 1) != b"\0":
            text += machine.read(address + len(text), 1)
        function = getattr(lib, symbol)
        function.restype, function.argtypes = ctypes.c_char_p, [ctypes.c_void_p]
        assert function(obj) == bytes(text), (symbol, function(obj), bytes(text))


TRAP_SLOTS = {3: 0x6F1650, 4: 0x6F0BC0, 5: 0x859A0B, 6: 0x859A0B, 7: 0x859A0B, 8: 0x6F15C0}


@register("prop.object.TGObject_slots_that_are_not_reconstructed_reach_their_trap")
def trap_slots_test():
    lib = native_lib()
    lib.bc_new_TGObject.restype = ctypes.c_void_p
    lib.bc_trap_address.restype = ctypes.c_uint32
    obj = lib.bc_new_TGObject()
    rows = {}
    for line in open(os.path.join(REPO_ROOT, "re", "layouts", "TGObject.vtable.tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[6] in ("trap", "pure"):
            rows[int(cells[2])] = int(cells[3], 16)
    assert rows == TRAP_SLOTS, (rows, TRAP_SLOTS)
    for slot, address in sorted(TRAP_SLOTS.items()):
        lib.bc_trap_clear()
        lib.bc_TGObject_CallSlot.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        lib.bc_TGObject_CallSlot(obj, slot)
        assert lib.bc_trap_count() == 1 and lib.bc_trap_address(0) == address, (slot, lib.bc_trap_count())


# Class factory --------------------------------------------------------------------------------------------

def factory_case(original, symbol, scenarios, **kw):
    stubs = pool_stubs_zeroed()
    stubs.update(creator_stubs())
    return registry_case(original, symbol, scenarios, stubs=stubs, **kw)


@register("prop.factory.TGObject_CreateByType")
def create_by_type_test():
    tables = [[], [(0x601, 0)], [(0x601, 0), (0x602, 1), (0x812B, 2)], [(5, 0), (5 + 0x25, 1), (5 + 0x4A, 2)],
              [(0, 3), (0xFFFFFFFF, 1)]]
    scenarios = []
    for entries in tables:
        for wanted in (0x601, 0x602, 0x812B, 5, 5 + 0x25, 5 + 0x4A, 0, -1, 77):
            for ident in (0, 12):
                scenarios.append(Scenario({}, [wanted, ident], globals=[factory_global(entries)],
                                          label="table %s, type %#x, id %d" % (entries, wanted & 0xFFFFFFFF, ident)))
    return factory_case(reg_orig(0x6F13E0, 0x6F1431, "cdecl", [I32, I32], PTR), "bc_TGObject_CreateByType",
                        scenarios, ret_class="TGObject")


@register("prop.factory.ClassFactoryTable_Insert")
def factory_insert_test():
    scenarios = []
    for entries in ([], [(0x601, 0)], [(5, 0), (5 + 0x25, 1)]):
        for key, index in ((0x601, 1), (0x602, 2), (5, 3), (5 + 0x25, 2), (5 + 0x4A, 3), (0, 0)):
            scenarios.append(Scenario({"t": Obj("TGObject")}, [Root("t"), key, creator(index)],
                                      globals=[factory_global(entries)], label="table %s, add %#x" % (entries, key)))
    original = reg_orig(0x4468F0, 0x446975, "thiscall", [PTR, U32, U32], None)
    scenarios = [Scenario(s.roots, [0x99A578] + s.args[1:], globals=s.globals, label=s.label) for s in scenarios]
    return factory_case(original, "bc_ClassFactoryTable_InsertGlobal", scenarios)


@register("prop.factory.ClassFactoryTable_slots")
def factory_slots_test():
    table = lambda n=0x25: Obj("ClassFactoryTable", bucketCount=n, buckets=[None] * n)
    cases = []
    cases.append(factory_case(reg_orig(0x6F1890, 0x6F189E, "thiscall", [PTR, U32], U32), "bc_ClassFactoryTable_Hash",
                              [Scenario({"t": table(n)}, [Root("t"), k]) for n in (1, 7, 0x25) for k in KEYS]))
    cases.append(factory_case(reg_orig(0x6F18A0, 0x6F18B1, "thiscall", [PTR, U32, U32], U8),
                              "bc_ClassFactoryTable_KeysEqual",
                              [Scenario({"t": table()}, [Root("t"), a, b]) for a in (0, 5, 0xFFFFFFFF) for b in (0, 5, 0xFFFFFFFF)]))
    cases.append(factory_case(reg_orig(0x6F18C0, 0x6F18D3, "thiscall", [PTR, PTR, U32, U32], None),
                              "bc_ClassFactoryTable_SetNode",
                              [Scenario({"t": table(), "n": Obj("ClassFactoryNode", key=3, value=creator(1))},
                                        [Root("t"), Root("n"), key, creator(index)])
                               for key in (0, 0x601, 0xFFFFFFFF) for index in (0, 2)]))
    cases.append(factory_case(reg_orig(0x6F18E0, 0x6F18E2, "thiscall", [PTR, PTR], None),
                              "bc_ClassFactoryTable_ClearNode",
                              [Scenario({"t": table(), "n": Obj("ClassFactoryNode", key=3, value=creator(1))},
                                        [Root("t"), Root("n")])]))
    return cases


@register("prop.factory.ClassFactoryTable_DeletingDestructor")
def factory_destructor_test():
    scenarios = []
    for flags in (0, 1):
        for entries in ([], [(0x601, 0)], [(5, 0), (5 + 0x25, 1), (6, 2), (0x812B, 3)]):
            heads = [None] * 0x25
            for key, index in entries:
                heads[key % 0x25] = Obj("ClassFactoryNode", key=key, value=creator(index), next=heads[key % 0x25])
            table = Obj("ClassFactoryTable", count=len(entries), bucketCount=0x25, buckets=heads)
            scenarios.append(Scenario({"t": table}, [Root("t"), flags], label="flags %d, entries %s" % (flags, entries)))
    return factory_case(reg_orig(0x6F19C0, 0x6F1A37, "thiscall", [PTR, U32], PTR), "bc_ClassFactoryTable_DeletingDestructor",
                        scenarios, ret_class="ClassFactoryTable", allow_leaks=True)


def guard_setup(value):
    def setup(machine, lib):
        lib.bc_ClassFactoryTable_SetGuard.argtypes = [ctypes.c_uint8]
        lib.bc_ClassFactoryTable_SetGuard(value)
        machine.write(0x99A524, bytes([value]))
    return setup


@register("prop.factory.ClassFactoryTable_InitGlobal")
def factory_init_test():
    scenarios = []
    for entries in ([], [(0x601, 0), (0x602, 1)]):
        scenarios.append(Scenario({}, [], globals=[factory_global(entries, 7)], setup=guard_setup(0),
                                  label="before: table with %d entries, 7 buckets" % len(entries)))
    return factory_case(reg_orig(0x6F08B0, 0x6F08EF, "cdecl", [], None), "bc_ClassFactoryTable_InitGlobal", scenarios,
                        allow_leaks=True)


@register("prop.factory.ClassFactoryTable_DestroyGlobal")
def factory_destroy_test():
    scenarios = []
    for guard in (0, 4, 1, 5):
        for entries in ([], [(0x601, 0), (0x602, 1), (5, 2), (5 + 0x25, 3)]):
            scenarios.append(Scenario({}, [], globals=[factory_global(entries)], setup=guard_setup(guard),
                                      label="guard %d, %d entries" % (guard, len(entries))))
    probe = Probe("destroy guard", lambda side, a, r: _guard(side))
    return factory_case(reg_orig(0x6F0900, 0x6F0988, "cdecl", [], None), "bc_ClassFactoryTable_DestroyGlobal", scenarios,
                        probes=[probe], allow_leaks=True)


def _guard(side):
    if isinstance(side, GuestSide):
        return side.machine.read(0x99A524, 1)[0]
    side.lib.bc_ClassFactoryTable_GetGuard.restype = ctypes.c_uint8
    return side.lib.bc_ClassFactoryTable_GetGuard()


# Planted defects ----------------------------------------------------------------------------------------------

@register("prop.object.planted_defect_register_uses_the_next_bucket")
def planted_register():
    scenarios = []
    for new_id in (1, 2, 3):
        glob, _ = with_registry([40])
        scenarios.append(Scenario({"o": Obj("TGObject", id=new_id)}, [Root("o")], globals=[glob],
                                  setup=object_state(), label="new id %d" % new_id))
    text = failing_text(registry_case(reg_orig(0x6F0F30, 0x6F0FBE, "cdecl", [PTR], U8), "bc_TGObject_RegisterID_defect",
                                      scenarios, allow_leaks=True))
    assert "registry_pointer.registry.buckets[1]: original=a ObjectRegistryNode object native=null" in text, text
    assert "registry_pointer.registry.buckets[2]: original=null native=a ObjectRegistryNode object" in text, text


@register("prop.object.planted_defect_unregister_leaves_the_node_in_its_chain")
def planted_unregister():
    glob, objects = with_registry([4, 4 + 0x7F7])
    scenarios = [Scenario({"victim": objects[4 + 0x7F7]}, [Root("victim")], globals=[glob], setup=object_state(),
                          label="head of a chain of two")]
    text = failing_text(registry_case(UNREGISTER, "bc_TGObject_UnregisterID_defect", scenarios, allow_leaks=True))
    assert "registry_pointer.registry.buckets[4]" in text, text


@register("prop.harness.setup_that_changes_only_one_side_is_found")
def planted_setup_one_side():
    def guest_only(machine, lib):
        object_state(next_id=1)(machine, lib)
        machine.write(NEXT_ID, struct.pack("<i", 9))

    scenarios = [Scenario({"o": Obj("TGObject")}, [Root("o"), 0], globals=[registry_global(None)], setup=guest_only,
                          label="counter 9 on the exe side only")]
    text = failing_text(registry_case(CTOR, "bc_TGObject_TGObject", scenarios, ret_class="TGObject",
                                      probes=[next_id_probe()]))
    assert "probe object ID counter: original=10 native=2" in text, text
    assert "o.id: original=9 native=1" in text, text
