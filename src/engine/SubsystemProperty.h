#pragma once
#include <cstdint>

#include "ExeLayout.h"
#include "NiPoint3.h"
#include "TGModelProperty.h"
#include "TGPoint3.h"

class NiNode;

// A ship subsystem template: the base of all subsystem property classes (E0049).
// The virtual functions follow the vtable at 0x895384 (E0054).
class SubsystemProperty : public TGModelProperty {
public:
    static constexpr int kObjType = 0x812b;  // CT_SUBSYSTEM_PROPERTY

    explicit SubsystemProperty(int requestedId);
    ~SubsystemProperty() override;

    int GetObjType() override;
    bool IsTypeOf(int type) override;
    bool SaveToStream(TGStream* stream) override;
    bool LoadFromStream(TGStream* stream) override;
    const char* GetClassName() override;
    const char* GetSwigPointerName() override;
    const char* GetShadowClassName() override;
    void WriteSource(TGFile* file) override;
    void WriteInit(TGFile* file, const char* propertyName) override;
    void Attach(bool attach, void* scene, void* parent) override;
    bool CopyFrom(const TGModelProperty* source) override;

    void SetMaxCondition(float value);
    void SetCritical(int value);
    void SetTargetable(int value);
    void SetPrimary(int value);
    void SetRepairComplexity(float value);
    void SetDisabledPercentage(float value);
    void SetRadius(float value);
    void SetPosition(float x, float y, float z);
    void SetPosition2D(float x, float y);
    float GetMaxCondition() const;
    int IsCritical() const;
    int IsTargetable() const;
    int IsPrimary() const;
    float GetRepairComplexity() const;
    float GetDisabledPercentage() const;
    float GetRadius() const;
    NiPoint3* GetPosition() const;
    void GetPositionTG(TGPoint3* out) const;
    void GetPosition2D(float* outX, float* outY) const;
    void SetPositionXYZ(float x, float y, float z);
    void SetPosition2DXY(float x, float y);

    static SubsystemProperty* Cast(TGModelProperty* property);
    static SubsystemProperty* Create(const char* name);
    static TGObject* CreateForFactory(int id);
    static void RegisterCreator();

    NiNode* displayNode;        // 0x1c, E0066
    float maxCondition;         // 0x20
    std::uint8_t critical;      // 0x24
    std::uint8_t targetable;    // 0x25
    std::uint8_t primary;       // 0x26
    TGPoint3 position;          // 0x28
    NiPoint2 position2D;        // 0x34
    float repairComplexity;     // 0x3c
    float disabledPercentage;   // 0x40
    float radius;               // 0x44
};

constexpr exe::Field kSubsystemPropertyLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(SubsystemProperty, id, 0x04),
    EXE_FIELD(SubsystemProperty, refCount, 0x08),
    EXE_FIELD_ALIGNED(SubsystemProperty, name, 0x0c, 0x10, 4),
    EXE_FIELD(SubsystemProperty, displayNode, 0x1c),
    EXE_FIELD(SubsystemProperty, maxCondition, 0x20),
    EXE_FIELD(SubsystemProperty, critical, 0x24),
    EXE_FIELD(SubsystemProperty, targetable, 0x25),
    EXE_FIELD(SubsystemProperty, primary, 0x26),
    EXE_FIELD_ALIGNED(SubsystemProperty, position, 0x28, 12, 4),
    EXE_FIELD_ALIGNED(SubsystemProperty, position2D, 0x34, 8, 4),
    EXE_FIELD(SubsystemProperty, repairComplexity, 0x3c),
    EXE_FIELD(SubsystemProperty, disabledPercentage, 0x40),
    EXE_FIELD(SubsystemProperty, radius, 0x44),
};
static_assert(exe::LayoutHolds(kSubsystemPropertyLayout, 0x48), "SubsystemProperty layout");
