"""Tests of TGIconManager (0x729b30 and the functions around it): constructor, destructor, the static initializer, the
registration of groups, the groups in the list, the camera and the screen size.

The manager is the global at 0x99d1f0; each scenario describes all its members. A group comes from the constructor of the
exe and goes into the list with AddIconGroup. The calls into the renderer and the script layer are stubs; the native test
functions write the same log lines, and the case compares the log.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import (ICON_ENVS, atexit_probe, atexit_stubs, build_group, camera, empty_cache, empty_list, holder,  # noqa: E402
                         icon_case, manager_global, manager_obj, registered_once, registered_pair, registration, scenario,
                         texture)
from emuharness import Inner  # noqa: E402

FILES = {"Data/a.tga": (64, 32), "Data/b.tga": (100, 100)}
REGS = [registration("A", "modA", "fnA"), registration("B", "modB", "fnB"), registration("C", "modC", "fnC")]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def regs(*names):
    return [registration(n, "mod" + n, "fn" + n) for n in names]


def loaded(names, contents=False, locks=None):
    """The roots and setup calls for groups that are in the list of the manager, in this order."""
    roots, pre = {}, []
    for i, name in enumerate(names):
        root = "g%d" % i
        roots[root] = Obj("IconGroup")
        if contents:
            pre += build_group(root, name, ["Data/a.tga", "Data/b.tga"], [(1, 0, 1.0, 2.0, 3.0, 4.0, 0, 0), (2, 1, 5.0, 6.0, 7.0, 8.0, 1, 1)],
                               lock=(locks or {}).get(i))
        else:
            pre += build_group(root, name, lock=(locks or {}).get(i))
        pre.append(Pre("addgroup", [Root("m"), Root(root)]))
    return roots, pre


def mscenario(args, label, names=(), registrations=(), contents=False, locks=None, roots=None, glob=None, capacity=None,
              extra_pre=(), **members):
    group_roots, pre = loaded(names, contents, locks)
    all_roots = dict(group_roots)
    all_roots.update(roots or {})
    manager = glob if glob is not None else manager_global(registrations=registrations, capacity=capacity, **members)
    return scenario(all_roots, args, pre + list(extra_pre), [manager], label, FILES)


# Constructor, destructor and the static initializer ---------------------------------------------------------------------------

@registered_once("icon.manager.TGIconManager_constructor")
def constructor_test(env):
    scenarios = [scenario({"m": Obj("IconManager")}, [Root("m")], [], [], "a blank object")]
    return icon_case(env, 0x729B30, 0x729C3F, "thiscall", [PTR], PTR, "bc_TGIconManager_TGIconManager", scenarios)


@registered_once("icon.manager.TGIconManager_destructor_body")
def destructor_body_test(env):
    tex = texture(60, 8, 8, count=1)
    states = [
        ("a new manager", mscenario([Root("m")], "")),
        ("registrations", mscenario([Root("m")], "", registrations=REGS)),
        ("groups in the list", mscenario([Root("m")], "", names=("A", "B", "C"), contents=True)),
        ("a camera that only the manager holds", mscenario([Root("m")], "", camera=camera(7, count=1))),
        ("a camera that another owner holds too", mscenario([Root("m")], "", camera=camera(8, count=2))),
        ("a camera without a drawing object", mscenario([Root("m")], "", camera=camera(9, count=1, drawing=False))),
        ("a cache with entries", mscenario([Root("m")], "", cache=Obj("TextureCache", count=1, bucketCount=0x25,
                                                                     buckets=[Obj("TextureCacheNode", key="Data/a.tga", value=tex)] + [None] * 0x24))),
    ]
    scenarios = []
    for label, item in states:
        item.label = label
        scenarios.append(item)
    return icon_case(env, 0x729A00, 0x729B2F, "thiscall", [PTR], None, "bc_TGIconManager_DestructorBody", scenarios,
                     allow_leaks=True)  # the destructor does not free the groups or the registrations


def blank_global():
    return Global("m", MANAGER, Obj("IconManager"), "bc_init_global_IconManager")


@registered_once("icon.manager.static_initializer_0x7299c0")
def static_init_test(env):
    scenarios = [scenario({}, [], [], [blank_global()], "the global is blank")]
    return icon_case(env, 0x7299C0, 0x7299CF, "cdecl", [], None, "bc_TGIconManager_InitGlobal", scenarios,
                     stubs=atexit_stubs(), probes=[log_probe(), trap_probe(), atexit_probe()])


@registered_once("icon.manager.TGIconManager_construct_thunk_0x7299d0")
def construct_thunk_test(env):
    scenarios = [scenario({}, [], [], [blank_global()], "the global is blank")]
    return icon_case(env, 0x7299D0, 0x7299D9, "cdecl", [], None, "bc_TGIconManager_ConstructGlobal", scenarios)


@registered_once("icon.manager.TGIconManager_register_cleanup_0x7299e0")
def register_cleanup_test(env):
    scenarios = [scenario({}, [], [], [], "no objects")]
    return icon_case(env, 0x7299E0, 0x7299EF, "cdecl", [], None, "bc_TGIconManager_RegisterGlobalCleanup", scenarios,
                     stubs=atexit_stubs(), probes=[log_probe(), trap_probe(), atexit_probe()])


@registered_once("icon.manager.TGIconManager_destruct_thunk_0x7299f0")
def destruct_thunk_test(env):
    scenarios = [mscenario([], "registrations and groups", names=("A", "B"), contents=True, registrations=REGS,
                           camera=camera(7, count=1))]
    return icon_case(env, 0x7299F0, 0x7299FF, "cdecl", [], None, "bc_TGIconManager_DestructGlobal", scenarios,
                     allow_leaks=True)


# Registration -------------------------------------------------------------------------------------------------------------------

@registered_once("icon.manager.TGIconManager_RegisterIconGroup")
def register_test(env):
    scenarios = []
    for count in (0, 1, 2, 3):
        for spare in (0, 2):
            records = REGS[:count]
            capacity = max(1, count) + spare
            for text in (("D", "modD", "fnD"), ("", "", ""), ("x" * 30, "y", "z" * 70)):
                scenarios.append(mscenario([Root("m")] + [narrow(t) for t in text], "%d records, room for %d, %r" % (count, capacity, text[0][:5]),
                                           registrations=records, capacity=capacity))
    return icon_case(env, 0x729E70, 0x729F9F, "thiscall", [PTR, Buf(io="in"), Buf(io="in"), Buf(io="in")], None,
                     "bc_TGIconManager_RegisterIconGroup", scenarios)


# The groups -----------------------------------------------------------------------------------------------------------------------

@registered_once("icon.manager.TGIconManager_CreateIconGroup")
def create_test(env):
    scenarios = [mscenario([Root("m"), narrow(name)], "name %r" % name[:6]) for name in ("ShipIcons", "", "A", "z" * 50)]
    return icon_case(env, 0x729FA0, 0x729FFF, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGIconManager_CreateIconGroup",
                     scenarios, ret_class="IconGroup")


def add_scenarios():
    scenarios = []
    for existing in ((), ("A",), ("A", "B", "C"), ("B",)):
        roots = {"n": Obj("IconGroup")}
        for name in ("A", "N", ""):
            pre = build_group("n", name)
            group_roots, loaded_pre = loaded(existing)
            all_roots = dict(group_roots)
            all_roots.update(roots)
            scenarios.append(scenario(all_roots, [Root("m"), Root("n")], loaded_pre + pre, [manager_global()],
                                      "the list %s, a new group %r" % (existing, name), FILES))
    # a list that has free nodes and a block record
    group_roots, pre = loaded(("A", "B", "C"))
    scenarios.append(scenario(dict(group_roots, n=Obj("IconGroup")), [Root("m"), Root("n")],
                              pre + [Pre("purge", [Root("m")])] + build_group("n", "Z"), [manager_global()],
                              "a list that was purged", FILES))
    return scenarios


@registered_once("icon.manager.TGIconManager_AddIconGroup")
def add_test(env):
    return icon_case(env, 0x72A000, 0x72A07F, "thiscall", [PTR, PTR], None, "bc_TGIconManager_AddIconGroup",
                     add_scenarios(), allow_leaks=True)  # B0025: a group with a loaded name is not freed


@registered_once("icon.manager.TGIconManager_FindLoadedGroup")
def find_loaded_test(env):
    scenarios = []
    for names in ((), ("A",), ("A", "B", "C"), ("", "x")):
        for name in ("A", "B", "C", "D", "", "x"):
            scenarios.append(mscenario([Root("m"), narrow(name)], "the list %s, name %r" % (names, name), names=names))
    return icon_case(env, 0x72A400, 0x72A46F, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGIconManager_FindLoadedGroup",
                     scenarios, ret_class="IconGroup")


@registered_once("icon.manager.TGIconManager_GetIconGroup")
def get_test(env):
    scenarios = []
    cases = [((), ()), (("A", "B", "C"), ("B",)), (("A", "B", "C"), ()), (("A",), ("X",)), (("A", "B"), ("A", "B")),
             (("A", "A"), ("A",)), (("A", "B", "C"), ("C", "B", "A"))]
    for registered, names in cases:
        for name in ("A", "B", "C", "D", "X", ""):
            scenarios.append(mscenario([Root("m"), narrow(name)], "registered %s, loaded %s, name %r" % (registered, names, name),
                                       names=names, registrations=regs(*registered)))
    return icon_case(env, 0x72A150, 0x72A20F, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGIconManager_GetIconGroup",
                     scenarios, ret_class="IconGroup")


@registered_once("icon.manager.TGIconManager_DeleteIconGroup")
def delete_test(env):
    scenarios = []
    cases = [(("A", "B", "C"), ("A", "B", "C")), (("A",), ("A",)), (("A", "B", "C"), ("B",)), (("A", "B"), ()),
             (("A", "A"), ("A",)), ((), ("A",)), (("A", "B", "C", "D"), ("D", "C", "B", "A")), (("A", "B", "C"), ("A", "C"))]
    for registered, names in cases:
        for name in ("A", "B", "C", "D", "Z"):
            scenarios.append(mscenario([Root("m"), narrow(name)], "registered %s, loaded %s, delete %r" % (registered, names, name),
                                       names=names, registrations=regs(*registered), contents=True))
    return icon_case(env, 0x72A210, 0x72A3FF, "thiscall", [PTR, Buf(io="in")], U8, "bc_TGIconManager_DeleteIconGroup",
                     scenarios)


@registered_once("icon.manager.TGIconManager_Purge")
def purge_test(env):
    scenarios = []
    lock_sets = [{}, {0: 1}, {1: 1}, {2: 5}, {0: 1, 2: 1}, {0: 1, 1: 1, 2: 1}, {1: -1}]
    for locks in lock_sets:
        for names in (("A",), ("A", "B", "C"), ("A", "B", "C", "D")):
            scenarios.append(mscenario([Root("m")], "locks %s, groups %s" % (locks, names), names=names, contents=True, locks=locks))
    scenarios.append(mscenario([Root("m")], "no groups"))
    return icon_case(env, 0x72A080, 0x72A14F, "thiscall", [PTR], None, "bc_TGIconManager_Purge", scenarios)


# Screen size, camera and drawing ---------------------------------------------------------------------------------------------

def adjustments():
    random.seed(77)
    return [-0.5, 0.0, 0.5, 1.0, -3.25, 1.0e10, 3.0e38, 1.0e-40] + [f32(random.uniform(-100, 100)) for _ in range(6)]


SCREENS = [(640.0, 480.0), (0.0, 0.0), (1280.0, 720.0), (1.0e-30, 5.0), (3.0e38, 3.0e38), (1920.0, 1080.0), (f32(0.1), f32(0.3)),
           (16777217.0, 3.0)]


@registered_pair("icon.manager.TGIconManager_SetPixelCenterAdj")
def set_adjustment_test(env):
    scenarios = []
    for width, height in SCREENS:
        for adjustment in adjustments():
            if adjustment == 0.0 and (width == 0.0 or height == 0.0):
                continue  # 0 / 0 gives a NaN that the x87 unit and the host sign in a different way
            scenarios.append(mscenario([Root("m"), adjustment], "screen %s, adjustment %r" % ((width, height), adjustment),
                                       width=width, height=height))
    return icon_case(env, 0x72A920, 0x72A93F, "thiscall", [PTR, F32], None, "bc_TGIconManager_SetPixelCenterAdj", scenarios)


@registered_pair("icon.manager.TGIconManager_SetDisplayInfo")
def set_display_info_test(env):
    scenarios = []
    sizes = [(640, 480), (0, 0), (1920, 1080), (0xFFFFFFFF, 0xFFFFFFFF), (16777217, 16777219), (1, 0x80000000)]
    for width, height in sizes:
        for scale in (1.0, 0.0, -1.5, 3.0e38):
            for adjustment in (-0.5, 0.25):
                roots = {"c": camera(5, width, height, count=1)}
                scenarios.append(mscenario([Root("m"), Root("c"), scale], "camera %s, scale %r, adjustment %r" % ((width, height), scale, adjustment),
                                           roots=roots, adjustment=adjustment, width=3.0, height=4.0))
    base = dict(adjustment=-0.5, width=3.0, height=4.0)
    scenarios.append(mscenario([Root("m"), 0, 2.0], "no camera", **base))
    scenarios.append(mscenario([Root("m"), 0, 2.0], "no camera, a camera in the manager", camera=camera(6, count=1), **base))
    current = camera(6, 800, 600, count=2)
    scenarios.append(mscenario([Root("m"), Root("c"), 1.0], "the camera of the manager again", roots={"c": current}, camera=current, **base))
    old = camera(6, 800, 600, count=2)
    scenarios.append(mscenario([Root("m"), Root("c"), 1.0], "another camera, the old one is held elsewhere",
                               roots={"c": camera(5, 640, 480, count=1), "old": old}, camera=old, **base))
    old = camera(6, 800, 600, count=1)
    scenarios.append(mscenario([Root("m"), Root("c"), 1.0], "another camera, the old one is deleted",
                               roots={"c": camera(5, 640, 480, count=1), "old": old}, camera=old, **base))
    return icon_case(env, 0x729DC0, 0x729E6F, "thiscall", [PTR, PTR, F32], None, "bc_TGIconManager_SetDisplayInfo", scenarios)


@registered_once("icon.manager.TGIconManager_GetCameraPtr")
def get_camera_ptr_test(env):
    scenarios = [mscenario([Root("m"), Root("cell")], "no camera", roots={"cell": Obj("CameraCellObject")}),
                 mscenario([Root("m"), Root("cell")], "a camera", roots={"cell": Obj("CameraCellObject")}, camera=camera(4, count=1)),
                 mscenario([Root("m"), Root("cell")], "a camera held twice", roots={"cell": Obj("CameraCellObject")},
                           camera=camera(4, count=2))]
    return icon_case(env, 0x42E6C0, 0x42E6DF, "thiscall", [PTR, PTR], PTR, "bc_TGIconManager_GetCameraPtr", scenarios)


@registered_once("icon.manager.TGIconManager_Draw")
def draw_test(env):
    scenarios = [mscenario([Root("m")], "no camera"), mscenario([Root("m")], "a camera without a drawing object",
                                                                camera=camera(4, count=1, drawing=False)),
                 mscenario([Root("m")], "a camera with a drawing object", camera=camera(4, count=1))]
    return icon_case(env, 0x72A690, 0x72A6DF, "thiscall", [PTR], None, "bc_TGIconManager_Draw", scenarios)


@registered_once("icon.manager.TGIconManager_GetTextureCache")
def get_texture_cache_test(env):
    scenarios = [mscenario([Root("m")], "a manager")]
    return icon_case(env, 0x72C250, 0x72C25F, "thiscall", [PTR], PTR, "bc_TGIconManager_GetTextureCache", scenarios,
                     ret_class="TextureCache")
