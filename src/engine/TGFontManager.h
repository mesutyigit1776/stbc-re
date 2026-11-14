#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "TGIconManager.h"
#include "TGList.h"

struct TGStream;

// One registered size of a font: the icon registration (the name of the family, the script module and the script
// function) and the size. The vtable at 0x897e64 has two slots, SaveToStream 0x7354d0 and LoadFromStream 0x7358e0,
// that are not reconstructed (E0230).
class TGFontRegistration : public TGIconRegistration {
public:
    TGFontRegistration(const char* familyName, const char* moduleName, const char* functionName, float fontSize);
    bool SaveToStream(TGStream* stream) override;
    bool LoadFromStream(TGStream* stream) override;

    float size;  // 0x10
};

// The registrations and the loaded groups of one font name. The object has no vtable and 0x34 bytes (E0229).
class TGFontFace {
public:
    explicit TGFontFace(const char* familyName);
    ~TGFontFace();

    void AddRegistration(float size, const char* module, const char* function);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    char* name;               // 0x00, a copy of the name, or null
    TGList registrations;     // 0x04, TGFontRegistration pointers
    TGList loadedGroups;      // 0x1c, font group pointers (not reconstructed)
};

// The registry of the fonts. The only instance is the global at 0x99d8e0 (0x24 bytes). The vtable at 0x897e5c has the
// slots SaveToStream 0x735410 and LoadFromStream 0x735500, which are not reconstructed (E0227).
class TGFontManager {
public:
    TGFontManager();
    ~TGFontManager();
    virtual bool SaveToStream(TGStream* stream);
    virtual bool LoadFromStream(TGStream* stream);

    void RegisterFont(const char* name, float size, const char* module, const char* function);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    static TGFontManager* Global();
    static void InitGlobal();
    static void ConstructGlobal();
    static void RegisterGlobalCleanup();
    static void DestructGlobal();

    TGList families;     // 0x04, TGFontFace pointers; new families go to the tail
    char* defaultName;   // 0x1c, a copy of the name of the first registered font, or null
    float defaultSize;   // 0x20
};

constexpr exe::Field kTGFontRegistrationLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGFontRegistration, groupName, 0x04),
    EXE_FIELD(TGFontRegistration, moduleName, 0x08),
    EXE_FIELD(TGFontRegistration, functionName, 0x0c),
    EXE_FIELD(TGFontRegistration, size, 0x10),
};
static_assert(exe::LayoutHolds(kTGFontRegistrationLayout, 0x14), "TGFontRegistration layout");

constexpr exe::Field kTGFontFaceLayout[] = {
    EXE_FIELD(TGFontFace, name, 0x00),
    EXE_FIELD_ALIGNED(TGFontFace, registrations, 0x04, 0x18, 4),
    EXE_FIELD_ALIGNED(TGFontFace, loadedGroups, 0x1c, 0x18, 4),
};
static_assert(exe::LayoutHolds(kTGFontFaceLayout, 0x34), "TGFontFace layout");

constexpr exe::Field kTGFontManagerLayout[] = {
    EXE_VPTR(),
    EXE_FIELD_ALIGNED(TGFontManager, families, 0x04, 0x18, 4),
    EXE_FIELD(TGFontManager, defaultName, 0x1c),
    EXE_FIELD(TGFontManager, defaultSize, 0x20),
};
static_assert(exe::LayoutHolds(kTGFontManagerLayout, 0x24), "TGFontManager layout");
