/* AI objects (ArtificialIntelligence family) and their execution.
   PlainAI::SetScriptModule 0048cd50: imports "AI.PlainAI.<module>" and calls <module>.<module>(PlainAI) with format "(O)", result "O";
   the result is the script instance (+0x28). PreprocessingAI keeps (instance, method name) and a contained AI.
   Approximations (marked): no Update/Activate cycle, no focus, no dormant/active status; ids count from 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "appc_runtime.h"
void *eng_new_TGPythonInstanceWrapper(void);
void eng_TGPythonInstanceWrapper_SetPyWrapper(void *p, PyObject *o);

extern double tg_game_time;

typedef struct AI {
    TGObject o; int kind; void *ship; char *name, *module; PyObject *script, *pre_inst; char *pre_method;
    struct AI *contained, **kids; int nkids; int *prio; int paused, interruptable, loop_count; long id;
    int active, focus, status, calls_activate, cur, *done, reset_if_int, double_check, skip_dormant, built;   /* +0x20 active, +0x21 focus, +0x30 status */
    double next_update; int native, conserve; PyObject *ext; void **conds; int nconds; PyObject *eval, *eval_cache; struct Block *blocks; int nblocks;
} AI;
typedef struct Block { char *name, *func; char **deps; PyObject **objs; int ndeps; AI *ai; } Block;
enum { K_PLAIN, K_PRIORITY, K_SEQUENCE, K_RANDOM, K_PRE, K_COND, K_BUILDER };
static const char *kind_name[] = {"PlainAI", "PriorityListAI", "SequenceAI", "RandomAI", "PreprocessingAI", "ConditionalAI", "BuilderAI"};
static AI **all; static long nall;
static PyObject *self_obj(struct AI *a);
static AI *ai(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x1000e) ? p : NULL; }
static AI *ai_new(int kind, void *ship, const char *name)
{
    AI *a = tg_new_plain("ArtificialIntelligence", sizeof(AI)); a->kind = kind; a->ship = ship; a->name = strdup(name ? name : ""); a->interruptable = 1; a->calls_activate = 1; a->loop_count = 1; a->cur = -1;
    all = realloc(all, (nall + 1) * sizeof *all); all[nall++] = a; a->id = nall; return a;
}
static void add_kid(AI *a, AI *k, int prio) { a->kids = realloc(a->kids, (a->nkids + 1) * sizeof *a->kids); a->prio = realloc(a->prio, (a->nkids + 1) * sizeof *a->prio); a->kids[a->nkids] = k; a->prio[a->nkids++] = prio; }
static void del_kid(AI *a, int i) { if (i < 0 || i >= a->nkids) return; memmove(a->kids + i, a->kids + i + 1, (a->nkids - i - 1) * sizeof *a->kids); memmove(a->prio + i, a->prio + i + 1, (a->nkids - i - 1) * sizeof *a->prio); a->nkids--; }

void *eng_PlainAI_Create(void *ship, char *name) { return ai_new(K_PLAIN, ship, name); }
void *eng_PriorityListAI_Create(void *ship, char *name) { return ai_new(K_PRIORITY, ship, name); }
void *eng_SequenceAI_Create(void *ship, char *name) { return ai_new(K_SEQUENCE, ship, name); }
void *eng_RandomAI_Create(void *ship, char *name) { return ai_new(K_RANDOM, ship, name); }
void *eng_PreprocessingAI_Create(void *ship, char *name) { return ai_new(K_PRE, ship, name); }
void *eng_ConditionalAI_Create(void *ship, char *name) { return ai_new(K_COND, ship, name); }
void *eng_BuilderAI_Create(void *ship, char *name, char *mod) { AI *a = ai_new(K_BUILDER, ship, name); a->module = mod ? strdup(mod) : NULL; return a; }
void *eng_PreprocessingAI_Cast(void *p) { AI *a = ai(p); return a && a->kind == K_PRE ? a : NULL; }

void eng_PlainAI_SetScriptModule(void *p, char *mod)                          /* 0048cd50 */
{
    AI *a = ai(p); char path[256]; PyObject *m, *f, *self, *args, *r;
    if (!a || !mod) return;
    snprintf(path, sizeof path, "AI.PlainAI.%s", mod); free(a->module); a->module = strdup(path);
    if (!(m = PyImport_ImportModule(path))) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, mod); Py_DECREF(m); if (!f) { PyErr_Print(); PyErr_Clear(); return; }
    self = appc_pyobj(a, "PlainAI"); args = Py_BuildValue("(O)", self); Py_DECREF(self);
    r = PyEval_CallObject(f, args); Py_DECREF(args); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); return; }
    Py_XDECREF(a->script); a->script = r;
}
void eng_PlainAI_SetScriptInstance(void *p, PyObject *o) { AI *a = ai(p); if (a) { Py_XINCREF(o); Py_XDECREF(a->script); a->script = o; } }
PyObject *eng_PlainAI_GetScriptInstance(void *p) { AI *a = ai(p); PyObject *r = a && a->script ? a->script : Py_None; Py_INCREF(r); return r; }
void eng_PreprocessingAI_SetPreprocessingMethod(void *p, PyObject *inst, char *meth)
{   /* 0048e400: also sets instance.pCodeAI = this AI */
    AI *a = ai(p); if (!a) return; Py_XINCREF(inst); Py_XDECREF(a->pre_inst); a->pre_inst = inst; free(a->pre_method); a->pre_method = meth ? strdup(meth) : NULL;
    if (inst && inst != Py_None) { PyObject *s = self_obj(a); if (PyObject_SetAttrString(inst, "pCodeAI", s) < 0) PyErr_Clear(); Py_DECREF(s); }
    /* The stock SelectTarget (AI/Preprocessors.py) uses self.pEventHandler in UpdateTargetInfo and in its state pickling but never
       creates it. Every other handler class creates it in __init__, so the instance gets an event handler object here, once.
       Approximation: how the exe provides it is not decoded yet. */
    if (inst && inst != Py_None && !PyObject_HasAttrString(inst, "pEventHandler")) {
        void *w = eng_new_TGPythonInstanceWrapper();
        PyObject *wo = appc_from_ptr(w, "TGPythonInstanceWrapper");
        eng_TGPythonInstanceWrapper_SetPyWrapper(w, inst);
        if (wo && PyObject_SetAttrString(inst, "pEventHandler", wo) < 0) PyErr_Clear();
        Py_XDECREF(wo);
    }
    /* PreprocessingAI vtbl +0x34 0048eb20 swaps an AI for a native one when it is attached, by the instance's class name. Only
       ManagePower is native here (00486fa0; its Python Update is "Unused" and returns PS_DONE). AvoidObstacles, FireScript and
       SelectTarget also have native versions in the exe; their Python versions do the same work, so they stay in Python. */
    a->native = 0;
    if (inst && PyInstance_Check(inst) && !strcmp(PyString_AsString(((PyInstanceObject *)inst)->in_class->cl_name), "ManagePower")) {
        PyObject *v = PyObject_GetAttrString(inst, "bConservePower"); a->native = 1; a->conserve = v && PyObject_IsTrue(v); Py_XDECREF(v); PyErr_Clear(); }
}
void eng_PreprocessingAI_SetPreprocessingMethod_manual(void *p, PyObject *inst, char *meth) { eng_PreprocessingAI_SetPreprocessingMethod(p, inst, meth); }
PyObject *eng_PreprocessingAI_GetPreprocessingInstance(void *p) { AI *a = ai(p); PyObject *r = a && a->pre_inst ? a->pre_inst : Py_None; Py_INCREF(r); return r; }
void eng_PreprocessingAI_SetContainedAI(void *p, void *c) { AI *a = ai(p); if (a) a->contained = ai(c); }
void *eng_PreprocessingAI_GetContainedAI(void *p) { AI *a = ai(p); return a ? a->contained : NULL; }
void eng_PreprocessingAI_ForceDormantStatus(void *p, int v) { }
void eng_PreprocessingAI_ForceStatusChange(void *p, int v) { }
void eng_ConditionalAI_SetContainedAI(void *p, void *c) { AI *a = ai(p); if (a) a->contained = ai(c); }
void *eng_ConditionalAI_GetContainedAI(void *p) { AI *a = ai(p); return a ? a->contained : NULL; }
void eng_PriorityListAI_AddAI(void *p, void *c, int prio) { AI *a = ai(p), *k = ai(c); int i; if (!a || !k) return; for (i = 0; i < a->nkids && a->prio[i] <= prio; i++) ; add_kid(a, k, prio); memmove(a->kids + i + 1, a->kids + i, (a->nkids - 1 - i) * sizeof *a->kids); memmove(a->prio + i + 1, a->prio + i, (a->nkids - 1 - i) * sizeof *a->prio); a->kids[i] = k; a->prio[i] = prio; }
void eng_PriorityListAI_RemoveAIByPriority(void *p, int prio) { AI *a = ai(p); int i; if (a) for (i = 0; i < a->nkids; i++) if (a->prio[i] == prio) { del_kid(a, i); return; } }
void eng_PriorityListAI_RemoveAI(void *p, void *c) { AI *a = ai(p); int i; if (a) for (i = 0; i < a->nkids; i++) if (a->kids[i] == c) { del_kid(a, i); return; } }
void eng_SequenceAI_AddAI(void *p, void *c) { AI *a = ai(p), *k = ai(c); if (a && k) add_kid(a, k, a->nkids); }
void eng_SequenceAI_RemoveAI(void *p, void *c) { eng_PriorityListAI_RemoveAI(p, c); }
void eng_SequenceAI_RemoveAIByIndex(void *p, int i) { AI *a = ai(p); if (a) del_kid(a, i); }
void *eng_SequenceAI_GetAI(void *p, int i) { AI *a = ai(p); return a && i >= 0 && i < a->nkids ? a->kids[i] : NULL; }
void eng_SequenceAI_SetLoopCount(void *p, int n) { AI *a = ai(p); if (a) a->loop_count = n; }
long eng_SequenceAI_GetLoopCount(void *p) { AI *a = ai(p); return a ? a->loop_count : 0; }
void eng_RandomAI_AddAI(void *p, void *c) { eng_SequenceAI_AddAI(p, c); }
void *eng_ArtificialIntelligence_GetObject(void *p) { AI *a = ai(p); return a ? a->ship : NULL; }
void *eng_ArtificialIntelligence_GetShip(void *p) { extern void *eng_ShipClass_Cast(PyObject *); AI *a = ai(p); return a && a->ship && tg_is_ours(a->ship) && tg_is_type(a->ship, 0x8008) ? a->ship : NULL; }
void eng_ArtificialIntelligence_Pause(void *p) { AI *a = ai(p); if (a) a->paused = 1; }
void eng_ArtificialIntelligence_Unpause(void *p) { AI *a = ai(p); if (a) a->paused = 0; }
long eng_ArtificialIntelligence_IsPaused(void *p) { AI *a = ai(p); return a && a->paused; }
void eng_ArtificialIntelligence_SetInterruptable(void *p, int v) { AI *a = ai(p); if (a) a->interruptable = v != 0; }
static int is_interruptable(AI *a);
long eng_ArtificialIntelligence_IsInterruptable(void *p) { AI *a = ai(p); return a && is_interruptable(a); }
long eng_ArtificialIntelligence_GetID(void *p) { AI *a = ai(p); return a ? a->id : 0; }
const char *eng_ArtificialIntelligence_GetName(void *p) { AI *a = ai(p); return a ? a->name : NULL; }
static void *ai_by_id(int id) { return id > 0 && id <= nall ? all[id - 1] : NULL; }
PyObject *eng_ArtificialIntelligence_GetAIByID(int id)                 /* 00470dd0: the AI's own shadow object (vslot 0x3c), None when not found */
{ AI *a = ai_by_id(id); if (!a) { Py_INCREF(Py_None); return Py_None; } return appc_pyobj(a, kind_name[a->kind]); }
long eng_ArtificialIntelligence_LogAITree(char *f, int v) { return 0; }
static void tree(AI *a, PyObject *l) { int i; PyObject *o = appc_pyobj(a, kind_name[a->kind]); PyList_Append(l, o); Py_DECREF(o); if (a->contained) tree(a->contained, l); for (i = 0; i < a->nkids; i++) tree(a->kids[i], l); }
PyObject *eng_ArtificialIntelligence_GetAllAIsInTree(void *p) { AI *a = ai(p); PyObject *l = PyList_New(0); if (a) tree(a, l); return l; }
PyObject *eng_ArtificialIntelligence_GetFocusAIs(void *p) { return PyList_New(0); }
/* External functions: name -> info dict. RegisterExternalFunction 00470aa0 keeps one dict per name; the ConditionalAI's override 00479300
   keeps a list of dicts per name (one per condition that registered it, from the condition's RegisterExternalFunctions when it is added).
   CallExternalFunction: base 00470d30 returns 1; PlainAI 0048d070 calls script.<dict["Name"]>(args); ConditionalAI 004794d0 calls, for each
   dict, the script of the condition whose id is dict["CodeID"]: script.<dict["FunctionName"]>(args). */
void eng_ArtificialIntelligence_RegisterExternalFunction(void *p, char *n, PyObject *f)
{
    AI *a = ai(p); PyObject *l;
    if (!a || !n || !f) return;
    if (!a->ext) a->ext = PyDict_New();
    if (a->kind != K_COND) { PyDict_SetItemString(a->ext, n, f); return; }
    if (!(l = PyDict_GetItemString(a->ext, n))) { l = PyList_New(0); PyDict_SetItemString(a->ext, n, l); Py_DECREF(l); }
    PyList_Append(l, f);
}
void eng_ArtificialIntelligence_UnregisterExternalFunction(void *p, char *n, PyObject *f) { AI *a = ai(p); if (a && a->ext && n && PyDict_GetItemString(a->ext, n)) PyDict_DelItemString(a->ext, n); }
static int call_named(PyObject *inst, PyObject *name, PyObject *arg)
{
    PyObject *f, *r;
    if (!inst || !name || !PyString_Check(name) || !(f = PyObject_GetAttr(inst, name))) { PyErr_Clear(); return 0; }
    r = PyObject_CallFunction(f, "(O)", arg ? arg : Py_None); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); return 0; }
    Py_DECREF(r); return 1;
}
long eng_ArtificialIntelligence_CallExternalFunction(void *p, char *n, PyObject *arg)
{
    extern PyObject *tg_condition_script(void *); AI *a = ai(p); PyObject *e; int i, j, ok = 1;
    if (!a) return 0;
    e = a->ext && n ? PyDict_GetItemString(a->ext, n) : NULL;
    if (a->kind == K_PLAIN) return e && PyDict_Check(e) ? call_named(a->script, PyDict_GetItemString(e, "Name"), arg) : 0;
    if (a->kind != K_COND || !e || !PyList_Check(e)) return 1;
    for (i = 0; i < PyList_Size(e); i++) {
        PyObject *d = PyList_GetItem(e, i), *id = PyDict_Check(d) ? PyDict_GetItemString(d, "CodeID") : NULL, *fn = PyDict_Check(d) ? PyDict_GetItemString(d, "FunctionName") : NULL; int hit = 0;
        if (!id || !PyInt_Check(id)) continue;
        for (j = 0; j < a->nconds; j++) if (((TGObject *)a->conds[j])->id == (unsigned)PyInt_AsLong(id) && tg_condition_script(a->conds[j])) hit = call_named(tg_condition_script(a->conds[j]), fn, arg);
        ok &= hit;
    }
    return ok;
}

/* ================= execution =================
   Status values: US_ACTIVE 0, US_DONE 1, US_DORMANT 2, US_INVALID 3. Every AI has active/focus flags; SetActive/GotFocus are called before
   an Update, LostFocus when another AI takes over. Behavior per kind, from stbc.exe:
   - PlainAI::Update 0048d100: when its next update time has come, status = script.Update(); unless DONE, next update = now +
     script.GetNextUpdateTime(). SetActive 0048d360 calls script.Activate() until the script calls StopCallingActivate. GotFocus 0048d4a0
     resets the update time and calls script.GotFocus(); LostFocus calls script.LostFocus(). IsDormant is always false.
   - PriorityListAI::Update 00490340: in priority order, skip finished entries; the current AI keeps running unless it is interruptable and
     a higher entry is not dormant; ACTIVE stops the walk (lower entries become inactive), DONE marks the entry finished, DORMANT continues.
     Result: ACTIVE, else DORMANT if any entry was dormant, else DONE.
   - SequenceAI::Update 00492d00: runs the current AI; on DONE (or DORMANT with SkipDormant) it moves to the next, looping LoopCount times
     (-1 = forever); DoubleCheckAllDone runs one more pass at the end.
   - ConditionalAI 00479870: the status comes from the evaluation function called with the conditions' states (cached per combination,
     ComputeStatusFromTable 00479bb0); ACTIVE runs the contained AI, otherwise that status is returned (the contained AI keeps running while
     it is active and not interruptable).
   - PreprocessingAI::Update 0048ea30: instance.method(dt) (CallPreprocessingMethod 0048ece0, honoring the instance's GetNextUpdateTime)
     gives PS_NORMAL (run the contained AI), PS_SKIP_ACTIVE (ACTIVE), PS_SKIP_DORMANT (DORMANT) or PS_DONE (DONE).
   - BuilderAI 00476b80: builds its blocks in the order they were added: func(ship, dependencies...) from its module; the last block is the
     contained AI; then it behaves like a PreprocessingAI without a method.
   - RandomAI: like a sequence in a random order (RandomAI::SelectNextAI 00491980).
   The roots are the AIs set on ships (PhysicsObjectClass::SetAI); every root is updated each frame (AIList::UpdateAI 004722d0).
   Approximations (marked): no time slicing between AIs (AIList::Update 004721b0 spreads them over frames); GetStatusInfo logging is off. */
static int ai_update(AI *a, double dt);
static PyObject *self_obj(struct AI *a) { return appc_pyobj(a, kind_name[a->kind]); }
static int call_status(PyObject *inst, const char *meth, PyObject *args, int dflt)
{
    PyObject *f, *r; int v = dflt;
    if (!inst || !(f = PyObject_GetAttrString(inst, (char *)meth))) { PyErr_Clear(); return dflt; }
    r = PyEval_CallObject(f, args); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); return dflt; }
    if (PyInt_Check(r)) v = (int)PyInt_AsLong(r); else if (PyFloat_Check(r)) v = (int)PyFloat_AsDouble(r);
    Py_DECREF(r); return v;
}
static double call_float(PyObject *inst, const char *meth, double dflt)
{
    PyObject *f, *r; double v = dflt;
    if (!inst || !(f = PyObject_GetAttrString(inst, (char *)meth))) { PyErr_Clear(); return dflt; }
    r = PyEval_CallObject(f, NULL); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); return dflt; }
    if (PyFloat_Check(r)) v = PyFloat_AsDouble(r); else if (PyInt_Check(r)) v = (double)PyInt_AsLong(r);
    Py_DECREF(r); return v;
}
static void call_void(PyObject *inst, const char *meth) { PyObject *f, *r; if (!inst || !(f = PyObject_GetAttrString(inst, (char *)meth))) { PyErr_Clear(); return; } r = PyEval_CallObject(f, NULL); Py_DECREF(f); if (!r) { PyErr_Print(); PyErr_Clear(); } Py_XDECREF(r); }
static void set_active(AI *a)                                              /* vtbl +0x20: the AIs that would run next are activated too */
{
    int i; AI *c = NULL;
    a->active = 1;
    if (a->kind == K_PLAIN && a->script && a->calls_activate) call_void(a->script, "Activate");
    if (a->kind == K_PRE || a->kind == K_BUILDER) c = a->contained;                            /* 0048e710 */
    else if (a->kind == K_COND && a->status == 0) c = a->contained;                            /* 00479670 */
    else if ((a->kind == K_SEQUENCE || a->kind == K_RANDOM) && a->cur >= 0 && a->cur < a->nkids) c = a->kids[a->cur]; /* 00492b10 */
    else if (a->kind == K_PRIORITY)                                                            /* 00490140: entries up to the current one */
        for (i = 0; i < a->nkids; i++) { if (!(a->done && a->done[i]) && !a->kids[i]->active) set_active(a->kids[i]); if (i == a->cur) break; }
    if (c && !c->active) set_active(c);
}
static int is_interruptable(AI *a)                                         /* vtbl +4: own flag, and the running child agrees (0048ea10, 00490310, 00492cd0, 00479840) */
{
    AI *c = NULL;
    if (!a->interruptable) return 0;
    if (!a->active) return 1;
    if (a->kind == K_PRIORITY || a->kind == K_SEQUENCE || a->kind == K_RANDOM) c = a->cur >= 0 && a->cur < a->nkids ? a->kids[a->cur] : NULL;
    else if (a->kind != K_PLAIN) c = a->contained;
    return c ? is_interruptable(c) : 1;
}
static void set_inactive(AI *a)
{
    int i; a->active = 0;
    if (a->kind == K_PLAIN && a->script) call_void(a->script, "Deactivate");
    for (i = 0; i < a->nkids; i++) if (a->kids[i]->active) set_inactive(a->kids[i]);
    if (a->contained && a->contained->active) set_inactive(a->contained);
}
static void got_focus(AI *a) { a->focus = 1; if (a->kind == K_PLAIN) { a->next_update = tg_game_time; if (a->script) call_void(a->script, "GotFocus"); } }
static void lost_focus(AI *a)
{
    int i; a->focus = 0;
    if (a->kind == K_PLAIN && a->script) call_void(a->script, "LostFocus");
    for (i = 0; i < a->nkids; i++) if (a->kids[i]->focus) lost_focus(a->kids[i]);
    if (a->contained && a->contained->focus) lost_focus(a->contained);
}
static int is_dormant(AI *a)                                              /* vtbl +0x38 */
{
    int i;
    switch (a->kind) {
    case K_PLAIN: return 0;
    case K_SEQUENCE: case K_RANDOM: if (!a->nkids) return 0; for (i = 0; i < a->nkids; i++) if (!is_dormant(a->kids[i])) return 0; return 1;
    case K_PRIORITY: for (i = 0; i < a->nkids; i++) if (!(a->done && a->done[i]) && !is_dormant(a->kids[i])) return 0; return a->nkids > 0;
    case K_COND: if (a->status == 2) return 1; if (a->status == 0 && a->contained) return is_dormant(a->contained); return 0;
    default: return a->contained ? is_dormant(a->contained) : 0;
    }
}
static int ai_update_logged(AI *a, double dt);
static int run_child(AI *c, double dt) { if (!c->active) set_active(c); if (!c->focus) got_focus(c); return ai_update_logged(c, dt); }
static int update_plain(AI *a)
{
    int st = a->status;
    if (a->next_update <= tg_game_time) {
        PyObject *self = self_obj(a), *args = Py_BuildValue("()"); (void)self; Py_DECREF(self);
        st = call_status(a->script, "Update", args, 1); Py_DECREF(args);
        if (st != 1) a->next_update = tg_game_time + call_float(a->script, "GetNextUpdateTime", 0.0);
    }
    return st;
}
static int update_priority(AI *a, double dt)
{
    int i, dormant = 0; AI *cur = a->cur >= 0 && a->cur < a->nkids ? a->kids[a->cur] : NULL;
    if (!a->done) a->done = calloc(a->nkids + 1, sizeof(int));
    for (i = 0; i < a->nkids; i++) {
        AI *c = a->kids[i]; int st;
        if (a->done[i]) continue;
        if (c != cur) {
            if (cur && !is_interruptable(cur)) continue;
            if (is_dormant(c)) { dormant++; continue; }
            if (cur && cur->focus) lost_focus(cur);
            cur = c; a->cur = i;
        }
        st = run_child(c, dt);
        if (st == 0) { int k; for (k = i + 1; k < a->nkids; k++) if (a->kids[k]->active) set_inactive(a->kids[k]); return 0; }
        if (st == 1) a->done[i] = 1; else if (st == 2) dormant++;
        if (c->focus) lost_focus(c);
        if (cur == c) { cur = NULL; a->cur = -1; }
    }
    return dormant ? 2 : 1;
}
static int update_sequence(AI *a, double dt)
{
    int runs = 0, dormant = 0;
    if (!a->nkids) return 1;
    if (a->cur < 0) a->cur = a->kind == K_RANDOM ? rand() % a->nkids : 0;
    while (a->loop_count != 0 && runs < a->nkids) {
        AI *c = a->kids[a->cur]; int st = run_child(c, dt); runs++;
        if (st != 1 && (st != 2 || !a->skip_dormant)) return 0 == st ? 0 : st;
        if (c->focus) lost_focus(c);
        if (st == 2) dormant++;
        if (a->kind == K_RANDOM) a->cur = rand() % a->nkids;
        else if (a->cur + 1 < a->nkids) a->cur++;
        else if (a->loop_count == -1 || --a->loop_count > 0) a->cur = 0;
    }
    if (dormant) return 2;
    return 1;
}
static int cond_status(AI *a)                                             /* the evaluation function on the conditions' states */
{
    extern int tg_condition_status(void *); PyObject *args, *r, *key; int i, st = 3; long mask = 0;
    if (!a->eval) return 3;
    for (i = 0; i < a->nconds; i++) mask |= (long)(tg_condition_status(a->conds[i]) != 0) << i;
    if (!a->eval_cache) a->eval_cache = PyDict_New();
    key = PyInt_FromLong(mask);
    if ((r = PyDict_GetItem(a->eval_cache, key))) { st = (int)PyInt_AsLong(r); Py_DECREF(key); return st; }
    args = PyTuple_New(a->nconds); for (i = 0; i < a->nconds; i++) PyTuple_SetItem(args, i, PyInt_FromLong(tg_condition_status(a->conds[i]) != 0));
    r = PyEval_CallObject(a->eval, args); Py_DECREF(args);
    if (!r) { PyErr_Print(); PyErr_Clear(); Py_DECREF(key); return 3; }
    st = PyInt_Check(r) ? (int)PyInt_AsLong(r) : 3; PyDict_SetItem(a->eval_cache, key, r); Py_DECREF(r); Py_DECREF(key);
    return st;
}
void tg_ai_add_condition(void *p, void *c)                                 /* AddCondition; then script.RegisterExternalFunctions(ai) (00478f00) */
{
    extern PyObject *tg_condition_script(void *); AI *a = ai(p); PyObject *s, *f, *r, *self;
    if (!a) return;
    a->conds = realloc(a->conds, (a->nconds + 1) * sizeof *a->conds); a->conds[a->nconds++] = c;
    if (a->eval_cache) { Py_DECREF(a->eval_cache); a->eval_cache = NULL; }
    if ((s = tg_condition_script(c)) && (f = PyObject_GetAttrString(s, "RegisterExternalFunctions"))) {
        self = self_obj(a); r = PyObject_CallFunction(f, "(O)", self); Py_DECREF(self); Py_DECREF(f);
        if (!r) PyErr_Print(); Py_XDECREF(r); PyErr_Clear();
    } else PyErr_Clear();
    a->status = cond_status(a);
}
void tg_ai_condition_changed(void *p, void *c) { AI *a = ai(p); if (a) a->status = cond_status(a); }
void eng_ConditionalAI_SetEvaluationFunction(void *p, PyObject *f)
{ AI *a = ai(p); if (!a) return; Py_XINCREF(f); Py_XDECREF(a->eval); a->eval = f; if (a->eval_cache) { Py_DECREF(a->eval_cache); a->eval_cache = NULL; } a->status = cond_status(a); }
static int update_cond(AI *a, double dt)
{
    AI *c = a->contained;
    if (a->status != 0) { if (c && c->active && !is_interruptable(c)) return run_child(c, dt); if (c && c->focus) lost_focus(c); return a->status; }
    return c ? run_child(c, dt) : 1;
}
static void build(AI *a)                                                  /* BuilderAI: blocks in order, last one is the contained AI */
{
    PyObject *m = a->module ? PyImport_ImportModule(a->module) : NULL; int i, k;
    a->built = 1;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    for (i = 0; i < a->nblocks; i++) {
        Block *b = &a->blocks[i]; PyObject *f = PyObject_GetAttrString(m, b->func), *args, *r, *ship; void *p = NULL;
        if (!f) { PyErr_Print(); PyErr_Clear(); continue; }
        args = PyTuple_New(1 + b->ndeps); ship = a->ship ? appc_pyobj(a->ship, "ShipClass") : (Py_INCREF(Py_None), Py_None); PyTuple_SetItem(args, 0, ship);
        for (k = 0; k < b->ndeps; k++) {
            PyObject *v = NULL; int j;
            if (b->objs[k]) { v = b->objs[k]; Py_INCREF(v); }
            else for (j = 0; j < i; j++) if (!strcmp(a->blocks[j].name, b->deps[k]) && a->blocks[j].ai) { v = self_obj(a->blocks[j].ai); break; }
            if (!v) { v = Py_None; Py_INCREF(v); }
            PyTuple_SetItem(args, 1 + k, v);
        }
        r = PyEval_CallObject(f, args); Py_DECREF(args); Py_DECREF(f);
        if (!r) { PyErr_Print(); PyErr_Clear(); continue; }
        if (r != Py_None && appc_to_ptr(r, &p, "void")) b->ai = ai(p); else PyErr_Clear();
        Py_DECREF(r);
    }
    Py_DECREF(m);
    for (i = a->nblocks - 1; i >= 0; i--) if (a->blocks[i].ai) { a->contained = a->blocks[i].ai; break; }
}
static int update_pre(AI *a, double dt)
{
    int ps = 0; AI *c;
    if (a->kind == K_BUILDER && !a->built) build(a);
    c = a->contained;
    if (c && c->active && !is_interruptable(c)) return run_child(c, dt);
    if (a->native) {                                                     /* ManagePowerAI 00487100 */
        if (a->next_update <= tg_game_time) {
            extern int tg_ship_manage_power(void *, int);
            if (!a->ship) a->status = 3;
            else { a->status = 0; if (tg_ship_manage_power(a->ship, a->conserve)) a->next_update = tg_game_time + 3.0; }
        }
        ps = a->status;
    } else if (a->pre_inst && a->pre_method) {
        if (a->next_update <= tg_game_time) {
            PyObject *args = Py_BuildValue("(f)", (float)dt); a->status = call_status(a->pre_inst, a->pre_method, args, 0); Py_DECREF(args);
            a->next_update = tg_game_time + call_float(a->pre_inst, "GetNextUpdateTime", 0.0);
        }
        ps = a->status;
    }
    if (getenv("APPC_AI_TRACE")) fprintf(stderr, "[AI] %s: preprocess %s.%s -> %d, contained %s\n", a->name, a->pre_inst ? "(instance)" : "-", a->pre_method ? a->pre_method : "-", ps, c ? c->name : "none");
    if (ps == 0) return c ? run_child(c, dt) : 1;
    if (c && c->focus) lost_focus(c);
    return ps == 1 ? 0 : ps == 2 ? 2 : 1;
}
static int ai_update(AI *a, double dt)
{
    int st;
    if (a->paused) return a->status == 3 ? 2 : 2;
    switch (a->kind) {
    case K_PLAIN: st = update_plain(a); break;
    case K_PRIORITY: st = update_priority(a, dt); break;
    case K_SEQUENCE: case K_RANDOM: st = update_sequence(a, dt); break;
    case K_COND: st = update_cond(a, dt); return st;               /* the conditional keeps its own status field */
    default: st = update_pre(a, dt); return st;
    }
    a->status = st; return st;
}
static int ai_update_logged(AI *a, double dt)                            /* APPC_AI_TRACE=1 prints every AI update (like the exe's AI log) */
{
    static int on = -1; static int depth; int st;
    if (on < 0) on = getenv("APPC_AI_TRACE") != NULL;
    if (!on) return ai_update(a, dt);
    depth++; st = ai_update(a, dt); depth--;
    fprintf(stderr, "[AI] %*s%s %s -> %s\n", depth * 2, "", kind_name[a->kind], a->name, st == 0 ? "ACTIVE" : st == 1 ? "DONE" : st == 2 ? "DORMANT" : "INVALID");
    return st;
}
/* the root AIs of ships, updated every frame from the motion tick */
/* SelectTarget.bUpdateInfoOnCodeAISet: a target forced with ForceCurrentTargetString never reaches the ship (its Update only sets a
   new target), so the update must run once the AI is in a ship. No stock script reads the flag, so the engine does: UpdateTargetInfo(0)
   of every preprocessing instance with the flag set, then the flag is cleared. Builder blocks are walked once they are built. */
static void attach_walk(AI *a)
{
    int i;
    if (!a) return;
    if (a->kind == K_PRE && a->pre_inst && PyInstance_Check(a->pre_inst)) {
        PyObject *v = PyObject_GetAttrString(a->pre_inst, "bUpdateInfoOnCodeAISet");
        if (v && PyObject_IsTrue(v)) {
            PyObject *f = PyObject_GetAttrString(a->pre_inst, "UpdateTargetInfo"), *r;
            PyObject_SetAttrString(a->pre_inst, "bUpdateInfoOnCodeAISet", PyInt_FromLong(0));
            /* what SelectTarget.Update does for a new target: the ship gets the target (if SetShipTarget), then the target info is updated */
            {
                PyObject *cai = PyObject_GetAttrString(a->pre_inst, "pCodeAI"), *ship = NULL, *tgt = PyObject_GetAttrString(a->pre_inst, "sCurrentTarget"), *g, *r2;
                if (cai) { g = PyObject_GetAttrString(a->pre_inst, "bSetShipTarget"); if (g && PyObject_IsTrue(g) && tgt && tgt != Py_None) { ship = PyObject_CallMethod(cai, "GetShip", NULL); if (ship && ship != Py_None) { r2 = PyObject_CallMethod(ship, "SetTarget", "(O)", tgt); if (!r2) PyErr_Print(); Py_XDECREF(r2); } } Py_XDECREF(g); }
                Py_XDECREF(ship); Py_XDECREF(tgt); Py_XDECREF(cai); PyErr_Clear();
            }
            if (f) { r = PyObject_CallFunction(f, "(f)", 0.0); if (!r) PyErr_Print(); Py_XDECREF(r); Py_DECREF(f); }
        }
        PyErr_Clear(); Py_XDECREF(v);
    }
    for (i = 0; i < a->nkids; i++) attach_walk(a->kids[i]);
    attach_walk(a->contained);
    for (i = 0; i < a->nblocks; i++) attach_walk(a->blocks[i].ai);
}
int tg_ai_tick(void *ship, void *root, double dt)                          /* returns 1 when the root AI is done (AIList::UpdateAI 004722d0) */
{
    AI *a = ai(root); int st;
    if (!a) return 0;
    attach_walk(a);
    st = run_child(a, dt);
    if (st != 1) return 0;
    if (a->focus) lost_focus(a);                                            /* LostFocus, SetInactive, RemoveAI 00472810: ET_AI_DONE to the ship */
    if (a->active) set_inactive(a);
    {   TGEvent *e = tg_ev_new("TGIntEvent", 0); e->type = (unsigned)tg_const("ET_AI_DONE"); e->dest = ship; e->v.i = a->id; tg_add_event(e); }
    return 1;
}
void eng_PlainAI_StopCallingActivate(void *p) { AI *a = ai(p); if (a) a->calls_activate = 0; }
void eng_SequenceAI_SetResetIfInterrupted(void *p, int v) { AI *a = ai(p); if (a) a->reset_if_int = v != 0; }
void eng_SequenceAI_SetDoubleCheckAllDone(void *p, int v) { AI *a = ai(p); if (a) a->double_check = v != 0; }
void eng_SequenceAI_SetSkipDormant(void *p, int v) { AI *a = ai(p); if (a) a->skip_dormant = v != 0; }
long eng_ArtificialIntelligence_IsActive(void *p) { AI *a = ai(p); return a && a->active; }
long eng_ArtificialIntelligence_HasFocus(void *p) { AI *a = ai(p); return a && a->focus; }
void eng_ArtificialIntelligence_Reset(void *p) { AI *a = ai(p); if (a) { a->cur = -1; free(a->done); a->done = NULL; a->next_update = 0; } }
void eng_PreprocessingAI_ForceUpdate(void *p) { AI *a = ai(p); if (a) a->next_update = 0; }
static Block *block_get(AI *a, const char *name) { int i; for (i = 0; i < a->nblocks; i++) if (!strcmp(a->blocks[i].name, name)) return &a->blocks[i]; return NULL; }
void eng_BuilderAI_AddAIBlock(void *p, char *name, char *func)
{ AI *a = ai(p); Block *b; if (!a || !name) return; a->blocks = realloc(a->blocks, (a->nblocks + 1) * sizeof *a->blocks); b = &a->blocks[a->nblocks++]; memset(b, 0, sizeof *b); b->name = strdup(name); b->func = strdup(func ? func : ""); }
static void add_dep(AI *a, const char *blk, const char *dep, PyObject *o)
{ Block *b = a ? block_get(a, blk) : NULL; if (!b) return; b->deps = realloc(b->deps, (b->ndeps + 1) * sizeof *b->deps); b->objs = realloc(b->objs, (b->ndeps + 1) * sizeof *b->objs); b->deps[b->ndeps] = strdup(dep ? dep : ""); b->objs[b->ndeps] = o; Py_XINCREF(o); b->ndeps++; }
void eng_BuilderAI_AddDependency(void *p, char *blk, char *dep) { add_dep(ai(p), blk, dep, NULL); }
void eng_BuilderAI_AddDependencyObject(void *p, char *blk, char *name, PyObject *o) { add_dep(ai(p), blk, name, o ? o : Py_None); }
