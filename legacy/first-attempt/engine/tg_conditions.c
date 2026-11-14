/* Conditions: TGCondition (701020), ConditionScript (47bd80), the condition handler interface and ConditionEventCreator (4420d0).
   A condition has a status and a list of handlers; a status change calls ConditionChanged on every handler (TGCondition::SetStatus 00701140). */
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

typedef struct TGCond { TGObject o; unsigned char status, active; int nh; void **handlers; PyObject *inst; int has_act, has_deact; } TGCond;
typedef struct CondHandler { TGObject o; TGCond **conds; int n; TGEvent *event; } CondHandler;     /* ConditionEventCreator */
static void *ptr_arg(PyObject *o) { void *p = NULL; if (!o || o == Py_None) return NULL; if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }
static TGCond *cnd(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x705) ? p : NULL; }
static CondHandler *hdl(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x8038) ? p : NULL; }

static void handler_changed(void *h, TGCond *c)                          /* ConditionChanged (vslot 0): the event creator posts a copy of its event (004421a0) */
{
    CondHandler *ch = hdl(h);
    extern void *eng_TGEvent_Duplicate(void *); extern void tg_ai_condition_changed(void *ai, void *cond);
    if (h && tg_is_ours(h) && tg_is_type(h, 0x1000e)) { tg_ai_condition_changed(h, c); return; }   /* ConditionalAI (tg_ai.c) */
    if (ch && ch->event) { TGEvent *d = eng_TGEvent_Duplicate(ch->event); if (d) tg_add_event(d); }
}
static void cond_set_status(TGCond *c, int flag)                          /* 00701140 */
{
    int i;
    if (c->status == (unsigned char)(flag != 0)) return;
    c->status = flag != 0;
    for (i = 0; i < c->nh; i++) handler_changed(c->handlers[i], c);
}
static void cond_add_handler(TGCond *c, void *h) { c->handlers = realloc(c->handlers, (c->nh + 1) * sizeof *c->handlers); c->handlers[c->nh++] = h; }
static void cond_remove_handler(TGCond *c, void *h)                       /* 007011f0: removes every match; a condition without handlers deletes itself */
{
    int i = 0;
    while (i < c->nh) { if (c->handlers[i] == h) { memmove(c->handlers + i, c->handlers + i + 1, (c->nh - i - 1) * sizeof *c->handlers); c->nh--; } else i++; }
    if (c->nh < 1) { Py_XDECREF(c->inst); c->inst = NULL; c->o.dead = 1; }
}

void *eng_TGCondition_Cast(PyObject *o) { return cnd(ptr_arg(o)); }
long eng_TGCondition_GetStatus(void *p) { TGCond *c = cnd(p); return c ? c->status : 0; }
void eng_TGCondition_SetStatus(void *p, int f) { TGCond *c = cnd(p); if (c) cond_set_status(c, f); }
long eng_TGCondition_IsActive(void *p) { TGCond *c = cnd(p); return c ? c->active : 0; }
void eng_TGCondition_AddHandler(void *p, void *h) { TGCond *c = cnd(p); if (c) cond_add_handler(c, h); }
void eng_TGCondition_RemoveHandler(void *p, void *h) { TGCond *c = cnd(p); if (c) cond_remove_handler(c, h); }

static void script_call_method(TGCond *c, const char *name)
{
    PyObject *f = PyObject_GetAttrString(c->inst, (char *)name), *r;
    if (!f) { PyErr_Clear(); return; }
    r = PyEval_CallObject(f, NULL); Py_DECREF(f); Py_XDECREF(r);
    if (PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); if (!strcmp(name, "Activate")) c->has_act = 0; else c->has_deact = 0; }
}
static void cond_activate(TGCond *c, int on)                              /* ConditionScript::Activate 0047be20 / Deactivate 0047be80 */
{
    c->active = on ? 1 : 0;
    if (c->inst && (on ? c->has_act : c->has_deact)) script_call_method(c, on ? "Activate" : "Deactivate");
}
void eng_TGCondition_SetActive(void *p) { TGCond *c = cnd(p); if (c) cond_activate(c, 1); }
void eng_TGCondition_SetInactive(void *p) { TGCond *c = cnd(p); if (c) cond_activate(c, 0); }
void *eng_ConditionScript_Cast(PyObject *o) { void *p = ptr_arg(o); return tg_is_ours(p) && tg_is_type(p, 0x8037) ? p : NULL; }
void eng_ConditionScript_SetActive(void *p) { eng_TGCondition_SetActive(p); }
void eng_ConditionScript_SetInactive(void *p) { eng_TGCondition_SetInactive(p); }
PyObject *eng_ConditionScript_GetConditionScript(void *p) { TGCond *c = cnd(p); if (!c || !c->inst) { Py_INCREF(Py_None); return Py_None; } Py_INCREF(c->inst); return c->inst; }

/* ConditionScript.Create(module, class, *args): builds module.class(pCondition, *args) and attaches it (py_ConditionScript_Create, SetPythonInstance 0047bee0) */
PyObject *eng_ConditionScript_Create(PyObject *args)
{
    int n = PyTuple_Size(args), i; TGCond *c; PyObject *m, *cls, *self, *cargs, *inst;
    if (n < 2 || !PyString_Check(PyTuple_GetItem(args, 0)) || !PyString_Check(PyTuple_GetItem(args, 1))) { Py_INCREF(Py_None); return Py_None; }
    c = tg_new("ConditionScript", sizeof(TGCond), 0); c->active = 1;
    m = PyImport_ImportModule(PyString_AsString(PyTuple_GetItem(args, 0)));
    if (!m) { PyErr_Print(); PyErr_Clear(); return appc_pyobj(c, "ConditionScript"); }
    cls = PyObject_GetAttrString(m, PyString_AsString(PyTuple_GetItem(args, 1))); Py_DECREF(m);
    if (!cls) { PyErr_Print(); PyErr_Clear(); return appc_pyobj(c, "ConditionScript"); }
    self = appc_pyobj(c, "ConditionScript");
    cargs = PyTuple_New(n - 1); PyTuple_SetItem(cargs, 0, self);                   /* (pCodeCondition, *extra) */
    for (i = 2; i < n; i++) { PyObject *x = PyTuple_GetItem(args, i); Py_INCREF(x); PyTuple_SetItem(cargs, i - 1, x); }
    inst = PyEval_CallObject(cls, cargs); Py_DECREF(cls); Py_DECREF(cargs);
    if (!inst) { PyErr_Print(); PyErr_Clear(); return appc_pyobj(c, "ConditionScript"); }
    c->inst = inst;                                                                 /* reference kept (SetPythonInstance) */
    { PyObject *a = PyObject_GetAttrString(inst, "Activate"); c->has_act = a != NULL; Py_XDECREF(a); PyErr_Clear();
      a = PyObject_GetAttrString(inst, "Deactivate"); c->has_deact = a != NULL; Py_XDECREF(a); PyErr_Clear(); }
    return appc_pyobj(c, "ConditionScript");
}

/* ---- condition handlers: ConditionEventCreator ---- */
void *eng_new_ConditionEventCreator(long id) { return tg_new("ConditionEventCreator", sizeof(CondHandler), id); }
void eng_delete_ConditionEventCreator(void *p) { if (hdl(p)) ((TGObject *)p)->dead = 1; }
void eng_ConditionEventCreator_SetEvent(void *p, PyObject *evo)
{
    CondHandler *h = hdl(p); void *e = ptr_arg(evo);
    if (!h) return;
    h->event = e && tg_is_ours(e) && tg_is_type(e, 0x101) ? e : NULL; if (h->event) h->event->refcount++;
}
void eng_ConditionEventCreator_ConditionChanged(void *p, void *c) { handler_changed(p, cnd(c)); }
void eng_TGConditionHandler_ConditionChanged(void *p, void *c) { handler_changed(p, cnd(c)); }
void eng_TGConditionHandler_AddCondition(void *p, void *cp)            /* addConditionToList: append, and register as the condition's handler */
{
    CondHandler *h = hdl(p); TGCond *c = cnd(cp);
    if (c && p && tg_is_ours(p) && tg_is_type(p, 0x1000e)) { extern void tg_ai_add_condition(void *, void *); tg_ai_add_condition(p, c); cond_add_handler(c, p); return; }
    if (!h || !c) return;
    h->conds = realloc(h->conds, (h->n + 1) * sizeof *h->conds); h->conds[h->n++] = c;
    cond_add_handler(c, h);
}
void eng_TGConditionHandler_RemoveCondition(void *p, void *cp)         /* removeConditionFromList */
{
    CondHandler *h = hdl(p); TGCond *c = cnd(cp); int i;
    if (!h || !c) return;
    for (i = 0; i < h->n; i++) if (h->conds[i] == c) { memmove(h->conds + i, h->conds + i + 1, (h->n - i - 1) * sizeof *h->conds); h->n--; break; }
    cond_remove_handler(c, h);
}

int tg_condition_status(void *c) { TGCond *x = cnd(c); return x ? x->status : 0; }
PyObject *tg_condition_script(void *c) { TGCond *x = cnd(c); return x && tg_is_type(c, 0x8037) ? x->inst : NULL; }   /* ConditionScript +0x18 (borrowed) */
