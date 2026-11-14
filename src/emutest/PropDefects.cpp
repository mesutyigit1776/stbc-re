// Deliberately wrong variants of the property foundation functions. The emulator tests must reject them.
#include "Crt.h"
#include "ObjectRegistry.h"
#include "TGList.h"
#include "TGObject.h"
#include "SubsystemProperty.h"
#include "TGModelProperty.h"
#include "TGModelPropertyManager.h"
#include "TGModelPropertySet.h"
#include "TGString.h"

extern "C" {

// The compare ignores the flag for case: it always compares case sensitive.
std::int32_t bc_TGString_Compare_defect(const TGString* self, const TGString* other, std::uint8_t)
{
    return self->Compare(*other, false);
}

// The object goes into the bucket after the right one.
std::uint8_t bc_TGObject_RegisterID_defect(TGObject* object)
{
    ObjectRegistry* table = TGObject::registry;
    std::uint32_t key = static_cast<std::uint32_t>(object->id);
    std::uint32_t bucket = (table->Hash(key) + 1) % table->bucketCount;
    ObjectRegistry::Node* node = static_cast<ObjectRegistry::Node*>(operator_new(sizeof(ObjectRegistry::Node)));
    table->SetNode(node, key, object);
    node->next = table->buckets[bucket];
    table->buckets[bucket] = node;
    ++table->count;
    return 1;
}

// The node is freed and counted off, but it stays in its bucket chain.
std::uint8_t bc_TGObject_UnregisterID_defect(TGObject* object)
{
    ObjectRegistry* table = TGObject::registry;
    ObjectRegistry::Node* node = table->FindNode(static_cast<std::uint32_t>(object->id));
    if (node == nullptr)
        return 0;
    table->ClearNode(node);
    operator_delete(node);
    --table->count;
    return 1;
}

// The nodes of a new block are chained from the last to the first.
void bc_TGList_AllocNode_defect(TGList* list, TGListNode** out)
{
    if (list->freeNodes == nullptr) {
        TGListBlock* block = static_cast<TGListBlock*>(operator_new(sizeof(TGListBlock)));
        block->nodes = static_cast<TGListNode*>(operator_new(list->blockSize * sizeof(TGListNode)));
        block->nodes[0].next = nullptr;
        for (std::uint32_t i = 1; i < list->blockSize; ++i)
            block->nodes[i].next = &block->nodes[i - 1];
        block->nextBlock = list->blocks;
        list->blocks = block;
        list->freeNodes = &block->nodes[list->blockSize - 1];
    }
    *out = list->freeNodes;
    list->freeNodes = list->freeNodes->next;
}

// A freed node goes to the end of the free chain, not to its head.
void bc_TGList_FreeNode_defect(TGList* list, TGListNode* node)
{
    node->data = nullptr;
    node->next = nullptr;
    if (list->freeNodes == nullptr) {
        list->freeNodes = node;
        return;
    }
    TGListNode* last = list->freeNodes;
    while (last->next != nullptr)
        last = last->next;
    last->next = node;
}

// The string shares the buffer of the other string but does not count the new owner.
void bc_TGString_AssignString_defect(TGString* self, const TGString* other)
{
    if (self == other || other->length == 0 || other->buffer == nullptr || TGString::ShareCount(other->buffer) >= 0xfe) {
        self->AssignString(*other);
        return;
    }
    self->Tidy(true);
    self->buffer = other->buffer;
    self->length = other->length;
    self->capacity = other->capacity;
}

// The assignment keeps a block that it never frees.
void bc_TGString_AssignNarrow_defect(TGString* self, const char* narrow)
{
    self->AssignNarrow(narrow);
    operator_new(16);
}

}

extern "C" {

// The copy of the text is never made.
char* bc_TGString_NewNarrowCopy_defect(const TGString*) { return nullptr; }

}

extern "C" {

// Writes four bytes behind the end of the block of the object.
std::uint32_t bc_heap_overrun_defect(TGObject* object)
{
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(object) + sizeof(TGObject)) = 0x11223344;
    return 0;
}

// Writes four bytes in front of the start of the block of the object.
std::uint32_t bc_heap_underrun_defect(TGObject* object)
{
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(object) - 4) = 0x11223344;
    return 0;
}

// Changes nothing.
std::uint32_t bc_heap_untouched(TGObject*) { return 0; }

}

extern "C" {

// SetRadius writes the field behind the radius setter's field: the disabled percentage.
std::uint32_t bc_Appc_SubsystemProperty_SetRadius_defect(std::uint32_t, SubsystemProperty* p, float value)
{
    p->SetDisabledPercentage(value);
    return 0x95dd20;
}

// The count reaches 0 but the object is not destroyed.
void bc_TGModelProperty_DecRefCount_defect(TGModelProperty* self)
{
    if (self->refCount <= 0)
        return;
    --self->refCount;
    if (self->refCount > 0)
        return;
    self->refCount = 0;
}

// The copy takes the name buffer of the original instead of copying it: both objects own one buffer.
TGModelProperty* bc_TGModelProperty_Duplicate_defect(TGModelProperty* self)
{
    TGModelProperty* copy = self->Duplicate();
    if (copy != nullptr) {
        copy->name.Tidy(true);
        copy->name.buffer = self->name.buffer;
        copy->name.length = self->name.length;
        copy->name.capacity = self->name.capacity;
    }
    return copy;
}

// IsTypeOf does not test the type ID of TGModelProperty.
std::uint8_t bc_SubsystemProperty_IsTypeOf_defect(SubsystemProperty*, std::int32_t type)
{
    return type == SubsystemProperty::kObjType || type == TGObject::kObjType;
}

}

extern "C" {

// AddToSet compares the template names with case.
std::uint8_t bc_TGModelPropertySet_AddToSet_defect(TGModelPropertySet* self, const char* hardpoint, TGModelProperty* prop)
{
    TGModelPropertyGroup* group = self->FindGroup(hardpoint);
    if (group == nullptr) {
        group = new TGModelPropertyGroup();
        group->SetHardpointName(hardpoint);
        self->groups->Append(group);
    }
    TGString name("");
    prop->GetName(&name);
    bool replaced = false;
    for (TGListNode* node = group->items.head; node != nullptr && !replaced; node = node->next) {
        TGModelPropertyInstance* instance = static_cast<TGModelPropertyInstance*>(node->data);
        TGString existing("");
        instance->originalTemplate->GetName(&existing);
        if (existing.Compare(name, false) == 0) {
            instance->SetProperty(prop->Duplicate());
            instance->SetOriginalTemplate(prop);
            replaced = true;
        }
    }
    if (!replaced)
        group->items.Append(new TGModelPropertyInstance(prop, hardpoint));
    return 1;
}

// GetPropertiesByType returns the instances of the last group first.
TGModelPropertyList* bc_TGModelPropertySet_GetPropertiesByType_defect(TGModelPropertySet* self, std::int32_t type)
{
    TGModelPropertyList* result = self->GetPropertiesByType(type);
    TGListNode* node = result->items.head;
    TGListNode* last = result->items.tail;
    while (node != nullptr && last != nullptr && node != last && node->prev != last) {
        void* data = node->data;
        node->data = last->data;
        last->data = data;
        node = node->next;
        last = last->prev;
    }
    return result;
}

// The destructor keeps the last group of the set.
void bc_TGModelPropertySet_DestructorBody_defect(TGModelPropertySet* self)
{
    TGListNode* node = self->groups->head;
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
        if (node != nullptr)
            delete group;
    }
    self->groups->ReleaseBlocks();
    operator_delete(self->groups);
    self->TGObject::~TGObject();
}

}

extern "C" {

// FindByName ignores the case of the names.
TGModelProperty* bc_TGModelPropertyManager_FindByName_defect(TGModelPropertyManager* self, const char* name,
                                                              std::int32_t local)
{
    static const char kEmpty[1] = {0};
    TGString wanted(kEmpty);
    wanted.AssignNarrowChecked(name);
    TGList* list = local != 0 ? &self->localTemplates : &self->globalTemplates;
    for (TGListNode* node = list->head; node != nullptr; node = node->next) {
        TGModelProperty* property = static_cast<TGModelProperty*>(node->data);
        TGString propertyName(kEmpty);
        property->GetName(&propertyName);
        if (propertyName.Compare(wanted, true) == 0)
            return property;
    }
    return nullptr;
}

// A template with the same name leaves the list but does not lose a reference.
std::uint8_t bc_TGModelPropertyManager_RegisterLocalTemplate_defect(TGModelPropertyManager* self, TGModelProperty* p)
{
    static const char kEmpty[1] = {0};
    char* name;
    {
        TGString propertyName(kEmpty);
        p->GetName(&propertyName);
        name = propertyName.NewNarrowCopy();
    }
    TGModelProperty* existing = self->FindByNameAndType(name, p->GetObjType(), TGModelPropertyManager::kLocalTemplates);
    if (existing != nullptr)
        self->localTemplates.Unlink(existing);
    self->localTemplates.Append(p);
    operator_delete(name);
    return 1;
}

// ClearLocalTemplates clears the list of the global templates.
void bc_TGModelPropertyManager_ClearLocalTemplates_defect(TGModelPropertyManager* self)
{
    for (TGListNode* node = self->globalTemplates.head; node != nullptr;) {
        TGModelProperty* property = static_cast<TGModelProperty*>(node->data);
        node = node->next;
        property->DecRefCount();
    }
    self->globalTemplates.ReleaseBlocks();
}

// The destructor releases the templates, which their owners still hold.
void bc_TGModelPropertyManager_DestructorBody_defect(TGModelPropertyManager* self)
{
    for (TGListNode* node = self->localTemplates.head; node != nullptr;) {
        TGModelProperty* property = static_cast<TGModelProperty*>(node->data);
        node = node->next;
        property->DecRefCount();
    }
    self->TGModelPropertyManager::~TGModelPropertyManager();
}

}

extern "C" {

// The indent text is one level too short.
const char* bc_TGObject_GetIndentString_defect(std::int32_t level)
{
    return TGObject::GetIndentString(level > 0 ? level - 1 : 0);
}

}

extern "C" {

void bc_set_captured_integer(std::int64_t value);

// The wrapper of TGObject.IsTypeOf asks only for the own type, not for the types of the base classes.
std::uint32_t bc_Appc_TGObject_IsTypeOf_defect(std::uint32_t, TGObject* object, std::int32_t type)
{
    bc_set_captured_integer(object->GetObjType() == type);
    return 0x1A000000;
}

// Create does not set the name.
TGModelProperty* bc_TGModelProperty_Create_defect(const char*) { return new TGModelProperty(0); }

}

extern "C" {

// The setter of g_kModelPropertyManager leaves out the list of the local templates.
std::uint32_t bc_Appc_set_g_kModelPropertyManager_defect(TGModelPropertyManager* value)
{
    TGModelPropertyManager* target = TGModelPropertyManager::Global();
    target->registeredFilters = value->registeredFilters;
    target->currentFilters = value->currentFilters;
    target->globalTemplates = value->globalTemplates;
    return 0;
}

}
