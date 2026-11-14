// C entry points for the TGModelPropertyManager tests. The first argument of a member function is the manager.
// The global manager (exe address 0x999de8) is built again for each scenario.
#include <cstring>
#include <new>

#include "BcFields.h"
#include "Crt.h"
#include "NiImage.h"
#include "TGFontManager.h"
#include "TGIconManager.h"
#include "TGModelPropertyManager.h"

using TemplateNode = TGListNode;
using TemplateBlock = TGListBlock;
using TemplateList = TGList;

BC_FIELDS(TGModelPropertyManager)
    BC_FIELD(TGModelPropertyManager, registeredFilters) BC_FIELD(TGModelPropertyManager, currentFilters)
    BC_FIELD(TGModelPropertyManager, globalTemplates) BC_FIELD(TGModelPropertyManager, localTemplates)
BC_FIELDS_END

BC_FIELDS(TemplateNode)
    BC_FIELD(TemplateNode, data) BC_FIELD(TemplateNode, next) BC_FIELD(TemplateNode, prev)
BC_FIELDS_END

BC_FIELDS(TemplateBlock)
    BC_FIELD(TemplateBlock, nextBlock) BC_FIELD(TemplateBlock, nodes)
BC_FIELDS_END

BC_FIELDS(TemplateList)
    BC_FIELD(TemplateList, count) BC_FIELD(TemplateList, head) BC_FIELD(TemplateList, tail)
    BC_FIELD(TemplateList, freeNodes) BC_FIELD(TemplateList, blocks) BC_FIELD(TemplateList, blockSize)
BC_FIELDS_END

namespace {

constexpr std::size_t kMaxRegistrations = 8;
std::uint32_t g_registered[kMaxRegistrations];
std::size_t g_registeredCount = 0;

// The functions that the manager registers with atexit, as the exe address of the same function.
void RecordAtExit(crt::AtExitFunction function)
{
    std::uint32_t address = function == &TGModelPropertyManager::DestructGlobal ? 0x6e9560
                            : function == &TGIconManager::DestructGlobal        ? 0x7299f0
                            : function == &TGFontManager::DestructGlobal        ? 0x734800
                            : function == &ni::ReleaseImageList                 ? 0x7c4d80
                            : function == &ni::ReleaseRendererList              ? 0x7e0be0
                            : function == &ni::ReleaseScratchRawImage           ? 0x7e0b60
                                                                                : 0;
    if (g_registeredCount < kMaxRegistrations)
        g_registered[g_registeredCount++] = address;
}

struct InstallAtExitHandler {
    InstallAtExitHandler() { crt::atExitHandler = RecordAtExit; }
} g_installAtExitHandler;

}  // namespace

extern "C" {

void bc_set_captured_pointer(void* value);

// The module attribute g_kModelPropertyManager. The Python value that stands for a failed conversion is 0xDEAD0000.
std::uint32_t bc_Appc_set_g_kModelPropertyManager(TGModelPropertyManager* value)
{
    if (value == reinterpret_cast<TGModelPropertyManager*>(0xDEAD0000))
        return 1;
    *TGModelPropertyManager::Global() = *value;
    return 0;
}

std::uint32_t bc_Appc_get_g_kModelPropertyManager()
{
    bc_set_captured_pointer(TGModelPropertyManager::Global());
    return 0xB1000000;
}

// A blank manager for a test: the test runs the constructor on it.
TGModelPropertyManager* bc_new_TGModelPropertyManager()
{
    return static_cast<TGModelPropertyManager*>(std::memset(operator_new(sizeof(TGModelPropertyManager)), 0, sizeof(TGModelPropertyManager)));
}

TGListNode* bc_new_TemplateNode() { return static_cast<TGListNode*>(std::memset(operator_new(sizeof(TGListNode)), 0, sizeof(TGListNode))); }
TGListBlock* bc_new_TemplateBlock()
{
    return static_cast<TGListBlock*>(std::memset(operator_new(sizeof(TGListBlock)), 0, sizeof(TGListBlock)));
}
TGList* bc_new_TemplateList() { return static_cast<TGList*>(std::memset(operator_new(sizeof(TGList)), 0, sizeof(TGList))); }

// The global manager as a test object: built again, so that each scenario starts from the state of the constructor.
TGModelPropertyManager* bc_init_global_TGModelPropertyManager()
{
    TGModelPropertyManager::ConstructGlobal();
    return TGModelPropertyManager::Global();
}

TGModelPropertyManager* bc_TGModelPropertyManager_TGModelPropertyManager(TGModelPropertyManager* self)
{
    return ::new (self) TGModelPropertyManager();
}
void bc_TGModelPropertyManager_DestructorBody(TGModelPropertyManager* self) { self->TGModelPropertyManager::~TGModelPropertyManager(); }

void bc_TGModelPropertyManager_ConstructGlobal() { TGModelPropertyManager::ConstructGlobal(); }
void bc_TGModelPropertyManager_DestructGlobal() { TGModelPropertyManager::DestructGlobal(); }
void bc_TGModelPropertyManager_RegisterGlobalCleanup() { TGModelPropertyManager::RegisterGlobalCleanup(); }
void bc_TGModelPropertyManager_InitGlobal() { TGModelPropertyManager::InitGlobal(); }

void bc_atexit_clear() { g_registeredCount = 0; }
std::int32_t bc_atexit_count() { return static_cast<std::int32_t>(g_registeredCount); }
std::uint32_t bc_atexit_address(std::int32_t index) { return g_registered[index]; }

TGList* bc_TGModelPropertyManager_GetGlobalList(TGModelPropertyManager* self) { return self->GetGlobalList(); }
TGList* bc_TGModelPropertyManager_GetLocalList(TGModelPropertyManager* self) { return self->GetLocalList(); }

TGModelProperty* bc_TGModelPropertyManager_FindByNameAndType(TGModelPropertyManager* self, const char* name,
                                                              std::int32_t type, std::int32_t local)
{
    return self->FindByNameAndType(name, type, local);
}
TGModelProperty* bc_TGModelPropertyManager_FindByName(TGModelPropertyManager* self, const char* name,
                                                       std::int32_t local)
{
    return self->FindByName(name, local);
}

std::uint8_t bc_TGModelPropertyManager_RegisterGlobalTemplate(TGModelPropertyManager* self, TGModelProperty* p)
{
    return self->RegisterGlobalTemplate(p);
}
std::uint8_t bc_TGModelPropertyManager_RegisterLocalTemplate(TGModelPropertyManager* self, TGModelProperty* p)
{
    return self->RegisterLocalTemplate(p);
}
std::uint8_t bc_TGModelPropertyManager_IsGlobalTemplate(TGModelPropertyManager* self, TGModelProperty* p)
{
    return self->IsGlobalTemplate(p);
}
std::uint8_t bc_TGModelPropertyManager_IsLocalTemplate(TGModelPropertyManager* self, TGModelProperty* p)
{
    return self->IsLocalTemplate(p);
}
std::uint8_t bc_TGModelPropertyManager_RemoveTemplate(TGModelPropertyManager* self, std::int32_t local,
                                                       TGModelProperty* p, std::uint8_t destroy)
{
    return self->RemoveTemplate(local, p, destroy != 0);
}
void bc_TGModelPropertyManager_ClearGlobalTemplates(TGModelPropertyManager* self) { self->ClearGlobalTemplates(); }
void bc_TGModelPropertyManager_ClearLocalTemplates(TGModelPropertyManager* self) { self->ClearLocalTemplates(); }

// The slots and methods that are not reconstructed. Each call must reach the trap of its exe address.
// 0, 1: the virtual slots; 2 to 7: the filter functions.
void bc_TGModelPropertyManager_CallTrap(TGModelPropertyManager* self, std::int32_t which)
{
    switch (which) {
    case 0: self->SaveToStream(nullptr); break;
    case 1: self->LoadFromStream(nullptr); break;
    case 2: self->RegisterFilter(nullptr, ""); break;
    case 3: self->AddFilter(nullptr); break;
    case 4: self->RemoveFilter(nullptr); break;
    case 5: self->ApplyFilters(nullptr); break;
    case 6: self->ClearRegisteredFilters(); break;
    case 7: self->ClearCurrentFilters(); break;
    }
}

}
