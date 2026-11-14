// C entry points for the TGObject, ObjectRegistry and ClassFactoryTable tests.
#include <cstring>
#include <new>

#include "BcFields.h"
#include "ExeStrings.h"
#include "Heap.h"
#include "TGList.h"
#include "SubsystemProperty.h"
#include "TGModelProperty.h"
#include "TGModelPropertySet.h"
#include "TGObject.h"

BC_FIELDS(TGObject)
    BC_FIELD(TGObject, id)
BC_FIELDS_END

struct RegistryPointer {
    ObjectRegistry* registry;
};
static_assert(sizeof(RegistryPointer) == sizeof(ObjectRegistry*), "RegistryPointer");

BC_FIELDS(RegistryPointer)
    BC_FIELD(RegistryPointer, registry)
BC_FIELDS_END

BC_FIELDS(ObjectRegistry)
    BC_FIELD(ObjectRegistry, count) BC_FIELD(ObjectRegistry, bucketCount) BC_FIELD(ObjectRegistry, buckets)
BC_FIELDS_END

BC_FIELDS(ObjectRegistryNode)
    BC_FIELD(ObjectRegistryNode, key) BC_FIELD(ObjectRegistryNode, value) BC_FIELD(ObjectRegistryNode, next)
BC_FIELDS_END

BC_FIELDS(ClassFactoryTable)
    BC_FIELD(ClassFactoryTable, count) BC_FIELD(ClassFactoryTable, bucketCount) BC_FIELD(ClassFactoryTable, buckets)
BC_FIELDS_END

BC_FIELDS(ClassFactoryNode)
    BC_FIELD(ClassFactoryNode, key) BC_FIELD(ClassFactoryNode, value) BC_FIELD(ClassFactoryNode, next)
BC_FIELDS_END

namespace {

// A TGObject made without entering it in the registry: the constructor runs against a scratch registry.
// Only the object stays in the heap counts.
TGObject* MakeBlankObject(int id)
{
    std::int64_t allocations = HeapAllocations(), frees = HeapFrees();
    ObjectRegistry* saved = TGObject::registry;
    std::int32_t savedNextId = TGObject::nextId;
    TGObject* object;
    {
        ObjectRegistry scratch;
        TGObject::registry = &scratch;
        object = new TGObject(1);
        TGObject::registry = saved;
        TGObject::nextId = savedNextId;
    }
    object->id = id;
    HeapDiscount(HeapAllocations() - allocations - 1, HeapFrees() - frees);
    return object;
}

// The creators that the table tests register. The exe side uses stubs at SCRATCH + 0xD000 + 0x10 * index.
constexpr std::uint32_t kCreatorBase = 0x3000D000;
constexpr std::uint32_t kCreatorStride = 0x10;

template <int Index>
TGObject* CreateTestObject(int id)
{
    return MakeBlankObject(id + 0x100 * Index);
}

const Creator kCreators[] = {CreateTestObject<0>, CreateTestObject<1>, CreateTestObject<2>, CreateTestObject<3>};

// The creators of the reconstructed classes stand for the creators in the exe.
struct RealCreator {
    std::uint32_t address;
    Creator creator;
};

const RealCreator kRealCreators[] = {{0x6e94b0, &TGModelProperty::CreateForFactory},
                                     {0x43ff30, &SubsystemProperty::CreateForFactory},
                                     {0x6e9500, &TGModelPropertySet::CreateForFactory}};

Creator CreatorFromAddress(std::uint32_t address)
{
    for (const RealCreator& real : kRealCreators) {
        if (real.address == address)
            return real.creator;
    }
    if (address < kCreatorBase)
        return nullptr;
    std::uint32_t index = (address - kCreatorBase) / kCreatorStride;
    return index < sizeof(kCreators) / sizeof(kCreators[0]) ? kCreators[index] : nullptr;
}

std::uint32_t AddressFromCreator(Creator creator)
{
    for (const RealCreator& real : kRealCreators) {
        if (real.creator == creator)
            return real.address;
    }
    for (std::uint32_t i = 0; i < sizeof(kCreators) / sizeof(kCreators[0]); ++i) {
        if (kCreators[i] == creator)
            return kCreatorBase + i * kCreatorStride;
    }
    return creator == nullptr ? 0 : 0xdeadbeef;
}

template <class Table>
void ReplaceBuckets(Table* table, typename Table::Node** blob)
{
    table->buckets = static_cast<typename Table::Node**>(operator_new(table->bucketCount * sizeof(void*)));
    std::memcpy(table->buckets, blob, table->bucketCount * sizeof(void*));
}

}  // namespace

extern "C" {

TGObject* bc_new_TGObject() { return MakeBlankObject(0); }
ObjectRegistry* bc_new_ObjectRegistry() { return new ObjectRegistry(); }
ClassFactoryTable* bc_new_ClassFactoryTable() { return new ClassFactoryTable(); }
ObjectRegistryNode* bc_new_ObjectRegistryNode()
{
    return static_cast<ObjectRegistryNode*>(operator_new(sizeof(ObjectRegistryNode)));
}
ClassFactoryNode* bc_new_ClassFactoryNode()
{
    return static_cast<ClassFactoryNode*>(operator_new(sizeof(ClassFactoryNode)));
}

// Accessors for the members that the harness fills from outside: the bucket array and the creator.
void bc_set_ObjectRegistry_buckets(ObjectRegistry* table, ObjectRegistryNode** blob) { ReplaceBuckets(table, blob); }
ObjectRegistryNode** bc_get_ObjectRegistry_buckets(ObjectRegistry* table) { return table->buckets; }
void bc_set_ClassFactoryTable_buckets(ClassFactoryTable* table, ClassFactoryNode** blob) { ReplaceBuckets(table, blob); }
ClassFactoryNode** bc_get_ClassFactoryTable_buckets(ClassFactoryTable* table) { return table->buckets; }
void bc_set_ClassFactoryNode_value(ClassFactoryNode* node, std::uint32_t address) { node->value = CreatorFromAddress(address); }
std::uint32_t bc_get_ClassFactoryNode_value(ClassFactoryNode* node) { return AddressFromCreator(node->value); }

// The state of the TGObject statics, set to the values of the exe image.
void bc_TGObject_ResetState()
{
    TGObject::registry = nullptr;
    TGObject::nextId = 1;
    TGObject::keepRegistry = 1;
    TGObject::advanceNextId = 1;
}
void bc_TGObject_SetState(std::int32_t nextId, std::uint8_t keepRegistry, std::uint8_t advanceNextId)
{
    TGObject::nextId = nextId;
    TGObject::keepRegistry = keepRegistry;
    TGObject::advanceNextId = advanceNextId;
}
ObjectRegistry* bc_TGObject_registry() { return TGObject::registry; }
std::int32_t bc_TGObject_nextId() { return TGObject::nextId; }
std::uint8_t bc_TGObject_advanceNextId() { return TGObject::advanceNextId; }
ObjectRegistry** bc_init_global_registry_pointer() { return &TGObject::registry; }

// The global class factory table, built again for each scenario.
ClassFactoryTable* bc_init_global_ClassFactoryTable()
{
    ClassFactoryTable::InitGlobal();
    return ClassFactoryTable::Global();
}
ClassFactoryTable* bc_ClassFactoryTable_global() { return ClassFactoryTable::Global(); }
void bc_ClassFactoryTable_InitGlobal() { ClassFactoryTable::InitGlobal(); }
void bc_ClassFactoryTable_DestroyGlobal() { ClassFactoryTable::DestroyGlobal(); }
void bc_ClassFactoryTable_SetGuard(std::uint8_t value) { ClassFactoryTable::destroyGuard = value; }
std::uint8_t bc_ClassFactoryTable_GetGuard() { return ClassFactoryTable::destroyGuard; }
void bc_ClassFactoryTable_InsertGlobal(void*, std::uint32_t key, std::uint32_t creator)
{
    ClassFactoryTable::Global()->Insert(key, CreatorFromAddress(creator));
}

std::uint32_t bc_ObjectRegistry_Hash(ObjectRegistry* table, std::uint32_t key) { return table->Hash(key); }
std::uint8_t bc_ObjectRegistry_KeysEqual(ObjectRegistry* table, std::uint32_t a, std::uint32_t b)
{
    return table->KeysEqual(a, b);
}
void bc_ObjectRegistry_SetNode(ObjectRegistry* table, ObjectRegistryNode* node, std::uint32_t key, TGObject* value)
{
    table->SetNode(node, key, value);
}
void bc_ObjectRegistry_ClearNode(ObjectRegistry* table, ObjectRegistryNode* node) { table->ClearNode(node); }
ObjectRegistry* bc_ObjectRegistry_DeletingDestructor(ObjectRegistry* table, std::uint32_t flags)
{
    if (flags & 1)
        delete table;
    else
        table->~ObjectRegistry();
    return table;
}

std::uint32_t bc_ClassFactoryTable_Hash(ClassFactoryTable* table, std::uint32_t key) { return table->Hash(key); }
std::uint8_t bc_ClassFactoryTable_KeysEqual(ClassFactoryTable* table, std::uint32_t a, std::uint32_t b)
{
    return table->KeysEqual(a, b);
}
void bc_ClassFactoryTable_SetNode(ClassFactoryTable* table, ClassFactoryNode* node, std::uint32_t key,
                                  std::uint32_t creator)
{
    table->SetNode(node, key, CreatorFromAddress(creator));
}
void bc_ClassFactoryTable_ClearNode(ClassFactoryTable* table, ClassFactoryNode* node) { table->ClearNode(node); }
ClassFactoryTable* bc_ClassFactoryTable_DeletingDestructor(ClassFactoryTable* table, std::uint32_t flags)
{
    if (flags & 1)
        delete table;
    else
        table->~ClassFactoryTable();
    return table;
}
void bc_ClassFactoryTable_Insert(ClassFactoryTable* table, std::uint32_t key, std::uint32_t creator)
{
    table->Insert(key, CreatorFromAddress(creator));
}

TGObject* bc_TGObject_TGObject(TGObject* self, std::int32_t id) { return ::new (self) TGObject(id); }
void bc_TGObject_DestructorBody(TGObject* self) { self->TGObject::~TGObject(); }
TGObject* bc_TGObject_DeletingDestructor(TGObject* self, std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~TGObject();
    return self;
}
std::int32_t bc_TGObject_GetObjType(TGObject* self) { return self->GetObjType(); }
std::uint8_t bc_TGObject_IsTypeOf(TGObject* self, std::int32_t type) { return self->IsTypeOf(type); }
TGObject* bc_TGObject_GetTGObjectPtr(std::int32_t id) { return TGObject::GetTGObjectPtr(id); }
const char* bc_TGObject_GetIndentString(std::int32_t level) { return TGObject::GetIndentString(level); }
std::uint8_t bc_TGObject_RegisterID(TGObject* object) { return TGObject::RegisterID(object); }
std::uint8_t bc_TGObject_UnregisterID(TGObject* object) { return TGObject::UnregisterID(object); }
TGObject* bc_TGObject_CreateByType(std::int32_t type, std::int32_t id) { return TGObject::CreateByType(type, id); }

std::uint32_t bc_TGObject_GetClassName(TGObject* self) { return ExeAddressOfString(self->GetClassName()); }
std::uint32_t bc_TGObject_GetSwigPointerName(TGObject* self) { return ExeAddressOfString(self->GetSwigPointerName()); }
std::uint32_t bc_TGObject_GetShadowClassName(TGObject* self) { return ExeAddressOfString(self->GetShadowClassName()); }
const char* bc_TGObject_GetClassNameText(TGObject* self) { return self->GetClassName(); }
const char* bc_TGObject_GetSwigPointerNameText(TGObject* self) { return self->GetSwigPointerName(); }
const char* bc_TGObject_GetShadowClassNameText(TGObject* self) { return self->GetShadowClassName(); }

// The virtual slots that are not reconstructed. Each call must reach the trap of its exe address.
void bc_TGObject_CallSlot(TGObject* self, std::int32_t slot)
{
    switch (slot) {
    case 3: self->Print(0, 0); break;
    case 4: self->SaveToStream(nullptr); break;
    case 5: self->LoadFromStream(nullptr); break;
    case 6: self->CheckLinks(); break;
    case 7: self->FixLinks(); break;
    case 8: self->GetPythonProxy(); break;
    }
}

}
