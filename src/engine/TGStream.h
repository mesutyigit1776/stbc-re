#pragma once
#include <cstddef>

// Only the parts that TGPoint3::LoadBinary and SaveBinary use: the reader at +0x40 and
// the writer at +0x44, each an object whose vtable slot 2 reads and slot 3 writes.
struct TGStreamBuffer {
    virtual void Slot0();
    virtual void Slot1();
    virtual void Read(void* destination, unsigned size);
    virtual void Write(const void* source, unsigned size);
};

struct TGStream {
    char unknown[0x40];
    TGStreamBuffer* reader;
    TGStreamBuffer* writer;
};

// The offsets hold for the 32-bit layout of the exe; a 64-bit host build has wider pointers.
static_assert(sizeof(void*) != 4 || (offsetof(TGStream, reader) == 0x40 && offsetof(TGStream, writer) == 0x44),
              "TGStream layout");
