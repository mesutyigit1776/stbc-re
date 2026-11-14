#pragma once
#include <cstddef>
#include <cstdint>

namespace crt {
int rand();
void SetRandSeed(std::uint32_t seed);
std::uint32_t GetRandSeed();

std::uint32_t wcslen16(const char16_t* text);
int wcscmp16(const char16_t* a, const char16_t* b);
int wcsicmp16(const char16_t* a, const char16_t* b);

using AtExitFunction = void (*)();

// 0x8594e4. The handler replaces the registration with the C library; a test sets it to see the registrations.
extern void (*atExitHandler)(AtExitFunction function);
void AtExit(AtExitFunction function);

// The flag at 0x9b3a68. When it is not 0, wcsicmp16 takes the locale branch, which is not reconstructed.
extern std::uint32_t g_localeCompareFlag;
}  // namespace crt

void* operator_new(std::size_t size);
void operator_delete(void* block);
void RunStaticInitializers();
void RunModuleInitializers();

// Inline in 0x6eb220 and 0x6ed380: a copy of a narrow text, with the terminator, in a new block.
inline char* NewStringCopy(const char* text)
{
    std::size_t size = 0;
    while (text[size] != 0)
        ++size;
    ++size;
    char* copy = static_cast<char*>(operator_new(size));
    for (std::size_t i = 0; i < size; ++i)
        copy[i] = text[i];
    return copy;
}
