"""Write the tables of the Appc module: all method names in exe order, the type tables, the constants
and the module attributes.

The wrappers of interface/*.appc are real functions. Every other name of the method table and every other
module attribute is a trap that raises a Python exception when it is called or read.
The rows come from target/appc_method_table.tsv, appc_constants.tsv, appc_globals.tsv and appc_swig_types.tsv.
"""

import csv
import os

import appcdesc

CONSTANT_TYPES = {"int": 1, "float": 2}
CAST_IDENTITY = "AppcCastIdentity"
MISSING_METHOD_INDEX = 1
WRONG_CONSTANT_INDEX = 2


def read_rows(target_dir, name):
    with open(os.path.join(target_dir, name), newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def c_string(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def real_wrapper_names(description):
    """The method table name of each generated wrapper and the wrapper function."""
    names = {}
    for func in description.all_funcs():
        for name in func.table_names:
            names[name] = "Appc_" + func.name
    return names


DEFECTS = ("wrong_constant", "missing_method")


def render_methods(description, banner, rows, defect=None):
    real = real_wrapper_names(description)
    known = {row["name"] for row in rows}
    missing = sorted(set(real) - known)
    if missing:
        raise appcdesc.DescriptionError("method names that are not in appc_method_table.tsv: %s" % ", ".join(missing[:5]))
    traps, table = [], []
    for position, row in enumerate(rows):
        name = row["name"]
        if defect == "missing_method" and position == MISSING_METHOD_INDEX:
            continue
        if name in real:
            function = real[name]
        else:
            function = "Trap_%d" % position
            traps.append("static PyObject *%s(PyObject *, PyObject *)\n{\n    return AppcTrapMethod(%s, %s);\n}\n"
                         % (function, c_string(name), row["function_address"]))
        table.append("    { APPC_CSTR(%s), %s, %s, NULL }," % (c_string(name), function, row["flags"]))
    return "".join([
        banner, '#include "appc_module.h"\n', '#include "appc_wrappers.h"\n\n',
        "\n".join(traps), "\n",
        "PyMethodDef appc_method_table[] = {\n", "\n".join(table), "\n    { NULL, NULL, 0, NULL }\n};\n\n",
        "const unsigned appc_method_count = %d;\n\n" % len(table),
        "const char *const appc_real_methods[] = {\n",
        "".join("    %s,\n" % c_string(name) for name in sorted(real)),
        "    NULL\n};\n",
    ])


def render_constants(banner, rows, defect=None):
    lines = []
    for position, row in enumerate(rows):
        kind = CONSTANT_TYPES[row["value_type"]]
        if kind == 1:
            value = int(row["value"]) + (1 if defect == "wrong_constant" and position == WRONG_CONSTANT_INDEX else 0)
            lines.append("    { SWIG_TYPE_INT, %s, %dL, 0, 0, 0 }," % (c_string(row["name"]), value))
        else:
            lines.append("    { SWIG_TYPE_FLOAT, %s, 0, %s, 0, 0 }," % (c_string(row["name"]), float(row["value"]).hex()))
    return "".join([
        banner, '#include "appc_module.h"\n\n', "const swig_const_info appc_constants[] = {\n", "\n".join(lines),
        "\n    { SWIG_TYPE_END, 0, 0, 0, 0, 0 }\n};\n\n", "const unsigned appc_constant_count = %d;\n" % len(rows),
    ])


def render_variables(description, banner, rows):
    real = {variable.name for variable in description.all_variables()}
    known = {row["name"] for row in rows}
    missing = sorted(real - known)
    if missing:
        raise appcdesc.DescriptionError("variables that are not in appc_globals.tsv: %s" % ", ".join(missing))
    traps, table = [], []
    for position, row in enumerate(rows):
        name = row["name"]
        if name in real:
            getter, setter = "Appc_get_" + name, "Appc_set_" + name
        else:
            getter, setter = "TrapGet_%d" % position, "TrapSet_%d" % position
            traps.append("static PyObject *%s(void)\n{\n    return AppcTrapValue(%s, %s);\n}\n"
                         % (getter, c_string(name), row["getter_address"]))
            traps.append("static int %s(PyObject *)\n{\n    return AppcTrapVariable(%s, \"setter\", %s);\n}\n"
                         % (setter, c_string(name), row["setter_address"]))
        table.append("    { %s, %s, %s }," % (c_string(name), getter, setter))
    return "".join([
        banner, '#include "appc_module.h"\n', '#include "appc_wrappers.h"\n\n',
        "\n".join(traps), "\n",
        "const AppcVariable appc_variables[] = {\n", "\n".join(table), "\n    { NULL, NULL, NULL }\n};\n\n",
        "const unsigned appc_variable_count = %d;\n" % len(rows),
    ])


def render_types(description, banner, rows):
    tables = {}
    for row in rows:
        tables.setdefault(int(row["type_index"]), []).append(row)
    heads = {entries[0]["name"]: index for index, entries in tables.items()}
    bindings = []
    for decl in description.types.values():
        if decl.swig_name not in heads:
            raise appcdesc.DescriptionError("type %s is not a table in appc_swig_types.tsv" % decl.swig_name)
        bindings.append((decl.swig_var, heads[decl.swig_name]))
    converters = sorted({row["converter_address"] for row in rows if row["converter_kind"] in ("offset", "other")})
    parts = [banner, '#include "appc_module.h"\n', '#include "Trap.h"\n\n']
    parts.append("static void *%s(void *pointer)\n{\n    return pointer;\n}\n\n" % CAST_IDENTITY)
    for address in converters:
        parts.append("static void *Cast_%s(void *)\n{\n    BC_NOT_RECONSTRUCTED(%s);\n    return 0;\n}\n\n" % (address, address))
    for index in sorted(tables):
        lines = []
        for row in tables[index]:
            if row["converter_kind"] == "none":
                converter = "0"
            elif row["converter_kind"] == "identity":
                converter = CAST_IDENTITY
            else:
                converter = "Cast_" + row["converter_address"]
            text = c_string(row["type_string"]) if row["type_string"] else "0"
            lines.append("    { %s, %s, %s, 0, 0 }," % (c_string(row["name"]), converter, text))
        parts.append("static swig_type_info types_%d[] = {\n%s\n    { 0, 0, 0, 0, 0 }\n};\n\n" % (index, "\n".join(lines)))
    count = len(tables)
    parts.append("swig_type_info *const appc_type_tables[%d] = {\n" % count)
    parts.append(",\n".join("    types_%d" % index for index in sorted(tables)))
    parts.append("\n};\n\nswig_type_info *appc_swig_types[%d];\n\nconst unsigned appc_type_count = %d;\n\n" % (count, count))
    for variable, _ in bindings:
        parts.append("swig_type_info *%s = 0;\n" % variable)
    parts.append("\nvoid AppcRegisterTypes()\n{\n    for (unsigned i = 0; i < appc_type_count; ++i)\n")
    parts.append("        appc_swig_types[i] = SWIG_TypeRegister(appc_type_tables[i]);\n")
    for variable, index in bindings:
        parts.append("    %s = appc_swig_types[%d];\n" % (variable, index))
    parts.append("}\n")
    return "".join(parts)


def render_helpers(banner, rows):
    traps, table = [], []
    for row in rows:
        function = "Helper_%s" % row["index"]
        traps.append("static PyObject *%s(PyObject *, PyObject *)\n{\n    return AppcTrapHelper(%s, %s, %s);\n}\n"
                     % (function, c_string(row["class"]), c_string(row["method"]), row["code_address"]))
        table.append("    { %s, %s, %s }," % (c_string(row["class"]), c_string(row["method"]), function))
    return "".join([
        banner, '#include "appc_module.h"\n\n', "\n".join(traps), "\n",
        "const AppcHelper appc_script_helpers[] = {\n", "\n".join(table), "\n    { NULL, NULL, NULL }\n};\n\n",
        "const unsigned appc_script_helper_count = %d;\n" % len(rows),
    ])


def render_module(description, target_dir, banner, defect=None):
    if defect not in (None,) + DEFECTS:
        raise appcdesc.DescriptionError("unknown defect %s" % defect)
    methods = read_rows(target_dir, "appc_method_table.tsv")
    constants = read_rows(target_dir, "appc_constants.tsv")
    variables = read_rows(target_dir, "appc_globals.tsv")
    types = read_rows(target_dir, "appc_swig_types.tsv")
    helpers = read_rows(target_dir, "script_helpers.tsv")
    return {
        "module_helpers.cpp": render_helpers(banner, helpers),
        "module_methods.cpp": render_methods(description, banner, methods, defect),
        "module_constants.cpp": render_constants(banner, constants, defect),
        "module_variables.cpp": render_variables(description, banner, variables),
        "module_types.cpp": render_types(description, banner, types),
    }
