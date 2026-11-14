#include "Crt.h"

#include <cstdlib>
#include <new>

#include "Trap.h"

namespace crt {

static std::uint32_t g_randSeed = 1;

// 0x859df3
int rand()
{
    g_randSeed = g_randSeed * 214013u + 2531011u;
    return static_cast<int>((g_randSeed >> 16) & 0x7fff);
}

void SetRandSeed(std::uint32_t seed) { g_randSeed = seed; }
std::uint32_t GetRandSeed() { return g_randSeed; }

std::uint32_t g_localeCompareFlag = 0;

void (*atExitHandler)(AtExitFunction function) = nullptr;

// 0x8594e4
void AtExit(AtExitFunction function)
{
    if (atExitHandler != nullptr)
        atExitHandler(function);
    else
        std::atexit(function);
}

// 0x859d47
std::uint32_t wcslen16(const char16_t* text)
{
    const char16_t* end = text;
    while (*end != 0)
        ++end;
    return static_cast<std::uint32_t>(end - text);
}

// 0x85be82
int wcscmp16(const char16_t* a, const char16_t* b)
{
    int difference;
    while ((difference = static_cast<int>(*a) - static_cast<int>(*b)) == 0 && *b != 0) {
        ++a;
        ++b;
    }
    return difference < 0 ? -1 : (difference > 0 ? 1 : 0);
}

static char16_t FoldAscii(char16_t c) { return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c + 0x20) : c; }

// 0x86f8e0
int wcsicmp16(const char16_t* a, const char16_t* b)
{
    if (g_localeCompareFlag != 0)
        BC_NOT_RECONSTRUCTED(0x86f938);
    char16_t first, second;
    do {
        first = FoldAscii(*a++);
        second = FoldAscii(*b++);
    } while (first != 0 && first == second);
    return static_cast<int>(first) - static_cast<int>(second);
}

}  // namespace crt

// 0x718cb0
void* operator_new(std::size_t size) { return ::operator new(size); }

// 0x718cf0
void operator_delete(void* block) { ::operator delete(block); }
