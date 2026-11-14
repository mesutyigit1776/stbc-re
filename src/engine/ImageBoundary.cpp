#include "ImageBoundary.h"

#include "Trap.h"

namespace {

// 0x7c4f30
void DefaultDestroyImage(NiDDImage*) { BC_NOT_RECONSTRUCTED(0x7c4f30); }

// 0x7c5200
bool DefaultCreateSurface(NiDDImage*, DirectDrawDevice*, TextureFormat*, DirectDrawSurface**, std::uint32_t)
{
    BC_NOT_RECONSTRUCTED(0x7c5200);
    return false;
}

// 0x7c5330
bool DefaultFillSurface(NiDDImage*, NiRawImageData*, TextureFormat*, DirectDrawSurface*)
{
    BC_NOT_RECONSTRUCTED(0x7c5330);
    return false;
}

// 0x7c6080
bool DefaultRefillSurface(NiDDImage*, TextureFormat*, DirectDrawSurface*, bool)
{
    BC_NOT_RECONSTRUCTED(0x7c6080);
    return false;
}

// 0x7c59e0
bool DefaultMakeFromStreamData(NiDDImage*, DirectDrawDevice*, TextureFormat**, std::uint32_t, DirectDrawSurface**,
                               TextureFormat*, TextureFormat**)
{
    BC_NOT_RECONSTRUCTED(0x7c59e0);
    return false;
}

// 0x86e550
ImageStream* DefaultOpenStream(const char*)
{
    BC_NOT_RECONSTRUCTED(0x86e550);
    return nullptr;
}

// 0x86e620
void DefaultCloseStream(ImageStream*) { BC_NOT_RECONSTRUCTED(0x86e620); }

// 0x817a40
bool DefaultLoadNif(const char*, NiRawImageData*)
{
    BC_NOT_RECONSTRUCTED(0x817a40);
    return false;
}

// 0x823f60
bool DefaultLoadSgiRgb(ImageStream*, NiRawImageData*)
{
    BC_NOT_RECONSTRUCTED(0x823f60);
    return false;
}

// 0x824710
bool DefaultLoadBmp(ImageStream*, NiRawImageData*)
{
    BC_NOT_RECONSTRUCTED(0x824710);
    return false;
}

}  // namespace

ImageDevice g_imageDevice = {DefaultDestroyImage, DefaultCreateSurface, DefaultFillSurface, DefaultRefillSurface,
                             DefaultMakeFromStreamData};

void ResetImageDevice()
{
    g_imageDevice = {DefaultDestroyImage, DefaultCreateSurface, DefaultFillSurface, DefaultRefillSurface,
                     DefaultMakeFromStreamData};
}

ImageFiles g_imageFiles = {DefaultOpenStream, DefaultCloseStream, DefaultLoadNif, DefaultLoadSgiRgb, DefaultLoadBmp};

void ResetImageFiles()
{
    g_imageFiles = {DefaultOpenStream, DefaultCloseStream, DefaultLoadNif, DefaultLoadSgiRgb, DefaultLoadBmp};
}
