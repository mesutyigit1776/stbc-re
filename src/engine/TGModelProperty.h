#pragma once
#include <cstdint>

#include "ExeLayout.h"
#include "TGObject.h"
#include "TGString.h"

struct TGFile;

// The base of the ship property templates. A property has a reference count and a name (E0047).
// The virtual functions follow the vtable at 0x8961ac (E0052).
class TGModelProperty : public TGObject {
public:
    static constexpr int kObjType = 0x601;

    explicit TGModelProperty(int requestedId);
    ~TGModelProperty() override;

    int GetObjType() override;
    bool IsTypeOf(int type) override;
    bool SaveToStream(TGStream* stream) override;
    bool LoadFromStream(TGStream* stream) override;
    bool CheckLinks() override;
    void FixLinks() override;
    const char* GetClassName() override;
    const char* GetSwigPointerName() override;
    const char* GetShadowClassName() override;

    virtual void WriteSource(TGFile* file);
    virtual void WriteInit(TGFile* file, const char* propertyName);
    virtual bool Edit(void* window);
    virtual void Attach(bool attach, void* scene, void* parent);
    virtual bool CopyFrom(const TGModelProperty* source);

    void IncRefCount();
    void DecRefCount();
    int GetRefCount() const;
    TGString* GetName(TGString* out) const;
    void SetNameString(TGString&& newName);
    void SetName(const char* newName);
    void SetNameW(const TGString& newName);
    TGModelProperty* Duplicate();

    static TGModelProperty* Cast(TGObject* object);
    static TGModelProperty* Create(const char* name);
    static TGObject* CreateForFactory(int id);
    static void RegisterCreator();

    std::int32_t refCount;  // 0x08
    TGString name;          // 0x0c
};

constexpr exe::Field kTGModelPropertyLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGModelProperty, id, 0x04),
    EXE_FIELD(TGModelProperty, refCount, 0x08),
    EXE_FIELD_ALIGNED(TGModelProperty, name, 0x0c, 0x10, 4),
};
static_assert(exe::LayoutHolds(kTGModelPropertyLayout, 0x1c), "TGModelProperty layout");
