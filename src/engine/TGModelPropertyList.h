#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "TGList.h"

// A list of pointers with its own node pool. GetPropertiesByType returns one that holds property instances; the
// group is derived from it (E0048, E0056). The virtual table at 0x896204 has one slot, the deleting destructor.
class TGModelPropertyList {
public:
    TGModelPropertyList();
    virtual ~TGModelPropertyList();

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    TGList items;            // 0x04
    std::uint32_t unused;    // 0x1c
};

constexpr exe::Field kTGModelPropertyListLayout[] = {
    EXE_VPTR(),
    EXE_FIELD_ALIGNED(TGModelPropertyList, items, 0x04, 0x18, 4),
    EXE_FIELD(TGModelPropertyList, unused, 0x1c),
};
static_assert(exe::LayoutHolds(kTGModelPropertyListLayout, 0x20), "TGModelPropertyList layout");
