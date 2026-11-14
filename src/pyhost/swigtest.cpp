// Checks of the SWIG runtime of the Appc module. The expected results come from the code of stbc.exe (E0136 to E0140).
// Usage: bc_swig_test

#include "swig_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(const char *name, bool ok)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    ++g_checks;
    if (!ok)
        ++g_failures;
}

void *DerivedToBase(void *pointer)
{
    return pointer;
}

swig_type_info g_baseTable[] = {
    { "_p_Base", nullptr, "Base *", nullptr, nullptr },
    { "_p_Base", nullptr, nullptr, nullptr, nullptr },
    { "_p_Derived", DerivedToBase, nullptr, nullptr, nullptr },
    { "_p_Other", nullptr, nullptr, nullptr, nullptr },
    { nullptr, nullptr, nullptr, nullptr, nullptr },
};

swig_type_info g_derivedTable[] = {
    { "_p_Derived", nullptr, "Derived *", nullptr, nullptr },
    { "_p_Derived", nullptr, nullptr, nullptr, nullptr },
    { nullptr, nullptr, nullptr, nullptr, nullptr },
};

swig_type_info g_duplicateTable[] = {
    { "_p_Base", nullptr, "Base *", nullptr, nullptr },
    { "_p_Extra", nullptr, nullptr, nullptr, nullptr },
    { nullptr, nullptr, nullptr, nullptr, nullptr },
};

int g_setCalls = 0;

PyObject *GetValue()
{
    return PyInt_FromLong(42);
}

int SetValue(PyObject *)
{
    ++g_setCalls;
    return 0;
}

std::string MakePtrText(void *pointer, swig_type_info *type)
{
    char buffer[128];
    SWIG_MakePtr(buffer, pointer, type);
    return buffer;
}

std::string ErrorText()
{
    PyObject *type = nullptr;
    PyObject *value = nullptr;
    PyObject *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    std::string text;
    if (value != nullptr && value->ob_type == &PyString_Type)
        text = PyString_AsString(value);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(trace);
    return text;
}

PyObject *PointerString(const char *text)
{
    return PyString_FromString(const_cast<char *>(text));
}

void TestPointerStrings(swig_type_info *base)
{
    Check("MakePtr: a null pointer is NULL", MakePtrText(nullptr, base) == "NULL");
    Check("MakePtr: lower case hex, no leading zero", MakePtrText(reinterpret_cast<void *>(0x80a1b20), base) == "_80a1b20_p_Base");
    Check("MakePtr: a host pointer of more than 32 bits",
          MakePtrText(reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x600003a40ull)), base) == "_600003a40_p_Base");
    Check("MakePtr: the pointer -1 gives the 32-bit text of the exe",
          MakePtrText(reinterpret_cast<void *>(static_cast<std::uintptr_t>(-1)), base) == "_ffffffff_p_Base");

    PyObject *none = SWIG_NewPointerObj(nullptr, base);
    Check("NewPointerObj: a null pointer is None", none == Py_None);
    Py_DECREF(none);
    PyObject *text = SWIG_NewPointerObj(reinterpret_cast<void *>(0x1234), base);
    Check("NewPointerObj: a string", text != nullptr && std::strcmp(PyString_AsString(text), "_1234_p_Base") == 0);
    Py_DECREF(text);

    void *pointer = reinterpret_cast<void *>(1);
    PyObject *round = PointerString("_600003a40_p_Base");
    Check("ConvertPtr: a host pointer of more than 32 bits",
          SWIG_ConvertPtr(round, &pointer, base, 1) == 0 && pointer == reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x600003a40ull)));
    Py_DECREF(round);

    PyObject *allOnes = PointerString("_ffffffff_p_Base");
    SWIG_ConvertPtr(allOnes, &pointer, base, 1);
    Check("ConvertPtr: ffffffff is the pointer -1", pointer == reinterpret_cast<void *>(static_cast<std::uintptr_t>(-1)));
    Py_DECREF(allOnes);

    pointer = reinterpret_cast<void *>(1);
    Check("ConvertPtr: None is a null pointer", SWIG_ConvertPtr(Py_None, &pointer, base, 1) == 0 && pointer == nullptr);
    pointer = reinterpret_cast<void *>(1);
    PyObject *nullText = PointerString("NULL");
    Check("ConvertPtr: the text NULL is a null pointer", SWIG_ConvertPtr(nullText, &pointer, base, 1) == 0 && pointer == nullptr);
    Py_DECREF(nullText);
    pointer = reinterpret_cast<void *>(7);
    Check("ConvertPtr: a missing object gives 0 and keeps the pointer", SWIG_ConvertPtr(nullptr, &pointer, base, 1) == 0 &&
                                                                          pointer == reinterpret_cast<void *>(7));

    PyObject *other = PointerString("_10_p_Unknown");
    Check("ConvertPtr: another type gives -1", SWIG_ConvertPtr(other, &pointer, base, 1) == -1);
    Check("ConvertPtr: the text of the type error", ErrorText() == "Type error. Expected _p_Base");
    Check("ConvertPtr: flags 0 raise nothing", SWIG_ConvertPtr(other, &pointer, base, 0) == -1 && PyErr_Occurred() == nullptr);
    Py_DECREF(other);

    PyObject *upper = PointerString("_AB_p_Base");
    Check("ConvertPtr: an upper case digit ends the digits", SWIG_ConvertPtr(upper, &pointer, base, 1) == -1);
    PyErr_Clear();
    Py_DECREF(upper);

    PyObject *plain = PointerString("text");
    Check("ConvertPtr: a text without underscore gives -1", SWIG_ConvertPtr(plain, &pointer, base, 1) == -1);
    PyErr_Clear();
    Check("ConvertPtr: no type gives the pointer error", SWIG_ConvertPtr(plain, &pointer, nullptr, 1) == -1 &&
                                                          ErrorText() == "Expected a pointer");
    Py_DECREF(plain);

    PyObject *number = PyInt_FromLong(5);
    Check("ConvertPtr: a number gives -1", SWIG_ConvertPtr(number, &pointer, base, 1) == -1);
    PyErr_Clear();
    Py_DECREF(number);

    PyObject *derived = PointerString("_20_p_Derived");
    pointer = nullptr;
    Check("ConvertPtr: a derived type is accepted", SWIG_ConvertPtr(derived, &pointer, base, 1) == 0 &&
                                                     pointer == reinterpret_cast<void *>(0x20));
    Py_DECREF(derived);
}

void TestTypeList()
{
    SWIG_ResetTypeList();
    swig_type_info *base = SWIG_TypeRegister(g_baseTable);
    swig_type_info *derived = SWIG_TypeRegister(g_derivedTable);
    Check("TypeRegister: gives the first entry of the table", base == &g_baseTable[0] && derived == &g_derivedTable[0]);
    Check("TypeRegister: the entries are a chain", base->next == &g_baseTable[1] && g_baseTable[1].next == &g_baseTable[2] &&
                                                  g_baseTable[2].next == &g_baseTable[3] && g_baseTable[3].next == nullptr);
    Check("TypeRegister: the back links", g_baseTable[1].prev == base && g_baseTable[3].prev == &g_baseTable[2]);

    swig_type_info *again = SWIG_TypeRegister(g_duplicateTable);
    Check("TypeRegister: a table with a known name gives the known head", again == base);
    Check("TypeRegister: the new entries follow the head", base->next == &g_duplicateTable[1]);
    Check("TypeRegister: the old chain follows the new entries", g_duplicateTable[1].next == &g_baseTable[1]);

    SWIG_ResetTypeList();
    for (swig_type_info *entry = g_baseTable; entry->name != nullptr; ++entry)
        entry->next = entry->prev = nullptr;
    base = SWIG_TypeRegister(g_baseTable);
    Check("TypeCheck: the first name gives the first entry", SWIG_TypeCheck("_p_Base", base) == &g_baseTable[1]);
    swig_type_info *oldPrev = g_baseTable[3].prev;
    swig_type_info *found = SWIG_TypeCheck("_p_Other", base);
    Check("TypeCheck: a name in the chain is found", found == &g_baseTable[3]);
    Check("TypeCheck: the found entry moves to the front", base->next == &g_baseTable[3]);
    Check("TypeCheck: the entry keeps its old prev (B0012)", g_baseTable[3].prev == oldPrev);
    Check("TypeCheck: an unknown name gives 0", SWIG_TypeCheck("_p_Nothing", base) == nullptr);
    Check("TypeCheck: no type gives 0", SWIG_TypeCheck("_p_Base", nullptr) == nullptr);
    Check("TypeCast: no converter gives the pointer", SWIG_TypeCast(&g_baseTable[1], reinterpret_cast<void *>(5)) == reinterpret_cast<void *>(5));
    Check("TypeCast: the converter is called", SWIG_TypeCast(&g_baseTable[2], reinterpret_cast<void *>(6)) == reinterpret_cast<void *>(6));
    Check("TypeCast: no entry gives the pointer", SWIG_TypeCast(nullptr, reinterpret_cast<void *>(8)) == reinterpret_cast<void *>(8));
}

void TestConstantsAndLinks(swig_type_info *base)
{
    static char text[] = "text value";
    static swig_type_info *baseVariable = base;
    swig_const_info table[] = {
        { SWIG_TYPE_INT, "C_INT", -5, 0, nullptr, nullptr },
        { SWIG_TYPE_FLOAT, "C_FLOAT", 0, 0.5, nullptr, nullptr },
        { SWIG_TYPE_STRING, "C_STRING", 0, 0, text, nullptr },
        { SWIG_TYPE_POINTER, "C_POINTER", 0, 0, reinterpret_cast<void *>(0x4321), &baseVariable },
        { SWIG_TYPE_END, nullptr, 0, 0, nullptr, nullptr },
    };
    PyObject *dict = PyDict_New();
    SWIG_InstallConstants(dict, table);
    PyObject *value = PyDict_GetItemString(dict, const_cast<char *>("C_INT"));
    Check("InstallConstants: an int", value != nullptr && PyInt_AsLong(value) == -5);
    value = PyDict_GetItemString(dict, const_cast<char *>("C_FLOAT"));
    Check("InstallConstants: a float", value != nullptr && PyFloat_AsDouble(value) == 0.5);
    value = PyDict_GetItemString(dict, const_cast<char *>("C_STRING"));
    Check("InstallConstants: a string", value != nullptr && std::strcmp(PyString_AsString(value), "text value") == 0);
    value = PyDict_GetItemString(dict, const_cast<char *>("C_POINTER"));
    Check("InstallConstants: a pointer", value != nullptr && std::strcmp(PyString_AsString(value), "_4321_p_Base") == 0);
    Py_DECREF(dict);

    PyObject *link = SWIG_newvarlink();
    SWIG_addvarlink(link, "first", GetValue, SetValue);
    SWIG_addvarlink(link, "second", GetValue, SetValue);
    PyObject *read = PyObject_GetAttrString(link, const_cast<char *>("second"));
    Check("varlink: the getter gives the value", read != nullptr && PyInt_AsLong(read) == 42);
    Py_XDECREF(read);
    Check("varlink: the setter is called", PyObject_SetAttrString(link, const_cast<char *>("first"), Py_None) == 0 && g_setCalls == 1);
    Check("varlink: an unknown name raises NameError", PyObject_GetAttrString(link, const_cast<char *>("third")) == nullptr &&
                                                       PyErr_Occurred() == PyExc_NameError);
    PyErr_Clear();
    PyObject *repr = PyObject_Repr(link);
    Check("varlink: the repr", repr != nullptr && std::strcmp(PyString_AsString(repr), "<Global variables>") == 0);
    Py_XDECREF(repr);
}

}  // namespace

int main()
{
    Py_Initialize();
    SWIG_ResetTypeList();
    swig_type_info *base = SWIG_TypeRegister(g_baseTable);
    TestPointerStrings(base);
    TestTypeList();
    SWIG_ResetTypeList();
    TestConstantsAndLinks(SWIG_TypeRegister(g_baseTable));
    std::printf("bc_swig_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
