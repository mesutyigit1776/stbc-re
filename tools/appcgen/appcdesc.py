"""Reader for the Appc interface description files (interface/*.appc).

A file holds `include`, `type`, `func` and `global` blocks. Indented lines belong to the block above them.

    type CLASS SWIGNAME [opaque] [pooled] [funcptr]
    func NAME             this, arg, opt, init, call, return, alias
    global NAME CLASS     object, assign

The argument letters are f (float), i (int), l (long), O (a SWIG pointer of a described type) and S (a string
or None, which the wrapper reads from an O argument).
"""

import os
import re

ARG_CTYPES = {"f": "float", "i": "int", "l": "long", "S": "string"}
FORMAT_CHARS = {"f": "f", "i": "i", "l": "l", "S": "O", "O": "O"}
RETURN_KINDS = ("none", "int", "float", "string", "new", "ptr")
CLASS_RETURNS = ("new", "ptr")
TYPE_OPTIONS = ("opaque", "pooled", "funcptr")
OUT_NAME = "out"  # in a `return new` call: the object that the engine function fills


class DescriptionError(Exception):
    pass


class TypeDecl:
    def __init__(self, cls, swig_name, options):
        self.cls = cls
        self.swig_name = swig_name
        self.options = options

    @property
    def opaque(self):
        return "opaque" in self.options

    @property
    def pooled(self):
        """The class has its own operator new, so a wrapper cannot use the nothrow form."""
        return "pooled" in self.options

    @property
    def funcptr(self):
        """The C type is a function pointer type named by the class."""
        return "funcptr" in self.options

    @property
    def swig_var(self):
        return "SWIGTYPE" + self.swig_name


class Arg:
    def __init__(self, fmt, ctype, name, optional, default="0"):
        self.fmt = fmt
        self.ctype = ctype
        self.name = name
        self.optional = optional
        self.default = default

    @property
    def format_char(self):
        return FORMAT_CHARS[self.fmt]

    @property
    def is_pointer(self):
        return self.fmt == "O"

    @property
    def is_text(self):
        return self.fmt == "S"


class Func:
    def __init__(self, name, source, line):
        self.name = name
        self.source = source
        self.line = line
        self.aliases = []
        self.this_class = None
        self.args = []
        self.init = None
        self.call = None
        self.return_kind = None
        self.return_class = None

    @property
    def all_args(self):
        """Python-visible arguments in call order, `this` first."""
        first = [Arg("O", self.this_class, "this", False)] if self.this_class else []
        return first + self.args

    @property
    def format(self):
        text = ""
        optional_seen = False
        for arg in self.all_args:
            if arg.optional and not optional_seen:
                text += "|"
                optional_seen = True
            text += arg.format_char
        return text + ":" + self.name

    @property
    def table_names(self):
        return [self.name] + self.aliases


class Variable:
    """A module attribute of the Python module: the object of a class at a fixed place."""

    def __init__(self, name, cls, source, line):
        self.name = name
        self.cls = cls
        self.source = source
        self.line = line
        self.object = None
        self.assign = "copy"  # `copy`: the setter copies the members; `none`: the setter only checks the type


class Family:
    def __init__(self, stem, path):
        self.stem = stem
        self.path = path
        self.includes = []
        self.funcs = []
        self.variables = []


class Description:
    def __init__(self):
        self.types = {}
        self.families = []

    def all_funcs(self):
        return [f for fam in self.families for f in fam.funcs]

    def all_variables(self):
        return [v for fam in self.families for v in fam.variables]


def parse_file(path, description):
    stem = os.path.splitext(os.path.basename(path))[0]
    family = Family(stem, path)
    current = None
    with open(path) as handle:
        for number, raw in enumerate(handle, 1):
            text = raw.split("#", 1)[0].rstrip()
            if not text.strip():
                continue
            where = "%s:%d" % (path, number)
            words = text.split()
            keyword = words[0]
            if text[0] not in " \t":
                current = parse_block_head(family, description, keyword, words, text, where, number)
                continue
            if current is None:
                raise DescriptionError("%s: line outside a block" % where)
            if isinstance(current, Variable):
                parse_variable_line(current, keyword, text.strip(), where)
            else:
                parse_func_line(current, keyword, text.strip(), words, where)
    description.families.append(family)


def parse_block_head(family, description, keyword, words, text, where, number):
    if keyword == "include" and len(words) == 2:
        family.includes.append(words[1])
    elif keyword == "type" and len(words) >= 3:
        options = words[3:]
        if any(option not in TYPE_OPTIONS for option in options):
            raise DescriptionError("%s: bad type option" % where)
        if words[1] in description.types:
            raise DescriptionError("%s: type %s declared twice" % (where, words[1]))
        description.types[words[1]] = TypeDecl(words[1], words[2], options)
    elif keyword == "func" and len(words) == 2:
        func = Func(words[1], family.path, number)
        family.funcs.append(func)
        return func
    elif keyword == "global" and len(words) == 3:
        variable = Variable(words[1], words[2], family.path, number)
        family.variables.append(variable)
        return variable
    else:
        raise DescriptionError("%s: cannot read '%s'" % (where, text.strip()))
    return None


def parse_variable_line(variable, keyword, text, where):
    if keyword == "object":
        variable.object = text[len("object"):].strip()
    elif keyword == "assign" and text.split()[1:] in (["copy"], ["none"]):
        variable.assign = text.split()[1]
    else:
        raise DescriptionError("%s: cannot read '%s'" % (where, text))


def parse_func_line(func, keyword, text, words, where):
    if keyword == "alias" and len(words) == 2:
        func.aliases.append(words[1])
    elif keyword == "this" and len(words) == 2:
        func.this_class = words[1]
    elif keyword == "arg" and len(words) == 4:
        func.args.append(Arg(words[1], words[2], words[3], False))
    elif keyword == "opt" and len(words) in (4, 5):
        func.args.append(Arg(words[1], words[2], words[3], True, words[4] if len(words) == 5 else "0"))
    elif keyword == "init":
        func.init = text[len("init"):].strip()
    elif keyword == "call":
        func.call = text[len("call"):].strip()
    elif keyword == "return" and len(words) in (2, 3):
        func.return_kind = words[1]
        func.return_class = words[2] if len(words) == 3 else None
    else:
        raise DescriptionError("%s: cannot read '%s'" % (where, text))


def validate_func(func, description):
    where = "%s:%d (%s)" % (func.source, func.line, func.name)
    if func.this_class and func.this_class not in description.types:
        raise DescriptionError("%s: unknown class %s" % (where, func.this_class))
    seen_optional = False
    arg_names = {"this"} if func.this_class else set()
    for arg in func.args:
        if arg.name in arg_names:
            raise DescriptionError("%s: argument %s twice" % (where, arg.name))
        arg_names.add(arg.name)
        if arg.fmt == "O":
            if arg.ctype not in description.types:
                raise DescriptionError("%s: unknown class %s" % (where, arg.ctype))
        elif ARG_CTYPES.get(arg.fmt) != arg.ctype:
            raise DescriptionError("%s: format %s does not match C type %s" % (where, arg.fmt, arg.ctype))
        if arg.optional:
            seen_optional = True
        elif seen_optional:
            raise DescriptionError("%s: required argument after optional" % where)
    if func.call is None:
        raise DescriptionError("%s: no call" % where)
    for ref in re.findall(r"\$(\w+)", func.call):
        if ref == OUT_NAME and func.return_kind == "new":
            continue
        if ref not in arg_names:
            raise DescriptionError("%s: call uses unknown argument $%s" % (where, ref))
    if func.return_kind not in RETURN_KINDS:
        raise DescriptionError("%s: bad return kind %s" % (where, func.return_kind))
    if (func.return_kind in CLASS_RETURNS) != (func.return_class is not None):
        raise DescriptionError("%s: 'return %s' needs exactly one class" % (where, func.return_kind))
    if func.return_class and func.return_class not in description.types:
        raise DescriptionError("%s: unknown class %s" % (where, func.return_class))
    if func.init is not None and not (func.return_kind == "new" and "$" + OUT_NAME in func.call):
        raise DescriptionError("%s: 'init' needs 'return new' and $%s in the call" % (where, OUT_NAME))


def validate_variable(variable, description):
    where = "%s:%d (%s)" % (variable.source, variable.line, variable.name)
    if variable.cls not in description.types:
        raise DescriptionError("%s: unknown class %s" % (where, variable.cls))
    if variable.object is None:
        raise DescriptionError("%s: no object" % where)


def validate(description):
    names = set()
    for func in description.all_funcs():
        for table_name in func.table_names:
            if table_name in names:
                raise DescriptionError("%s:%d: name %s used twice" % (func.source, func.line, table_name))
            names.add(table_name)
        validate_func(func, description)
    variable_names = set()
    for variable in description.all_variables():
        if variable.name in variable_names:
            raise DescriptionError("%s:%d: variable %s used twice" % (variable.source, variable.line, variable.name))
        variable_names.add(variable.name)
        validate_variable(variable, description)


def load(interface_dir):
    description = Description()
    paths = sorted(
        os.path.join(interface_dir, name)
        for name in os.listdir(interface_dir)
        if name.endswith(".appc")
    )
    if not paths:
        raise DescriptionError("no .appc files in %s" % interface_dir)
    for path in paths:
        parse_file(path, description)
    validate(description)
    return description
