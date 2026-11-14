#pragma once
#include "IdHashTable.h"

class TGObject;

// A function that makes an object of one class, with the object ID as its argument.
using Creator = TGObject* (*)(int id);

using ClassFactoryNode = IdHashNode<Creator>;

// The table from a type ID to the creator of that class. The only instance is the global at 0x99a578.
class ClassFactoryTable : public IdHashTable<Creator> {
public:
    static constexpr std::uint32_t kBucketCount = 0x25;

    // 0x6f08b0
    ClassFactoryTable() : IdHashTable(kBucketCount) {}

    static ClassFactoryTable* Global();
    static void InitGlobal();
    static void RegisterDestroyAtExit();
    static void DestroyGlobal();

    // The bit with the value 4 in the byte at 0x99a524 makes the destruction run once.
    static std::uint8_t destroyGuard;
};

constexpr exe::Field kClassFactoryTableLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(ClassFactoryTable, count, 0x04),
    EXE_FIELD(ClassFactoryTable, bucketCount, 0x08),
    EXE_FIELD(ClassFactoryTable, buckets, 0x0c),
};
static_assert(exe::LayoutHolds(kClassFactoryTableLayout, 0x10), "ClassFactoryTable layout");

constexpr exe::Field kClassFactoryNodeLayout[] = {
    EXE_FIELD(ClassFactoryNode, key, 0x00),
    EXE_FIELD(ClassFactoryNode, value, 0x04),
    EXE_FIELD(ClassFactoryNode, next, 0x08),
};
static_assert(exe::LayoutHolds(kClassFactoryNodeLayout, 0x0c), "ClassFactoryNode layout");
