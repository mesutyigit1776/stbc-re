"""Planted defects of the icon tests: each native variant in src/emutest/IconDefects.cpp is wrong in one way, and the
test of the function must find it. A test that passes on a defect would not show a mistake in the real source.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icon_common import *  # noqa: F401,F403,E402
from icon_common import (ICON_ENVS, build_group, cache_hash, camera, holder, icon_case, iterator_probe, manager_global,  # noqa: E402
                         registration, scenario, texture)
from icon_wrapper_common import double_probe, manager_scenario, wrapper_case  # noqa: E402
from icon_wrapper_common import group_scenario as wrapper_group_scenario  # noqa: E402
from prop_common import failing_text  # noqa: E402

FILES = {"Data/a.tga": (64, 32), "Data/b.tga": (100, 100)}
E53, E24 = ICON_ENVS[53], ICON_ENVS[24]


def group_scenario(args, pre, label, **members):
    manager = manager_global(scale=members.get("scale", 1.0), width=members.get("width", 640.0), height=members.get("height", 480.0))
    return scenario({"g": Obj("IconGroup")}, args, pre, [manager], label, FILES)


def passes(case):
    results = case.run()
    assert all(r.ok for r in results), "the case with the real function must pass:\n" + "\n".join(
        line for r in results for line in r.lines)


def fails(case, fragment):
    text = failing_text(case)
    assert fragment in text, "expected %r in the report:\n%s" % (fragment, text)


SET_LOCATION = [PTR, I32, I32, F32, F32, F32, F32, I32, I32]


def set_location_case(env, symbol, values, path="Data/a.tga"):
    scenarios = [group_scenario([Root("g"), 5, 0] + list(v) + [0, 0], build_group("g", "G", [path]), "values %s" % (v,))
                 for v in values]
    return icon_case(env, 0x72C260, 0x72C47F, "thiscall", SET_LOCATION, None, symbol, scenarios)


@register("icon.defect.SetIconLocation_rounds_the_sum_of_y_and_height_to_float")
def defect_set_location_sum():
    values = [(1.0, 0.1, 1.0, 0.2), (3.0, 4.1, 5.0, 6.3)]
    fails(set_location_case(E53, "bc_TGIconGroup_SetIconLocation_defect1", values, "Data/b.tga"), ".c3")


@register("icon.defect.SetIconLocation_swaps_the_left_and_the_top_corner")
def defect_set_location_swap():
    values = [(8.0, 16.0, 1.0, 1.0)]
    fails(set_location_case(E53, "bc_TGIconGroup_SetIconLocation_defect2", values), ".c0")


@register("icon.defect.RotateIconLocation_moves_the_rotation_3_like_the_rotation_1")
def defect_rotate():
    pre = build_group("g", "G", ["Data/a.tga"], [(1, 0, 3.0, 5.0, 20.0, 10.0, 0, 0)])
    case = icon_case(E53, 0x72C530, 0x72C68F, "thiscall", [PTR, I32, I32], None, "bc_TGIconGroup_RotateIconLocation_defect",
                     [group_scenario([Root("g"), 1, 3], pre, "rotation 3")])
    fails(case, ".c0")


@register("icon.defect.MirrorIconLocation_treats_every_mirror_as_1")
def defect_mirror():
    pre = build_group("g", "G", ["Data/a.tga"], [(1, 0, 3.0, 5.0, 20.0, 10.0, 0, 0)])
    case = icon_case(E53, 0x72C690, 0x72C77F, "thiscall", [PTR, I32, I32], None, "bc_TGIconGroup_MirrorIconLocation_defect",
                     [group_scenario([Root("g"), 1, 2], pre, "mirror 2")])
    fails(case, ".c0")


@register("icon.defect.GetNext_gives_the_number_of_the_next_node")
def defect_get_next():
    pre = build_group("g", "G", ["Data/a.tga"], [(n, 0, 1.0, 2.0, 3.0, 4.0, 0, 0) for n in (1, 2, 3)]) + [Pre("gofirst", [Root("g")])]
    case = icon_case(E53, 0x72CE00, 0x72CE5F, "thiscall", [PTR], I32, "bc_TGIconGroup_GetNext_defect",
                     [group_scenario([Root("g")], pre, "the first call")], probes=[log_probe(), trap_probe(), iterator_probe()])
    fails(case, "ret")


def size_case(env, symbol):
    pre = build_group("g", "G", ["Data/a.tga"], [(1, 0, 3.0, 5.0, 20.0, 10.0, 0, 0), (2, 0, 1.0, 1.0, 0.1, 0.3, 0, 0)])
    scenarios = [group_scenario([Root("g"), n], pre, "number %d" % n, scale=0.3, width=640.0, height=480.0) for n in (1, 2)]
    return icon_case(env, 0x72C8A0, 0x72C90F, "thiscall", [PTR, I32], F64, symbol, scenarios)


@register("icon.defect.GetIconScreenWidth_uses_plain_double_arithmetic_and_is_equal_only_at_53_bits")
def defect_screen_width():
    passes(size_case(E53, "bc_TGIconGroup_GetIconScreenWidth_defect_p24"))
    fails(size_case(E24, "bc_TGIconGroup_GetIconScreenWidth_defect_p24"), "ret")


@register("icon.defect.LoadIconTexture_does_not_use_the_cache_and_keeps_a_reference")
def defect_load():
    pre = build_group("g", "G", ["Data/a.tga"])
    case = icon_case(E53, 0x72BFE0, 0x72C24F, "thiscall", [PTR, Buf(io="in"), U32], I32, "bc_TGIconGroup_LoadIconTexture_defect",
                     [group_scenario([Root("g"), narrow("Data/a.tga"), 0], pre, "a cached file")])
    fails(case, "")


@register("icon.defect.Reset_does_not_release_the_texture_properties")
def defect_reset():
    pre = build_group("g", "G", ["Data/a.tga", "Data/b.tga"], [(1, 0, 1.0, 1.0, 1.0, 1.0, 0, 0)])
    case = icon_case(E53, 0x72BDE0, 0x72BF5F, "thiscall", [PTR], None, "bc_TGIconGroup_Reset_defect",
                     [group_scenario([Root("g")], pre, "two textures")])
    fails(case, "")


@register("icon.defect.SetAtGrow_grows_the_array_by_one_element_too_much")
def defect_set_at_grow():
    roots = {"g": Obj("IconGroup"), "h1": holder(1, count=1)}
    case = icon_case(E53, 0x72D050, 0x72D1BF, "thiscall", [PTR, U32, PTR], U32, "bc_IconTextureArray_SetAtGrow_defect",
                     [scenario(roots, [Inner("g", "textures"), 20, Root("h1")], build_group("g", "G"), [], "an index past the end")])
    fails(case, "capacity")


@register("icon.defect.cache_Hash_takes_the_characters_as_unsigned")
def defect_cache_hash():
    case = icon_case(E53, 0x44EDB0, 0x44EDDF, "thiscall", [PTR, Buf(io="in")], U32, "bc_TextureCache_Hash_defect",
                     [scenario({}, [Inner("m", "textureCache"), narrow("caf\xe9")], [], [manager_global()], "a text with a high character")])
    fails(case, "ret")


@register("icon.defect.cache_Insert_loses_the_old_key_text_of_a_node")
def defect_cache_insert():
    cache = Obj("TextureCache", count=1, bucketCount=0x25, buckets=[None] * 0x25)
    bucket = cache_hash("Data/a.tga")
    tex = texture(1, 8, 8, count=1)
    cache.values["buckets"][bucket] = Obj("TextureCacheNode", key="Data/a.tga", value=tex)
    roots = {"t": texture(2, 4, 4, count=1)}
    case = icon_case(E53, 0x44EBD0, 0x44ECDF, "thiscall", [PTR, Buf(io="in"), PTR], None, "bc_TextureCache_Insert_defect",
                     [scenario(roots, [Inner("m", "textureCache"), narrow("Data/a.tga"), Root("t")], [], [manager_global(cache=cache)],
                               "an existing key")])
    fails(case, "")


@register("icon.defect.RegisterIconGroup_does_not_count_the_record")
def defect_register():
    case = icon_case(E53, 0x729E70, 0x729F9F, "thiscall", [PTR, Buf(io="in"), Buf(io="in"), Buf(io="in")], None,
                     "bc_TGIconManager_RegisterIconGroup_defect",
                     [scenario({}, [Root("m"), narrow("A"), narrow("B"), narrow("C")], [], [manager_global()], "one record")])
    fails(case, "count")


@register("icon.defect.DeleteIconGroup_does_not_flush_the_renderer")
def defect_delete():
    group = Obj("IconGroup")
    pre = build_group("g0", "A") + [Pre("addgroup", [Root("m"), Root("g0")])]
    case = icon_case(E53, 0x72A210, 0x72A3FF, "thiscall", [PTR, Buf(io="in")], U8, "bc_TGIconManager_DeleteIconGroup_defect",
                     [scenario({"g0": group}, [Root("m"), narrow("A")], pre, [manager_global(registrations=[registration("A", "m", "f")])],
                               "a loaded group")])
    fails(case, "calls outside the icon classes")


@register("icon.defect.Purge_deletes_a_locked_group")
def defect_purge():
    pre = build_group("g0", "A", lock=2) + [Pre("addgroup", [Root("m"), Root("g0")])]
    case = icon_case(E53, 0x72A080, 0x72A14F, "thiscall", [PTR], None, "bc_TGIconManager_Purge_defect",
                     [scenario({"g0": Obj("IconGroup")}, [Root("m")], pre, [manager_global()], "a locked group")])
    fails(case, "lockCount")


@register("icon.defect.AddIconGroup_adds_a_group_with_a_loaded_name")
def defect_add():
    pre = build_group("g0", "A") + [Pre("addgroup", [Root("m"), Root("g0")])] + build_group("g1", "A")
    case = icon_case(E53, 0x72A000, 0x72A07F, "thiscall", [PTR, PTR], None, "bc_TGIconManager_AddIconGroup_defect",
                     [scenario({"g0": Obj("IconGroup"), "g1": Obj("IconGroup")}, [Root("m"), Root("g1")], pre, [manager_global()],
                               "the same name twice")], allow_leaks=True)
    fails(case, "groups")


@register("icon.defect.SetDisplayInfo_reads_the_size_as_a_signed_value")
def defect_display_info():
    roots = {"c": camera(5, 0xFFFFFFFF, 0xFFFFFFFF, count=1)}
    case = icon_case(E53, 0x729DC0, 0x729E6F, "thiscall", [PTR, PTR, F32], None, "bc_TGIconManager_SetDisplayInfo_defect",
                     [scenario(roots, [Root("m"), Root("c"), 1.0], [], [manager_global()], "a view of 0xffffffff pixels")])
    fails(case, "screenWidth")


@register("icon.defect.SetPixelCenterAdj_divides_both_by_the_screen_height")
def defect_adjustment():
    case = icon_case(E53, 0x72A920, 0x72A93F, "thiscall", [PTR, F32], None, "bc_TGIconManager_SetPixelCenterAdj_defect",
                     [scenario({}, [Root("m"), 1.0], [], [manager_global(width=640.0, height=480.0)], "different sizes")])
    fails(case, "pixelCenterAdjX")


# The wrappers ---------------------------------------------------------------------------------------------------------------

def wrapper_defect_case(first, last, symbol, scenarios, probes=()):
    return wrapper_case(E53, first, last, symbol, scenarios, probes=list(probes))


@register("icon.defect.wrapper_GetScreenWidth_reads_the_screen_height")
def defect_wrapper_width():
    case = wrapper_defect_case(0x5D5BE0, 0x5D5C4F, "bc_Appc_TGIconManager_GetScreenWidth_defect",
                               [manager_scenario([], "different sizes", width=640.0, height=480.0)], [double_probe()])
    fails(case, "float handed to Python")


@register("icon.defect.wrapper_SetCamera_does_not_count_references")
def defect_wrapper_camera():
    old, fresh = camera(6, count=1), camera(5, count=1)
    case = wrapper_defect_case(0x5D6160, 0x5D623F, "bc_Appc_TGIconManager_SetCamera_defect",
                               [manager_scenario([], "another camera", fresh, {"old": old, "fresh": fresh}, camera=old)])
    fails(case, "refCount")


@register("icon.defect.wrapper_delete_does_not_run_the_destructor")
def defect_wrapper_delete():
    case = wrapper_defect_case(0x5D4FF0, 0x5D505F, "bc_Appc_TGIconGroup_delete_defect",
                               [wrapper_group_scenario(build_group("g", "G", ["Data/a.tga"]), "a group with a texture")])
    fails(case, "")
