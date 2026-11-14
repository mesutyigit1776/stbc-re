/* Appc runtime. Pointer string format follows SWIG 1.1 as used by the original binary: "_<hex>_p_<Class>".
   64-bit note: the original used 8 hex digits; here the digit count follows the pointer size. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "appc_runtime.h"

static const struct appc_class *class_of(const char *cls)
{
    const struct appc_class *c;
    for (c = appc_classes; c->name; c++) if (strcmp(c->name, cls) == 0) return c;
    return NULL;
}

static int is_a_depth(const char *derived, const char *target, int depth)
{
    const struct appc_class *c; int i;
    if (!derived || depth > 32) return 0;
    if (strcmp(derived, target) == 0) return 1;
    c = class_of(derived);
    if (!c) return 0;
    for (i = 0; i < 4; i++) if (c->bases[i] && is_a_depth(c->bases[i], target, depth + 1)) return 1;
    return 0;
}
static int is_a(const char *derived, const char *target) { return is_a_depth(derived, target, 0); }

static int type_error(const char *type)
{
    char msg[200];
    sprintf(msg, "Type error. Expected _p_%s", type ? type : "pointer");
    PyErr_SetString(PyExc_TypeError, msg);
    return 0;
}

#define MEMO_SIZE 65536
static struct { void *ptr; char type[48]; } memo[MEMO_SIZE];
void appc_memo_type(void *ptr, const char *type)
{
    unsigned h = (unsigned)(((unsigned long)ptr >> 3) * 2654435761u) & (MEMO_SIZE - 1);
    if (!ptr || !type || !strcmp(type, "void")) return;
    memo[h].ptr = ptr; if (type != memo[h].type) { strncpy(memo[h].type, type, 47); memo[h].type[47] = 0; }   /* type can be this entry (from appc_memo_get) */
}
/* the engine globals (g_kTGActionManager ...) are not engine objects with a class; their types are kept in a table that never loses an entry,
   because the cache below is direct-mapped and another address with the same hash would push a global's type out */
static struct { void *ptr; const char *type; } pinned[256]; static int npinned;
static void appc_pin_type(void *ptr, const char *type)
{
    int i;
    if (!ptr || !type) return;
    for (i = 0; i < npinned; i++) if (pinned[i].ptr == ptr) { pinned[i].type = type; return; }
    if (npinned < 256) { pinned[npinned].ptr = ptr; pinned[npinned].type = type; npinned++; }
}
const char *appc_memo_get(void *ptr)
{
    int i;
    for (i = 0; i < npinned; i++) if (pinned[i].ptr == ptr) return pinned[i].type;
    unsigned h = (unsigned)(((unsigned long)ptr >> 3) * 2654435761u) & (MEMO_SIZE - 1);
    return memo[h].ptr == ptr ? memo[h].type : NULL;
}

int appc_to_ptr(PyObject *obj, void **out, const char *type)
{
    PyObject *s = NULL; char *str; unsigned long v = 0; int own = 0;
    if (obj == NULL) return type_error(type);
    if (obj == Py_None) { *out = NULL; return 1; }
    if (!PyString_Check(obj)) {
        s = PyObject_GetAttrString(obj, "this");
        if (s == NULL) { PyErr_Clear(); return type_error(type); }
        if (!PyString_Check(s)) { Py_DECREF(s); return type_error(type); }
        own = 1; obj = s;
    }
    str = PyString_AsString(obj);
    if (str[0] == '_') {
        char *p = str + 1;
        while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) { v = v * 16 + (*p <= '9' ? *p - '0' : *p - 'a' + 10); p++; }
        if (*p == '_' && p[1] == 'p' && p[2] == '_' && type && strcmp(type, "void") != 0 && !is_a(p + 3, type)) {
            if (own) Py_DECREF(s);
            return type_error(type);
        }
        *out = (void *)v;
        if (*p == '_' && p[1] == 'p' && p[2] == '_') appc_memo_type((void *)v, p + 3);
        if (own) Py_DECREF(s);
        return 1;
    }
    if (strcmp(str, "NULL") == 0) { *out = NULL; if (own) Py_DECREF(s); return 1; }
    if (own) Py_DECREF(s);
    return type_error(type);
}

PyObject *appc_from_ptr(void *ptr, const char *type)
{
    char buf[256];
    if (ptr == NULL) { Py_INCREF(Py_None); return Py_None; }
    appc_memo_type(ptr, type);
    sprintf(buf, "_%lx_p_%s", (unsigned long)ptr, type);
    return PyString_FromString(buf);
}

void appc_unimplemented(const char *name)
{
    static int count = 0;
    if (getenv("APPC_TRACE")) fprintf(stderr, "[Appc] unimplemented: %s\n", name);
    count++;
}


int appc_to_cstr(PyObject *obj, char **out)
{
    char msg[160];
    if (obj == NULL || obj == Py_None) { *out = NULL; return 1; }
    if (PyString_Check(obj)) { *out = PyString_AsString(obj); return 1; }
    sprintf(msg, "Expected None or string, found %s", obj->ob_type->tp_name);
    PyErr_SetString(PyExc_TypeError, msg);
    return 0;
}

int tg_is_ours(void *p);
PyObject *appc_pyobj(void *ptr, const char *type)
{
    PyObject *app, *cls, *s, *args, *r; char name[96];
    if (!ptr) { Py_INCREF(Py_None); return Py_None; }
    /* an engine object knows its class; the memo is a cache keyed by address and goes stale when an address is used again */
    if (!type && tg_is_ours(ptr) && ((struct { const struct { const char *name; } *cls; } *)ptr)->cls) type = ((struct { const struct { const char *name; } *cls; } *)ptr)->cls->name;
    if (!type) type = appc_memo_get(ptr);
    if (!type) type = "TGObject";
    app = PyImport_ImportModule("App"); if (!app) return NULL;
    sprintf(name, "%sPtr", type);
    cls = PyObject_GetAttrString(app, name);
    if (!cls) {                                    /* no wrapper class of that name: the cached type, then the plain object */
        const char *m; PyErr_Clear();
        m = appc_memo_get(ptr);
        if (m && strcmp(m, type)) { type = m; sprintf(name, "%sPtr", type); cls = PyObject_GetAttrString(app, name); if (!cls) PyErr_Clear(); }
        if (!cls) { type = "TGObject"; cls = PyObject_GetAttrString(app, "TGObjectPtr"); }
    }
    Py_DECREF(app);
    if (!cls) return NULL;
    s = appc_from_ptr(ptr, type);
    args = Py_BuildValue("(O)", s); Py_DECREF(s);
    r = PyEval_CallObject(cls, args);
    Py_DECREF(args); Py_DECREF(cls);
    return r;
}

void *appc_placeholder(void) { return calloc(1, 512); }

/* Appc.globals: attribute access returns the engine global as a SWIG pointer */
typedef struct { PyObject_HEAD } GlobalsObject;
static PyObject *globals_getattr(GlobalsObject *self, char *name)
{
    const struct appc_global *g;
    for (g = appc_globals; g->name; g++)
        if (strcmp(g->name, name) == 0) {
            if (g->kind == 1) return appc_from_ptr((void *)(unsigned long)g->num, g->type);
            if (g->kind == 2) return PyFloat_FromDouble(g->num);
            if (g->kind == 3) return PyInt_FromLong((long)g->num);
            appc_pin_type(eng_global_get(g), g->type);
            return appc_from_ptr(eng_global_get(g), g->type);
        }
    PyErr_SetString(PyExc_AttributeError, name);
    return NULL;
}
static PyTypeObject GlobalsType = {
    PyObject_HEAD_INIT(&PyType_Type) 0, "appc_globals", sizeof(GlobalsObject), 0,
    0, 0, (getattrfunc)globals_getattr, 0, 0, 0, 0, 0, 0, 0,
};

/* Appc.attach_extras(App): attach the hand-written methods to the App shadow classes, like addPythonMethodToClass (0x6fa...)
   after the first `import App`. The engine calls this right after importing App. Each becomes an unbound method of the class. */
static PyObject *appc_attach_extras(PyObject *self, PyObject *args)
{
    PyObject *app, *appc, *d; const struct appc_extra *e; int n = 0;
    if (!PyArg_ParseTuple(args, "O:attach_extras", &app)) return NULL;
    appc = PyImport_ImportModule("Appc"); if (!appc) return NULL;
    d = PyModule_GetDict(appc);
    for (e = appc_extras; e->cls; e++) {
        PyObject *cls = PyObject_GetAttrString(app, (char *)e->cls), *fn = PyDict_GetItemString(d, (char *)e->func), *m;
        if (!cls) { PyErr_Clear(); continue; }
        if (!fn) { Py_DECREF(cls); continue; }
        m = PyMethod_New(fn, NULL, cls);
        if (m) { PyObject_SetAttrString(cls, (char *)e->method, m); Py_DECREF(m); n++; }
        Py_DECREF(cls);
    }
    Py_DECREF(appc);
    return PyInt_FromLong(n);
}

void engine_register_host(PyObject *dict) __attribute__((weak));
void engine_register_host(PyObject *dict) { }

void initAppc(void)
{
    PyObject *m, *d, *g; const PyMethodDef *f;
    m = Py_InitModule("Appc", (PyMethodDef *)appc_methods);
    d = PyModule_GetDict(m);
    appc_install_consts(d);
    engine_register_host(d);
    { static PyMethodDef ae = {"attach_extras", appc_attach_extras, 1}; PyObject *f = PyCFunction_New(&ae, NULL); PyDict_SetItemString(d, "attach_extras", f); Py_DECREF(f); }
    g = PyObject_NEW(PyObject, &GlobalsType);
    PyDict_SetItemString(d, "globals", g);
    (void)f;
}
