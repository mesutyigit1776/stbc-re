#pragma once
#include <cstdint>

#include "ClassFactoryTable.h"
#include "ExeLayout.h"
#include "ObjectRegistry.h"

struct TGStream;
struct PythonObject;

// The base of all game objects that have an ID. Every instance is entered in the ID registry (E0046, E0062).
// The virtual functions follow the vtable at 0x896278; slots that are not reconstructed call BC_NOT_RECONSTRUCTED.
class TGObject {
public:
    static constexpr int kObjType = 2;

    explicit TGObject(int requestedId);
    virtual ~TGObject();
    virtual int GetObjType();
    virtual bool IsTypeOf(int type);
    virtual void Print(int, int);
    virtual bool SaveToStream(TGStream* stream);
    virtual bool LoadFromStream(TGStream* stream);
    virtual bool CheckLinks();
    virtual void FixLinks();
    virtual PythonObject* GetPythonProxy();
    virtual const char* GetClassName();
    virtual const char* GetSwigPointerName();
    virtual const char* GetShadowClassName();

    static TGObject* GetTGObjectPtr(int id);
    static const char* GetIndentString(int level);
    static bool RegisterID(TGObject* object);
    static bool UnregisterID(TGObject* object);
    static TGObject* CreateByType(int type, int id);
    static void RemoveFromEventTables(TGObject* object);

    static void* operator new(std::size_t size) { return operator_new(size); }
    static void operator delete(void* block) { operator_delete(block); }

    int id;  // 0x04

    static ObjectRegistry* registry;      // 0x99a67c
    static std::int32_t nextId;           // 0x95b078, starts at 1
    static std::uint8_t keepRegistry;     // 0x95b07c, starts at 1: the registry stays when it becomes empty
    static std::uint8_t advanceNextId;    // 0x95b07d, starts at 1: an explicit ID moves the counter on
};

constexpr exe::Field kTGObjectLayout[] = {
    EXE_VPTR(),
    EXE_FIELD(TGObject, id, 0x04),
};
static_assert(exe::LayoutHolds(kTGObjectLayout, 0x08), "TGObject layout");
