// C entry points for the TGModelProperty and SubsystemProperty tests. The first argument of a member function is
// the object. Objects made by the factories are blank (zeroed memory): the tests run a constructor on them.
#include <cstring>
#include <new>
#include <utility>

#include "BcFields.h"
#include "ExeStrings.h"
#include "Heap.h"
#include "NiNode.h"
#include "SubsystemProperty.h"
#include "TGModelProperty.h"
#include "X87.h"

struct PropPoint3 {
    float x, y, z;
};
struct PropPoint2 {
    float x, y;
};

BC_FIELDS(PropPoint3)
    BC_FIELD(PropPoint3, x) BC_FIELD(PropPoint3, y) BC_FIELD(PropPoint3, z)
BC_FIELDS_END

BC_FIELDS(PropPoint2)
    BC_FIELD(PropPoint2, x) BC_FIELD(PropPoint2, y)
BC_FIELDS_END

BC_FIELDS(TGModelProperty)
    BC_FIELD(TGModelProperty, id) BC_FIELD(TGModelProperty, refCount) BC_FIELD(TGModelProperty, name)
BC_FIELDS_END

BC_FIELDS(SubsystemProperty)
    BC_FIELD(SubsystemProperty, id) BC_FIELD(SubsystemProperty, refCount) BC_FIELD(SubsystemProperty, name)
    BC_FIELD(SubsystemProperty, displayNode) BC_FIELD(SubsystemProperty, maxCondition)
    BC_FIELD(SubsystemProperty, critical) BC_FIELD(SubsystemProperty, targetable) BC_FIELD(SubsystemProperty, primary)
    BC_FIELD(SubsystemProperty, position) BC_FIELD(SubsystemProperty, position2D)
    BC_FIELD(SubsystemProperty, repairComplexity) BC_FIELD(SubsystemProperty, disabledPercentage)
    BC_FIELD(SubsystemProperty, radius)
BC_FIELDS_END

BC_FIELDS(NiNode)
    BC_FIELD(NiNode, refCount) BC_FIELD(NiNode, parent)
BC_FIELDS_END

void RegisterClassCreators();
void RunModuleInitializers();

namespace {

// A scene node for the destructor tests. It logs the calls that the destructor makes.
struct TestNode : NiNode {
    void DeleteThis() override { LogCall(1); }
    void DetachChild(NiNode** removed, NiNode* child) override
    {
        LogCall(2);
        *removed = detachReturnsNothing ? nullptr : child;
    }
    static void LogCall(int kind)
    {
        if (logSize < sizeof(log))
            log[logSize++] = static_cast<std::uint8_t>(kind);
    }
    static bool detachReturnsNothing;
    static std::uint8_t log[16];
    static std::size_t logSize;
};
bool TestNode::detachReturnsNothing = false;
std::uint8_t TestNode::log[16];
std::size_t TestNode::logSize = 0;

template <class T>
T* NewBlank()
{
    void* block = operator_new(sizeof(T));
    std::memset(block, 0, sizeof(T));
    return static_cast<T*>(block);
}

// The results that the Python helpers of the wrapper tests receive.
double g_capturedDouble = 0;
long g_capturedInteger = 0;
void* g_capturedPointer = nullptr;
constexpr std::uint32_t kNoneAddress = 0x95dd20;

}  // namespace

extern "C" {

TGModelProperty* bc_new_TGModelProperty() { return NewBlank<TGModelProperty>(); }
SubsystemProperty* bc_new_SubsystemProperty() { return NewBlank<SubsystemProperty>(); }
NiNode* bc_new_NiNode() { return new TestNode(); }
PropPoint3* bc_new_PropPoint3() { return NewBlank<PropPoint3>(); }
PropPoint2* bc_new_PropPoint2() { return NewBlank<PropPoint2>(); }

TGModelProperty* bc_TGModelProperty_TGModelProperty(TGModelProperty* self, std::int32_t id)
{
    return ::new (self) TGModelProperty(id);
}
void bc_TGModelProperty_DestructorBody(TGModelProperty* self) { self->TGModelProperty::~TGModelProperty(); }
TGModelProperty* bc_TGModelProperty_DeletingDestructor(TGModelProperty* self, std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~TGModelProperty();
    return self;
}
std::int32_t bc_TGModelProperty_GetObjType(TGModelProperty* self) { return self->GetObjType(); }
std::uint8_t bc_TGModelProperty_IsTypeOf(TGModelProperty* self, std::int32_t type) { return self->IsTypeOf(type); }
std::uint32_t bc_TGModelProperty_GetClassName(TGModelProperty* self) { return ExeAddressOfString(self->GetClassName()); }
std::uint32_t bc_TGModelProperty_GetSwigPointerName(TGModelProperty* self)
{
    return ExeAddressOfString(self->GetSwigPointerName());
}
std::uint32_t bc_TGModelProperty_GetShadowClassName(TGModelProperty* self)
{
    return ExeAddressOfString(self->GetShadowClassName());
}
std::uint8_t bc_TGModelProperty_CheckLinks(TGModelProperty* self) { return self->CheckLinks(); }
void bc_TGModelProperty_FixLinks(TGModelProperty* self) { self->FixLinks(); }
void bc_TGModelProperty_WriteInit(TGModelProperty* self, TGFile* file, const char* name) { self->WriteInit(file, name); }
std::uint8_t bc_TGModelProperty_Edit(TGModelProperty* self, void* window) { return self->Edit(window); }
void bc_TGModelProperty_Attach(TGModelProperty* self, std::int32_t attach, std::int32_t scene, std::int32_t parent)
{
    self->Attach(attach != 0, reinterpret_cast<void*>(static_cast<std::intptr_t>(scene)),
                 reinterpret_cast<void*>(static_cast<std::intptr_t>(parent)));
}
void bc_TGModelProperty_IncRefCount(TGModelProperty* self) { self->IncRefCount(); }
void bc_TGModelProperty_DecRefCount(TGModelProperty* self) { self->DecRefCount(); }
TGString* bc_TGModelProperty_GetName(TGModelProperty* self, TGString* out) { return self->GetName(out); }

// The exe passes the string by value and destroys it inside the callee.
// The argument is a bitwise copy of the string, which the callee destroys; the string of the caller keeps its fields.
void bc_TGModelProperty_SetNameString(TGModelProperty* self, TGString* temporary)
{
    alignas(TGString) unsigned char raw[sizeof(TGString)];
    std::memcpy(raw, temporary, sizeof(TGString));
    TGString& copy = *reinterpret_cast<TGString*>(raw);
    self->SetNameString(std::move(copy));
    copy.~TGString();
}
void bc_TGModelProperty_SetName(TGModelProperty* self, const char* name) { self->SetName(name); }
std::uint8_t bc_TGModelProperty_CopyFrom(TGModelProperty* self, const TGModelProperty* source)
{
    return self->CopyFrom(source);
}
TGModelProperty* bc_TGModelProperty_Duplicate(TGModelProperty* self) { return self->Duplicate(); }
std::int32_t bc_TGModelProperty_GetRefCount(TGModelProperty* self) { return self->GetRefCount(); }
TGModelProperty* bc_TGModelProperty_Cast(TGObject* object) { return TGModelProperty::Cast(object); }
TGModelProperty* bc_TGModelProperty_Create(const char* name) { return TGModelProperty::Create(name); }
SubsystemProperty* bc_SubsystemProperty_Create(const char* name) { return SubsystemProperty::Create(name); }
TGObject* bc_TGModelProperty_CreateForFactory(std::int32_t id) { return TGModelProperty::CreateForFactory(id); }
void bc_RegisterClassCreators() { RegisterClassCreators(); }
void bc_RunModuleInitializers() { RunModuleInitializers(); }
void bc_TGModelProperty_RegisterCreator() { TGModelProperty::RegisterCreator(); }

// The virtual slots that are not reconstructed. Each call must reach the trap of its exe address.
void bc_TGModelProperty_CallSlot(TGModelProperty* self, std::int32_t slot)
{
    switch (slot) {
    case 4: self->SaveToStream(nullptr); break;
    case 5: self->LoadFromStream(nullptr); break;
    case 12: self->WriteSource(nullptr); break;
    }
}

SubsystemProperty* bc_SubsystemProperty_SubsystemProperty(SubsystemProperty* self, std::int32_t id)
{
    return ::new (self) SubsystemProperty(id);
}
void bc_SubsystemProperty_DestructorBody(SubsystemProperty* self) { self->SubsystemProperty::~SubsystemProperty(); }
SubsystemProperty* bc_SubsystemProperty_DeletingDestructor(SubsystemProperty* self, std::uint32_t flags)
{
    if (flags & 1)
        delete self;
    else
        self->~SubsystemProperty();
    return self;
}
std::int32_t bc_SubsystemProperty_GetObjType(SubsystemProperty* self) { return self->GetObjType(); }
std::uint8_t bc_SubsystemProperty_IsTypeOf(SubsystemProperty* self, std::int32_t type) { return self->IsTypeOf(type); }
std::uint32_t bc_SubsystemProperty_GetClassName(SubsystemProperty* self)
{
    return ExeAddressOfString(self->GetClassName());
}
std::uint32_t bc_SubsystemProperty_GetSwigPointerName(SubsystemProperty* self)
{
    return ExeAddressOfString(self->GetSwigPointerName());
}
std::uint32_t bc_SubsystemProperty_GetShadowClassName(SubsystemProperty* self)
{
    return ExeAddressOfString(self->GetShadowClassName());
}
SubsystemProperty* bc_SubsystemProperty_Cast(TGModelProperty* property) { return SubsystemProperty::Cast(property); }
void bc_SubsystemProperty_GetPositionTG(SubsystemProperty* self, float* out)
{
    self->GetPositionTG(reinterpret_cast<TGPoint3*>(out));
}
void bc_SubsystemProperty_SetPositionXYZ(SubsystemProperty* self, float x, float y, float z)
{
    self->SetPositionXYZ(x, y, z);
}
void bc_SubsystemProperty_SetPosition2DXY(SubsystemProperty* self, float x, float y) { self->SetPosition2DXY(x, y); }
std::uint8_t bc_SubsystemProperty_CopyFrom(SubsystemProperty* self, const TGModelProperty* source)
{
    return self->CopyFrom(source);
}
TGObject* bc_SubsystemProperty_CreateForFactory(std::int32_t id) { return SubsystemProperty::CreateForFactory(id); }
void bc_SubsystemProperty_RegisterCreator() { SubsystemProperty::RegisterCreator(); }

// The virtual slots that are not reconstructed.
void bc_SubsystemProperty_CallSlot(SubsystemProperty* self, std::int32_t slot)
{
    switch (slot) {
    case 4: self->SaveToStream(nullptr); break;
    case 5: self->LoadFromStream(nullptr); break;
    case 12: self->WriteSource(nullptr); break;
    case 13: self->WriteInit(nullptr, nullptr); break;
    case 15: self->Attach(false, nullptr, nullptr); break;
    }
}

// Writes 1 or 4 bytes at an exe offset of a property object. kind 0: TGModelProperty, 1: SubsystemProperty.
// The exe offset is mapped to the member of the native object, so that a test sets any bit pattern.
void bc_poke(std::int32_t kind, void* object, std::uint32_t exeOffset, std::uint32_t value, std::uint32_t size)
{
    const exe::Field* fields = kind == 0 ? kTGModelPropertyLayout : kSubsystemPropertyLayout;
    std::size_t count = kind == 0 ? sizeof(kTGModelPropertyLayout) / sizeof(exe::Field)
                                  : sizeof(kSubsystemPropertyLayout) / sizeof(exe::Field);
    for (std::size_t i = 0; i < count; ++i) {
        const exe::Field& f = fields[i];
        if (exeOffset >= f.offset && exeOffset + size <= f.offset + f.size) {
            std::memcpy(static_cast<char*>(object) + f.hostOffset + (exeOffset - f.offset), &value, size);
            return;
        }
    }
}

// Runs the constructor of a property class on a blank object. kind 0: TGModelProperty, 1: SubsystemProperty.
void bc_object_construct(void* object, std::int32_t kind, std::int32_t id)
{
    if (kind == 0)
        ::new (object) TGModelProperty(id);
    else
        ::new (object) SubsystemProperty(id);
}

// The class name strings of the virtual functions: 0 class name, 1 SWIG pointer name, 2 shadow class name.
const char* bc_property_text(TGModelProperty* self, std::int32_t which)
{
    return which == 0 ? self->GetClassName() : (which == 1 ? self->GetSwigPointerName() : self->GetShadowClassName());
}

// Stores the scene node pointer of a SubsystemProperty (exe offset 0x1c).
void bc_poke_pointer(SubsystemProperty* object, std::uint32_t, NiNode* node, std::uint32_t) { object->displayNode = node; }

// Stores the share count byte of the name buffer.
void bc_poke_name_share(TGModelProperty* object, std::uint32_t value)
{
    TGString::ShareCount(object->name.buffer) = static_cast<std::uint8_t>(value);
}

void bc_poke_pre(void* object, std::uint32_t exeOffset, std::uint32_t value, std::uint32_t size)
{
    bc_poke(1, object, exeOffset, value, size);
}

// The default values that the constructor copies as bits from the globals at 0x9a2878 and 0x9a2bb8.
void bc_SubsystemProperty_SetDefaultPositions(const std::uint32_t* three, const std::uint32_t* two)
{
    std::memcpy(&NiPoint3::zero, three, sizeof(NiPoint3));
    std::memcpy(&NiPoint2::zero, two, sizeof(NiPoint2));
}

// The scene node of the destructor tests.
void bc_node_log_clear() { TestNode::logSize = 0; }
std::int32_t bc_node_log_size() { return static_cast<std::int32_t>(TestNode::logSize); }
std::uint8_t bc_node_log_entry(std::int32_t index) { return TestNode::log[index]; }
void bc_node_detach_returns_nothing(std::uint8_t value) { TestNode::detachReturnsNothing = value != 0; }

// The Appc wrapper of a SubsystemProperty method. The first argument stands for the Python self, the second is the
// object (the wrapper tests pass it in the Python arguments). A setter returns None. A getter keeps the value that
// the wrapper hands to Python and returns the handle that the Python stub of the emulator test returns.
#define WRAPPER_SETTER_F(Name)                                                                              \
    std::uint32_t bc_Appc_SubsystemProperty_##Name(std::uint32_t, SubsystemProperty* p, float v)         \
    {                                                                                                         \
        p->Name(v);                                                                                           \
        return kNoneAddress;                                                                                  \
    }
#define WRAPPER_SETTER_I(Name)                                                                              \
    std::uint32_t bc_Appc_SubsystemProperty_##Name(std::uint32_t, SubsystemProperty* p, std::int32_t v)  \
    {                                                                                                         \
        p->Name(v);                                                                                           \
        return kNoneAddress;                                                                                  \
    }
#define WRAPPER_GETTER_F(Name)                                                                              \
    std::uint32_t bc_Appc_SubsystemProperty_##Name(std::uint32_t, SubsystemProperty* p)                      \
    {                                                                                                         \
        g_capturedDouble = static_cast<double>(p->Name());                                                    \
        return 0xF1000000;                                                                                    \
    }
#define WRAPPER_GETTER_I(Name)                                                                              \
    std::uint32_t bc_Appc_SubsystemProperty_##Name(std::uint32_t, SubsystemProperty* p)                      \
    {                                                                                                         \
        g_capturedInteger = p->Name();                                                                        \
        return 0x1A000000;                                                                                    \
    }

WRAPPER_SETTER_F(SetMaxCondition)
WRAPPER_SETTER_I(SetCritical)
WRAPPER_SETTER_I(SetTargetable)
WRAPPER_SETTER_I(SetPrimary)
WRAPPER_SETTER_F(SetRepairComplexity)
WRAPPER_SETTER_F(SetDisabledPercentage)
WRAPPER_SETTER_F(SetRadius)
WRAPPER_GETTER_F(GetMaxCondition)
WRAPPER_GETTER_I(IsCritical)
WRAPPER_GETTER_I(IsTargetable)
WRAPPER_GETTER_F(GetRepairComplexity)
WRAPPER_GETTER_F(GetDisabledPercentage)
WRAPPER_GETTER_F(GetRadius)

std::uint32_t bc_Appc_SubsystemProperty_SetPosition(std::uint32_t, SubsystemProperty* p, float x, float y, float z)
{
    p->SetPosition(x, y, z);
    return kNoneAddress;
}

std::uint32_t bc_Appc_SubsystemProperty_SetPosition2D(std::uint32_t, SubsystemProperty* p, float x, float y)
{
    p->SetPosition2D(x, y);
    return kNoneAddress;
}

std::uint32_t bc_Appc_SubsystemProperty_IsPrimary(std::uint32_t, SubsystemProperty* p)
{
    g_capturedInteger = p->IsPrimary();
    return 0x1A000000;
}

std::uint32_t bc_Appc_SubsystemProperty_GetPosition(std::uint32_t, SubsystemProperty* p)
{
    g_capturedPointer = p->GetPosition();
    return 0xB1000000;
}

// The Appc wrappers of the TGObject methods that the property classes inherit. The object is the second argument.
std::uint32_t bc_Appc_TGObject_GetObjType(std::uint32_t, TGObject* object)
{
    g_capturedInteger = object->GetObjType();
    return 0x1A000000;
}

std::uint32_t bc_Appc_TGObject_IsTypeOf(std::uint32_t, TGObject* object, std::int32_t type)
{
    g_capturedInteger = object->IsTypeOf(type);
    return 0x1A000000;
}

std::uint32_t bc_Appc_TGObject_GetObjID(std::uint32_t, TGObject* object)
{
    g_capturedInteger = object->id;
    return 0x1A000000;
}

std::uint32_t bc_Appc_TGObject_SetIncrementID(std::uint32_t, void*, std::int32_t value)
{
    TGObject::advanceNextId = value != 0;
    return kNoneAddress;
}

std::uint32_t bc_Appc_TGObject_IsIncrementID(std::uint32_t, void*)
{
    g_capturedInteger = TGObject::advanceNextId;
    return 0x1A000000;
}

std::uint32_t bc_Appc_TGObject_Destroy(std::uint32_t, TGObject* object)
{
    delete object;
    return kNoneAddress;
}

void bc_set_captured_double(double value) { g_capturedDouble = value; }
void bc_set_captured_integer(std::int64_t value) { g_capturedInteger = value; }
void bc_set_captured_pointer(void* value) { g_capturedPointer = value; }
double bc_captured_double() { return g_capturedDouble; }
std::int64_t bc_captured_integer() { return g_capturedInteger; }
void* bc_captured_pointer() { return g_capturedPointer; }
void bc_capture_reset()
{
    g_capturedDouble = 0;
    g_capturedInteger = 0;
    g_capturedPointer = nullptr;
}

}
