/* Generic property store: values kept by (object pointer, property name).
   Used by the generated accessors of pure data classes (the *Property family, sound settings, ...), see re/gen_props.py.
   The engine code that consumes the data reads it back with the prop_get_* functions. */
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

struct pv { char *key; int kind; union { long i; double d; char *s; void *p; PyObject *o; } v; struct pv *next; };
struct pn { void *obj; struct pv *list; struct pn *next; };
#define PTAB 8191
static struct pn *ptab[PTAB];
static struct pv *find(void *obj, const char *key, int create)
{
    struct pn *n; struct pv *v; size_t h = (((size_t)obj >> 4) * 2654435761u) % PTAB;
    for (n = ptab[h]; n; n = n->next) if (n->obj == obj) break;
    if (!n) { if (!create) return NULL; n = calloc(1, sizeof *n); n->obj = obj; n->next = ptab[h]; ptab[h] = n; }
    for (v = n->list; v; v = v->next) if (!strcmp(v->key, key)) return v;
    if (!create) return NULL;
    v = calloc(1, sizeof *v); v->key = strdup(key); v->next = n->list; n->list = v; return v;
}
void prop_set_i(void *o, const char *k, long x) { struct pv *v = find(o, k, 1); v->kind = 'i'; v->v.i = x; }
void prop_set_d(void *o, const char *k, double x) { struct pv *v = find(o, k, 1); v->kind = 'd'; v->v.d = x; }
void prop_set_s(void *o, const char *k, const char *x) { struct pv *v = find(o, k, 1); if (v->kind == 's') free(v->v.s); v->kind = 's'; v->v.s = x ? strdup(x) : NULL; }
void prop_set_p(void *o, const char *k, void *x) { struct pv *v = find(o, k, 1); v->kind = 'p'; v->v.p = x; }
void prop_set_o(void *o, const char *k, PyObject *x) { struct pv *v = find(o, k, 1); if (x) Py_INCREF(x); if (v->kind == 'o') Py_XDECREF(v->v.o); v->kind = 'o'; v->v.o = x; }
long prop_get_i(void *o, const char *k) { struct pv *v = find(o, k, 0); return v ? (v->kind == 'd' ? (long)v->v.d : v->v.i) : 0; }
double prop_get_d(void *o, const char *k) { struct pv *v = find(o, k, 0); return v ? (v->kind == 'i' ? (double)v->v.i : v->v.d) : 0.0; }
const char *prop_get_s(void *o, const char *k) { struct pv *v = find(o, k, 0); return v && v->kind == 's' ? v->v.s : NULL; }
void *prop_get_p(void *o, const char *k) { struct pv *v = find(o, k, 0); return v && v->kind == 'p' ? v->v.p : NULL; }
PyObject *prop_get_o(void *o, const char *k) { struct pv *v = find(o, k, 0); PyObject *r = v && v->kind == 'o' && v->v.o ? v->v.o : Py_None; Py_INCREF(r); return r; }
int prop_has(void *o, const char *k) { return find(o, k, 0) != NULL; }

