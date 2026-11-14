#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "NiImage.h"
#include "TGIconGroup.h"
#include "TGIconTextureCache.h"
#include "TGList.h"

struct TGStream;

// The wrapper that GetCamera gives to Python: a pointer to the camera that holds a reference (0x42e6c0).
struct NiCameraPtr {
    NiCamera* camera;
};

// One registered icon group: the name, the script module and the function that loads the group. The vtable at
// 0x897c04 has two slots (SaveToStream, LoadFromStream) that are not reconstructed (E0183).
class TGIconRegistration {
public:
    TGIconRegistration(const char* groupName, const char* moduleName, const char* functionName);
    ~TGIconRegistration();
    virtual bool SaveToStream(TGStream* stream);
    virtual bool LoadFromStream(TGStream* stream);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    char* groupName;     // 0x04
    char* moduleName;    // 0x08
    char* functionName;  // 0x0c
};

// The array of the registrations at the start of the icon manager. The vtable at 0x897bd4 has one slot, the
// deleting destructor 0x72aee0. The array is a pointer array with its own growth rule and no element policy (E0176).
class TGIconRegistrationArray {
public:
    TGIconRegistrationArray();
    virtual ~TGIconRegistrationArray();

    void Append(TGIconRegistration* record);
    void RemoveAt(std::uint32_t index);

    TGIconRegistration** data;  // 0x04
    std::uint32_t capacity;     // 0x08
    std::uint32_t used;         // 0x0c
    std::int32_t count;         // 0x10, the number of records that are not null
    std::uint32_t growBy;       // 0x14
};

// The registry of the icon groups. The only instance is the global at 0x99d1f0 (0x7c bytes). It has the registrations,
// the loaded groups, the screen size and scale that the icon code reads, the camera and the texture cache (E0176).
class TGIconManager {
public:
    TGIconManager();
    ~TGIconManager();

    void RegisterIconGroup(const char* name, const char* module, const char* function);
    TGIconGroup* CreateIconGroup(const char* name);
    void AddIconGroup(TGIconGroup* group);
    bool DeleteIconGroup(const char* name);
    TGIconGroup* GetIconGroup(const char* name);
    TGIconGroup* FindLoadedGroup(const char* name);
    void Purge();
    void SetPixelCenterAdj(float adjustment);
    void SetDisplayInfo(NiCamera* newCamera, float newScale);
    void SetCamera(NiCamera* newCamera);
    NiCameraPtr* GetCameraPtr(NiCameraPtr* out);
    void Draw();
    TGIconTextureCache* GetTextureCache() { return &textureCache; }

    float GetScale() const { return scale; }
    void SetScale(float value) { scale = value; }
    float GetScreenWidth() const { return screenWidth; }
    void SetScreenWidth(float value) { screenWidth = value; }
    float GetScreenHeight() const { return screenHeight; }
    void SetScreenHeight(float value) { screenHeight = value; }
    float GetPixelCenterAdj() const { return pixelCenterAdj; }

    static TGIconManager* Global();
    static void InitGlobal();
    static void ConstructGlobal();
    static void RegisterGlobalCleanup();
    static void DestructGlobal();

    TGIconRegistrationArray registrations;  // 0x00
    TGList groups;                          // 0x18, the loaded groups
    TGList otherList;                       // 0x30, used by the drawing code
    std::uint32_t unused48;                 // 0x48
    std::uint32_t unused4c;                 // 0x4c
    NiCamera* camera;                       // 0x50, with a reference
    float scale;                            // 0x54
    float screenWidth;                      // 0x58
    float screenHeight;                     // 0x5c
    float pixelCenterAdj;                   // 0x60
    float pixelCenterAdjX;                  // 0x64, pixelCenterAdj divided by the screen width
    float pixelCenterAdjY;                  // 0x68, pixelCenterAdj divided by the screen height
    TGIconTextureCache textureCache;        // 0x6c
};

constexpr exe::Field kTGIconRegistrationLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconRegistration, groupName, 0x04),
    EXE_FIELD(TGIconRegistration, moduleName, 0x08),
    EXE_FIELD(TGIconRegistration, functionName, 0x0c),
};
static_assert(exe::LayoutHolds(kTGIconRegistrationLayout, 0x10), "TGIconRegistration layout");

constexpr exe::Field kTGIconManagerLayout[] = {
    EXE_VPTR(),
    {"data", 0x04, 4, 4, offsetof(TGIconManager, registrations.data)},
    {"capacity", 0x08, 4, 4, offsetof(TGIconManager, registrations.capacity)},
    {"used", 0x0c, 4, 4, offsetof(TGIconManager, registrations.used)},
    {"count", 0x10, 4, 4, offsetof(TGIconManager, registrations.count)},
    {"growBy", 0x14, 4, 4, offsetof(TGIconManager, registrations.growBy)},
    EXE_FIELD_ALIGNED(TGIconManager, groups, 0x18, 0x18, 4),
    EXE_FIELD_ALIGNED(TGIconManager, otherList, 0x30, 0x18, 4),
    EXE_FIELD(TGIconManager, unused48, 0x48),
    EXE_FIELD(TGIconManager, unused4c, 0x4c),
    EXE_FIELD(TGIconManager, camera, 0x50),
    EXE_FIELD(TGIconManager, scale, 0x54),
    EXE_FIELD(TGIconManager, screenWidth, 0x58),
    EXE_FIELD(TGIconManager, screenHeight, 0x5c),
    EXE_FIELD(TGIconManager, pixelCenterAdj, 0x60),
    EXE_FIELD(TGIconManager, pixelCenterAdjX, 0x64),
    EXE_FIELD(TGIconManager, pixelCenterAdjY, 0x68),
    EXE_FIELD_ALIGNED(TGIconManager, textureCache, 0x6c, 0x10, 4),
};
static_assert(exe::LayoutHolds(kTGIconManagerLayout, 0x7c), "TGIconManager layout");
