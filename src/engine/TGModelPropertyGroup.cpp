#include "TGModelPropertyGroup.h"

// Inline in 0x6eb220: the list constructor 0x6edff0, then the vtable 0x8961a8 and the empty name
TGModelPropertyGroup::TGModelPropertyGroup() : hardpointName(nullptr) {}

// 0x6ec590, called by the deleting destructor 0x6ec570. The list destructor runs after this body.
TGModelPropertyGroup::~TGModelPropertyGroup()
{
    if (hardpointName != nullptr)
        operator_delete(hardpointName);
    hardpointName = nullptr;
}

// Inline in 0x6eb220
void TGModelPropertyGroup::SetHardpointName(const char* name)
{
    if (hardpointName != nullptr)
        operator_delete(hardpointName);
    hardpointName = nullptr;
    hardpointName = NewStringCopy(name);
}
