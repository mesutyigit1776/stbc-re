// C entry points for the tests of the icon manager, the icon group and their containers. The calls of the icon classes
// into the renderer, NetImmerse and the script layer go to test functions here. They write one text line per call into
// a log with fixed storage (no allocation), and the tests compare the log with the log of the exe side.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include "BcFields.h"
#include "Crt.h"
#include "IconBoundary.h"
#include "TGIconGroup.h"
#include "TGIconManager.h"
#include "TGList.h"
#include "TGObject.h"
#include "Trap.h"

using IconGroupNode = TGListNode;
using IconGroupBlock = TGListBlock;
using IconGroupList = TGList;
using IconOtherNode = TGListNode;
using IconOtherBlock = TGListBlock;
using IconOtherList = TGList;
using IconLocationNode = TGIconLocationNode;
using IconLocationTable = TGIconLocationTable;
using IconTextureArray = TGIconTextureArray;
using IconRegistration = TGIconRegistration;
using IconManager = TGIconManager;
using IconGroup = TGIconGroup;
using IconLocation = TGIconLocation;
using TextureCache = TGIconTextureCache;
using TextureCacheNode = TGIconTextureNode;

// Test objects: the deletion writes a log line and frees the block; the memory stays readable, so that a test can read
// the object afterwards. The serial number is in the first word of the part that the icon code does not use.
struct TestTextureProperty : NiTextureProperty {
    void DeleteThis() override;
    std::uint32_t serial = 0;
};
struct TestTexture : NiImage {
    void DeleteThis() override;
    std::uint32_t serial = 0;
};
struct TestCamera : NiCamera {
    void DeleteThis() override;
};
struct TestRenderer : NiD3DRender {
    std::uint32_t GetWidth() override { return width; }
    std::uint32_t GetHeight() override { return height; }
    std::uint32_t width;
    std::uint32_t height;
};

// A pointer in memory of its own: the argument of the functions that take a pointer to a smart pointer.
struct PropertyCellObject {
    NiTextureProperty* value;
};
struct CameraCellObject {
    NiCamera* camera;
};

#define BC_OFFSET_FIELD(name, cls, member) {name, static_cast<std::uint32_t>(offsetof(cls, member))},

BC_FIELDS(IconManager)
    BC_FIELD(IconManager, groups) BC_FIELD(IconManager, otherList) BC_FIELD(IconManager, unused48)
    BC_FIELD(IconManager, unused4c) BC_FIELD(IconManager, camera) BC_FIELD(IconManager, scale)
    BC_FIELD(IconManager, screenWidth) BC_FIELD(IconManager, screenHeight) BC_FIELD(IconManager, pixelCenterAdj)
    BC_FIELD(IconManager, pixelCenterAdjX) BC_FIELD(IconManager, pixelCenterAdjY) BC_FIELD(IconManager, textureCache)
    BC_OFFSET_FIELD("data", IconManager, registrations.data)
    BC_OFFSET_FIELD("capacity", IconManager, registrations.capacity)
    BC_OFFSET_FIELD("used", IconManager, registrations.used)
    BC_OFFSET_FIELD("count", IconManager, registrations.count)
    BC_OFFSET_FIELD("growBy", IconManager, registrations.growBy)
BC_FIELDS_END

BC_FIELDS(IconGroup)
    BC_FIELD(IconGroup, name) BC_FIELD(IconGroup, lockCount) BC_FIELD(IconGroup, locations) BC_FIELD(IconGroup, textures)
BC_FIELDS_END

BC_FIELDS(IconLocation)
    BC_FIELD(IconLocation, texture) BC_FIELD(IconLocation, pixelWidth) BC_FIELD(IconLocation, pixelHeight)
    {"c0", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 0)},
    {"c1", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 4)},
    {"c2", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 8)},
    {"c3", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 12)},
    {"c4", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 16)},
    {"c5", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 20)},
    {"c6", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 24)},
    {"c7", static_cast<std::uint32_t>(offsetof(IconLocation, corners) + 28)},
BC_FIELDS_END

BC_FIELDS(IconLocationNode)
    BC_FIELD(IconLocationNode, key) BC_FIELD(IconLocationNode, value) BC_FIELD(IconLocationNode, next)
BC_FIELDS_END

BC_FIELDS(IconLocationTable)
    BC_FIELD(IconLocationTable, count) BC_FIELD(IconLocationTable, bucketCount) BC_FIELD(IconLocationTable, buckets)
BC_FIELDS_END

BC_FIELDS(IconTextureArray)
    BC_FIELD(IconTextureArray, data) BC_FIELD(IconTextureArray, capacity) BC_FIELD(IconTextureArray, used)
    BC_FIELD(IconTextureArray, count) BC_FIELD(IconTextureArray, growBy)
BC_FIELDS_END

BC_FIELDS(IconRegistration)
    BC_FIELD(IconRegistration, groupName) BC_FIELD(IconRegistration, moduleName) BC_FIELD(IconRegistration, functionName)
BC_FIELDS_END

BC_FIELDS(TextureCache)
    BC_FIELD(TextureCache, count) BC_FIELD(TextureCache, bucketCount) BC_FIELD(TextureCache, buckets)
BC_FIELDS_END

BC_FIELDS(TextureCacheNode)
    BC_FIELD(TextureCacheNode, key) BC_FIELD(TextureCacheNode, value) BC_FIELD(TextureCacheNode, next)
BC_FIELDS_END

BC_FIELDS(IconGroupNode)
    BC_FIELD(IconGroupNode, data) BC_FIELD(IconGroupNode, next) BC_FIELD(IconGroupNode, prev)
BC_FIELDS_END

BC_FIELDS(IconGroupBlock)
    BC_FIELD(IconGroupBlock, nextBlock) BC_FIELD(IconGroupBlock, nodes)
BC_FIELDS_END

BC_FIELDS(IconGroupList)
    BC_FIELD(IconGroupList, count) BC_FIELD(IconGroupList, head) BC_FIELD(IconGroupList, tail)
    BC_FIELD(IconGroupList, freeNodes) BC_FIELD(IconGroupList, blocks) BC_FIELD(IconGroupList, blockSize)
BC_FIELDS_END

BC_FIELDS(IconOtherNode)
    BC_FIELD(IconOtherNode, data) BC_FIELD(IconOtherNode, next) BC_FIELD(IconOtherNode, prev)
BC_FIELDS_END

BC_FIELDS(IconOtherBlock)
    BC_FIELD(IconOtherBlock, nextBlock) BC_FIELD(IconOtherBlock, nodes)
BC_FIELDS_END

BC_FIELDS(IconOtherList)
    BC_FIELD(IconOtherList, count) BC_FIELD(IconOtherList, head) BC_FIELD(IconOtherList, tail)
    BC_FIELD(IconOtherList, freeNodes) BC_FIELD(IconOtherList, blocks) BC_FIELD(IconOtherList, blockSize)
BC_FIELDS_END

BC_FIELDS(PropertyCellObject)
    BC_FIELD(PropertyCellObject, value)
BC_FIELDS_END

BC_FIELDS(CameraCellObject)
    BC_FIELD(CameraCellObject, camera)
BC_FIELDS_END

BC_FIELDS(NiTextureProperty)
    BC_FIELD(NiTextureProperty, refCount) BC_FIELD(NiTextureProperty, texture) BC_FIELD(NiTextureProperty, name)
    BC_FIELD(NiTextureProperty, extraData) BC_FIELD(NiTextureProperty, controller) BC_FIELD(NiTextureProperty, flags)
    {"serial", static_cast<std::uint32_t>(offsetof(TestTextureProperty, serial))},
BC_FIELDS_END

BC_FIELDS(NiImage)
    BC_FIELD(NiImage, refCount) BC_FIELD(NiImage, width) BC_FIELD(NiImage, height) BC_FIELD(NiImage, name)
    BC_FIELD(NiImage, rawImage) BC_FIELD(NiImage, mipLevels) BC_FIELD(NiImage, id) BC_FIELD(NiImage, reserved20)
    BC_FIELD(NiImage, flags) BC_FIELD(NiImage, mask) BC_FIELD(NiImage, scale)
    {"serial", static_cast<std::uint32_t>(offsetof(TestTexture, serial))},
BC_FIELDS_END

BC_FIELDS(NiCamera)
    BC_FIELD(NiCamera, refCount) BC_FIELD(NiCamera, renderer)
    {"serial", static_cast<std::uint32_t>(offsetof(NiCamera, reserved))},
BC_FIELDS_END

BC_FIELDS(NiD3DRender)
    BC_FIELD(NiD3DRender, textureManager)
    {"width", static_cast<std::uint32_t>(offsetof(TestRenderer, width))},
    {"height", static_cast<std::uint32_t>(offsetof(TestRenderer, height))},
BC_FIELDS_END

// The argument tuple of the wrapper tests: the test puts the objects that the Python arguments stand for into it.
struct ManagerArgs {
    IconManager* m;
    NiCamera* c;
};
struct GroupArgs {
    IconGroup* g;
};

BC_FIELDS(ManagerArgs)
    BC_FIELD(ManagerArgs, m) BC_FIELD(ManagerArgs, c)
BC_FIELDS_END

BC_FIELDS(GroupArgs)
    BC_FIELD(GroupArgs, g)
BC_FIELDS_END

extern "C" {
void bc_set_captured_double(double value);
void bc_set_captured_integer(std::int64_t value);
void bc_set_captured_pointer(void* value);
}

namespace {

constexpr int kLogCapacity = 256;
constexpr int kLogLine = 200;
char g_log[kLogCapacity][kLogLine];
int g_logCount = 0;
std::uint32_t g_serial = 0;

struct TextureEntry {
    char path[160];
    std::uint32_t width;
    std::uint32_t height;
};
constexpr int kTextureCapacity = 16;
TextureEntry g_textures[kTextureCapacity];
int g_textureCount = 0;

void LogLine(const char* format, ...)
{
    if (g_logCount >= kLogCapacity)
        return;
    va_list list;
    va_start(list, format);
    std::vsnprintf(g_log[g_logCount++], kLogLine, format, list);
    va_end(list);
}

NiTextureProperty* TestNewProperty()
{
    TestTextureProperty* property = new TestTextureProperty();
    property->refCount = 0;
    property->texture = nullptr;
    property->serial = ++g_serial;
    LogLine("new_property %u", property->serial);
    return property;
}

NiImage* TestCreateTexture(const char* path, bool flag)
{
    for (int i = 0; i < g_textureCount; ++i) {
        if (std::strcmp(g_textures[i].path, path) == 0) {
            TestTexture* texture = new TestTexture();
            texture->refCount = 0;
            texture->width = g_textures[i].width;
            texture->height = g_textures[i].height;
            texture->serial = ++g_serial;
            LogLine("create_texture %s %d %u", path, flag ? 1 : 0, texture->serial);
            return texture;
        }
    }
    LogLine("create_texture %s %d 0", path, flag ? 1 : 0);
    return nullptr;
}

void TestFlush() { LogLine("flush"); }

int TestCallModuleFunction(const char* module, const char* function)
{
    LogLine("module_call %s %s", module, function);
    return 0;
}

constexpr std::uint32_t kWrapperHandle = 0xBEEF0001;

PyWrapperObject* TestNewPythonWrapper(void*)
{
    LogLine("pywrapper");
    return reinterpret_cast<PyWrapperObject*>(static_cast<std::uintptr_t>(kWrapperHandle));
}

void InstallTestBoundary()
{
    g_iconBoundary.newTextureProperty = TestNewProperty;
    g_iconBoundary.createTexture = TestCreateTexture;
    g_iconBoundary.flushRenderQueue = TestFlush;
    g_iconBoundary.callModuleFunction = TestCallModuleFunction;
    g_iconBoundary.newPythonWrapper = TestNewPythonWrapper;
}

struct InstallBoundary {
    InstallBoundary() { InstallTestBoundary(); }
} g_installBoundary;

template <class T>
T* Blank()
{
    return static_cast<T*>(std::memset(operator_new(sizeof(T)), 0, sizeof(T)));
}

}  // namespace

void TestTextureProperty::DeleteThis()
{
    LogLine("delete property %u", serial);
    ::operator delete(this);
}
void TestTexture::DeleteThis()
{
    LogLine("delete texture %u", serial);
    ::operator delete(this);
}
void TestCamera::DeleteThis()
{
    LogLine("delete camera %u", reserved[0]);
    ::operator delete(this);
}

extern "C" {

// The state of a scenario: the log is empty, the serial counter starts at the value, no texture file exists and the
// working directory is the text.
void bc_icon_reset(std::uint32_t firstSerial, const char* workingDirectory)
{
    g_logCount = 0;
    g_serial = firstSerial;
    g_textureCount = 0;
    std::strncpy(g_workingDirectory, workingDirectory, sizeof(g_workingDirectory) - 1);
    g_workingDirectory[sizeof(g_workingDirectory) - 1] = 0;
    TGIconGroup::iterator = nullptr;
    TGObject::registry = nullptr;  // the trap log has the registry count 0, as the exe memory has
    InstallTestBoundary();
}

void bc_icon_add_texture(const char* path, std::uint32_t width, std::uint32_t height)
{
    if (g_textureCount >= kTextureCapacity)
        return;
    TextureEntry& entry = g_textures[g_textureCount++];
    std::strncpy(entry.path, path, sizeof(entry.path) - 1);
    entry.path[sizeof(entry.path) - 1] = 0;
    entry.width = width;
    entry.height = height;
}

// The log as text, one line for each call. Returns the length, or -1 when it does not fit.
int bc_icon_log_dump(char* out, int capacity)
{
    int used = 0;
    for (int i = 0; i < g_logCount; ++i) {
        int length = static_cast<int>(std::strlen(g_log[i]));
        if (used + length + 2 > capacity)
            return -1;
        std::memcpy(out + used, g_log[i], static_cast<std::size_t>(length));
        used += length;
        out[used++] = '\n';
    }
    out[used] = 0;
    return used;
}

// The default functions of the boundary table. Each one stops at the exe address that it stands for.
void bc_icon_call_default_boundary(std::int32_t which)
{
    IconBoundary saved = g_iconBoundary;
    ResetIconBoundary();
    switch (which) {
    case 0: g_iconBoundary.newTextureProperty(); break;
    case 1: g_iconBoundary.createTexture("a", false); break;
    case 2: g_iconBoundary.callModuleFunction("a", "b"); break;
    case 3: g_iconBoundary.newPythonWrapper(nullptr); break;
    }
    g_iconBoundary = saved;
}

// Blank objects for the field maps.
IconManager* bc_new_IconManager() { return Blank<IconManager>(); }
IconGroup* bc_new_IconGroup() { return Blank<IconGroup>(); }
IconLocation* bc_new_IconLocation() { return Blank<IconLocation>(); }
IconLocationNode* bc_new_IconLocationNode() { return Blank<IconLocationNode>(); }
IconLocationTable* bc_new_IconLocationTable() { return Blank<IconLocationTable>(); }
IconTextureArray* bc_new_IconTextureArray() { return Blank<IconTextureArray>(); }
IconRegistration* bc_new_IconRegistration() { return Blank<IconRegistration>(); }
TextureCache* bc_new_TextureCache() { return Blank<TextureCache>(); }
TextureCacheNode* bc_new_TextureCacheNode() { return Blank<TextureCacheNode>(); }
IconGroupNode* bc_new_IconGroupNode() { return Blank<IconGroupNode>(); }
IconGroupBlock* bc_new_IconGroupBlock() { return Blank<IconGroupBlock>(); }
IconGroupList* bc_new_IconGroupList() { return Blank<IconGroupList>(); }
IconOtherNode* bc_new_IconOtherNode() { return Blank<IconOtherNode>(); }
IconOtherBlock* bc_new_IconOtherBlock() { return Blank<IconOtherBlock>(); }
IconOtherList* bc_new_IconOtherList() { return Blank<IconOtherList>(); }
PropertyCellObject* bc_new_PropertyCellObject() { return Blank<PropertyCellObject>(); }
CameraCellObject* bc_new_CameraCellObject() { return Blank<CameraCellObject>(); }
ManagerArgs* bc_new_ManagerArgs() { return Blank<ManagerArgs>(); }
GroupArgs* bc_new_GroupArgs() { return Blank<GroupArgs>(); }
// The builder stands for an exe object that exists already, so the count of the objects keeps its value.
NiTextureProperty* bc_new_NiTextureProperty()
{
    NiTextureProperty* property = new TestTextureProperty();
    --ni::g_objectCount;
    return property;
}
// The builder stands for an exe object that exists already: the counters keep their values and the object is blank.
NiImage* bc_new_NiImage()
{
    TestTexture* image = new TestTexture();
    --ni::g_objectCount;
    --ni::g_imageCounter;
    image->id = 0;
    image->scale = 0;
    return image;
}
NiCamera* bc_new_NiCamera() { return new TestCamera(); }
NiD3DRender* bc_new_NiD3DRender() { return new TestRenderer(); }

// The global manager, built again for each scenario: the constructor runs and the test sets the members.
IconManager* bc_init_global_IconManager()
{
    TGIconManager::ConstructGlobal();
    return TGIconManager::Global();
}

// Group ------------------------------------------------------------------------------------------------------------

IconGroup* bc_TGIconGroup_TGIconGroup(IconGroup* self, const char* name) { return ::new (self) IconGroup(name); }
void bc_TGIconGroup_DestructorBody(IconGroup* self) { self->IconGroup::~IconGroup(); }
IconGroup* bc_TGIconGroup_DeletingDestructor(IconGroup* self, std::uint32_t flags)
{
    self->IconGroup::~IconGroup();
    if (flags & 1)
        operator_delete(self);
    return self;
}
void bc_TGIconGroup_Reset(IconGroup* self) { self->Reset(); }
void bc_TGIconGroup_SetName(IconGroup* self, const char* name) { self->SetName(name); }
std::int32_t bc_TGIconGroup_LoadIconTexture(IconGroup* self, const char* file, std::uint32_t flag)
{
    return self->LoadIconTexture(file, (flag & 0xff) != 0);
}
void bc_TGIconGroup_SetIconLocation(IconGroup* self, std::int32_t number, std::int32_t handle, float x, float y,
                                    float width, float height, std::int32_t rotation, std::int32_t mirror)
{
    self->IconGroup::SetIconLocation(number, handle, x, y, width, height, rotation, mirror);
}
void bc_TGIconGroup_RotateIconLocation(IconGroup* self, std::int32_t number, std::int32_t rotation)
{
    self->RotateIconLocation(number, rotation);
}
void bc_TGIconGroup_MirrorIconLocation(IconGroup* self, std::int32_t number, std::int32_t mirror)
{
    self->MirrorIconLocation(number, mirror);
}
double bc_TGIconGroup_GetIconPixelWidth(IconGroup* self, std::int32_t number) { return self->GetIconPixelWidth(number); }
double bc_TGIconGroup_GetIconPixelHeight(IconGroup* self, std::int32_t number) { return self->GetIconPixelHeight(number); }
double bc_TGIconGroup_GetIconScreenWidth(IconGroup* self, std::int32_t number) { return self->GetIconScreenWidth(number); }
double bc_TGIconGroup_GetIconScreenHeight(IconGroup* self, std::int32_t number) { return self->GetIconScreenHeight(number); }
std::uint8_t bc_TGIconGroup_IsNumValid(IconGroup* self, std::int32_t number) { return self->IsNumValid(number); }
void bc_TGIconGroup_GoToFirstPos(IconGroup* self) { self->GoToFirstPos(); }
std::int32_t bc_TGIconGroup_GetNext(IconGroup* self) { return self->GetNext(); }
std::uint32_t bc_TGIconGroup_GetPyWrapper(IconGroup* self)
{
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self->GetPyWrapper()));
}

void bc_TGIconGroup_setLock(IconGroup* self, std::int32_t value) { self->lockCount = value; }

// The slots that are not reconstructed. Each call must reach the trap of its exe address.
void bc_TGIconGroup_CallTrap(IconGroup*, std::int32_t which)
{
    switch (which) {
    case 0: {
        TGIconGroup group("g");
        group.SaveToStream(nullptr);
        break;
    }
    case 1: {
        TGIconGroup group("g");
        group.LoadFromStream(nullptr);
        break;
    }
    case 2: {
        TGIconLocation location;
        location.SaveToStream(nullptr);
        location.LoadFromStream(nullptr);
        break;
    }
    case 3: {
        TGIconRegistration record("a", "b", "c");
        record.SaveToStream(nullptr);
        record.LoadFromStream(nullptr);
        break;
    }
    }
}

// The iterator of the groups, as the number of its node or -1, and a way to set it.
std::int32_t bc_TGIconGroup_iteratorKey() { return TGIconGroup::iterator ? static_cast<std::int32_t>(TGIconGroup::iterator->key) : -1; }
void bc_TGIconGroup_setIterator(IconGroup* group, std::int32_t key)
{
    TGIconGroup::iterator = key < 0 ? nullptr : group->locations.FindNode(static_cast<std::uint32_t>(key));
}

// The location table (member of the group) --------------------------------------------------------------------------

std::uint32_t bc_IconLocationTable_Hash(IconLocationTable* self, std::uint32_t key) { return self->Hash(key); }
std::uint8_t bc_IconLocationTable_KeysEqual(IconLocationTable* self, std::uint32_t a, std::uint32_t b)
{
    return self->KeysEqual(a, b);
}
void bc_IconLocationTable_SetNode(IconLocationTable* self, IconLocationNode* node, std::uint32_t key, IconLocation* value)
{
    self->SetNode(node, key, value);
}
void bc_IconLocationTable_ClearNode(IconLocationTable* self, IconLocationNode* node) { self->ClearNode(node); }
IconLocationTable* bc_IconLocationTable_DeletingDestructor(IconLocationTable* self, std::uint32_t flags)
{
    self->IconLocationTable::~IconLocationTable();
    if (flags & 1)
        operator_delete(self);
    return self;
}

// The texture array (member of the group) ---------------------------------------------------------------------------

void bc_IconTextureArray_AssignElement(PropertyCellObject* element, PropertyCellObject* source)
{
    TGIconTextureArray::AssignElement(&element->value, &source->value);
}
void bc_IconTextureArray_Add(IconTextureArray* self, NiTextureProperty* owned) { self->Add(owned); }
std::uint32_t bc_IconTextureArray_SetAtGrow(IconTextureArray* self, std::uint32_t index, NiTextureProperty* owned)
{
    return self->SetAtGrow(index, owned);
}
void bc_IconTextureArray_StoreAt(IconTextureArray* self, std::uint32_t index, NiTextureProperty* owned)
{
    self->StoreAt(index, owned);
}
void bc_IconTextureArray_GetAt(IconTextureArray* self, PropertyCellObject* out, std::uint32_t index)
{
    self->GetAt(&out->value, index);
}
void bc_IconTextureArray_RemoveAt(IconTextureArray* self, PropertyCellObject* out, std::uint32_t index)
{
    self->RemoveAt(&out->value, index);
}
void bc_IconTextureArray_DestructorBody(IconTextureArray* self) { self->IconTextureArray::~IconTextureArray(); }
IconTextureArray* bc_IconTextureArray_DeletingDestructor(IconTextureArray* self, std::uint32_t flags)
{
    self->IconTextureArray::~IconTextureArray();
    if (flags & 1)
        operator_delete(self);
    return self;
}

// The texture cache (member of the manager) -------------------------------------------------------------------------

std::uint32_t bc_TextureCache_Hash(TextureCache* self, const char* key) { return self->Hash(key); }
std::uint8_t bc_TextureCache_KeysEqual(TextureCache* self, const char* a, const char* b) { return self->KeysEqual(a, b); }
void bc_TextureCache_SetNode(TextureCache* self, TextureCacheNode* node, const char* key, NiImage* owned)
{
    self->SetNode(node, key, owned);
}
void bc_TextureCache_ClearNode(TextureCache* self, TextureCacheNode* node) { self->ClearNode(node); }
void bc_TextureCache_Insert(TextureCache* self, const char* key, NiImage* owned) { self->Insert(key, owned); }
TextureCache* bc_TextureCache_DeletingDestructor(TextureCache* self, std::uint32_t flags)
{
    self->Destroy();
    if (flags & 1)
        operator_delete(self);
    return self;
}

// Registration -----------------------------------------------------------------------------------------------------

IconRegistration* bc_TGIconRegistration_TGIconRegistration(IconRegistration* self, const char* group, const char* module,
                                                            const char* function)
{
    return ::new (self) IconRegistration(group, module, function);
}
void bc_TGIconRegistration_Destructor(IconRegistration* self) { self->IconRegistration::~IconRegistration(); }

// Manager ----------------------------------------------------------------------------------------------------------

IconManager* bc_TGIconManager_TGIconManager(IconManager* self) { return ::new (self) IconManager(); }
void bc_TGIconManager_DestructorBody(IconManager* self) { self->IconManager::~IconManager(); }
void bc_TGIconManager_RegisterIconGroup(IconManager* self, const char* name, const char* module, const char* function)
{
    self->RegisterIconGroup(name, module, function);
}
IconGroup* bc_TGIconManager_CreateIconGroup(IconManager* self, const char* name) { return self->CreateIconGroup(name); }
void bc_TGIconManager_AddIconGroup(IconManager* self, IconGroup* group) { self->AddIconGroup(group); }
std::uint8_t bc_TGIconManager_DeleteIconGroup(IconManager* self, const char* name) { return self->DeleteIconGroup(name); }
IconGroup* bc_TGIconManager_GetIconGroup(IconManager* self, const char* name) { return self->GetIconGroup(name); }
IconGroup* bc_TGIconManager_FindLoadedGroup(IconManager* self, const char* name) { return self->FindLoadedGroup(name); }
void bc_TGIconManager_Purge(IconManager* self) { self->Purge(); }
void bc_TGIconManager_SetPixelCenterAdj(IconManager* self, float adjustment) { self->SetPixelCenterAdj(adjustment); }
void bc_TGIconManager_SetDisplayInfo(IconManager* self, NiCamera* camera, float scale) { self->SetDisplayInfo(camera, scale); }
void bc_TGIconManager_SetCamera(IconManager* self, NiCamera* camera) { self->SetCamera(camera); }
CameraCellObject* bc_TGIconManager_GetCameraPtr(IconManager* self, CameraCellObject* out)
{
    self->GetCameraPtr(reinterpret_cast<NiCameraPtr*>(out));
    return out;
}
void bc_TGIconManager_Draw(IconManager* self) { self->Draw(); }
TextureCache* bc_TGIconManager_GetTextureCache(IconManager* self) { return self->GetTextureCache(); }
void bc_TGIconManager_InitGlobal() { TGIconManager::InitGlobal(); }
void bc_TGIconManager_ConstructGlobal() { TGIconManager::ConstructGlobal(); }
void bc_TGIconManager_RegisterGlobalCleanup() { TGIconManager::RegisterGlobalCleanup(); }
void bc_TGIconManager_DestructGlobal() { TGIconManager::DestructGlobal(); }

// The Appc wrappers that have engine code of their own, as native functions. Each takes the unused first argument and
// the argument tuple, keeps the Python value that the wrapper makes (as the test stubs of the interpreter do) and returns
// the handle of the result.
constexpr std::uint32_t kFloatHandle = 0xF1000000, kIntHandle = 0x1A000000, kStringHandle = 0x5C000000,
                        kPointerHandle = 0xB1000000, kNone = 0x95DD20;
static char g_capturedText[256];
static int g_capturedTextKind = 0;  // 0 none, 1 text

std::uint32_t bc_Appc_TGIconManager_GetScreenWidth(std::uint32_t, ManagerArgs* a)
{
    bc_set_captured_double(static_cast<double>(a->m->GetScreenWidth()));
    return kFloatHandle;
}
std::uint32_t bc_Appc_TGIconManager_GetScreenHeight(std::uint32_t, ManagerArgs* a)
{
    bc_set_captured_double(static_cast<double>(a->m->GetScreenHeight()));
    return kFloatHandle;
}
std::uint32_t bc_Appc_TGIconManager_GetScale(std::uint32_t, ManagerArgs* a)
{
    bc_set_captured_double(static_cast<double>(a->m->GetScale()));
    return kFloatHandle;
}
std::uint32_t bc_Appc_TGIconManager_GetPixelCenterAdj(std::uint32_t, ManagerArgs* a)
{
    bc_set_captured_double(static_cast<double>(a->m->GetPixelCenterAdj()));
    return kFloatHandle;
}
std::uint32_t bc_Appc_TGIconManager_SetScreenWidth(std::uint32_t, ManagerArgs* a, float value)
{
    a->m->SetScreenWidth(value);
    return kNone;
}
std::uint32_t bc_Appc_TGIconManager_SetScreenHeight(std::uint32_t, ManagerArgs* a, float value)
{
    a->m->SetScreenHeight(value);
    return kNone;
}
std::uint32_t bc_Appc_TGIconManager_SetScale(std::uint32_t, ManagerArgs* a, float value)
{
    a->m->SetScale(value);
    return kNone;
}
std::uint32_t bc_Appc_TGIconManager_SetCamera(std::uint32_t, ManagerArgs* a)
{
    a->m->SetCamera(a->c);
    return kNone;
}
std::uint32_t bc_Appc_TGIconManager_GetCamera(std::uint32_t, ManagerArgs* a)
{
    NiCameraPtr* result = new NiCameraPtr();
    a->m->GetCameraPtr(result);
    bc_set_captured_pointer(result);
    return kPointerHandle;
}
std::uint32_t bc_Appc_TGIconGroup_delete(std::uint32_t, GroupArgs* a)
{
    delete a->g;
    return kNone;
}
std::uint32_t bc_Appc_TGIconGroup_GetName(std::uint32_t, GroupArgs* a)
{
    const char* name = a->g->GetName();
    if (name == nullptr) {
        g_capturedTextKind = 0;
        return kNone;
    }
    g_capturedTextKind = 1;
    std::strncpy(g_capturedText, name, sizeof(g_capturedText) - 1);
    g_capturedText[sizeof(g_capturedText) - 1] = 0;
    return kStringHandle;
}
std::uint32_t bc_Appc_TGIconGroup_GetCount(std::uint32_t, GroupArgs* a)
{
    bc_set_captured_integer(a->g->GetCount());
    return kIntHandle;
}
std::uint32_t bc_Appc_TGIconGroup_GetPyWrapper(std::uint32_t, GroupArgs* a)
{
    bc_set_captured_pointer(a->g->GetPyWrapper());
    return kPointerHandle;
}
int bc_icon_captured_text(char* out, int capacity)
{
    if (g_capturedTextKind == 0)
        return -1;
    std::strncpy(out, g_capturedText, static_cast<std::size_t>(capacity) - 1);
    out[capacity - 1] = 0;
    return static_cast<int>(std::strlen(out));
}
void bc_icon_captured_text_clear() { g_capturedTextKind = 0; }

}  // extern "C"
