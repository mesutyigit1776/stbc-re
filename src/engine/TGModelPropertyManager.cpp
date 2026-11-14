#include "TGModelPropertyManager.h"

#include <new>

#include "Crt.h"
#include "TGString.h"
#include "Trap.h"

namespace {

// The text at 0x97dc28, which the temporary name strings start from. Its bytes are 0 in the file (E0075).
const char kEmptyText[1] = {0};

// The global at 0x999de8. It is in uninitialized data in the exe and the static initializer builds it (E0045).
alignas(TGModelPropertyManager) unsigned char g_storage[sizeof(TGModelPropertyManager)];

}  // namespace

// 0x6e9650
TGModelPropertyManager::TGModelPropertyManager()
{
    registeredFilters.Reset();
    currentFilters.Reset();
    globalTemplates.Reset();
    localTemplates.Reset();
}

// 0x6e9570. The templates are not released: their owners hold the references (E0057).
TGModelPropertyManager::~TGModelPropertyManager()
{
    localTemplates.ReleaseBlocks();
    globalTemplates.ReleaseBlocks();
    currentFilters.ReleaseBlocks();
    registeredFilters.ReleaseBlocks();
}

// 0x6ea2a0
bool TGModelPropertyManager::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6ea2a0);
    return false;
}

// 0x6ea320
bool TGModelPropertyManager::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6ea320);
    return false;
}

// 0x6e9c60
TGList* TGModelPropertyManager::GetGlobalList() { return &globalTemplates; }

// 0x6e9c70
TGList* TGModelPropertyManager::GetLocalList() { return &localTemplates; }

TGList* TGModelPropertyManager::ListFor(int local) { return local != 0 ? &localTemplates : &globalTemplates; }

// 0x6e9a50. The compare is case sensitive (E0058). The result is a borrowed pointer.
TGModelProperty* TGModelPropertyManager::FindByNameAndType(const char* name, int type, int local)
{
    for (TGListNode* node = ListFor(local)->head; node != nullptr;) {
        TGModelProperty* property = static_cast<TGModelProperty*>(node->data);
        node = node->next;
        if (!property->IsTypeOf(type))
            continue;
        TGString propertyName(kEmptyText);
        property->GetName(&propertyName);
        if (propertyName.CompareC(name, 1) == 0)
            return property;
    }
    return nullptr;
}

// 0x6e9b00. The compare is case sensitive (E0058). The result is a borrowed pointer.
TGModelProperty* TGModelPropertyManager::FindByName(const char* name, int local)
{
    for (TGListNode* node = ListFor(local)->head; node != nullptr;) {
        TGModelProperty* property = static_cast<TGModelProperty*>(node->data);
        node = node->next;
        TGString propertyName(kEmptyText);
        property->GetName(&propertyName);
        if (propertyName.CompareC(name, 1) == 0)
            return property;
    }
    return nullptr;
}

// The code of 0x6e96b0 and 0x6e9880. A template with the same name and type leaves the list and loses one
// reference; the new template does not gain one (E0057).
bool TGModelPropertyManager::RegisterTemplate(TGModelProperty* property, int local)
{
    char* name;
    {
        TGString propertyName(kEmptyText);
        property->GetName(&propertyName);
        name = propertyName.NewNarrowCopy();
    }
    TGList* list = ListFor(local);
    TGModelProperty* existing = FindByNameAndType(name, property->GetObjType(), local);
    if (existing != nullptr) {
        list->Unlink(existing);
        existing->DecRefCount();  // B0006
    }
    list->Append(property);
    operator_delete(name);
    return true;
}

// 0x6e96b0
bool TGModelPropertyManager::RegisterGlobalTemplate(TGModelProperty* property)
{
    return RegisterTemplate(property, kGlobalTemplates);
}

// 0x6e9880
bool TGModelPropertyManager::RegisterLocalTemplate(TGModelProperty* property)
{
    return RegisterTemplate(property, kLocalTemplates);
}

// 0x6ea220
bool TGModelPropertyManager::IsGlobalTemplate(const TGModelProperty* property) const
{
    return globalTemplates.Contains(property);
}

// 0x6ea260
bool TGModelPropertyManager::IsLocalTemplate(const TGModelProperty* property) const
{
    return localTemplates.Contains(property);
}

// 0x6e9c80. The template leaves the list without a change of its count; with destroy it is deleted.
bool TGModelPropertyManager::RemoveTemplate(int local, TGModelProperty* property, bool destroy)
{
    if (!ListFor(local)->Unlink(property))
        return false;
    if (destroy && property != nullptr)
        delete property;
    return true;
}

// The code of 0x6e9c00 and 0x6e9ba0: every template loses one reference, then the pool of the list is freed.
void TGModelPropertyManager::ReleaseTemplates(TGList& list)
{
    for (TGListNode* node = list.head; node != nullptr;) {
        TGModelProperty* property = static_cast<TGModelProperty*>(node->data);
        node = node->next;
        property->DecRefCount();
    }
    list.ReleaseBlocks();
}

// 0x6e9c00
void TGModelPropertyManager::ClearGlobalTemplates() { ReleaseTemplates(globalTemplates); }

// 0x6e9ba0
void TGModelPropertyManager::ClearLocalTemplates() { ReleaseTemplates(localTemplates); }

// 0x6ea0e0
void TGModelPropertyManager::RegisterFilter(ModelPropertyFilter, const char*) { BC_NOT_RECONSTRUCTED(0x6ea0e0); }

// 0x6e9de0
void TGModelPropertyManager::AddFilter(ModelPropertyFilter) { BC_NOT_RECONSTRUCTED(0x6e9de0); }

// 0x6e9e50
bool TGModelPropertyManager::RemoveFilter(ModelPropertyFilter)
{
    BC_NOT_RECONSTRUCTED(0x6e9e50);
    return false;
}

// 0x6e9f40
bool TGModelPropertyManager::ApplyFilters(TGModelProperty*)
{
    BC_NOT_RECONSTRUCTED(0x6e9f40);
    return false;
}

// 0x6ea1a0
void TGModelPropertyManager::ClearRegisteredFilters() { BC_NOT_RECONSTRUCTED(0x6ea1a0); }

// 0x6e9d90
void TGModelPropertyManager::ClearCurrentFilters() { BC_NOT_RECONSTRUCTED(0x6e9d90); }

TGModelPropertyManager* TGModelPropertyManager::Global() { return reinterpret_cast<TGModelPropertyManager*>(g_storage); }

// 0x6e9530, the static initializer at the entry 0x8d0fec of the initializer table
void TGModelPropertyManager::InitGlobal()
{
    ConstructGlobal();
    RegisterGlobalCleanup();
}

// 0x6e9540
void TGModelPropertyManager::ConstructGlobal() { new (g_storage) TGModelPropertyManager(); }

// 0x6e9550
void TGModelPropertyManager::RegisterGlobalCleanup() { crt::AtExit(&TGModelPropertyManager::DestructGlobal); }

// 0x6e9560
void TGModelPropertyManager::DestructGlobal() { Global()->TGModelPropertyManager::~TGModelPropertyManager(); }
