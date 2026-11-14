// Tools of the test host that do not exist in the exe: the compile oracle and the import gate.

#include "host_tools.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include "IconBoundary.h"
#include "ImageBoundary.h"
#include "ImageLoaders.h"
#include "appc_module.h"

#include "Python.h"
#include "compile.h"
#include "graminit.h"
#include "marshal.h"
#include "node.h"
#include "pythonrun.h"

extern "C" {
#include "securelevel.h"
}

namespace {

bool ReadFile(const std::string &path, std::string &bytes)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char buffer[65536];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0)
        bytes.append(buffer, count);
    std::fclose(file);
    return true;
}

// The text mode of the Windows CRT: CR LF is read as LF, and 0x1a ends the file.
std::string CrtTextMode(const std::string &bytes)
{
    std::string text;
    text.reserve(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) {
        char c = bytes[i];
        if (c == 0x1a)
            break;
        if (c == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n')
            continue;
        text.push_back(c);
    }
    return text;
}

void WriteRecord(const char *tag, const std::string &body)
{
    std::printf("%s %zu\n", tag, body.size());
    std::fwrite(body.data(), 1, body.size(), stdout);
    std::fflush(stdout);
}

std::string ExceptionText()
{
    PyObject *type, *value, *trace;
    PyErr_Fetch(&type, &value, &trace);
    std::string text = "unknown error";
    if (value != nullptr) {
        PyObject *str = PyObject_Str(value);
        if (str != nullptr) {
            text = PyString_AsString(str);
            Py_DECREF(str);
        }
    }
    PyErr_Clear();
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(trace);
    return text;
}

std::string CompileOne(const std::string &path, const std::string &codeName, bool crtTextMode, bool &ok)
{
    ok = false;
    std::string bytes;
    if (!ReadFile(path, bytes))
        return "cannot read " + path;
    if (crtTextMode)
        bytes = CrtTextMode(bytes);
    FILE *file = bytes.empty() ? std::tmpfile() : fmemopen(&bytes[0], bytes.size(), "r");
    if (file == nullptr)
        return "cannot open the text of " + path;
    node *tree = PyParser_SimpleParseFile(file, const_cast<char *>(codeName.c_str()), file_input);
    std::fclose(file);
    if (tree == nullptr)
        return ExceptionText();
    PyCodeObject *code = PyNode_Compile(tree, const_cast<char *>(codeName.c_str()));
    PyNode_Free(tree);
    if (code == nullptr)
        return ExceptionText();
    PyObject *marshalled = PyMarshal_WriteObjectToString(reinterpret_cast<PyObject *>(code));
    Py_DECREF(code);
    if (marshalled == nullptr)
        return ExceptionText();
    std::string result(PyString_AsString(marshalled), static_cast<size_t>(PyString_Size(marshalled)));
    Py_DECREF(marshalled);
    ok = true;
    return result;
}

std::string TypeName(PyObject *type)
{
    if (type == nullptr)
        return "";
    if (PyString_Check(type))
        return PyString_AsString(type);
    if (PyClass_Check(type)) {
        PyObject *name = reinterpret_cast<PyClassObject *>(type)->cl_name;
        if (name != nullptr && PyString_Check(name))
            return PyString_AsString(name);
    }
    return "?";
}

std::string OneLine(std::string text)
{
    for (char &c : text)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    return text;
}

}  // namespace

int bc_RunCompileOracle(bool crtTextMode)
{
    char line[8192];
    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        std::string row(line);
        while (!row.empty() && (row.back() == '\n' || row.back() == '\r'))
            row.pop_back();
        size_t tab = row.find('\t');
        std::string path = row.substr(0, tab);
        std::string codeName = tab == std::string::npos ? path : row.substr(tab + 1);
        bool ok;
        std::string result = CompileOne(path, codeName, crtTextMode, ok);
        WriteRecord(ok ? "OK" : "ERR", result);
    }
    return 0;
}

int bc_RunImportGate(const char *module, bool trapsReturnNone)
{
    AppcSetTrapsReturnNone(trapsReturnNone);
    unsigned before = AppcTrapEventCount();
    Py_SetSecurelevel(1);
    PyObject *imported = PyImport_ImportModule(const_cast<char *>(module));
    Py_SetSecurelevel(0);

    std::string result = "ok", trap, type, text;
    if (imported == nullptr) {
        PyObject *errorType, *value, *trace;
        PyErr_Fetch(&errorType, &value, &trace);
        type = TypeName(errorType);
        if (value != nullptr) {
            PyObject *str = PyObject_Str(value);
            if (str != nullptr) {
                text = PyString_AsString(str);
                Py_DECREF(str);
            }
        }
        PyErr_Clear();
        Py_XDECREF(errorType);
        Py_XDECREF(value);
        Py_XDECREF(trace);
        result = "error";
        if (type == "RuntimeError" && text.find(" is not reconstructed (exe ") != std::string::npos) {
            result = "trap";
            trap = text;
        }
    } else {
        Py_DECREF(imported);
    }
    std::printf("GATE\t%s\t%s\t%s\t%s\n", result.c_str(), OneLine(trap).c_str(), OneLine(type).c_str(),
                OneLine(text).c_str());
    for (unsigned i = before; i < AppcTrapEventCount(); ++i)
        std::printf("EVENT\t%s\n", OneLine(AppcTrapEventText(i)).c_str());
    std::fflush(stdout);
    return 0;
}

namespace {

struct HostTextureProperty final : NiTextureProperty {
    void DeleteThis() override { delete this; }
};

NiTextureProperty *NewHostProperty()
{
    HostTextureProperty *property = new HostTextureProperty();
    property->refCount = 0;
    property->texture = nullptr;
    return property;
}

// The file layer of the host: the stream reads a file of the game folder.
struct HostStream final : ImageStream {
    explicit HostStream(const char *path) : file(std::fopen(path, "rb")) {}
    ~HostStream() override
    {
        if (file != nullptr)
            std::fclose(file);
    }
    bool IsOpen() override { return file != nullptr; }
    void Read(void *destination, std::uint32_t count) override
    {
        if (file != nullptr && std::fread(destination, 1, count, file) != count)
            std::memset(destination, 0, count);
    }
    void Seek(std::int32_t offset, std::uint32_t) override { std::fseek(file, offset, SEEK_CUR); }
    FILE *file;
};

ImageStream *OpenHostStream(const char *path) { return new HostStream(path); }
void CloseHostStream(ImageStream *stream) { delete stream; }
bool HostLoadNif(const char *, NiRawImageData *) { return false; }
bool HostLoadSgi(ImageStream *, NiRawImageData *) { return false; }
bool HostLoadBmp(ImageStream *, NiRawImageData *) { return false; }

// The device of the host: a surface has no memory and every fill succeeds.
struct HostSurface final : DirectDrawSurface {
    std::uint32_t Release() override
    {
        delete this;
        return 0;
    }
    std::int32_t SetPalette(void *) override { return 0; }
};

void DestroyHostImage(NiDDImage *image)
{
    for (TGListNode *node = image->surfaces.head; node != nullptr; node = node->next) {
        ImageSurfaceRecord *record = static_cast<ImageSurfaceRecord *>(node->data);
        if (record->surface != nullptr)
            record->surface->Release();
        operator_delete(record);
    }
    image->surfaces.ReleaseBlocks();
    ni::g_imageList.Unlink(image);
    --ni::g_ddImageCount;
    operator_delete(image->name);
    ni::Release(image->rawImage);
    ::operator delete(image);
}
bool CreateHostSurface(NiDDImage *, DirectDrawDevice *, TextureFormat *, DirectDrawSurface **surface, std::uint32_t)
{
    *surface = new HostSurface();
    return true;
}
bool FillHostSurface(NiDDImage *, NiRawImageData *, TextureFormat *, DirectDrawSurface *) { return true; }
bool RefillHostSurface(NiDDImage *, TextureFormat *, DirectDrawSurface *, bool) { return true; }
bool HostStreamData(NiDDImage *, DirectDrawDevice *, TextureFormat **, std::uint32_t, DirectDrawSurface **, TextureFormat *,
                    TextureFormat **)
{
    return false;
}

// The renderer of the host: seven formats without a palette, one device and no drawing.
struct HostRenderer final : NiD3DRender {
    std::uint32_t GetWidth() override { return 640; }
    std::uint32_t GetHeight() override { return 480; }
};
struct HostDevice final : DirectDrawDevice {
    std::int32_t CreatePalette(std::uint32_t, const void *, void **, void *) override { return 0; }
};

void InstallHostRenderer()
{
    static TextureFormat formats[7];
    static HostDevice device;
    static HostRenderer renderer;
    static TextureManager manager;
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    for (std::uint32_t i = 0; i < 7; ++i) {
        formats[i].paletted = 0;
        formats[i].id = i + 1;
        manager.slots[i] = &formats[i];
    }
    manager.flags = 0;
    manager.device = &device;
    renderer.textureManager = &manager;
    ni::g_rendererList.Append(&renderer);
    ni::g_imageFactory = []() -> NiImage * { return NiDDImage::Create(); };
    g_imageDevice = {DestroyHostImage, CreateHostSurface, FillHostSurface, RefillHostSurface, HostStreamData};
    g_imageFiles = {OpenHostStream, CloseHostStream, HostLoadNif, HostLoadSgi, HostLoadBmp};
}

}  // namespace

// The planted defect of the test of the icon script: no file gives a texture.
NiImage *CreateNoTexture(const char *, bool) { return nullptr; }

void bc_InstallIconFileTextures()
{
    g_iconBoundary.newTextureProperty = NewHostProperty;
    InstallHostRenderer();
    const char *defect = std::getenv("BC_PYHOST_DEFECT");
    if (defect != nullptr && std::strcmp(defect, "no_textures") == 0)
        g_iconBoundary.createTexture = CreateNoTexture;
    char directory[sizeof(g_workingDirectory)];
    if (getcwd(directory, sizeof directory - 2) != nullptr) {
        std::strcat(directory, "/");
        std::strcpy(g_workingDirectory, directory);
    }
}

int bc_RunScriptFile(const char *path)
{
    FILE *file = std::fopen(path, "r");
    if (file == nullptr) {
        std::perror(path);
        return 1;
    }
    int result = PyRun_SimpleFile(file, const_cast<char *>(path));
    std::fclose(file);
    return result == 0 ? 0 : 1;
}
