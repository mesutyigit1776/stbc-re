// Test host for the Python start-up of stbc.exe: no renderer, no game loop.
// Usage: bc_pyhost GAME_ROOT [--app-only]
//        bc_pyhost --compile-oracle [--host-text]     (tool: see host_tools.h)
//        bc_pyhost GAME_ROOT --gate MODULE [--trap-none]   (tool: see host_tools.h)
//        bc_pyhost GAME_ROOT --run SCRIPT.py     (the script runs after the start-up, with texture sizes from TGA files)
// The program changes to GAME_ROOT and does the Python part of 0x43ad70: the helper registrations, the import of
// App, FixApp.FixApp(), the import of Autoexec. It stops at the first Python call that needs engine code.

#include "Crt.h"
#include "appc_module.h"
#include "host_tools.h"
#include "Trap.h"
#include "script_calls.h"
#include "startup.h"

#include "Python.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// The whole start-up stops where the engine code of the exe is not reconstructed yet. This is not a failure of the
// Python start-up, which the tests of this mode check.
static void StopAtExeCode(std::uint32_t address)
{
    std::printf("import Autoexec: stopped, engine code missing: exe code at 0x%08x\n", address);
    std::fflush(stdout);
    std::_Exit(0);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--compile-oracle") == 0) {
        bool hostText = argc == 3 && std::strcmp(argv[2], "--host-text") == 0;
        RunStaticInitializers();
        RunModuleInitializers();
        bc_StartPython();
        return bc_RunCompileOracle(!hostText);
    }
    bool gate = argc >= 4 && std::strcmp(argv[2], "--gate") == 0;
    if (gate) {
        bool trapNone = argc == 5 && std::strcmp(argv[4], "--trap-none") == 0;
        if (argc != 4 && !trapNone) {
            std::fprintf(stderr, "usage: %s GAME_ROOT --gate MODULE [--trap-none]\n", argv[0]);
            return 2;
        }
        if (chdir(argv[1]) != 0) {
            std::perror(argv[1]);
            return 2;
        }
        RunStaticInitializers();
        RunModuleInitializers();
        bc_StartPython();
        AppcRegisterScriptHelpers();
        char fixApp[] = "FixApp";
        if (bc_ImportScriptModule("App", false) == nullptr ||
            bc_CallModuleFunction("FixApp", fixApp, "", nullptr, "()") != 0) {
            std::printf("GATE\tstartup\t\t\t\n");
            return 1;
        }
        // The first lines of Autoexec: the game runs them after FixApp, before every other script module.
        if (PyRun_SimpleString(const_cast<char *>("import sys\nsys.path.append('scripts/Icons')\n"
                                                  "sys.setcheckinterval(200)\nimport FontsAndIcons\n")) != 0) {
            std::printf("GATE\tstartup\t\t\t\n");
            return 1;
        }
        return bc_RunImportGate(argv[3], trapNone);
    }
    if (argc == 4 && std::strcmp(argv[2], "--run") == 0) {
        if (chdir(argv[1]) != 0) {
            std::perror(argv[1]);
            return 2;
        }
        RunStaticInitializers();
        RunModuleInitializers();
        bc_StartPython();
        AppcRegisterScriptHelpers();
        char fixApp[] = "FixApp";
        if (bc_ImportScriptModule("App", false) == nullptr || bc_CallModuleFunction("FixApp", fixApp, "", nullptr, "()") != 0) {
            std::printf("start-up: failed\n");
            return 1;
        }
        bc_InstallIconFileTextures();
        return bc_RunScriptFile(argv[3]);
    }
    bool appOnly = argc == 3 && std::strcmp(argv[2], "--app-only") == 0;
    if (argc != 2 && !appOnly) {
        std::fprintf(stderr, "usage: %s GAME_ROOT [--app-only]\n", argv[0]);
        return 2;
    }
    if (chdir(argv[1]) != 0) {
        std::perror(argv[1]);
        return 2;
    }
    RunStaticInitializers();
    RunModuleInitializers();
    bc_StartPython();
    AppcRegisterScriptHelpers();

    if (bc_ImportScriptModule("App", false) == nullptr) {
        std::printf("import App: failed\n");
        return 1;
    }
    std::printf("import App: ok\n");
    if (appOnly)
        return 0;

    char fixApp[] = "FixApp";
    if (bc_CallModuleFunction("FixApp", fixApp, "", nullptr, "()") != 0) {
        std::printf("FixApp.FixApp: failed\n");
        return 1;
    }
    std::printf("FixApp.FixApp: ok\n");

    bc::SetTrapHandler(StopAtExeCode);
    if (bc_ImportScriptModule("Autoexec", false) == nullptr) {
        const char *trap = AppcLastTrapText();
        if (*trap == 0) {
            std::printf("import Autoexec: failed\n");
            return 1;
        }
        std::printf("import Autoexec: stopped, engine call missing: %s\n", trap);
        return 0;
    }
    std::printf("import Autoexec: ok\n");
    return 0;
}
