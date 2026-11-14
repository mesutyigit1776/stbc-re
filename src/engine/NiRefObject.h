#pragma once
#include <cstdint>

#include "ExeLayout.h"

// A NetImmerse object with a reference count. The count is the int at +4 and slot 10 of the exe vtable (offset
// 0x28) deletes the object when it is called with 1 (E0187). The icon code holds objects of this kind only through
// the count. The classes below are not reconstructed: they hold only the members that the icon code reads.
class NiRefObject {
public:
    virtual void DeleteThis() = 0;

    std::int32_t refCount;  // 0x04
};

namespace ni {

inline void Acquire(NiRefObject* object)
{
    if (object != nullptr)
        ++object->refCount;
}

// The code of 0x40cfe0 and of the inline copies: the count falls and the object deletes itself at 0.
inline void Release(NiRefObject* object)
{
    if (object != nullptr && --object->refCount == 0)
        object->DeleteThis();
}

}  // namespace ni

class NiImage;
class TextureManager;

// A NetImmerse object with a name, extra data and controllers. Run time type NiObjectNET (RTTI object 0x9a1500, name text
// at 0x978228, E0247); the constructor is 0x7dac80 (vtable 0x898c48), the destructor body 0x7dad10. The exe object has
// 0x14 bytes.
class NiObjectNET : public NiRefObject {
public:
    NiObjectNET();
    void Destroy();

    char* name;                // 0x08, a block that the object owns
    NiRefObject* extraData;    // 0x0c, with a reference
    NiRefObject* controller;   // 0x10, with a reference
};

// The object that an icon group keeps for each loaded texture: the class NiTextureProperty (RTTI object 0x9a24d8, name text
// at 0x97919c, parent NiProperty 0x9a1528, E0247). The exe object has 0x1c bytes, made by operator new and constructed by
// 0x809fe0. The image is the pointer at +0x18, with a reference.
class NiTextureProperty : public NiObjectNET {
public:
    NiTextureProperty();
    // Slot 10 of the vtable 0x89a288 (0x80a8a0) called with 1: Destroy, then the block is freed.
    void DeleteThis() override;
    // 0x80a1d0, the destructor body: releases the image, then runs the body of NiObjectNET.
    void Destroy();

    std::uint16_t flags;       // 0x14, set to 0 by the constructor
    NiImage* texture;          // 0x18
};

// The drawing object that a camera holds at +0x188: the renderer of the exe, run time type NiD3DRender (RTTI object
// 0x9a1160, name text at 0x976724, E0215). Slot 19 (offset 0x4c) and slot 20 (offset 0x50) give the size of the view in
// pixels (E0186). Slot 53 (offset 0xd4) accepts an image (0x7c2a10, E0206).
class NiD3DRender {
public:
    virtual std::uint32_t GetWidth() = 0;
    virtual std::uint32_t GetHeight() = 0;
    // Slot 53 (offset 0xd4), 0x7c2a10: the renderer asks its texture manager to accept the image (E0206).
    virtual bool AcceptImage(NiImage* image);

    std::uint32_t reserved[0x30];     // 0x04 to 0xc3
    TextureManager* textureManager;   // 0xc4
};

// A camera. The icon manager keeps one by reference count and reads its drawing object (E0186, E0187).
class NiCamera : public NiRefObject {
public:
    std::uint32_t reserved[0x60];  // 0x08 to 0x187
    NiD3DRender* renderer;              // 0x188
};

constexpr exe::Field kNiObjectNETLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiObjectNET, refCount, 0x04),
    EXE_FIELD(NiObjectNET, name, 0x08),
    EXE_FIELD(NiObjectNET, extraData, 0x0c),
    EXE_FIELD(NiObjectNET, controller, 0x10),
};
static_assert(exe::LayoutHolds(kNiObjectNETLayout, 0x14), "NiObjectNET layout");

constexpr exe::Field kNiTexturePropertyLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiTextureProperty, refCount, 0x04),
    EXE_FIELD(NiTextureProperty, name, 0x08),
    EXE_FIELD(NiTextureProperty, extraData, 0x0c),
    EXE_FIELD(NiTextureProperty, controller, 0x10),
    EXE_FIELD(NiTextureProperty, flags, 0x14),
    EXE_FIELD(NiTextureProperty, texture, 0x18),
};
static_assert(exe::LayoutHolds(kNiTexturePropertyLayout, 0x1c), "NiTextureProperty layout");

constexpr exe::Field kNiD3DRenderLayout[] = {
    EXE_VPTR(),
    EXE_FIELD_ALIGNED(NiD3DRender, reserved, 0x04, 0xc0, 4),
    EXE_FIELD(NiD3DRender, textureManager, 0xc4),
};
static_assert(exe::LayoutHolds(kNiD3DRenderLayout, 0xc8), "NiD3DRender layout");

constexpr exe::Field kNiCameraLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiCamera, refCount, 0x04),
    EXE_FIELD_ALIGNED(NiCamera, reserved, 0x08, 0x180, 4),
    EXE_FIELD(NiCamera, renderer, 0x188),
};
static_assert(exe::LayoutHolds(kNiCameraLayout, 0x18c), "NiCamera layout");
