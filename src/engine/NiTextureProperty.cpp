#include "Crt.h"
#include "NiImage.h"
#include "NiRefObject.h"

namespace {

// Inline in the constructors: the base constructor 0x7d87a0 sets the count to 0 and adds 1 to the count of all objects.
void InitObject(NiRefObject* object)
{
    object->refCount = 0;
    ++ni::g_objectCount;
}

}  // namespace

// 0x7dac80. The constructor also releases the extra data and the controller, which are null at that point.
NiObjectNET::NiObjectNET()
{
    InitObject(this);
    extraData = nullptr;
    controller = nullptr;
    name = nullptr;
}

// 0x7dad10. The name is freed first, then the extra data and the controller are released, then 0x7d87f0 takes 1 from the
// count of the objects.
void NiObjectNET::Destroy()
{
    operator_delete(name);
    if (extraData != nullptr) {
        ni::Release(extraData);
        extraData = nullptr;
    }
    if (controller != nullptr) {
        ni::Release(controller);
        controller = nullptr;
    }
    --ni::g_objectCount;
}

// 0x809fe0
NiTextureProperty::NiTextureProperty()
{
    flags = 0;
    texture = nullptr;
}

// 0x80a1d0
void NiTextureProperty::Destroy()
{
    if (texture != nullptr) {
        ni::Release(texture);
        texture = nullptr;
    }
    NiObjectNET::Destroy();
}

// 0x80a8a0
void NiTextureProperty::DeleteThis()
{
    Destroy();
    operator_delete(this);
}
