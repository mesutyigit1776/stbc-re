// The call layer between the application and the scripts of stbc.exe (E0143 to E0147).

#pragma once

#include "Python.h"

// 0x6f9b80: 1 when a Python exception is set. The exception is printed and cleared.
int bc_CheckScriptError();

// 0x6f7d90: imports a module, or reloads it. A name of 0 means "__main__". The securelevel counter is raised
// while the code of the module runs. Returns the module, or 0 after an error that was printed.
PyObject *bc_ImportScriptModule(const char *name, bool reload);

// 0x6f8490: the attribute of a module that bc_ImportScriptModule imports. Returns a new reference, or 0.
PyObject *bc_GetModuleAttribute(const char *module, char *attribute);

// 0x6f8580: takes the result of a call. With an output address, the result is converted with the format.
int bc_ReceiveResult(PyObject *result, const char *format, void *output);

// 0x6f8c00: calls a callable with an argument tuple.
int bc_CallScriptFunction(PyObject *callable, const char *format, void *output, PyObject *args, bool debugModule, bool releaseCallable);

// 0x6f8ab0: calls the function of a module. The argument format is a Py_BuildValue format, or 0.
int bc_CallModuleFunction(const char *module, char *function, const char *resultFormat, void *output,
                          const char *argumentFormat, ...);

// 0x6f9bc0: a method that the engine implements and the script classes get. It is put in the class of the
// module App at once when App is loaded, else when bc_ImportScriptModule imports App (0x6f9db0).
bool bc_RegisterScriptHelper(PyCFunction code, const char *className, const char *methodName);

// 0x43ad70, the calls of 0x6f9bc0: registers the 33 helpers in the order of the exe.
void AppcRegisterScriptHelpers();
