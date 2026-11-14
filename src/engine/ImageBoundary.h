#pragma once
#include "ImageLoaders.h"
#include "NiImage.h"

// The calls of the image code into the Direct3D layer of the renderer. The exe functions behind them are not
// reconstructed. A test or the script host installs the functions; the default of each one stops at the exe address
// (BC_NOT_RECONSTRUCTED).
struct ImageDevice {
    // Slot 10 of the vtable 0x898a7c (0x7c4f30): the deleting destructor of the image, called with 1.
    void (*destroyImage)(NiDDImage* image);
    // 0x7c5200. Makes the surface for the format on the device and gives it in surfaceOut.
    bool (*createSurface)(NiDDImage* image, DirectDrawDevice* device, TextureFormat* format,
                          DirectDrawSurface** surfaceOut, std::uint32_t count);
    // 0x7c5330. Copies the pixels of the raw image into the surface.
    bool (*fillSurface)(NiDDImage* image, NiRawImageData* raw, TextureFormat* format, DirectDrawSurface* surface);
    // 0x7c6080. Copies the pixels of the raw image into a surface again.
    bool (*refillSurface)(NiDDImage* image, TextureFormat* format, DirectDrawSurface* surface, bool flag);
    // 0x7c59e0. The surface of an image that has stream data (the field at +0x58 is not 0).
    bool (*makeFromStreamData)(NiDDImage* image, DirectDrawDevice* device, TextureFormat** formatOut,
                               std::uint32_t count, DirectDrawSurface** surfaceOut, TextureFormat* given,
                               TextureFormat** slots);
};

extern ImageDevice g_imageDevice;

// Puts the default functions into g_imageDevice.
void ResetImageDevice();

// The calls of the loaders into the file layer and into the NetImmerse stream reader. The default of each one stops at
// the exe address.
struct ImageFiles {
    // 0x86e550 with the mode 0 and the buffer size 0x2800. The stream is valid when slot 1 of its vtable gives true.
    ImageStream* (*openStream)(const char* path);
    // 0x86e620 (the destructor of the stream, run when the loader ends).
    void (*closeStream)(ImageStream* stream);
    // The branch for '.nif' files of 0x819000: 0x816c60, 0x817a40, the search of the first NiRawImageData in the file,
    // 0x7deea0 and 0x816da0.
    bool (*loadNif)(const char* path, NiRawImageData* raw);
    // 0x823f60 and 0x824710.
    bool (*loadSgiRgb)(ImageStream* stream, NiRawImageData* raw);
    bool (*loadBmp)(ImageStream* stream, NiRawImageData* raw);
};

extern ImageFiles g_imageFiles;
void ResetImageFiles();
