"""Tests of the TGList helpers: FreeNode (0x6ea8a0), RemoveTail (0x6ea860) and AllocNode (0x6ebca0).

The lists are built as graphs: nodes linked through next and prev, a free chain and a chain of block records.
The nodes are separate allocations here; AllocNode makes real node arrays, whose first node the block record
points to.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from prop_common import orig, pool_stubs_zeroed  # noqa: E402


def make_list(count, free=0, blocks=0, block_size=2):
    """A list of count nodes (head to tail), a free chain of `free` nodes and `blocks` block records."""
    nodes = [Obj("TGListNode", data=Obj("TGObject", id=i + 1)) for i in range(count)]
    for i, node in enumerate(nodes):
        node.values["next"] = nodes[i + 1] if i + 1 < count else None
        node.values["prev"] = nodes[i - 1] if i > 0 else None
    free_nodes = [Obj("TGListNode", prev=nodes[0] if nodes else None) for _ in range(free)]
    for i, node in enumerate(free_nodes):
        node.values["next"] = free_nodes[i + 1] if i + 1 < free else None
    block_chain = None
    for _ in range(blocks):
        block_chain = Obj("TGListBlock", nextBlock=block_chain, nodes=free_nodes[0] if free_nodes else None)
    return Obj("TGList", count=count, head=nodes[0] if nodes else None, tail=nodes[-1] if nodes else None,
               freeNodes=free_nodes[0] if free_nodes else None, blocks=block_chain, blockSize=block_size), nodes


def list_case(original, symbol, scenarios, ret_class=None, allow_leaks=False):
    return ObjectCase(MODEL, original, Native(LIB, symbol), scenarios, stubs=pool_stubs_zeroed(), ret_class=ret_class,
                      allow_leaks=allow_leaks)


FREE_NODE = orig(0x6EA8A0, 0x6EA8B5, "thiscall", [PTR, PTR], None)
REMOVE_TAIL = orig(0x6EA860, 0x6EA89D, "thiscall", [PTR], PTR)
ALLOC_NODE = orig(0x6EBCA0, 0x6EBD59, "thiscall", [PTR, PTR], None)


@register("prop.list.TGList_FreeNode")
def free_node_test():
    scenarios = []
    for count in (0, 1, 3):
        for free in (0, 1, 2):
            lst, nodes = make_list(count, free)
            node = nodes[-1] if nodes else Obj("TGListNode", data=Obj("TGObject", id=50))
            scenarios.append(Scenario({"l": lst, "n": node}, [Root("l"), Root("n")],
                                      label="%d nodes, %d free" % (count, free)))
    return list_case(FREE_NODE, "bc_TGList_FreeNode", scenarios, allow_leaks=True)  # the node data is dropped


@register("prop.list.TGList_RemoveTail")
def remove_tail_test():
    scenarios = []
    for count in (1, 2, 3, 5):
        for free in (0, 1, 3):
            lst, _ = make_list(count, free, block_size=2)
            scenarios.append(Scenario({"l": lst}, [Root("l")], label="%d nodes, %d free" % (count, free)))
    return list_case(REMOVE_TAIL, "bc_TGList_RemoveTail", scenarios, ret_class="TGObject")


@register("prop.list.TGList_AllocNode")
def alloc_node_test():
    scenarios = []
    for count in (0, 2):
        for free in (0, 1, 2):
            for blocks in (0, 2):
                for size in (1, 2, 5):
                    lst, _ = make_list(count, free, blocks, size)
                    scenarios.append(Scenario({"l": lst, "cell": Obj("NodeCell")}, [Root("l"), Root("cell")],
                                              label="%d nodes, %d free, %d blocks, block size %d"
                                              % (count, free, blocks, size)))
    return list_case(ALLOC_NODE, "bc_TGList_AllocNode", scenarios)


# Planted defects ------------------------------------------------------------------------------------------------

@register("prop.list.planted_defect_new_block_chained_in_reverse_order")
def planted_alloc_order():
    lst, _ = make_list(0, 0, 0, 3)
    text = failing_text(list_case(ALLOC_NODE, "bc_TGList_AllocNode_defect",
                                  [Scenario({"l": lst, "cell": Obj("NodeCell")}, [Root("l"), Root("cell")],
                                            label="block size 3")]))
    assert "l.freeNodes" in text or "cell.node" in text, text


@register("prop.list.planted_defect_freed_node_goes_to_the_end_of_the_free_chain")
def planted_free_order():
    lst, nodes = make_list(2, 2)
    text = failing_text(list_case(FREE_NODE, "bc_TGList_FreeNode_defect",
                                  [Scenario({"l": lst, "n": nodes[-1]}, [Root("l"), Root("n")], label="two free")],
                                  allow_leaks=True))
    assert "l.freeNodes" in text, text
