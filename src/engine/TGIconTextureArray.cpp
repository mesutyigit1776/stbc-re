#include "TGIconTextureArray.h"

namespace {

// The number of elements sits in front of the first element. The exe stores 4 bytes; the host keeps the alignment
// of a pointer.
constexpr std::size_t kCookieSize = sizeof(NiTextureProperty*);

}  // namespace

// Inline in 0x72bb20: the array starts with one element and the group grows it to 0xf. The count of elements that
// are not null starts at 0.
TGIconTextureArray::TGIconTextureArray()
    : data(nullptr), capacity(kInitialCapacity), used(0), count(0), growBy(kInitialCapacity)
{
    data = NewElements(kInitialCapacity);
}

// 0x72cea0, called by the deleting destructor 0x72af90
TGIconTextureArray::~TGIconTextureArray()
{
    if (data != nullptr)
        DeleteElements(data);
}

// The array operator new of 0x72d0f0 and 0x72bb98: the block holds the number and the elements, which start as 0
// (0x72afe0).
NiTextureProperty** TGIconTextureArray::NewElements(std::uint32_t number)
{
    char* block = static_cast<char*>(operator_new(kCookieSize + number * sizeof(NiTextureProperty*)));
    if (block == nullptr)
        return nullptr;
    *reinterpret_cast<std::uint32_t*>(block) = number;
    NiTextureProperty** elements = reinterpret_cast<NiTextureProperty**>(block + kCookieSize);
    for (std::uint32_t i = 0; i < number; ++i)
        elements[i] = nullptr;
    return elements;
}

// 0x401fb0 with the flags 3: each element loses its reference, from the last to the first (0x859865), then the
// block is freed.
void TGIconTextureArray::DeleteElements(NiTextureProperty** elements)
{
    char* block = reinterpret_cast<char*>(elements) - kCookieSize;
    for (std::uint32_t i = *reinterpret_cast<std::uint32_t*>(block); i > 0; --i)
        ni::Release(elements[i - 1]);
    operator_delete(block);
}

// 0x72ce60
void TGIconTextureArray::AssignElement(NiTextureProperty** element, NiTextureProperty* const* source)
{
    if (*element == *source)
        return;
    ni::Release(*element);
    *element = *source;
    ni::Acquire(*element);
}

// The code of 0x72d085 to 0x72d15c, also inline in 0x72bbd7: a new element block of the capacity, the old
// elements copied with a reference each, the old block deleted.
void TGIconTextureArray::Reallocate(std::uint32_t newCapacity)
{
    if (newCapacity == capacity)
        return;
    if (newCapacity < used) {
        for (std::uint32_t i = newCapacity; i < used; ++i) {
            if (data[i] != nullptr) {
                NiTextureProperty* none = nullptr;
                AssignElement(&data[i], &none);
                --count;
            }
        }
        used = newCapacity;
    }
    NiTextureProperty** old = data;
    capacity = newCapacity;
    data = NewElements(newCapacity);
    for (std::uint32_t i = 0; i < used; ++i)
        AssignElement(&data[i], &old[i]);
    if (old != nullptr)
        DeleteElements(old);
}

// 0x72ced0
void TGIconTextureArray::Add(NiTextureProperty* owned)
{
    ni::Acquire(owned);
    SetAtGrow(used, owned);
    ni::Release(owned);
}

// 0x72d050
std::uint32_t TGIconTextureArray::SetAtGrow(std::uint32_t index, NiTextureProperty* owned)
{
    if (index >= capacity)
        Reallocate(growBy + index);
    ni::Acquire(owned);
    StoreAt(index, owned);
    ni::Release(owned);
    return index;
}

// 0x72d1f0
void TGIconTextureArray::StoreAt(std::uint32_t index, NiTextureProperty* owned)
{
    if (index >= capacity) {
        ni::Release(owned);
        return;
    }
    if (index >= used) {
        used = index + 1;
        if (owned != nullptr)
            ++count;
    } else {
        NiTextureProperty* previous = data[index];
        if (owned != nullptr) {
            if (previous == nullptr)
                ++count;
        } else if (previous != nullptr) {
            --count;
        }
    }
    NiTextureProperty** element = &data[index];
    if (*element != owned) {
        ni::Release(*element);
        *element = owned;
        ni::Acquire(owned);
    }
    ni::Release(owned);
}

// 0x4d7290. The index is not checked (B0023).
void TGIconTextureArray::GetAt(NiTextureProperty** out, std::uint32_t index)
{
    NiTextureProperty* element = data[index];  // B0023
    *out = element;
    ni::Acquire(element);
}

// 0x72cf40
void TGIconTextureArray::RemoveAt(NiTextureProperty** out, std::uint32_t index)
{
    if (index >= used) {
        *out = nullptr;
        return;
    }
    NiTextureProperty* element = data[index];
    ni::Acquire(element);
    NiTextureProperty** slot = &data[index];
    ni::Release(*slot);
    *slot = nullptr;
    if (element != nullptr)
        --count;
    if (index == used - 1)
        used = used - 1;
    *out = element;
    ni::Acquire(element);
    ni::Release(element);
}
