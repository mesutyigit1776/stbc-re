// Deliberately wrong variants of the font functions. The emulator tests must reject them.
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

#include "CrtFloat.h"
#include "TGFontManager.h"

namespace {

// RegisterFont with one deliberate defect; the code follows TGFontManager::RegisterFont.
// 1: the default font changes on every call. 2: the family name is compared without case. 3: every call makes a new
// family. 4: the default size is not stored. 5: a new family goes to the head of the list.
void RegisterWithDefect(TGFontManager* self, const char* name, float size, const char* module, const char* function,
                        int defect)
{
    TGFontFace* face = nullptr;
    if (defect != 3) {
        for (TGListNode* node = self->families.head; node != nullptr; node = node->next) {
            TGFontFace* candidate = static_cast<TGFontFace*>(node->data);
            bool same = defect == 2 ? strcasecmp(name, candidate->name) == 0 : std::strcmp(name, candidate->name) == 0;
            if (same) {
                face = candidate;
                break;
            }
        }
    }
    if (face == nullptr) {
        face = new TGFontFace(name);
        if (defect == 5) {
            self->families.Append(face);
            TGListNode* node = self->families.tail;
            if (node != self->families.head) {
                node->prev->next = nullptr;
                self->families.tail = node->prev;
                node->prev = nullptr;
                node->next = self->families.head;
                self->families.head->prev = node;
                self->families.head = node;
            }
        } else {
            self->families.Append(face);
        }
    }
    face->AddRegistration(size, module, function);
    if (self->defaultName == nullptr || defect == 1) {
        if (self->defaultName != nullptr)
            operator_delete(self->defaultName);
        self->defaultName = NewStringCopy(name);
        if (defect != 4)
            self->defaultSize = size;
    }
}

// AddRegistration with one deliberate defect. 1: the group name has one decimal. 2: module and function are swapped.
// 3: the size is stored as an integer. 4: the name is made by the host sprintf, which rounds a tie to even.
void AddWithDefect(TGFontFace* self, float size, const char* module, const char* function, int defect)
{
    TGFontRegistration* record = new TGFontRegistration(self->name, module, function,
                                                        defect == 3 ? static_cast<float>(static_cast<int>(size)) : size);
    self->registrations.Append(record);
    char text[256];
    if (defect == 4)
        std::snprintf(text, sizeof(text), "%s%.2f", self->name, static_cast<double>(size));
    else
        crt::Sprintf(text, defect == 1 ? "%s%.1f" : "%s%.2f", self->name, static_cast<double>(size));
    TGIconManager::Global()->RegisterIconGroup(text, defect == 2 ? function : module, defect == 2 ? module : function);
}

}  // namespace

extern "C" {

void bc_TGFontManager_RegisterFont_defect_default_every_call(TGFontManager* self, const char* name, float size,
                                                             const char* module, const char* function)
{
    RegisterWithDefect(self, name, size, module, function, 1);
}
void bc_TGFontManager_RegisterFont_defect_case_insensitive(TGFontManager* self, const char* name, float size,
                                                           const char* module, const char* function)
{
    RegisterWithDefect(self, name, size, module, function, 2);
}
void bc_TGFontManager_RegisterFont_defect_new_family_every_call(TGFontManager* self, const char* name, float size,
                                                                const char* module, const char* function)
{
    RegisterWithDefect(self, name, size, module, function, 3);
}
void bc_TGFontManager_RegisterFont_defect_no_default_size(TGFontManager* self, const char* name, float size,
                                                          const char* module, const char* function)
{
    RegisterWithDefect(self, name, size, module, function, 4);
}
void bc_TGFontManager_RegisterFont_defect_family_at_head(TGFontManager* self, const char* name, float size,
                                                         const char* module, const char* function)
{
    RegisterWithDefect(self, name, size, module, function, 5);
}

void bc_TGFontFace_AddRegistration_defect_one_decimal(TGFontFace* self, float size, const char* module, const char* function)
{
    AddWithDefect(self, size, module, function, 1);
}
void bc_TGFontFace_AddRegistration_defect_swapped_texts(TGFontFace* self, float size, const char* module,
                                                        const char* function)
{
    AddWithDefect(self, size, module, function, 2);
}
void bc_TGFontFace_AddRegistration_defect_integer_size(TGFontFace* self, float size, const char* module, const char* function)
{
    AddWithDefect(self, size, module, function, 3);
}
void bc_TGFontFace_AddRegistration_defect_host_format(TGFontFace* self, float size, const char* module, const char* function)
{
    AddWithDefect(self, size, module, function, 4);
}

// The destructor of the family frees the registrations too, which the exe does not.
void bc_TGFontFace_Destructor_defect_frees_registrations(TGFontFace* self)
{
    for (TGListNode* node = self->registrations.head; node != nullptr; node = node->next)
    {
        TGIconRegistration* record = static_cast<TGFontRegistration*>(node->data);
        record->TGIconRegistration::~TGIconRegistration();
        operator_delete(record);
    }
    self->TGFontFace::~TGFontFace();
}

// The destructor of the manager keeps the default name.
void bc_TGFontManager_DestructorBody_defect_keeps_default_name(TGFontManager* self)
{
    char* name = self->defaultName;
    self->defaultName = nullptr;
    self->TGFontManager::~TGFontManager();
    self->defaultName = name;
}

// The constructor of the manager gives the list the block size 3.
TGFontManager* bc_TGFontManager_TGFontManager_defect_block_size(TGFontManager* self)
{
    ::new (self) TGFontManager();
    self->families.blockSize = 3;
    return self;
}

// The static initializer does not register the destructor.
void bc_TGFontManager_InitGlobal_defect_no_cleanup() { TGFontManager::ConstructGlobal(); }

}  // extern "C"
