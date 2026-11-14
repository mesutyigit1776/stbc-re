"""Tests of TGIconGroup (0x72bb20 and the functions around it): constructor, destructor, Reset, SetName, the texture
load, SetIconLocation with rotation and mirror, the size functions and the walk over the icon numbers.

A group comes from the constructor of the exe on both sides. The textures are made by stubs (0x809fe0 and 0x7e0c50) and
by native test functions; both write a log that the case compares. The size and corner functions run at both x87
precisions.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import (ICON_ENVS, build_group, icon_case, iterator_probe, manager_global, registered_once,  # noqa: E402
                         registered_pair, scenario)
from prop_common import failing_text  # noqa: E402

WORKDIR = "C:\\BC\\"
NAMES = ("ShipIcons", "", "A", "LCARS 640", "x" * 40)
FILES = {WORKDIR + "Data/a.tga": (64, 32), WORKDIR + "Data/b.tga": (100, 100), WORKDIR + "Data/c.tga": (1024, 768),
         WORKDIR + "Data/d.tga": (7, 3), WORKDIR + "Data/e.tga": (0, 0), "C:\\Elsewhere\\f.tga": (256, 128)}


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def group_roots(*names):
    return {name: Obj("IconGroup") for name in names}


def manager(**members):
    return manager_global(**members)


def gscenario(args, pre, label, roots=None, textures=None, glob=None, **kw):
    roots = roots if roots is not None else group_roots("g")
    return scenario(roots, args, pre, glob if glob is not None else [manager(scale=1.0, width=640.0, height=480.0)],
                    label, textures if textures is not None else FILES, WORKDIR, **kw)


# Constructor ---------------------------------------------------------------------------------------------------------------

@registered_once("icon.group.TGIconGroup_constructor")
def constructor_test(env):
    scenarios = [scenario(group_roots("g"), [Root("g"), narrow(name)], label="name %r" % name[:9]) for name in NAMES]
    return icon_case(env, 0x72BB20, 0x72BCDF, "thiscall", [PTR, Buf(io="in")], PTR, "bc_TGIconGroup_TGIconGroup",
                     scenarios)


@registered_once("icon.group.TGIconGroup_constructor_with_a_null_name")
def constructor_null_test(env):
    scenarios = [scenario(group_roots("g"), [Root("g"), 0], label="null name")]
    return icon_case(env, 0x72BB20, 0x72BCDF, "thiscall", [PTR, PTR], PTR, "bc_TGIconGroup_TGIconGroup", scenarios)


# SetName -------------------------------------------------------------------------------------------------------------------

@registered_once("icon.group.TGIconGroup_SetName")
def set_name_test(env):
    scenarios = []
    for first in ("ShipIcons", "", "A"):
        for second in ("ShipIcons", "Other", "", "y" * 70):
            scenarios.append(gscenario([Root("g"), narrow(second)], build_group("g", first), "%r then %r" % (first, second[:6])))
    return icon_case(env, 0x72BF70, 0x72BFDF, "thiscall", [PTR, Buf(io="in")], None, "bc_TGIconGroup_SetName", scenarios)


@registered_once("icon.group.TGIconGroup_SetName_with_a_null_name")
def set_name_null_test(env):
    scenarios = [gscenario([Root("g"), 0], build_group("g", first), "null after %r" % first) for first in ("A", "")]
    scenarios.append(gscenario([Root("g"), 0], build_group("g", None), "null after null"))
    return icon_case(env, 0x72BF70, 0x72BFDF, "thiscall", [PTR, PTR], None, "bc_TGIconGroup_SetName", scenarios)


# LoadIconTexture -----------------------------------------------------------------------------------------------------------

def load_scenarios():
    a, b, c = "Data/a.tga", "Data/b.tga", "Data/c.tga"
    cases = [
        ("a new texture", build_group("g", "G"), a, 0),
        ("the second texture", build_group("g", "G", [a]), b, 0),
        ("the same file again", build_group("g", "G", [a]), a, 0),
        ("the same file a third time", build_group("g", "G", [a, a]), a, 1),
        ("a file with a drive", build_group("g", "G"), "C:\\Elsewhere\\f.tga", 1),
        ("a file that is missing", build_group("g", "G"), "Data/missing.tga", 0),
        ("a file that is missing, again", build_group("g", "G", ["Data/missing.tga"]), "Data/missing.tga", 0),
        ("a missing file between textures", build_group("g", "G", [a, "Data/missing.tga"]), b, 0),
        ("the flag 0x101", build_group("g", "G"), c, 0x101),
        ("the flag 0x100", build_group("g", "G"), c, 0x100),
        ("an empty name", build_group("g", "G"), "", 0),
        ("a short name", build_group("g", "G"), "x", 0),
        ("a texture of size 0", build_group("g", "G"), "Data/e.tga", 0),
        ("the colon in place 1", build_group("g", "G"), "Z:", 0),
        ("fifteen textures", build_group("g", "G", [a] * 15), b, 0),
        ("sixteen textures: the array grows", build_group("g", "G", [a, b, c] * 5 + [a]), c, 0),
        ("twenty-six textures: the array grows twice", build_group("g", "G", [a, b, c, a, b] * 5 + [a]), c, 0),
    ]
    scenarios = [gscenario([Root("g"), narrow(name), flag], pre, label) for label, pre, name, flag in cases]
    return scenarios


@registered_once("icon.group.TGIconGroup_LoadIconTexture")
def load_test(env):
    return icon_case(env, 0x72BFE0, 0x72C24F, "thiscall", [PTR, Buf(io="in"), U32], I32, "bc_TGIconGroup_LoadIconTexture",
                     load_scenarios())


@registered_once("icon.group.TGIconGroup_LoadIconTexture_in_a_second_group_uses_the_cache")
def load_second_group_test(env):
    a = "Data/a.tga"
    roots = group_roots("g", "h")
    pre = build_group("g", "G", [a, "Data/b.tga"]) + build_group("h", "H", [a])
    scenarios = [gscenario([Root("h"), narrow(a), 0], pre, "a texture of the first group", roots),
                 gscenario([Root("h"), narrow("Data/b.tga"), 0], pre, "the second texture of the first group", roots)]
    return icon_case(env, 0x72BFE0, 0x72C24F, "thiscall", [PTR, Buf(io="in"), U32], I32, "bc_TGIconGroup_LoadIconTexture",
                     scenarios)


# SetIconLocation -----------------------------------------------------------------------------------------------------------

def location_values():
    random.seed(20261017)
    values = [(0.0, 0.0, 8.0, 8.0), (10.0, 20.0, 30.0, 40.0), (0.5, 0.25, 0.125, 1.0), (-5.0, -7.5, 12.0, 3.0),
              (100.0, 200.0, 0.0, 0.0), (3.0, 4.0, 5.0, 6.0), (1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3), (1000.0, 1000.0, 50.0, 70.0),
              (16777216.0, 1.0, 1.0, 16777217.0), (3.0e38, 1.0, 3.0e38, 1.0), (1.0e-40, 2.0e-40, 3.0e-40, 4.0e-40)]
    for _ in range(14):
        values.append(tuple(f32(random.uniform(-2000.0, 2000.0)) for _ in range(4)))
    for _ in range(6):
        values.append(tuple(f32(random.uniform(0.0, 1.0) * 10 ** random.randint(-6, 6)) for _ in range(4)))
    return values


def set_location_scenarios():
    scenarios = []
    textures = ["Data/a.tga", "Data/b.tga", "Data/c.tga", "Data/d.tga"]
    for index, (x, y, w, h) in enumerate(location_values()):
        path = textures[index % len(textures)]
        scenarios.append(gscenario([Root("g"), 7 + index, 0, x, y, w, h, 0, 0], build_group("g", "G", [path]),
                                   "texture %s values %s" % (path[-5:], (x, y, w, h))))
    base = build_group("g", "G", ["Data/a.tga", "Data/b.tga"])
    for handle in (0, 1, -1):
        scenarios.append(gscenario([Root("g"), 3, handle, 4.0, 6.0, 20.0, 10.0, 0, 0], base, "handle %d" % handle))
    for rotation in (0, 1, 2, 3, 4, 5, -1, 0x10000):
        for mirror in (0, 1, 2, 3, -1):
            scenarios.append(gscenario([Root("g"), 12, 1, 4.0, 6.0, 20.0, 10.0, rotation, mirror], base,
                                       "rotation %d mirror %d" % (rotation, mirror)))
    for number in (0, 1, 37, 38, 74, -1, 0x7FFFFFFF, -0x80000000):
        scenarios.append(gscenario([Root("g"), number, 0, 1.5, 2.5, 3.5, 4.5, 0, 0], base, "number %d" % number))
    crowd = [(n, 0, float(n), float(n), 2.0, 2.0, 0, 0) for n in (1, 38, 75, 2, 39)]
    for number in (1, 38, 75, 112, 2):
        scenarios.append(gscenario([Root("g"), number, 0, 9.0, 8.0, 7.0, 6.0, 0, 0], build_group("g", "G", ["Data/a.tga"], crowd),
                                   "number %d in a crowded bucket" % number))
    return scenarios


@registered_pair("icon.group.TGIconGroup_SetIconLocation")
def set_location_test(env):
    args = [PTR, I32, I32, F32, F32, F32, F32, I32, I32]
    return icon_case(env, 0x72C260, 0x72C47F, "thiscall", args, None, "bc_TGIconGroup_SetIconLocation",
                     set_location_scenarios(), allow_leaks=True)  # B0022: an old location stays allocated


# Rotate and mirror ---------------------------------------------------------------------------------------------------------

def shaped_group(root="g"):
    locations = [(1, 0, 3.0, 5.0, 20.0, 10.0, 0, 0), (2, 1, 0.5, 0.25, 6.0, 12.0, 0, 0), (38, 0, 1.0, 2.0, 3.0, 4.0, 0, 0)]
    return build_group(root, "G", ["Data/a.tga", "Data/c.tga"], locations)


@registered_pair("icon.group.TGIconGroup_RotateIconLocation")
def rotate_test(env):
    scenarios = [gscenario([Root("g"), number, rotation], shaped_group(), "number %d rotation %d" % (number, rotation))
                 for number in (1, 2, 38) for rotation in (0, 1, 2, 3, 4, -1, 7)]
    return icon_case(env, 0x72C530, 0x72C68F, "thiscall", [PTR, I32, I32], None, "bc_TGIconGroup_RotateIconLocation",
                     scenarios)


@registered_pair("icon.group.TGIconGroup_MirrorIconLocation")
def mirror_test(env):
    scenarios = [gscenario([Root("g"), number, mirror], shaped_group(), "number %d mirror %d" % (number, mirror))
                 for number in (1, 2, 38) for mirror in (0, 1, 2, 3, -1, 99)]
    return icon_case(env, 0x72C690, 0x72C77F, "thiscall", [PTR, I32, I32], None, "bc_TGIconGroup_MirrorIconLocation",
                     scenarios)


# Size functions ------------------------------------------------------------------------------------------------------------

SIZE_MANAGERS = [dict(scale=1.0, width=640.0, height=480.0), dict(scale=0.75, width=800.0, height=600.0),
                 dict(scale=2.5, width=1024.0, height=768.0), dict(scale=1.0, width=0.0, height=0.0),
                 dict(scale=3.0e38, width=1.0e-30, height=7.0), dict(scale=0.0, width=640.0, height=480.0),
                 dict(scale=f32(0.1), width=f32(1280.0), height=f32(720.0)), dict(scale=-1.0, width=1920.0, height=1080.0)]


def size_scenarios():
    pre = build_group("g", "G", ["Data/a.tga", "Data/b.tga"],
                      [(1, 0, 3.0, 5.0, 20.0, 10.0, 0, 0), (2, 1, 0.5, 0.25, 6.5, 12.25, 1, 0), (38, 0, 1.0, 2.0, 0.0, 1e-3, 0, 0),
                       (3, 1, 0.0, 0.0, 16777217.0, 123456.789, 0, 0)])
    scenarios = []
    for manager_values in SIZE_MANAGERS:
        for number in (1, 2, 38, 3, 4, 0, -1):
            scenarios.append(gscenario([Root("g"), number], pre, "number %d manager %s" % (number, manager_values),
                                       glob=[manager(**manager_values)]))
    return scenarios


@registered_pair("icon.group.TGIconGroup_GetIconPixelWidth")
def pixel_width_test(env):
    return icon_case(env, 0x72C7E0, 0x72C83F, "thiscall", [PTR, I32], F64, "bc_TGIconGroup_GetIconPixelWidth",
                     size_scenarios())


@registered_pair("icon.group.TGIconGroup_GetIconPixelHeight")
def pixel_height_test(env):
    return icon_case(env, 0x72C840, 0x72C89F, "thiscall", [PTR, I32], F64, "bc_TGIconGroup_GetIconPixelHeight",
                     size_scenarios())


@registered_pair("icon.group.TGIconGroup_GetIconScreenWidth")
def screen_width_test(env):
    return icon_case(env, 0x72C8A0, 0x72C90F, "thiscall", [PTR, I32], F64, "bc_TGIconGroup_GetIconScreenWidth",
                     size_scenarios())


@registered_pair("icon.group.TGIconGroup_GetIconScreenHeight")
def screen_height_test(env):
    return icon_case(env, 0x72C910, 0x72C97F, "thiscall", [PTR, I32], F64, "bc_TGIconGroup_GetIconScreenHeight",
                     size_scenarios())


# Walk over the icon numbers --------------------------------------------------------------------------------------------------

def numbered_group(numbers, root="g"):
    return build_group(root, "G", ["Data/a.tga"], [(n, 0, 1.0, 2.0, 3.0, 4.0, 0, 0) for n in numbers])


NUMBER_SETS = [(), (5,), (1, 38, 75), (1, 2, 3, 4), (36, 0, 37, 73), (10, 20, 30, 40, 50, 60, 70, 80, 90), (0x7FFFFFFF, -1),
               (36, 36 + 37, 36 + 74, 5)]


@registered_once("icon.group.TGIconGroup_IsNumValid")
def is_num_valid_test(env):
    scenarios = []
    for numbers in NUMBER_SETS:
        for number in (0, 1, 5, 38, 75, 36, 37, 0x7FFFFFFF, -1, 112):
            scenarios.append(gscenario([Root("g"), number], numbered_group(numbers), "numbers %s number %d" % (numbers, number)))
    return icon_case(env, 0x72CD80, 0x72CDCF, "thiscall", [PTR, I32], U8, "bc_TGIconGroup_IsNumValid", scenarios)


@registered_once("icon.group.TGIconGroup_GoToFirstPos")
def go_to_first_pos_test(env):
    scenarios = [gscenario([Root("g")], numbered_group(numbers), "numbers %s" % (numbers,)) for numbers in NUMBER_SETS]
    return icon_case(env, 0x72CDD0, 0x72CDFF, "thiscall", [PTR], None, "bc_TGIconGroup_GoToFirstPos", scenarios,
                     probes=[log_probe(), trap_probe(), iterator_probe()])


@registered_once("icon.group.TGIconGroup_GetNext")
def get_next_test(env):
    scenarios = []
    for numbers in NUMBER_SETS[1:]:
        for calls in range(0, len(numbers)):
            pre = numbered_group(numbers) + [Pre("gofirst", [Root("g")])] + [Pre("getnext", [Root("g")]) for _ in range(calls)]
            scenarios.append(gscenario([Root("g")], pre, "numbers %s after %d calls" % (numbers, calls)))
    return icon_case(env, 0x72CE00, 0x72CE5F, "thiscall", [PTR], I32, "bc_TGIconGroup_GetNext", scenarios,
                     probes=[log_probe(), trap_probe(), iterator_probe()])


# Reset and the destructor -----------------------------------------------------------------------------------------------------

def group_states():
    a, b, c = "Data/a.tga", "Data/b.tga", "Data/c.tga"
    return [
        ("an empty group", build_group("g", "G")),
        ("a group without a name", build_group("g", None)),
        ("textures only", build_group("g", "G", [a, b])),
        ("a missing texture", build_group("g", "G", [a, "Data/missing.tga", b])),
        ("locations of one texture", build_group("g", "G", [a], [(1, 0, 1.0, 2.0, 3.0, 4.0, 0, 0), (2, 0, 2.0, 2.0, 3.0, 4.0, 0, 0)])),
        ("locations of two textures", build_group("g", "G", [a, b], [(n, n % 2, float(n), 1.0, 2.0, 3.0, 0, 0) for n in range(1, 8)])),
        ("crowded buckets", build_group("g", "G", [a], [(n, 0, 1.0, 1.0, 1.0, 1.0, 0, 0) for n in (1, 38, 75, 112, 2, 39)])),
        ("a shared texture", build_group("g", "G", [a, a, a], [(1, 0, 1.0, 1.0, 1.0, 1.0, 0, 0), (2, 1, 1.0, 1.0, 1.0, 1.0, 0, 0)])),
        ("sixteen textures", build_group("g", "G", [a, b, c] * 5 + [a], [(n, n, 1.0, 1.0, 1.0, 1.0, 1, 2) for n in range(0, 16)])),
        ("a locked group", build_group("g", "G", [a], [(1, 0, 1.0, 1.0, 1.0, 1.0, 0, 0)], lock=3)),
    ]


@registered_once("icon.group.TGIconGroup_Reset")
def reset_test(env):
    scenarios = [gscenario([Root("g")], pre, label) for label, pre in group_states()]
    return icon_case(env, 0x72BDE0, 0x72BF5F, "thiscall", [PTR], None, "bc_TGIconGroup_Reset", scenarios,
                     probes=[log_probe(), trap_probe()])


@registered_once("icon.group.TGIconGroup_destructor_body")
def destructor_body_test(env):
    scenarios = [gscenario([Root("g")], pre, label) for label, pre in group_states()]
    return icon_case(env, 0x72BCE0, 0x72BDDF, "thiscall", [PTR], None, "bc_TGIconGroup_DestructorBody", scenarios)


@registered_once("icon.group.TGIconGroup_deleting_destructor")
def deleting_destructor_test(env):
    scenarios = []
    for label, pre in group_states():
        for flags in (0, 1, 3):
            scenarios.append(gscenario([Root("g"), flags], pre, "%s, flags %d" % (label, flags)))
    return icon_case(env, 0x72ACB0, 0x72ACCF, "thiscall", [PTR, U32], PTR, "bc_TGIconGroup_DeletingDestructor", scenarios)


# GetPyWrapper -----------------------------------------------------------------------------------------------------------------

@registered_once("icon.group.TGIconGroup_GetPyWrapper")
def py_wrapper_test(env):
    scenarios = [gscenario([Root("g")], build_group("g", "G"), "a group")]
    return icon_case(env, 0x72C980, 0x72C98F, "thiscall", [PTR], U32, "bc_TGIconGroup_GetPyWrapper", scenarios)
