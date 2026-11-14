#include "TGIconManager.h"

#include <cstring>
#include <new>

#include "IconBoundary.h"
#include "Trap.h"
#include "X87.h"

namespace {

// The global at 0x99d1f0. It is in uninitialized data in the exe and the static initializer builds it (E0176).
alignas(TGIconManager) unsigned char g_storage[sizeof(TGIconManager)];

}  // namespace

// Inline in 0x747990: the three texts are copies.
TGIconRegistration::TGIconRegistration(const char* group, const char* module, const char* function)
{
    groupName = NewStringCopy(group);
    moduleName = NewStringCopy(module);
    functionName = NewStringCopy(function);
}

// 0x747a60
TGIconRegistration::~TGIconRegistration()
{
    if (groupName != nullptr)
        operator_delete(groupName);
    if (moduleName != nullptr)
        operator_delete(moduleName);
    if (functionName != nullptr)
        operator_delete(functionName);
}

// 0x747aa0
bool TGIconRegistration::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x747aa0);
    return false;
}

// 0x747ad0
bool TGIconRegistration::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x747ad0);
    return false;
}

// The first part of 0x729b30: one element of room, which is not set.
TGIconRegistrationArray::TGIconRegistrationArray() : capacity(1), used(0), count(0), growBy(1)
{
    data = static_cast<TGIconRegistration**>(operator_new(sizeof(TGIconRegistration*)));
}

// 0x72ad70, called by the deleting destructor 0x72aee0. The records are not freed.
TGIconRegistrationArray::~TGIconRegistrationArray() { operator_delete(data); }

// The code of 0x729e70 behind the new record. The array grows by growBy elements past the index and gets a new
// block with the old pointers copied.
void TGIconRegistrationArray::Append(TGIconRegistration* record)
{
    std::uint32_t index = used;
    if (capacity <= index) {
        std::uint32_t newCapacity = growBy + index;
        if (newCapacity != capacity) {
            if (newCapacity < used) {
                for (std::uint32_t i = newCapacity; i < used; ++i) {
                    if (data[i] != nullptr) {
                        data[i] = nullptr;
                        --count;
                    }
                }
                used = newCapacity;
            }
            TGIconRegistration** old = data;
            capacity = newCapacity;
            data = static_cast<TGIconRegistration**>(operator_new(newCapacity * sizeof(TGIconRegistration*)));
            for (std::uint32_t i = 0; i < used; ++i)
                data[i] = old[i];
            operator_delete(old);
        }
        if (capacity <= index)
            return;
    }
    if (index < used) {
        if (record == nullptr) {
            if (data[index] != nullptr)
                --count;
        } else if (data[index] == nullptr) {
            ++count;
        }
    } else {
        used = index + 1;
        if (record != nullptr)
            ++count;
    }
    data[index] = record;
}

// The code of 0x72a210 that takes a record out: the entry becomes null, the array moves the records together and
// gets a block that fits the count.
void TGIconRegistrationArray::RemoveAt(std::uint32_t index)
{
    if (index < used) {
        TGIconRegistration* removed = data[index];
        data[index] = nullptr;
        if (removed != nullptr)
            --count;
        if (index == used - 1)
            used = used - 1;
    }
    if (count == static_cast<std::int32_t>(used))
        return;
    if (count != 0 && used != 0) {
        std::uint32_t target = 0;
        for (std::uint32_t i = 0; i < used; ++i) {
            if (data[i] != nullptr) {
                if (data[target] != data[i])
                    data[target] = data[i];
                ++target;
            }
        }
    }
    used = static_cast<std::uint32_t>(count);
    capacity = static_cast<std::uint32_t>(count);
    TGIconRegistration** old = data;
    data = static_cast<TGIconRegistration**>(operator_new(static_cast<std::size_t>(count) * sizeof(TGIconRegistration*)));
    for (std::uint32_t i = 0; i < used; ++i)
        data[i] = old[i];
    operator_delete(old);
}

// 0x729b30
TGIconManager::TGIconManager()
    : unused48(0), unused4c(0), camera(nullptr), scale(0), screenWidth(0), screenHeight(0), pixelCenterAdj(-0.5f),
      pixelCenterAdjX(0), pixelCenterAdjY(0)
{
    groups.Reset();
    otherList.Reset();
}

// 0x729a00. The cache goes first, then the camera and the two lists; the array of the registrations is freed last
// by its own destructor. The groups and the records are not freed.
TGIconManager::~TGIconManager()
{
    textureCache.Destroy();
    ni::Release(camera);
    otherList.ReleaseBlocks();
    groups.ReleaseBlocks();
}

// 0x729e70. The record is made first; a name, module or function that is null stops in the copy (B0028).
void TGIconManager::RegisterIconGroup(const char* name, const char* module, const char* function)
{
    TGIconRegistration* record = new TGIconRegistration(name, module, function);  // B0028
    registrations.Append(record);
}

// 0x729fa0
TGIconGroup* TGIconManager::CreateIconGroup(const char* name) { return new TGIconGroup(name); }

// 0x72a400. The name comparison reads the text of the group: a null name stops here (B0021).
TGIconGroup* TGIconManager::FindLoadedGroup(const char* name)
{
    for (TGListNode* node = groups.head; node != nullptr; node = node->next) {
        TGIconGroup* group = static_cast<TGIconGroup*>(node->data);
        if (std::strcmp(name, group->name) == 0)  // B0021
            return group;
    }
    return nullptr;
}

// 0x72a000. A group with the name of a loaded group is not added and is not freed (B0025).
void TGIconManager::AddIconGroup(TGIconGroup* group)
{
    if (FindLoadedGroup(group->name) == nullptr)  // B0021
        groups.Append(group);
}

// 0x72a150. A name that was registered gives the loaded group; when the group is not loaded yet, the script
// function of the registration runs and the group is looked for again. A name that was not registered gives null,
// also when a group of that name is loaded (E0184).
TGIconGroup* TGIconManager::GetIconGroup(const char* name)
{
    for (std::uint32_t i = 0; i < registrations.used; ++i) {
        TGIconRegistration* record = registrations.data[i];
        if (std::strcmp(name, record->groupName) != 0)  // B0021
            continue;
        TGIconGroup* group = FindLoadedGroup(name);
        if (group != nullptr)
            return group;
        g_iconBoundary.callModuleFunction(record->moduleName, record->functionName);
        return FindLoadedGroup(name);
    }
    return nullptr;
}

// 0x72a210. The registration goes first, then the group leaves the list and is deleted. The renderer finishes the
// pending commands before (E0185).
bool TGIconManager::DeleteIconGroup(const char* name)
{
    TGIconGroup* group = GetIconGroup(name);
    if (group == nullptr)
        return false;
    if (g_iconBoundary.flushRenderQueue != nullptr)
        g_iconBoundary.flushRenderQueue();
    for (std::uint32_t i = 0; i < registrations.used; ++i) {
        if (std::strcmp(name, registrations.data[i]->groupName) != 0)
            continue;
        TGIconRegistration* record = registrations.data[i];
        registrations.RemoveAt(i);
        if (record != nullptr) {
            record->TGIconRegistration::~TGIconRegistration();
            operator_delete(record);
        }
        break;
    }
    groups.Unlink(group);
    delete group;
    return true;
}

// 0x72a080. A group with a lock count of 0 leaves the list and is deleted; the other groups stay.
void TGIconManager::Purge()
{
    for (TGListNode* node = groups.head; node != nullptr;) {
        TGIconGroup* group = static_cast<TGIconGroup*>(node->data);
        TGListNode* following = node->next;
        if (group->lockCount != 0) {
            node = following;
            continue;
        }
        groups.UnlinkNode(node);
        node = following;
        delete group;
    }
}

// 0x72a920
void TGIconManager::SetPixelCenterAdj(float adjustment)
{
    using x87::Reg;
    pixelCenterAdj = adjustment;
    pixelCenterAdjX = StoreF(Reg(adjustment) / Reg(screenWidth));    // B0026
    pixelCenterAdjY = StoreF(Reg(adjustment) / Reg(screenHeight));   // B0026
}

// Inline in 0x5d6160 and the first part of 0x729dc0: the new camera gets a reference and the old one loses its
// reference.
void TGIconManager::SetCamera(NiCamera* newCamera)
{
    if (camera == newCamera)
        return;
    ni::Release(camera);
    camera = newCamera;
    ni::Acquire(newCamera);
}

// 0x729dc0. The screen size comes from the drawing object of the camera; the scale is set also without a camera.
void TGIconManager::SetDisplayInfo(NiCamera* newCamera, float newScale)
{
    using x87::Reg;
    if (newCamera != nullptr) {
        SetCamera(newCamera);
        NiD3DRender* renderer = camera->renderer;
        screenWidth = StoreF(Reg(static_cast<double>(renderer->GetWidth())));
        screenHeight = StoreF(Reg(static_cast<double>(renderer->GetHeight())));
        SetPixelCenterAdj(pixelCenterAdj);
    }
    scale = newScale;
}

// 0x42e6c0
NiCameraPtr* TGIconManager::GetCameraPtr(NiCameraPtr* out)
{
    out->camera = camera;
    ni::Acquire(camera);
    return out;
}

// 0x72a690. The drawing commands go to the renderer, which is not reconstructed.
void TGIconManager::Draw()
{
    if (camera != nullptr && camera->renderer != nullptr)
        BC_NOT_RECONSTRUCTED(0x72a690);
}

TGIconManager* TGIconManager::Global() { return reinterpret_cast<TGIconManager*>(g_storage); }

// 0x7299c0, the static initializer at the entry 0x8d3b1c of the initializer table
void TGIconManager::InitGlobal()
{
    ConstructGlobal();
    RegisterGlobalCleanup();
}

// 0x7299d0
void TGIconManager::ConstructGlobal() { new (g_storage) TGIconManager(); }

// 0x7299e0
void TGIconManager::RegisterGlobalCleanup() { crt::AtExit(&TGIconManager::DestructGlobal); }

// 0x7299f0
void TGIconManager::DestructGlobal() { Global()->~TGIconManager(); }
