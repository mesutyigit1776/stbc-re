"""Tests of SubsystemProperty (0x692470 and the functions around it) and of the inline code in its Appc wrappers.

Objects are built with the constructors of the exe. Float fields are set with a setup call that writes a bit pattern, so
that NaN payloads, zeros of both signs, denormals and the largest values reach the code. Every test that computes with
floats runs against the library of each precision (x87 control word 0x027F with the 53-bit library, 0x007F with the
24-bit library). The wrapper tests run the whole wrapper of the exe with stubs for the Python interpreter: the
argument parser writes the test values, the SWIG pointer conversion gives the object, and the result helpers keep what
the wrapper hands to Python.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_property_common import *  # noqa: F401,F403,E402
from prop_property_common import (ENVS, FACTORY_TABLES, NODE_LOG, NONE_OBJECT, PROPERTY_ALLOW, TYPES,  # noqa: E402
                                  build_case, copy_probe, duplicate_probe, name_state, real_factory,
                                  new_property, node_log_probe, one, poke, prop_original, property_stubs,
                                  registered_both, registered_once, registry_for, state_setup, with_constructors)
from prop_common import failing_text, narrow  # noqa: E402
from emuharness import Raw  # noqa: E402
from emuharness import types as T  # noqa: E402
from prop_wrapper_common import (CONVERT, FLOAT_FROM_DOUBLE, HANDLES, INT_FROM_LONG, NEW_POINTER, PARSE, WSTATE,  # noqa: E402
                                 double_probe, integer_probe, pointer_probe, wrapper_setup, wrapper_stubs)

F32_BITS = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x43480000, 0x3E800000, 0x3F000000, 0x00000001,
            0x80000001, 0x007FFFFF, 0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000,
            0xFFC00000, 0x7FC12345, 0x4B800001, 0x3DCCCCCD, 0x461C4000, 0xC2C80000]
EDGE = [Raw(b) for b in F32_BITS]
OFFSETS = {"maxCondition": 0x20, "critical": 0x24, "targetable": 0x25, "primary": 0x26, "x": 0x28, "y": 0x2C, "z": 0x30,
           "u": 0x34, "v": 0x38, "repairComplexity": 0x3C, "disabledPercentage": 0x40, "radius": 0x44}
PROBES = [name_state("p", "q"), trap_probe()]
SUBSYSTEM_FIELDS = (("maxCondition", 4), ("critical", 1), ("targetable", 1), ("primary", 1), ("repairComplexity", 4),
                    ("disabledPercentage", 4), ("radius", 4))


def sroots(**extra):
    roots = {"p": Obj("SubsystemProperty"), "q": Obj("SubsystemProperty")}
    roots.update(extra)
    return roots


def subsystem(root="p", ident=0, name=None, refs=0, **fields):
    """Setup calls for a constructed SubsystemProperty with the given field bits (name -> bits or byte)."""
    pre = new_property(root, ident, name, refs, cls="sctor")
    for field, value in fields.items():
        pre.append(poke(root, OFFSETS[field], value, 1 if field in ("critical", "targetable", "primary") else 4))
    return pre


def pairs(count, mix=1):
    """Rows of float bit patterns: every edge pattern with a shifted partner."""
    n = len(F32_BITS)
    return [tuple(F32_BITS[(i + k * mix * 3) % n] for k in range(count)) for i in range(n)]


def thiscall(first, last, args, ret):
    return prop_original(first, last, "thiscall", args, ret)


def float_args(bits):
    return [Raw(b) for b in bits]


# Construction ---------------------------------------------------------------------------------------------

POSITIONS = [((0, 0, 0), (0, 0)), ((0x3F800000, 0x7FC00001, 0x80000000), (0xFFFFFFFF, 0x00000001)),
             ((0xDEADBEEF, 0x12345678, 0x7F800000), (0x80000000, 0x7FC00000))]


def ctor_builder(env):
    scenarios = []
    for positions in POSITIONS:
        for requested in (0, 1, 3, 100, 0x7FFFFFFF):
            for keys in (None, [], [2, 3]):
                glob = registry_global(None) if keys is None else registry_for(keys)
                scenarios.append(Scenario({"p": Obj("SubsystemProperty")}, [Root("p"), requested], globals=[glob],
                                          setup=state_setup(next_id=50, positions=positions),
                                          label="positions %s id %d registry %s" % (positions[0][:1], requested, keys)))
    return build_case(env, thiscall(0x692470, 0x6924DF, [PTR, I32], PTR), "bc_SubsystemProperty_SubsystemProperty",
                      scenarios, ret_class="SubsystemProperty", setup_calls={}, allow_leaks=True,  # an equal ID drops an object
                      probes=[next_id_probe(), name_state("p"), trap_probe()])


registered_both("prop.subsystem.SubsystemProperty_SubsystemProperty", ctor_builder)


def create_builder(env):
    scenarios = [one(sroots(), [ident], [], "id %d registry %s" % (ident, keys), keys=keys,
                     setup=state_setup(500, positions=POSITIONS[1])) for ident in (0, 1, 100, 0x7FFFFFFF)
                 for keys in ([], [3, 4])]
    original = prop_original(0x43FF30, 0x43FF5D, "cdecl", [I32], PTR)
    return build_case(env, original, "bc_SubsystemProperty_CreateForFactory", scenarios, ret_class="SubsystemProperty",
                      probes=[next_id_probe(), name_state("ret"), trap_probe()])


registered_both("prop.subsystem.SubsystemProperty_CreateForFactory", create_builder)


# Type functions and constant strings -------------------------------------------------------------------------

def simple(first, last, symbol, args, ret, arg_lists, ret_class=None, conv="thiscall", probes=PROBES):
    scenarios = [one(sroots(), a, subsystem(name="Phaser 1"), "args %s" % (a,)) for a in arg_lists]
    return build_case(ENVS[53], prop_original(first, last, conv, args, ret), symbol, scenarios, ret_class=ret_class,
                      probes=probes)


@register("prop.subsystem.SubsystemProperty_GetObjType")
def get_obj_type_test():
    return simple(0x6924E0, 0x6924E5, "bc_SubsystemProperty_GetObjType", [PTR], I32, [[Root("p")]])


@register("prop.subsystem.SubsystemProperty_IsTypeOf")
def is_type_of_test():
    return simple(0x6924F0, 0x692514, "bc_SubsystemProperty_IsTypeOf", [PTR, I32], U8,
                  [[Root("p"), t] for t in TYPES + [0x812B, 0x601, 2]])


@register("prop.subsystem.SubsystemProperty_class_name_functions")
def class_names_test():
    return [simple(first, last, symbol, [PTR], U32, [[Root("p")]]) for first, last, symbol in (
        (0x692520, 0x692525, "bc_SubsystemProperty_GetClassName"),
        (0x692530, 0x692535, "bc_SubsystemProperty_GetSwigPointerName"),
        (0x692540, 0x692545, "bc_SubsystemProperty_GetShadowClassName"))]


@register("prop.subsystem.SubsystemProperty_Cast")
def cast_test():
    scenarios = [one(sroots(t=Obj("TGModelProperty")), [Root(name)], [], "argument %s" % name) for name in ("p", "t")]
    scenarios.append(one(sroots(t=Obj("TGModelProperty")), [0], [], "null argument"))
    return build_case(ENVS[53], prop_original(0x6923F0, 0x692410, "cdecl", [PTR], PTR), "bc_SubsystemProperty_Cast",
                      scenarios, ret_class="SubsystemProperty", probes=PROBES)


# Positions ----------------------------------------------------------------------------------------------------

def get_position_builder(env):
    scenarios = []
    for x, y, z in pairs(3):
        pre = subsystem(x=x, y=y, z=z)
        scenarios.append(one(sroots(out=Obj("PropPoint3")), [Root("p"), Root("out")], pre,
                             "position %08x %08x %08x" % (x, y, z)))
    return build_case(env, thiscall(0x63AE20, 0x63AE41, [PTR, PTR], None), "bc_SubsystemProperty_GetPositionTG",
                      scenarios, probes=PROBES)


registered_both("prop.subsystem.SubsystemProperty_GetPositionTG", get_position_builder)


def set_position_builder(env):
    scenarios = [one(sroots(), [Root("p")] + float_args(bits), subsystem(), "position %08x %08x %08x" % bits)
                 for bits in pairs(3, 2) + pairs(3, 5)]
    return build_case(env, thiscall(0x6927A0, 0x6927B7, [PTR, F32, F32, F32], None),
                      "bc_SubsystemProperty_SetPositionXYZ", scenarios, probes=PROBES)


registered_both("prop.subsystem.SubsystemProperty_SetPositionXYZ", set_position_builder)


def set_position2d_builder(env):
    scenarios = [one(sroots(), [Root("p")] + float_args(bits), subsystem(), "position2D %08x %08x" % bits)
                 for bits in pairs(2, 2) + pairs(2, 7)]
    return build_case(env, thiscall(0x6927C0, 0x6927D0, [PTR, F32, F32], None), "bc_SubsystemProperty_SetPosition2DXY",
                      scenarios, probes=PROBES)


registered_both("prop.subsystem.SubsystemProperty_SetPosition2DXY", set_position2d_builder)


# CopyFrom ----------------------------------------------------------------------------------------------------

def source_fields(i):
    n = len(F32_BITS)
    return dict(maxCondition=F32_BITS[i % n], critical=i % 3, targetable=(i + 1) % 2, primary=(i // 2) % 3,
                x=F32_BITS[(i + 1) % n], y=F32_BITS[(i + 2) % n], z=F32_BITS[(i + 3) % n],
                u=F32_BITS[(i + 4) % n], v=F32_BITS[(i + 5) % n], repairComplexity=F32_BITS[(i + 6) % n],
                disabledPercentage=F32_BITS[(i + 7) % n], radius=F32_BITS[(i + 8) % n])


def quiet(bits):
    """The patterns that the load and store through the x87 registers keep: all but signalling NaNs."""
    return bits


def copy_from_builder(env):
    scenarios = []
    for i in range(len(F32_BITS)):
        pre = subsystem("p", name="target") + subsystem("q", name=["", "Phaser 1", "caf\xe9", "n" * 40][i % 4],
                                                        **source_fields(i))
        scenarios.append(one(sroots(), [Root("p"), Root("q")], pre, "source fields #%d" % i))
    return build_case(env, thiscall(0x692710, 0x692798, [PTR, PTR], U8), "bc_SubsystemProperty_CopyFrom", scenarios,
                      probes=PROBES)


registered_both("prop.subsystem.SubsystemProperty_CopyFrom", copy_from_builder)


# Destruction ---------------------------------------------------------------------------------------------------

def node_roots(count):
    parent = Obj("NiNode", refCount=5)
    return sroots(n=Obj("NiNode", refCount=count, parent=parent), np=parent)


def node_scenarios(args_for, counts=(1, 2, 3, 0, -1), name="Phaser 1"):
    scenarios = []
    for count in counts:
        for nothing in (False, True):
            pre = subsystem(name=name) + [Pre("setnode", [Root("p"), 0x1C, Root("n"), 4])]
            scenarios.append(one(node_roots(count), args_for, pre,
                                 "node count %d, detach returns %s" % (count, "nothing" if nothing else "the node"),
                                 setup=state_setup(detach_returns_nothing=nothing)))
    return scenarios


def destructor_probes():
    return PROBES + [node_log_probe()]


@register("prop.subsystem.SubsystemProperty_destructor")
def destructor_test():
    scenarios = node_scenarios([Root("p")])
    for name in (None, "Phaser 1", "n" * 40):
        scenarios.append(one(sroots(), [Root("p")], subsystem(name=name), "no node, name %r" % (name and name[:6]),
                             keys=[5, 5 + 0x7F7]))
    return build_case(ENVS[53], thiscall(0x692680, 0x69270E, [PTR], None), "bc_SubsystemProperty_DestructorBody",
                      scenarios, probes=destructor_probes())


@register("prop.subsystem.SubsystemProperty_DeletingDestructor")
def deleting_destructor_test():
    scenarios = []
    for flags in (0, 1, 2, 3):
        for s in node_scenarios([Root("p"), flags], counts=(1, 2)) + [
                one(sroots(), [Root("p"), flags], subsystem(name="Phaser 1"), "no node")]:
            s.label = "flags %d, %s" % (flags, s.label)
            scenarios.append(s)
    return build_case(ENVS[53], thiscall(0x692650, 0x692678, [PTR, U32], PTR), "bc_SubsystemProperty_DeletingDestructor",
                      scenarios, ret_class="SubsystemProperty", probes=destructor_probes())


@register("prop.subsystem.B0004_CopyFrom_with_a_source_that_is_not_a_SubsystemProperty_faults_in_the_exe")
def copy_from_wrong_source_test():
    scenario = one(sroots(t=Obj("TGModelProperty")), [Root("p"), Root("t")], subsystem(name="target"),
                   "source is a TGModelProperty")
    case = build_case(ENVS[53], thiscall(0x692710, 0x692798, [PTR, PTR], U8), "bc_SubsystemProperty_CopyFrom",
                      [scenario], probes=PROBES)
    text = failing_text(case)
    assert "original run stopped: UnmappedAccess: read from unmapped memory at eip 0x00692733" in text, text


# Duplicate ---------------------------------------------------------------------------------------------------

def duplicate_builder(env):
    scenarios = []
    for table in ("both", "property", "empty"):
        for i in range(0, len(F32_BITS), 3):
            glob_set = [registry_for([4]), real_factory(FACTORY_TABLES[table])]
            pre = with_constructors(sroots(), subsystem(name=["Phaser 1", "", "caf\xe9"][i % 3], refs=i % 3,
                                                        **source_fields(i)))
            scenarios.append(Scenario(sroots(), [Root("p")], globals=glob_set, setup=state_setup(next_id=60),
                                      pre=pre, label="table %s, source fields #%d" % (table, i)))
    probes = [name_state("p", "q", "ret"), trap_probe(), next_id_probe(), copy_probe(),
              duplicate_probe("SubsystemProperty", SUBSYSTEM_FIELDS, node=True)]
    return build_case(env, thiscall(0x6EC7E0, 0x6EC80D, [PTR], PTR), "bc_TGModelProperty_Duplicate", scenarios,
                      ret_class="SubsystemProperty", probes=probes)


registered_both("prop.subsystem.SubsystemProperty_Duplicate", duplicate_builder)


# Appc wrappers -------------------------------------------------------------------------------------------------

INTS = [0, 1, 2, -1, 255, 256, 0x7FFFFFFF, -2147483648, 0x100, 0x10000]


def wrapper_original(first, last, extra=()):
    return Original(first, last - first + 1, "cdecl", [U32, PTR] + list(extra), U32,
                    allow=[tuple(a) for a in PROPERTY_ALLOW])


def wrapper_case(env, first, last, symbol, rows, extra, pokes=None, probes=(), allow_leaks=False, pass_row=True):
    """One scenario per row. A row holds the values for the Python arguments; pokes(row) gives the setup calls that
    put a state into the object first."""
    scenarios = []
    for row in rows:
        pre = subsystem(**(pokes(row) if pokes else {}))
        given = list(row) if pass_row else []
        scenarios.append(Scenario(sroots(), [0, Root("p")] + given, globals=[registry_for([])], pre=with_constructors(
            sroots(), pre), setup=wrapper_setup(given), label="row %s" % (tuple(getattr(v, "bits", v) for v in row),)))
    return build_case(env, wrapper_original(first, last, extra), symbol, scenarios, stubs=wrapper_stubs(),
                      probes=PROBES + list(probes), allow_leaks=allow_leaks)


FLOAT_SETTERS = [("SetMaxCondition", 0x63A970, 0x63A9E2), ("SetRepairComplexity", 0x63AB70, 0x63ABE2),
                 ("SetDisabledPercentage", 0x63ABF0, 0x63AC62), ("SetRadius", 0x63AC70, 0x63ACE2)]
INT_SETTERS = [("SetCritical", 0x63A9F0, 0x63AA67), ("SetTargetable", 0x63AA70, 0x63AAE7),
               ("SetPrimary", 0x63AAF0, 0x63AB67)]
FLOAT_GETTERS = [("GetMaxCondition", 0x63A630, 0x63A690, "maxCondition"),
                 ("GetRepairComplexity", 0x63A820, 0x63A880, "repairComplexity"),
                 ("GetDisabledPercentage", 0x63A890, 0x63A8F0, "disabledPercentage"),
                 ("GetRadius", 0x63A900, 0x63A960, "radius")]
INT_GETTERS = [("IsCritical", 0x63A6A0, 0x63A711, "critical"), ("IsTargetable", 0x63A720, 0x63A791, "targetable"),
               ("IsPrimary", 0x63A7A0, 0x63A811, "primary")]


def register_float_setter(name, first, last):
    def build(env):
        rows = [(Raw(b),) for b in F32_BITS]
        return wrapper_case(env, first, last, "bc_Appc_SubsystemProperty_" + name, rows, [F32])
    registered_both("prop.subsystem.Appc_SubsystemProperty_" + name, build)


def register_int_setter(name, first, last):
    registered_once("prop.subsystem.Appc_SubsystemProperty_" + name, lambda env: wrapper_case(
        env, first, last, "bc_Appc_SubsystemProperty_" + name, [(v,) for v in INTS], [I32]))


def register_float_getter(name, first, last, field):
    def build(env):
        rows = [(b,) for b in F32_BITS]
        return wrapper_case(env, first, last, "bc_Appc_SubsystemProperty_" + name, rows, [], pokes=lambda row: {field: row[0]},
                            probes=[double_probe()], pass_row=False)
    registered_both("prop.subsystem.Appc_SubsystemProperty_" + name, build)


def register_int_getter(name, first, last, field):
    registered_once("prop.subsystem.Appc_SubsystemProperty_" + name, lambda env: wrapper_case(
        env, first, last, "bc_Appc_SubsystemProperty_" + name, [(v,) for v in (0, 1, 2, 0x7F, 0x80, 0xFF)], [],
        pokes=lambda row: {field: row[0]}, probes=[integer_probe()], pass_row=False))


for _name, _first, _last in FLOAT_SETTERS:
    register_float_setter(_name, _first, _last)
for _name, _first, _last in INT_SETTERS:
    register_int_setter(_name, _first, _last)
for _name, _first, _last, _field in FLOAT_GETTERS:
    register_float_getter(_name, _first, _last, _field)
for _name, _first, _last, _field in INT_GETTERS:
    register_int_getter(_name, _first, _last, _field)


def set_position_wrapper(env):
    rows = [tuple(Raw(b) for b in bits) for bits in pairs(3, 2) + pairs(3, 5)]
    return wrapper_case(env, 0x63AF30, 0x63AFCD, "bc_Appc_SubsystemProperty_SetPosition", rows, [F32, F32, F32])


registered_both("prop.subsystem.Appc_SubsystemProperty_SetPosition", set_position_wrapper)


def set_position2d_wrapper(env):
    rows = [tuple(Raw(b) for b in bits) for bits in pairs(2, 2) + pairs(2, 7)]
    return wrapper_case(env, 0x63AFD0, 0x63B059, "bc_Appc_SubsystemProperty_SetPosition2D", rows, [F32, F32])


registered_both("prop.subsystem.Appc_SubsystemProperty_SetPosition2D", set_position2d_wrapper)


def get_position_wrapper(env):
    rows = [(x, y, z) for x, y, z in pairs(3)]
    return wrapper_case(env, 0x63ACF0, 0x63AD85, "bc_Appc_SubsystemProperty_GetPosition", rows, [],
                        pokes=lambda row: {"x": row[0], "y": row[1], "z": row[2]}, probes=[pointer_probe()],
                        allow_leaks=True, pass_row=False)  # the point belongs to Python


registered_both("prop.subsystem.Appc_SubsystemProperty_GetPosition", get_position_wrapper)


# GetPosition2D: the wrapper takes the object and two float pointers from its Python arguments. The pointers reach
# the stub through the tuple, which is an object with three pointers.

def get_position2d_builder(env):
    state = {"index": 0}
    stubs = wrapper_stubs()

    def parse(ctx):
        state["args"] = ctx.arg(0)
        state["index"] = 0
        ctx.write(ctx.arg(2), struct.pack("<I", 0x1000))
        ctx.write(ctx.arg(3), struct.pack("<I", 0x1000))
        ctx.write(ctx.arg(4), struct.pack("<I", 0x1000))
        ctx.returns(1)

    def convert(ctx):
        value = ctx.machine.read_u32(state["args"] + 4 * state["index"])
        state["index"] += 1
        ctx.write(ctx.arg(1), struct.pack("<I", value))
        ctx.returns(0)
    stubs[0x74E310] = Stub(parse, nargs=0)
    stubs[CONVERT] = Stub(convert, nargs=4)
    scenarios = []
    for u, v in pairs(2, 3) + pairs(2, 5):
        cells = {"x": Obj("FloatCell", value=0), "y": Obj("FloatCell")}
        sub = Obj("SubsystemProperty")
        roots = {"p": sub, "x": cells["x"], "y": cells["y"], "t": Obj("WrapperPointers", object=sub, x=cells["x"], y=cells["y"])}
        pre = subsystem(u=u, v=v)
        scenarios.append(Scenario(roots, [0, Root("t")], globals=[registry_for([])],
                                  pre=with_constructors(roots, pre), setup=wrapper_setup(),
                                  label="position2D %08x %08x" % (u, v)))
    return build_case(env, wrapper_original(0x63AE50, 0x63AF27, []), "bc_Appc_SubsystemProperty_GetPosition2D",
                      scenarios, stubs=stubs, probes=PROBES)


registered_both("prop.subsystem.Appc_SubsystemProperty_GetPosition2D", get_position2d_builder)




# Planted defects ----------------------------------------------------------------------------------------------

@register("prop.subsystem.planted_defect_SetRadius_writes_the_neighbour_field")
def planted_set_radius():
    env = ENVS[53]
    rows = [(Raw(0x40490FDB),), (Raw(0x3F000000),)]
    scenarios = [Scenario(sroots(), [0, Root("p")] + list(row), globals=[registry_for([])],
                          pre=with_constructors(sroots(), subsystem()), setup=wrapper_setup(row),
                          label="radius %08x" % row[0].bits) for row in rows]
    text = failing_text(build_case(env, wrapper_original(0x63AC70, 0x63ACE2, [F32]),
                                   "bc_Appc_SubsystemProperty_SetRadius_defect", scenarios, stubs=wrapper_stubs(),
                                   probes=PROBES))
    assert "p.radius: original=3.1415927410125732 [0x40490fdb] native=0.25 [0x3e800000]" in text, text
    assert "p.disabledPercentage: original=0.5 [0x3f000000] native=3.1415927410125732 [0x40490fdb]" in text, text


@register("prop.subsystem.planted_defect_IsTypeOf_does_not_test_the_TGModelProperty_ID")
def planted_is_type_of():
    scenarios = [one(sroots(), [Root("p"), t], subsystem(), "type %#x" % t) for t in (0x812B, 0x601, 2, 0x602)]
    text = failing_text(build_case(ENVS[53], thiscall(0x6924F0, 0x692514, [PTR, I32], U8),
                                   "bc_SubsystemProperty_IsTypeOf_defect", scenarios, probes=PROBES))
    assert "scenario #1 type 0x601" in text and "ret: original=0x01 (u8 1)  native=0x00 (u8 0)" in text, text
    assert "1 of 4 scenarios differ" in text, text
