#include "TGIconGroup.h"

#include <cstring>

#include "IconBoundary.h"
#include "TGIconManager.h"
#include "Trap.h"
#include "X87.h"

TGIconLocationNode* TGIconGroup::iterator = nullptr;

// 0x72bb20. The hash table and the texture array are made first; the array starts with one element, grows to 0xf
// and then grows by 10.
TGIconGroup::TGIconGroup(const char* initialName)
    : name(nullptr), lockCount(0), locations(kBucketCount)
{
    textures.Reallocate(TGIconTextureArray::kGroupCapacity);
    textures.growBy = TGIconTextureArray::kGroupGrowBy;
    SetName(initialName);
}

// 0x72bce0, called by the deleting destructor 0x72acb0. The members go in reverse order as the exe does: the
// texture array, then the location table (0x72bd2b, 0x72bd5b).
TGIconGroup::~TGIconGroup()
{
    lockCount = 0;
    if (name != nullptr)
        operator_delete(name);
    Reset();
}

// 0x72c980
PyWrapperObject* TGIconGroup::GetPyWrapper() { return g_iconBoundary.newPythonWrapper(this); }

// 0x72cb70
bool TGIconGroup::SaveToStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x72cb70);
    return false;
}

// 0x72cc50
bool TGIconGroup::LoadFromStream(TGStream*)
{
    BC_NOT_RECONSTRUCTED(0x72cc50);
    return false;
}

// 0x72bf70
void TGIconGroup::SetName(const char* newName)
{
    if (name != nullptr) {
        operator_delete(name);
        name = nullptr;
    }
    if (newName != nullptr)
        name = NewStringCopy(newName);
}

// 0x72bde0. The textures lose the references of the array, the locations are deleted with the reference of their
// texture, then the nodes of the location table are freed and the count is 0.
void TGIconGroup::Reset()
{
    for (std::uint32_t i = 0, number = textures.used; i < number; ++i) {
        NiTextureProperty* removed;
        textures.RemoveAt(&removed, i);
        ni::Release(removed);
    }
    for (std::uint32_t i = 0; i < textures.used; ++i) {
        NiTextureProperty* none = nullptr;
        TGIconTextureArray::AssignElement(&textures.data[i], &none);
    }
    textures.used = 0;
    textures.count = 0;

    for (std::uint32_t bucket = 0; bucket < locations.bucketCount; ++bucket) {
        for (TGIconLocationNode* node = locations.buckets[bucket]; node != nullptr;) {
            TGIconLocation* location = node->value;
            node = node->next;
            if (location != nullptr) {
                if (location->texture != nullptr)
                    ni::Release(location->texture);
                operator_delete(location);
            }
        }
    }
    for (std::uint32_t bucket = 0; bucket < locations.bucketCount; ++bucket) {
        while (TGIconLocationNode* node = locations.buckets[bucket]) {
            locations.buckets[bucket] = node->next;
            locations.ClearNode(node);
            operator_delete(node);
        }
    }
    locations.count = 0;
}

// 0x72bfe0. The path is the file name, or the working directory and the file name when the second character of the
// name is not a colon (B0029, B0030). A texture that is in the cache is taken from it; else the renderer makes it and the
// cache keeps it. The result is the index of the new texture property in the array, or -1 (E0188).
std::int32_t TGIconGroup::LoadIconTexture(const char* file, bool flag)
{
    NiTextureProperty* property = nullptr;
    NiTextureProperty* made = g_iconBoundary.newTextureProperty();
    if (made != nullptr) {
        ++made->refCount;
        property = made;
    }

    char path[kPathBufferSize];
    std::size_t workingLength = 0;
    if (file[0] == 0 || file[1] != ':')  // B0029: the exe reads file[1] also for the empty text
        workingLength = std::strlen(g_workingDirectory);
    if (workingLength + std::strlen(file) + 1 > sizeof(path)) {
        BC_NOT_RECONSTRUCTED(0x72c059);  // B0030: the exe writes past the buffer of 256 bytes
        return -1;
    }
    if (workingLength != 0)
        std::memcpy(path, g_workingDirectory, workingLength);
    std::strcpy(path + workingLength, file);

    NiImage* texture = nullptr;
    TGIconManager& manager = *TGIconManager::Global();
    TGIconTextureCache& cache = manager.textureCache;
    if (TGIconTextureNode* node = cache.FindNode(path)) {
        texture = node->value;
        ni::Acquire(texture);
    } else {
        NiImage* created = g_iconBoundary.createTexture(path, flag);
        if (texture != created) {
            ni::Release(texture);
            texture = created;
            ni::Acquire(texture);
        }
        ni::Acquire(texture);
        cache.Insert(path, texture);  // B0027
    }

    if (property->texture != texture) {
        ni::Release(property->texture);
        property->texture = texture;
        ni::Acquire(texture);
    }
    std::int32_t handle = -1;
    if (property->texture != nullptr) {
        ni::Acquire(property);
        textures.Add(property);
        handle = static_cast<std::int32_t>(textures.used) - 1;
    }
    ni::Release(texture);
    ni::Release(property);
    return handle;
}

// The chain walk that nine functions of the exe do inline (0x72c780 is the first): the location of an icon number,
// or null.
TGIconLocation* TGIconGroup::FindLocation(std::int32_t number)
{
    TGIconLocationNode* node = locations.FindNode(static_cast<std::uint32_t>(number));
    return node != nullptr ? node->value : nullptr;
}

// 0x72c260. An icon number that has a location keeps the node and loses the old location without freeing it
// (B0022). A handle that names no element reads outside the array (B0023).
void TGIconGroup::SetIconLocation(std::int32_t number, std::int32_t textureHandle, float x, float y, float width,
                                  float height, std::int32_t rotation, std::int32_t mirror)
{
    using x87::Reg;
    if (textureHandle == -1)
        return;
    TGIconLocation* location = new TGIconLocation();

    NiTextureProperty* property;
    textures.GetAt(&property, static_cast<std::uint32_t>(textureHandle));
    if (location->texture != property) {
        ni::Release(location->texture);
        location->texture = property;
        ni::Acquire(property);
    }
    ni::Release(property);

    location->pixelWidth = width;
    location->pixelHeight = height;
    NiImage* texture = location->texture->texture;
    float textureWidth = StoreF(Reg(static_cast<double>(texture->width)));
    Reg textureHeight = Reg(static_cast<double>(texture->height));
    Reg bottom = Reg(y) + Reg(height);
    float right = StoreF(Reg(x) + Reg(width));
    float left = StoreF(Reg(x) / Reg(textureWidth));
    float top = StoreF(Reg(y) / textureHeight);
    float bottomEdge = StoreF(bottom / textureHeight);
    float rightEdge = StoreF(Reg(right) / Reg(textureWidth));
    location->corners[0] = left;
    location->corners[1] = top;
    location->corners[2] = left;
    location->corners[3] = bottomEdge;
    location->corners[4] = rightEdge;
    location->corners[5] = bottomEdge;
    location->corners[6] = rightEdge;
    location->corners[7] = top;

    locations.Insert(static_cast<std::uint32_t>(number), location);  // B0022
    if (rotation != 0)
        RotateIconLocation(number, rotation);
    if (mirror != 0)
        MirrorIconLocation(number, mirror);
}

// 0x72c530. A rotation of 1, 2 or 3 moves the corners by that many places; 1 and 3 swap the width and the height.
// Other values change nothing.
void TGIconGroup::RotateIconLocation(std::int32_t number, std::int32_t rotation)
{
    using x87::Reg;
    TGIconLocation* location = FindLocation(number);
    float old[8];
    for (int i = 0; i < 8; ++i)
        old[i] = location->corners[i];
    Reg width = Reg(location->pixelWidth);
    Reg height = Reg(location->pixelHeight);
    int shift;
    switch (rotation) {
    case 1:
        shift = 2;
        break;
    case 2:
        shift = 4;
        break;
    case 3:
        shift = 6;
        break;
    default:
        return;
    }
    for (int i = 0; i < 8; ++i)
        location->corners[i] = old[(i + shift) % 8];
    if (rotation != 2) {
        location->pixelWidth = StoreF(height);
        location->pixelHeight = StoreF(width);
    }
}

// 0x72c690. A mirror of 1 reverses the order of the four corners; another value that is not 0 swaps the corners in
// pairs.
void TGIconGroup::MirrorIconLocation(std::int32_t number, std::int32_t mirror)
{
    TGIconLocation* location = FindLocation(number);
    float old[8];
    for (int i = 0; i < 8; ++i)
        old[i] = location->corners[i];
    if (mirror == 0)
        return;
    static constexpr int kReverse[4] = {3, 2, 1, 0};
    static constexpr int kPairs[4] = {1, 0, 3, 2};
    const int* order = mirror == 1 ? kReverse : kPairs;
    for (int corner = 0; corner < 4; ++corner) {
        location->corners[2 * corner] = old[2 * order[corner]];
        location->corners[2 * corner + 1] = old[2 * order[corner] + 1];
    }
}

// 0x72c7e0
double TGIconGroup::GetIconPixelWidth(std::int32_t number)
{
    TGIconLocation* location = FindLocation(number);
    return location != nullptr ? static_cast<double>(location->pixelWidth) : 0.0;
}

// 0x72c840
double TGIconGroup::GetIconPixelHeight(std::int32_t number)
{
    TGIconLocation* location = FindLocation(number);
    return location != nullptr ? static_cast<double>(location->pixelHeight) : 0.0;
}

// 0x72c8a0. The scale of the icon manager times the pixel width, divided by the screen width of the manager.
double TGIconGroup::GetIconScreenWidth(std::int32_t number)
{
    using x87::Reg;
    TGIconLocation* location = FindLocation(number);
    if (location == nullptr)
        return 0.0;
    TGIconManager& manager = *TGIconManager::Global();
    return StoreD(Reg(manager.scale) * Reg(location->pixelWidth) / Reg(manager.screenWidth));  // B0026
}

// 0x72c910
double TGIconGroup::GetIconScreenHeight(std::int32_t number)
{
    using x87::Reg;
    TGIconLocation* location = FindLocation(number);
    if (location == nullptr)
        return 0.0;
    TGIconManager& manager = *TGIconManager::Global();
    return StoreD(Reg(manager.scale) * Reg(location->pixelHeight) / Reg(manager.screenHeight));  // B0026
}

// 0x72cd80
bool TGIconGroup::IsNumValid(std::int32_t number) { return locations.FindNode(static_cast<std::uint32_t>(number)) != nullptr; }

// 0x72cdd0. The first node of the first bucket that has one, or null.
void TGIconGroup::GoToFirstPos()
{
    for (std::uint32_t bucket = 0; bucket < locations.bucketCount; ++bucket) {
        if (locations.buckets[bucket] != nullptr) {
            iterator = locations.buckets[bucket];
            return;
        }
    }
    iterator = nullptr;
}

// 0x72ce00. The number of the node that the iterator is at; the iterator moves to the next node of the chain or to
// the first node of a later bucket. Without a node the call reads null (B0024).
std::int32_t TGIconGroup::GetNext()
{
    TGIconLocationNode* node = iterator;
    std::uint32_t key = node->key;  // B0024
    if (node->next != nullptr) {
        iterator = node->next;
        return static_cast<std::int32_t>(key);
    }
    for (std::uint32_t bucket = locations.Hash(key) + 1; bucket < locations.bucketCount; ++bucket) {
        if (locations.buckets[bucket] != nullptr) {
            iterator = locations.buckets[bucket];
            return static_cast<std::int32_t>(key);
        }
    }
    iterator = nullptr;
    return static_cast<std::int32_t>(key);
}
