#pragma once
#include <cstdint>

#include "ExeLayout.h"
#include "TGList.h"
#include "TGModelProperty.h"

struct TGStream;

// A filter function: the Python type _TGModelPropertyManager__FilterFunc. ApplyFilters calls it with one property
// (cdecl) and reads its result byte (E0094).
using ModelPropertyFilter = bool (*)(TGModelProperty* property);

// The registry of the property templates. The only instance is the global at 0x999de8. It is a plain object, not
// derived from TGObject, and has the virtual table at 0x8961a0 with two slots (E0050, E0055). The size is 0x64
// bytes (E0095).
class TGModelPropertyManager {
public:
    static constexpr int kGlobalTemplates = 0;  // the list selector that FindByName takes (E0057)
    static constexpr int kLocalTemplates = 1;

    TGModelPropertyManager();
    ~TGModelPropertyManager();

    virtual bool SaveToStream(TGStream* stream);
    virtual bool LoadFromStream(TGStream* stream);

    TGList* GetGlobalList();
    TGList* GetLocalList();
    TGModelProperty* FindByNameAndType(const char* name, int type, int local);
    TGModelProperty* FindByName(const char* name, int local);
    bool RegisterGlobalTemplate(TGModelProperty* property);
    bool RegisterLocalTemplate(TGModelProperty* property);
    bool IsGlobalTemplate(const TGModelProperty* property) const;
    bool IsLocalTemplate(const TGModelProperty* property) const;
    bool RemoveTemplate(int local, TGModelProperty* property, bool destroy);
    void ClearGlobalTemplates();
    void ClearLocalTemplates();

    void RegisterFilter(ModelPropertyFilter filter, const char* name);
    void AddFilter(ModelPropertyFilter filter);
    bool RemoveFilter(ModelPropertyFilter filter);
    bool ApplyFilters(TGModelProperty* property);
    void ClearRegisteredFilters();
    void ClearCurrentFilters();

    static TGModelPropertyManager* Global();
    static void InitGlobal();
    static void ConstructGlobal();
    static void RegisterGlobalCleanup();
    static void DestructGlobal();

    TGList registeredFilters;  // 0x04, records of a filter function and its name (E0094)
    TGList currentFilters;     // 0x1c, filter functions (E0094)
    TGList globalTemplates;    // 0x34, TGModelProperty pointers
    TGList localTemplates;     // 0x4c, TGModelProperty pointers

private:
    bool RegisterTemplate(TGModelProperty* property, int local);
    TGList* ListFor(int local);
    static void ReleaseTemplates(TGList& list);
};

constexpr exe::Field kTGModelPropertyManagerLayout[] = {
    EXE_VPTR(),
    EXE_FIELD_ALIGNED(TGModelPropertyManager, registeredFilters, 0x04, 0x18, 4),
    EXE_FIELD_ALIGNED(TGModelPropertyManager, currentFilters, 0x1c, 0x18, 4),
    EXE_FIELD_ALIGNED(TGModelPropertyManager, globalTemplates, 0x34, 0x18, 4),
    EXE_FIELD_ALIGNED(TGModelPropertyManager, localTemplates, 0x4c, 0x18, 4),
};
static_assert(exe::LayoutHolds(kTGModelPropertyManagerLayout, 0x64), "TGModelPropertyManager layout");
