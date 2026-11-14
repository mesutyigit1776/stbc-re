"""Tests of the Appc wrappers of the icon classes that have engine code of their own: the wrappers read and store the
screen size, the scale and the pixel center adjustment inline, replace the camera inline, delete a group through its
vtable, read the name and the count inline, and build their Python results (see icon_wrapper_common.py).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import ICON_ENVS, build_group, camera, manager_global, registered_once  # noqa: E402
from icon_wrapper_common import (VALUES, double_probe, group_scenario, integer_probe, manager_scenario,  # noqa: E402
                                 pointer_probe, text_probe, wrapper_case, wrapper_setup)

def accessor_case(first, last, symbol, member, setter):
    scenarios = []
    for value in VALUES:
        if setter:
            scenarios.append(manager_scenario([value], "set %r" % value, **{member: 7.5}))
        else:
            scenarios.append(manager_scenario([], "get %r" % value, **{member: value}))
    probes = [] if setter else [double_probe()]
    return wrapper_case(ICON_ENVS[53], first, last, symbol, scenarios, [F32] if setter else [], probes)


@registered_once("icon.wrappers.Appc_TGIconManager_GetScreenWidth")
def get_screen_width(env):
    return accessor_case(0x5D5BE0, 0x5D5C4F, "bc_Appc_TGIconManager_GetScreenWidth", "width", False)


@registered_once("icon.wrappers.Appc_TGIconManager_GetScreenHeight")
def get_screen_height(env):
    return accessor_case(0x5D5C50, 0x5D5CBF, "bc_Appc_TGIconManager_GetScreenHeight", "height", False)


@registered_once("icon.wrappers.Appc_TGIconManager_GetScale")
def get_scale(env):
    return accessor_case(0x5D5EB0, 0x5D5F1F, "bc_Appc_TGIconManager_GetScale", "scale", False)


@registered_once("icon.wrappers.Appc_TGIconManager_GetPixelCenterAdj")
def get_adjustment(env):
    return accessor_case(0x5D5FA0, 0x5D600F, "bc_Appc_TGIconManager_GetPixelCenterAdj", "adjustment", False)


@registered_once("icon.wrappers.Appc_TGIconManager_SetScreenWidth")
def set_screen_width(env):
    return accessor_case(0x5D5CC0, 0x5D5D3F, "bc_Appc_TGIconManager_SetScreenWidth", "width", True)


@registered_once("icon.wrappers.Appc_TGIconManager_SetScreenHeight")
def set_screen_height(env):
    return accessor_case(0x5D5D40, 0x5D5DBF, "bc_Appc_TGIconManager_SetScreenHeight", "height", True)


@registered_once("icon.wrappers.Appc_TGIconManager_SetScale")
def set_scale(env):
    return accessor_case(0x5D5E30, 0x5D5EAF, "bc_Appc_TGIconManager_SetScale", "scale", True)


@registered_once("icon.wrappers.Appc_TGIconManager_SetCamera")
def set_camera(env):
    scenarios = []
    for old_count, same in ((None, False), (1, False), (2, False), (1, True), (3, True)):
        for new in (True, False):
            old = None if old_count is None else camera(6, count=old_count)
            fresh = None
            if new:
                fresh = old if same and old is not None else camera(5, count=1)
            roots = {"old": old} if old is not None else {}
            if fresh is not None:
                roots["fresh"] = fresh
            scenarios.append(manager_scenario([], "old count %s, same %s, new %s" % (old_count, same, new), fresh, roots,
                                              camera=old))
    return wrapper_case(env, 0x5D6160, 0x5D623F, "bc_Appc_TGIconManager_SetCamera", scenarios)


@registered_once("icon.wrappers.Appc_TGIconManager_GetCamera")
def get_camera(env):
    scenarios = []
    for count in (None, 1, 2):
        current = None if count is None else camera(6, count=count)
        roots = {"c": current} if current is not None else {}
        scenarios.append(manager_scenario([], "count %s" % count, None, roots, camera=current))
    return wrapper_case(env, 0x5D60D0, 0x5D615F, "bc_Appc_TGIconManager_GetCamera", scenarios,
                        probes=[pointer_probe("c")], allow_leaks=True)


@registered_once("icon.wrappers.Appc_delete_TGIconGroup")
def delete_group(env):
    scenarios = [group_scenario(build_group("g", "G"), "an empty group"),
                 group_scenario(build_group("g", "G", ["Data/a.tga"], [(1, 0, 1.0, 1.0, 1.0, 1.0, 0, 0)]), "a group with an icon")]
    return wrapper_case(env, 0x5D4FF0, 0x5D505F, "bc_Appc_TGIconGroup_delete", scenarios)


@registered_once("icon.wrappers.Appc_delete_TGIconGroup_with_a_null_group")
def delete_null_group(env):
    manager = manager_global()
    arguments = Obj("GroupArgs", g=None)
    scenarios = [Scenario({"a": arguments}, [0, Root("a")], globals=[manager], label="null", setup=wrapper_setup())]
    return wrapper_case(env, 0x5D4FF0, 0x5D505F, "bc_Appc_TGIconGroup_delete", scenarios)


@registered_once("icon.wrappers.Appc_TGIconGroup_GetName")
def get_name(env):
    scenarios = [group_scenario(build_group("g", name), "name %r" % (name,)) for name in ("ShipIcons", "", "A", "x" * 100, None)]
    return wrapper_case(env, 0x5D5190, 0x5D51FF, "bc_Appc_TGIconGroup_GetName", scenarios, probes=[text_probe()])


@registered_once("icon.wrappers.Appc_TGIconGroup_GetCount")
def get_count(env):
    scenarios = []
    for numbers in ((), (1,), (1, 38, 75), tuple(range(1, 20))):
        pre = build_group("g", "G", ["Data/a.tga"], [(n, 0, 1.0, 1.0, 1.0, 1.0, 0, 0) for n in numbers])
        scenarios.append(group_scenario(pre, "%d icons" % len(numbers)))
    return wrapper_case(env, 0x5D5700, 0x5D575F, "bc_Appc_TGIconGroup_GetCount", scenarios, probes=[integer_probe()])


@registered_once("icon.wrappers.Appc_TGIconGroup_GetPyWrapper")
def get_py_wrapper(env):
    scenarios = [group_scenario(build_group("g", "G"), "a group")]
    return wrapper_case(env, 0x5D55A0, 0x5D560F, "bc_Appc_TGIconGroup_GetPyWrapper", scenarios, probes=[pointer_probe()])
