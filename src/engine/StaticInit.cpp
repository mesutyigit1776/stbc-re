#include "ClassFactoryTable.h"
#include "ColorA.h"
#include "Crt.h"
#include "NiImage.h"
#include "TGFontManager.h"
#include "TGIconManager.h"
#include "TGModelPropertyManager.h"
#include "TGPoint3.h"

void RegisterClassCreators();

// Runs the static initializers of the property manager (0x6e9530), of the point types (0x8119e0, 0x820010, 0x811a00,
// 0x811a20, 0x811a40, 0x811a60, 0x811ae0, 0x45e460 to 0x45e640), of the class factory table (0x6f08a0), of the icon
// manager (0x7299c0), of the font manager (0x7347d0) and of the lists of the images and of the renderers (0x7c4d40, 0x7e0ba0) and of the raw image that the loaders fill (0x7e0b00).
void RunStaticInitializers()
{
    TGModelPropertyManager::InitGlobal();
    NiPoint3::InitZero();
    NiPoint2::InitZero();
    NiPoint3::InitUnitX();
    NiPoint3::InitUnitY();
    NiPoint3::InitUnitZ();
    NiPoint3::InitFastSqrtTable();
    NiPoint3::RegisterFastSqrtTableCleanup();
    TGPoint3::InitModelForward();
    TGPoint3::InitModelBackward();
    TGPoint3::InitModelUp();
    TGPoint3::InitModelDown();
    TGPoint3::InitModelRight();
    TGPoint3::InitModelLeft();
    TGPoint3::InitZero();
    ClassFactoryTable::InitGlobal();
    ClassFactoryTable::RegisterDestroyAtExit();
    TGIconManager::InitGlobal();
    TGFontManager::InitGlobal();
    ni::InitImageList();
    ni::InitRendererList();
    ni::InitScratchRawImage();
}

// Runs the module initializers after all static initializers. The exe keeps the modules in a list (0x99c8a4) in
// descending order of their priority and calls the virtual function in slot 0 of each (0x71e3f0, called at
// 0x438ae6). The two modules that register the class creators have the same priority and run in the order of their
// static initializers (E0088).
void RunModuleInitializers()
{
    RegisterClassCreators();
}
