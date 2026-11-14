#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "IdHashTable.h"
#include "NiImage.h"
#include "TGIconLocation.h"
#include "TGIconTextureArray.h"

struct TGStream;
class PyWrapperObject;  // the Python object of a group, the SWIG type _p_PyObject; not reconstructed

using TGIconLocationNode = IdHashNode<TGIconLocation*>;
using TGIconLocationTable = IdHashTable<TGIconLocation*>;

// A set of icons that share some textures: the places of the icons by icon number and the textures, which a script
// loads with LoadIconTexture and names by the handle that the call gives. The object has 0x34 bytes. The vtable at
// 0x897bd8 has the slots deleting destructor, SetIconLocation, GetPyWrapper, SaveToStream and LoadFromStream (E0177).
class TGIconGroup {
public:
    static constexpr std::uint32_t kBucketCount = 0x25;
    static constexpr std::size_t kPathBufferSize = 256;  // the stack buffer of LoadIconTexture

    explicit TGIconGroup(const char* initialName);
    virtual ~TGIconGroup();
    virtual void SetIconLocation(std::int32_t number, std::int32_t textureHandle, float x, float y, float width,
                                 float height, std::int32_t rotation, std::int32_t mirror);
    virtual PyWrapperObject* GetPyWrapper();
    virtual bool SaveToStream(TGStream* stream);
    virtual bool LoadFromStream(TGStream* stream);

    void Reset();
    void SetName(const char* newName);
    std::int32_t LoadIconTexture(const char* file, bool flag);
    double GetIconPixelWidth(std::int32_t number);
    double GetIconPixelHeight(std::int32_t number);
    double GetIconScreenWidth(std::int32_t number);
    double GetIconScreenHeight(std::int32_t number);
    bool IsNumValid(std::int32_t number);
    void GoToFirstPos();
    std::int32_t GetNext();
    const char* GetName() const { return name; }
    std::int32_t GetCount() const { return static_cast<std::int32_t>(locations.count); }

    void RotateIconLocation(std::int32_t number, std::int32_t rotation);
    void MirrorIconLocation(std::int32_t number, std::int32_t mirror);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    char* name;                     // 0x04, a copy of the name, or null
    std::int32_t lockCount;         // 0x08, the purge keeps a group while it is not 0 (E0185)
    TGIconLocationTable locations;  // 0x0c, the icon locations by icon number; the count is the group count
    TGIconTextureArray textures;    // 0x1c, the texture properties by texture handle

    // The node that GoToFirstPos and GetNext walk from. One variable for all groups, at 0x99d48c (B0024).
    static TGIconLocationNode* iterator;

private:
    TGIconLocation* FindLocation(std::int32_t number);
};

constexpr exe::Field kTGIconGroupLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconGroup, name, 0x04),
    EXE_FIELD(TGIconGroup, lockCount, 0x08),
    EXE_FIELD_ALIGNED(TGIconGroup, locations, 0x0c, 0x10, 4),
    EXE_FIELD_ALIGNED(TGIconGroup, textures, 0x1c, 0x18, 4),
};
static_assert(exe::LayoutHolds(kTGIconGroupLayout, 0x34), "TGIconGroup layout");

constexpr exe::Field kTGIconLocationNodeLayout[] = {
    EXE_FIELD(TGIconLocationNode, key, 0x00),
    EXE_FIELD(TGIconLocationNode, value, 0x04),
    EXE_FIELD(TGIconLocationNode, next, 0x08),
};
static_assert(exe::LayoutHolds(kTGIconLocationNodeLayout, 0x0c), "TGIconLocationNode layout");

constexpr exe::Field kTGIconLocationTableLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconLocationTable, count, 0x04),
    EXE_FIELD(TGIconLocationTable, bucketCount, 0x08),
    EXE_FIELD(TGIconLocationTable, buckets, 0x0c),
};
static_assert(exe::LayoutHolds(kTGIconLocationTableLayout, 0x10), "TGIconLocationTable layout");
