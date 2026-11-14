// Checks of the Appc module in the Python 1.5.2 host.
// Usage: bc_appc_test SCRIPT.py [ARGUMENT...]
// The program starts the host like bc_pyhost, adds the module bc_test and runs the script.
// The script reports each check with bc_test.report(name, ok).

#include "Crt.h"
#include "appc_module.h"
#include "startup.h"

#include "Python.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int g_failures = 0;
int g_checks = 0;

PyObject *Report(PyObject *, PyObject *args)
{
    char *name;
    int ok;
    if (!PyArg_ParseTuple(args, const_cast<char *>("si"), &name, &ok))
        return nullptr;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    ++g_checks;
    if (!ok)
        ++g_failures;
    Py_INCREF(Py_None);
    return Py_None;
}

PyObject *NameList(const char *const *names, unsigned count)
{
    PyObject *list = PyList_New(0);
    for (unsigned i = 0; i < count && names[i] != nullptr; ++i) {
        PyObject *name = PyString_FromString(const_cast<char *>(names[i]));
        PyList_Append(list, name);
        Py_DECREF(name);
    }
    return list;
}

// The names of the method table in table order.
PyObject *MethodNames(PyObject *, PyObject *)
{
    PyObject *list = PyList_New(0);
    for (const PyMethodDef *method = appc_method_table; method->ml_name != nullptr; ++method) {
        PyObject *name = PyString_FromString(method->ml_name);
        PyList_Append(list, name);
        Py_DECREF(name);
    }
    return list;
}

// The names of the methods that are real functions.
PyObject *RealNames(PyObject *, PyObject *)
{
    return NameList(appc_real_methods, 100000);
}

// The names of the module attributes in the order of the table.
PyObject *VariableNames(PyObject *, PyObject *)
{
    PyObject *list = PyList_New(0);
    for (const AppcVariable *variable = appc_variables; variable->name != nullptr; ++variable) {
        PyObject *name = PyString_FromString(const_cast<char *>(variable->name));
        PyList_Append(list, name);
        Py_DECREF(name);
    }
    return list;
}

PyMethodDef g_testMethods[] = {
    { const_cast<char *>("report"), Report, 1, nullptr },
    { const_cast<char *>("method_names"), MethodNames, 1, nullptr },
    { const_cast<char *>("real_names"), RealNames, 1, nullptr },
    { const_cast<char *>("variable_names"), VariableNames, 1, nullptr },
    { nullptr, nullptr, 0, nullptr },
};

char kPrelude[] =
    "import bc_test\n"
    "def check(name, ok):\n"
    "    bc_test.report(name, ok)\n";

char *ReadAll(const char *path)
{
    std::FILE *file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::perror(path);
        std::exit(2);
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    char *text = static_cast<char *>(std::malloc(size + 1));
    std::fread(text, 1, size, file);
    text[size] = 0;
    std::fclose(file);
    return text;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s SCRIPT.py [ARGUMENT...]\n", argv[0]);
        return 2;
    }
    char *text = ReadAll(argv[1]);
    RunStaticInitializers();
    RunModuleInitializers();
    bc_RegisterBuiltinModules();
    Py_NoSiteFlag = 1;
    Py_Initialize();
    PySys_SetArgv(argc - 1, argv + 1);
    Py_InitModule(const_cast<char *>("bc_test"), g_testMethods);
    PyObject *globals = PyModule_GetDict(PyImport_AddModule(const_cast<char *>("__main__")));
    PyObject *result = PyRun_String(kPrelude, Py_file_input, globals, globals);
    if (result == nullptr) {
        PyErr_Print();
        return 2;
    }
    result = PyRun_String(text, Py_file_input, globals, globals);
    if (result == nullptr) {
        PyErr_Print();
        ++g_failures;
    }
    std::printf("%s: %d checks, %d failed\n", argv[1], g_checks, g_failures);
    return g_failures == 0 && g_checks > 0 ? 0 : 1;
}
