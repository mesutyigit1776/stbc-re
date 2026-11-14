// The Python start-up of stbc.exe (E0116, E0141).

#pragma once

// 0x6f6c90: registers the built-in modules, then starts the interpreter. The table of the built-in modules
// and the module Appc are put in the run-time table that the import code uses before Py_Initialize (0x5bc190).
void bc_StartPython();

// 0x5bc190
void bc_RegisterBuiltinModules();
