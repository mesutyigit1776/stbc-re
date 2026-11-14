// The Appc module: initialisation and the traps for the parts that are not reconstructed.

#include "appc_module.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

char g_lastTrap[256];
bool g_trapsReturnNone = false;
std::vector<std::string> g_trapEvents;

// Returns true when the trap raised an exception.
bool RecordTrap(const char *text)
{
    std::strncpy(g_lastTrap, text, sizeof g_lastTrap - 1);
    g_trapEvents.push_back(text);
    if (g_trapsReturnNone)
        return false;
    PyErr_SetString(PyExc_RuntimeError, const_cast<char *>(text));
    return true;
}

PyObject *TrapResult(bool raised)
{
    if (raised)
        return nullptr;
    Py_INCREF(Py_None);
    return Py_None;
}

}  // namespace

const char *AppcLastTrapText()
{
    return g_lastTrap;
}

void AppcSetTrapsReturnNone(bool enabled)
{
    g_trapsReturnNone = enabled;
}

unsigned AppcTrapEventCount()
{
    return static_cast<unsigned>(g_trapEvents.size());
}

const char *AppcTrapEventText(unsigned index)
{
    return g_trapEvents[index].c_str();
}

// 0x65a250
extern "C" void initAppc()
{
    PyObject *link = SWIG_newvarlink();
    PyObject *module = Py_InitModule4(const_cast<char *>("Appc"), appc_method_table, nullptr, nullptr, PYTHON_API_VERSION);
    PyObject *dict = PyModule_GetDict(module);
    AppcRegisterTypes();
    PyDict_SetItemString(dict, const_cast<char *>("globals"), link);
    for (const AppcVariable *variable = appc_variables; variable->name != nullptr; ++variable)
        SWIG_addvarlink(link, variable->name, variable->get, variable->set);
    SWIG_InstallConstants(dict, appc_constants);
}

PyObject *AppcTrapHelper(const char *className, const char *methodName, unsigned address)
{
    char text[256];
    std::snprintf(text, sizeof text, "App.%s.%s is not reconstructed (exe function 0x%x)", className, methodName, address);
    return TrapResult(RecordTrap(text));
}

PyObject *AppcTrapMethod(const char *name, unsigned address)
{
    char text[256];
    std::snprintf(text, sizeof text, "Appc.%s is not reconstructed (exe wrapper 0x%x)", name, address);
    return TrapResult(RecordTrap(text));
}

namespace {

struct AppcTrapObject {
    PyObject_HEAD
    const char *name;
    unsigned address;
};

bool SetVariableTrap(const char *name, const char *kind, unsigned address)
{
    char text[256];
    std::snprintf(text, sizeof text, "Appc.globals.%s is not reconstructed (exe %s 0x%x)", name, kind, address);
    return RecordTrap(text);
}

PyObject *TrapObjectGetattr(AppcTrapObject *object, char *)
{
    return TrapResult(SetVariableTrap(object->name, "getter", object->address));
}

PyObject *TrapObjectRepr(AppcTrapObject *object)
{
    char text[256];
    std::snprintf(text, sizeof text, "<Appc.globals.%s not reconstructed (exe getter 0x%x)>", object->name, object->address);
    return PyString_FromString(text);
}

void TrapObjectDealloc(AppcTrapObject *object)
{
    PyMem_DEL(object);
}

PyTypeObject g_trapObjectType;

void InitTrapObjectType()
{
    g_trapObjectType.ob_refcnt = 1;
    g_trapObjectType.ob_type = &PyType_Type;
    g_trapObjectType.tp_name = const_cast<char *>("appc_trap");
    g_trapObjectType.tp_basicsize = sizeof(AppcTrapObject);
    g_trapObjectType.tp_dealloc = reinterpret_cast<destructor>(TrapObjectDealloc);
    g_trapObjectType.tp_getattr = reinterpret_cast<getattrfunc>(TrapObjectGetattr);
    g_trapObjectType.tp_repr = reinterpret_cast<reprfunc>(TrapObjectRepr);
}

}  // namespace

PyObject *AppcTrapValue(const char *name, unsigned address)
{
    InitTrapObjectType();
    AppcTrapObject *object = PyObject_NEW(AppcTrapObject, &g_trapObjectType);
    if (object == nullptr)
        return nullptr;
    object->name = name;
    object->address = address;
    return reinterpret_cast<PyObject *>(object);
}

int AppcTrapVariable(const char *name, const char *kind, unsigned address)
{
    return SetVariableTrap(name, kind, address) ? 1 : 0;
}
