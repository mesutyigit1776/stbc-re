// The Appc module of stbc.exe: the tables that the generator writes and the start-up function (E0006, E0017).

#pragma once

#include "appc_swig.h"

struct AppcVariable {
    const char *name;
    PyObject *(*get)(void);
    int (*set)(PyObject *);
};

extern PyMethodDef appc_method_table[];
extern const unsigned appc_method_count;
// The names of the methods that are real functions, sorted.
extern const char *const appc_real_methods[];
extern const swig_const_info appc_constants[];
extern const unsigned appc_constant_count;
extern const AppcVariable appc_variables[];
extern const unsigned appc_variable_count;
extern swig_type_info *const appc_type_tables[];
extern swig_type_info *appc_swig_types[];
extern const unsigned appc_type_count;

// A method of a script class that the engine implements (E0147). The code is not reconstructed.
struct AppcHelper {
    const char *className;
    const char *methodName;
    PyCFunction code;
};

extern const AppcHelper appc_script_helpers[];
extern const unsigned appc_script_helper_count;

void AppcRegisterTypes();

// The method of a script helper that is not reconstructed. It raises a Python exception with the names and the exe address.
PyObject *AppcTrapHelper(const char *className, const char *methodName, unsigned address);


// A method of the exe that the source tree does not have yet. It raises a Python exception with the name and the exe address.
PyObject *AppcTrapMethod(const char *name, unsigned address);

// A module attribute of the exe that the source tree does not have yet. Reading it gives a trap value:
// any attribute of the value raises a Python exception with the name and the exe address of the getter.
// The scripts read all module attributes when they import App, so the read itself cannot raise (E0142).
PyObject *AppcTrapValue(const char *name, unsigned address);

// Writing such an attribute raises at once. The kind is "setter". Returns 1 after the exception, or 0 when the
// traps return None (see AppcSetTrapsReturnNone).
int AppcTrapVariable(const char *name, const char *kind, unsigned address);

// Host option of the import gate, off by default: a trap does not raise. A method gives None, a module attribute
// gives None, a write is accepted. Each trap is still recorded as an event.
void AppcSetTrapsReturnNone(bool enabled);

// The traps that ran since the start, in order. The text is the same as the text of the exception.
unsigned AppcTrapEventCount();
const char *AppcTrapEventText(unsigned index);

// The text of the last trap exception. A host prints it to tell which engine call is missing.
const char *AppcLastTrapText();

// 0x65a250
extern "C" void initAppc();
