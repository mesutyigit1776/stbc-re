// C entry points for the TGList tests.
#include "BcFields.h"
#include "TGList.h"

struct NodeCell {
    TGListNode* node;
};

BC_FIELDS(TGList)
    BC_FIELD(TGList, count) BC_FIELD(TGList, head) BC_FIELD(TGList, tail) BC_FIELD(TGList, freeNodes)
    BC_FIELD(TGList, blocks) BC_FIELD(TGList, blockSize)
BC_FIELDS_END

BC_FIELDS(TGListNode)
    BC_FIELD(TGListNode, data) BC_FIELD(TGListNode, next) BC_FIELD(TGListNode, prev)
BC_FIELDS_END

BC_FIELDS(TGListBlock)
    BC_FIELD(TGListBlock, nextBlock) BC_FIELD(TGListBlock, nodes)
BC_FIELDS_END

BC_FIELDS(NodeCell)
    BC_FIELD(NodeCell, node)
BC_FIELDS_END

extern "C" {

TGList* bc_new_TGList() { return static_cast<TGList*>(operator_new(sizeof(TGList))); }
TGListNode* bc_new_TGListNode() { return static_cast<TGListNode*>(operator_new(sizeof(TGListNode))); }
TGListBlock* bc_new_TGListBlock() { return static_cast<TGListBlock*>(operator_new(sizeof(TGListBlock))); }
NodeCell* bc_new_NodeCell() { return static_cast<NodeCell*>(operator_new(sizeof(NodeCell))); }

void bc_TGList_FreeNode(TGList* list, TGListNode* node) { list->FreeNode(node); }
void* bc_TGList_RemoveTail(TGList* list) { return list->RemoveTail(); }
void bc_TGList_AllocNode(TGList* list, NodeCell* out) { list->AllocNode(&out->node); }

}
