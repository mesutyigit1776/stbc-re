#pragma once
#include <cstdint>

#include "ExeLayout.h"

// A scene graph node of the NetImmerse library. The exe class has the run time type name NiNode (RTTI object
// at 0x9a1870, name string at 0x978500), the constructor 0x7e37c0 and the size 0xe8 (E0077). The class is not
// reconstructed. This declaration holds only the members that SubsystemProperty uses.
class NiNode {
public:
    // Slot 10 of the exe vtable at 0x898f2c: the deleting destructor, called with 1.
    virtual void DeleteThis() = 0;
    // Slot 40 (offset 0xa0): removes the child from the child array and gives it back in removed.
    virtual void DetachChild(NiNode** removed, NiNode* child) = 0;

    std::int32_t refCount;  // 0x04
    std::uint32_t reserved[4];  // 0x08 to 0x17, not used here
    NiNode* parent;         // 0x18
};

constexpr exe::Field kNiNodeLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiNode, refCount, 0x04),
    EXE_FIELD_ALIGNED(NiNode, reserved, 0x08, 0x10, 4),
    EXE_FIELD(NiNode, parent, 0x18),
};
static_assert(exe::LayoutHolds(kNiNodeLayout, 0x1c), "NiNode layout");
