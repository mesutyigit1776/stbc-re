// Deliberately wrong variants of the icon functions. The emulator tests must reject them.
#include <cstring>

#include "IconBoundary.h"
#include "TGIconGroup.h"
#include "TGIconManager.h"
#include "X87.h"

struct DefectManagerArgs {
    TGIconManager* m;
    NiCamera* c;
};
struct DefectGroupArgs {
    TGIconGroup* g;
};

extern "C" {

void bc_set_captured_double(double value);

// SetIconLocation. Defect 1: the sum of y and the height is rounded to float before the division, as float-only code
// would do. Defect 2: the left and the top coordinates are swapped.
static void SetLocation(TGIconGroup* self, std::int32_t number, std::int32_t handle, float x, float y, float width,
                        float height, int defect)
{
    using x87::Reg;
    if (handle == -1)
        return;
    TGIconLocation* location = new TGIconLocation();
    NiTextureProperty* property;
    self->textures.GetAt(&property, static_cast<std::uint32_t>(handle));
    location->texture = property;
    ni::Acquire(property);
    ni::Release(property);
    location->pixelWidth = width;
    location->pixelHeight = height;
    NiImage* texture = location->texture->texture;
    float textureWidth = StoreF(Reg(static_cast<double>(texture->width)));
    Reg textureHeight = Reg(static_cast<double>(texture->height));
    Reg bottom = defect == 1 ? Reg(StoreF(Reg(y) + Reg(height))) : Reg(y) + Reg(height);
    float right = StoreF(Reg(x) + Reg(width));
    float left = StoreF(Reg(x) / Reg(textureWidth));
    float top = StoreF(Reg(y) / textureHeight);
    float bottomEdge = StoreF(bottom / textureHeight);
    float rightEdge = StoreF(Reg(right) / Reg(textureWidth));
    location->corners[0] = defect == 2 ? top : left;
    location->corners[1] = defect == 2 ? left : top;
    location->corners[2] = left;
    location->corners[3] = bottomEdge;
    location->corners[4] = rightEdge;
    location->corners[5] = bottomEdge;
    location->corners[6] = rightEdge;
    location->corners[7] = top;
    self->locations.Insert(static_cast<std::uint32_t>(number), location);
}

void bc_TGIconGroup_SetIconLocation_defect1(TGIconGroup* self, std::int32_t number, std::int32_t handle, float x, float y,
                                            float width, float height, std::int32_t, std::int32_t)
{
    SetLocation(self, number, handle, x, y, width, height, 1);
}

void bc_TGIconGroup_SetIconLocation_defect2(TGIconGroup* self, std::int32_t number, std::int32_t handle, float x, float y,
                                            float width, float height, std::int32_t, std::int32_t)
{
    SetLocation(self, number, handle, x, y, width, height, 2);
}

// RotateIconLocation: the rotation 3 moves the corners by two pairs, as the rotation 1 does.
void bc_TGIconGroup_RotateIconLocation_defect(TGIconGroup* self, std::int32_t number, std::int32_t rotation)
{
    self->RotateIconLocation(number, rotation == 3 ? 1 : rotation);
}

// MirrorIconLocation: a mirror that is not 1 reverses the corners, as the mirror 1 does.
void bc_TGIconGroup_MirrorIconLocation_defect(TGIconGroup* self, std::int32_t number, std::int32_t mirror)
{
    self->MirrorIconLocation(number, mirror != 0 ? 1 : 0);
}

// GetNext: gives the number of the node that comes next.
std::int32_t bc_TGIconGroup_GetNext_defect(TGIconGroup* self)
{
    TGIconLocationNode* start = TGIconGroup::iterator;
    self->GetNext();
    return TGIconGroup::iterator != nullptr ? static_cast<std::int32_t>(TGIconGroup::iterator->key)
                                            : static_cast<std::int32_t>(start->key);
}

// GetIconScreenWidth: the product and the quotient are computed with plain double arithmetic. The result equals the
// original at 53 bits and differs at 24 bits.
double bc_TGIconGroup_GetIconScreenWidth_defect_p24(TGIconGroup* self, std::int32_t number)
{
    TGIconLocationNode* node = self->locations.FindNode(static_cast<std::uint32_t>(number));
    if (node == nullptr || node->value == nullptr)
        return 0.0;
    TGIconManager& manager = *TGIconManager::Global();
    double product = static_cast<double>(manager.scale) * static_cast<double>(node->value->pixelWidth);
    return product / static_cast<double>(manager.screenWidth);
}

// LoadIconTexture: the texture is not entered into the cache, and the local reference of the property is not released.
std::int32_t bc_TGIconGroup_LoadIconTexture_defect(TGIconGroup* self, const char* file, std::uint32_t flag)
{
    NiTextureProperty* property = g_iconBoundary.newTextureProperty();
    ++property->refCount;
    char path[256];
    std::strcpy(path, g_workingDirectory);
    std::strcat(path, file);
    NiImage* texture = g_iconBoundary.createTexture(path, (flag & 0xff) != 0);
    property->texture = texture;
    ni::Acquire(texture);
    std::int32_t handle = -1;
    if (texture != nullptr) {
        ni::Acquire(property);
        self->textures.Add(property);
        handle = static_cast<std::int32_t>(self->textures.used) - 1;
    }
    return handle;
}

// Reset: the removed texture properties are not released.
void bc_TGIconGroup_Reset_defect(TGIconGroup* self)
{
    for (std::uint32_t i = 0, number = self->textures.used; i < number; ++i) {
        NiTextureProperty* removed;
        self->textures.RemoveAt(&removed, i);
    }
    self->textures.used = 0;
    self->textures.count = 0;
    for (std::uint32_t bucket = 0; bucket < self->locations.bucketCount; ++bucket) {
        for (TGIconLocationNode* node = self->locations.buckets[bucket]; node != nullptr;) {
            TGIconLocation* location = node->value;
            node = node->next;
            if (location != nullptr) {
                ni::Release(location->texture);
                operator_delete(location);
            }
        }
    }
    for (std::uint32_t bucket = 0; bucket < self->locations.bucketCount; ++bucket) {
        while (TGIconLocationNode* node = self->locations.buckets[bucket]) {
            self->locations.buckets[bucket] = node->next;
            operator_delete(node);
        }
    }
    self->locations.count = 0;
}

// SetAtGrow: the array grows by one element more than growBy asks for.
std::uint32_t bc_IconTextureArray_SetAtGrow_defect(TGIconTextureArray* self, std::uint32_t index, NiTextureProperty* owned)
{
    if (index >= self->capacity)
        self->Reallocate(self->growBy + index + 1);
    ni::Acquire(owned);
    self->StoreAt(index, owned);
    ni::Release(owned);
    return index;
}

// The hash of the texture cache takes the characters as unsigned.
std::uint32_t bc_TextureCache_Hash_defect(TGIconTextureCache* self, const char* key)
{
    std::uint32_t hash = 0;
    for (const char* p = key; *p != 0; ++p)
        hash = hash * 33 + static_cast<std::uint32_t>(static_cast<unsigned char>(*p));
    return hash % self->bucketCount;
}

// Insert of the cache: an existing node keeps its old key text, which then is lost.
void bc_TextureCache_Insert_defect(TGIconTextureCache* self, const char* key, NiImage* owned)
{
    std::uint32_t bucket = self->Hash(key);
    for (TGIconTextureNode* existing = self->buckets[bucket]; existing != nullptr; existing = existing->next) {
        if (self->KeysEqual(key, existing->key)) {
            ni::Acquire(owned);
            self->SetNode(existing, key, owned);
            ni::Release(owned);
            return;
        }
    }
    self->Insert(key, owned);
}

// RegisterIconGroup: the count of the records is not raised.
void bc_TGIconManager_RegisterIconGroup_defect(TGIconManager* self, const char* name, const char* module, const char* function)
{
    self->RegisterIconGroup(name, module, function);
    --self->registrations.count;
}

// DeleteIconGroup: the renderer is not asked to finish its commands.
std::uint8_t bc_TGIconManager_DeleteIconGroup_defect(TGIconManager* self, const char* name)
{
    auto saved = g_iconBoundary.flushRenderQueue;
    g_iconBoundary.flushRenderQueue = nullptr;
    std::uint8_t result = self->DeleteIconGroup(name);
    g_iconBoundary.flushRenderQueue = saved;
    return result;
}

// Purge: a group with a lock count is deleted too.
void bc_TGIconManager_Purge_defect(TGIconManager* self)
{
    for (TGListNode* node = self->groups.head; node != nullptr; node = node->next)
        static_cast<TGIconGroup*>(node->data)->lockCount = 0;
    self->Purge();
}

// AddIconGroup: a group with a loaded name is added too.
void bc_TGIconManager_AddIconGroup_defect(TGIconManager* self, TGIconGroup* group) { self->groups.Append(group); }

// SetDisplayInfo: the size of the view is turned into a float from a signed 32 bit value.
void bc_TGIconManager_SetDisplayInfo_defect(TGIconManager* self, NiCamera* camera, float scale)
{
    using x87::Reg;
    if (camera != nullptr) {
        self->SetCamera(camera);
        self->screenWidth = StoreF(Reg(static_cast<double>(static_cast<std::int32_t>(self->camera->renderer->GetWidth()))));
        self->screenHeight = StoreF(Reg(static_cast<double>(static_cast<std::int32_t>(self->camera->renderer->GetHeight()))));
        self->SetPixelCenterAdj(self->pixelCenterAdj);
    }
    self->scale = scale;
}

// SetPixelCenterAdj: the quotients use the screen height for both.
void bc_TGIconManager_SetPixelCenterAdj_defect(TGIconManager* self, float adjustment)
{
    using x87::Reg;
    self->pixelCenterAdj = adjustment;
    self->pixelCenterAdjX = StoreF(Reg(adjustment) / Reg(self->screenHeight));
    self->pixelCenterAdjY = StoreF(Reg(adjustment) / Reg(self->screenHeight));
}

// The wrapper of GetScreenWidth reads the screen height.
std::uint32_t bc_Appc_TGIconManager_GetScreenWidth_defect(std::uint32_t, DefectManagerArgs* a)
{
    bc_set_captured_double(static_cast<double>(a->m->GetScreenHeight()));
    return 0xF1000000;
}

// The wrapper of SetCamera stores the pointer without reference counts.
std::uint32_t bc_Appc_TGIconManager_SetCamera_defect(std::uint32_t, DefectManagerArgs* a)
{
    a->m->camera = a->c;
    return 0x95DD20;
}

// The wrapper of delete frees the block without the destructor.
std::uint32_t bc_Appc_TGIconGroup_delete_defect(std::uint32_t, DefectGroupArgs* a)
{
    operator_delete(a->g);
    return 0x95DD20;
}

}  // extern "C"
