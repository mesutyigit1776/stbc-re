#include "TGFontManager.h"

#include <cstring>
#include <new>

#include "CrtFloat.h"
#include "Trap.h"

namespace {

// The global at 0x99d8e0. It is in uninitialized data in the exe and the static initializer builds it (E0227).
alignas(TGFontManager) unsigned char g_storage[sizeof(TGFontManager)];

// The stack buffer of 0x734c20 for the icon group name: 0x80 bytes between the locals and the saved exception link
// (E0232).
constexpr std::size_t kNameBufferSize = 0x80;

}  // namespace

// Inline in 0x734c20: the base constructor is 0x747990; the vtable then changes to 0x897e64 and the size is stored.
TGFontRegistration::TGFontRegistration(const char* familyName, const char* moduleName, const char* functionName,
                                       float fontSize)
    : TGIconRegistration(familyName, moduleName, functionName), size(fontSize)
{
}

// 0x7354d0
bool TGFontRegistration::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x7354d0);
    return false;
}

// 0x7358e0
bool TGFontRegistration::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x7358e0);
    return false;
}

// 0x7348f0. A null name stays null.
TGFontFace::TGFontFace(const char* familyName) : name(nullptr)
{
    registrations.Reset();
    loadedGroups.Reset();
    if (familyName != nullptr)
        name = NewStringCopy(familyName);
}

// 0x7349b0. The name and the blocks of the two lists are freed; the registrations and the groups are not (B0039).
TGFontFace::~TGFontFace()
{
    operator_delete(name);
    loadedGroups.ReleaseBlocks();  // B0039
    registrations.ReleaseBlocks();
}

// 0x734c20. The registration goes to the tail of the list first. Then the group name is the family name and the size
// with two decimals; the icon manager registers the group with the module and the function (E0232).
void TGFontFace::AddRegistration(float size, const char* module, const char* function)
{
    TGFontRegistration* record = new TGFontRegistration(name, module, function, size);
    registrations.Append(record);
    if (std::strlen(name) >= kNameBufferSize) {
        BC_NOT_RECONSTRUCTED(0x734d68);  // B0040: the exe writes past the buffer of 0x80 bytes
        return;
    }
    char text[kNameBufferSize + 64];
    crt::Sprintf(text, "%s%.2f", name, static_cast<double>(size));
    if (std::strlen(text) >= kNameBufferSize) {
        BC_NOT_RECONSTRUCTED(0x734d68);  // B0040
        return;
    }
    TGIconManager::Global()->RegisterIconGroup(text, module, function);
}

// 0x734810
TGFontManager::TGFontManager() : defaultName(nullptr), defaultSize(0)
{
    families.Reset();
}

// 0x734840. The families are destroyed and freed, then the default name; the blocks of the list go last.
TGFontManager::~TGFontManager()
{
    for (TGListNode* node = families.head; node != nullptr;) {
        TGFontFace* face = static_cast<TGFontFace*>(node->data);
        node = node->next;
        if (face != nullptr)
            delete face;
    }
    if (defaultName != nullptr)
        operator_delete(defaultName);
    families.ReleaseBlocks();
}

// 0x735410
bool TGFontManager::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x735410);
    return false;
}

// 0x735500
bool TGFontManager::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x735500);
    return false;
}

// 0x734a30. The family is looked for by name from the head of the list; a new family goes to the tail. The first
// font that is registered becomes the default font. A null name stops in the compare (B0041).
void TGFontManager::RegisterFont(const char* name, float size, const char* module, const char* function)
{
    TGFontFace* face = nullptr;
    for (TGListNode* node = families.head; node != nullptr; node = node->next) {
        TGFontFace* candidate = static_cast<TGFontFace*>(node->data);
        if (std::strcmp(name, candidate->name) == 0) {  // B0041
            face = candidate;
            break;
        }
    }
    if (face == nullptr) {
        face = new TGFontFace(name);
        families.Append(face);
    }
    face->AddRegistration(size, module, function);
    if (defaultName == nullptr) {
        defaultName = NewStringCopy(name);
        defaultSize = size;
    }
}

TGFontManager* TGFontManager::Global() { return reinterpret_cast<TGFontManager*>(g_storage); }

// 0x7347d0, the static initializer at the entry 0x8d40e0 of the initializer table
void TGFontManager::InitGlobal()
{
    ConstructGlobal();
    RegisterGlobalCleanup();
}

// 0x7347e0
void TGFontManager::ConstructGlobal() { ::new (g_storage) TGFontManager(); }

// 0x7347f0
void TGFontManager::RegisterGlobalCleanup() { crt::AtExit(&TGFontManager::DestructGlobal); }

// 0x734800
void TGFontManager::DestructGlobal() { Global()->TGFontManager::~TGFontManager(); }
