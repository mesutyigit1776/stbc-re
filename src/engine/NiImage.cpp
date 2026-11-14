#include "NiImage.h"

#include <cstring>
#include <new>

#include "Crt.h"
#include "ImageBoundary.h"
#include "ImageLoaders.h"
#include "Trap.h"

namespace ni {
std::int32_t g_objectCount = 0;          // 0x9a1478
std::uint32_t g_imageCounter = 1;        // 0x9783d0
std::uint32_t g_imageError = 0;          // 0x9a168c
std::uint8_t g_imageFlag2Enabled = 0;    // 0x9a1689
std::uint8_t g_imageFlag4Enabled = 0;    // 0x9a168a
std::uint8_t g_imageListGuard = 0;       // 0x9a11e5
std::uint8_t g_rawAndRendererGuard = 0;  // 0x9a163d
TGList g_rendererList = {};              // 0x9a1660
TGList g_imageList = {};                 // 0x9a11e8
std::uint32_t g_ddImageCount = 0;        // 0x9a123c
NiImage* (*g_imageFactory)() = nullptr;  // 0x9a1694
NiRawImageData* g_scratchRawImage = nullptr;  // 0x9a165c
}  // namespace ni

namespace {

// Inline in the constructors 0x7de3e0 and 0x7e12f0: the base constructor 0x7d87a0 sets the count to 0 and adds 1 to
// the count of all objects.
void InitObject(NiRefObject* object)
{
    object->refCount = 0;
    ++ni::g_objectCount;
}

}  // namespace

// 0x7de3e0
NiRawImageData::NiRawImageData()
{
    InitObject(this);
    width = 0;
    height = 0;
    stride = 0;
    palette = nullptr;
    pixels = nullptr;
    format = 1;
    bytesPerPixel = 3;
}

// 0x7e12f0
NiImage::NiImage()
{
    InitObject(this);
    name = nullptr;
    rawImage = nullptr;
    width = 0;
    height = 0;
    mipLevels = 0;
    reserved20 = 0;
    flags = 0;
    scale = 128.5f;
    id = ni::g_imageCounter;
    ++ni::g_imageCounter;
}

namespace {

// 0x7e0b00. The raw image that every loaded file goes into. The variable holds one reference.
void MakeScratchRawImage()
{
    NiRawImageData* image = static_cast<NiRawImageData*>(operator_new(sizeof(NiRawImageData)));
    image = image != nullptr ? ::new (image) NiRawImageData() : nullptr;
    ni::g_scratchRawImage = image;
    if (image != nullptr)
        ++image->refCount;
}

}  // namespace

// 0x7c4d80 and 0x7e0be0: the functions that the static initializers register with atexit. They free the blocks of the node
// pools and clear the lists once.
void ni::ReleaseImageList()
{
    if ((ni::g_imageListGuard & 1) != 0)
        return;
    ni::g_imageListGuard |= 1;
    g_imageList.ReleaseBlocks();
}

void ni::ReleaseRendererList()
{
    if ((ni::g_rawAndRendererGuard & 2) != 0)
        return;
    ni::g_rawAndRendererGuard |= 2;
    g_rendererList.ReleaseBlocks();
}

// 0x7e0b60
void ni::ReleaseScratchRawImage()
{
    if ((ni::g_rawAndRendererGuard & 1) != 0)
        return;
    ni::g_rawAndRendererGuard |= 1;
    Release(g_scratchRawImage);
}

// 0x7e0ae0 (static initializer)
void ni::InitScratchRawImage()
{
    MakeScratchRawImage();
    crt::AtExit(ReleaseScratchRawImage);
}

// 0x7c4d40
void ni::InitImageList()
{
    g_imageList.Reset();
    crt::AtExit(ReleaseImageList);
}

// 0x7e0ba0
void ni::InitRendererList()
{
    g_rendererList.Reset();
    crt::AtExit(ReleaseRendererList);
}

// 0x7c6490. The exe makes the object with a block of 0x5c bytes and the NiImage constructor; the fields of the class
// NiDDImage follow. The image goes to the list of all images, and the counter of the images rises.
NiDDImage* NiDDImage::Create()
{
    NiDDImage* image = new NiDDImage();
    image->surfaces.Reset();
    image->reserved48 = 0;
    image->reserved50 = 0;
    image->reserved54 = 0;
    image->streamData = nullptr;
    ++ni::g_ddImageCount;
    ni::g_imageList.Append(image);
    return image;
}

// Slot 10 of the vtable 0x898a7c (0x7c4f30).
void NiDDImage::DeleteThis() { g_imageDevice.destroyImage(this); }

// 0x7e1340
void NiImage::Destroy()
{
    if (name != nullptr)
        operator_delete(name);
    ni::Release(rawImage);
    --ni::g_objectCount;
}

// 0x7e1f80
void NiImage::DeleteThis()
{
    Destroy();
    operator_delete(this);
}

// 0x7e15c0
std::uint32_t GetMipLevelCount(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
        return 0;
    std::uint32_t widthShifts = 0;
    while ((width & 1) == 0) {
        width >>= 1;
        ++widthShifts;
    }
    if ((width & 0xFFFFFFFEu) != 0)
        return 0;
    std::uint32_t heightShifts = 0;
    while ((height & 1) == 0) {
        height >>= 1;
        ++heightShifts;
    }
    if ((height & 0xFFFFFFFEu) != 0)
        return 0;
    return heightShifts < widthShifts ? heightShifts : widthShifts;
}

// 0x7e0c50. The name block is replaced by a copy of the path. The result of the factory is a NiDDImage once the renderer
// exists. The flags 2 and 4 depend on the mode and on the kind. The image goes to every renderer; a refusal deletes it.
NiImage* NiImage::CreateFromFile(const char* path, std::uint32_t mode, std::uint32_t mask, std::uint32_t kind)
{
    if (path == nullptr) {
        ni::g_imageError = 5;
        return nullptr;
    }
    if (ni::g_imageFactory == nullptr)
        return nullptr;
    NiImage* image = ni::g_imageFactory();
    if (image == nullptr) {
        ni::g_imageError = 4;
        return nullptr;
    }
    if (image->name != nullptr)
        operator_delete(image->name);
    std::size_t size = std::strlen(path) + 1;
    image->name = static_cast<char*>(operator_new(size));
    std::memcpy(image->name, path, size);

    if (mode == 1 || (mode == 2 && ni::g_imageFlag2Enabled != 0))
        image->flags |= 2;
    if (kind == 1 || (kind == 2 && ni::g_imageFlag4Enabled != 0))
        image->flags |= 4;
    image->mask = mask;

    for (TGListNode* node = ni::g_rendererList.head; node != nullptr;) {
        NiD3DRender* renderer = static_cast<NiD3DRender*>(node->data);
        node = node->next;
        if (!renderer->AcceptImage(image)) {
            image->DeleteThis();
            return nullptr;
        }
    }
    return image;
}

// 0x7c2a10. The image goes to the texture manager when it is a NiDDImage; else the manager gets null (B0036).
bool NiD3DRender::AcceptImage(NiImage* image)
{
    if (image == nullptr || dynamic_cast<NiDDImage*>(image) == nullptr)
        return textureManager->AcceptImage(nullptr);
    return textureManager->AcceptImage(image);
}

// 0x7d6f30. The manager asks the image for a surface on its device. The exe passes the image as the object of 0x7c6370
// also when it is null (B0036).
bool TextureManager::AcceptImage(NiImage* image)
{
    DirectDrawSurface* surface = nullptr;
    TextureFormat* format = nullptr;
    if (image == nullptr) {
        BC_NOT_RECONSTRUCTED(0x7c6370);  // B0036
        return false;
    }
    return static_cast<NiDDImage*>(image)->GetSurface(device, slots, ((flags >> 2) & 1) != 0, 1, &surface, &format);
}

// 0x7c6370. A surface that the image made on the same device with the same format is taken from the list at +0x30. A
// record that is marked dirty fills its surface again. The image makes a new surface when it has none: from the stream data
// (0x7c59e0) or from its file (0x7c5c60).
bool NiDDImage::GetSurface(DirectDrawDevice* device, TextureFormat** slots, bool bit2, std::uint32_t count,
                           DirectDrawSurface** surfaceOut, TextureFormat** formatOut)
{
    *surfaceOut = nullptr;
    if (slots == nullptr)
        return false;
    if ((flags & 1) != 0) {
        for (std::uint32_t index = 0; index < 7; ++index) {
            TextureFormat* format = slots[index];
            *formatOut = format;
            if (format == nullptr)
                continue;
            for (TGListNode* node = surfaces.head; node != nullptr; node = node->next) {
                ImageSurfaceRecord* record = static_cast<ImageSurfaceRecord*>(node->data);
                if (record == nullptr || record->formatId != format->id || record->device != device)
                    continue;
                *surfaceOut = record->surface;
                if (record->dirty != 0) {
                    if (!g_imageDevice.refillSurface(this, *formatOut, record->surface, false))
                        return false;
                    record->dirty = 0;
                }
                index = 7;
                break;
            }
        }
    }
    if (*surfaceOut != nullptr)
        return true;
    if (streamData != nullptr)
        return g_imageDevice.makeFromStreamData(this, device, formatOut, count, surfaceOut, nullptr, slots);
    if (name == nullptr && rawImage == nullptr) {
        ni::g_imageError = 8;
        return false;
    }
    return LoadFile(device, formatOut, count, surfaceOut, nullptr, slots, bit2);
}

namespace {

// The choice of a format in 0x7c5c60 (0x7c5d1a to 0x7c5e8a): the first slot that is not empty in an order that depends on
// the format of the raw image and on the bit 2 of the manager.
TextureFormat* ChooseFormat(NiDDImage* image, TextureFormat** slots, std::uint32_t rawFormat, bool bit2)
{
    std::uint32_t format = rawFormat;
    if (((image->flags >> 1) & 1) != 0 && format == 0)
        format = 1;
    std::int32_t mask = static_cast<std::int32_t>(image->mask);
    if (mask != 7 && !(mask == 6 && format != 0)) {
        if (mask < 0) {
            BC_NOT_RECONSTRUCTED(0x7c5d3e);  // B0037: the exe reads the slot before it tests the index
            return nullptr;
        }
        if (mask <= 7 && slots[mask] != nullptr)
            return slots[mask];
    }
    static const int kBit2Order[3][6] = {{6, 2, 1, 5, 3, 4}, {2, 1, 5, 3, 4, -1}, {5, 3, 4, 2, 1, -1}};
    static const int kOrder[3][6] = {{6, 1, 2, 4, 3, 5}, {1, 2, 4, 3, 5, -1}, {4, 3, 5, 1, 2, -1}};
    if (format > 2)
        return nullptr;
    const int* order = bit2 ? kBit2Order[format] : kOrder[format];
    for (int i = 0; i < 6 && order[i] >= 0; ++i) {
        if (slots[order[i]] != nullptr)
            return slots[order[i]];
    }
    return nullptr;
}

}  // namespace

// 0x7c5c60. The file is read into the raw image of 0x9a165c; the width and the height of the image are those of the raw
// image (E0203). The format of the surface comes from the slots, or from the format that the caller gives. A paletted
// format needs the format 0 of the raw image; the palette goes to the surface through the device. The record of the new
// surface goes to the list of the image. The reference that the function takes on the raw image is given back at each end.
bool NiDDImage::LoadFile(DirectDrawDevice* device, TextureFormat** formatOut, std::uint32_t count,
                         DirectDrawSurface** surfaceOut, TextureFormat* given, TextureFormat** slots, bool bit2)
{
    NiRawImageData* raw = rawImage;
    if (raw != nullptr)
        ++raw->refCount;
    if (name != nullptr) {
        NiRawImageData* loaded = LoadRawImageFile(name);
        if (raw != loaded) {
            ni::Release(raw);
            raw = loaded;
            if (loaded != nullptr)
                ++loaded->refCount;
        }
        if (raw == nullptr) {
            ni::g_imageError = 7;
            return false;
        }
    }
    width = raw->width;
    height = raw->height;
    mipLevels = GetMipLevelCount(width, height);
    if (mipLevels == 0)
        mipLevels = 1;

    const std::uint32_t rawFormat = raw->format;
    TextureFormat* format = slots == nullptr ? given : ChooseFormat(this, slots, rawFormat, bit2);
    *formatOut = format;
    if (format == nullptr) {
        ni::Release(raw);
        return false;
    }
    if (!g_imageDevice.createSurface(this, device, format, surfaceOut, count)) {
        ni::Release(raw);
        return false;
    }

    ImageSurfaceRecord* record = static_cast<ImageSurfaceRecord*>(operator_new(sizeof(ImageSurfaceRecord)));
    record->dirty = 0;
    record->formatId = (*formatOut)->id;
    record->surface = *surfaceOut;
    record->device = device;
    bool filled;
    if ((*formatOut)->paletted != 0) {
        filled = false;
        if (rawFormat != 0) {
            ni::g_imageError = 3;
        } else if (g_imageDevice.fillSurface(this, raw, *formatOut, record->surface)) {
            if (device == nullptr) {
                ni::g_imageError = 0xC;
            } else {
                void* palette = nullptr;  // B0038: the palette object is never released
                if (device->CreatePalette(0x44, raw->palette, &palette, nullptr) != 0 ||
                    record->surface->SetPalette(palette) != 0)
                    ni::g_imageError = 0xB;
                else
                    filled = true;
            }
        }
    } else {
        filled = g_imageDevice.fillSurface(this, raw, *formatOut, record->surface);
    }
    if (!filled) {
        if (record->surface != nullptr)
            record->surface->Release();
        operator_delete(record);
        *surfaceOut = nullptr;
        ni::Release(raw);
        return false;
    }
    surfaces.Append(record);
    *surfaceOut = record->surface;
    flags |= 1;
    ni::Release(raw);
    return true;
}
