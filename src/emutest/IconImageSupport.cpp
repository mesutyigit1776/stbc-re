// C entry points for the tests of the image code: the raw image, the image loaders, the image classes and the
// renderer chain that gives an image its size. The calls of this code into the file layer and the Direct3D layer
// go to test functions here. They write one text line per call into a log with fixed storage.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include "BcFields.h"
#include "Crt.h"
#include "ImageBoundary.h"
#include "ImageLoaders.h"
#include "NiImage.h"
#include "TGList.h"
#include "Trap.h"

namespace {

// The log keeps runs: a line that equals the line before it adds 1 to the count of that line.
constexpr int kLogCapacity = 8192;
constexpr int kLogLine = 320;
char g_log[kLogCapacity][kLogLine];
std::uint32_t g_logRepeat[kLogCapacity];
int g_logCount = 0;

void LogLine(const char* format, ...)
{
    char line[kLogLine];
    va_list list;
    va_start(list, format);
    std::vsnprintf(line, kLogLine, format, list);
    va_end(list);
    if (g_logCount > 0 && std::strcmp(g_log[g_logCount - 1], line) == 0) {
        ++g_logRepeat[g_logCount - 1];
        return;
    }
    if (g_logCount >= kLogCapacity)
        return;
    std::strcpy(g_log[g_logCount], line);
    g_logRepeat[g_logCount++] = 1;
}

template <class T>
T* Blank()
{
    return static_cast<T*>(std::memset(operator_new(sizeof(T)), 0, sizeof(T)));
}

}  // namespace

// A block of bytes that an object points to. The tests compare the pointers by identity and the bytes by probes.
struct PixelBlock {
    unsigned char first;
};

struct TestRawImage : NiRawImageData {
    void DeleteThis() override
    {
        LogLine("delete raw_image");
        ::operator delete(this);
    }
};

// The bytes of the file that the test stream reads and the position of the stream. Reads past the end give zeros.
namespace {
unsigned char g_fileData[1 << 16];
std::uint32_t g_fileSize = 0;
bool g_streamOpen = true;
}  // namespace

struct TestStream : ImageStream {
    std::uint32_t position = 0;
    bool IsOpen() override { return g_streamOpen; }
    void Read(void* destination, std::uint32_t count) override
    {
        LogLine("read %u", count);
        unsigned char* out = static_cast<unsigned char*>(destination);
        for (std::uint32_t i = 0; i < count; ++i)
            out[i] = position + i < g_fileSize ? g_fileData[position + i] : 0;
        position += count;
    }
    void Seek(std::int32_t offset, std::uint32_t mode) override
    {
        LogLine("seek %d %u", offset, mode);
        position += static_cast<std::uint32_t>(offset);
    }
};

BC_FIELDS(PixelBlock)
BC_FIELDS_END

BC_FIELDS(ImageStream)
    {"position", static_cast<std::uint32_t>(offsetof(TestStream, position))},
BC_FIELDS_END

BC_FIELDS(NiRawImageData)
    BC_FIELD(NiRawImageData, refCount) BC_FIELD(NiRawImageData, format) BC_FIELD(NiRawImageData, width)
    BC_FIELD(NiRawImageData, height) BC_FIELD(NiRawImageData, bytesPerPixel) BC_FIELD(NiRawImageData, stride)
    BC_FIELD(NiRawImageData, palette) BC_FIELD(NiRawImageData, pixels)
BC_FIELDS_END

// The configuration of the test device: what its functions give. The tests set it for each scenario.
namespace {
struct DeviceConfig {
    bool createOk = true;
    bool fillOk = true;
    bool refillOk = true;
    bool streamDataOk = true;
    std::int32_t paletteResult = 0;
    std::int32_t setPaletteResult = 0;
    bool nifOk = true;
    bool sgiOk = true;
    bool bmpOk = true;
};
DeviceConfig g_config;
std::uint32_t g_surfaceSerial = 0;
std::uint32_t g_paletteSerial = 0;
}  // namespace

struct TestSurface : DirectDrawSurface {
    std::uint32_t serial = 0;
    std::uint32_t Release() override
    {
        LogLine("surface_release %u", serial);
        ::operator delete(this);
        return 0;
    }
    std::int32_t SetPalette(void* palette) override
    {
        LogLine("set_palette %u %u", serial, static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(palette) & 0xFFFF));
        return g_config.setPaletteResult;
    }
};

struct TestDevice : DirectDrawDevice {
    std::uint32_t serial = 0;
    std::int32_t CreatePalette(std::uint32_t flags, const void*, void** palette, void*) override
    {
        LogLine("create_palette %u 0x%x", serial, flags);
        if (g_config.paletteResult == 0)
            *palette = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEF0000u + ++g_paletteSerial));
        return g_config.paletteResult;
    }
};

struct SlotArray {
    TextureFormat* slot[7];
};
struct SurfaceCell {
    DirectDrawSurface* surface;
};
struct FormatCell {
    TextureFormat* format;
};
struct RawImagePointer {
    NiRawImageData* image;
};

struct TestObjectNET : NiObjectNET {
    void DeleteThis() override {}
};

using ImageSurfaceNode = TGListNode;
using ImageSurfaceBlock = TGListBlock;
using ImageSurfaceList = TGList;
using RendererNode = TGListNode;
using RendererBlock = TGListBlock;
using RendererList = TGList;
using ImageAllNode = TGListNode;
using ImageAllBlock = TGListBlock;
using ImageAllList = TGList;

#define BC_SLOT_FIELD(index) {"slot" #index, static_cast<std::uint32_t>(offsetof(SlotArray, slot) + (index) * sizeof(void*))},
#define BC_MANAGER_SLOT(index) \
    {"slot" #index, static_cast<std::uint32_t>(offsetof(TextureManager, slots) + (index) * sizeof(void*))},

BC_FIELDS(SlotArray)
    BC_SLOT_FIELD(0) BC_SLOT_FIELD(1) BC_SLOT_FIELD(2) BC_SLOT_FIELD(3) BC_SLOT_FIELD(4) BC_SLOT_FIELD(5) BC_SLOT_FIELD(6)
BC_FIELDS_END

BC_FIELDS(TextureFormat)
    BC_FIELD(TextureFormat, paletted) BC_FIELD(TextureFormat, id)
BC_FIELDS_END

BC_FIELDS(ImageSurfaceRecord)
    BC_FIELD(ImageSurfaceRecord, surface) BC_FIELD(ImageSurfaceRecord, formatId) BC_FIELD(ImageSurfaceRecord, device)
    BC_FIELD(ImageSurfaceRecord, dirty)
BC_FIELDS_END

BC_FIELDS(NiObjectNET)
    BC_FIELD(NiObjectNET, refCount) BC_FIELD(NiObjectNET, name) BC_FIELD(NiObjectNET, extraData)
    BC_FIELD(NiObjectNET, controller)
BC_FIELDS_END

BC_FIELDS(DdSurface)
    {"serial", static_cast<std::uint32_t>(offsetof(TestSurface, serial))},
BC_FIELDS_END

BC_FIELDS(DdDevice)
    {"serial", static_cast<std::uint32_t>(offsetof(TestDevice, serial))},
BC_FIELDS_END

BC_FIELDS(SurfaceCell)
    BC_FIELD(SurfaceCell, surface)
BC_FIELDS_END

BC_FIELDS(FormatCell)
    BC_FIELD(FormatCell, format)
BC_FIELDS_END

BC_FIELDS(RawImagePointer)
    BC_FIELD(RawImagePointer, image)
BC_FIELDS_END

BC_FIELDS(TextureManager)
    BC_MANAGER_SLOT(0) BC_MANAGER_SLOT(1) BC_MANAGER_SLOT(2) BC_MANAGER_SLOT(3) BC_MANAGER_SLOT(4) BC_MANAGER_SLOT(5)
    BC_MANAGER_SLOT(6) BC_FIELD(TextureManager, flags) BC_FIELD(TextureManager, device)
BC_FIELDS_END

BC_FIELDS(NiDDImage)
    BC_FIELD(NiDDImage, refCount) BC_FIELD(NiDDImage, name) BC_FIELD(NiDDImage, rawImage) BC_FIELD(NiDDImage, width)
    BC_FIELD(NiDDImage, height) BC_FIELD(NiDDImage, mipLevels) BC_FIELD(NiDDImage, id) BC_FIELD(NiDDImage, reserved20)
    BC_FIELD(NiDDImage, flags) BC_FIELD(NiDDImage, mask) BC_FIELD(NiDDImage, scale) BC_FIELD(NiDDImage, surfaces)
    BC_FIELD(NiDDImage, reserved48) BC_FIELD(NiDDImage, reserved4c) BC_FIELD(NiDDImage, reserved50)
    BC_FIELD(NiDDImage, reserved54) BC_FIELD(NiDDImage, streamData)
BC_FIELDS_END

#define BC_LIST_FIELDS(name) \
    BC_FIELDS(name##Node) BC_FIELD(name##Node, data) BC_FIELD(name##Node, next) BC_FIELD(name##Node, prev) BC_FIELDS_END \
    BC_FIELDS(name##Block) BC_FIELD(name##Block, nextBlock) BC_FIELD(name##Block, nodes) BC_FIELDS_END \
    BC_FIELDS(name##List) BC_FIELD(name##List, count) BC_FIELD(name##List, head) BC_FIELD(name##List, tail) \
        BC_FIELD(name##List, freeNodes) BC_FIELD(name##List, blocks) BC_FIELD(name##List, blockSize) BC_FIELDS_END

BC_LIST_FIELDS(ImageSurface)
BC_LIST_FIELDS(Renderer)
BC_LIST_FIELDS(ImageAll)

namespace {

TestStream g_testStream;

// The test functions of the device and of the file layer: each writes a line into the log.
void TestDestroyImage(NiDDImage* image)
{
    LogLine("delete_image %u", image->id);
    ::operator delete(image);
}

bool TestCreateSurface(NiDDImage*, DirectDrawDevice* device, TextureFormat* format, DirectDrawSurface** surfaceOut,
                       std::uint32_t count)
{
    LogLine("create_surface %u %u %u", device != nullptr ? static_cast<TestDevice*>(device)->serial : 0xFFFFu, format->id,
            count);
    if (!g_config.createOk)
        return false;
    TestSurface* surface = new TestSurface();
    surface->serial = ++g_surfaceSerial;
    *surfaceOut = surface;
    return true;
}

bool TestFillSurface(NiDDImage*, NiRawImageData* raw, TextureFormat* format, DirectDrawSurface* surface)
{
    LogLine("fill_surface %u %u %u %u", static_cast<TestSurface*>(surface)->serial, format->id, raw->width, raw->height);
    return g_config.fillOk;
}

bool TestRefillSurface(NiDDImage*, TextureFormat* format, DirectDrawSurface* surface, bool flag)
{
    LogLine("refill_surface %u %u %d", static_cast<TestSurface*>(surface)->serial, format->id, flag ? 1 : 0);
    return g_config.refillOk;
}

bool TestMakeFromStreamData(NiDDImage*, DirectDrawDevice*, TextureFormat**, std::uint32_t count, DirectDrawSurface**,
                            TextureFormat*, TextureFormat**)
{
    LogLine("stream_data %u", count);
    return g_config.streamDataOk;
}

ImageStream* TestOpenStream(const char* path)
{
    LogLine("open %s", path);
    g_testStream.position = 0;
    return &g_testStream;
}

void TestCloseStream(ImageStream*) { LogLine("close"); }

bool TestLoadNif(const char* path, NiRawImageData*)
{
    LogLine("nif %s", path);
    LogLine("nif_close");
    return false;
}

bool TestLoadSgi(ImageStream*, NiRawImageData*)
{
    LogLine("sgi");
    return g_config.sgiOk;
}

bool TestLoadBmp(ImageStream*, NiRawImageData*)
{
    LogLine("bmp");
    return g_config.bmpOk;
}

void InstallImageTestFunctions()
{
    g_imageDevice = {TestDestroyImage, TestCreateSurface, TestFillSurface, TestRefillSurface, TestMakeFromStreamData};
    g_imageFiles = {TestOpenStream, TestCloseStream, TestLoadNif, TestLoadSgi, TestLoadBmp};
}

}  // namespace

namespace {

// A stream that returns the bytes of a TGA file that makes the loader stop at an address of the exe.
struct BytesStream : ImageStream {
    const unsigned char* data;
    std::uint32_t position = 0;
    explicit BytesStream(const unsigned char* bytes) : data(bytes) {}
    bool IsOpen() override { return true; }
    void Read(void* destination, std::uint32_t count) override
    {
        std::memcpy(destination, data + position, count);
        position += count;
    }
    void Seek(std::int32_t offset, std::uint32_t) override { position += static_cast<std::uint32_t>(offset); }
};

struct ImageTestRenderer : NiD3DRender {
    std::uint32_t GetWidth() override { return 0; }
    std::uint32_t GetHeight() override { return 0; }
};

}  // namespace

extern "C" {

// The calls that stop at an address of the exe, with the default functions of the tables. which: 0 to 4 the device, 5 to 9
// the file layer, 10 and 11 unused, 12 a colour index past the map (TGA), 13 a colour map entry
// with an unknown size (TGA), 14 a long path, 15 an image that is not a NiDDImage given to the renderer, 16 a negative mask.
void bc_image_call_trap(std::int32_t which)
{
    ImageDevice savedDevice = g_imageDevice;
    ImageFiles savedFiles = g_imageFiles;
    ResetImageDevice();
    ResetImageFiles();
    NiDDImage image;
    switch (which) {
    case 0: g_imageDevice.destroyImage(&image); break;
    case 1: g_imageDevice.createSurface(&image, nullptr, nullptr, nullptr, 0); break;
    case 2: g_imageDevice.fillSurface(&image, nullptr, nullptr, nullptr); break;
    case 3: g_imageDevice.refillSurface(&image, nullptr, nullptr, false); break;
    case 4: g_imageDevice.makeFromStreamData(&image, nullptr, nullptr, 0, nullptr, nullptr, nullptr); break;
    case 5: g_imageFiles.openStream("a"); break;
    case 6: g_imageFiles.closeStream(nullptr); break;
    case 7: g_imageFiles.loadNif("a", nullptr); break;
    case 8: g_imageFiles.loadSgiRgb(nullptr, nullptr); break;
    case 9: g_imageFiles.loadBmp(nullptr, nullptr); break;
    case 10:
    case 11:
        break;
    case 12:
    case 13: {
        // a colour mapped file of 16 bit indices (12) or a truecolor file with a colour map of an entry size of 12 (13)
        static unsigned char file12[18 + 2 + 3 + 2] = {0, 1, 1, 0, 0, 1, 0, 24, 0, 0, 0, 0, 1, 0, 1, 0, 16, 0, 1, 2, 3, 0, 0x40};
        static unsigned char file13[18 + 2 + 3] = {0, 1, 2, 0, 0, 1, 0, 12, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 1, 2, 3, 4, 5};
        BytesStream stream(which == 12 ? file12 : file13);
        NiRawImageData raw;
        LoadTgaFile(&stream, &raw);
        break;
    }
    case 14: {
        char path[300];
        std::memset(path, 'x', sizeof(path));
        std::memcpy(path + 290, ".tga", 5);
        NiRawImageData raw;
        LoadTextureFileByExtension(path, &raw);
        break;
    }
    case 15: {
        NiImage plain;
        TextureManager manager;
        ImageTestRenderer renderer;
        renderer.textureManager = &manager;
        renderer.AcceptImage(&plain);
        break;
    }
    case 16: {
        NiDDImage dd;
        NiRawImageData raw;
        raw.refCount = 2;
        dd.rawImage = &raw;
        dd.name = nullptr;
        dd.mask = 0xFFFFFFFFu;
        dd.flags = 0;
        TextureFormat format = {};
        TextureFormat* slots[7] = {&format, &format, &format, &format, &format, &format, &format};
        DirectDrawSurface* surface = nullptr;
        TextureFormat* chosen = nullptr;
        dd.LoadFile(nullptr, &chosen, 1, &surface, nullptr, slots, false);
        break;
    }
    }
    g_imageDevice = savedDevice;
    g_imageFiles = savedFiles;
}

// The state of a scenario: the log is empty and the counters have the given values.
void bc_image_reset(std::int32_t objectCount, std::uint32_t imageCounter)
{
    g_logCount = 0;
    ni::g_objectCount = objectCount;
    ni::g_imageCounter = imageCounter;
    ni::g_imageError = 0;
    ni::g_imageFlag2Enabled = 0;
    ni::g_imageFlag4Enabled = 0;
    g_fileSize = 0;
    g_tgaState = TgaState();
    g_streamOpen = true;
    g_config = DeviceConfig();
    g_surfaceSerial = 0;
    g_paletteSerial = 0;
    ni::g_ddImageCount = 0;
    ni::g_imageFactory = nullptr;
    ni::g_scratchRawImage = nullptr;
    ni::g_rendererList = {};
    ni::g_imageList = {};
    ni::g_imageListGuard = 0;
    ni::g_rawAndRendererGuard = 0;
    InstallImageTestFunctions();
}

// What the test device and the test file layer give. The flags are 1 or 0.
void bc_image_configure(std::int32_t createOk, std::int32_t fillOk, std::int32_t refillOk, std::int32_t streamDataOk,
                        std::int32_t paletteResult, std::int32_t setPaletteResult, std::int32_t nifOk,
                        std::int32_t sgiOk, std::int32_t bmpOk, std::int32_t streamOpen)
{
    g_config.createOk = createOk != 0;
    g_config.fillOk = fillOk != 0;
    g_config.refillOk = refillOk != 0;
    g_config.streamDataOk = streamDataOk != 0;
    g_config.paletteResult = paletteResult;
    g_config.setPaletteResult = setPaletteResult;
    g_config.nifOk = nifOk != 0;
    g_config.sgiOk = sgiOk != 0;
    g_config.bmpOk = bmpOk != 0;
    g_streamOpen = streamOpen != 0;
}

// The factory function that the renderer sets: 0 for none, 1 for NiDDImage::Create.
void bc_image_set_factory(std::int32_t which)
{
    ni::g_imageFactory = which == 1 ? []() -> NiImage* { return NiDDImage::Create(); } : nullptr;
}

void bc_image_set_guards(std::uint32_t imageList, std::uint32_t rawAndRenderer)
{
    ni::g_imageListGuard = static_cast<std::uint8_t>(imageList);
    ni::g_rawAndRendererGuard = static_cast<std::uint8_t>(rawAndRenderer);
}
std::uint32_t bc_image_guard_image_list() { return ni::g_imageListGuard; }
std::uint32_t bc_image_guard_raw_renderer() { return ni::g_rawAndRendererGuard; }
void bc_NiImage_InitImageList() { ni::InitImageList(); }
void bc_NiImage_InitRendererList() { ni::InitRendererList(); }
void bc_NiImage_InitScratchRawImage() { ni::InitScratchRawImage(); }
void bc_NiImage_ReleaseImageList() { ni::ReleaseImageList(); }
void bc_NiImage_ReleaseRendererList() { ni::ReleaseRendererList(); }
void bc_NiImage_ReleaseScratchRawImage() { ni::ReleaseScratchRawImage(); }

std::uint32_t bc_image_dd_count() { return ni::g_ddImageCount; }
std::uint32_t bc_image_error() { return ni::g_imageError; }
void bc_image_set_error_and_flags(std::uint32_t error, std::uint32_t flag2, std::uint32_t flag4)
{
    ni::g_imageError = error;
    ni::g_imageFlag2Enabled = static_cast<std::uint8_t>(flag2);
    ni::g_imageFlag4Enabled = static_cast<std::uint8_t>(flag4);
}
void bc_image_set_dd_count(std::uint32_t count) { ni::g_ddImageCount = count; }

void bc_image_set_file(const unsigned char* data, std::uint32_t size)
{
    g_fileSize = size < sizeof(g_fileData) ? size : static_cast<std::uint32_t>(sizeof(g_fileData));
    std::memcpy(g_fileData, data, g_fileSize);
}

// The TGA state in the layout of the exe memory from 0x9a2e30 to 0x9b2e53 (0x10024 bytes).
void bc_tga_state_get(unsigned char* out)
{
    std::memset(out, 0, 0x10024);
    const TgaState& s = g_tgaState;
    std::memcpy(out + 0x00, &s.indexB, 4);
    out[0x04] = s.red;
    out[0x05] = s.runLength;
    out[0x06] = s.green;
    out[0x09] = s.blueB;
    out[0x0A] = s.redB;
    out[0x0B] = s.alpha;
    out[0x0C] = s.colorMapped;
    out[0x0D] = s.greenB;
    std::memcpy(out + 0x10, &s.indexA, 4);
    std::memcpy(out + 0x14, s.palette, sizeof(s.palette));
    out[0x10018] = s.blue;
    std::memcpy(out + 0x1001C, &s.packetCount, 4);
    std::memcpy(out + 0x10020, &s.packetIsRun, 4);
}

void bc_tga_state_set(const unsigned char* in)
{
    TgaState& s = g_tgaState;
    std::memcpy(&s.indexB, in + 0x00, 4);
    s.red = in[0x04];
    s.runLength = in[0x05];
    s.green = in[0x06];
    s.blueB = in[0x09];
    s.redB = in[0x0A];
    s.alpha = in[0x0B];
    s.colorMapped = in[0x0C];
    s.greenB = in[0x0D];
    std::memcpy(&s.indexA, in + 0x10, 4);
    std::memcpy(s.palette, in + 0x14, sizeof(s.palette));
    s.blue = in[0x10018];
    std::memcpy(&s.packetCount, in + 0x1001C, 4);
    std::memcpy(&s.packetIsRun, in + 0x10020, 4);
}

std::int32_t bc_image_object_count() { return ni::g_objectCount; }
std::uint32_t bc_image_counter() { return ni::g_imageCounter; }

int bc_image_log_dump(char* out, int capacity)
{
    int used = 0;
    for (int i = 0; i < g_logCount; ++i) {
        int length = static_cast<int>(std::strlen(g_log[i]));
        for (std::uint32_t repeat = 0; repeat < g_logRepeat[i]; ++repeat) {
            if (used + length + 2 > capacity)
                return -1;
            std::memcpy(out + used, g_log[i], static_cast<std::size_t>(length));
            used += length;
            out[used++] = '\n';
        }
    }
    out[used] = 0;
    return used;
}

PixelBlock* bc_new_PixelBlock() { return Blank<PixelBlock>(); }
ImageStream* bc_new_ImageStream() { return new TestStream(); }

std::uint8_t bc_LoadTgaFile(ImageStream* stream, NiRawImageData* raw) { return LoadTgaFile(stream, raw) ? 1 : 0; }
// The builder stands for an exe object that exists already, so the count of the objects keeps its value.
NiRawImageData* bc_new_NiRawImageData()
{
    NiRawImageData* image = new TestRawImage();
    --ni::g_objectCount;
    return image;
}

NiRawImageData* bc_NiRawImageData_NiRawImageData(NiRawImageData* self) { return ::new (self) NiRawImageData(); }

NiRawImageData* bc_NiRawImageData_DeletingDestructor(NiRawImageData* self, std::uint32_t flags)
{
    self->NiRawImageData::Destroy();
    if (flags & 1)
        operator_delete(self);
    return self;
}
NiImage* bc_NiImage_DeletingDestructor(NiImage* self, std::uint32_t flags)
{
    self->NiImage::Destroy();
    if (flags & 1)
        operator_delete(self);
    return self;
}

// The builder stands for an exe object that exists already, so the count of the objects keeps its value.
NiObjectNET* bc_new_NiObjectNET()
{
    NiObjectNET* object = new TestObjectNET();
    --ni::g_objectCount;
    return object;
}
NiObjectNET* bc_NiObjectNET_NiObjectNET(NiObjectNET* self) { return ::new (self) TestObjectNET(); }
void bc_NiObjectNET_Destroy(NiObjectNET* self) { self->NiObjectNET::Destroy(); }
NiTextureProperty* bc_NiTextureProperty_NiTextureProperty(NiTextureProperty* self) { return ::new (self) NiTextureProperty(); }
void bc_NiTextureProperty_Destroy(NiTextureProperty* self) { self->NiTextureProperty::Destroy(); }
NiTextureProperty* bc_NiTextureProperty_DeletingDestructor(NiTextureProperty* self, std::uint32_t flags)
{
    self->NiTextureProperty::Destroy();
    if (flags & 1)
        operator_delete(self);
    return self;
}

std::uint8_t bc_NiRawImageData_Allocate(NiRawImageData* self, std::uint32_t format, std::uint32_t width,
                                        std::uint32_t height)
{
    return self->Allocate(format, width, height) ? 1 : 0;
}

std::uint32_t bc_GetMipLevelCount(std::uint32_t width, std::uint32_t height) { return GetMipLevelCount(width, height); }

RawImagePointer* bc_init_global_RawImagePointer() { return reinterpret_cast<RawImagePointer*>(&ni::g_scratchRawImage); }
RendererList* bc_init_global_RendererList() { return &ni::g_rendererList; }
ImageAllList* bc_init_global_ImageAllList() { return &ni::g_imageList; }

SlotArray* bc_new_SlotArray() { return Blank<SlotArray>(); }
TextureFormat* bc_new_TextureFormat() { return Blank<TextureFormat>(); }
ImageSurfaceRecord* bc_new_ImageSurfaceRecord() { return Blank<ImageSurfaceRecord>(); }
SurfaceCell* bc_new_SurfaceCell() { return Blank<SurfaceCell>(); }
FormatCell* bc_new_FormatCell() { return Blank<FormatCell>(); }
TextureManager* bc_new_TextureManager() { return new TextureManager(); }
ImageSurfaceNode* bc_new_ImageSurfaceNode() { return Blank<ImageSurfaceNode>(); }
ImageSurfaceBlock* bc_new_ImageSurfaceBlock() { return Blank<ImageSurfaceBlock>(); }
ImageSurfaceList* bc_new_ImageSurfaceList() { return Blank<ImageSurfaceList>(); }
RendererNode* bc_new_RendererNode() { return Blank<RendererNode>(); }
RendererBlock* bc_new_RendererBlock() { return Blank<RendererBlock>(); }
RendererList* bc_new_RendererList() { return Blank<RendererList>(); }
ImageAllNode* bc_new_ImageAllNode() { return Blank<ImageAllNode>(); }
ImageAllBlock* bc_new_ImageAllBlock() { return Blank<ImageAllBlock>(); }
ImageAllList* bc_new_ImageAllList() { return Blank<ImageAllList>(); }

// The builder stands for exe objects that exist already, so the counters keep their values.
NiDDImage* bc_new_NiDDImage()
{
    NiDDImage* image = new NiDDImage();
    --ni::g_objectCount;
    --ni::g_imageCounter;
    return image;
}
TestSurface* bc_new_DdSurface() { return new TestSurface(); }
TestDevice* bc_new_DdDevice() { return new TestDevice(); }

NiImage* bc_NiImage_NiImage(NiImage* self) { return ::new (self) NiImage(); }
NiDDImage* bc_NiDDImage_Create() { return NiDDImage::Create(); }
NiImage* bc_NiImage_CreateFromFile(const char* path, std::uint32_t mode, std::uint32_t mask, std::uint32_t kind)
{
    return NiImage::CreateFromFile(path, mode, mask, kind);
}
std::uint8_t bc_NiD3DRender_AcceptImage(NiD3DRender* self, NiImage* image) { return self->NiD3DRender::AcceptImage(image) ? 1 : 0; }
std::uint8_t bc_TextureManager_AcceptImage(TextureManager* self, NiImage* image)
{
    return self->TextureManager::AcceptImage(image) ? 1 : 0;
}
std::uint8_t bc_NiDDImage_GetSurface(NiDDImage* self, DirectDrawDevice* device, TextureFormat** slots, std::uint32_t bit2,
                                     std::uint32_t count, DirectDrawSurface** surfaceOut, TextureFormat** formatOut)
{
    return self->GetSurface(device, slots, (bit2 & 0xFF) != 0, count, surfaceOut, formatOut) ? 1 : 0;
}
std::uint8_t bc_NiDDImage_LoadFile(NiDDImage* self, DirectDrawDevice* device, TextureFormat** formatOut,
                                   std::uint32_t count, DirectDrawSurface** surfaceOut, TextureFormat* given,
                                   TextureFormat** slots, std::uint32_t bit2)
{
    return self->LoadFile(device, formatOut, count, surfaceOut, given, slots, (bit2 & 0xFF) != 0) ? 1 : 0;
}
std::uint8_t bc_LoadTextureFileByExtension(const char* path, NiRawImageData* raw)
{
    return LoadTextureFileByExtension(path, raw) ? 1 : 0;
}
NiRawImageData* bc_LoadRawImageFile(const char* path) { return LoadRawImageFile(path); }

// The bytes behind a pointer of a raw image, for the probes.
const unsigned char* bc_raw_image_bytes(NiRawImageData* self, int which) { return which == 0 ? self->palette : self->pixels; }

}  // extern "C"
