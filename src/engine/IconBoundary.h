#pragma once
#include "NiImage.h"

class PyWrapperObject;

// The calls of the icon classes into the renderer, NetImmerse and the script layer. The exe functions behind them are
// not reconstructed. A test or the script host installs the functions; the default of each one stops at the exe
// address (BC_NOT_RECONSTRUCTED), except the flush, which does nothing when no renderer is present, and the creation of
// an image, which is reconstructed.
struct IconBoundary {
    // 0x809fe0 on a block of 0x1c bytes from operator_new: a new NiTextureProperty with the count 0.
    NiTextureProperty* (*newTextureProperty)();
    // 0x7e0c50: the image of a file, or null. The count of a new image is 0. The default is the reconstruction
    // (NiImage::CreateFromFile) with the renderer and the device functions of the image code.
    NiImage* (*createTexture)(const char* path, bool flag);
    // 0x728270 with the object at 0x99d040: finishes the pending drawing commands. Null: nothing is pending.
    void (*flushRenderQueue)();
    // 0x6f8ab0 with no result format and no arguments: calls the function of a script module. Returns 0 or -1.
    int (*callModuleFunction)(const char* module, const char* function);
    // 0x6f9920: the Python object of an icon group.
    PyWrapperObject* (*newPythonWrapper)(void* object);
};

extern IconBoundary g_iconBoundary;

// Puts the default functions into g_iconBoundary.
void ResetIconBoundary();

// The working directory with a trailing backslash, at 0x997bc0 (0x104 bytes). The start-up code of the exe fills it
// from GetCurrentDirectory (0x6cd841). LoadIconTexture puts it before a file name that has no drive (E0188).
extern char g_workingDirectory[0x104];
