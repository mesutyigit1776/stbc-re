// Tools of the test host that do not exist in the exe: the compile oracle and the import gate.

#pragma once

// Reads lines "SOURCE_PATH<TAB>CODE_FILE_NAME" from standard input. For each line it compiles the source the way
// the import code of the exe does (0x75cf00: PyParser_SimpleParseFile, then PyNode_Compile) and writes one record to
// standard output: "OK <size>\n" and the marshal bytes of the code object, or "ERR <size>\n" and the error text.
// With crtTextMode the file is read as the CRT text mode of Windows does: CR LF gives LF, 0x1a ends the file.
int bc_RunCompileOracle(bool crtTextMode);

// Imports one script module like bc_ImportScriptModule and writes the result as tab separated lines:
// "GATE", result (ok, trap, error), trap text, exception type, exception text; then one "EVENT" line for each
// trap that ran. With trapsReturnNone the traps do not raise.
int bc_RunImportGate(const char *module, bool trapsReturnNone);

// Puts texture functions into the icon boundary that read the size of a texture from the 18 byte header of an
// uncompressed TGA file (width and height at +12 and +14). A missing file gives no texture. The working directory
// text is the current directory with a trailing slash. The exe loads the file with the renderer (0x7e0c50); the
// host has no renderer, so this is a stand-in that gives the sizes of the real files.
void bc_InstallIconFileTextures();

// Runs a Python script file in the module __main__ and returns 0, or 1 after an exception.
int bc_RunScriptFile(const char *path);
