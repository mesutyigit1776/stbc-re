#include "TGModelProperty.h"

#include <utility>

#include "ClassFactoryTable.h"
#include "PythonObject.h"
#include "Trap.h"

// The text at 0x97dc28, which the constructor passes to the name string. Its bytes are 0 in the file (E0075).
static const char kEmptyText[1] = {0};

// The text at 0x95affc, which the constructor assigns to the name.
static const char kUninitializedName[] = "Uninitialized";

// 0x6ec640
TGModelProperty::TGModelProperty(int requestedId) : TGObject(requestedId), refCount(1), name(kEmptyText)
{
    name.AssignNarrowChecked(kUninitializedName);
}

// 0x6ec6f0, called by the deleting destructor 0x6ec6c0. The name string is destroyed after this body.
TGModelProperty::~TGModelProperty()
{
    PythonObject* proxy = TGObject::GetPythonProxy();
    if (proxy != PythonNone()) {
        if (--proxy->refCount == 0)
            BC_NOT_RECONSTRUCTED(0x6ec732);  // the deallocation function of the proxy type
    }
}

// 0x6ed220
int TGModelProperty::GetObjType() { return kObjType; }

// 0x6ed230
bool TGModelProperty::IsTypeOf(int type) { return type == kObjType || type == TGObject::kObjType; }

// 0x6ec840
bool TGModelProperty::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6ec840);
    return false;
}

// 0x6ec880
bool TGModelProperty::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x6ec880);
    return false;
}

// 0x67c560, shared with other classes
bool TGModelProperty::CheckLinks() { return true; }

// 0x67c570, shared with other classes
void TGModelProperty::FixLinks() {}

// 0x6ed250
const char* TGModelProperty::GetClassName() { return "TGModelProperty"; }

// 0x6ed260
const char* TGModelProperty::GetSwigPointerName() { return "_p_TGModelProperty"; }

// 0x6ed270
const char* TGModelProperty::GetShadowClassName() { return "TGModelPropertyPtr"; }

// 0x6ed280
void TGModelProperty::WriteSource(TGFile*) { BC_NOT_RECONSTRUCTED(0x6ed280); }

// 0x67ddd0
void TGModelProperty::WriteInit(TGFile*, const char*) {}

// 0x6ec7d0
bool TGModelProperty::Edit(void*) { return false; }

// 0x6ec770
void TGModelProperty::Attach(bool, void*, void*) {}

// 0x6ec810
bool TGModelProperty::CopyFrom(const TGModelProperty* source)
{
    TGString copy(kEmptyText);
    source->GetName(&copy);
    SetNameString(std::move(copy));
    return true;
}

// 0x6ec780
void TGModelProperty::IncRefCount() { ++refCount; }

// 0x6ec790
void TGModelProperty::DecRefCount()
{
    if (refCount <= 0)
        return;
    --refCount;
    if (refCount > 0)
        return;
    refCount = 0;
    delete this;
}

// 0x6ec7c0
int TGModelProperty::GetRefCount() const { return refCount; }

// 0x5378d0, the copy of the name: the substring from 0 to the end, shared when the buffer allows it
TGString* TGModelProperty::GetName(TGString* out) const
{
    out->allocator = name.allocator;
    out->buffer = nullptr;
    out->length = 0;
    out->capacity = 0;
    std::uint32_t count = name.length;
    if (out == &name) {
        BC_NOT_RECONSTRUCTED(0x537903);
        return out;
    }
    const char16_t* source = name.Characters();
    if (count > 0 && TGString::ShareCount(const_cast<char16_t*>(source)) < 0xfe) {
        if (name.buffer == nullptr)
            BC_NOT_RECONSTRUCTED(0x5379a8);
        out->Tidy(true);
        out->buffer = const_cast<char16_t*>(source);
        out->length = name.length;
        out->capacity = name.capacity;
        ++TGString::ShareCount(out->buffer);
        return out;
    }
    if (out->Grow(count, true)) {
        TGString::CopyChars(out->buffer, source, count);
        out->Eos(count);
    }
    return out;
}

// 0x5ddc40. The exe passes the string by value and destroys it at the end; the caller's temporary is destroyed after the call.
void TGModelProperty::SetNameString(TGString&& newName) { name.AssignString(newName); }

// 0x5ddc10
void TGModelProperty::SetName(const char* newName) { SetNameString(TGString(newName)); }

// 0x5dde50 and the copy that the wrapper of SetNameW makes first: the name becomes a copy of the argument
void TGModelProperty::SetNameW(const TGString& newName)
{
    TGString copy(kEmptyText);
    copy.AssignString(newName);
    SetNameString(std::move(copy));
}

// 0x6ec5c0
TGModelProperty* TGModelProperty::Cast(TGObject* object)
{
    if (object == nullptr)
        return nullptr;
    return object->IsTypeOf(kObjType) ? static_cast<TGModelProperty*>(object) : nullptr;
}

// 0x6ec5f0
TGModelProperty* TGModelProperty::Create(const char* name)
{
    TGModelProperty* property = new TGModelProperty(0);
    property->SetName(name);
    return property;
}

// 0x6ec7e0
TGModelProperty* TGModelProperty::Duplicate()
{
    TGObject* object = CreateByType(GetObjType(), 0);
    TGModelProperty* copy = static_cast<TGModelProperty*>(object);
    if (copy != nullptr) {
        copy->CopyFrom(this);
        copy->refCount = 1;
    }
    return copy;
}

// 0x6e94b0
TGObject* TGModelProperty::CreateForFactory(int id) { return new TGModelProperty(id); }

// inline in 0x6e8980, which also registers type 0x602
void TGModelProperty::RegisterCreator() { ClassFactoryTable::Global()->Insert(kObjType, &TGModelProperty::CreateForFactory); }
