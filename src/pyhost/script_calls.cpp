// The call layer between the application and the scripts of stbc.exe.

#include "script_calls.h"

#include "appc_module.h"
#include "IconBoundary.h"
#include "Trap.h"

#include <cstdarg>
#include <cstring>
#include <list>
#include <string>

extern "C" {
#include "securelevel.h"
}

namespace {

// The flags at 0x99add4, 0x99add5 and 0x99add6. The exe sets them from engine code. They are 0 at start-up.
bool g_reloadModules = false;
bool g_debugCalls = false;
bool g_traceErrors = false;

const char kMainModule[] = "__main__";  // 0x8d9c3c
const char kAppModule[] = "App";        // 0x8d9c80

// 0x6f7a00
const char *ModuleNameOrMain(const char *name)
{
    return name != nullptr ? name : kMainModule;
}

struct ScriptHelper {
    PyMethodDef method;
    std::string className;
};

std::list<ScriptHelper *> g_pendingHelpers;     // 0x99ad3c
std::list<PyMethodDef *> g_registeredHelpers;   // 0x99acec

PyObject *LoadedModule(const char *name)
{
    return PyDict_GetItemString(PyImport_GetModuleDict(), const_cast<char *>(name));
}

// 0x6f9eb0
bool InstallHelper(PyMethodDef *method, const char *className)
{
    PyObject *app = LoadedModule(kAppModule);
    if (app == nullptr)
        return false;
    PyObject *cls = PyObject_GetAttrString(app, const_cast<char *>(className));
    if (cls == nullptr)
        return false;
    PyObject *function = PyCFunction_New(method, nullptr);
    if (function == nullptr) {
        Py_DECREF(cls);
        return false;
    }
    if (PyObject_SetAttrString(cls, method->ml_name, function) == -1) {
        Py_DECREF(cls);
        return false;
    }
    g_registeredHelpers.push_back(method);
    Py_DECREF(cls);
    return true;
}

// 0x6f9db0
void FlushHelpers()
{
    for (auto it = g_pendingHelpers.begin(); it != g_pendingHelpers.end();) {
        ScriptHelper *helper = *it;
        if (InstallHelper(&helper->method, helper->className.c_str())) {
            it = g_pendingHelpers.erase(it);
            delete helper;
        } else {
            ++it;
        }
    }
}

}  // namespace

// 0x6f9bc0
bool bc_RegisterScriptHelper(PyCFunction code, const char *className, const char *methodName)
{
    auto *helper = new ScriptHelper;
    helper->method.ml_name = strdup(methodName);
    helper->method.ml_meth = code;
    helper->method.ml_flags = 1;
    helper->method.ml_doc = nullptr;
    helper->className = className;
    if (Py_IsInitialized() && LoadedModule(kAppModule) != nullptr)
        return InstallHelper(&helper->method, helper->className.c_str());
    g_pendingHelpers.push_back(helper);
    return true;
}

// 0x6f9b80
int bc_CheckScriptError()
{
    if (PyErr_Occurred() == nullptr)
        return 0;
    if (g_traceErrors) {
        if (!g_debugCalls) {
            BC_NOT_RECONSTRUCTED(0x6f9470);
            PyErr_Clear();
            return 1;
        }
    } else if (!g_debugCalls) {
        PyErr_Print();
    }
    PyErr_Clear();
    return 1;
}

// 0x6f7d90
PyObject *bc_ImportScriptModule(const char *name, bool reload)
{
    Py_SetSecurelevel(1);
    name = ModuleNameOrMain(name);
    PyObject *module = LoadedModule(name);
    if (std::strcmp(name, kMainModule) == 0) {
        module = PyImport_AddModule(const_cast<char *>(name));
        if (bc_CheckScriptError() == 1) {
            Py_SetSecurelevel(0);
            return nullptr;
        }
        Py_SetSecurelevel(0);
        return module;
    }
    if (module != nullptr) {
        PyObject *dict = PyModule_GetDict(module);
        if (PyDict_GetItemString(dict, const_cast<char *>("__dummy__")) != nullptr) {
            if (bc_CheckScriptError() == 1) {
                Py_SetSecurelevel(0);
                return nullptr;
            }
            Py_SetSecurelevel(0);
            return module;
        }
    }
    if ((reload || g_reloadModules) && module != nullptr && module->ob_type == &PyModule_Type) {
        PyObject *reloaded = PyImport_ReloadModule(module);
        if (reloaded != nullptr) {
            Py_DECREF(reloaded);
        }
        if (bc_CheckScriptError() == 1) {
            Py_SetSecurelevel(0);
            return nullptr;
        }
        Py_SetSecurelevel(0);
        return reloaded;
    }
    module = PyImport_ImportModule(const_cast<char *>(name));
    if (module != nullptr) {
        if (std::strcmp(name, kAppModule) == 0)
            FlushHelpers();
        Py_DECREF(module);
    }
    if (bc_CheckScriptError() == 1) {
        Py_SetSecurelevel(0);
        return nullptr;
    }
    Py_SetSecurelevel(0);
    return module;
}

// 0x6f8490
PyObject *bc_GetModuleAttribute(const char *module, char *attribute)
{
    Py_SetSecurelevel(1);
    PyObject *imported = bc_ImportScriptModule(ModuleNameOrMain(module), false);
    if (bc_CheckScriptError() == 1 || imported == nullptr) {
        Py_SetSecurelevel(0);
        return nullptr;
    }
    PyObject *value = PyObject_GetAttrString(imported, attribute);
    Py_SetSecurelevel(0);
    return value;
}

// 0x6f8580
int bc_ReceiveResult(PyObject *result, const char *format, void *output)
{
    if (result == nullptr)
        return -1;
    if (output == nullptr) {
        Py_DECREF(result);
        return 0;
    }
    if (PyArg_Parse(result, const_cast<char *>(format), output) == 0) {
        Py_DECREF(result);
        return -1;
    }
    if (bc_CheckScriptError() == 1)
        return -1;
    if (std::strcmp(format, "O") != 0) {
        Py_DECREF(result);
    }
    return 0;
}

// 0x6f8c00
int bc_CallScriptFunction(PyObject *callable, const char *format, void *output, PyObject *args, bool debugModule,
                          bool releaseCallable)
{
    Py_SetSecurelevel(1);
    PyObject *result;
    if (!g_debugCalls || !debugModule) {
        result = PyEval_CallObject(callable, args);
    } else {
        BC_NOT_RECONSTRUCTED(0x6f89e0);
        result = nullptr;
    }
    if (releaseCallable) {
        Py_DECREF(callable);
    }
    if (args != nullptr) {
        Py_DECREF(args);
    }
    int received = bc_ReceiveResult(result, format, output);
    if (bc_CheckScriptError() == 1) {
        Py_SetSecurelevel(0);
        return -1;
    }
    Py_SetSecurelevel(0);
    return received;
}

// 0x6f8ab0
int bc_CallModuleFunction(const char *module, char *function, const char *resultFormat, void *output,
                          const char *argumentFormat, ...)
{
    Py_SetSecurelevel(1);
    PyObject *callable = bc_GetModuleAttribute(module, function);
    if (callable == nullptr || bc_CheckScriptError() == 1) {
        Py_SetSecurelevel(0);
        return -1;
    }
    PyObject *args = nullptr;
    if (argumentFormat != nullptr && *argumentFormat != '\0') {
        va_list list;
        va_start(list, argumentFormat);
        args = Py_VaBuildValue(const_cast<char *>(argumentFormat), list);
        va_end(list);
        if (args == nullptr || bc_CheckScriptError() == 1) {
            Py_DECREF(callable);
            Py_SetSecurelevel(0);
            return -1;
        }
    } else if (bc_CheckScriptError() == 1) {
        Py_SetSecurelevel(0);
        return -1;
    }
    bool debugModule = std::strcmp(module, "TGdb") == 0;
    int result = bc_CallScriptFunction(callable, resultFormat, output, args, debugModule, true);
    Py_SetSecurelevel(0);
    return result;
}

void AppcRegisterScriptHelpers()
{
    for (const AppcHelper *helper = appc_script_helpers; helper->className != nullptr; ++helper)
        bc_RegisterScriptHelper(helper->code, helper->className, helper->methodName);
}

namespace {

// The call of the icon manager into the scripts (0x72a1c0): the function of the registration runs with no result and
// no arguments.
int CallIconModuleFunction(const char *module, const char *function)
{
    // A host that swaps the module and the function must make the checks of the font tests fail.
    const char *defect = std::getenv("BC_PYHOST_DEFECT");
    if (defect != nullptr && std::strcmp(defect, "swap_script_texts") == 0)
        std::swap(module, function);
    return bc_CallModuleFunction(module, const_cast<char *>(function), nullptr, nullptr, nullptr);
}

struct InstallScriptHooks {
    InstallScriptHooks() { g_iconBoundary.callModuleFunction = CallIconModuleFunction; }
} g_installScriptHooks;

}  // namespace
