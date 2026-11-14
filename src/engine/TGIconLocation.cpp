#include "TGIconLocation.h"

#include "Trap.h"

// Inline in 0x72c260, 0x72c990 and 0x72cc50
TGIconLocation::TGIconLocation() : texture(nullptr), corners{}, pixelWidth(0), pixelHeight(0) {}

// 0x72c480
bool TGIconLocation::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x72c480);
    return false;
}

// 0x72c4e0
bool TGIconLocation::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x72c4e0);
    return false;
}
