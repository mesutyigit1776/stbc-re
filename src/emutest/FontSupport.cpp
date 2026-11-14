// C entry points for the tests of the font manager, the font family record and the font registration. The calls of the
// font code into the icon manager and into the C run time go to the engine code. The first argument of a member function
// is the object.
#include <cstring>
#include <new>

#include "BcFields.h"
#include "Crt.h"
#include "TGFontManager.h"
#include "TGList.h"

using FontFaceNode = TGListNode;
using FontFaceBlock = TGListBlock;
using FontFaceList = TGList;
using FontRegNode = TGListNode;
using FontRegBlock = TGListBlock;
using FontRegList = TGList;
using FontGroupNode = TGListNode;
using FontGroupBlock = TGListBlock;
using FontGroupList = TGList;
using FontManager = TGFontManager;
using FontFace = TGFontFace;
using FontRegistration = TGFontRegistration;

BC_FIELDS(FontManager)
    BC_FIELD(FontManager, families) BC_FIELD(FontManager, defaultName) BC_FIELD(FontManager, defaultSize)
BC_FIELDS_END

BC_FIELDS(FontFace)
    BC_FIELD(FontFace, name) BC_FIELD(FontFace, registrations) BC_FIELD(FontFace, loadedGroups)
BC_FIELDS_END

BC_FIELDS(FontRegistration)
    BC_FIELD(FontRegistration, groupName) BC_FIELD(FontRegistration, moduleName) BC_FIELD(FontRegistration, functionName)
    BC_FIELD(FontRegistration, size)
BC_FIELDS_END

BC_FIELDS(FontFaceNode)
    BC_FIELD(FontFaceNode, data) BC_FIELD(FontFaceNode, next) BC_FIELD(FontFaceNode, prev)
BC_FIELDS_END
BC_FIELDS(FontFaceBlock)
    BC_FIELD(FontFaceBlock, nextBlock) BC_FIELD(FontFaceBlock, nodes)
BC_FIELDS_END
BC_FIELDS(FontFaceList)
    BC_FIELD(FontFaceList, count) BC_FIELD(FontFaceList, head) BC_FIELD(FontFaceList, tail)
    BC_FIELD(FontFaceList, freeNodes) BC_FIELD(FontFaceList, blocks) BC_FIELD(FontFaceList, blockSize)
BC_FIELDS_END

BC_FIELDS(FontRegNode)
    BC_FIELD(FontRegNode, data) BC_FIELD(FontRegNode, next) BC_FIELD(FontRegNode, prev)
BC_FIELDS_END
BC_FIELDS(FontRegBlock)
    BC_FIELD(FontRegBlock, nextBlock) BC_FIELD(FontRegBlock, nodes)
BC_FIELDS_END
BC_FIELDS(FontRegList)
    BC_FIELD(FontRegList, count) BC_FIELD(FontRegList, head) BC_FIELD(FontRegList, tail)
    BC_FIELD(FontRegList, freeNodes) BC_FIELD(FontRegList, blocks) BC_FIELD(FontRegList, blockSize)
BC_FIELDS_END

BC_FIELDS(FontGroupNode)
    BC_FIELD(FontGroupNode, data) BC_FIELD(FontGroupNode, next) BC_FIELD(FontGroupNode, prev)
BC_FIELDS_END
BC_FIELDS(FontGroupBlock)
    BC_FIELD(FontGroupBlock, nextBlock) BC_FIELD(FontGroupBlock, nodes)
BC_FIELDS_END
BC_FIELDS(FontGroupList)
    BC_FIELD(FontGroupList, count) BC_FIELD(FontGroupList, head) BC_FIELD(FontGroupList, tail)
    BC_FIELD(FontGroupList, freeNodes) BC_FIELD(FontGroupList, blocks) BC_FIELD(FontGroupList, blockSize)
BC_FIELDS_END

namespace {

template <class T>
T* Blank()
{
    return static_cast<T*>(std::memset(operator_new(sizeof(T)), 0, sizeof(T)));
}

// The slot of a virtual member function in an Itanium ABI vtable (the two destructors take slots 0 and 1).
template <class F>
std::int32_t SlotOf(F member)
{
    std::uintptr_t raw[2];
    static_assert(sizeof(raw) == sizeof(F), "member pointer size");
    std::memcpy(raw, &member, sizeof(raw));
#if defined(__aarch64__)
    return static_cast<std::int32_t>(raw[0] / sizeof(void*));
#else
    return static_cast<std::int32_t>((raw[0] - 1) / sizeof(void*));
#endif
}

}  // namespace

extern "C" {

// Blank objects for the field maps.
FontManager* bc_new_FontManager() { return Blank<FontManager>(); }
FontFace* bc_new_FontFace() { return Blank<FontFace>(); }
FontRegistration* bc_new_FontRegistration() { return Blank<FontRegistration>(); }
FontFaceNode* bc_new_FontFaceNode() { return Blank<FontFaceNode>(); }
FontFaceBlock* bc_new_FontFaceBlock() { return Blank<FontFaceBlock>(); }
FontFaceList* bc_new_FontFaceList() { return Blank<FontFaceList>(); }
FontRegNode* bc_new_FontRegNode() { return Blank<FontRegNode>(); }
FontRegBlock* bc_new_FontRegBlock() { return Blank<FontRegBlock>(); }
FontRegList* bc_new_FontRegList() { return Blank<FontRegList>(); }
FontGroupNode* bc_new_FontGroupNode() { return Blank<FontGroupNode>(); }
FontGroupBlock* bc_new_FontGroupBlock() { return Blank<FontGroupBlock>(); }
FontGroupList* bc_new_FontGroupList() { return Blank<FontGroupList>(); }

// The global manager, built again for each scenario: the constructor runs and the test sets the members.
FontManager* bc_init_global_FontManager()
{
    TGFontManager::ConstructGlobal();
    return TGFontManager::Global();
}

FontManager* bc_TGFontManager_TGFontManager(FontManager* self) { return ::new (self) FontManager(); }
void bc_TGFontManager_DestructorBody(FontManager* self) { self->FontManager::~FontManager(); }
void bc_TGFontManager_RegisterFont(FontManager* self, const char* name, float size, const char* module,
                                   const char* function)
{
    self->RegisterFont(name, size, module, function);
}
void bc_TGFontManager_InitGlobal() { TGFontManager::InitGlobal(); }
void bc_TGFontManager_ConstructGlobal() { TGFontManager::ConstructGlobal(); }
void bc_TGFontManager_RegisterGlobalCleanup() { TGFontManager::RegisterGlobalCleanup(); }
void bc_TGFontManager_DestructGlobal() { TGFontManager::DestructGlobal(); }

FontFace* bc_TGFontFace_TGFontFace(FontFace* self, const char* name) { return ::new (self) FontFace(name); }
FontFace* bc_TGFontFace_TGFontFace_null(FontFace* self, const char*) { return ::new (self) FontFace(nullptr); }
void bc_TGFontFace_Destructor(FontFace* self) { self->FontFace::~FontFace(); }
void bc_TGFontFace_AddRegistration(FontFace* self, float size, const char* module, const char* function)
{
    self->AddRegistration(size, module, function);
}

// The slots of the virtual functions in the order of the exe vtables. kind 0: TGFontManager, 1: TGFontRegistration.
// Neither class has a virtual destructor in the exe table.
int bc_font_virtual_slots(int kind, std::int32_t* out)
{
    if (kind == 0) {
        std::int32_t slots[] = {SlotOf(&TGFontManager::SaveToStream), SlotOf(&TGFontManager::LoadFromStream)};
        std::memcpy(out, slots, sizeof(slots));
        return 2;
    }
    std::int32_t slots[] = {SlotOf(&TGFontRegistration::SaveToStream), SlotOf(&TGFontRegistration::LoadFromStream)};
    std::memcpy(out, slots, sizeof(slots));
    return 2;
}

// The stream functions are traps: each call reaches its exe address.
void bc_TGFontManager_CallTrap(FontManager* self, int which)
{
    if (which == 0)
        self->TGFontManager::SaveToStream(nullptr);
    else
        self->TGFontManager::LoadFromStream(nullptr);
}
void bc_TGFontRegistration_CallTrap(FontRegistration* self, int which)
{
    if (which == 0)
        self->TGFontRegistration::SaveToStream(nullptr);
    else
        self->TGFontRegistration::LoadFromStream(nullptr);
}

}  // extern "C"
