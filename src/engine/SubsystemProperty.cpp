#include "SubsystemProperty.h"

#include <cstring>

#include "ClassFactoryTable.h"
#include "NiNode.h"
#include "Trap.h"
#include "X87.h"

using x87::Reg;

namespace {

// Copies the bits of a float. The exe moves these values as integers, so a NaN keeps its bits.
void CopyBits(void* destination, const void* source, std::size_t size) { std::memcpy(destination, source, size); }

}  // namespace

// 0x692470
SubsystemProperty::SubsystemProperty(int requestedId)
    : TGModelProperty(requestedId), displayNode(nullptr), maxCondition(200.0f), critical(0), targetable(1), primary(1),
      repairComplexity(1.0f), disabledPercentage(0.5f), radius(0.25f)
{
    CopyBits(&position, &NiPoint3::zero, sizeof(position));
    CopyBits(&position2D, &NiPoint2::zero, sizeof(position2D));
}

// 0x692680, called by the deleting destructor 0x692650. ~TGModelProperty runs after this body.
SubsystemProperty::~SubsystemProperty()
{
    if (displayNode == nullptr)
        return;
    NiNode* removed = nullptr;
    displayNode->parent->DetachChild(&removed, displayNode);
    if (removed != nullptr && --removed->refCount == 0)
        removed->DeleteThis();
    displayNode = nullptr;
}

// 0x6924e0
int SubsystemProperty::GetObjType() { return kObjType; }

// 0x6924f0
bool SubsystemProperty::IsTypeOf(int type)
{
    return type == kObjType || type == TGModelProperty::kObjType || type == TGObject::kObjType;
}

// 0x6933b0
bool SubsystemProperty::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6933b0);
    return false;
}

// 0x693460
bool SubsystemProperty::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x693460);
    return false;
}

// 0x692520
const char* SubsystemProperty::GetClassName() { return "SubsystemProperty"; }

// 0x692530
const char* SubsystemProperty::GetSwigPointerName() { return "_p_SubsystemProperty"; }

// 0x692540
const char* SubsystemProperty::GetShadowClassName() { return "SubsystemPropertyPtr"; }

// 0x692550
void SubsystemProperty::WriteSource(TGFile*) { BC_NOT_RECONSTRUCTED(0x692550); }

// 0x6927e0
void SubsystemProperty::WriteInit(TGFile*, const char*) { BC_NOT_RECONSTRUCTED(0x6927e0); }

// 0x6929a0
void SubsystemProperty::Attach(bool, void*, void*) { BC_NOT_RECONSTRUCTED(0x6929a0); }

// 0x692710
bool SubsystemProperty::CopyFrom(const TGModelProperty* source)
{
    const SubsystemProperty* from = Cast(const_cast<TGModelProperty*>(source));
    bool copied = TGModelProperty::CopyFrom(source);
    if (copied) {
        maxCondition = StoreF(Reg(from->maxCondition));  // B0004: from is null when source is not a SubsystemProperty
        critical = from->critical;
        targetable = from->targetable;
        primary = from->primary;
        float x, y, z;
        CopyBits(&x, &from->position.x, sizeof(float));
        CopyBits(&y, &from->position.y, sizeof(float));
        CopyBits(&z, &from->position.z, sizeof(float));
        SetPositionXYZ(x, y, z);
        float u, v;
        CopyBits(&u, &from->position2D.x, sizeof(float));
        CopyBits(&v, &from->position2D.y, sizeof(float));
        SetPosition2DXY(u, v);
        repairComplexity = StoreF(Reg(from->repairComplexity));
        disabledPercentage = StoreF(Reg(from->disabledPercentage));
        radius = StoreF(Reg(from->radius));
    }
    return copied;
}

// inline in the Appc wrapper of SubsystemProperty.SetMaxCondition (0x63a970)
void SubsystemProperty::SetMaxCondition(float value) { maxCondition = StoreF(Reg(value)); }

// inline in 0x63a9f0
void SubsystemProperty::SetCritical(int value) { critical = value != 0; }

// inline in 0x63aa70
void SubsystemProperty::SetTargetable(int value) { targetable = value != 0; }

// inline in 0x63aaf0
void SubsystemProperty::SetPrimary(int value) { primary = value != 0; }

// inline in 0x63ab70
void SubsystemProperty::SetRepairComplexity(float value) { repairComplexity = StoreF(Reg(value)); }

// inline in 0x63abf0
void SubsystemProperty::SetDisabledPercentage(float value) { disabledPercentage = StoreF(Reg(value)); }

// inline in 0x63ac70
void SubsystemProperty::SetRadius(float value) { radius = StoreF(Reg(value)); }

// inline in 0x63af30
void SubsystemProperty::SetPosition(float x, float y, float z)
{
    position.x = StoreF(Reg(x));
    position.y = StoreF(Reg(y));
    position.z = StoreF(Reg(z));
}

// inline in 0x63afd0
void SubsystemProperty::SetPosition2D(float x, float y)
{
    position2D.x = StoreF(Reg(x));
    position2D.y = StoreF(Reg(y));
}

// inline in 0x63a630
float SubsystemProperty::GetMaxCondition() const { return maxCondition; }

// inline in 0x63a6a0
int SubsystemProperty::IsCritical() const { return critical; }

// inline in 0x63a720
int SubsystemProperty::IsTargetable() const { return targetable; }

// inline in 0x63a7a0
int SubsystemProperty::IsPrimary() const { return primary; }

// inline in 0x63a820
float SubsystemProperty::GetRepairComplexity() const { return repairComplexity; }

// inline in 0x63a890
float SubsystemProperty::GetDisabledPercentage() const { return disabledPercentage; }

// inline in 0x63a900
float SubsystemProperty::GetRadius() const { return radius; }

// inline in 0x63acf0, the copy that the wrapper hands to Python
NiPoint3* SubsystemProperty::GetPosition() const
{
    NiPoint3* copy = static_cast<NiPoint3*>(operator_new(sizeof(NiPoint3)));
    CopyBits(copy, &position, sizeof(NiPoint3));
    return copy;
}

// 0x63ae20
void SubsystemProperty::GetPositionTG(TGPoint3* out) const { CopyBits(out, &position, sizeof(TGPoint3)); }

// inline in the Appc wrapper of SubsystemProperty.GetPosition2D (0x63ae50), which writes through two pointers
void SubsystemProperty::GetPosition2D(float* outX, float* outY) const
{
    CopyBits(outX, &position2D.x, sizeof(float));
    CopyBits(outY, &position2D.y, sizeof(float));
}

// 0x6927a0
void SubsystemProperty::SetPositionXYZ(float x, float y, float z)
{
    position.x = x;
    position.y = y;
    position.z = z;
}

// 0x6927c0
void SubsystemProperty::SetPosition2DXY(float x, float y)
{
    position2D.x = x;
    position2D.y = y;
}

// 0x6923f0
SubsystemProperty* SubsystemProperty::Cast(TGModelProperty* property)
{
    if (property == nullptr)
        return nullptr;
    return property->IsTypeOf(kObjType) ? static_cast<SubsystemProperty*>(property) : nullptr;
}

// 0x692420
SubsystemProperty* SubsystemProperty::Create(const char* name)
{
    SubsystemProperty* property = new SubsystemProperty(0);
    property->SetName(name);
    return property;
}

// 0x43ff30
TGObject* SubsystemProperty::CreateForFactory(int id) { return new SubsystemProperty(id); }

// inline in 0x43bdf0, at 0x43c5e6
void SubsystemProperty::RegisterCreator()
{
    ClassFactoryTable::Global()->Insert(kObjType, &SubsystemProperty::CreateForFactory);
}
