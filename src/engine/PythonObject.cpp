#include "PythonObject.h"

static PythonObject g_none = {1, nullptr};

// 0x95dd20
PythonObject* PythonNone() { return &g_none; }
