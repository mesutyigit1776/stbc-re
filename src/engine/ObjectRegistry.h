#pragma once
#include "IdHashTable.h"

class TGObject;

using ObjectRegistryNode = IdHashNode<TGObject*>;

// The table of all TGObject instances by object ID. The only instance is created by the first TGObject (E0062).
class ObjectRegistry : public IdHashTable<TGObject*> {
public:
    static constexpr std::uint32_t kBucketCount = 0x7f7;

    // inline in 0x6f0a70
    ObjectRegistry() : IdHashTable(kBucketCount) {}
};

constexpr exe::Field kObjectRegistryLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(ObjectRegistry, count, 0x04),
    EXE_FIELD(ObjectRegistry, bucketCount, 0x08),
    EXE_FIELD(ObjectRegistry, buckets, 0x0c),
};
static_assert(exe::LayoutHolds(kObjectRegistryLayout, 0x10), "ObjectRegistry layout");

constexpr exe::Field kObjectRegistryNodeLayout[] = {
    EXE_FIELD(ObjectRegistryNode, key, 0x00),
    EXE_FIELD(ObjectRegistryNode, value, 0x04),
    EXE_FIELD(ObjectRegistryNode, next, 0x08),
};
static_assert(exe::LayoutHolds(kObjectRegistryNodeLayout, 0x0c), "ObjectRegistryNode layout");
