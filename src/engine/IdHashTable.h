#pragma once
#include <cstdint>
#include <cstring>

#include "Crt.h"
#include "ExeLayout.h"

// A hash table from a 32 bit key to one value, chained through the nodes. The exe has two tables of this
// shape with the same code: the object ID registry and the class factory table. Slots 1 to 4 are the
// customization points; nothing overrides them.
template <class Value>
struct IdHashNode {
    std::uint32_t key;     // 0x00
    Value value;           // 0x04
    IdHashNode* next;      // 0x08
};

template <class Value>
class IdHashTable {
public:
    using Node = IdHashNode<Value>;

    explicit IdHashTable(std::uint32_t bucketCountInit);
    virtual ~IdHashTable();
    virtual std::uint32_t Hash(std::uint32_t key);
    virtual bool KeysEqual(std::uint32_t a, std::uint32_t b);
    virtual void SetNode(Node* node, std::uint32_t key, Value nodeValue);
    virtual void ClearNode(Node* node);

    Node* FindNode(std::uint32_t key);
    void Insert(std::uint32_t key, Value nodeValue);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block) { operator_delete(block); }

    std::uint32_t count;        // 0x04
    std::uint32_t bucketCount;  // 0x08
    Node** buckets;             // 0x0c
};

// 0x6f19c0 (class factory table) and 0x6f1ac0 (ID registry): the destructor body. The deleting form frees the table.
template <class Value>
IdHashTable<Value>::~IdHashTable()
{
    for (std::uint32_t i = 0; i < bucketCount; ++i) {
        while (Node* node = buckets[i]) {
            buckets[i] = node->next;
            ClearNode(node);
            operator_delete(node);
        }
    }
    operator_delete(buckets);
    count = 0;
}

// The table constructor: 0x6f08b0 (class factory table, 0x25 buckets), inline in 0x6f0a70 (ID registry, 0x7f7 buckets).
template <class Value>
IdHashTable<Value>::IdHashTable(std::uint32_t bucketCountInit) : count(0), bucketCount(bucketCountInit)
{
    buckets = static_cast<Node**>(operator_new(bucketCountInit * sizeof(Node*)));
    std::memset(buckets, 0, bucketCountInit * sizeof(Node*));
}

// 0x6f1890 (class factory table), 0x6f1960 (ID registry)
template <class Value>
std::uint32_t IdHashTable<Value>::Hash(std::uint32_t key)
{
    return key % bucketCount;
}

// 0x6f18a0 (class factory table), 0x6f1970 (ID registry)
template <class Value>
bool IdHashTable<Value>::KeysEqual(std::uint32_t a, std::uint32_t b)
{
    return a == b;
}

// 0x6f18c0 (class factory table), 0x6f1990 (ID registry)
template <class Value>
void IdHashTable<Value>::SetNode(Node* node, std::uint32_t key, Value nodeValue)
{
    node->key = key;
    node->value = nodeValue;
}

// 0x6f18e0 (class factory table), 0x6f19b0 (ID registry)
template <class Value>
void IdHashTable<Value>::ClearNode(Node*)
{
}

// The chain walk that 0x6f0ee0 (ID registry) and 0x6f13e0 (class factory table) do inline.
template <class Value>
typename IdHashTable<Value>::Node* IdHashTable<Value>::FindNode(std::uint32_t key)
{
    for (Node* node = buckets[Hash(key)]; node != nullptr; node = node->next) {
        if (KeysEqual(key, node->key))
            return node;
    }
    return nullptr;
}

// 0x4468f0 (class factory table), inline in 0x6f0f30 (ID registry)
template <class Value>
void IdHashTable<Value>::Insert(std::uint32_t key, Value nodeValue)
{
    std::uint32_t bucket = Hash(key);
    for (Node* existing = buckets[bucket]; existing != nullptr; existing = existing->next) {
        if (KeysEqual(key, existing->key)) {
            ClearNode(existing);
            SetNode(existing, key, nodeValue);
            return;
        }
    }
    Node* node = static_cast<Node*>(operator_new(sizeof(Node)));
    SetNode(node, key, nodeValue);
    node->next = buckets[bucket];
    buckets[bucket] = node;
    ++count;
}
