// Support functions of the property class tests: the class name function and the trap log.
#include <cstdio>
#include <cstring>
#include <string>

#include "ClassFactoryTable.h"
#include "NiNode.h"
#include "NiRefObject.h"
#include "TGFontManager.h"
#include "TGIconGroup.h"
#include "TGIconManager.h"
#include "ObjectRegistry.h"
#include "SubsystemProperty.h"
#include "TGModelProperty.h"
#include "TGModelPropertyManager.h"
#include "TGModelPropertySet.h"
#include "TGList.h"
#include "TGObject.h"
#include "TGString.h"
#include "Trap.h"

namespace {

struct TrapRecord {
    std::uint32_t address;
    std::uint32_t registryCount;
};

constexpr std::size_t kMaxTraps = 64;
TrapRecord g_traps[kMaxTraps];
std::size_t g_trapCount = 0;

void RecordTrap(std::uint32_t exeAddress)
{
    if (g_trapCount < kMaxTraps)
        g_traps[g_trapCount++] = {exeAddress, TGObject::registry ? TGObject::registry->count : 0};
}

struct InstallHandler {
    InstallHandler() { bc::SetTrapHandler(RecordTrap); }
} g_install;

// The slot of a virtual member function in an Itanium ABI vtable (the two destructors take slots 0 and 1).
template <class F>
std::int32_t SlotOf(F member)
{
    std::uintptr_t raw[2];
    static_assert(sizeof(raw) == sizeof(F), "member pointer size");
    std::memcpy(raw, &member, sizeof(raw));
#if defined(__aarch64__)
    return static_cast<std::int32_t>(raw[0] / sizeof(void*));  // the ARM variant keeps the virtual flag in the adjustment
#else
    return static_cast<std::int32_t>((raw[0] - 1) / sizeof(void*));
#endif
}

template <std::size_t N>
void AppendLayout(std::string& out, const exe::Field (&fields)[N])
{
    char line[96];
    for (const exe::Field& f : fields) {
        std::snprintf(line, sizeof(line), "%s %u %u\n", f.name, f.offset, f.size);
        out += line;
    }
}

}  // namespace

extern "C" {

// The vtable slots of the virtual functions after the destructor, in the order of the exe vtable.
// kind 0: TGObject (11 slots), 1: ObjectRegistry (4 slots), 2: ClassFactoryTable (4 slots),
// 3: TGModelProperty (16 slots), 4: SubsystemProperty (16 slots), 5: TGModelPropertySet (11 slots),
// 6: TGModelPropertyInstance (4 slots), 7: TGModelPropertyManager (2 slots, no virtual destructor).
int bc_virtual_slots(int kind, std::int32_t* out)
{
    if (kind == 0) {
        std::int32_t slots[] = {SlotOf(&TGObject::GetObjType), SlotOf(&TGObject::IsTypeOf), SlotOf(&TGObject::Print),
                                SlotOf(&TGObject::SaveToStream), SlotOf(&TGObject::LoadFromStream),
                                SlotOf(&TGObject::CheckLinks), SlotOf(&TGObject::FixLinks),
                                SlotOf(&TGObject::GetPythonProxy), SlotOf(&TGObject::GetClassName),
                                SlotOf(&TGObject::GetSwigPointerName), SlotOf(&TGObject::GetShadowClassName)};
        std::memcpy(out, slots, sizeof(slots));
        return 11;
    }
    if (kind == 3) {
        using C = TGModelProperty;
        std::int32_t slots[] = {SlotOf(&C::GetObjType), SlotOf(&C::IsTypeOf), SlotOf(&C::Print),
                                SlotOf(&C::SaveToStream), SlotOf(&C::LoadFromStream), SlotOf(&C::CheckLinks),
                                SlotOf(&C::FixLinks), SlotOf(&C::GetPythonProxy), SlotOf(&C::GetClassName),
                                SlotOf(&C::GetSwigPointerName), SlotOf(&C::GetShadowClassName),
                                SlotOf(&C::WriteSource), SlotOf(&C::WriteInit), SlotOf(&C::Edit), SlotOf(&C::Attach),
                                SlotOf(&C::CopyFrom)};
        std::memcpy(out, slots, sizeof(slots));
        return 16;
    }
    if (kind == 4) {
        using C = SubsystemProperty;
        std::int32_t slots[] = {SlotOf(&C::GetObjType), SlotOf(&C::IsTypeOf), SlotOf(&C::Print),
                                SlotOf(&C::SaveToStream), SlotOf(&C::LoadFromStream), SlotOf(&C::CheckLinks),
                                SlotOf(&C::FixLinks), SlotOf(&C::GetPythonProxy), SlotOf(&C::GetClassName),
                                SlotOf(&C::GetSwigPointerName), SlotOf(&C::GetShadowClassName),
                                SlotOf(&C::WriteSource), SlotOf(&C::WriteInit), SlotOf(&C::Edit), SlotOf(&C::Attach),
                                SlotOf(&C::CopyFrom)};
        std::memcpy(out, slots, sizeof(slots));
        return 16;
    }
    if (kind == 5) {
        using C = TGModelPropertySet;
        std::int32_t slots[] = {SlotOf(&C::GetObjType), SlotOf(&C::IsTypeOf), SlotOf(&C::Print),
                                SlotOf(&C::SaveToStream), SlotOf(&C::LoadFromStream), SlotOf(&C::CheckLinks),
                                SlotOf(&C::FixLinks), SlotOf(&C::GetPythonProxy), SlotOf(&C::GetClassName),
                                SlotOf(&C::GetSwigPointerName), SlotOf(&C::GetShadowClassName)};
        std::memcpy(out, slots, sizeof(slots));
        return 11;
    }
    if (kind == 7) {
        using C = TGModelPropertyManager;
        std::int32_t slots[] = {SlotOf(&C::SaveToStream), SlotOf(&C::LoadFromStream)};
        std::memcpy(out, slots, sizeof(slots));
        return 2;
    }
    if (kind == 6) {
        using C = TGModelPropertyInstance;
        std::int32_t slots[] = {SlotOf(&C::SaveToStream), SlotOf(&C::LoadFromStream), SlotOf(&C::CheckLinks),
                                SlotOf(&C::FixLinks)};
        std::memcpy(out, slots, sizeof(slots));
        return 4;
    }
    std::int32_t slots[] = {SlotOf(&ObjectRegistry::Hash), SlotOf(&ObjectRegistry::KeysEqual),
                            SlotOf(&ObjectRegistry::SetNode), SlotOf(&ObjectRegistry::ClearNode)};
    std::int32_t factory[] = {SlotOf(&ClassFactoryTable::Hash), SlotOf(&ClassFactoryTable::KeysEqual),
                              SlotOf(&ClassFactoryTable::SetNode), SlotOf(&ClassFactoryTable::ClearNode)};
    std::memcpy(out, kind == 1 ? slots : factory, sizeof(slots));
    return 4;
}

// The slots of the virtual functions of the icon classes after the destructor (when the class has one), in the order of
// the declarations. kind 0: TGIconGroup (destructor), 1: TGIconLocation, 2: TGIconRegistration, 3: TGIconLocationTable
// (destructor), 4: TGIconTextureArray (destructor only), 5: NiRefObject, 6: TGIconTextureCache (destructor), 7:
// TGIconRegistrationArray (destructor only).
int bc_icon_virtual_slots(int kind, std::int32_t* out)
{
    if (kind == 0) {
        using C = TGIconGroup;
        std::int32_t slots[] = {SlotOf(&C::SetIconLocation), SlotOf(&C::GetPyWrapper), SlotOf(&C::SaveToStream),
                                SlotOf(&C::LoadFromStream)};
        std::memcpy(out, slots, sizeof(slots));
        return 4;
    }
    if (kind == 1) {
        std::int32_t slots[] = {SlotOf(&TGIconLocation::SaveToStream), SlotOf(&TGIconLocation::LoadFromStream)};
        std::memcpy(out, slots, sizeof(slots));
        return 2;
    }
    if (kind == 2) {
        std::int32_t slots[] = {SlotOf(&TGIconRegistration::SaveToStream), SlotOf(&TGIconRegistration::LoadFromStream)};
        std::memcpy(out, slots, sizeof(slots));
        return 2;
    }
    if (kind == 3) {
        using C = TGIconLocationTable;
        std::int32_t slots[] = {SlotOf(&C::Hash), SlotOf(&C::KeysEqual), SlotOf(&C::SetNode), SlotOf(&C::ClearNode)};
        std::memcpy(out, slots, sizeof(slots));
        return 4;
    }
    if (kind == 5) {
        out[0] = SlotOf(&NiRefObject::DeleteThis);
        return 1;
    }
    if (kind == 6) {
        using C = TGIconTextureCache;
        std::int32_t slots[] = {SlotOf(&C::Hash), SlotOf(&C::KeysEqual), SlotOf(&C::SetNode), SlotOf(&C::ClearNode)};
        std::memcpy(out, slots, sizeof(slots));
        return 4;
    }
    return 0;
}

// The exe offsets of the fields of a class, as lines of "name offset size". Returns the text length, or -1.
int bc_exe_layout(const char* cls, char* out, int capacity)
{
    std::string text;
    if (!std::strcmp(cls, "TGString")) AppendLayout(text, kTGStringLayout);
    else if (!std::strcmp(cls, "TGObject")) AppendLayout(text, kTGObjectLayout);
    else if (!std::strcmp(cls, "ObjectRegistry")) AppendLayout(text, kObjectRegistryLayout);
    else if (!std::strcmp(cls, "ObjectRegistryNode")) AppendLayout(text, kObjectRegistryNodeLayout);
    else if (!std::strcmp(cls, "ClassFactoryTable")) AppendLayout(text, kClassFactoryTableLayout);
    else if (!std::strcmp(cls, "ClassFactoryNode")) AppendLayout(text, kClassFactoryNodeLayout);
    else if (!std::strcmp(cls, "TGList")) AppendLayout(text, kTGListLayout);
    else if (!std::strcmp(cls, "TGListNode")) AppendLayout(text, kTGListNodeLayout);
    else if (!std::strcmp(cls, "TGListBlock")) AppendLayout(text, kTGListBlockLayout);
    else if (!std::strcmp(cls, "TGModelProperty")) AppendLayout(text, kTGModelPropertyLayout);
    else if (!std::strcmp(cls, "SubsystemProperty")) AppendLayout(text, kSubsystemPropertyLayout);
    else if (!std::strcmp(cls, "NiNode")) AppendLayout(text, kNiNodeLayout);
    else if (!std::strcmp(cls, "TGModelPropertySet")) AppendLayout(text, kTGModelPropertySetLayout);
    else if (!std::strcmp(cls, "TGModelPropertyList")) AppendLayout(text, kTGModelPropertyListLayout);
    else if (!std::strcmp(cls, "TGModelPropertyGroup")) AppendLayout(text, kTGModelPropertyGroupLayout);
    else if (!std::strcmp(cls, "TGModelPropertyInstance")) AppendLayout(text, kTGModelPropertyInstanceLayout);
    else if (!std::strcmp(cls, "TGModelPropertyManager")) AppendLayout(text, kTGModelPropertyManagerLayout);
    else if (!std::strcmp(cls, "TGIconManager")) AppendLayout(text, kTGIconManagerLayout);
    else if (!std::strcmp(cls, "TGFontManager")) AppendLayout(text, kTGFontManagerLayout);
    else if (!std::strcmp(cls, "TGFontFace")) AppendLayout(text, kTGFontFaceLayout);
    else if (!std::strcmp(cls, "TGFontRegistration")) AppendLayout(text, kTGFontRegistrationLayout);
    else if (!std::strcmp(cls, "TGIconGroup")) AppendLayout(text, kTGIconGroupLayout);
    else if (!std::strcmp(cls, "TGIconLocation")) AppendLayout(text, kTGIconLocationLayout);
    else if (!std::strcmp(cls, "TGIconLocationNode")) AppendLayout(text, kTGIconLocationNodeLayout);
    else if (!std::strcmp(cls, "TGIconLocationTable")) AppendLayout(text, kTGIconLocationTableLayout);
    else if (!std::strcmp(cls, "TGIconRegistration")) AppendLayout(text, kTGIconRegistrationLayout);
    else if (!std::strcmp(cls, "TGIconTextureArray")) AppendLayout(text, kTGIconTextureArrayLayout);
    else if (!std::strcmp(cls, "TGIconTextureCache")) AppendLayout(text, kTGIconTextureCacheLayout);
    else if (!std::strcmp(cls, "TGIconTextureNode")) AppendLayout(text, kTGIconTextureNodeLayout);
    else if (!std::strcmp(cls, "NiTextureProperty")) AppendLayout(text, kNiTexturePropertyLayout);
    else if (!std::strcmp(cls, "NiObjectNET")) AppendLayout(text, kNiObjectNETLayout);
    else if (!std::strcmp(cls, "NiImage")) AppendLayout(text, kNiImageLayout);
    else if (!std::strcmp(cls, "NiDDImage")) AppendLayout(text, kNiDDImageLayout);
    else if (!std::strcmp(cls, "NiRawImageData")) AppendLayout(text, kNiRawImageDataLayout);
    else if (!std::strcmp(cls, "TextureFormat")) AppendLayout(text, kTextureFormatLayout);
    else if (!std::strcmp(cls, "ImageSurfaceRecord")) AppendLayout(text, kImageSurfaceRecordLayout);
    else if (!std::strcmp(cls, "TextureManager")) AppendLayout(text, kTextureManagerLayout);
    else if (!std::strcmp(cls, "NiD3DRender")) AppendLayout(text, kNiD3DRenderLayout);
    else if (!std::strcmp(cls, "NiCamera")) AppendLayout(text, kNiCameraLayout);
    else return -1;
    if (static_cast<int>(text.size()) + 1 > capacity)
        return -1;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return static_cast<int>(text.size());
}

const char* bc_class_name(void* object, const char* declared)
{
    if (std::strcmp(declared, "TGObject") == 0 || std::strcmp(declared, "TGModelProperty") == 0 ||
        std::strcmp(declared, "SubsystemProperty") == 0 || std::strcmp(declared, "TGModelPropertySet") == 0)
        return static_cast<TGObject*>(object)->GetClassName();
    if (std::strcmp(declared, "TGModelPropertyList") == 0 || std::strcmp(declared, "TGModelPropertyGroup") == 0)
        return dynamic_cast<TGModelPropertyGroup*>(static_cast<TGModelPropertyList*>(object)) != nullptr
                   ? "TGModelPropertyGroup"
                   : "TGModelPropertyList";
    return declared;
}

// The number of live objects of a class: the objects in the ID registry whose class has this name.
std::int64_t bc_live_count(const char* name)
{
    ObjectRegistry* registry = TGObject::registry;
    if (registry == nullptr)
        return 0;
    std::int64_t count = 0;
    for (std::uint32_t i = 0; i < registry->bucketCount; ++i) {
        for (ObjectRegistry::Node* node = registry->buckets[i]; node != nullptr; node = node->next) {
            if (node->value != nullptr && std::strcmp(node->value->GetClassName(), name) == 0)
                ++count;
        }
    }
    return count;
}

void bc_trap_clear() { g_trapCount = 0; }
std::int32_t bc_trap_count() { return static_cast<std::int32_t>(g_trapCount); }
std::uint32_t bc_trap_address(std::int32_t index) { return g_traps[index].address; }
std::uint32_t bc_trap_registry_count(std::int32_t index) { return g_traps[index].registryCount; }

}
