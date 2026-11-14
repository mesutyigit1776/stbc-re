#include "TGModelPropertyInstance.h"

#include "Trap.h"

// 0x6ed380
TGModelPropertyInstance::TGModelPropertyInstance(TGModelProperty* original, const char* hardpoint)
    : originalTemplate(nullptr), hardpointName(nullptr), property(nullptr)
{
    if (hardpoint != nullptr)
        hardpointName = NewStringCopy(hardpoint);
    originalTemplate = original;
    if (original != nullptr)
        original->IncRefCount();
    property = original->Duplicate();  // B0005: Duplicate returns null when the factory has no creator
}

// 0x6ed440, called by the deleting destructor 0x6ed400
TGModelPropertyInstance::~TGModelPropertyInstance()
{
    if (originalTemplate != nullptr)
        originalTemplate->DecRefCount();
    if (hardpointName != nullptr) {
        operator_delete(hardpointName);
        hardpointName = nullptr;
    }
    property->DecRefCount();  // B0005: no test for null
}

// 0x6ed500
void TGModelPropertyInstance::SaveToStream(TGStream*) { BC_NOT_RECONSTRUCTED(0x6ed500); }

// 0x6ed5c0
void TGModelPropertyInstance::LoadFromStream(TGStream*) { BC_NOT_RECONSTRUCTED(0x6ed5c0); }

// 0x6ed630
bool TGModelPropertyInstance::CheckLinks()
{
    BC_NOT_RECONSTRUCTED(0x6ed630);
    return false;
}

// 0x6ed650
void TGModelPropertyInstance::FixLinks() { BC_NOT_RECONSTRUCTED(0x6ed650); }

// 0x6ed4a0
void TGModelPropertyInstance::SetProperty(TGModelProperty* newProperty)
{
    if (property != nullptr)
        property->DecRefCount();
    property = newProperty;
    if (newProperty != nullptr)
        newProperty->IncRefCount();  // B0002: the copy that AddToSet passes already has the count 1
}

// 0x6ed4d0
void TGModelPropertyInstance::SetOriginalTemplate(TGModelProperty* newTemplate)
{
    if (originalTemplate != nullptr)
        originalTemplate->DecRefCount();
    originalTemplate = newTemplate;
    if (newTemplate != nullptr)
        newTemplate->IncRefCount();
}
