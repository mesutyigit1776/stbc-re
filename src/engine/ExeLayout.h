#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// The exe is a 32-bit program: its pointers are 4 bytes. A class of this source tree keeps the field order and
// the field types of the exe, but its pointers are wider on a 64-bit host. Each class lists the exe offset of
// every field in a table. LayoutHolds checks at compile time that the table is the layout that a 32-bit
// compiler makes from the field types, and that the host fields keep the exe order.
namespace exe {

template <class T>
constexpr std::uint32_t SizeOf()
{
    return std::is_pointer<T>::value ? 4u : static_cast<std::uint32_t>(sizeof(T));
}

struct Field {
    const char* name;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t align;
    std::size_t hostOffset;
};

template <std::size_t N>
constexpr bool LayoutHolds(const Field (&fields)[N], std::uint32_t total)
{
    std::uint32_t end = 0;
    std::uint32_t widest = 1;
    std::size_t previousHost = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const Field& f = fields[i];
        std::uint32_t aligned = (end + f.align - 1) / f.align * f.align;
        if (f.offset != aligned || (i > 0 && f.hostOffset <= previousHost))
            return false;
        end = f.offset + f.size;
        previousHost = f.hostOffset;
        if (f.align > widest)
            widest = f.align;
    }
    return (end + widest - 1) / widest * widest == total;
}

}  // namespace exe

#define EXE_VPTR() ::exe::Field{"vtable", 0, 4, 4, 0}
#define EXE_FIELD(cls, member, off) \
    ::exe::Field{#member, off, ::exe::SizeOf<decltype(cls::member)>(), ::exe::SizeOf<decltype(cls::member)>(), offsetof(cls, member)}
#define EXE_FIELD_ALIGNED(cls, member, off, bytes, alignment) \
    ::exe::Field{#member, off, bytes, alignment, offsetof(cls, member)}
