// The SWIG 1.3 runtime of the Appc module of stbc.exe. Each function has the exe address in its comment.

#include "swig_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

swig_type_info *g_typeList = nullptr;  // 0x9916a4
PyObject *g_thisName = nullptr;        // 0x9916a8

const char kHexDigits[] = "0123456789abcdef";  // 0x8e6374

// The pointer value of the exe that stands for (void *)-1: the scripts compare this repr (E0140).
const char kAllOnesDigits[] = "ffffffff";

bool IsHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

int HexValue(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

}  // namespace

// 0x5ba9e0
swig_type_info *SWIG_TypeRegister(swig_type_info *table)
{
    swig_type_info *head = table;
    swig_type_info *next = nullptr;
    swig_type_info *found = nullptr;
    for (swig_type_info *entry = g_typeList; entry != nullptr; entry = entry->prev) {
        if (std::strcmp(entry->name, table->name) == 0) {
            found = entry;
            break;
        }
    }
    if (found != nullptr) {
        head = found;
        next = found->next;
    } else {
        table->prev = g_typeList;
        g_typeList = table;
    }
    swig_type_info *last = head;
    for (swig_type_info *entry = table + 1; entry->name != nullptr; ++entry) {
        last->next = entry;
        entry->prev = last;
        last = entry;
    }
    last->next = next;
    return head;
}

// 0x5baa70. B0012: the moved entry keeps its old prev pointer.
swig_type_info *SWIG_TypeCheck(const char *name, swig_type_info *type)
{
    if (type == nullptr)
        return nullptr;
    for (swig_type_info *entry = type->next; entry != nullptr; entry = entry->next) {
        if (std::strcmp(entry->name, name) != 0)
            continue;
        if (entry == type->next)
            return entry;
        entry->prev->next = entry->next;  // B0012
        if (entry->next != nullptr)
            entry->next->prev = entry->prev;
        entry->next = type->next;
        if (type->next != nullptr)
            type->next->prev = entry;
        type->next = entry;
        return entry;
    }
    return nullptr;
}

// 0x5bab10
void *SWIG_TypeCast(swig_type_info *type, void *pointer)
{
    if (type != nullptr && type->converter != nullptr)
        return type->converter(pointer);
    return pointer;
}

// 0x5bae00
int SWIG_ConvertPtr(PyObject *obj, void **ptr, swig_type_info *type, int flags)
{
    bool ownsObject = false;
    bool ownsText = false;
    if (obj == nullptr)
        return 0;
    if (obj == Py_None) {
        *ptr = nullptr;
        return 0;
    }
    bool failed = false;
    if (obj->ob_type != &PyString_Type) {
        if (g_thisName == nullptr)
            g_thisName = PyString_InternFromString(const_cast<char *>("this"));
        obj = PyObject_GetAttr(obj, g_thisName);
        ownsObject = true;
        if (obj == nullptr) {
            failed = true;
        } else if (obj->ob_type != &PyString_Type) {
            Py_DECREF(obj);
            failed = true;
        } else {
            ownsText = true;
        }
    }
    if (!failed) {
        const char *text = PyString_AsString(obj);
        if (*text == '_') {
            const char *digits = text + 1;
            std::uintptr_t value = 0;
            while (IsHexDigit(*digits)) {
                value = value * 16 + static_cast<std::uintptr_t>(HexValue(*digits));
                ++digits;
            }
            if (static_cast<std::size_t>(digits - (text + 1)) == sizeof(kAllOnesDigits) - 1 &&
                std::strncmp(text + 1, kAllOnesDigits, sizeof(kAllOnesDigits) - 1) == 0)
                value = static_cast<std::uintptr_t>(-1);
            *ptr = reinterpret_cast<void *>(value);
            if (ownsObject) {
                Py_DECREF(obj);
            }
            if (type == nullptr)
                return 0;
            swig_type_info *cast = SWIG_TypeCheck(digits, type);
            if (cast == nullptr)
                failed = true;
            else {
                *ptr = SWIG_TypeCast(cast, *ptr);
                return 0;
            }
        } else {
            *ptr = nullptr;
            if (std::strcmp(text, "NULL") == 0) {
                if (ownsText) {
                    Py_DECREF(obj);
                }
                return 0;
            }
            if (ownsText) {
                Py_DECREF(obj);
            }
            failed = true;
        }
    }
    if (flags != 0) {
        if (type != nullptr) {
            std::size_t size = std::strlen(type->name) + 0x40;
            char *message = static_cast<char *>(std::malloc(size));
            std::snprintf(message, size, "Type error. Expected %s", type->name);
            PyErr_SetString(PyExc_TypeError, message);
            std::free(message);
        } else {
            PyErr_SetString(PyExc_TypeError, const_cast<char *>("Expected a pointer"));
        }
    }
    return -1;
}

// 0x5bb040
void SWIG_MakePtr(char *buffer, void *ptr, swig_type_info *type)
{
    if (ptr == nullptr) {
        std::strcpy(buffer, "NULL");
        return;
    }
    char digits[32];
    int count = 0;
    std::uintptr_t value = reinterpret_cast<std::uintptr_t>(ptr);
    if (value == static_cast<std::uintptr_t>(-1)) {
        std::memcpy(digits, kAllOnesDigits, sizeof(kAllOnesDigits) - 1);
        count = sizeof(kAllOnesDigits) - 1;
    } else {
        do {
            digits[count++] = kHexDigits[value & 0xf];
            value >>= 4;
        } while (value != 0);
    }
    *buffer++ = '_';
    while (count > 0)
        *buffer++ = digits[--count];
    std::strcpy(buffer, type->name);
}

// 0x5bb0e0
PyObject *SWIG_NewPointerObj(void *ptr, swig_type_info *type)
{
    if (ptr == nullptr) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    char text[512];
    SWIG_MakePtr(text, ptr, type);
    return PyString_FromString(text);
}

// 0x5bb130
void SWIG_InstallConstants(PyObject *dict, const swig_const_info *table)
{
    for (const swig_const_info *entry = table; entry->type != SWIG_TYPE_END; ++entry) {
        PyObject *value = nullptr;
        switch (entry->type) {
        case SWIG_TYPE_INT:
            value = PyInt_FromLong(entry->lvalue);
            break;
        case SWIG_TYPE_FLOAT:
            value = PyFloat_FromDouble(entry->dvalue);
            break;
        case SWIG_TYPE_STRING:
            value = PyString_FromString(static_cast<char *>(entry->pvalue));
            break;
        case SWIG_TYPE_POINTER:
            value = SWIG_NewPointerObj(entry->pvalue, *entry->ptype);
            break;
        default:
            break;
        }
        if (value != nullptr) {
            PyDict_SetItemString(dict, const_cast<char *>(entry->name), value);
            Py_DECREF(value);
        }
    }
}

namespace {

// 0x5babd0
PyObject *VarlinkRepr(swig_varlinkobject *)
{
    return PyString_FromString("<Global variables>");
}

// 0x5babe0
int VarlinkPrint(swig_varlinkobject *link, FILE *fp, int)
{
    std::fprintf(fp, "Global variables { ");
    for (swig_globalvar *var = link->vars; var != nullptr; var = var->next) {
        std::fprintf(fp, "%s", var->name);
        if (var->next != nullptr)
            std::fprintf(fp, ", ");
    }
    std::fprintf(fp, " }\n");
    return 0;
}

// 0x5bac40
PyObject *VarlinkGetattr(swig_varlinkobject *link, char *name)
{
    for (swig_globalvar *var = link->vars; var != nullptr; var = var->next) {
        if (std::strcmp(var->name, name) == 0)
            return var->get_attr();
    }
    PyErr_SetString(PyExc_NameError, const_cast<char *>("Unknown C global variable"));
    return nullptr;
}

// 0x5bacc0
int VarlinkSetattr(swig_varlinkobject *link, char *name, PyObject *value)
{
    for (swig_globalvar *var = link->vars; var != nullptr; var = var->next) {
        if (std::strcmp(var->name, name) == 0)
            return var->set_attr(value);
    }
    PyErr_SetString(PyExc_NameError, const_cast<char *>("Unknown C global variable"));
    return 1;
}

// 0x8e6308
PyTypeObject g_varlinkType;

void InitVarlinkType()
{
    g_varlinkType.ob_refcnt = 1;
    g_varlinkType.ob_type = &PyType_Type;
    g_varlinkType.ob_size = 0;
    g_varlinkType.tp_name = const_cast<char *>("swigvarlink");
    g_varlinkType.tp_basicsize = sizeof(swig_varlinkobject);
    g_varlinkType.tp_itemsize = 0;
    g_varlinkType.tp_dealloc = nullptr;
    g_varlinkType.tp_print = reinterpret_cast<printfunc>(VarlinkPrint);
    g_varlinkType.tp_getattr = reinterpret_cast<getattrfunc>(VarlinkGetattr);
    g_varlinkType.tp_setattr = reinterpret_cast<setattrfunc>(VarlinkSetattr);
    g_varlinkType.tp_compare = nullptr;
    g_varlinkType.tp_repr = reinterpret_cast<reprfunc>(VarlinkRepr);
}

}  // namespace

// 0x5bad40
PyObject *SWIG_newvarlink()
{
    InitVarlinkType();
    swig_varlinkobject *link = PyObject_NEW(swig_varlinkobject, &g_varlinkType);
    link->ob_type = &g_varlinkType;
    link->vars = nullptr;
    link->ob_refcnt = 1;
    return reinterpret_cast<PyObject *>(link);
}

// 0x5bad80
void SWIG_addvarlink(PyObject *link, const char *name, PyObject *(*get_attr)(void), int (*set_attr)(PyObject *))
{
    swig_varlinkobject *object = reinterpret_cast<swig_varlinkobject *>(link);
    swig_globalvar *var = static_cast<swig_globalvar *>(std::malloc(sizeof(swig_globalvar)));
    var->name = static_cast<char *>(std::malloc(std::strlen(name) + 1));
    std::strcpy(var->name, name);
    var->get_attr = get_attr;
    var->set_attr = set_attr;
    var->next = object->vars;
    object->vars = var;
}

void SWIG_ResetTypeList()
{
    g_typeList = nullptr;
}
