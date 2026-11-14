#pragma once
#include <cstddef>
#include <cstdint>

#include "ExeLayout.h"

// A string of 16 bit characters. The layout is the one of a Visual C++ 6 basic_string<unsigned short> (E0065).
// The buffer is shared between strings: the byte before the first character holds the share count
// (0: one owner, 0xff: frozen and never shared). The block starts one character before the buffer.
class TGString {
public:
    static constexpr std::uint32_t kMaxSize = 0x7ffffffd;
    static constexpr std::uint8_t kFrozen = 0xff;

    explicit TGString(const char* init);
    ~TGString();
    TGString(const TGString&) = delete;
    TGString& operator=(const TGString&) = delete;

    void AssignNarrow(const char* narrow);
    void AssignNarrowChecked(const char* narrow);
    void AssignString(const TGString& other);
    int Compare(const TGString& other, bool ignoreCase) const;
    int CompareC(const char* narrow, int unused) const;
    char* NewNarrowCopy() const;

    // Helpers of the string class (the code of the VC6 template, shared in the exe).
    void Tidy(bool built);
    void Eos(std::uint32_t newLength);
    bool Grow(std::uint32_t newSize, bool trim);
    void CopyBuffer(std::uint32_t newSize);
    void Split();
    TGString& AssignChars(const char16_t* chars, std::uint32_t count);
    static void MoveChars(char16_t* destination, const char16_t* source, std::uint32_t count);
    static void CopyChars(char16_t* destination, const char16_t* source, std::uint32_t count);
    static char16_t* FillChars(char16_t* destination, char16_t value, std::uint32_t count);
    static std::uint32_t MaxSize();
    [[noreturn]] static void Xlen();
    [[noreturn]] static void Xran();

    static std::uint8_t& ShareCount(char16_t* buffer) { return reinterpret_cast<std::uint8_t*>(buffer)[-1]; }
    const char16_t* Characters() const;

    std::uint8_t allocator;    // 0x00
    char16_t* buffer;          // 0x04
    std::uint32_t length;      // 0x08
    std::uint32_t capacity;    // 0x0c
};

constexpr exe::Field kTGStringLayout[] = {
    EXE_FIELD(TGString, allocator, 0x00),
    EXE_FIELD(TGString, buffer, 0x04),
    EXE_FIELD(TGString, length, 0x08),
    EXE_FIELD(TGString, capacity, 0x0c),
};
static_assert(exe::LayoutHolds(kTGStringLayout, 0x10), "TGString layout");
