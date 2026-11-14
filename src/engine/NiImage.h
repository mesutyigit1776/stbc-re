#pragma once
#include <cstdint>

#include "ExeLayout.h"
#include "NiRefObject.h"
#include "TGList.h"

class NiRawImageData;

namespace ni {
// The count of the NetImmerse objects that are alive, at 0x9a1478. The base constructor 0x7d87a0 adds 1.
extern std::int32_t g_objectCount;
// The counter at 0x9783d0 that gives each new NiImage its id. The exe starts it at 1.
extern std::uint32_t g_imageCounter;
// The code of the last error of the image functions, at 0x9a168c.
extern std::uint32_t g_imageError;
// The bytes at 0x9a1689 and 0x9a168a: the flag 2 and the flag 4 of an image whose mode or kind is 2 (E0196).
extern std::uint8_t g_imageFlag2Enabled;
extern std::uint8_t g_imageFlag4Enabled;
// The guard bits of the functions that run at the end of the program: the byte at 0x9a11e5 (bit 0, the list of the images)
// and the byte at 0x9a163d (bit 0 for the raw image, bit 1 for the list of the renderers).
extern std::uint8_t g_imageListGuard;
extern std::uint8_t g_rawAndRendererGuard;
}  // namespace ni

class NiImage;
class NiDDImage;
class TextureManager;

// The pixel format of a surface that the device offers. The part of the exe object that the image code uses is the
// flag at +0 (not 0: the surface has a palette) and the number at +0x24.
struct TextureFormat {
    std::uint8_t paletted;       // 0x00
    std::uint8_t reserved[0x23]; // 0x01 to 0x23
    std::uint32_t id;            // 0x24
};

// The COM interfaces of DirectDraw that the image code calls. The exe calls slot 2 (Release, offset 8) and slot 31
// (SetPalette, offset 0x7c) of the surface and slot 5 (CreatePalette, offset 0x14) of the device.
class DirectDrawSurface {
public:
    virtual std::uint32_t Release() = 0;
    virtual std::int32_t SetPalette(void* palette) = 0;
};
class DirectDrawDevice {
public:
    virtual std::int32_t CreatePalette(std::uint32_t flags, const void* entries, void** palette, void* outer) = 0;
};

// The record that a NiDDImage keeps for each surface it made (0x10 bytes, made in 0x7c5c60).
struct ImageSurfaceRecord {
    DirectDrawSurface* surface;   // 0x00
    std::uint32_t formatId;       // 0x04, the id of the format
    DirectDrawDevice* device;     // 0x08
    std::uint8_t dirty;           // 0x0c
};

// 0x7e15c0. The number of mip levels of an image: the smaller shift count of the width and the height when both are
// powers of two, else 0 (E0205).
std::uint32_t GetMipLevelCount(std::uint32_t width, std::uint32_t height);

// The pixels of an image file after the loader has read it. Run time type NiRawImageData (RTTI object 0x9a15d0, name
// text at 0x978330, E0209); vtable 0x898d44; the exe object has 0x24 bytes. The formats are 0 (8 bit with a palette),
// 1 (24 bit RGB) and 2 (32 bit RGBA).
class NiRawImageData : public NiRefObject {
public:
    NiRawImageData();
    // Slot 10 of the vtable 0x898d44 (0x7e0900) called with 1: Destroy, then the block is freed.
    void DeleteThis() override;
    // 0x7de680, the destructor body: frees the palette and the pixels and takes 1 from the count of the objects.
    void Destroy();
    bool Allocate(std::uint32_t format, std::uint32_t width, std::uint32_t height);

    std::uint32_t format;          // 0x08
    std::uint32_t width;           // 0x0c
    std::uint32_t height;          // 0x10
    std::uint32_t bytesPerPixel;   // 0x14
    std::uint32_t stride;          // 0x18, bytes in a row
    std::uint8_t* palette;         // 0x1c, 0x400 bytes for the format 0, else null
    std::uint8_t* pixels;          // 0x20
};

// An image of a file or of a buffer. Run time type NiImage (RTTI object 0x9a1648, name text at 0x9783dc, E0209);
// vtable 0x898d78; the exe object has 0x30 bytes. The width and the height are zero until the renderer accepts the image
// (E0212).
class NiImage : public NiRefObject {
public:
    NiImage();
    // Slot 10 of the vtable 0x898d78 (0x7e1f80) called with 1: Destroy, then the block is freed.
    void DeleteThis() override;
    // 0x7e1340, the destructor body: frees the name, releases the raw image and takes 1 from the count of the objects.
    void Destroy();

    // 0x7e0c50. Makes an image with the factory function of the exe (0x9a1694) and asks every renderer in the list at
    // 0x9a1660 to accept it. A renderer that refuses makes the function delete the image and give null.
    static NiImage* CreateFromFile(const char* path, std::uint32_t mode, std::uint32_t mask, std::uint32_t kind);

    char* name;                  // 0x08, the path text; a block that the image owns
    NiRawImageData* rawImage;    // 0x0c, with a reference
    std::uint32_t width;         // 0x10, in pixels
    std::uint32_t height;        // 0x14, in pixels
    std::uint32_t mipLevels;     // 0x18
    std::uint32_t id;            // 0x1c, the value of a counter at 0x9783d0
    std::uint32_t reserved20;    // 0x20
    std::uint32_t flags;         // 0x24
    std::uint32_t mask;          // 0x28
    float scale;                 // 0x2c, 128.5 from the constructor
};

// The image that the renderer works on. Run time type NiDDImage (RTTI object 0x9a1218, name text at 0x976eb0, parent
// NiImage, E0209); vtable 0x898a7c; the exe object has 0x5c bytes. The function pointer at 0x9a1694 makes it (0x7c6490,
// E0208).
class NiDDImage : public NiImage {
public:
    static NiDDImage* Create();
    void DeleteThis() override;

    // 0x7c6370. Looks for a surface of this image on the device; makes one when there is none.
    bool GetSurface(DirectDrawDevice* device, TextureFormat** slots, bool bit2, std::uint32_t count,
                    DirectDrawSurface** surfaceOut, TextureFormat** formatOut);
    // 0x7c5c60. Reads the file of the image, copies its size and makes the surface.
    bool LoadFile(DirectDrawDevice* device, TextureFormat** formatOut, std::uint32_t count,
                  DirectDrawSurface** surfaceOut, TextureFormat* given, TextureFormat** slots, bool bit2);

    TGList surfaces;               // 0x30, records of the surfaces that the image has on a device
    std::uint32_t reserved48;      // 0x48, zero
    std::uint32_t reserved4c;      // 0x4c, not set by the constructor
    std::uint32_t reserved50;      // 0x50, zero
    std::uint32_t reserved54;      // 0x54, zero
    void* streamData;              // 0x58, zero for an image from a file (E0213)
};

// The texture manager of the renderer (made by 0x7d5080, constructor 0x7d4b90, vtable 0x898b58, 0xf8 bytes). The exe
// has no run time type name for it. The source declares the part that the image code reads.
class TextureManager {
public:
    virtual ~TextureManager() = default;
    // Slot 2 (offset 8), 0x7d6f30.
    virtual bool AcceptImage(NiImage* image);

    std::uint32_t reserved04;        // 0x04
    TextureFormat* slots[7];         // 0x08, a format of the device for each kind of image
    std::uint32_t reserved24[0x21];  // 0x24 to 0xa7
    std::uint32_t flags;             // 0xa8, bit 2 is passed to 0x7c6370
    DirectDrawDevice* device;        // 0xac
};

namespace ni {
// The renderers that the images ask: the list at 0x9a1660. Its nodes hold NiD3DRender pointers. The static initializer
// 0x7e0ba0 makes it empty with the block size 2.
extern TGList g_rendererList;
void InitRendererList();
void ReleaseRendererList();  // 0x7e0be0, registered with atexit
// All NiDDImages: the list at 0x9a11e8 (static initializer 0x7c4d40) and the counter at 0x9a123c that each constructor adds
// 1 to.
extern TGList g_imageList;
extern std::uint32_t g_ddImageCount;
void InitImageList();
void ReleaseImageList();  // 0x7c4d80, registered with atexit
// The function pointer at 0x9a1694 that 0x7e0c50 calls to make an image. The renderer sets it to 0x7c6490.
extern NiImage* (*g_imageFactory)();
// The raw image at 0x9a165c that every loaded file goes into. The static initializer 0x7e0b00 makes it.
extern NiRawImageData* g_scratchRawImage;
void InitScratchRawImage();      // 0x7e0ae0: 0x7e0b00 and the atexit registration of 0x7e0b60
void ReleaseScratchRawImage();   // 0x7e0b60
}  // namespace ni

// 0x7e15a0. Loads the file into the raw image of 0x9a165c and gives that image, or null.
NiRawImageData* LoadRawImageFile(const char* path);

constexpr exe::Field kNiRawImageDataLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiRawImageData, refCount, 0x04),
    EXE_FIELD(NiRawImageData, format, 0x08),
    EXE_FIELD(NiRawImageData, width, 0x0c),
    EXE_FIELD(NiRawImageData, height, 0x10),
    EXE_FIELD(NiRawImageData, bytesPerPixel, 0x14),
    EXE_FIELD(NiRawImageData, stride, 0x18),
    EXE_FIELD(NiRawImageData, palette, 0x1c),
    EXE_FIELD(NiRawImageData, pixels, 0x20),
};
static_assert(exe::LayoutHolds(kNiRawImageDataLayout, 0x24), "NiRawImageData layout");

constexpr exe::Field kNiImageLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiImage, refCount, 0x04),
    EXE_FIELD(NiImage, name, 0x08),
    EXE_FIELD(NiImage, rawImage, 0x0c),
    EXE_FIELD(NiImage, width, 0x10),
    EXE_FIELD(NiImage, height, 0x14),
    EXE_FIELD(NiImage, mipLevels, 0x18),
    EXE_FIELD(NiImage, id, 0x1c),
    EXE_FIELD(NiImage, reserved20, 0x20),
    EXE_FIELD(NiImage, flags, 0x24),
    EXE_FIELD(NiImage, mask, 0x28),
    EXE_FIELD(NiImage, scale, 0x2c),
};
static_assert(exe::LayoutHolds(kNiImageLayout, 0x30), "NiImage layout");

constexpr exe::Field kNiDDImageLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(NiDDImage, refCount, 0x04),
    EXE_FIELD(NiDDImage, name, 0x08),
    EXE_FIELD(NiDDImage, rawImage, 0x0c),
    EXE_FIELD(NiDDImage, width, 0x10),
    EXE_FIELD(NiDDImage, height, 0x14),
    EXE_FIELD(NiDDImage, mipLevels, 0x18),
    EXE_FIELD(NiDDImage, id, 0x1c),
    EXE_FIELD(NiDDImage, reserved20, 0x20),
    EXE_FIELD(NiDDImage, flags, 0x24),
    EXE_FIELD(NiDDImage, mask, 0x28),
    EXE_FIELD(NiDDImage, scale, 0x2c),
    EXE_FIELD_ALIGNED(NiDDImage, surfaces, 0x30, 0x18, 4),
    EXE_FIELD(NiDDImage, reserved48, 0x48),
    EXE_FIELD(NiDDImage, reserved4c, 0x4c),
    EXE_FIELD(NiDDImage, reserved50, 0x50),
    EXE_FIELD(NiDDImage, reserved54, 0x54),
    EXE_FIELD(NiDDImage, streamData, 0x58),
};
static_assert(exe::LayoutHolds(kNiDDImageLayout, 0x5c), "NiDDImage layout");

constexpr exe::Field kTextureFormatLayout[] = {
    EXE_FIELD(TextureFormat, paletted, 0x00),
    EXE_FIELD_ALIGNED(TextureFormat, reserved, 0x01, 0x23, 1),
    EXE_FIELD(TextureFormat, id, 0x24),
};
static_assert(exe::LayoutHolds(kTextureFormatLayout, 0x28), "TextureFormat layout");

constexpr exe::Field kImageSurfaceRecordLayout[] = {
    EXE_FIELD(ImageSurfaceRecord, surface, 0x00),
    EXE_FIELD(ImageSurfaceRecord, formatId, 0x04),
    EXE_FIELD(ImageSurfaceRecord, device, 0x08),
    EXE_FIELD(ImageSurfaceRecord, dirty, 0x0c),
};
static_assert(exe::LayoutHolds(kImageSurfaceRecordLayout, 0x10), "ImageSurfaceRecord layout");

constexpr exe::Field kTextureManagerLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TextureManager, reserved04, 0x04),
    EXE_FIELD_ALIGNED(TextureManager, slots, 0x08, 0x1c, 4),
    EXE_FIELD_ALIGNED(TextureManager, reserved24, 0x24, 0x84, 4),
    EXE_FIELD(TextureManager, flags, 0xa8),
    EXE_FIELD(TextureManager, device, 0xac),
};
static_assert(exe::LayoutHolds(kTextureManagerLayout, 0xb0), "TextureManager layout");
