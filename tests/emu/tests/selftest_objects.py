"""Self-tests for object graphs: field maps, building, reading, comparison, heap and virtual calls.

The original side is hand-written x86 code in the scratch region of the emulator. The exe has
no small function with a list walk that could be described from its instructions alone.
The native side is fixtures/objects.cpp.

Guest code (cdecl):
  TAG_LIST(head, base)  walks head->next and stores base, base+1, ... in each tag; returns the next base
  APPEND(head)          allocates a Node with the pool allocator, sets vtable and tag 99, links it at the end
  NOOP(x)               returns 0
  VIRTUAL(obj)          calls slot 2 of the vtable of obj
  DOUBLE_FREE(ptr)      frees ptr twice with the pool free function
  LEAK()                allocates a Node with the pool allocator and drops it
  BUMP_GLOBAL()         adds 1 to the tag of the global node
"""
import struct

from emuharness import (CSTR, F32, I32, PTR, SCRATCH, U32, VTBL, WSTR, Accessor, Array, ClassDef, Global,
                        GraphReader, GuestSide, HarnessError, Inline, Machine, Member, Model, Native,
                        NativeApi, NativeSide, Obj, ObjectBuilder, ObjectCase, Original, Ptr, Root,
                        Scenario, Stub, UnallowedVirtualCall, build_fixture, compare_graphs,
                        expect_error, pool_stubs, register)
from emuharness.case import EXE_PATH
from emuharness.pe import parse_pe

LIB = build_fixture("objects.cpp")

VT_NODE = SCRATCH + 0xC000
VT_SPECIAL = SCRATCH + 0xC100
VT_HOLDER = SCRATCH + 0xC200
STRDUP, STRLEN, COPY, ALLOC = 0x85AEAD, 0x860440, 0x863840, 0x85B028

NODE = ClassDef("Node", 0x14, [
    Member("vtbl", 0x00, VTBL),
    Member("name", 0x04, CSTR),
    Member("weight", 0x08, F32),
    Member("next", 0x0C, Ptr("Node")),
    Member("tag", 0x10, I32),
], vtable=VT_NODE)
SPECIAL = ClassDef("Special", 0x18, [Member("extra", 0x14, Ptr("Node"))], vtable=VT_SPECIAL, base="Node")
VEC3 = ClassDef("Vec3", 0x0C, [Member("x", 0, F32), Member("y", 4, F32), Member("z", 8, F32)])
HOLDER = ClassDef("Holder", 0x24, [
    Member("vtbl", 0x00, VTBL),
    Member("left", 0x04, Ptr("Node")),
    Member("right", 0x08, Ptr("Node")),
    Member("count", 0x0C, I32),
    Member("items", 0x10, Array(Ptr("Node"), "count")),
    Member("label", 0x14, WSTR),
    Member("pos", 0x18, Inline("Vec3")),
], vtable=VT_HOLDER)
MODEL = Model([NODE, SPECIAL, VEC3, HOLDER], NativeApi(LIB))


def le(value):
    return struct.pack("<I", value)


def vtable(*slots):
    return b"".join(le(s) for s in slots)


CODE_TAG_LIST = SCRATCH + 0x2000
TAG_LIST = bytes.fromhex("8B4C2404 8B442408 85C9 7409 894110 40 8B490C EBF3 C3")
CODE_APPEND = SCRATCH + 0x2100
APPEND = (bytes.fromhex("56 8B742408 6A14 B8B08C7100 FFD0 83C404 C700") + le(VT_NODE)
          + bytes.fromhex("C7401063000000 8B4E0C 85C9 7404 89CE EBF5 89460C 5E C3"))
CODE_NOOP = SCRATCH + 0x2200
NOOP = bytes.fromhex("31C0C3")
CODE_VIRTUAL = SCRATCH + 0x2300
VIRTUAL = bytes.fromhex("8B4C2404 8B01 FF5008 C3")
CODE_DOUBLE_FREE = SCRATCH + 0x2400
DOUBLE_FREE = bytes.fromhex("8B442404 50 B9F08C7100 FFD1 FFD1 83C404 31C0 C3")
CODE_LEAK = SCRATCH + 0x2500
LEAK = (bytes.fromhex("6A14 B8B08C7100 FFD0 83C404 C700") + le(VT_NODE)
        + bytes.fromhex("31C0C3"))
CODE_BUMP = SCRATCH + 0x2600


def global_address():
    """A 0x20-byte window of zeros at the end of the .data section of the exe."""
    with open(EXE_PATH, "rb") as f:
        image = parse_pe(f.read())
    data = next(s for s in image.sections if s.name == ".data")
    addr = image.image_base + data.va + data.mapped_size - 0x40
    window = image.section_bytes(data)[data.mapped_size - 0x40:data.mapped_size - 0x20]
    assert not any(window), "the window for the global object is not empty"
    return addr


GLOBAL_ADDR = global_address()
BUMP_GLOBAL = bytes.fromhex("FF05") + le(GLOBAL_ADDR + 0x10) + bytes.fromhex("C3")

MEMORY = {
    VT_NODE: vtable(STRDUP, STRLEN, COPY),
    VT_SPECIAL: vtable(STRDUP, STRLEN, COPY, ALLOC),
    VT_HOLDER: vtable(STRDUP),
    CODE_TAG_LIST: TAG_LIST, CODE_APPEND: APPEND, CODE_NOOP: NOOP, CODE_VIRTUAL: VIRTUAL,
    CODE_DOUBLE_FREE: DOUBLE_FREE, CODE_LEAK: LEAK, CODE_BUMP: BUMP_GLOBAL,
}


def chain(*tags):
    """A list of Nodes with the given tags. Returns the head."""
    head = None
    for tag in reversed(tags):
        head = Obj("Node", name="n%d" % tag, weight=tag / 2.0, tag=tag, next=head)
    return head


def case(code, size, native, args, ret, scenarios, **kw):
    original = Original(code, size, "cdecl", args, ret)
    return ObjectCase(MODEL, original, Native(LIB, native), scenarios, memory=MEMORY, **kw)


def list_scenarios(lengths, base=7):
    return [Scenario({"list": chain(*range(1, n + 1)) if n else None}, [Root("list"), base],
                     label="length %d" % n) for n in lengths]


def failing_text(case_):
    results = case_.run()
    assert not all(r.ok for r in results), "the planted defect was not found"
    return "\n".join(line for r in results for line in r.lines)


def guest_side():
    machine = Machine(EXE_PATH)
    machine.vtables = dict(MODEL.by_vtable)
    for addr, data in MEMORY.items():
        machine.write(addr, data)
    return GuestSide(machine, MODEL)


def rich_graph():
    """Shared pointers, a cycle, a subclass, an array with a null, a wide string and an inline object."""
    a = Obj("Node", name="Phaser1", weight=1.5, tag=1)
    b = Obj("Special", name="Torpedo", weight=-2.25, tag=2, next=a, extra=a)
    a.values["next"] = b
    holder = Obj("Holder", left=a, right=a, items=[a, b, None], label="Wide é",
                 pos=Obj("Vec3", x=1.0, y=2.0, z=3.5))
    return holder


def check_rich_graph(graph):
    holder = graph["holder"]
    a, b = holder.fields["left"], holder.fields["items"][1]
    assert holder.cls == "Holder" and a.cls == "Node" and b.cls == "Special"
    assert holder.fields["right"] is a and holder.fields["items"][0] is a
    assert holder.fields["items"][2] is None
    assert holder.fields["count"].raw == struct.pack("<i", 3)
    assert holder.fields["label"] == "Wide é"
    assert holder.fields["pos"].obj.fields["z"].raw == struct.pack("<f", 3.5)
    assert a.fields["name"] == "Phaser1" and b.fields["name"] == "Torpedo"
    assert a.fields["next"] is b and b.fields["next"] is a and b.fields["extra"] is a
    assert a.fields["weight"].raw == struct.pack("<f", 1.5)
    assert a.fields["tag"].raw == struct.pack("<i", 1)


@register("selftest.objects.model_rejects_wrong_field_maps")
def model_validation():
    with expect_error(HarnessError, "overlaps"):
        Model([ClassDef("X", 8, [Member("a", 0, I32), Member("b", 2, I32)])])
    with expect_error(HarnessError, "after the size"):
        Model([ClassDef("X", 4, [Member("a", 0, I32), Member("b", 4, I32)])])
    with expect_error(HarnessError, "not in the model"):
        Model([ClassDef("X", 8, [Member("p", 0, Ptr("Y"))])])
    with expect_error(HarnessError, "no VTBL member"):
        Model([ClassDef("X", 8, [Member("a", 4, I32)], vtable=0x1000)])
    with expect_error(HarnessError, "not an integer"):
        Model([ClassDef("X", 16, [Member("n", 0, F32), Member("p", 4, Array(I32, "n"))])])


@register("selftest.objects.build_and_read_back_on_the_guest_side")
def guest_round_trip():
    side = guest_side()
    built = ObjectBuilder(side).build({"holder": rich_graph()})
    check_rich_graph(GraphReader(side).read(built.roots()))
    assert side.machine.read(built.addr["holder"], 4) == le(VT_HOLDER)


@register("selftest.objects.build_and_read_back_on_the_native_side")
def native_round_trip():
    side = NativeSide(MODEL)
    built = ObjectBuilder(side).build({"holder": rich_graph()})
    check_rich_graph(GraphReader(side).read(built.roots()))


@register("selftest.objects.native_accessors_read_and_write_members")
def native_accessors():
    node = ClassDef("Node", 0x14, [
        Member("vtbl", 0x00, VTBL),
        Member("tag", 0x10, I32, native=Accessor("fx_get_tag", "fx_set_tag")),
    ], vtable=VT_NODE)
    side = NativeSide(Model([node], NativeApi(LIB)))
    built = ObjectBuilder(side).build({"n": Obj("Node", tag=41)})
    assert GraphReader(side).read(built.roots())["n"].fields["tag"].raw == struct.pack("<i", 41)
    by_offset = GraphReader(NativeSide(MODEL)).read({"n": (built.addr["n"], "Node")})
    assert by_offset["n"].fields["tag"].raw == struct.pack("<i", 41)


@register("selftest.objects.same_graph_on_both_sides_after_a_call")
def same_graph():
    scenario = Scenario({"holder": rich_graph()}, [Root("holder")], label="rich graph")
    return case(CODE_NOOP, len(NOOP), "fx_noop", [PTR], U32, [scenario])


@register("selftest.objects.list_walk_matches_native")
def tag_list_matches():
    return case(CODE_TAG_LIST, len(TAG_LIST), "fx_tag_list", [PTR, U32], U32, list_scenarios(range(6)))


@register("selftest.objects.allocation_through_the_pool_stub_matches_native")
def append_matches():
    scenarios = [Scenario({"list": chain(*range(1, n + 1))}, [Root("list")], label="length %d" % n)
                 for n in range(1, 5)]
    return case(CODE_APPEND, len(APPEND), "fx_append_node", [PTR], None, scenarios, stubs=pool_stubs())


def read_holder(side, root):
    built = ObjectBuilder(side).build({"holder": root})
    return GraphReader(side).read(built.roots())


@register("selftest.objects.pointer_identity_equal_when_both_share_one_object")
def identity_shared():
    shared = Obj("Node", tag=1)
    graphs = [read_holder(side, Obj("Holder", left=shared, right=shared))
              for side in (guest_side(), NativeSide(MODEL))]
    assert compare_graphs(*graphs) == []


@register("selftest.objects.pointer_identity_differs_for_two_equal_copies")
def identity_copies():
    shared = Obj("Node", tag=1)
    one = read_holder(guest_side(), Obj("Holder", left=shared, right=shared))
    two = read_holder(NativeSide(MODEL),
                      Obj("Holder", left=Obj("Node", tag=1), right=Obj("Node", tag=1)))
    assert compare_graphs(one, two) == [
        "holder.right: original points to the same object as holder.left, native points to another object"]
    assert compare_graphs(two, one) == [
        "holder.right: native points to the same object as holder.left, original points to another object"]


@register("selftest.objects.native_function_that_unshares_a_pointer_is_found")
def identity_defect_in_case():
    shared = Obj("Node", tag=1)
    scenario = Scenario({"holder": Obj("Holder", left=shared, right=shared)}, [Root("holder")])
    text = failing_text(case(CODE_NOOP, len(NOOP), "fx_unshare", [PTR], U32, [scenario]))
    assert ("holder.right: original points to the same object as holder.left, "
            "native points to another object") in text, text


@register("selftest.objects.planted_defect_wrong_field_is_found_with_its_path")
def defect_wrong_field():
    text = failing_text(case(CODE_TAG_LIST, len(TAG_LIST), "fx_tag_list_wrong_field", [PTR, U32], U32,
                             list_scenarios([3])))
    assert "list.tag: original=7 native=1" in text, text
    assert "list.weight: original=0.5 [0x3f000000] native=7.0" in text, text


@register("selftest.objects.planted_defect_leaked_object_is_found")
def defect_leak():
    scenario = Scenario({"list": chain(1, 2, 3)}, [Root("list")])
    text = failing_text(case(CODE_APPEND, len(APPEND), "fx_append_node_leak", [PTR], None, [scenario],
                             stubs=pool_stubs()))
    assert "native leak: 1 object(s) of Node are alive and no root reaches them" in text, text
    assert "live objects of Node: original=4 native=5" in text, text


@register("selftest.objects.planted_defect_different_link_order_is_found")
def defect_order():
    scenario = Scenario({"list": chain(1, 2, 3)}, [Root("list")])
    text = failing_text(case(CODE_APPEND, len(APPEND), "fx_append_node_second", [PTR], None, [scenario],
                             stubs=pool_stubs()))
    assert "list.next.tag: original=2 native=99" in text, text


@register("selftest.objects.virtual_call_not_allowed_names_class_slot_and_target")
def virtual_call_stops():
    scenario = Scenario({"obj": Obj("Special", tag=3)}, [Root("obj")])
    text = failing_text(case(CODE_VIRTUAL, len(VIRTUAL), "fx_noop", [PTR], U32, [scenario]))
    assert "UnallowedVirtualCall" in text and "class Special, slot 2" in text, text
    assert "0x%08x" % COPY in text, text
    side = guest_side()
    obj = ObjectBuilder(side).build({"o": Obj("Node")}).addr["o"]
    with expect_error(UnallowedVirtualCall, "class Node, slot 2", "vtable 0x%08x" % VT_NODE):
        side.machine.call(Original(CODE_VIRTUAL, len(VIRTUAL), "cdecl", [PTR], U32), [obj], fresh=False)


@register("selftest.objects.virtual_call_with_a_stub_is_allowed")
def virtual_call_allowed():
    stub = Stub(lambda ctx: ctx.returns(0), nargs=0)
    scenario = Scenario({"obj": Obj("Special", tag=3)}, [Root("obj")])
    return case(CODE_VIRTUAL, len(VIRTUAL), "fx_noop", [PTR], U32, [scenario], stubs={COPY: stub})


@register("selftest.objects.heap_reports_double_free_and_leak_on_the_original_side")
def heap_faults():
    scenario = Scenario({"obj": Obj("Node", tag=1)}, [Root("obj")])
    text = failing_text(case(CODE_DOUBLE_FREE, len(DOUBLE_FREE), "fx_noop", [PTR], U32, [scenario],
                             stubs=pool_stubs()))
    assert "original heap fault: double free of 0x" in text, text
    text = failing_text(case(CODE_LEAK, len(LEAK), "fx_noop", [], U32, [Scenario({})], stubs=pool_stubs()))
    assert "original leak: Node of 20 bytes" in text, text
    assert "live objects of Node: original=1 native=0" in text, text


@register("selftest.objects.allowed_leak_is_not_a_failure_but_live_counts_still_compare")
def allowed_leak():
    text = failing_text(case(CODE_LEAK, len(LEAK), "fx_noop", [], U32, [Scenario({})],
                             stubs=pool_stubs(), allow_leaks=True))
    assert "original leak" not in text, text
    assert "live objects of Node: original=1 native=0" in text, text


def global_node(tag, **values):
    return Global("gnode", GLOBAL_ADDR, Obj("Node", tag=tag, **values), "fx_init_global")


@register("selftest.objects.global_object_at_exe_address_matches_native")
def global_matches():
    first, second = global_node(5, weight=3.5), global_node(8)
    list_to_global = chain(1, 2)
    list_to_global.values["next"].values["next"] = first
    return case(CODE_BUMP, len(BUMP_GLOBAL), "fx_bump_global", [], U32, [
        Scenario({"list": list_to_global}, [], [first], label="weight set, list points to it"),
        Scenario({"list": chain(1)}, [], [second], label="weight not set"),
    ])


@register("selftest.objects.global_object_is_restored_between_scenarios")
def global_restored():
    side = guest_side()
    ObjectBuilder(side).build({}, [global_node(5, weight=3.5)])
    assert side.machine.read(GLOBAL_ADDR + 8, 4) == struct.pack("<f", 3.5)
    side.machine.reset_state()
    assert side.machine.read(GLOBAL_ADDR + 8, 4) == bytes(4), "data section was not restored"
