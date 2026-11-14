"""Write the Appc wrapper functions and method table rows from interface/*.appc.

Usage: python3 -I tools/appcgen/appcgen.py [--interface DIR] [--out DIR]
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appcdesc  # noqa: E402
import appcmodule  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANNER = "// Written by tools/appcgen/appcgen.py from interface/*.appc. Do not edit.\n"

# The text of the TypeError that every wrapper raises for a string argument of another type (0x930bdc).
TEXT_ERROR_MESSAGE = "Expected None or string, %s found"

RUNTIME_DECLARATIONS = """\
#include "swig_runtime.h"

#define APPC_CSTR(text) const_cast<char *>(text)
"""


def local_name(name):
    return "a_" + name


def substitute(expression):
    return re.sub(r"\$(\w+)", lambda m: "result" if m.group(1) == appcdesc.OUT_NAME else local_name(m.group(1)), expression)


def wrapper_name(func):
    return "Appc_" + func.name


def variable_functions(variable):
    return "Appc_get_" + variable.name, "Appc_set_" + variable.name


def new_expression(cls, arguments, description):
    """The allocation of a result object. A class with its own operator new has no nothrow form."""
    if description.types[cls].pooled:
        return "new %s(%s)" % (cls, arguments)
    return "new (std::nothrow) %s(%s)" % (cls, arguments)


def constructor_arguments(cls, call):
    """The arguments of a call written as a constructor call `Class(...)`, else the call as one copy argument."""
    if call.startswith(cls + "(") and call.endswith(")"):
        return call[len(cls) + 1:-1]
    return call


def render_return(func, description):
    kind = func.return_kind
    call = substitute(func.call)
    if kind == "none":
        return ["    %s;" % call, "    Py_INCREF(Py_None);", "    return Py_None;"]
    if kind == "float":
        return ["    return PyFloat_FromDouble(static_cast<double>(%s));" % call]
    if kind == "int":
        return ["    return PyInt_FromLong(static_cast<Py_long32>(%s));" % call]
    if kind == "string":
        return ["    const char *result = %s;" % call,
                "    if (result == NULL)",
                "        return Py_None;  // B0007: no new reference",
                "    return PyString_FromString(result);"]
    cls = func.return_class
    swig_return = "    return SWIG_NewPointerObj(result, %s);" % description.types[cls].swig_var
    if kind == "ptr":
        return ["    %s *result = %s;" % (cls, call), swig_return]
    if "$" + appcdesc.OUT_NAME in func.call:
        return ["    %s *result = %s;" % (cls, new_expression(cls, substitute(func.init or ""), description)),
                "    %s;" % call, swig_return]
    arguments = constructor_arguments(cls, call)
    return ["    %s *result = %s;" % (cls, new_expression(cls, arguments, description)), swig_return]


def render_locals(arg, description):
    """The declarations of a Python argument and the target for PyArg_ParseTuple."""
    if arg.is_text:
        return ["    PyObject *obj_%s = 0;" % arg.name, "    const char *%s = 0;" % local_name(arg.name)], "&obj_" + arg.name
    if arg.is_pointer:
        separator = " " if description.types[arg.ctype].funcptr else " *"
        return (["    PyObject *obj_%s = 0;" % arg.name, "    %s%s%s = 0;" % (arg.ctype, separator, local_name(arg.name))],
                "&obj_" + arg.name)
    default = arg.default if arg.optional else "0"
    ctype = "Py_long32" if arg.fmt == "l" else appcdesc.ARG_CTYPES[arg.fmt]  # the long of the exe has 32 bits
    return ["    %s %s = %s;" % (ctype, local_name(arg.name), default)], "&" + local_name(arg.name)


def render_conversion(arg, description):
    if arg.is_text:
        name = arg.name
        return [
            "    if (obj_%s == Py_None)" % name,
            "        %s = 0;" % local_name(name),
            "    else if (obj_%s->ob_type == &PyString_Type)" % name,
            "        %s = PyString_AsString(obj_%s);" % (local_name(name), name),
            "    else {",
            "        PyErr_Format(PyExc_TypeError, APPC_CSTR(\"%s\"), obj_%s->ob_type->tp_name);" % (TEXT_ERROR_MESSAGE, name),
            "        return NULL;",
            "    }",
        ]
    if arg.is_pointer:
        return [
            "    if (SWIG_ConvertPtr(obj_%s, (void **)&%s, %s, SWIG_RAISE_TYPE_ERROR) == -1)"
            % (arg.name, local_name(arg.name), description.types[arg.ctype].swig_var),
            "        return NULL;",
        ]
    return []


def render_wrapper(func, description):
    args = func.all_args
    lines = ["PyObject *%s(PyObject *, PyObject *args)" % wrapper_name(func), "{"]
    parse_targets = []
    for arg in args:
        declarations, target = render_locals(arg, description)
        lines.extend(declarations)
        parse_targets.append(target)
    parse = 'PyArg_ParseTuple(args, APPC_CSTR("%s")' % func.format
    if parse_targets:
        parse += ", " + ", ".join(parse_targets)
    lines.append("    if (!%s))" % parse)
    lines.append("        return NULL;")
    for arg in args:
        lines.extend(render_conversion(arg, description))
    lines.extend(render_return(func, description))
    lines.append("}")
    return "\n".join(lines) + "\n"


def render_variable(variable, description):
    getter, setter = variable_functions(variable)
    swig_var = description.types[variable.cls].swig_var
    if variable.assign == "none":
        assignment = ["    // assign: none (the setter of the exe only checks the type)"]
        ending = ["    return 0;"]
    else:
        assignment = []
        ending = ["    *(%s) = *source;" % variable.object, "    return 0;"]
    return "\n".join([
        "int %s(PyObject *value)" % setter,
        "{",
        "    %s *source = 0;" % variable.cls,
    ] + assignment + [
        "    if (SWIG_ConvertPtr(value, (void **)&source, %s, SWIG_RAISE_TYPE_ERROR) == -1)" % swig_var,
        "        return 1;",
    ] + ending + [
        "}",
        "",
        "PyObject *%s(void)" % getter,
        "{",
        "    return SWIG_NewPointerObj(%s, %s);" % (variable.object, swig_var),
        "}",
        "",
    ])


def render_family(family, description):
    parts = [BANNER, '#include "appc_swig.h"\n', "#include <new>\n"]
    parts.extend('#include "%s"\n' % include for include in family.includes)
    parts.append("\n")
    parts.append("\n".join(render_wrapper(f, description) for f in family.funcs))
    if family.variables:
        parts.append("\n" + "\n".join(render_variable(v, description) for v in family.variables))
    return "".join(parts)


def render_swig_header(description):
    lines = [BANNER, "#ifndef APPC_SWIG_H", "#define APPC_SWIG_H", "", RUNTIME_DECLARATIONS]
    for decl in description.types.values():
        if decl.opaque:
            lines.append("class %s;" % decl.cls)
    lines.append("")
    for decl in description.types.values():
        lines.append("extern swig_type_info *%s;" % decl.swig_var)
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def render_wrapper_header(description):
    lines = [BANNER, '#include "Python.h"', ""]
    for func in description.all_funcs():
        lines.append("PyObject *%s(PyObject *, PyObject *);" % wrapper_name(func))
    for variable in description.all_variables():
        getter, setter = variable_functions(variable)
        lines.append("PyObject *%s(void);" % getter)
        lines.append("int %s(PyObject *);" % setter)
    return "\n".join(lines) + "\n"


def render_method_rows(description):
    lines = [BANNER]
    for func in description.all_funcs():
        for name in func.table_names:
            lines.append('{ APPC_CSTR("%s"), %s, 1, NULL },' % (name, wrapper_name(func)))
    return "\n".join(lines) + "\n"


def render_variable_rows(description):
    lines = [BANNER]
    for variable in description.all_variables():
        getter, setter = variable_functions(variable)
        lines.append('{ APPC_CSTR("%s"), %s, %s },' % (variable.name, getter, setter))
    return "\n".join(lines) + "\n"


def generate(interface_dir, out_dir, defect=None):
    description = appcdesc.load(interface_dir)
    os.makedirs(out_dir, exist_ok=True)
    outputs = {
        "appc_swig.h": render_swig_header(description),
        "appc_wrappers.h": render_wrapper_header(description),
        "appc_methods.inc": render_method_rows(description),
        "appc_variables.inc": render_variable_rows(description),
    }
    for family in description.families:
        outputs["appc_%s.cpp" % family.stem] = render_family(family, description)
    outputs.update(appcmodule.render_module(description, os.path.join(REPO, "target"), BANNER, defect))
    for name, text in outputs.items():
        path = os.path.join(out_dir, name)
        with open(path, "w") as handle:
            handle.write(text)
    return sorted(outputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--interface", default=os.path.join(REPO, "interface"))
    parser.add_argument("--out", default=os.path.join(REPO, "build", "appc"))
    parser.add_argument("--defect", default=None, choices=appcmodule.DEFECTS,
                        help="plant a defect in the module tables (the module tests must then fail)")
    options = parser.parse_args()
    try:
        names = generate(options.interface, options.out, options.defect or None)
    except appcdesc.DescriptionError as error:
        sys.exit("appcgen: %s" % error)
    print("appcgen: wrote %s to %s" % (", ".join(names), options.out))


if __name__ == "__main__":
    main()
