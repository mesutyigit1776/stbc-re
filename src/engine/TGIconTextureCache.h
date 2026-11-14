#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "NiImage.h"

// One entry of the texture cache: the file path and the texture that was made from it.
struct TGIconTextureNode {
    char* key;                   // 0x00, a copy of the path text
    NiImage* value;            // 0x04, with a reference
    TGIconTextureNode* next;     // 0x08
};

// The cache of the textures by file path. The only instance is inside the icon manager (+0x6c). It is a hash table
// with a text key: the hash is h*33 plus the signed character, modulo the number of buckets (E0188). The exe class
// derives from a table with a number key (vtable 0x88b68c); the vtable of this class is 0x88b6a0 and has the slots
// deleting destructor, Hash, KeysEqual, SetNode and ClearNode. The owner destroys the cache with Destroy (the body of
// the destructor 0x44f050): the destructor of the C++ class does nothing, so that the owner chooses the moment.
class TGIconTextureCache {
public:
    static constexpr std::uint32_t kBucketCount = 0x25;

    TGIconTextureCache();
    virtual ~TGIconTextureCache() = default;
    virtual std::uint32_t Hash(const char* key);
    virtual bool KeysEqual(const char* a, const char* b);
    virtual void SetNode(TGIconTextureNode* node, const char* key, NiImage* owned);
    virtual void ClearNode(TGIconTextureNode* node);

    TGIconTextureNode* FindNode(const char* key);
    void Insert(const char* key, NiImage* owned);
    void Destroy();

    std::uint32_t count;               // 0x04
    std::uint32_t bucketCount;         // 0x08
    TGIconTextureNode** buckets;       // 0x0c
};

constexpr exe::Field kTGIconTextureCacheLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconTextureCache, count, 0x04),
    EXE_FIELD(TGIconTextureCache, bucketCount, 0x08),
    EXE_FIELD(TGIconTextureCache, buckets, 0x0c),
};
static_assert(exe::LayoutHolds(kTGIconTextureCacheLayout, 0x10), "TGIconTextureCache layout");

constexpr exe::Field kTGIconTextureNodeLayout[] = {
    EXE_FIELD(TGIconTextureNode, key, 0x00),
    EXE_FIELD(TGIconTextureNode, value, 0x04),
    EXE_FIELD(TGIconTextureNode, next, 0x08),
};
static_assert(exe::LayoutHolds(kTGIconTextureNodeLayout, 0x0c), "TGIconTextureNode layout");
