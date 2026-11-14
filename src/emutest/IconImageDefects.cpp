// Deliberately wrong variants of the image functions. The emulator tests must reject them.
#include <cstdint>
#include <algorithm>
#include <utility>

#include <cstring>

#include "ImageLoaders.h"
#include "NiImage.h"

namespace {

// A stream that changes what the loader reads, so that the loader acts as if it took another header field. mode 0: the
// width and the height fields are swapped; 1: the width is rounded up to a power of two; 2: the descriptor loses the bit
// of the row order; 3: the descriptor loses the bits of the interleave.
struct FilterStream : ImageStream {
    ImageStream* inner;
    int mode;
    int reads = 0;
    FilterStream(ImageStream* stream, int filter) : inner(stream), mode(filter) {}
    bool IsOpen() override { return inner->IsOpen(); }
    void Read(void* destination, std::uint32_t count) override
    {
        inner->Read(destination, count);
        unsigned char* bytes = static_cast<unsigned char*>(destination);
        ++reads;
        if (count == 0x11) {
            if (mode == 0) {
                std::swap(bytes[12], bytes[14]);
                std::swap(bytes[13], bytes[15]);
            } else if (mode == 1) {
                std::uint32_t width = bytes[12] | (bytes[13] << 8), rounded = 1;
                while (rounded < width)
                    rounded <<= 1;
                bytes[12] = static_cast<unsigned char>(rounded);
                bytes[13] = static_cast<unsigned char>(rounded >> 8);
            }
        } else if (count == 1 && reads == 2) {
            if (mode == 2)
                bytes[0] &= ~0x20;
            else if (mode == 3)
                bytes[0] &= 0x3F;
        }
    }
    void Seek(std::int32_t offset, std::uint32_t whence) override { inner->Seek(offset, whence); }
};

std::uint8_t LoadFiltered(ImageStream* stream, NiRawImageData* raw, int mode)
{
    FilterStream filter(stream, mode);
    return LoadTgaFile(&filter, raw) ? 1 : 0;
}

}  // namespace

extern "C" {

static std::uint32_t RoundUp(std::uint32_t value)
{
    std::uint32_t result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

// Allocate: the width and the height are rounded up to a power of two.
std::uint8_t bc_NiRawImageData_Allocate_defect_round(NiRawImageData* self, std::uint32_t format, std::uint32_t width,
                                                     std::uint32_t height)
{
    return self->Allocate(format, RoundUp(width), RoundUp(height)) ? 1 : 0;
}

// Allocate: the width and the height are swapped.
std::uint8_t bc_NiRawImageData_Allocate_defect_swap(NiRawImageData* self, std::uint32_t format, std::uint32_t width,
                                                    std::uint32_t height)
{
    return self->Allocate(format, height, width) ? 1 : 0;
}

// Allocate: the format 1 has 4 bytes for each pixel.
std::uint8_t bc_NiRawImageData_Allocate_defect_four_bytes(NiRawImageData* self, std::uint32_t format,
                                                          std::uint32_t width, std::uint32_t height)
{
    bool ok = self->Allocate(format == 1 ? 2 : format, width, height);
    if (format == 1)
        self->format = 1;
    return ok ? 1 : 0;
}

// Allocate: an image of the same format and size is made again.
std::uint8_t bc_NiRawImageData_Allocate_defect_always(NiRawImageData* self, std::uint32_t format, std::uint32_t width,
                                                      std::uint32_t height)
{
    self->width = ~width;
    return self->Allocate(format, width, height) ? 1 : 0;
}

// The mip level count takes the floor of the base 2 logarithm of each size, so a size that is not a power of two gets a
// level count.
std::uint32_t bc_GetMipLevelCount_defect_floor(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
        return 0;
    std::uint32_t widthLog = 0, heightLog = 0;
    while (widthLog < 31 && (width >> (widthLog + 1)) != 0)
        ++widthLog;
    while (heightLog < 31 && (height >> (heightLog + 1)) != 0)
        ++heightLog;
    return heightLog < widthLog ? heightLog : widthLog;
}

// The TGA loader. Defect 1: the width and the height fields are swapped. Defect 2: the width is rounded up to a power of
// two. Defect 3: the bit of the row order is ignored. Defect 4: the interleave is ignored. Defect 5: red and blue are
// swapped in every pixel.
std::uint8_t bc_LoadTgaFile_defect_swap(ImageStream* stream, NiRawImageData* raw) { return LoadFiltered(stream, raw, 0); }
std::uint8_t bc_LoadTgaFile_defect_round(ImageStream* stream, NiRawImageData* raw) { return LoadFiltered(stream, raw, 1); }
std::uint8_t bc_LoadTgaFile_defect_row_order(ImageStream* stream, NiRawImageData* raw) { return LoadFiltered(stream, raw, 2); }
std::uint8_t bc_LoadTgaFile_defect_interleave(ImageStream* stream, NiRawImageData* raw) { return LoadFiltered(stream, raw, 3); }

std::uint8_t bc_LoadTgaFile_defect_red_blue(ImageStream* stream, NiRawImageData* raw)
{
    std::uint8_t result = LoadTgaFile(stream, raw) ? 1 : 0;
    if (result != 0 && raw->pixels != nullptr && raw->bytesPerPixel >= 3) {
        for (std::uint32_t y = 0; y < raw->height; ++y) {
            for (std::uint32_t x = 0; x < raw->width; ++x) {
                std::uint8_t* pixel = raw->pixels + y * raw->stride + x * raw->bytesPerPixel;
                std::swap(pixel[0], pixel[2]);
            }
        }
    }
    return result;
}

// The image classes. Each variant is wrong in one way.

// The constructor of NiImage: the scale is 128.0 and not 128.5.
NiImage* bc_NiImage_NiImage_defect_scale(NiImage* self)
{
    ::new (self) NiImage();
    self->scale = 128.0f;
    return self;
}

// NiDDImage::Create: the new image is not put into the list of all images.
NiDDImage* bc_NiDDImage_Create_defect_list()
{
    NiDDImage* image = NiDDImage::Create();
    ni::g_imageList.Unlink(image);
    return image;
}

// CreateFromFile: the flag 2 is set for the mode 2 also when the byte at 0x9a1689 is 0.
NiImage* bc_NiImage_CreateFromFile_defect_flags(const char* path, std::uint32_t mode, std::uint32_t mask, std::uint32_t kind)
{
    NiImage* image = NiImage::CreateFromFile(path, mode, mask, kind);
    if (image != nullptr && mode == 2)
        image->flags |= 2;
    return image;
}

// CreateFromFile: the mask is stored only when the mode is 1.
NiImage* bc_NiImage_CreateFromFile_defect_mask(const char* path, std::uint32_t mode, std::uint32_t mask, std::uint32_t kind)
{
    return NiImage::CreateFromFile(path, mode, mode == 1 ? mask : 0, kind);
}

// The renderer: the image loses the flag 1 that marks a surface.
std::uint8_t bc_NiD3DRender_AcceptImage_defect(NiD3DRender* self, NiImage* image)
{
    bool ok = self->NiD3DRender::AcceptImage(image);
    image->flags &= ~1u;
    return ok ? 1 : 0;
}

// The texture manager takes the bit 1 of its flags, not the bit 2.
std::uint8_t bc_TextureManager_AcceptImage_defect(TextureManager* self, NiImage* image)
{
    std::uint32_t saved = self->flags;
    self->flags = ((saved >> 1) & 1) << 2;
    bool ok = self->TextureManager::AcceptImage(image);
    self->flags = saved;
    return ok ? 1 : 0;
}

// GetSurface: a record of another device is taken too.
std::uint8_t bc_NiDDImage_GetSurface_defect_device(NiDDImage* self, DirectDrawDevice* device, TextureFormat** slots,
                                                  std::uint32_t bit2, std::uint32_t count, DirectDrawSurface** surfaceOut,
                                                  TextureFormat** formatOut)
{
    if (self->surfaces.head != nullptr && self->surfaces.head->data != nullptr)
        device = static_cast<ImageSurfaceRecord*>(self->surfaces.head->data)->device;
    return self->GetSurface(device, slots, (bit2 & 0xFF) != 0, count, surfaceOut, formatOut) ? 1 : 0;
}

// GetSurface: a record that is dirty is not filled again.
std::uint8_t bc_NiDDImage_GetSurface_defect_dirty(NiDDImage* self, DirectDrawDevice* device, TextureFormat** slots,
                                                 std::uint32_t bit2, std::uint32_t count, DirectDrawSurface** surfaceOut,
                                                 TextureFormat** formatOut)
{
    for (TGListNode* node = self->surfaces.head; node != nullptr; node = node->next)
        static_cast<ImageSurfaceRecord*>(node->data)->dirty = 0;
    return self->GetSurface(device, slots, (bit2 & 0xFF) != 0, count, surfaceOut, formatOut) ? 1 : 0;
}

// LoadFile: the width and the height of the image are swapped.
std::uint8_t bc_NiDDImage_LoadFile_defect_swap(NiDDImage* self, DirectDrawDevice* device, TextureFormat** formatOut,
                                              std::uint32_t count, DirectDrawSurface** surfaceOut, TextureFormat* given,
                                              TextureFormat** slots, std::uint32_t bit2)
{
    bool ok = self->LoadFile(device, formatOut, count, surfaceOut, given, slots, (bit2 & 0xFF) != 0);
    std::swap(self->width, self->height);
    return ok ? 1 : 0;
}

// LoadFile: the number of mip levels is 0 for a size that is not a power of two.
std::uint8_t bc_NiDDImage_LoadFile_defect_mips(NiDDImage* self, DirectDrawDevice* device, TextureFormat** formatOut,
                                              std::uint32_t count, DirectDrawSurface** surfaceOut, TextureFormat* given,
                                              TextureFormat** slots, std::uint32_t bit2)
{
    bool ok = self->LoadFile(device, formatOut, count, surfaceOut, given, slots, (bit2 & 0xFF) != 0);
    self->mipLevels = GetMipLevelCount(self->width, self->height);
    return ok ? 1 : 0;
}

// LoadFile: the slots are searched from the other end.
std::uint8_t bc_NiDDImage_LoadFile_defect_slots(NiDDImage* self, DirectDrawDevice* device, TextureFormat** formatOut,
                                               std::uint32_t count, DirectDrawSurface** surfaceOut, TextureFormat* given,
                                               TextureFormat** slots, std::uint32_t bit2)
{
    if (slots != nullptr)
        std::reverse(slots, slots + 7);
    bool ok = self->LoadFile(device, formatOut, count, surfaceOut, given, slots, (bit2 & 0xFF) != 0);
    if (slots != nullptr)
        std::reverse(slots, slots + 7);
    return ok ? 1 : 0;
}

// The loader by extension: the extension is compared with the case.
std::uint8_t bc_LoadTextureFileByExtension_defect_case(const char* path, NiRawImageData* raw)
{
    const char* dot = std::strrchr(path, '.');
    if (dot != nullptr) {
        for (const char* c = dot; *c != 0; ++c) {
            if (*c >= 'A' && *c <= 'Z')
                return 0;
        }
    }
    return LoadTextureFileByExtension(path, raw) ? 1 : 0;
}

// The loader by extension: the SGI extension '.int' is not known.
std::uint8_t bc_LoadTextureFileByExtension_defect_int(const char* path, NiRawImageData* raw)
{
    const char* dot = std::strrchr(path, '.');
    if (dot != nullptr && (std::strcmp(dot, ".int") == 0 || std::strcmp(dot, ".inta") == 0))
        return 0;
    return LoadTextureFileByExtension(path, raw) ? 1 : 0;
}

// The raw image loader gives the raw image also when the load failed.
NiRawImageData* bc_LoadRawImageFile_defect(const char* path)
{
    LoadRawImageFile(path);
    return ni::g_scratchRawImage;
}

// The static initializers and the functions at the end of the program.

// The list of the images gets the block size 1.
void bc_NiImage_InitImageList_defect()
{
    ni::InitImageList();
    ni::g_imageList.blockSize = 1;
}

// The release of the list of the images does not look at its guard bit.
void bc_NiImage_ReleaseImageList_defect()
{
    ni::g_imageListGuard &= ~1u;
    ni::ReleaseImageList();
}

// The raw image of the loaders has no reference of the variable.
void bc_NiImage_InitScratchRawImage_defect()
{
    ni::InitScratchRawImage();
    --ni::g_scratchRawImage->refCount;
}

// The destructors. The raw image keeps its palette. The image does not release its raw image.
NiRawImageData* bc_NiRawImageData_DeletingDestructor_defect(NiRawImageData* self, std::uint32_t flags)
{
    self->palette = nullptr;
    self->Destroy();
    if (flags & 1)
        operator_delete(self);
    return self;
}

NiImage* bc_NiImage_DeletingDestructor_defect(NiImage* self, std::uint32_t flags)
{
    self->rawImage = nullptr;
    self->Destroy();
    if (flags & 1)
        operator_delete(self);
    return self;
}

// The texture property. Defect 1: the constructor sets the flags to 1. Defect 2: the base destructor body does not release the
// controller. Defect 3: the destructor body does not release the image. Defect 4: the deleting destructor frees the block
// also when the flag is 0.
NiTextureProperty* bc_NiTextureProperty_NiTextureProperty_defect(NiTextureProperty* self)
{
    ::new (self) NiTextureProperty();
    self->flags = 1;
    return self;
}

void bc_NiObjectNET_Destroy_defect(NiObjectNET* self)
{
    self->controller = nullptr;
    self->NiObjectNET::Destroy();
}

void bc_NiTextureProperty_Destroy_defect(NiTextureProperty* self)
{
    self->texture = nullptr;
    self->NiTextureProperty::Destroy();
}

NiTextureProperty* bc_NiTextureProperty_DeletingDestructor_defect(NiTextureProperty* self, std::uint32_t)
{
    self->NiTextureProperty::Destroy();
    operator_delete(self);
    return self;
}

}  // extern "C"
