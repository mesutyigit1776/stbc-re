#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "TGModelProperty.h"

struct TGStream;

// One property of a hardpoint: the template that was added and a copy of it (E0048, E0056).
// The virtual table at 0x8961f0 has five slots; only the deleting destructor is reconstructed.
class TGModelPropertyInstance {
public:
    TGModelPropertyInstance(TGModelProperty* original, const char* hardpoint);
    virtual ~TGModelPropertyInstance();
    virtual void SaveToStream(TGStream* stream);
    virtual void LoadFromStream(TGStream* stream);
    virtual bool CheckLinks();
    virtual void FixLinks();

    void SetProperty(TGModelProperty* newProperty);
    void SetOriginalTemplate(TGModelProperty* newTemplate);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    TGModelProperty* originalTemplate;  // 0x04
    char* hardpointName;                // 0x08
    TGModelProperty* property;          // 0x0c
};

constexpr exe::Field kTGModelPropertyInstanceLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGModelPropertyInstance, originalTemplate, 0x04),
    EXE_FIELD(TGModelPropertyInstance, hardpointName, 0x08),
    EXE_FIELD(TGModelPropertyInstance, property, 0x0c),
};
static_assert(exe::LayoutHolds(kTGModelPropertyInstanceLayout, 0x10), "TGModelPropertyInstance layout");
