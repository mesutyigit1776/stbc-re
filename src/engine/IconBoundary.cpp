#include "IconBoundary.h"

#include <new>

#include "Crt.h"
#include "Trap.h"

namespace {

// 0x809fe0 on a block of 0x1c bytes from operator_new (LoadIconTexture, 0x72c008 to 0x72c02b)
NiTextureProperty* DefaultNewTextureProperty()
{
    void* block = operator_new(sizeof(NiTextureProperty));
    return block != nullptr ? ::new (block) NiTextureProperty() : nullptr;
}

// 0x7e0c50 with the arguments of 0x72c12c: the mode is the flag, the mask is 7 and the kind is 2.
NiImage* DefaultCreateTexture(const char* path, bool flag)
{
    return NiImage::CreateFromFile(path, flag ? 1 : 0, 7, 2);
}

// 0x6f8ab0
int DefaultCallModuleFunction(const char*, const char*)
{
    BC_NOT_RECONSTRUCTED(0x6f8ab0);
    return -1;
}

// 0x6f9920
PyWrapperObject* DefaultNewPythonWrapper(void*)
{
    BC_NOT_RECONSTRUCTED(0x6f9920);
    return nullptr;
}

}  // namespace

char g_workingDirectory[0x104] = {0};

IconBoundary g_iconBoundary = {DefaultNewTextureProperty, DefaultCreateTexture, nullptr, DefaultCallModuleFunction,
                               DefaultNewPythonWrapper};

// Puts the default functions back.
void ResetIconBoundary()
{
    g_iconBoundary = {DefaultNewTextureProperty, DefaultCreateTexture, nullptr, DefaultCallModuleFunction,
                      DefaultNewPythonWrapper};
}
