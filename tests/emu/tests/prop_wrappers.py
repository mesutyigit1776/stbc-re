"""Tests of the Appc wrappers of TGObject that have engine code of their own, and of the small functions that the
property wrappers call.

The wrapper tests run the whole wrapper of the exe with stubs for the Python interpreter (see
prop_wrapper_common.py): the native side calls the method through a shim. The other tests run the exe function
and the source function on equal objects, as the tests of the property classes do.
"""
import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_wrapper_common import *  # noqa: F401,F403,E402
from prop_wrapper_common import WSTATE, integer_probe, wrapper_setup, wrapper_stubs  # noqa: E402
from prop_common import GuestSide  # noqa: E402
from prop_property_common import (ENVS, PROPERTY_ALLOW, TYPES, build_case, new_property, poke,  # noqa: E402
                                  property_stubs, registry_for, state_setup, Env)
from prop_common import failing_text, narrow, narrow_text, orig, span  # noqa: E402

SET_SLOTS = [span(0x5963A0, 0x5963A5), span(0x5963B0, 0x5963C8), span(0x6EB040, 0x6EB0C6)]
WRAPPER_ALLOW = PROPERTY_ALLOW + SET_SLOTS


def make_env():
    base = ENVS[53]
    lib = base.lib
    calls = dict(base.setup_calls)
    calls["tgctor"] = SetupCall(orig(0x6F0A70, 0x6F0B54, "thiscall", [PTR, I32], PTR, allow=WRAPPER_ALLOW),
                                Native(lib, "bc_TGObject_TGObject"))
    calls["setctor"] = SetupCall(orig(0x6EB040, 0x6EB0C6, "thiscall", [PTR, I32], PTR, allow=WRAPPER_ALLOW),
                                 Native(lib, "bc_TGModelPropertySet_TGModelPropertySet"))
    return Env(53, lib, base.model, base.cw, calls)


WENV = make_env()
CONSTRUCTORS = {"o": "tgctor", "p": "ctor", "s": "sctor", "g": "setctor"}
CLASSES_OF_ROOTS = {"o": "TGObject", "p": "TGModelProperty", "s": "SubsystemProperty", "g": "TGModelPropertySet"}


def roots():
    return {name: Obj(cls) for name, cls in CLASSES_OF_ROOTS.items()}


def constructed(tested, ident):
    """Setup calls: the other objects get counter IDs, the tested object gets the requested ID."""
    others = [Pre(CONSTRUCTORS[name], [Root(name), 0]) for name in CLASSES_OF_ROOTS if name != tested]
    return others + [Pre(CONSTRUCTORS[tested], [Root(tested), ident])]


def wrapper_original(first, last, extra=()):
    return Original(first, last - first + 1, "cdecl", [U32, PTR] + list(extra), U32,
                    allow=[tuple(a) for a in WRAPPER_ALLOW])


def wrapper_case(first, last, symbol, rows, extra, probes=()):
    """rows: (root name or None for a null object, ident, Python argument values)."""
    scenarios = []
    for root, ident, values in rows:
        pre = constructed(root or "o", ident)
        scenarios.append(Scenario(roots(), [0, Root(root) if root else 0] + list(values), globals=[registry_for([])],
                                  pre=pre, setup=wrapper_setup(values),
                                  label="object %s (ident %d), values %s" % (root, ident, tuple(values))))
    return build_case(WENV, wrapper_original(first, last, extra), symbol, scenarios, stubs=wrapper_stubs(),
                      probes=[trap_probe(), next_id_probe()] + list(probes))


OBJECTS = ("o", "p", "s", "g")
IDENTS = (0, 7, 50, 0x7FFFFFFF, -1, 0x12345678)


@register("prop.wrappers.Appc_TGObject_GetObjType")
def get_obj_type_wrapper():
    rows = [(root, ident, ()) for root in OBJECTS for ident in IDENTS]
    return wrapper_case(0x5C12C0, 0x5C131D, "bc_Appc_TGObject_GetObjType", rows, [], [integer_probe()])


@register("prop.wrappers.Appc_TGObject_IsTypeOf")
def is_type_of_wrapper():
    rows = [(root, 0, (type_,)) for root in OBJECTS for type_ in TYPES + [0x602, 0x2]]
    return wrapper_case(0x5C1320, 0x5C139B, "bc_Appc_TGObject_IsTypeOf", rows, [I32], [integer_probe()])


@register("prop.wrappers.Appc_TGObject_GetObjID")
def get_obj_id_wrapper():
    rows = [(root, ident, ()) for root in OBJECTS for ident in IDENTS]
    return wrapper_case(0x5C13A0, 0x5C13FB, "bc_Appc_TGObject_GetObjID", rows, [], [integer_probe()])


@register("prop.wrappers.Appc_TGObject_Destroy")
def destroy_wrapper():
    rows = [(root, ident, ()) for root in ("o", "p", "s") for ident in (0, 9)] + [(None, 0, ())]
    return wrapper_case(0x5C1400, 0x5C1469, "bc_Appc_TGObject_Destroy", rows, [])


def flag_probe():
    """The byte at 0x95b07d that SetIncrementID writes and IsIncrementID reads."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return side.read(0x95B07D, 1)
        side.lib.bc_TGObject_advanceNextId.restype = ctypes.c_uint8
        return bytes([side.lib.bc_TGObject_advanceNextId()])
    return Probe("flag byte of the ID counter", read)


def flag_case(first, last, symbol, rows, extra, probes=()):
    """rows: (initial flag byte, Python argument values). The wrapper has no object."""
    scenarios = []
    for flag, values in rows:
        def reset(machine, lib, values=values):
            WSTATE.update(values=tuple(values), object=0, double=None, integer=None, pointer=None)
        scenarios.append(Scenario({}, [0, 0] + list(values), globals=[registry_for([])],
                                  setup=state_setup(advance=flag, extra=reset),
                                  label="flag %d, values %s" % (flag, tuple(values))))
    return build_case(WENV, wrapper_original(first, last, extra), symbol, scenarios, stubs=wrapper_stubs(),
                      probes=[flag_probe(), next_id_probe()] + list(probes))


@register("prop.wrappers.Appc_TGObject_SetIncrementID")
def set_increment_wrapper():
    rows = [(flag, (value,)) for flag in (0, 1) for value in (0, 1, 2, -1, 0x100, 0x7FFFFFFF, -0x80000000)]
    return flag_case(0x5C1540, 0x5C157B, "bc_Appc_TGObject_SetIncrementID", rows, [I32])


@register("prop.wrappers.Appc_TGObject_IsIncrementID")
def is_increment_wrapper():
    rows = [(flag, ()) for flag in (0, 1, 2, 0xFF)]
    return flag_case(0x5C1580, 0x5C15B4, "bc_Appc_TGObject_IsIncrementID", rows, [], [integer_probe()])


# Functions that the property wrappers call ------------------------------------------------------------------------

def scenario(args, pre, label, objects=None):
    return Scenario(objects or {}, args, globals=[registry_for([])], pre=pre, setup=state_setup(next_id=100), label=label)


def function_case(first, last, conv, args, ret, symbol, scenarios, **kw):
    kw.setdefault("stubs", property_stubs())
    return build_case(WENV, orig(first, last, conv, args, ret, allow=WRAPPER_ALLOW), symbol, scenarios, **kw)


@register("prop.wrappers.TGModelProperty_GetRefCount")
def get_ref_count_test():
    scenarios = []
    for count in (0, 1, 2, 5, 0x7FFFFFFF, -1, -0x80000000):
        for name in (None, "Phaser 1"):
            pre = new_property("p", 0, name) + [poke("p", 8, count)]
            scenarios.append(scenario([Root("p")], pre, "count %d" % count, {"p": Obj("TGModelProperty")}))
    return function_case(0x6EC7C0, 0x6EC7C3, "thiscall", [PTR], I32, "bc_TGModelProperty_GetRefCount", scenarios)


@register("prop.wrappers.TGModelProperty_Cast")
def cast_test():
    scenarios = []
    for root in ("o", "p", "s", "g"):
        scenarios.append(scenario([Root(root)], constructed(root, 0), "object %s" % root, roots()))
    scenarios.append(scenario([0], constructed("o", 0), "null", roots()))
    return function_case(0x6EC5C0, 0x6EC5E0, "cdecl", [PTR], PTR, "bc_TGModelProperty_Cast", scenarios,
                         ret_class="TGModelProperty")


NAMES = ("", "Phaser 1", "caf\xe9", "x" * 40, "x" * 31, "x" * 32)


@register("prop.wrappers.TGModelProperty_Create")
def create_test():
    scenarios = [scenario([narrow(name)], [], "name %r" % name[:8]) for name in NAMES]
    return function_case(0x6EC5F0, 0x6EC63D, "cdecl", [Buf(io="in")], PTR, "bc_TGModelProperty_Create", scenarios,
                         ret_class="TGModelProperty", probes=[next_id_probe(), name_state("ret")])


@register("prop.wrappers.SubsystemProperty_Create")
def create_subsystem_test():
    scenarios = [scenario([narrow(name)], [], "name %r" % name[:8]) for name in NAMES]
    return function_case(0x692420, 0x69246D, "cdecl", [Buf(io="in")], PTR, "bc_SubsystemProperty_Create", scenarios,
                         ret_class="SubsystemProperty", probes=[next_id_probe(), name_state("ret")])


@register("prop.wrappers.TGObject_GetIndentString")
def indent_test():
    scenarios = [scenario([level], [], "level %d" % level) for level in (0, 1, 2, 3, 15, 16, 63, 64, 65, 100, 0x7FFFFFFF)]
    return function_case(0x6F1680, 0x6F169A, "cdecl", [I32], PTR, "bc_TGObject_GetIndentString", scenarios,
                         probes=[narrow_text()])


@register("prop.wrappers.TGObject_GetIndentString_with_a_negative_level_reaches_its_trap")
def indent_negative_test():
    lib = native_lib()
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_TGObject_GetIndentString.argtypes = [ctypes.c_int32]
    lib.bc_TGObject_GetIndentString.restype = ctypes.c_char_p
    lib.bc_trap_clear()
    assert lib.bc_TGObject_GetIndentString(-1) == b""
    assert lib.bc_trap_count() == 1 and lib.bc_trap_address(0) == 0x6F1680


# Planted defects ---------------------------------------------------------------------------------------------------

@register("prop.wrappers.planted_defect_GetIndentString_takes_the_wrong_step")
def planted_indent():
    scenarios = [scenario([3], [], "level 3")]
    text = failing_text(function_case(0x6F1680, 0x6F169A, "cdecl", [I32], PTR, "bc_TGObject_GetIndentString_defect",
                                      scenarios, probes=[narrow_text()]))
    assert "probe returned narrow text: original=" in text, text


@register("prop.wrappers.planted_defect_IsTypeOf_wrapper_asks_for_the_own_type_only")
def planted_is_type_of():
    rows = [("p", 0, (2,))]
    text = failing_text(wrapper_case(0x5C1320, 0x5C139B, "bc_Appc_TGObject_IsTypeOf_defect", rows, [I32],
                                     [integer_probe()]))
    assert "probe integer handed to Python: original=1 native=0" in text, text


@register("prop.wrappers.planted_defect_Create_does_not_set_the_name")
def planted_create():
    scenarios = [scenario([narrow("Phaser 1")], [], "name Phaser 1")]
    text = failing_text(function_case(0x6EC5F0, 0x6EC63D, "cdecl", [Buf(io="in")], PTR,
                                      "bc_TGModelProperty_Create_defect", scenarios, ret_class="TGModelProperty",
                                      probes=[name_state("ret")]))
    assert "ret.name.length: original=8 native=13" in text, text
