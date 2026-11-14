#include "TGObject.h"

#include "PythonObject.h"
#include "Trap.h"

ObjectRegistry* TGObject::registry = nullptr;
std::int32_t TGObject::nextId = 1;
std::uint8_t TGObject::keepRegistry = 1;
std::uint8_t TGObject::advanceNextId = 1;

namespace {

// The 257 spaces at 0x95b090 to 0x95b190 and the 0 at 0x95b191 (E0099).
struct IndentText {
    char text[258];
    IndentText()
    {
        for (int i = 0; i < 257; ++i)
            text[i] = ' ';
        text[257] = 0;
    }
};
const IndentText kIndent;

}  // namespace

// 0x6f1680. The text ends at 0x95b190: each level of 4 takes the pointer 4 characters back, up to 64 levels.
// A negative level points behind the spaces into other data of the exe, which is not reconstructed.
const char* TGObject::GetIndentString(int level)
{
    if (level < 0) {
        BC_NOT_RECONSTRUCTED(0x6f1680);
        return kIndent.text + 0x101;
    }
    if (level > 0x40)
        level = 0x40;
    return kIndent.text + 0x100 - 4 * level;
}

// 0x6f0a70
TGObject::TGObject(int requestedId)
{
    if (requestedId == 0) {
        id = nextId;
        nextId = nextId + 1;
    } else {
        id = requestedId;
        if (advanceNextId != 0 && !(nextId > requestedId))
            nextId = requestedId + 1;
    }
    if (registry == nullptr)
        registry = new ObjectRegistry();
    RegisterID(this);
}

// 0x6f0ba0, called by the deleting destructor 0x6f0b70
TGObject::~TGObject()
{
    RemoveFromEventTables(this);
    UnregisterID(this);
}

// 0x6f0b60
int TGObject::GetObjType() { return kObjType; }

// 0x518ab0
bool TGObject::IsTypeOf(int type) { return type == kObjType; }

// 0x6f1650
void TGObject::Print(int, int) { BC_NOT_RECONSTRUCTED(0x6f1650); }

// 0x6f0bc0
bool TGObject::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6f0bc0);
    return false;
}

// 0x859a0b, the pure virtual call stub of the C run time
bool TGObject::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x859a0b);
    return false;
}

// 0x859a0b
bool TGObject::CheckLinks()
{
    BC_NOT_RECONSTRUCTED(0x859a0b);
    return false;
}

// 0x859a0b
void TGObject::FixLinks() { BC_NOT_RECONSTRUCTED(0x859a0b); }

// 0x6f15c0
PythonObject* TGObject::GetPythonProxy()
{
    BC_NOT_RECONSTRUCTED(0x6f15c0);
    return PythonNone();
}

// 0x6f1540
const char* TGObject::GetClassName() { return "TGObject"; }

// 0x6f1550
const char* TGObject::GetSwigPointerName() { return "_p_TGObject"; }

// 0x6f1560
const char* TGObject::GetShadowClassName() { return "TGObjectPtr"; }

// 0x6f0ee0
TGObject* TGObject::GetTGObjectPtr(int objectId)
{
    ObjectRegistry::Node* node = registry->FindNode(static_cast<std::uint32_t>(objectId));  // B0003
    return node ? node->value : nullptr;
}

// 0x6f0f30
bool TGObject::RegisterID(TGObject* object)
{
    registry->Insert(static_cast<std::uint32_t>(object->id), object);
    return true;
}

// 0x6f0fc0
bool TGObject::UnregisterID(TGObject* object)
{
    ObjectRegistry* table = registry;
    std::uint32_t key = static_cast<std::uint32_t>(object->id);
    std::uint32_t bucket = table->Hash(key);
    ObjectRegistry::Node* previous = nullptr;
    ObjectRegistry::Node* node = table->buckets[bucket];
    while (node != nullptr && !table->KeysEqual(key, node->key)) {
        previous = node;
        node = node->next;
    }
    bool removed = node != nullptr;
    if (removed) {
        if (previous == nullptr)
            table->buckets[bucket] = node->next;
        else
            previous->next = node->next;
        table->ClearNode(node);
        operator_delete(node);
        --table->count;
    }
    if (keepRegistry == 0 && registry != nullptr && registry->count == 0)
        delete registry;  // B0001
    return removed;
}

// 0x6f13e0
TGObject* TGObject::CreateByType(int type, int id)
{
    ClassFactoryTable* table = ClassFactoryTable::Global();
    ClassFactoryNode* node = table->FindNode(static_cast<std::uint32_t>(type));
    return node ? node->value(id) : nullptr;
}

// 0x6d6320, outside the reconstructed classes: the event handler tables
void TGObject::RemoveFromEventTables(TGObject*) { BC_NOT_RECONSTRUCTED(0x6d6320); }
