#include "TGModelPropertySet.h"

#include <cstring>

#include "ClassFactoryTable.h"
#include "Trap.h"

namespace {

// The text at 0x97dc28, which the temporary name strings start from. Its bytes are 0 in the file (E0075).
const char kEmptyText[1] = {0};

}  // namespace

// 0x6eb040
TGModelPropertySet::TGModelPropertySet(int requestedId) : TGObject(requestedId)
{
    groups = static_cast<TGList*>(operator_new(sizeof(TGList)));
    groups->Reset();
}

// 0x5dea10, the same code as the constructor 0x6eb040 with the ID 0
TGModelPropertySet::TGModelPropertySet() : TGModelPropertySet(0) {}

// 0x6eb0d0, called by the deleting destructor 0x596400. ~TGObject runs after this body.
TGModelPropertySet::~TGModelPropertySet()
{
    TGListNode* node = groups->head;
    while (node != nullptr) {
        TGModelPropertyGroup* group = static_cast<TGModelPropertyGroup*>(node->data);
        node = node->next;
        if (group == nullptr)
            continue;
        for (TGListNode* entry = group->items.head; entry != nullptr;) {
            TGModelPropertyInstance* instance = static_cast<TGModelPropertyInstance*>(entry->data);
            entry = entry->next;
            delete instance;
        }
        group->items.ReleaseBlocks();
        delete group;
    }
    groups->ReleaseBlocks();
    operator_delete(groups);
}

// 0x5963a0
int TGModelPropertySet::GetObjType() { return kObjType; }

// 0x5963b0
bool TGModelPropertySet::IsTypeOf(int type) { return type == kObjType || type == TGObject::kObjType; }

// 0x6eb730
bool TGModelPropertySet::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6eb730);
    return false;
}

// 0x6eb780
bool TGModelPropertySet::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6eb780);
    return false;
}

// 0x6eb8f0
bool TGModelPropertySet::CheckLinks()
{
    BC_NOT_RECONSTRUCTED(0x6eb8f0);
    return false;
}

// 0x6eb940
void TGModelPropertySet::FixLinks() { BC_NOT_RECONSTRUCTED(0x6eb940); }

// 0x5963d0
const char* TGModelPropertySet::GetClassName() { return "TGModelPropertySet"; }

// 0x5963e0
const char* TGModelPropertySet::GetSwigPointerName() { return "_p_TGModelPropertySet"; }

// 0x5963f0
const char* TGModelPropertySet::GetShadowClassName() { return "TGModelPropertySetPtr"; }

// 0x6eb4f0, compares the narrow names byte by byte (case sensitive)
TGModelPropertyGroup* TGModelPropertySet::FindGroup(const char* hardpoint)
{
    for (TGListNode* node = groups->head; node != nullptr; node = node->next) {
        TGModelPropertyGroup* group = static_cast<TGModelPropertyGroup*>(node->data);
        if (std::strcmp(group->hardpointName, hardpoint) == 0)
            return group;
    }
    return nullptr;
}

// 0x6eb590
TGModelPropertyList* TGModelPropertySet::GetPropertiesByType(int type)
{
    TGModelPropertyList* result = new TGModelPropertyList();
    for (TGListNode* node = groups->head; node != nullptr; node = node->next) {
        TGModelPropertyGroup* group = static_cast<TGModelPropertyGroup*>(node->data);
        if (group == nullptr)
            continue;
        for (TGListNode* entry = group->items.head; entry != nullptr; entry = entry->next) {
            TGModelPropertyInstance* instance = static_cast<TGModelPropertyInstance*>(entry->data);
            if (instance->property->IsTypeOf(type))
                result->items.Append(instance);
        }
    }
    return result;
}

// 0x6eb220. The template names are compared without case (E0058); the hardpoint names in FindGroup with case.
bool TGModelPropertySet::AddToSet(const char* hardpoint, TGModelProperty* prop)
{
    TGModelPropertyGroup* group = FindGroup(hardpoint);
    if (group == nullptr) {
        group = new TGModelPropertyGroup();
        group->SetHardpointName(hardpoint);
        groups->Append(group);
    }
    TGString name(kEmptyText);
    prop->GetName(&name);
    bool replaced = false;
    for (TGListNode* node = group->items.head; node != nullptr && !replaced; node = node->next) {
        TGModelPropertyInstance* instance = static_cast<TGModelPropertyInstance*>(node->data);
        TGString existing(kEmptyText);
        instance->originalTemplate->GetName(&existing);
        if (existing.Compare(name, true) == 0) {
            instance->SetProperty(prop->Duplicate());  // B0002: the copy ends with the count 2
            instance->SetOriginalTemplate(prop);
            replaced = true;
        }
    }
    if (!replaced)
        group->items.Append(new TGModelPropertyInstance(prop, hardpoint));
    return true;
}

// 0x6e9500, the creator that the class factory table holds for the type 0x602
TGObject* TGModelPropertySet::CreateForFactory(int id) { return new TGModelPropertySet(id); }

// 0x6eb560, a second copy of the creator
TGObject* TGModelPropertySet::Create(int id) { return new TGModelPropertySet(id); }

// inline in 0x6e8980, after the registration of the type 0x601
void TGModelPropertySet::RegisterCreator()
{
    ClassFactoryTable::Global()->Insert(kObjType, &TGModelPropertySet::CreateForFactory);
}
