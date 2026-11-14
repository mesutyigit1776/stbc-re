// The SWIG 1.3 runtime of the Appc module of stbc.exe (E0021, E0136).
// The pointer strings have the format of the exe: "_", the pointer in lower case hex without leading
// zeros, and the type name. The exe writes 32-bit values. This build writes the full host pointer (E0140).

#pragma once

#include "Python.h"

#include <cstdint>

// One entry of a type table. The exe entry has 20 bytes: name +0, converter +4, string +8, next +0xc, prev +0x10.
struct swig_type_info {
    const char *name;
    void *(*converter)(void *);
    const char *str;
    swig_type_info *next;
    swig_type_info *prev;
};

// One entry of the constant table. The exe entry has 32 bytes: type +0, name +4, long +8, double +0x10,
// pointer +0x18, type pointer +0x1c.
struct swig_const_info {
    int type;
    const char *name;
    long lvalue;
    double dvalue;
    void *pvalue;
    swig_type_info **ptype;
};

enum { SWIG_TYPE_END = 0, SWIG_TYPE_INT = 1, SWIG_TYPE_FLOAT = 2, SWIG_TYPE_STRING = 3, SWIG_TYPE_POINTER = 4 };

enum { SWIG_RAISE_TYPE_ERROR = 1 };

// One entry of the list of module attributes. The exe entry has 16 bytes: name, getter, setter, next.
struct swig_globalvar {
    char *name;
    PyObject *(*get_attr)(void);
    int (*set_attr)(PyObject *);
    swig_globalvar *next;
};

struct swig_varlinkobject {
    PyObject_HEAD
    swig_globalvar *vars;
};

swig_type_info *SWIG_TypeRegister(swig_type_info *table);
swig_type_info *SWIG_TypeCheck(const char *name, swig_type_info *type);
void *SWIG_TypeCast(swig_type_info *type, void *pointer);

int SWIG_ConvertPtr(PyObject *obj, void **ptr, swig_type_info *type, int flags);
void SWIG_MakePtr(char *buffer, void *ptr, swig_type_info *type);
PyObject *SWIG_NewPointerObj(void *ptr, swig_type_info *type);
void SWIG_InstallConstants(PyObject *dict, const swig_const_info *table);

PyObject *SWIG_newvarlink();
void SWIG_addvarlink(PyObject *link, const char *name, PyObject *(*get_attr)(void), int (*set_attr)(PyObject *));

// Clears the type list. Only a test uses it.
void SWIG_ResetTypeList();
