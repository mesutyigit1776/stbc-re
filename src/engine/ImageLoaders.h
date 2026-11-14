#pragma once
#include <cstdint>

#include "NiImage.h"

// The file stream that the image loaders read. The exe object is made by 0x86e550 and is not reconstructed. Slot 2
// (offset 8) of its vtable reads count bytes. 0x86e6a0, which is not virtual in the exe, moves the position.
class ImageStream {
public:
    virtual ~ImageStream() = default;
    // Slot 1 (offset 4): true when the file is open.
    virtual bool IsOpen() = 0;
    virtual void Read(void* destination, std::uint32_t count) = 0;
    virtual void Seek(std::int32_t offset, std::uint32_t mode) = 0;
};

// The value of the global at 0x89c4c8 that the loaders pass as the mode of Seek: from the current position.
constexpr std::uint32_t kSeekFromCurrent = 1;

// The state that the TGA loader keeps between its calls: the colour map, the values of the last pixel and the counters
// of the run length packets. The state is not reset at the start of a file (B0032).
struct TgaState {
    std::uint32_t indexB;        // 0x9a2e30, the pixel value in 0x8254e0
    std::uint8_t red;            // 0x9a2e34
    std::uint8_t runLength;      // 0x9a2e35, 1 for the image types 9, 10 and 11
    std::uint8_t green;          // 0x9a2e36
    std::uint8_t blueB;          // 0x9a2e39, in 0x8254e0
    std::uint8_t redB;           // 0x9a2e3a, in 0x8254e0
    std::uint8_t alpha;          // 0x9a2e3b
    std::uint8_t colorMapped;    // 0x9a2e3c
    std::uint8_t greenB;         // 0x9a2e3d, in 0x8254e0
    std::uint32_t indexA;        // 0x9a2e40, the pixel value in 0x824c70
    std::uint32_t palette[0x4000];  // 0x9a2e44, one entry for each colour: red, green, blue, alpha in the byte order
    std::uint8_t blue;           // 0x9b2e48
    std::uint32_t packetCount;   // 0x9b2e4c, the pixels that are left in the packet
    std::uint32_t packetIsRun;   // 0x9b2e50, 0x80 for a packet that repeats one pixel
};
extern TgaState g_tgaState;

// 0x824c70. Reads a TGA file into the raw image: the image types 1, 2, 3, 9, 10 and 11 with 8, 15, 16, 24 or 32 bits for
// each pixel. The width is the header field at offset 12 and the height the field at offset 14; they go unchanged to
// Allocate (E0200). A pixel has the order red, green, blue (and alpha for 32 bits).
bool LoadTgaFile(ImageStream* stream, NiRawImageData* raw);

// 0x819000. Loads the file of the path into the raw image by its extension: '.nif' through the NetImmerse reader, '.tga',
// '.bmp' and the SGI extensions ('.rgb', '.rgba', '.int', '.inta') through the loaders. A prefix 'file://' is dropped. The
// extension test ignores the case. Gives false for another extension or a file that does not open.
bool LoadTextureFileByExtension(const char* path, NiRawImageData* raw);
