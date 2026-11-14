#pragma once
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"

struct TGListNode {
    void* data;          // 0x00
    TGListNode* next;    // 0x04
    TGListNode* prev;    // 0x08
};

struct TGListBlock {
    TGListBlock* nextBlock;   // 0x00
    TGListNode* nodes;        // 0x04
};

// A doubly linked list of pointers with its own pool of nodes. New nodes go to the tail; a freed node goes to the
// head of the free chain.
struct TGList {
    std::int32_t count;       // 0x00
    TGListNode* head;         // 0x04
    TGListNode* tail;         // 0x08
    TGListNode* freeNodes;    // 0x0c
    TGListBlock* blocks;      // 0x10
    std::uint32_t blockSize;  // 0x14

    void Reset();
    void FreeNode(TGListNode* node);
    void* RemoveTail();
    void AllocNode(TGListNode** out);
    void Append(void* data);
    void ReleaseBlocks();
    bool Contains(const void* data) const;
    bool Unlink(void* data);
    void UnlinkNode(TGListNode* node);
};

// Inline in 0x6eb040, 0x5dea10, 0x6edff0 and 0x6e9650: an empty list with the block size 2.
inline void TGList::Reset()
{
    count = 0;
    head = nullptr;
    tail = nullptr;
    freeNodes = nullptr;
    blocks = nullptr;
    blockSize = 2;
}

// 0x6ea8a0
inline void TGList::FreeNode(TGListNode* node)
{
    node->data = nullptr;
    node->next = freeNodes;
    freeNodes = node;
}

// 0x6ea860
inline void* TGList::RemoveTail()
{
    TGListNode* removed = tail;
    TGListNode* previous = removed->prev;
    tail = previous;
    if (previous != nullptr)
        previous->next = nullptr;
    else
        head = nullptr;
    void* data = removed->data;
    removed->data = nullptr;
    removed->next = freeNodes;
    freeNodes = removed;
    --count;
    return data;
}

// 0x6ebca0
inline void TGList::AllocNode(TGListNode** out)
{
    if (freeNodes == nullptr) {
        TGListBlock* block = static_cast<TGListBlock*>(operator_new(sizeof(TGListBlock)));
        block->nextBlock = nullptr;
        block->nodes = static_cast<TGListNode*>(operator_new(blockSize * sizeof(TGListNode)));
        for (std::uint32_t i = 0; i + 1 < blockSize; ++i)
            block->nodes[i].next = &block->nodes[i + 1];
        block->nodes[blockSize - 1].next = nullptr;
        block->nextBlock = blocks;
        blocks = block;
        freeNodes = block->nodes;
    }
    *out = freeNodes;
    freeNodes = freeNodes->next;
}

// Inline in 0x6eb220 (twice) and 0x6eb590: a new node at the tail.
inline void TGList::Append(void* data)
{
    TGListNode* node;
    AllocNode(&node);
    node->data = data;
    node->next = nullptr;
    node->prev = tail;
    if (tail != nullptr)
        tail->next = node;
    else
        head = node;
    tail = node;
    ++count;
}

// Inline in 0x6ee040 and 0x6eb0d0: frees the node arrays and the block records, then clears the header except the
// block size.
inline void TGList::ReleaseBlocks()
{
    TGListBlock* block = blocks;
    while (block != nullptr) {
        TGListBlock* following = block->nextBlock;
        operator_delete(block->nodes);
        operator_delete(block);
        block = following;
    }
    count = 0;
    head = nullptr;
    tail = nullptr;
    freeNodes = nullptr;
    blocks = nullptr;
}

// Inline in 0x6ea220, 0x6ea260 and 0x6e9c80: a walk from the head for the first node that holds data.
inline bool TGList::Contains(const void* data) const
{
    for (const TGListNode* node = head; node != nullptr; node = node->next) {
        if (node->data == data)
            return true;
    }
    return false;
}

// Inline in 0x6e96b0, 0x6e9880 and 0x6e9c80: removes the first node that holds data and puts it on the free chain.
// The tail node goes through RemoveTail (0x6ea860), the others through FreeNode (0x6ea8a0).
inline bool TGList::Unlink(void* data)
{
    TGListNode* node = head;
    while (node != nullptr && node->data != data)
        node = node->next;
    if (node == nullptr)
        return false;
    if (node == head) {
        head = node->next;
        if (head != nullptr)
            head->prev = nullptr;
        else
            tail = nullptr;
        FreeNode(node);
        --count;
    } else if (node == tail) {
        RemoveTail();
    } else {
        TGListNode* previous = node->prev;
        TGListNode* following = node->next;
        if (previous != nullptr)
            previous->next = following;
        if (following != nullptr)
            following->prev = previous;
        FreeNode(node);
        --count;
    }
    return true;
}

// Inline in 0x72a080 and 0x72a210: takes a node of the list out of the chain and puts it on the free chain. The node
// is the head, the tail or a node between.
inline void TGList::UnlinkNode(TGListNode* node)
{
    if (node == head) {
        head = node->next;
        if (head != nullptr)
            head->prev = nullptr;
        else
            tail = nullptr;
        FreeNode(node);
        --count;
    } else if (node == tail) {
        RemoveTail();
    } else {
        TGListNode* previous = node->prev;
        TGListNode* following = node->next;
        if (previous != nullptr)
            previous->next = following;
        if (following != nullptr)
            following->prev = previous;
        FreeNode(node);
        --count;
    }
}

constexpr exe::Field kTGListNodeLayout[] = {
    EXE_FIELD(TGListNode, data, 0x00),
    EXE_FIELD(TGListNode, next, 0x04),
    EXE_FIELD(TGListNode, prev, 0x08),
};
static_assert(exe::LayoutHolds(kTGListNodeLayout, 0x0c), "TGListNode layout");

constexpr exe::Field kTGListBlockLayout[] = {
    EXE_FIELD(TGListBlock, nextBlock, 0x00),
    EXE_FIELD(TGListBlock, nodes, 0x04),
};
static_assert(exe::LayoutHolds(kTGListBlockLayout, 0x08), "TGListBlock layout");

constexpr exe::Field kTGListLayout[] = {
    EXE_FIELD(TGList, count, 0x00),
    EXE_FIELD(TGList, head, 0x04),
    EXE_FIELD(TGList, tail, 0x08),
    EXE_FIELD(TGList, freeNodes, 0x0c),
    EXE_FIELD(TGList, blocks, 0x10),
    EXE_FIELD(TGList, blockSize, 0x14),
};
static_assert(exe::LayoutHolds(kTGListLayout, 0x18), "TGList layout");
