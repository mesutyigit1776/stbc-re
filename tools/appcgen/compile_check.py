"""Compile the generated Appc wrappers with clang++ -fsyntax-only.

The Python 1.5.2 headers come from a pristine source tree and are copied to a scratch folder.
The class headers are the real ones in src/engine. The check proves that the generated code is
well formed against their signatures. It does not run the code and does not compare the
bodies of the engine functions. The planted-defect step compiles against a copy of the headers
in the scratch folder with one method removed.

Usage: python3 -I tools/appcgen/compile_check.py [--python-src DIR] [--scratch DIR] [--out DIR]
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appcgen  # noqa: E402

REPO = appcgen.REPO
DEFAULT_PYTHON_SRC = os.path.join(REPO, "third_party", "Python-1.5.2")

CONFIG_H = """\
#define HAVE_PROTOTYPES 1
#define HAVE_STDARG_PROTOTYPES 1
#define STDC_HEADERS 1
#define HAVE_STDLIB_H 1
#define HAVE_UNISTD_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDARG_H 1
#define HAVE_SYS_TYPES_H 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_VOID_P 8
#define RETSIGTYPE void
"""

ENGINE_DIR = os.path.join(REPO, "src", "engine")
RUNTIME_DIRS = [os.path.join(REPO, "src", "pyhost", "swig"), os.path.join(REPO, "src", "pyhost", "appc")]
# Planted defects: one method that a wrapper calls is removed from a copy of the engine headers. The first is in a
# method call of a value type, the second in a method call on a pooled class that returns an int, the third in
# a static function that returns a pointer, and the fourth in the global object of a module attribute. The last two
# are in a method that returns a float and in a method that returns a pointer of the icon classes.
PLANTED = [
    ("TGPoint3.h", "    void Scale(float scale);\n"),
    ("TGModelProperty.h", "    int GetRefCount() const;\n"),
    ("TGModelProperty.h", "    static TGModelProperty* Cast(TGObject* object);\n"),
    ("TGModelPropertyManager.h", "    static TGModelPropertyManager* Global();\n"),
    ("TGIconGroup.h", "    double GetIconScreenWidth(std::int32_t number);\n"),
    ("TGIconManager.h", "    TGIconGroup* GetIconGroup(const char* name);\n"),
]


def python_include(python_src, scratch):
    include = os.path.join(scratch, "py152_include")
    shutil.rmtree(include, ignore_errors=True)
    os.makedirs(include)
    for header in glob.glob(os.path.join(python_src, "Include", "*.h")):
        shutil.copy(header, include)
    with open(os.path.join(include, "config.h"), "w") as handle:
        handle.write(CONFIG_H)
    return include


def planted_headers(scratch, header, line):
    """Copy the engine headers and remove one method that the wrappers call."""
    root = os.path.join(scratch, "engine_planted")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    for name in glob.glob(os.path.join(ENGINE_DIR, "*.h")):
        shutil.copy(name, root)
    path = os.path.join(root, header)
    text = open(path).read()
    if line not in text:
        raise SystemExit("compile_check: the planted-defect line is not in %s" % header)
    with open(path, "w") as handle:
        handle.write(text.replace(line, "", 1))
    return root


TABLE_TEST = """\
#include "appc_swig.h"
#include "appc_wrappers.h"

PyMethodDef appc_table_test[] = {
#include "appc_methods.inc"
    { NULL, NULL, 0, NULL }
};

struct AppcVariable {
    char *name;
    PyObject *(*get)(void);
    int (*set)(PyObject *);
};

AppcVariable appc_variable_test[] = {
#include "appc_variables.inc"
    { NULL, NULL, NULL }
};
"""


def compile_all(generated, include, engine_root):
    failures = 0
    table_test = os.path.join(os.path.dirname(include), "table_test.cpp")
    with open(table_test, "w") as handle:
        handle.write(TABLE_TEST)
    sources = sorted(glob.glob(os.path.join(generated, "appc_*.cpp")) + glob.glob(os.path.join(generated, "module_*.cpp")))
    for source in sources + [table_test]:
        command = [
            "clang++", "-std=c++17", "-fsyntax-only", "-Wall", "-Wno-write-strings", "-Wno-invalid-offsetof",
            "-I", generated, "-I", include, "-I", engine_root, source,
        ]
        for runtime_dir in RUNTIME_DIRS:
            command[command.index(source):command.index(source)] = ["-I", runtime_dir]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        status = "ok" if result.returncode == 0 else "FAILED"
        print("compile %-28s %s" % (os.path.basename(source), status))
        if result.returncode != 0:
            print(result.stdout)
            failures += 1
        elif result.stdout.strip():
            print(result.stdout)
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--python-src", default=DEFAULT_PYTHON_SRC)
    parser.add_argument("--scratch", default=os.path.join(REPO, "re", "out", "appc_compile"))
    parser.add_argument("--out", default=os.path.join(REPO, "build", "appc"))
    options = parser.parse_args()
    appcgen.generate(os.path.join(REPO, "interface"), options.out)

    include = python_include(options.python_src, options.scratch)
    failures = compile_all(options.out, include, ENGINE_DIR)

    for header, line in PLANTED:
        print("planted defect: %s without %s" % (header, line.strip()))
        if compile_all(options.out, include, planted_headers(options.scratch, header, line)) == 0:
            print("compile check did not fail on the planted defect")
            failures += 1
        else:
            print("planted defect detected")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
