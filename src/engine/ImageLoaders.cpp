#include "ImageLoaders.h"

#include <cstring>

#include "ImageBoundary.h"
#include "Trap.h"

TgaState g_tgaState = {};

namespace {

std::uint8_t ReadByte(ImageStream* stream)
{
    std::uint8_t value = 0;
    stream->Read(&value, 1);
    return value;
}

bool SupportedDepth(std::uint32_t depth) { return depth == 8 || depth == 15 || depth == 16 || depth == 24 || depth == 32; }

std::uint32_t PackColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
{
    return (static_cast<std::uint32_t>(alpha) << 24) | (static_cast<std::uint32_t>(blue) << 16) |
           (static_cast<std::uint32_t>(green) << 8) | red;
}

// The colour map entries of 0x824c70 (0x824e3f to 0x824ebd). The entry size 15 and 16 have 5 bits for each colour; the
// bits go to the high end of the byte and the low bits are 0.
void ReadColorMap(ImageStream* stream, const std::uint8_t* header, std::uint32_t first, std::uint32_t last)
{
    const std::uint32_t entrySize = header[7];
    for (std::uint32_t index = first; index < last; ++index) {
        std::uint8_t red, green, blue, alpha = 0xFF;
        switch (entrySize) {
        case 8:
            red = green = blue = ReadByte(stream);
            break;
        case 15:
        case 16: {
            std::uint8_t low = ReadByte(stream);
            std::uint8_t high = ReadByte(stream);
            red = static_cast<std::uint8_t>((high & 0xFC) << 1);
            green = static_cast<std::uint8_t>((high & 3) * 64 + ((low >> 2) & 0x38));
            blue = static_cast<std::uint8_t>(low << 3);
            break;
        }
        case 24:
        case 32:
            blue = ReadByte(stream);
            green = ReadByte(stream);
            red = ReadByte(stream);
            if (entrySize == 32)
                alpha = ReadByte(stream);
            break;
        default:
            BC_NOT_RECONSTRUCTED(0x824e80);  // B0034: the exe uses values that earlier instructions left in registers
            return;
        }
        g_tgaState.palette[index] = PackColor(red, green, blue, alpha);
    }
}

// The step of both pixel loops from one row to the next (0x8252a6 to 0x8252fe and 0x825391 to 0x8253f7): the rows of an
// image with the interleave 1 or 2 go in steps of 2 or 4 and start again one row later.
struct RowWalker {
    std::int32_t topDown = 0;
    std::int32_t bottomUp = 0;
    std::int32_t topDownBase = 0;
    std::int32_t bottomUpBase = 0;
    std::int32_t height;
    std::int32_t step;
    bool topToBottom;

    std::int32_t Row() const { return topToBottom ? topDown : bottomUp + height - 1; }

    void Next()
    {
        topDown += step;
        bottomUp -= step;
        if (topDown >= height) {
            ++topDownBase;
            --bottomUpBase;
            topDown = topDownBase;
            bottomUp = bottomUpBase;
        }
    }
};

// 0x8254e0. Reads one pixel for a 24 bit image (the image without alpha) and writes its red, green and blue bytes. The
// run length state is the same as in the loader.
void ReadPixelRgb(ImageStream* stream, std::uint8_t* destination, std::uint32_t depth)
{
    TgaState& state = g_tgaState;
    bool previous = false;
    if (state.runLength != 0) {
        if (state.packetCount == 0) {
            std::uint8_t header = ReadByte(stream);
            state.packetIsRun = header & 0x80;
            state.packetCount = state.packetIsRun == 0 ? header : static_cast<std::uint32_t>(header) - 0x80;
        } else {
            --state.packetCount;
            if (state.packetIsRun != 0)
                previous = true;
        }
    }
    if (!previous) {
        switch (depth) {
        case 8: {
            std::uint8_t value = ReadByte(stream);
            state.indexB = value;
            state.blueB = value;
            state.greenB = value;
            state.redB = value;
            break;
        }
        case 15:
        case 16: {
            std::uint8_t low = ReadByte(stream);
            std::uint8_t high = ReadByte(stream);
            state.indexB = static_cast<std::uint32_t>(high) * 256 + low;
            state.greenB = static_cast<std::uint8_t>((high & 3) * 64 + ((low >> 2) & 0x38));
            state.redB = static_cast<std::uint8_t>((high & 0xFC) << 1);
            state.blueB = static_cast<std::uint8_t>(low << 3);
            break;
        }
        case 24:
        case 32:
            state.blueB = ReadByte(stream);
            state.greenB = ReadByte(stream);
            state.redB = ReadByte(stream);
            if (depth == 32)
                ReadByte(stream);
            state.indexB = 0;
            break;
        default:
            break;
        }
    }
    if (state.colorMapped != 0) {
        if (state.indexB >= 0x4000) {
            BC_NOT_RECONSTRUCTED(0x8256dc);  // B0033: the exe reads outside the colour map
            return;
        }
        std::uint32_t color = state.palette[state.indexB];
        destination[0] = static_cast<std::uint8_t>(color);
        destination[1] = static_cast<std::uint8_t>(color >> 8);
        destination[2] = static_cast<std::uint8_t>(color >> 16);
    } else {
        destination[0] = state.redB;
        destination[1] = state.greenB;
        destination[2] = state.blueB;
    }
}

// The pixel read of the loader for an image with alpha (0x824f86 to 0x82526b).
void ReadPixelRgba(ImageStream* stream, std::uint8_t* destination, std::uint32_t depth)
{
    TgaState& state = g_tgaState;
    bool previous = false;
    if (state.runLength != 0) {
        if (state.packetCount == 0) {
            std::uint8_t header = ReadByte(stream);
            state.packetIsRun = header & 0x80;
            state.packetCount = state.packetIsRun == 0 ? header : static_cast<std::uint32_t>(header) - 0x80;
        } else {
            --state.packetCount;
            if (state.packetIsRun != 0)
                previous = true;
        }
    }
    if (!previous) {
        switch (depth) {
        case 8: {
            std::uint8_t value = ReadByte(stream);
            state.alpha = 0xFF;
            state.indexA = value;
            state.blue = value;
            state.green = value;
            state.red = value;
            break;
        }
        case 15:
        case 16: {
            std::uint8_t low = ReadByte(stream);
            std::uint8_t high = ReadByte(stream);
            state.red = static_cast<std::uint8_t>((high & 0xFC) << 1);
            state.green = static_cast<std::uint8_t>((high & 3) * 64 + ((low >> 2) & 0x38));
            state.blue = static_cast<std::uint8_t>(low << 3);
            state.alpha = 0xFF;
            state.indexA = static_cast<std::uint32_t>(high) * 256 + low;
            break;
        }
        case 24:
            state.blue = ReadByte(stream);
            state.green = ReadByte(stream);
            state.red = ReadByte(stream);
            state.alpha = 0xFF;
            state.indexA = 0;
            break;
        case 32:
            state.blue = ReadByte(stream);
            state.green = ReadByte(stream);
            state.red = ReadByte(stream);
            state.indexA = 0;
            state.alpha = ReadByte(stream);
            break;
        default:
            break;
        }
    }
    if (state.colorMapped != 0) {
        std::uint32_t color = state.palette[0];  // B0031: the exe reads the first entry, not the entry of the pixel
        destination[0] = static_cast<std::uint8_t>(color);
        destination[1] = static_cast<std::uint8_t>(color >> 8);
        destination[2] = static_cast<std::uint8_t>(color >> 16);
        destination[3] = static_cast<std::uint8_t>(color >> 24);
    } else {
        destination[0] = state.red;
        destination[1] = state.green;
        destination[2] = state.blue;
        destination[3] = state.alpha;
    }
}

}  // namespace

// 0x824c70
bool LoadTgaFile(ImageStream* stream, NiRawImageData* raw)
{
    std::uint8_t header[0x11];
    stream->Read(header, sizeof(header));
    const std::uint8_t descriptor = ReadByte(stream);
    const bool topToBottom = ((descriptor >> 5) & 1) != 0;
    const std::uint32_t interleave = descriptor >> 6;
    const std::uint32_t idLength = header[0];
    const std::uint32_t colorMapType = header[1];
    const std::uint32_t imageType = header[2];
    const std::uint32_t entrySize = header[7];
    const std::uint32_t pixelDepth = header[16];
    if (idLength != 0)
        stream->Seek(static_cast<std::int32_t>(idLength), kSeekFromCurrent);
    const std::uint32_t width = header[12] | (static_cast<std::uint32_t>(header[13]) << 8);
    const std::uint32_t height = header[14] | (static_cast<std::uint32_t>(header[15]) << 8);

    if (imageType != 1 && imageType != 2 && imageType != 3 && imageType != 9 && imageType != 10 && imageType != 11)
        return false;

    bool hasAlpha;
    bool readColorMap;
    if (imageType != 1 && imageType != 9) {
        g_tgaState.colorMapped = 0;
        hasAlpha = pixelDepth == 32;
        if (!SupportedDepth(pixelDepth))
            return false;
        readColorMap = colorMapType != 0;
    } else {
        if (colorMapType != 1)
            return false;
        g_tgaState.colorMapped = 1;
        hasAlpha = entrySize == 32;
        if (!SupportedDepth(entrySize))
            return false;
        readColorMap = true;
    }

    if (readColorMap) {
        const std::uint32_t first = header[3] | (static_cast<std::uint32_t>(header[4]) << 8);
        const std::uint32_t length = header[5] | (static_cast<std::uint32_t>(header[6]) << 8);
        const std::uint32_t last = first + length;
        if (last + 1 >= 0x4000)
            return false;
        if (first < last)
            ReadColorMap(stream, header, first, last);
    }

    g_tgaState.runLength = (imageType == 9 || imageType == 10 || imageType == 11) ? 1 : 0;  // B0032: the counters stay
    if (!raw->Allocate(hasAlpha ? 2 : 1, width, height))
        return false;
    if (static_cast<std::int32_t>(height) <= 0)
        return true;

    RowWalker rows;
    rows.height = static_cast<std::int32_t>(height);
    rows.step = interleave == 2 ? 4 : (interleave == 1 ? 2 : 1);
    rows.topToBottom = topToBottom;
    for (std::int32_t remaining = rows.height; remaining != 0; --remaining) {
        const std::int32_t row = rows.Row();
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
            std::uint32_t offset = raw->bytesPerPixel * static_cast<std::uint32_t>(x) +
                                   raw->stride * static_cast<std::uint32_t>(row);
            std::uint8_t* destination = raw->pixels + static_cast<std::int32_t>(offset);
            if (hasAlpha)
                ReadPixelRgba(stream, destination, pixelDepth);
            else
                ReadPixelRgb(stream, destination, pixelDepth);
        }
        rows.Next();
    }
    return true;
}

namespace {

// The comparison of 0x85b2b0 (_stricmp), with the letters A to Z folded to a to z.
int CompareIgnoringCase(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        unsigned char x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
        if (x >= 'A' && x <= 'Z')
            x = static_cast<unsigned char>(x + 0x20);
        if (y >= 'A' && y <= 'Z')
            y = static_cast<unsigned char>(y + 0x20);
        if (x != y)
            return x < y ? -1 : 1;
        if (x == 0)
            return 0;
    }
}

}  // namespace

// 0x819000. The name is copied into a buffer of 256 bytes on the stack without a length test (B0035).
bool LoadTextureFileByExtension(const char* path, NiRawImageData* raw)
{
    if (path == nullptr)
        return false;
    const char* source = std::strncmp(path, "file://", 7) == 0 ? path + 7 : path;
    char name[256];
    if (std::strlen(source) >= sizeof(name)) {
        BC_NOT_RECONSTRUCTED(0x819047);  // B0035
        return false;
    }
    std::strcpy(name, source);
    const char* extension = std::strrchr(name, '.');
    if (extension == nullptr)
        return false;
    if (CompareIgnoringCase(extension, ".nif") == 0)
        return g_imageFiles.loadNif(name, raw);

    ImageStream* stream = g_imageFiles.openStream(name);
    if (!stream->IsOpen()) {
        g_imageFiles.closeStream(stream);
        return false;
    }
    bool loaded = false;
    if (CompareIgnoringCase(extension, ".tga") == 0)
        loaded = LoadTgaFile(stream, raw);
    else if (CompareIgnoringCase(extension, ".bmp") == 0)
        loaded = g_imageFiles.loadBmp(stream, raw);
    else if (CompareIgnoringCase(extension, ".rgb") == 0 || CompareIgnoringCase(extension, ".rgba") == 0 ||
             CompareIgnoringCase(extension, ".int") == 0 || CompareIgnoringCase(extension, ".inta") == 0)
        loaded = g_imageFiles.loadSgiRgb(stream, raw);
    g_imageFiles.closeStream(stream);
    return loaded;
}

// 0x7e15a0
NiRawImageData* LoadRawImageFile(const char* path)
{
    bool loaded = LoadTextureFileByExtension(path, ni::g_scratchRawImage);
    return loaded ? ni::g_scratchRawImage : nullptr;
}
