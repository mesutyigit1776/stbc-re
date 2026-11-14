#pragma once
#include "TGModelPropertyList.h"

// The instances that one hardpoint holds. The group is a property list with the hardpoint name (E0048, E0056).
// The virtual table at 0x8961a8 has one slot, the deleting destructor.
class TGModelPropertyGroup : public TGModelPropertyList {
public:
    TGModelPropertyGroup();
    ~TGModelPropertyGroup() override;

    void SetHardpointName(const char* name);

    char* hardpointName;  // 0x20
};

constexpr exe::Field kTGModelPropertyGroupLayout[] = {
    EXE_VPTR(),
    EXE_FIELD_ALIGNED(TGModelPropertyGroup, items, 0x04, 0x18, 4),
    EXE_FIELD(TGModelPropertyGroup, unused, 0x1c),
    EXE_FIELD(TGModelPropertyGroup, hardpointName, 0x20),
};
static_assert(exe::LayoutHolds(kTGModelPropertyGroupLayout, 0x24), "TGModelPropertyGroup layout");
