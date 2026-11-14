#include <cstring>
#include <new>

#include "Crt.h"
#include "NiImage.h"
#include "Trap.h"

namespace {

// The block of operator new 0x718cb0: null when the memory is not available.
void* TryNew(std::size_t size)
{
    try {
        return operator_new(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

}  // namespace

// 0x7de680. The palette is freed first; the pixels are freed by 0x822210. The pointers keep their values.
void NiRawImageData::Destroy()
{
    operator_delete(palette);
    operator_delete(pixels);
    --ni::g_objectCount;
}

// 0x7e0900
void NiRawImageData::DeleteThis()
{
    Destroy();
    operator_delete(this);
}

// 0x7de4a0. The pixel block is made by 0x8221f0 and freed by 0x822210 (operator new and delete on +0x20). The format 0
// has a palette of 0x400 bytes, set to zero. The format 1 has 3 bytes and the format 2 has 4 bytes for each pixel. Any
// other format keeps the size and allocates nothing. The sizes are 32 bit products.
bool NiRawImageData::Allocate(std::uint32_t newFormat, std::uint32_t newWidth, std::uint32_t newHeight)
{
    if (newWidth == width && newHeight == height && newFormat == format)
        return true;

    operator_delete(pixels);
    pixels = nullptr;
    operator_delete(palette);
    palette = nullptr;
    format = newFormat;
    width = 0;
    height = 0;
    bytesPerPixel = 0;
    stride = 0;

    switch (newFormat) {
    case 0: {
        palette = static_cast<std::uint8_t*>(TryNew(0x400));
        if (palette == nullptr)
            return false;
        std::memset(palette, 0, 0x400);
        pixels = static_cast<std::uint8_t*>(TryNew(newWidth * newHeight));
        if (pixels == nullptr) {
            operator_delete(palette);
            palette = nullptr;
            return false;
        }
        bytesPerPixel = 1;
        stride = newWidth;
        width = newWidth;
        height = newHeight;
        return true;
    }
    case 1:
        pixels = static_cast<std::uint8_t*>(TryNew(newWidth * newHeight * 3));
        if (pixels == nullptr)
            return false;
        width = newWidth;
        stride = newWidth * 3;
        bytesPerPixel = 3;
        height = newHeight;
        return true;
    case 2:
        pixels = static_cast<std::uint8_t*>(TryNew(newWidth * newHeight * 4));
        if (pixels == nullptr)
            return false;
        width = newWidth;
        bytesPerPixel = 4;
        stride = newWidth * 4;
        height = newHeight;
        return true;
    default:
        width = newWidth;
        format = 0;
        height = newHeight;
        return true;
    }
}
