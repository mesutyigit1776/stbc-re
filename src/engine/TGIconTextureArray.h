#pragma once
#include <cstdint>

#include "Crt.h"
#include "ExeLayout.h"
#include "NiRefObject.h"

// A growable array of texture properties. Each element holds a reference. A texture handle is an index into the
// array. The element block is made like the C++ array operator new of the compiler: the number of elements is
// stored in front of the first element (E0182). The vtable at 0x897bec has one slot, the deleting destructor.
// Functions that take an object `owned` take over one reference of it, as the by-value smart pointer parameters of
// the exe do: the caller acquires the reference first.
class TGIconTextureArray {
public:
    static constexpr std::uint32_t kInitialCapacity = 1;
    static constexpr std::uint32_t kGroupCapacity = 0xf;
    static constexpr std::uint32_t kGroupGrowBy = 10;

    TGIconTextureArray();
    virtual ~TGIconTextureArray();

    void Reallocate(std::uint32_t newCapacity);
    void Add(NiTextureProperty* owned);
    std::uint32_t SetAtGrow(std::uint32_t index, NiTextureProperty* owned);
    void StoreAt(std::uint32_t index, NiTextureProperty* owned);
    void GetAt(NiTextureProperty** out, std::uint32_t index);
    void RemoveAt(NiTextureProperty** out, std::uint32_t index);
    static void AssignElement(NiTextureProperty** element, NiTextureProperty* const* source);

    NiTextureProperty** data;   // 0x04 (0x20 in the group)
    std::uint32_t capacity;     // 0x08
    std::uint32_t used;         // 0x0c
    std::int32_t count;         // 0x10, the number of elements that are not null
    std::uint32_t growBy;       // 0x14

private:
    static NiTextureProperty** NewElements(std::uint32_t number);
    static void DeleteElements(NiTextureProperty** elements);
};

constexpr exe::Field kTGIconTextureArrayLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGIconTextureArray, data, 0x04),
    EXE_FIELD(TGIconTextureArray, capacity, 0x08),
    EXE_FIELD(TGIconTextureArray, used, 0x0c),
    EXE_FIELD(TGIconTextureArray, count, 0x10),
    EXE_FIELD(TGIconTextureArray, growBy, 0x14),
};
static_assert(exe::LayoutHolds(kTGIconTextureArrayLayout, 0x18), "TGIconTextureArray layout");
