#include "TGString.h"

#include <cstring>
#include <stdexcept>

#include "Crt.h"
#include "Trap.h"

// The shared empty string at 0x888b44: the header word before it is 0.
static const char16_t kEmptyBlock[2] = {0, 0};
static const char16_t* const kEmptyString = kEmptyBlock + 1;

// The text at 0x97dc28, which the exe passes to the constructor of a temporary string. Its bytes are 0 in the file.
static const char kDefaultInit[1] = {0};

static bool IsShared(char16_t* buffer)
{
    return buffer != nullptr && TGString::ShareCount(buffer) != 0 && TGString::ShareCount(buffer) != TGString::kFrozen;
}

static void ReleaseBlock(char16_t* buffer)
{
    std::uint8_t& count = TGString::ShareCount(buffer);
    if (count == 0 || count == TGString::kFrozen)
        operator_delete(buffer - 1);
    else
        --count;
}

// 0x6f3e90
TGString::TGString(const char* init) : allocator(static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(init))),
                                       buffer(nullptr), length(0), capacity(0)
{
    AssignNarrow(init);
}

// 0x6f3ef0
TGString::~TGString() { Tidy(true); }

// the shared empty string at 0x888b44 stands in for a missing buffer in 0x6f4450, 0x6f4ee0 and 0x6f56f0
const char16_t* TGString::Characters() const { return buffer ? buffer : kEmptyString; }

// 0x4166d0
void TGString::Tidy(bool built)
{
    if (built && buffer != nullptr)
        ReleaseBlock(buffer);
    buffer = nullptr;
    length = 0;
    capacity = 0;
}

// 0x416720
void TGString::Eos(std::uint32_t newLength)
{
    length = newLength;
    buffer[newLength] = 0;
}

// 0x416740
bool TGString::Grow(std::uint32_t newSize, bool trim)
{
    if (newSize > kMaxSize)
        Xlen();
    if (IsShared(buffer)) {
        if (newSize == 0) {
            --ShareCount(buffer);
            Tidy(false);
            return false;
        }
        CopyBuffer(newSize);
        return true;
    }
    if (newSize == 0) {
        if (trim) {
            Tidy(true);
        } else if (buffer != nullptr) {
            length = 0;
            buffer[0] = 0;
        }
        return false;
    }
    if (trim) {
        if (capacity <= 0x1f && capacity >= newSize)
            return true;
        Tidy(true);
        CopyBuffer(newSize);
        return true;
    }
    if (capacity < newSize)
        CopyBuffer(newSize);
    return true;
}

// 0x416870
void TGString::CopyBuffer(std::uint32_t newSize)
{
    std::uint32_t rounded = newSize | 0x1f;
    if (rounded > kMaxSize)
        rounded = newSize;
    char16_t* block = static_cast<char16_t*>(operator_new((rounded + 2) * 2u));
    std::uint32_t oldLength = length;
    if (oldLength > 0)
        CopyChars(block + 1, buffer, oldLength < rounded ? oldLength : rounded);
    if (buffer != nullptr)
        ReleaseBlock(buffer);
    length = 0;
    buffer = block + 1;
    ShareCount(buffer) = 0;
    capacity = rounded;
    Eos(oldLength > rounded ? rounded : oldLength);
}

// 0x416980
void TGString::Split()
{
    if (!IsShared(buffer))
        return;
    char16_t* shared = buffer;
    --ShareCount(shared);
    buffer = nullptr;
    length = 0;
    capacity = 0;
    AssignChars(shared, crt::wcslen16(shared));
}

// 0x6f5820
TGString& TGString::AssignChars(const char16_t* chars, std::uint32_t count)
{
    if (count > kMaxSize)
        Xlen();
    if (IsShared(buffer)) {
        if (count == 0) {
            --ShareCount(buffer);
            Tidy(false);
            return *this;
        }
        CopyBuffer(count);
    } else {
        if (count == 0) {
            Tidy(true);
            return *this;
        }
        if (!(capacity <= 0x1f && capacity >= count)) {
            Tidy(true);
            CopyBuffer(count);
        }
    }
    CopyChars(buffer, chars, count);
    Eos(count);
    return *this;
}

// 0x40e360
void TGString::MoveChars(char16_t* destination, const char16_t* source, std::uint32_t count)
{
    std::memmove(destination, source, count * sizeof(char16_t));
}

// 0x40e330
void TGString::CopyChars(char16_t* destination, const char16_t* source, std::uint32_t count)
{
    for (std::uint32_t i = 0; i < count; ++i)
        destination[i] = source[i];
}

// 0x6f41a0
char16_t* TGString::FillChars(char16_t* destination, char16_t value, std::uint32_t count)
{
    for (std::uint32_t i = 0; i < count; ++i)
        destination[i] = value;
    return destination;
}

// 0x416dd0
std::uint32_t TGString::MaxSize() { return kMaxSize; }

// 0x857cd7
void TGString::Xlen() { throw std::length_error("string too long"); }

// 0x857f03
void TGString::Xran() { throw std::out_of_range("invalid string position"); }

// 0x6f47c0
void TGString::AssignNarrow(const char* narrow)
{
    std::uint32_t count = narrow ? static_cast<std::uint32_t>(std::strlen(narrow)) : 0;
    if (length >= count) {
        // The shrink to the new length: the string is made unique, then its tail is erased.
        Split();
        std::uint32_t tail = length - count;
        if (tail > 0) {
            MoveChars(buffer + count, buffer + count + tail, length - tail - count);
            std::uint32_t newLength = length - tail;
            if (Grow(newLength, false))
                Eos(newLength);
        }
    } else {
        std::uint32_t missing = count - length;
        if (!(0xffffffffu - length > missing))
            Xlen();
        std::uint32_t newLength = length + missing;
        if (Grow(newLength, false)) {
            FillChars(buffer + length, 0, missing);
            Eos(newLength);
        }
    }
    // Each character is stored through the non-const index operator: the buffer is made unique and frozen.
    for (std::uint32_t i = 0; i < count; ++i) {
        if (length < i || buffer == nullptr)
            BC_NOT_RECONSTRUCTED(0x6f4945);
        if (IsShared(buffer)) {
            if (MaxSize() < length)
                Xlen();
            Grow(length, false);
        }
        if (buffer != nullptr)
            ShareCount(buffer) = kFrozen;
        buffer[i] = static_cast<std::uint8_t>(narrow[i]);
    }
}

// 0x6f4cc0
void TGString::AssignNarrowChecked(const char* narrow)
{
    if (narrow != nullptr)
        AssignNarrow(narrow);
}

// 0x6f4ee0
void TGString::AssignString(const TGString& other)
{
    std::uint32_t count = other.length;
    if (this == &other) {
        if (length < count)
            Xran();
        Split();
        Split();
        return;
    }
    std::uint8_t otherShare = other.buffer ? ShareCount(other.buffer) : 0;
    if (count > 0 && count == other.length && otherShare < 0xfe) {
        if (other.buffer == nullptr)
            BC_NOT_RECONSTRUCTED(0x6f4f72);
        Tidy(true);
        buffer = other.buffer;
        length = other.length;
        capacity = other.capacity;
        ++ShareCount(buffer);
        return;
    }
    if (Grow(count, true)) {
        CopyChars(buffer, other.Characters(), count);
        Eos(count);
    }
}

// 0x6f4450
int TGString::Compare(const TGString& other, bool ignoreCase) const
{
    if (!ignoreCase)
        return crt::wcscmp16(Characters(), other.Characters());
    return crt::wcsicmp16(Characters(), other.Characters());
}

// 0x6f43e0
int TGString::CompareC(const char* narrow, int) const
{
    TGString probe(kDefaultInit);
    probe.AssignNarrowChecked(narrow);
    return Compare(probe, false);
}

// 0x6f56f0
char* TGString::NewNarrowCopy() const
{
    std::uint32_t count = length;
    char* out = static_cast<char*>(operator_new(count + 1));
    for (std::uint32_t i = 0; i < count; ++i) {
        char16_t c = buffer ? buffer[i] : kEmptyString[0];
        out[i] = c > 0xff ? 1 : static_cast<char>(c);
    }
    out[count] = 0;
    return out;
}
