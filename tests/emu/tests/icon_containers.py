"""Tests of the containers of the icon classes: the texture array of a group (0x72ced0, 0x72d050, 0x72d1f0, 0x4d7290,
0x72cf40, 0x72ce60, 0x72cea0, 0x72af90), the location table of a group (0x72ae80 to 0x72aed0, 0x72af10), the texture
cache of the manager (0x44ebd0, 0x44edb0 to 0x44ef10, 0x44f030, 0x44f050), the registration record (0x747990, 0x747a60)
and the list code of the group list (0x72aff0, 0x72b0b0).

The array, the table and the cache are members of a group or of the manager, so the arguments name them with Inner. A
group comes from the constructor of the exe; an object that a function takes over with one reference has the count 1.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import (build_group, cache_hash, empty_cache, empty_list, holder, icon_case, manager_global,  # noqa: E402
                         manager_obj, registered_once, registered_pair, scenario, texture)
from emuharness import Inner  # noqa: E402

ARRAY = Inner("g", "textures")
TABLE = Inner("g", "locations")
CACHE = Inner("m", "textureCache")


def holder_roots(count, **extra):
    roots = {"g": Obj("IconGroup")}
    for i in range(1, count + 1):
        roots["h%d" % i] = holder(i, count=1)
    roots["cell"] = Obj("PropertyCellObject")
    roots["cell2"] = Obj("PropertyCellObject")
    roots.update(extra)
    return roots


def filled(n, root="g"):
    """Setup calls: a group and n holders added to its array."""
    pre = build_group(root, "G")
    pre += [Pre("arradd", [Inner(root, "textures"), Root("h%d" % i)]) for i in range(1, n + 1)]
    return pre


def array_scenario(args, n, label, roots=None, spare=4):
    roots = roots or holder_roots(n + spare)
    return scenario(roots, args, filled(n), [], label)


# The texture array -------------------------------------------------------------------------------------------------------

ARRAY_SIZES = (0, 1, 3, 14, 15, 16, 25)


@registered_once("icon.array.TGIconTextureArray_Add")
def add_test(env):
    scenarios = [array_scenario([ARRAY, Root("h%d" % (n + 1))], n, "after %d elements" % n) for n in ARRAY_SIZES]
    return icon_case(env, 0x72CED0, 0x72CF3F, "thiscall", [PTR, PTR], None, "bc_IconTextureArray_Add", scenarios)


@registered_once("icon.array.TGIconTextureArray_SetAtGrow")
def set_at_grow_test(env):
    scenarios = []
    for n in (0, 3, 15):
        for index in (0, 1, 2, 3, 4, 14, 15, 16, 20, 40, 100):
            scenarios.append(array_scenario([ARRAY, index, Root("h%d" % (n + 1))], n, "%d elements, index %d" % (n, index)))
    return icon_case(env, 0x72D050, 0x72D1BF, "thiscall", [PTR, U32, PTR], U32, "bc_IconTextureArray_SetAtGrow", scenarios)


@registered_once("icon.array.TGIconTextureArray_StoreAt")
def store_at_test(env):
    scenarios = []
    for n in (0, 3, 15):
        for index in (0, 1, 2, 3, 5, 14, 15, 16, 40):
            scenarios.append(array_scenario([ARRAY, index, Root("h%d" % (n + 1))], n, "%d elements, index %d" % (n, index)))
            scenarios.append(array_scenario([ARRAY, index, 0], n, "%d elements, index %d, a null element" % (n, index)))
    scenarios.append(array_scenario([ARRAY, 1, Root("h2")], 3, "the element that is already there"))
    return icon_case(env, 0x72D1F0, 0x72D2FF, "thiscall", [PTR, U32, PTR], None, "bc_IconTextureArray_StoreAt", scenarios)


@registered_once("icon.array.TGIconTextureArray_GetAt")
def get_at_test(env):
    scenarios = []
    for n in (0, 1, 3, 15):
        for index in (0, 1, 2, 3, 5, 14):
            scenarios.append(array_scenario([ARRAY, Root("cell"), index], n, "%d elements, index %d" % (n, index)))
    return icon_case(env, 0x4D7290, 0x4D72BF, "thiscall", [PTR, PTR, U32], None, "bc_IconTextureArray_GetAt", scenarios)


@registered_once("icon.array.TGIconTextureArray_RemoveAt")
def remove_at_test(env):
    scenarios = []
    for n in (0, 1, 3, 15, 16):
        for index in (0, 1, 2, 3, 14, 15, 16, 40):
            scenarios.append(array_scenario([ARRAY, Root("cell"), index], n, "%d elements, index %d" % (n, index)))
    # a gap in the array: StoreAt with a null element
    pre = filled(4) + [Pre("arrstore", [ARRAY, 1, 0]), Pre("arrstore", [ARRAY, 3, 0])]
    for index in (0, 1, 2, 3):
        scenarios.append(scenario(holder_roots(8), [ARRAY, Root("cell"), index], pre, [], "gaps, index %d" % index))
    return icon_case(env, 0x72CF40, 0x72D04F, "thiscall", [PTR, PTR, U32], PTR, "bc_IconTextureArray_RemoveAt", scenarios)


@registered_once("icon.array.TGIconTextureArray_AssignElement")
def assign_element_test(env):
    tex = texture(30, 8, 8, count=1)
    cases = [("null to null", None, None), ("a holder to null", holder(10, count=1), None),
             ("null to a holder", None, holder(11, count=1)), ("a holder to another", holder(12, count=1), holder(13, count=1)),
             ("a holder to itself", "same", "same"), ("a holder that has the count 1", holder(14, count=1), holder(15, count=2))]
    scenarios = []
    for label, destination, source in cases:
        shared = holder(16, count=2)
        dest = shared if destination == "same" else destination
        src = shared if source == "same" else source
        roots = {"cell": Obj("PropertyCellObject", value=dest), "cell2": Obj("PropertyCellObject", value=src)}
        scenarios.append(scenario(roots, [Root("cell"), Root("cell2")], [], [], label))
    return icon_case(env, 0x72CE60, 0x72CE9F, "thiscall", [PTR, PTR], PTR, "bc_IconTextureArray_AssignElement", scenarios)


@registered_once("icon.array.TGIconTextureArray_destructor_body")
def array_destructor_test(env):
    scenarios = [array_scenario([ARRAY], n, "%d elements" % n) for n in ARRAY_SIZES]
    scenarios.append(scenario(holder_roots(3), [ARRAY], filled(3) + [Pre("arrstore", [ARRAY, 1, 0])], [], "a gap"))
    return icon_case(env, 0x72CEA0, 0x72CECF, "thiscall", [PTR], None, "bc_IconTextureArray_DestructorBody", scenarios)


@registered_once("icon.array.TGIconTextureArray_deleting_destructor")
def array_deleting_destructor_test(env):
    scenarios = [array_scenario([ARRAY, 0], n, "%d elements, flags 0" % n) for n in ARRAY_SIZES]
    empty = Obj("IconTextureArray", data=None, capacity=0, used=0, count=0, growBy=1)
    scenarios.append(scenario({"a": empty}, [Root("a"), 1], [], [], "an array without a block, flags 1"))
    scenarios.append(scenario({"a": Obj("IconTextureArray", data=None, capacity=0, used=0, count=0, growBy=1)},
                              [Root("a"), 0], [], [], "an array without a block, flags 0"))
    return icon_case(env, 0x72AF90, 0x72AFDF, "thiscall", [PTR, U32], PTR, "bc_IconTextureArray_DeletingDestructor",
                     scenarios)


# The location table -----------------------------------------------------------------------------------------------------------

def table_scenario(args, label, numbers=(1, 38, 75), extra_roots=None):
    roots = {"g": Obj("IconGroup"), "n": Obj("IconLocationNode"), "loc": Obj("IconLocation", c0=1.0)}
    roots.update(extra_roots or {})
    pre = build_group("g", "G", ["Data/a.tga"], [(n, 0, 1.0, 2.0, 3.0, 4.0, 0, 0) for n in numbers])
    return scenario(roots, args, pre, [manager_global(scale=1.0, width=640.0, height=480.0)], label,
                    {"Data/a.tga": (64, 32)})


KEYS = (0, 1, 36, 37, 38, 74, 75, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 12345678)


@registered_once("icon.table.TGIconLocationTable_Hash")
def table_hash_test(env):
    scenarios = [table_scenario([TABLE, key], "key %d" % key) for key in KEYS]
    return icon_case(env, 0x72AE80, 0x72AE8F, "thiscall", [PTR, U32], U32, "bc_IconLocationTable_Hash", scenarios)


@registered_once("icon.table.TGIconLocationTable_KeysEqual")
def table_keys_equal_test(env):
    scenarios = [table_scenario([TABLE, a, b], "keys %d and %d" % (a, b)) for a in (0, 1, 38, 0xFFFFFFFF) for b in (0, 1, 38, 0xFFFFFFFF)]
    return icon_case(env, 0x72AE90, 0x72AEAF, "thiscall", [PTR, U32, U32], U8, "bc_IconLocationTable_KeysEqual", scenarios)


@registered_once("icon.table.TGIconLocationTable_SetNode")
def table_set_node_test(env):
    scenarios = [table_scenario([TABLE, Root("n"), key, Root("loc")], "key %d" % key) for key in KEYS[:6]]
    scenarios.append(table_scenario([TABLE, Root("n"), 5, 0], "a null location"))
    return icon_case(env, 0x72AEB0, 0x72AECF, "thiscall", [PTR, PTR, U32, PTR], None, "bc_IconLocationTable_SetNode", scenarios)


@registered_once("icon.table.TGIconLocationTable_ClearNode")
def table_clear_node_test(env):
    roots = {"n": Obj("IconLocationNode", key=5, value=Obj("IconLocation", c0=2.0), next=None)}
    scenarios = [table_scenario([TABLE, Root("n")], "a node with a location", extra_roots=roots)]
    return icon_case(env, 0x72AED0, 0x72AEDF, "thiscall", [PTR, PTR], None, "bc_IconLocationTable_ClearNode", scenarios)


@registered_once("icon.table.TGIconLocationTable_deleting_destructor")
def table_destructor_test(env):
    scenarios = [table_scenario([TABLE, 0], "numbers %s, flags 0" % (numbers,), numbers)
                 for numbers in ((), (5,), (1, 38, 75), (1, 2, 3, 4, 5, 6, 7), (0, 37, 74, 36))]
    empty = Obj("IconLocationTable", count=0, bucketCount=0x25, buckets=[None] * 0x25)
    scenarios.append(scenario({"t": empty}, [Root("t"), 1], [], [], "an empty table, flags 1"))
    return icon_case(env, 0x72AF10, 0x72AF8F, "thiscall", [PTR, U32], PTR, "bc_IconLocationTable_DeletingDestructor", scenarios,
                     allow_leaks=True)  # the table does not free the locations: the group frees them in Reset


# The texture cache -------------------------------------------------------------------------------------------------------------

def colliding(text, count=2):
    found, number = [], 0
    while len(found) < count:
        candidate = "k%d" % number
        number += 1
        if candidate != text and cache_hash(candidate) == cache_hash(text):
            found.append(candidate)
    return found


def cache_obj(entries):
    """A cache with the entries (key, texture serial): later entries go in front of earlier ones in the same bucket."""
    buckets = [None] * 0x25
    textures = {}
    uses = {}
    for _, serial in entries:
        uses[serial] = uses.get(serial, 0) + 1
    for key, serial in entries:
        tex = textures.setdefault(serial, texture(serial, 16, 16, count=uses[serial]))
        index = cache_hash(key)
        buckets[index] = Obj("TextureCacheNode", key=key, value=tex, next=buckets[index])
    return Obj("TextureCache", count=len(entries), bucketCount=0x25, buckets=buckets), textures


CACHE_STATES = [("an empty cache", []), ("one entry", [("Data/a.tga", 1)]),
                ("three entries", [("Data/a.tga", 1), ("Data/b.tga", 2), ("x", 1)]),
                ("a bucket with a chain", [("k1", 1)] + [(k, 2 + i) for i, k in enumerate(colliding("k1", 2))]),
                ("an entry without a texture", [("missing", 0)])]


def cache_scenario(args, entries, label, extra=None):
    cache, textures = cache_obj([(k, s) for k, s in entries if s])
    nodeless = [(k, s) for k, s in entries if not s]
    for key, _ in nodeless:
        index = cache_hash(key)
        cache.values["buckets"][index] = Obj("TextureCacheNode", key=key, value=None, next=cache.values["buckets"][index])
    cache.values["count"] = len(entries)
    roots = {"n": Obj("TextureCacheNode"), "t1": texture(40, 4, 4, count=1), "t2": texture(41, 8, 8, count=2),
             "old": Obj("TextureCacheNode", key="old key", value=texture(42, 2, 2, count=1))}
    roots.update(textures)
    roots.update(extra or {})
    return scenario(roots, args, [], [manager_global(cache=cache)], label)


CACHE_KEYS = ("", "a", "Data/a.tga", "k1", "k37", "caf\xe9", "\xff\xfe", "x" * 60)


@registered_once("icon.cache.TGIconTextureCache_Hash")
def cache_hash_test(env):
    scenarios = [cache_scenario([CACHE, narrow(key)], [], "key %r" % key[:10]) for key in CACHE_KEYS]
    return icon_case(env, 0x44EDB0, 0x44EDDF, "thiscall", [PTR, Buf(io="in")], U32, "bc_TextureCache_Hash", scenarios)


@registered_once("icon.cache.TGIconTextureCache_KeysEqual")
def cache_keys_equal_test(env):
    pairs = [("a", "a"), ("a", "b"), ("", ""), ("a", ""), ("ab", "a"), ("Data/a.tga", "Data/a.tga"), ("Data/a.tga", "data/a.tga"),
             ("caf\xe9", "caf\xe9"), ("caf\xe9", "cafe"), ("\xff", "\x7f")]
    scenarios = [cache_scenario([CACHE, narrow(a), narrow(b)], [], "%r and %r" % (a, b)) for a, b in pairs]
    return icon_case(env, 0x44EDE0, 0x44EE3F, "thiscall", [PTR, Buf(io="in"), Buf(io="in")], U8, "bc_TextureCache_KeysEqual",
                     scenarios)


@registered_once("icon.cache.TGIconTextureCache_SetNode")
def cache_set_node_test(env):
    scenarios = []
    for key in ("Data/a.tga", "", "x" * 50):
        scenarios.append(cache_scenario([CACHE, Root("n"), narrow(key), Root("t1")], [], "a new node, key %r" % key[:6]))
        scenarios.append(cache_scenario([CACHE, Root("old"), narrow(key), Root("t1")], [], "a node that has a texture, key %r" % key[:6]))
    scenarios.append(cache_scenario([CACHE, Root("n"), narrow("k"), 0], [], "a null texture"))
    scenarios.append(cache_scenario([CACHE, Root("old"), narrow("k"), 0], [], "a null texture on a node that has one"))
    return icon_case(env, 0x44EE40, 0x44EF0F, "thiscall", [PTR, PTR, Buf(io="in"), PTR], None, "bc_TextureCache_SetNode", scenarios,
                     allow_leaks=True)  # SetNode does not free the old key: ClearNode does


@registered_once("icon.cache.TGIconTextureCache_ClearNode")
def cache_clear_node_test(env):
    scenarios = [cache_scenario([CACHE, Root("old")], [], "a node with a key"),
                 cache_scenario([CACHE, Root("n")], [], "a node without a key")]
    return icon_case(env, 0x44EF10, 0x44EF1F, "thiscall", [PTR, PTR], None, "bc_TextureCache_ClearNode", scenarios)


@registered_once("icon.cache.TGIconTextureCache_Insert")
def cache_insert_test(env):
    scenarios = []
    for label, entries in CACHE_STATES:
        for key in ("Data/a.tga", "Data/new.tga", "k1", "k37", "missing", colliding("k1", 1)[0]):
            scenarios.append(cache_scenario([CACHE, narrow(key), Root("t1")], entries, "%s, key %r" % (label, key)))
        scenarios.append(cache_scenario([CACHE, narrow("Data/new.tga"), 0], entries, "%s, a null texture" % label))
    scenarios.append(cache_scenario([CACHE, narrow("Data/a.tga"), Root("t1")], [("Data/a.tga", 40)],
                                    "the same texture again", extra={}))
    return icon_case(env, 0x44EBD0, 0x44ECDF, "thiscall", [PTR, Buf(io="in"), PTR], None, "bc_TextureCache_Insert", scenarios)


@registered_once("icon.cache.TGIconTextureCache_destructor")
def cache_destructor_test(env):
    scenarios = [cache_scenario([CACHE, 0], entries, label) for label, entries in CACHE_STATES]
    scenarios.append(cache_scenario([CACHE, 0], [("a", 1), ("b", 1), ("c", 1), ("d", 2)], "four entries, two textures"))
    return icon_case(env, 0x44F030, 0x44F04F, "thiscall", [PTR, U32], PTR, "bc_TextureCache_DeletingDestructor", scenarios)


# The registration record ----------------------------------------------------------------------------------------------------

@registered_once("icon.registration.TGIconRegistration_constructor")
def registration_constructor_test(env):
    texts = [("ShipIcons", "Icons.ShipIcons", "LoadShipIcons"), ("", "", ""), ("a", "b", "c"), ("x" * 40, "y" * 5, "z" * 100)]
    scenarios = [scenario({"r": Obj("IconRegistration")}, [Root("r"), narrow(a), narrow(b), narrow(c)], [], [], "%r" % (a[:5],))
                 for a, b, c in texts]
    return icon_case(env, 0x747990, 0x747A5F, "thiscall", [PTR, Buf(io="in"), Buf(io="in"), Buf(io="in")], PTR,
                     "bc_TGIconRegistration_TGIconRegistration", scenarios)


@registered_once("icon.registration.TGIconRegistration_destructor")
def registration_destructor_test(env):
    scenarios = []
    for a, b, c in (("ShipIcons", "Icons.ShipIcons", "LoadShipIcons"), ("", "", "")):
        roots = {"r": Obj("IconRegistration", groupName=a, moduleName=b, functionName=c)}
        scenarios.append(scenario(roots, [Root("r")], [], [], "%r" % (a[:5],)))
    scenarios.append(scenario({"r": Obj("IconRegistration", groupName="a")}, [Root("r")], [], [], "the module and the function are null"))
    scenarios.append(scenario({"r": Obj("IconRegistration")}, [Root("r")], [], [], "all texts are null"))
    return icon_case(env, 0x747A60, 0x747A9F, "thiscall", [PTR], None, "bc_TGIconRegistration_Destructor", scenarios)


# The list code of the group list ---------------------------------------------------------------------------------------------

def group_list(count, free=0, blocks=0, block_size=2):
    nodes = [Obj("IconGroupNode", data=Obj("IconGroup", name="g%d" % i)) for i in range(count)]
    for i, node in enumerate(nodes):
        node.values["next"] = nodes[i + 1] if i + 1 < count else None
        node.values["prev"] = nodes[i - 1] if i > 0 else None
    free_nodes = [Obj("IconGroupNode", prev=nodes[0] if nodes else None) for _ in range(free)]
    for i, node in enumerate(free_nodes):
        node.values["next"] = free_nodes[i + 1] if i + 1 < free else None
    chain = None
    for _ in range(blocks):
        chain = Obj("IconGroupBlock", nextBlock=chain, nodes=free_nodes[0] if free_nodes else None)
    return Obj("IconGroupList", count=count, head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
               freeNodes=free_nodes[0] if free_nodes else None, blocks=chain, blockSize=block_size), nodes


@registered_once("icon.list.TGIconManager_AllocGroupNode")
def alloc_group_node_test(env):
    scenarios = []
    for count in (0, 2):
        for free in (0, 1, 2):
            for blocks in (0, 2):
                for size in (1, 2, 5):
                    groups, _ = group_list(count, free, blocks, size)
                    scenarios.append(scenario({"cell": Obj("NodeCell")}, [Inner("m", "groups"), Root("cell")], [],
                                              [manager_global(groups=groups)],
                                              "%d nodes, %d free, %d blocks, block size %d" % (count, free, blocks, size)))
    return icon_case(env, 0x72AFF0, 0x72B0AF, "thiscall", [PTR, PTR], None, "bc_TGList_AllocNode", scenarios)


@registered_once("icon.list.TGIconManager_FreeGroupNode")
def free_group_node_test(env):
    scenarios = []
    for count in (1, 3):
        for free in (0, 1, 2):
            groups, nodes = group_list(count, free)
            roots = {"n": nodes[-1]}
            scenarios.append(scenario(roots, [Inner("m", "groups"), Root("n")], [], [manager_global(groups=groups)],
                                      "%d nodes, %d free" % (count, free)))
    return icon_case(env, 0x72B0B0, 0x72B0CF, "thiscall", [PTR, PTR], None, "bc_TGList_FreeNode", scenarios, allow_leaks=True)
