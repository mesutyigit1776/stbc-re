// C entry points for the TGString tests. The first argument of a member function is the object.
#include <new>

#include "BcFields.h"
#include "Crt.h"
#include "TGString.h"

BC_FIELDS(TGString)
    BC_FIELD(TGString, buffer) BC_FIELD(TGString, length) BC_FIELD(TGString, capacity)
BC_FIELDS_END

extern "C" {

TGString* bc_new_TGString() { return new TGString(nullptr); }

TGString* bc_TGString_TGString(TGString* self, const char* init) { return new (self) TGString(init); }
void bc_TGString_Destroy(TGString* self) { self->~TGString(); }
void bc_TGString_AssignNarrow(TGString* self, const char* narrow) { self->AssignNarrow(narrow); }
void bc_TGString_AssignNarrowChecked(TGString* self, const char* narrow) { self->AssignNarrowChecked(narrow); }
void bc_TGString_AssignString(TGString* self, const TGString* other) { self->AssignString(*other); }
std::int32_t bc_TGString_Compare(const TGString* self, const TGString* other, std::uint8_t ignoreCase)
{
    return self->Compare(*other, ignoreCase != 0);
}
std::int32_t bc_TGString_CompareC(const TGString* self, const char* narrow, std::int32_t unused)
{
    return self->CompareC(narrow, unused);
}
char* bc_TGString_NewNarrowCopy(const TGString* self) { return self->NewNarrowCopy(); }

void bc_TGString_Tidy(TGString* self, std::uint8_t built) { self->Tidy(built != 0); }
void bc_TGString_Eos(TGString* self, std::uint32_t length) { self->Eos(length); }
std::uint8_t bc_TGString_Grow(TGString* self, std::uint32_t size, std::uint8_t trim)
{
    return self->Grow(size, trim != 0);
}
void bc_TGString_CopyBuffer(TGString* self, std::uint32_t size) { self->CopyBuffer(size); }
void bc_TGString_Split(TGString* self) { self->Split(); }
TGString* bc_TGString_AssignChars(TGString* self, const char16_t* chars, std::uint32_t count)
{
    return &self->AssignChars(chars, count);
}

void bc_TGString_MoveChars(char16_t* destination, const char16_t* source, std::uint32_t count)
{
    TGString::MoveChars(destination, source, count);
}
void bc_TGString_CopyChars(char16_t* destination, const char16_t* source, std::uint32_t count)
{
    TGString::CopyChars(destination, source, count);
}
void bc_TGString_FillChars(char16_t* destination, std::uint32_t value, std::uint32_t count)
{
    TGString::FillChars(destination, static_cast<char16_t>(value), count);
}
std::uint32_t bc_TGString_MaxSize_this(void*) { return TGString::MaxSize(); }

std::uint32_t bc_crt_wcslen(const char16_t* text) { return crt::wcslen16(text); }
std::int32_t bc_crt_wcscmp(const char16_t* a, const char16_t* b) { return crt::wcscmp16(a, b); }
std::int32_t bc_crt_wcsicmp(const char16_t* a, const char16_t* b) { return crt::wcsicmp16(a, b); }

}
