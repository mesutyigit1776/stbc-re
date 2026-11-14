#pragma once
#include <cstdint>

#include "ExeLayout.h"
#include "TGList.h"
#include "TGModelProperty.h"
#include "TGModelPropertyGroup.h"
#include "TGModelPropertyInstance.h"
#include "TGModelPropertyList.h"
#include "TGObject.h"

// The properties of a ship, grouped by hardpoint name (E0048). Each group holds one instance for each template name.
// The virtual functions follow the vtable at 0x893d58 (E0053).
class TGModelPropertySet : public TGObject {
public:
    static constexpr int kObjType = 0x602;

    explicit TGModelPropertySet(int requestedId);
    TGModelPropertySet();
    ~TGModelPropertySet() override;

    int GetObjType() override;
    bool IsTypeOf(int type) override;
    bool SaveToStream(TGStream* stream) override;
    bool LoadFromStream(TGStream* stream) override;
    bool CheckLinks() override;
    void FixLinks() override;
    const char* GetClassName() override;
    const char* GetSwigPointerName() override;
    const char* GetShadowClassName() override;

    TGModelPropertyGroup* FindGroup(const char* hardpoint);
    TGModelPropertyList* GetPropertiesByType(int type);
    bool AddToSet(const char* hardpoint, TGModelProperty* prop);

    static TGObject* CreateForFactory(int id);
    static TGObject* Create(int id);
    static void RegisterCreator();

    TGList* groups;  // 0x08
};

constexpr exe::Field kTGModelPropertySetLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGModelPropertySet, id, 0x04),
    EXE_FIELD(TGModelPropertySet, groups, 0x08),
};
static_assert(exe::LayoutHolds(kTGModelPropertySetLayout, 0x0c), "TGModelPropertySet layout");
