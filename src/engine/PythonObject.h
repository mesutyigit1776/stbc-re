#pragma once
#include <cstdint>

// The head of a Python 1.5.2 object of the interpreter in the exe: the reference count and the type.
// The interpreter is not reconstructed. The type holds the deallocation function at +0x18.
struct PythonObject {
    std::int32_t refCount;  // 0x00
    void* type;             // 0x04
};

// The singleton None (0x95dd20).
PythonObject* PythonNone();
