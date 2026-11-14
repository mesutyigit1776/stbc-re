/* Appc runtime support: SWIG-style pointer strings, class compatibility, engine globals. */
#ifndef APPC_RUNTIME_H
#define APPC_RUNTIME_H
#include "Python.h"

struct appc_class  { const char *name; const char *bases[4]; };
struct appc_extra  { const char *cls; const char *method; const char *func; };
extern const struct appc_extra appc_extras[];
struct appc_global { const char *name; const char *type; int kind; unsigned long addr; unsigned size; double num; };
extern const struct appc_class  appc_classes[];
extern const struct appc_global appc_globals[];
extern const PyMethodDef appc_methods[];
extern const int appc_method_count;
void appc_install_consts(PyObject *dict);

/* None -> NULL; instance -> its "this" attribute; string "_<hex>_p_<Class>" -> pointer (type must be the class or a derived class). */
int  appc_to_ptr(PyObject *obj, void **out, const char *type);
/* NULL -> None; else the string "_<hex>_p_<Class>" */
PyObject *appc_from_ptr(void *ptr, const char *type);
/* None -> NULL; string -> its C string (borrowed); anything else -> TypeError like the original ("Expected None or string") */
int appc_to_cstr(PyObject *obj, char **out);
void appc_unimplemented(const char *name);
/* opaque zeroed object returned by constructor stubs */
void *appc_placeholder(void);
/* The engine provides this; returns the address of an engine global (g_kEventManager ...) or NULL. */
/* kind 0 only: storage of the global object (shared by globals with the same original address). */
void *eng_global_get(const struct appc_global *g);
/* Remember the SWIG type of every pointer that crosses the boundary, so that the engine can rebuild a shadow instance later
   (the original asks the object itself through vtable slot 0x20). */
void appc_memo_type(void *ptr, const char *type);
const char *appc_memo_get(void *ptr);
/* App.<Type>Ptr("_hex_p_<Type>") for a pointer; falls back to TGObjectPtr. New reference, or NULL with an exception set. */
PyObject *appc_pyobj(void *ptr, const char *type);
/* The engine registers its host functions (engine_init, engine_tick, ...) into the Appc module dictionary. */
void engine_register_host(PyObject *dict);
void initAppc(void);
#endif
