// The Python start-up of stbc.exe.

#include "startup.h"

#include "Python.h"

extern "C" struct _inittab _PyImport_Inittab[];
extern "C" void initAppc();

namespace {

const unsigned kTableCapacity = 64;

_inittab g_runtimeTable[kTableCapacity];  // 0x98f098
unsigned g_runtimeCount = 0;               // 0x9916ac

// 0x5bc1e0
void AppendModule(char *name, void (*init)())
{
    g_runtimeTable[g_runtimeCount].name = name;
    g_runtimeTable[g_runtimeCount].initfunc = init;
    ++g_runtimeCount;
    g_runtimeTable[g_runtimeCount].name = nullptr;
    g_runtimeTable[g_runtimeCount].initfunc = nullptr;
}

// 0x5bc1a0
void CopyModuleTable()
{
    for (const _inittab *entry = _PyImport_Inittab; entry->name != nullptr; ++entry)
        AppendModule(entry->name, entry->initfunc);
    AppendModule(const_cast<char *>("Appc"), initAppc);
}

}  // namespace

// 0x5bc190
void bc_RegisterBuiltinModules()
{
    CopyModuleTable();
    PyImport_Inittab = g_runtimeTable;
}

// 0x6f6c90
void bc_StartPython()
{
    bc_RegisterBuiltinModules();
    Py_Initialize();
}
