#include "TGIconTextureCache.h"

#include <cstring>

// Inline in 0x729b30: the buckets are zero.
TGIconTextureCache::TGIconTextureCache() : count(0), bucketCount(kBucketCount)
{
    buckets = static_cast<TGIconTextureNode**>(operator_new(kBucketCount * sizeof(TGIconTextureNode*)));
    std::memset(buckets, 0, kBucketCount * sizeof(TGIconTextureNode*));
}

// 0x44f050 (the deleting destructor is 0x44f030), inline in 0x729a00. The key texts of all nodes go first, then the table
// of the base class (0x72b0d0 and 0x44efa0, whose ClearNode does nothing) releases the textures and frees the nodes and
// the buckets. The pointer to the buckets stays in the object.
void TGIconTextureCache::Destroy()
{
    for (std::uint32_t i = 0; i < bucketCount; ++i) {
        for (TGIconTextureNode* node = buckets[i]; node != nullptr; node = node->next)
            operator_delete(node->key);
    }
    for (std::uint32_t i = 0; i < bucketCount; ++i) {
        while (TGIconTextureNode* node = buckets[i]) {
            buckets[i] = node->next;
            ni::Release(node->value);
            operator_delete(node);
        }
    }
    count = 0;
    operator_delete(buckets);
}

// 0x44edb0
std::uint32_t TGIconTextureCache::Hash(const char* key)
{
    std::uint32_t hash = 0;
    for (const char* p = key; *p != 0; ++p)
        hash = hash * 33 + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(*p)));
    return hash % bucketCount;
}

// 0x44ede0
bool TGIconTextureCache::KeysEqual(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        if (static_cast<unsigned char>(*a) != static_cast<unsigned char>(*b))
            return false;
        if (*a == 0)
            return true;
    }
}

// 0x44ee40. The key is copied and the node takes the reference of the value that the caller gave.
void TGIconTextureCache::SetNode(TGIconTextureNode* node, const char* key, NiImage* owned)
{
    node->key = NewStringCopy(key);
    if (node->value != owned) {
        ni::Release(node->value);
        node->value = owned;
        ni::Acquire(owned);
    }
    ni::Release(owned);
}

// 0x44ef10
void TGIconTextureCache::ClearNode(TGIconTextureNode* node) { operator_delete(node->key); }

// The chain walk that 0x72bfe0 does inline.
TGIconTextureNode* TGIconTextureCache::FindNode(const char* key)
{
    for (TGIconTextureNode* node = buckets[Hash(key)]; node != nullptr; node = node->next) {
        if (KeysEqual(key, node->key))
            return node;
    }
    return nullptr;
}

// 0x44ebd0. The caller gave one reference of the value; the function releases it at the end.
void TGIconTextureCache::Insert(const char* key, NiImage* owned)
{
    std::uint32_t bucket = Hash(key);
    for (TGIconTextureNode* existing = buckets[bucket]; existing != nullptr; existing = existing->next) {
        if (KeysEqual(key, existing->key)) {
            ClearNode(existing);
            ni::Acquire(owned);
            SetNode(existing, key, owned);
            ni::Release(owned);
            return;
        }
    }
    TGIconTextureNode* node = static_cast<TGIconTextureNode*>(operator_new(sizeof(TGIconTextureNode)));
    node->value = nullptr;
    ni::Acquire(owned);
    SetNode(node, key, owned);
    node->next = buckets[bucket];
    buckets[bucket] = node;
    ++count;
    ni::Release(owned);
}
