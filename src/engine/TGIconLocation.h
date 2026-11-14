#pragma once
#include <cstddef>
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "NiRefObject.h"

struct TGStream;

// The place of one icon in the texture of its group. The record has 0x30 bytes and is made inline in
// TGIconGroup::SetIconLocation (0x72c260, E0178). The texture coordinates of the four corners are the 8 floats at
// +8: two for each corner. The vtable at 0x897c0c has slots that are not reconstructed.
class TGIconLocation {
public:
    TGIconLocation();
    virtual bool SaveToStream(TGStream* stream);
    virtual bool LoadFromStream(TGStream* stream);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    NiTextureProperty* texture;  // 0x04, with a reference
    float corners[8];            // 0x08 to 0x27
    float pixelWidth;            // 0x28
    float pixelHeight;           // 0x2c
};

constexpr exe::Field kTGIconLocationLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconLocation, texture, 0x04),
    EXE_FIELD_ALIGNED(TGIconLocation, corners, 0x08, 0x20, 4),
    EXE_FIELD(TGIconLocation, pixelWidth, 0x28),
    EXE_FIELD(TGIconLocation, pixelHeight, 0x2c),
};
static_assert(exe::LayoutHolds(kTGIconLocationLayout, 0x30), "TGIconLocation layout");
