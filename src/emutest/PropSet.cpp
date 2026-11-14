// C entry points for the TGModelPropertySet tests and for its list, group and instance classes. The first argument
// of a member function is the object. Objects made by the factories are blank (zeroed memory): the tests run a
// constructor on them.
#include <cstring>
#include <new>

#include "BcFields.h"
#include "ExeStrings.h"
#include "SubsystemProperty.h"
#include "TGModelPropertySet.h"

using GroupNode = TGListNode;
using InstanceNode = TGListNode;
using GroupList = TGList;
using InstanceList = TGList;
using GroupBlock = TGListBlock;
using InstanceBlock = TGListBlock;

// The Python arguments of the wrapper of SubsystemProperty.GetPosition2D: the object and two float pointers.
struct WrapperPointers {
    SubsystemProperty* object;
    float* x;
    float* y;
};

struct FloatCell {
    float value;
};

BC_FIELDS(TGModelPropertySet)
    BC_FIELD(TGModelPropertySet, id) BC_FIELD(TGModelPropertySet, groups)
BC_FIELDS_END

BC_FIELDS(TGModelPropertyList)
    BC_FIELD(TGModelPropertyList, items) BC_FIELD(TGModelPropertyList, unused)
BC_FIELDS_END

BC_FIELDS(TGModelPropertyGroup)
    BC_FIELD(TGModelPropertyGroup, items) BC_FIELD(TGModelPropertyGroup, unused)
    BC_FIELD(TGModelPropertyGroup, hardpointName)
BC_FIELDS_END

BC_FIELDS(TGModelPropertyInstance)
    BC_FIELD(TGModelPropertyInstance, originalTemplate) BC_FIELD(TGModelPropertyInstance, hardpointName)
    BC_FIELD(TGModelPropertyInstance, property)
BC_FIELDS_END

BC_FIELDS(GroupNode)
    BC_FIELD(GroupNode, data) BC_FIELD(GroupNode, next) BC_FIELD(GroupNode, prev)
BC_FIELDS_END

BC_FIELDS(InstanceNode)
    BC_FIELD(InstanceNode, data) BC_FIELD(InstanceNode, next) BC_FIELD(InstanceNode, prev)
BC_FIELDS_END

BC_FIELDS(GroupList)
    BC_FIELD(GroupList, count) BC_FIELD(GroupList, head) BC_FIELD(GroupList, tail) BC_FIELD(GroupList, freeNodes)
    BC_FIELD(GroupList, blocks) BC_FIELD(GroupList, blockSize)
BC_FIELDS_END

BC_FIELDS(InstanceList)
    BC_FIELD(InstanceList, count) BC_FIELD(InstanceList, head) BC_FIELD(InstanceList, tail)
    BC_FIELD(InstanceList, freeNodes) BC_FIELD(InstanceList, blocks) BC_FIELD(InstanceList, blockSize)
BC_FIELDS_END

BC_FIELDS(GroupBlock)
    BC_FIELD(GroupBlock, nextBlock) BC_FIELD(GroupBlock, nodes)
BC_FIELDS_END

BC_FIELDS(InstanceBlock)
    BC_FIELD(InstanceBlock, nextBlock) BC_FIELD(InstanceBlock, nodes)
BC_FIELDS_END

BC_FIELDS(WrapperPointers)
    BC_FIELD(WrapperPointers, object) BC_FIELD(WrapperPointers, x) BC_FIELD(WrapperPointers, y)
BC_FIELDS_END

BC_FIELDS(FloatCell)
    BC_FIELD(FloatCell, value)
BC_FIELDS_END

namespace {

template <class T>
T* NewBlank()
{
    void* block = operator_new(sizeof(T));
    std::memset(block, 0, sizeof(T));
    return static_cast<T*>(block);
}

// The compiler drops the final stores to the virtual table pointer of a destroyed object. The exe leaves the table of
// the list class there, and the tests read the object afterwards.
void LeaveListTable(void* object)
{
    TGModelPropertyList donor;
    std::memcpy(object, static_cast<void*>(&donor), sizeof(void*));
}

constexpr std::uint32_t kNoneAddress = 0x95dd20;

}  // namespace

extern "C" {

TGModelPropertySet* bc_new_TGModelPropertySet() { return NewBlank<TGModelPropertySet>(); }
TGModelPropertyList* bc_new_TGModelPropertyList() { return NewBlank<TGModelPropertyList>(); }
TGModelPropertyGroup* bc_new_TGModelPropertyGroup() { return NewBlank<TGModelPropertyGroup>(); }
TGModelPropertyInstance* bc_new_TGModelPropertyInstance() { return NewBlank<TGModelPropertyInstance>(); }
TGListNode* bc_new_GroupNode() { return NewBlank<TGListNode>(); }
TGListNode* bc_new_InstanceNode() { return NewBlank<TGListNode>(); }
TGList* bc_new_GroupList() { return NewBlank<TGList>(); }
TGList* bc_new_InstanceList() { return NewBlank<TGList>(); }
TGListBlock* bc_new_GroupBlock() { return NewBlank<TGListBlock>(); }
TGListBlock* bc_new_InstanceBlock() { return NewBlank<TGListBlock>(); }
WrapperPointers* bc_new_WrapperPointers() { return NewBlank<WrapperPointers>(); }
FloatCell* bc_new_FloatCell() { return NewBlank<FloatCell>(); }

// TGModelPropertySet
TGModelPropertySet* bc_TGModelPropertySet_TGModelPropertySet(TGModelPropertySet* self, std::int32_t id)
{
    return ::new (self) TGModelPropertySet(id);
}
TGModelPropertySet* bc_TGModelPropertySet_TGModelPropertySet_default(TGModelPropertySet* self)
{
    return ::new (self) TGModelPropertySet();
}
void bc_TGModelPropertySet_DestructorBody(TGModelPropertySet* self) { self->TGModelPropertySet::~TGModelPropertySet(); }
TGModelPropertySet* bc_TGModelPropertySet_DeletingDestructor(TGModelPropertySet* self, std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~TGModelPropertySet();
    return self;
}
std::int32_t bc_TGModelPropertySet_GetObjType(TGModelPropertySet* self) { return self->GetObjType(); }
std::uint8_t bc_TGModelPropertySet_IsTypeOf(TGModelPropertySet* self, std::int32_t type) { return self->IsTypeOf(type); }
std::uint32_t bc_TGModelPropertySet_GetClassName(TGModelPropertySet* self)
{
    return ExeAddressOfString(self->GetClassName());
}
std::uint32_t bc_TGModelPropertySet_GetSwigPointerName(TGModelPropertySet* self)
{
    return ExeAddressOfString(self->GetSwigPointerName());
}
std::uint32_t bc_TGModelPropertySet_GetShadowClassName(TGModelPropertySet* self)
{
    return ExeAddressOfString(self->GetShadowClassName());
}
TGModelPropertyGroup* bc_TGModelPropertySet_FindGroup(TGModelPropertySet* self, const char* hardpoint)
{
    return self->FindGroup(hardpoint);
}
TGModelPropertyList* bc_TGModelPropertySet_GetPropertiesByType(TGModelPropertySet* self, std::int32_t type)
{
    return self->GetPropertiesByType(type);
}
std::uint8_t bc_TGModelPropertySet_AddToSet(TGModelPropertySet* self, const char* hardpoint, TGModelProperty* prop)
{
    return self->AddToSet(hardpoint, prop);
}
TGObject* bc_TGModelPropertySet_CreateForFactory(std::int32_t id) { return TGModelPropertySet::CreateForFactory(id); }
TGObject* bc_TGModelPropertySet_Create(std::int32_t id) { return TGModelPropertySet::Create(id); }
void bc_TGModelPropertySet_RegisterCreator() { TGModelPropertySet::RegisterCreator(); }

// The virtual slots that are not reconstructed. Each call must reach the trap of its exe address.
void bc_TGModelPropertySet_CallSlot(TGModelPropertySet* self, std::int32_t slot)
{
    switch (slot) {
    case 4: self->SaveToStream(nullptr); break;
    case 5: self->LoadFromStream(nullptr); break;
    case 6: self->CheckLinks(); break;
    case 7: self->FixLinks(); break;
    }
}

// TGModelPropertyList
TGModelPropertyList* bc_TGModelPropertyList_TGModelPropertyList(TGModelPropertyList* self)
{
    return ::new (self) TGModelPropertyList();
}
void bc_TGModelPropertyList_DestructorBody(TGModelPropertyList* self)
{
    self->TGModelPropertyList::~TGModelPropertyList();
    LeaveListTable(self);
}
TGModelPropertyList* bc_TGModelPropertyList_DeletingDestructor(TGModelPropertyList* self, std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~TGModelPropertyList();
    LeaveListTable(self);
    return self;
}

// TGModelPropertyGroup
void bc_TGModelPropertyGroup_DestructorBody(TGModelPropertyGroup* self)
{
    self->TGModelPropertyGroup::~TGModelPropertyGroup();
    LeaveListTable(self);
}
TGModelPropertyGroup* bc_TGModelPropertyGroup_DeletingDestructor(TGModelPropertyGroup* self, std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~TGModelPropertyGroup();
    LeaveListTable(self);
    return self;
}

// TGModelPropertyInstance
TGModelPropertyInstance* bc_TGModelPropertyInstance_TGModelPropertyInstance(TGModelPropertyInstance* self,
                                                                            TGModelProperty* original,
                                                                            const char* hardpoint)
{
    return ::new (self) TGModelPropertyInstance(original, hardpoint);
}
void bc_TGModelPropertyInstance_DestructorBody(TGModelPropertyInstance* self)
{
    self->TGModelPropertyInstance::~TGModelPropertyInstance();
}
TGModelPropertyInstance* bc_TGModelPropertyInstance_DeletingDestructor(TGModelPropertyInstance* self,
                                                                       std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~TGModelPropertyInstance();
    return self;
}
void bc_TGModelPropertyInstance_SetProperty(TGModelPropertyInstance* self, TGModelProperty* property)
{
    self->SetProperty(property);
}
void bc_TGModelPropertyInstance_SetOriginalTemplate(TGModelPropertyInstance* self, TGModelProperty* original)
{
    self->SetOriginalTemplate(original);
}
void bc_TGModelPropertyInstance_CallSlot(TGModelPropertyInstance* self, std::int32_t slot)
{
    switch (slot) {
    case 1: self->SaveToStream(nullptr); break;
    case 2: self->LoadFromStream(nullptr); break;
    case 3: self->CheckLinks(); break;
    case 4: self->FixLinks(); break;
    }
}

// Gives a blank list (kind 0) or group (kind 1) the virtual table of its class, without touching the fields.
void bc_set_vptr(void* object, std::int32_t kind)
{
    if (kind == 0) {
        TGModelPropertyList donor;
        std::memcpy(object, static_cast<void*>(&donor), sizeof(void*));
    } else {
        TGModelPropertyGroup donor;
        std::memcpy(object, static_cast<void*>(&donor), sizeof(void*));
    }
}

// The wrapper of SubsystemProperty.GetPosition2D. The Python arguments are the object and two float pointers.
std::uint32_t bc_Appc_SubsystemProperty_GetPosition2D(std::uint32_t, WrapperPointers* arguments)
{
    arguments->object->GetPosition2D(arguments->x, arguments->y);
    return kNoneAddress;
}

}
