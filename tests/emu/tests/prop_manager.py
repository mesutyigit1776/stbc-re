"""Tests of TGModelPropertyManager (0x6e9650 and the functions around it).

The manager is the global object at 0x999de8. Each scenario describes the whole object (the four lists with their
node pools) and the property templates as a graph; the exe and the native library start from equal graphs. Templates
are put into the lists by the register functions of each side (setup calls), or the lists are built by hand. The
allocator, the event tables and the Python proxy lookup are stubs. The atexit function (0x8594e4) is a stub that
records the registered function. The filter functions, the save and the load function are traps in the source.
"""
import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_property_common import *  # noqa: F401,F403,E402
from prop_property_common import (ENVS, PROPERTY_ALLOW, build_case, new_property, property_stubs,  # noqa: E402
                                  registry_for, state_setup, with_constructors, Env)
from prop_common import _pointer_at, _u32_at, failing_text, narrow, orig, span  # noqa: E402
from prop_wrapper_common import CONVERT, HANDLES, NEW_POINTER, WSTATE  # noqa: E402

MANAGER = 0x999DE8
ATEXIT = 0x8594E4
MANAGER_CODE = [
    span(0x6E9530, 0x6E9539), span(0x6E9540, 0x6E9549), span(0x6E9550, 0x6E955B), span(0x6E9560, 0x6E9569),
    span(0x6E9570, 0x6E964C), span(0x6E9650, 0x6E96A7), span(0x6E96B0, 0x6E9870), span(0x6E9880, 0x6E9A40),
    span(0x6E9A50, 0x6E9AF6), span(0x6E9B00, 0x6E9B96), span(0x6E9BA0, 0x6E9BFB), span(0x6E9C00, 0x6E9C5B),
    span(0x6E9C60, 0x6E9C63), span(0x6E9C70, 0x6E9C73), span(0x6E9C80, 0x6E9D84), span(0x6EA220, 0x6EA252),
    span(0x6EA260, 0x6EA292), span(0x6EA860, 0x6EA89D), span(0x6EA8A0, 0x6EA8B5), span(0x6EBCA0, 0x6EBD59),
]
MANAGER_ALLOW = PROPERTY_ALLOW + MANAGER_CODE
ATEXIT_LOG = []


def mcall(first, last, conv, args, ret):
    return orig(first, last, conv, args, ret, allow=MANAGER_ALLOW)


def make_env():
    base = ENVS[53]
    lib = base.lib
    calls = dict(base.setup_calls)
    calls["reg_global"] = SetupCall(mcall(0x6E96B0, 0x6E9870, "thiscall", [PTR, PTR], U8),
                                    Native(lib, "bc_TGModelPropertyManager_RegisterGlobalTemplate"))
    calls["reg_local"] = SetupCall(mcall(0x6E9880, 0x6E9A40, "thiscall", [PTR, PTR], U8),
                                   Native(lib, "bc_TGModelPropertyManager_RegisterLocalTemplate"))
    calls["mctor"] = SetupCall(mcall(0x6E9650, 0x6E96A7, "thiscall", [PTR], PTR),
                               Native(lib, "bc_TGModelPropertyManager_TGModelPropertyManager"))
    return Env(53, lib, base.model, base.cw, calls)


MENV = make_env()


def manager_stubs():
    def record(ctx):
        ATEXIT_LOG.append(ctx.arg(0))
        ctx.returns(0)
    stubs = property_stubs()
    stubs[ATEXIT] = Stub(record, nargs=1)
    return stubs


def reset_logs(machine, lib):
    del ATEXIT_LOG[:]
    lib.bc_atexit_clear()


def atexit_probe():
    """The functions that were registered with atexit, as exe addresses."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return list(ATEXIT_LOG)
        lib = side.lib
        lib.bc_atexit_address.restype = ctypes.c_uint32
        lib.bc_atexit_address.argtypes = [ctypes.c_int32]
        return [lib.bc_atexit_address(i) for i in range(lib.bc_atexit_count())]
    return Probe("functions registered with atexit", read)


def count_probe(*roots):
    """The reference count of each template root. A destroyed template keeps its memory in both heaps."""
    def read(side, addresses, ret):
        out = []
        for name in roots:
            if addresses.get(name):
                out.append(_u32_at(side, addresses[name], "TGModelProperty", "refCount"))
        return out
    return Probe("reference counts of " + ", ".join(roots), read)


def list_member_probe():
    """Which list of the manager the returned pointer is: the name of the member at that address."""
    def read(side, addresses, ret):
        base = addresses["m"]
        for member in side.model.members("TGModelPropertyManager"):
            if isinstance(member.type, Inline) and base + side.offset("TGModelPropertyManager", member) == ret:
                return member.name
        return None
    return Probe("list that the pointer addresses", read)


PROBES = [trap_probe(), next_id_probe(), atexit_probe(), count_probe("a", "b", "c", "d", "e", "n", "s", "t", "u")]


def empty_list(block_size=2):
    return Obj("TemplateList", count=0, head=None, tail=None, freeNodes=None, blocks=None, blockSize=block_size)


def manager_global(**lists):
    """The manager in the state of the constructor, with the given lists instead of empty ones."""
    values = {name: lists.get(name) or empty_list() for name in
              ("registeredFilters", "currentFilters", "globalTemplates", "localTemplates")}
    return Global("m", MANAGER, Obj("TGModelPropertyManager", **values), "bc_init_global_TGModelPropertyManager")


PLAIN = ("a", "b", "c", "d", "e", "n")
SUBS = ("s", "t", "u")


def roots():
    out = {name: Obj("TGModelProperty") for name in PLAIN}
    out.update({name: Obj("SubsystemProperty") for name in SUBS})
    return out


def case(first, last, conv, args, ret, symbol, scenarios, **kw):
    kw.setdefault("stubs", manager_stubs())
    kw.setdefault("probes", PROBES)
    return build_case(MENV, mcall(first, last, conv, args, ret), symbol, scenarios, **kw)


def prefill(local=(), glob=(), more=()):
    """Setup calls: construct the templates (root, name), put them into the lists, then run `more`."""
    pre, built = [], set()
    for root, name in list(local) + list(glob):
        if root not in built:
            pre += new_property(root, 0, name, cls="sctor" if root in SUBS else "ctor")
            built.add(root)
    pre += [Pre("reg_local", [Root("m"), Root(r)]) for r, _ in local]
    pre += [Pre("reg_global", [Root("m"), Root(r)]) for r, _ in glob]
    return pre + list(more)


def scenario(args, pre, label, manager=None):
    rts = roots()
    return Scenario(rts, args, globals=[registry_for([]), manager or manager_global()],
                    pre=with_constructors(rts, pre), setup=state_setup(next_id=100, extra=reset_logs), label=label)


def incref(root, times):
    return [Pre("incref", [Root(root)])] * times


# Contents of a list: the templates (root, name) in registration order.
CONTENTS = {
    "empty": [],
    "one": [("a", "Phaser 1")],
    "two": [("a", "Phaser 1"), ("b", "Phaser 2")],
    "three": [("a", "Phaser 1"), ("b", "Phaser 2"), ("c", "Phaser 3")],
    "five": [("a", "Phaser 1"), ("b", "Phaser 2"), ("c", "Phaser 3"), ("d", "Phaser 4"), ("e", "Phaser 5")],
    "mixed types": [("a", "Phaser 1"), ("s", "Impulse"), ("b", "Phaser 2"), ("t", "Warp")],
    "case and accents": [("a", "Phaser"), ("b", "phaser"), ("c", "caf\xe9"), ("d", ""), ("e", "x" * 40)],
}
OTHER = {"empty": [], "same names": [("d", "Phaser 1"), ("e", "Phaser 2")]}


def both_lists(content, other, kind):
    """The local and global contents of a scenario: `content` goes to the list of `kind`, `other` to the other."""
    local, glob = (content, other) if kind == "l" else (other, content)
    return list(local), list(glob)


# Construction and destruction ----------------------------------------------------------------------------------

def dirty_list(count):
    nodes = [Obj("TemplateNode") for _ in range(count)]
    chain = None
    for node in nodes:
        chain = Obj("TemplateBlock", nextBlock=chain, nodes=node)
    return Obj("TemplateList", count=count, head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
               freeNodes=nodes[-1] if nodes else None, blocks=chain, blockSize=count + 5)


@register("prop.manager.TGModelPropertyManager_TGModelPropertyManager")
def ctor_test():
    scenarios = []
    for count in (0, 1, 3):
        manager = manager_global(registeredFilters=dirty_list(count), currentFilters=dirty_list(count + 1),
                                 globalTemplates=dirty_list(count), localTemplates=dirty_list(count + 2))
        scenarios.append(scenario([Root("m")], [], "dirty lists of %d nodes" % count, manager))
    scenarios.append(scenario([Root("m")], [], "constructed manager"))
    return case(0x6E9650, 0x6E96A7, "thiscall", [PTR], PTR, "bc_TGModelPropertyManager_TGModelPropertyManager",
                scenarios, ret_class="TGModelPropertyManager", allow_leaks=True)  # the old lists are dropped


def hand_filters(count):
    """A list of `count` node blocks of one node each, as the filter functions leave it (nodes without a template)."""
    nodes = [Obj("TemplateNode", prev=None) for _ in range(count)]
    for i, node in enumerate(nodes):
        node.values["next"] = nodes[i + 1] if i + 1 < count else None
        node.values["prev"] = nodes[i - 1] if i else None
    chain = None
    for node in nodes:
        chain = Obj("TemplateBlock", nextBlock=chain, nodes=node)
    return Obj("TemplateList", count=count, head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
               freeNodes=None, blocks=chain, blockSize=2)


def destruction_scenarios(args):
    out = []
    for label, content in CONTENTS.items():
        for other_label, other in OTHER.items():
            local, glob = both_lists(content, other, "l")
            out.append(scenario(args, prefill(local, glob), "local %s, global %s" % (label, other_label)))
            local, glob = both_lists(content, other, "g")
            out.append(scenario(args, prefill(local, glob), "global %s, local %s" % (label, other_label)))
    for filters in (1, 2, 4):
        manager = manager_global(registeredFilters=hand_filters(filters),
                                 currentFilters=hand_filters(filters + 1))
        out.append(scenario(args, prefill([("a", "Phaser 1")], [("b", "Phaser 2")]),
                            "%d registered filter nodes, %d current" % (filters, filters + 1), manager))
    return out


@register("prop.manager.TGModelPropertyManager_destructor")
def destructor_test():
    return case(0x6E9570, 0x6E964C, "thiscall", [PTR], None, "bc_TGModelPropertyManager_DestructorBody",
                destruction_scenarios([Root("m")]))


def global_thunk(name, first, last, symbol, probes=None):
    def build():
        scenarios = [scenario([], prefill(*both_lists(content, [], "l")), "local %s" % label)
                     for label, content in CONTENTS.items()]
        scenarios.append(scenario([], [], "constructed manager"))
        manager = manager_global(globalTemplates=dirty_list(2), localTemplates=dirty_list(3))
        scenarios.append(scenario([], [], "dirty lists", manager))
        return case(first, last, "cdecl", [], None, symbol, scenarios, allow_leaks=True,
                    probes=PROBES if probes is None else probes)
    register("prop.manager.TGModelPropertyManager_" + name)(build)


global_thunk("ConstructGlobal", 0x6E9540, 0x6E9549, "bc_TGModelPropertyManager_ConstructGlobal")
global_thunk("InitGlobal", 0x6E9530, 0x6E9539, "bc_TGModelPropertyManager_InitGlobal")
global_thunk("RegisterGlobalCleanup", 0x6E9550, 0x6E955B, "bc_TGModelPropertyManager_RegisterGlobalCleanup")


@register("prop.manager.TGModelPropertyManager_DestructGlobal")
def destruct_global_test():
    return case(0x6E9560, 0x6E9569, "cdecl", [], None, "bc_TGModelPropertyManager_DestructGlobal",
                destruction_scenarios([]))


@register("prop.manager.the_cleanup_function_that_the_static_initializer_registers_is_the_destructor_thunk")
def cleanup_registration_test():
    init = case(0x6E9530, 0x6E9539, "cdecl", [], None, "bc_TGModelPropertyManager_InitGlobal",
                [scenario([], [], "constructed manager")], allow_leaks=True)
    assert all(r.ok for r in init.run())
    assert ATEXIT_LOG == [0x6E9560], ATEXIT_LOG


# Lookup -----------------------------------------------------------------------------------------------------------

@register("prop.manager.TGModelPropertyManager_GetGlobalList")
def get_global_list_test():
    scenarios = [scenario([Root("m")], prefill(*both_lists(content, [], "g")), "global %s" % label)
                 for label, content in CONTENTS.items()]
    return case(0x6E9C60, 0x6E9C63, "thiscall", [PTR], PTR, "bc_TGModelPropertyManager_GetGlobalList", scenarios,
                probes=PROBES + [list_member_probe()])


@register("prop.manager.TGModelPropertyManager_GetLocalList")
def get_local_list_test():
    scenarios = [scenario([Root("m")], prefill(*both_lists(content, [], "l")), "local %s" % label)
                 for label, content in CONTENTS.items()]
    return case(0x6E9C70, 0x6E9C73, "thiscall", [PTR], PTR, "bc_TGModelPropertyManager_GetLocalList", scenarios,
                probes=PROBES + [list_member_probe()])


QUERIES = ["Phaser 1", "Phaser 3", "phaser 1", "PHASER 1", "Phaser", "Phaser 11", "", "Impulse", "impulse",
           "caf\xe9", "CAF\xc9", "x" * 40, "x" * 39]


def lookup_scenarios(make_args):
    out = []
    for label, content in CONTENTS.items():
        for kind in ("l", "g"):
            local, glob = both_lists(content, OTHER["same names"], kind)
            for name in QUERIES:
                for args in make_args(name, kind):
                    out.append(scenario(args, prefill(local, glob),
                                        "%s in the %s list, %r %s" % (label, "local" if kind == "l" else "global",
                                                                      name[:8], args[2:])))
    return out


@register("prop.manager.TGModelPropertyManager_FindByName")
def find_by_name_test():
    def make(name, kind):
        return [[Root("m"), narrow(name), 1 if kind == "l" else 0], [Root("m"), narrow(name), 0 if kind == "l" else 1]]
    return case(0x6E9B00, 0x6E9B96, "thiscall", [PTR, Buf(io="in"), I32], PTR, "bc_TGModelPropertyManager_FindByName",
                lookup_scenarios(make), ret_class="TGModelProperty")


@register("prop.manager.TGModelPropertyManager_FindByName_list_selector_values")
def find_by_name_selector_test():
    scenarios = []
    for selector in (0, 1, 2, -1, 0x100, 0x7FFFFFFF):
        local, glob = [("a", "Phaser 1")], [("b", "Phaser 1")]
        scenarios.append(scenario([Root("m"), narrow("Phaser 1"), selector], prefill(local, glob),
                                  "selector %d" % selector))
    return case(0x6E9B00, 0x6E9B96, "thiscall", [PTR, Buf(io="in"), I32], PTR, "bc_TGModelPropertyManager_FindByName",
                scenarios, ret_class="TGModelProperty")


@register("prop.manager.TGModelPropertyManager_FindByName_with_a_null_name")
def find_by_name_null_test():
    """The wrapper passes 0 for the Python None. The null name compares as the empty name."""
    scenarios = []
    for label, content in CONTENTS.items():
        for selector in (0, 1):
            local, glob = both_lists(content, [], "l" if selector else "g")
            scenarios.append(scenario([Root("m"), 0, selector], prefill(local, glob), "%s, selector %d" % (label, selector)))
    return case(0x6E9B00, 0x6E9B96, "thiscall", [PTR, PTR, I32], PTR, "bc_TGModelPropertyManager_FindByName",
                scenarios, ret_class="TGModelProperty")


@register("prop.manager.TGModelPropertyManager_FindByNameAndType")
def find_by_name_and_type_test():
    types = (0x601, 0x812B, 0x602, 0x7FFFFFFF)
    names = ("Phaser 1", "phaser 1", "Phaser 3", "Impulse", "", "caf\xe9", "x" * 40)

    def make(name, kind):
        if name not in names:
            return []
        out = [[Root("m"), narrow(name), type_, 1 if kind == "l" else 0] for type_ in types]
        out.append([Root("m"), narrow(name), 0x601, 0 if kind == "l" else 1])
        out.append([Root("m"), narrow(name), 0x812B, 7 if kind == "l" else 0])
        return out
    return case(0x6E9A50, 0x6E9AF6, "thiscall", [PTR, Buf(io="in"), I32, I32], PTR,
                "bc_TGModelPropertyManager_FindByNameAndType", lookup_scenarios(make), ret_class="TGModelProperty")


@register("prop.manager.TGModelPropertyManager_FindByNameAndType_with_a_null_name")
def find_by_name_and_type_null_test():
    scenarios = []
    for label, content in CONTENTS.items():
        for type_ in (0x601, 0x812B):
            local, glob = both_lists(content, [], "l")
            scenarios.append(scenario([Root("m"), 0, type_, 1], prefill(local, glob), "%s, type %#x" % (label, type_)))
    return case(0x6E9A50, 0x6E9AF6, "thiscall", [PTR, PTR, I32, I32], PTR,
                "bc_TGModelPropertyManager_FindByNameAndType", scenarios, ret_class="TGModelProperty")


# Registration -----------------------------------------------------------------------------------------------------

# The template to register: (label, name, class). Its root is "n" for a plain property and "t" for a subsystem one.
NEW_TEMPLATES = [
    ("new name", "Phaser 9", "ctor"), ("same name as the first", "Phaser 1", "ctor"),
    ("same name as the second", "Phaser 2", "ctor"), ("same name as the third", "Phaser 3", "ctor"),
    ("same name as the last", "Phaser 5", "ctor"), ("name that differs in case", "PHASER 1", "ctor"),
    ("empty name", "", "ctor"), ("name with an accent", "caf\xe9", "ctor"), ("name that differs in an accent", "CAF\xc9", "ctor"),
    ("long name", "x" * 40, "ctor"), ("subsystem with an existing plain name", "Phaser 1", "sctor"),
    ("subsystem with an existing subsystem name", "Impulse", "sctor"), ("plain with an existing subsystem name", "Impulse", "ctor"),
    ("subsystem, new name", "Phaser 9", "sctor"),
]


def register_scenarios(kind):
    out = []
    for label, content in CONTENTS.items():
        for other_label, other in OTHER.items():
            local, glob = both_lists(content, other, kind)
            for new_label, name, cls in NEW_TEMPLATES:
                root = "u" if cls == "sctor" else "n"
                for extra in (0, 2):
                    pre = prefill(local, glob)
                    for replaced_root in [r for r, _ in content]:
                        pre += incref(replaced_root, extra)
                    pre += new_property(root, 0, name, cls=cls)
                    out.append(scenario([Root("m"), Root(root)], pre,
                                        "%s in the %s list, other list %s: %s, %d extra references" % (
                                            label, "local" if kind == "l" else "global", other_label, new_label, extra)))
    return out


def register_case(first, last, symbol, kind):
    return case(first, last, "thiscall", [PTR, PTR], U8, symbol, register_scenarios(kind))


@register("prop.manager.TGModelPropertyManager_RegisterGlobalTemplate")
def register_global_test():
    return register_case(0x6E96B0, 0x6E9870, "bc_TGModelPropertyManager_RegisterGlobalTemplate", "g")


@register("prop.manager.TGModelPropertyManager_RegisterLocalTemplate")
def register_local_test():
    return register_case(0x6E9880, 0x6E9A40, "bc_TGModelPropertyManager_RegisterLocalTemplate", "l")


def register_twice_case(first, last, symbol, kind):
    scenarios = []
    for label in ("one", "three"):
        local, glob = both_lists(CONTENTS[label], [], kind)
        scenarios.append(scenario([Root("m"), Root("a")], prefill(local, glob) + incref("a", 2),
                                  "list of %s, the first template again, count 3" % label))
    return case(first, last, "thiscall", [PTR, PTR], U8, symbol, scenarios)


register("prop.manager.RegisterGlobalTemplate_with_a_template_that_is_in_the_list")(
    lambda: register_twice_case(0x6E96B0, 0x6E9870, "bc_TGModelPropertyManager_RegisterGlobalTemplate", "g"))
register("prop.manager.RegisterLocalTemplate_with_a_template_that_is_in_the_list")(
    lambda: register_twice_case(0x6E9880, 0x6E9A40, "bc_TGModelPropertyManager_RegisterLocalTemplate", "l"))


def template_classes_probe(member, expected):
    """The class of each template in a list, from the head. A destroyed object shows the class TGObject."""
    def read(side, addresses, ret):
        model_member = side.model.member("TGModelPropertyManager", member)
        header = addresses["m"] + side.offset("TGModelPropertyManager", model_member)
        node = _pointer_at(side, header, "TemplateList", "head")
        classes = []
        while node:
            classes.append(side.dynamic_class(_pointer_at(side, node, "TemplateNode", "data"), "TGModelProperty"))
            node = _pointer_at(side, node, "TemplateNode", "next")
        assert classes == expected, classes
        return classes
    return Probe("classes of the templates in " + member, read)


def b0006_case(first, last, symbol, kind, member):
    local, glob = both_lists(CONTENTS["two"], [], kind)
    scenarios = [scenario([Root("m"), Root("a")], prefill(local, glob), "the first template again, count 1")]
    probes = PROBES + [template_classes_probe(member, ["TGModelProperty", "TGObject"])]
    return case(first, last, "thiscall", [PTR, PTR], U8, symbol, scenarios, probes=probes, allow_leaks=True)


# B0006: the old entry is the template itself. DecRefCount frees it at count 1, and the function appends the freed object.
register("prop.manager.B0006_RegisterGlobalTemplate_frees_a_template_that_is_in_the_list_and_lists_it_again")(
    lambda: b0006_case(0x6E96B0, 0x6E9870, "bc_TGModelPropertyManager_RegisterGlobalTemplate", "g", "globalTemplates"))
register("prop.manager.B0006_RegisterLocalTemplate_frees_a_template_that_is_in_the_list_and_lists_it_again")(
    lambda: b0006_case(0x6E9880, 0x6E9A40, "bc_TGModelPropertyManager_RegisterLocalTemplate", "l", "localTemplates"))


# Membership, removal and clearing ---------------------------------------------------------------------------------

def membership_scenarios(args):
    out = []
    for label, content in CONTENTS.items():
        for other_label, other in OTHER.items():
            for kind in ("l", "g"):
                local, glob = both_lists(content, other, kind)
                for root in ("a", "b", "e", "n", "s"):
                    out.append(scenario(args + [Root(root)], prefill(local, glob) + new_property("n", 0, "Phaser 1"),
                                        "%s in the %s list, other %s, template %s" % (
                                            label, "local" if kind == "l" else "global", other_label, root)))
    out.append(scenario(args + [0], prefill([("a", "Phaser 1")], [("b", "Phaser 1")]), "null template"))
    return out


@register("prop.manager.TGModelPropertyManager_IsGlobalTemplate")
def is_global_test():
    return case(0x6EA220, 0x6EA252, "thiscall", [PTR, PTR], U8, "bc_TGModelPropertyManager_IsGlobalTemplate",
                membership_scenarios([Root("m")]))


@register("prop.manager.TGModelPropertyManager_IsLocalTemplate")
def is_local_test():
    return case(0x6EA260, 0x6EA292, "thiscall", [PTR, PTR], U8, "bc_TGModelPropertyManager_IsLocalTemplate",
                membership_scenarios([Root("m")]))


@register("prop.manager.TGModelPropertyManager_RemoveTemplate")
def remove_template_test():
    scenarios = []
    for label, content in CONTENTS.items():
        for other_label, other in OTHER.items():
            for kind in ("l", "g"):
                local, glob = both_lists(content, other, kind)
                for selector in (1, 0, 2):
                    if selector == 2 and other_label != "empty":
                        continue
                    for root in ("a", "c", "e", "n", "s"):
                        for destroy in (0, 1):
                            pre = prefill(local, glob) + new_property("n", 0, "Phaser 1")
                            scenarios.append(scenario(
                                [Root("m"), selector, Root(root), destroy], pre,
                                "%s in the %s list, other %s, selector %d, template %s, destroy %d" % (
                                    label, "local" if kind == "l" else "global", other_label, selector, root, destroy)))
    scenarios.append(scenario([Root("m"), 1, 0, 1], prefill([("a", "Phaser 1")], []), "null template, destroy"))
    scenarios.append(scenario([Root("m"), 0, 0, 0], prefill([], [("a", "Phaser 1")]), "null template, keep"))
    return case(0x6E9C80, 0x6E9D84, "thiscall", [PTR, I32, PTR, U8], U8, "bc_TGModelPropertyManager_RemoveTemplate",
                scenarios)


def clear_scenarios(args):
    out = []
    for label, content in CONTENTS.items():
        for other_label, other in OTHER.items():
            for kind in ("l", "g"):
                local, glob = both_lists(content, other, kind)
                for extra in (0, 1):
                    pre = prefill(local, glob)
                    for root in [r for r, _ in content]:
                        pre += incref(root, extra)
                    out.append(scenario(args, pre, "%s in the %s list, other %s, %d extra references" % (
                        label, "local" if kind == "l" else "global", other_label, extra)))
    both = [("a", "Phaser 1"), ("b", "Phaser 2")]
    out.append(scenario(args, prefill(both, both) + incref("a", 1), "the same templates in both lists"))
    return out


@register("prop.manager.TGModelPropertyManager_ClearLocalTemplates")
def clear_local_test():
    return case(0x6E9BA0, 0x6E9BFB, "thiscall", [PTR], None, "bc_TGModelPropertyManager_ClearLocalTemplates",
                clear_scenarios([Root("m")]))


@register("prop.manager.TGModelPropertyManager_ClearGlobalTemplates")
def clear_global_test():
    return case(0x6E9C00, 0x6E9C5B, "thiscall", [PTR], None, "bc_TGModelPropertyManager_ClearGlobalTemplates",
                clear_scenarios([Root("m")]))


# Traps and layout --------------------------------------------------------------------------------------------------

@register("prop.manager.slots_and_filter_functions_that_are_not_reconstructed_reach_their_trap")
def trap_test():
    lib = native_lib()
    lib.bc_trap_address.restype = ctypes.c_uint32
    lib.bc_init_global_TGModelPropertyManager.restype = ctypes.c_void_p
    address = lib.bc_init_global_TGModelPropertyManager()
    rows = {}
    for line in open(os.path.join(REPO_ROOT, "re", "layouts", "TGModelPropertyManager.vtable.tsv")).read().splitlines()[1:]:
        cells = line.split("\t")
        if cells[6] == "trap":
            rows[int(cells[2])] = int(cells[3], 16)
    assert rows == {0: 0x6EA2A0, 1: 0x6EA320}, rows
    expected = {0: 0x6EA2A0, 1: 0x6EA320, 2: 0x6EA0E0, 3: 0x6E9DE0, 4: 0x6E9E50, 5: 0x6E9F40, 6: 0x6EA1A0, 7: 0x6E9D90}
    lib.bc_TGModelPropertyManager_CallTrap.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    for which, trap in sorted(expected.items()):
        lib.bc_trap_clear()
        lib.bc_TGModelPropertyManager_CallTrap(address, which)
        assert lib.bc_trap_count() == 1 and lib.bc_trap_address(0) == trap, (which, hex(trap))


@register("prop.manager.virtual_functions_follow_the_exe_slot_order")
def vtable_order_test():
    lib = native_lib()
    out = (ctypes.c_int32 * 16)()
    count = lib.bc_virtual_slots(7, out)
    rows = [line.split("\t") for line in open(os.path.join(REPO_ROOT, "re", "layouts",
                                                           "TGModelPropertyManager.vtable.tsv")).read().splitlines()[1:]]
    # The class has no virtual destructor: the Itanium ABI numbers the slots from 0, like the exe.
    assert [int(r[2]) for r in rows] == [0, 1] and [out[i] for i in range(count)] == [0, 1], (rows, list(out[:count]))


@register("prop.manager.the_global_instance_is_built_by_the_initializer_in_the_table_of_the_exe")
def initializer_entry_test():
    from emuharness.case import EXE_PATH
    from emuharness.machine import Machine
    machine = Machine(EXE_PATH)
    assert machine.read_u32(0x8D0FEC) == 0x6E9530
    # InitGlobal calls the construction thunk and goes on to the registration function.
    assert machine.read(0x6E9530, 10) == b"\xE8\x0B\x00\x00\x00\xE9\x16\x00\x00\x00"
    # The thunks load the address of the global into ECX and jump to the constructor and the destructor.
    assert machine.read(0x6E9540, 5) == b"\xB9\xE8\x9D\x99\x00" and machine.read(0x6E9560, 5) == b"\xB9\xE8\x9D\x99\x00"
    assert machine.read_u32(0x999DE8) == 0 and machine.read(0x999DE8, 0x64) == bytes(0x64)
    # The registration function pushes the destructor thunk and calls atexit.
    assert machine.read(0x6E9550, 12) == b"\x68\x60\x95\x6E\x00\xE8\x8A\xFF\x16\x00\x59\xC3"


# Planted defects ---------------------------------------------------------------------------------------------------

def defect_text(first, last, conv, args, ret, symbol, scenarios, **kw):
    return failing_text(case(first, last, conv, args, ret, symbol, scenarios, **kw))


@register("prop.manager.planted_defect_FindByName_ignores_the_case")
def planted_find_case():
    scenarios = [scenario([Root("m"), narrow("PHASER 1"), 1], prefill([("a", "Phaser 1")], []),
                          "name that differs in case")]
    text = defect_text(0x6E9B00, 0x6E9B96, "thiscall", [PTR, Buf(io="in"), I32], PTR,
                       "bc_TGModelPropertyManager_FindByName_defect", scenarios, ret_class="TGModelProperty")
    assert "ret: original=null native=a TGModelProperty object" in text, text


@register("prop.manager.planted_defect_RegisterLocalTemplate_does_not_release_the_replaced_template")
def planted_register_release():
    scenarios = [scenario([Root("m"), Root("n")], prefill([("a", "Phaser 1")], []) + new_property("n", 0, "Phaser 1"),
                          "same name, count 1")]
    text = defect_text(0x6E9880, 0x6E9A40, "thiscall", [PTR, PTR], U8,
                       "bc_TGModelPropertyManager_RegisterLocalTemplate_defect", scenarios)
    assert "heap blocks freed by the call: original=" in text, text
    assert "probe reference counts of a, b, c, d, e, n, s, t: original=[1, 1] native=[1, 1]" not in text, text


@register("prop.manager.planted_defect_ClearLocalTemplates_clears_the_global_list")
def planted_clear_list():
    both = [("a", "Phaser 1"), ("b", "Phaser 2")]
    scenarios = [scenario([Root("m")], prefill(both, [("c", "Phaser 3")]), "both lists filled")]
    text = defect_text(0x6E9BA0, 0x6E9BFB, "thiscall", [PTR], None,
                       "bc_TGModelPropertyManager_ClearLocalTemplates_defect", scenarios)
    assert "m.globalTemplates.count: original=1 native=0" in text, text
    assert "m.globalTemplates.head: original=a TemplateNode object native=null" in text, text


@register("prop.manager.planted_defect_destructor_releases_the_templates")
def planted_destructor_release():
    scenarios = [scenario([Root("m")], prefill([("a", "Phaser 1")], []), "one local template")]
    text = defect_text(0x6E9570, 0x6E964C, "thiscall", [PTR], None,
                       "bc_TGModelPropertyManager_DestructorBody_defect", scenarios)
    assert "live objects of TGModelProperty: original=6 native=5" in text, text
    assert "probe reference counts of a, b, c, d, e, n, s, t, u: original=[1, 1, 1, 1, 1, 1, 1, 1, 1] native=[0, 1" in text, text


# The module attribute g_kModelPropertyManager ------------------------------------------------------------------------

CONVERSION_FAILS = 0xDEAD0000


def attribute_stubs():
    """The SWIG pointer conversion hands the Python value on as the object; the value 0xdead0000 fails to convert."""
    def convert(ctx):
        value = ctx.arg(0)
        if value == CONVERSION_FAILS:
            ctx.returns(0xFFFFFFFF)
            return
        ctx.write(ctx.arg(1), struct.pack("<I", value))
        ctx.returns(0)

    def new_pointer(ctx):
        WSTATE["pointer"] = ctx.stack_arg(0)
        ctx.returns(HANDLES["pointer"])
    stubs = manager_stubs()
    stubs.update({CONVERT: Stub(convert, nargs=4), NEW_POINTER: Stub(new_pointer, nargs=2)})
    return stubs


def attribute_reset(machine, lib):
    WSTATE.update(values=(), object=0, double=None, integer=None, pointer=None)
    reset_logs(machine, lib)
    lib.bc_capture_reset()


def attribute_scenario(args, pre, label, with_source=True):
    rts = roots()
    if with_source:
        rts["src"] = Obj("TGModelPropertyManager")
    return Scenario(rts, args, globals=[registry_for([]), manager_global()], pre=with_constructors(rts, pre),
                    setup=state_setup(next_id=100, extra=attribute_reset), label=label)


def attribute_original(first, last, args):
    return orig(first, last, "cdecl", args, U32, allow=MANAGER_ALLOW)


@register("prop.manager.Appc_set_g_kModelPropertyManager")
def set_attribute_test():
    def source(local, glob):
        pre = [Pre("mctor", [Root("src")])]
        for root, name in local + glob:
            pre += new_property(root, 0, name, cls="sctor" if root in SUBS else "ctor")
        pre += [Pre("reg_local", [Root("src"), Root(r)]) for r, _ in local]
        pre += [Pre("reg_global", [Root("src"), Root(r)]) for r, _ in glob]
        return pre
    scenarios = []
    for label, content in CONTENTS.items():
        for other_label, other in (("nothing", []), ("other templates", OTHER["same names"])):
            local, glob = both_lists(content, [(r, n) for r, n in other if r not in {x for x, _ in content}], "l")
            scenarios.append(attribute_scenario([Root("src")], source(local, glob),
                                                "local %s, global %s" % (label, other_label)))
            scenarios.append(attribute_scenario([Root("src")], source(glob, local),
                                                "global %s, local %s" % (label, other_label)))
    scenarios.append(attribute_scenario([CONVERSION_FAILS], [], "the conversion fails"))
    return build_case(MENV, attribute_original(0x65BD40, 0x65BDAB, [PTR]), "bc_Appc_set_g_kModelPropertyManager", scenarios,
                      stubs=attribute_stubs(), probes=PROBES, allow_leaks=True)


def attribute_address_probe():
    """The pointer that the getter hands to Python is the address of the global manager."""
    def read(side, addresses, ret):
        if isinstance(side, GuestSide):
            return WSTATE["pointer"] == addresses["m"]
        side.lib.bc_captured_pointer.restype = ctypes.c_void_p
        return side.lib.bc_captured_pointer() == addresses["m"]
    return Probe("object address handed to Python is the global manager", read)


@register("prop.manager.Appc_get_g_kModelPropertyManager")
def get_attribute_test():
    scenarios = [attribute_scenario([], [], "constructed manager", with_source=False),
                 attribute_scenario([], prefill([("a", "Phaser 1")], [("b", "Phaser 2")]), "filled lists", with_source=False)]
    return build_case(MENV, attribute_original(0x65BDB0, 0x65BDC3, []), "bc_Appc_get_g_kModelPropertyManager", scenarios,
                      stubs=attribute_stubs(), probes=PROBES + [attribute_address_probe()])


@register("prop.manager.planted_defect_the_attribute_setter_leaves_out_the_local_list")
def planted_attribute_setter():
    pre = [Pre("mctor", [Root("src")])] + new_property("a", 0, "Phaser 1") + [Pre("reg_local", [Root("src"), Root("a")])]
    scenarios = [attribute_scenario([Root("src")], pre, "one local template")]
    text = failing_text(build_case(MENV, attribute_original(0x65BD40, 0x65BDAB, [PTR]),
                                   "bc_Appc_set_g_kModelPropertyManager_defect", scenarios, stubs=attribute_stubs(),
                                   probes=PROBES, allow_leaks=True))
    assert "m.localTemplates.count: original=1 native=0" in text, text
