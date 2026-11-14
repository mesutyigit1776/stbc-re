/* Actions: TGAction, TGTimedAction, TGScriptAction, TGSequence and the TGActionManager events.
   Behavior from stbc.exe: TGAction::TGAction 006fe5a0, FinishAction 006fe7c0, TGTimedAction::Play 007022f0, TGScriptAction::Play 006ff840,
   TGSequence::Play 007008e0 / AddAction 00700710 / AppendAction 007007f0 / Completed 00700a70 / Skip 00700b40 / RemoveAction 00700af0,
   TGActionManager handlers 006ff2d0..006ff34e (events 0x50001 completed, 0x50002 skip, 0x50003 play, 0x50005 play delayed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

#define ET_ACTION_COMPLETED 0x50001
#define ET_ACTION_SKIP      0x50002
#define ET_PLAY_ACTION      0x50003
#define ET_PLAY_ACTION_DELAYED 0x50005

struct TGAct; struct act_vt {
    void (*play)(struct TGAct *); void (*restart)(struct TGAct *, float); void (*skip)(struct TGAct *);
    void (*abort)(struct TGAct *); void (*complete)(struct TGAct *); void (*clear_events)(struct TGAct *);
};
typedef struct TGAct {
    TGObject o; const struct act_vt *vt;
    int nev; TGEvent **ev;                 /* completion events (+0x10) */
    struct TGAct *seq;                     /* the sequence this action belongs to (+0x14) */
    float start;                           /* +0x18, -1 = not started */
    unsigned char skippable, real, survive;
    float duration; unsigned timer_id;     /* TGTimedAction +0x20 / +0x24 */
    char *mod, *fn; PyObject *args;        /* TGScriptAction */
    struct ent { struct TGAct *action, *after; float delay; } *ents; int nents;   /* TGSequence entries (+0x24, count +0x20) */
    unsigned *timers; int ntimers; int completed;                                 /* TGSequence timers (+0x30) and completed flag (+0x28) */
    void (*on_play)(void *a, void *user); void *user;                            /* timed actions written in other files (sound, character) */
    char *reg_name;                        /* name given by TGActionManager::RegisterAction (manager list entry +4) */
    int (*pre_play)(void *a, void *user); void (*post_play)(void *a, void *user); void (*on_complete)(void *a, void *user);   /* subclass hooks (WarpSequence) */
} TGAct;

static void *manager;                      /* g_kTGActionManager: the destination of the internal events */

/* debug: APPC_ACTION_TRACE=1 logs when actions start and finish */
static void trace(const char *what, struct TGAct *a);
static TGAct *act(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x701) ? p : NULL; }
static int alive(TGAct *a) { return a && tg_is_ours(a) && !a->o.dead; }
static double now_of(TGAct *a) { return a->real ? tg_real_time : tg_game_time; }
static void set_start(TGAct *a) { a->start = (float)now_of(a); }

/* ---- helpers: internal events and timers ---- */
static TGEvent *mk_event(int type, TGAct *target, float delay)
{
    TGEvent *e = tg_ev_new("TGEvent", 0);
    e->type = (unsigned)type; e->dest = manager; e->ref_id = target ? target->o.id : 0; e->fval = delay;
    return e;
}
static unsigned start_timer(TGAct *a, double fire, TGEvent *e)
{
    TGTimer *t = tg_new("TGTimer", sizeof(TGTimer), 0);
    t->next = t->start = (float)fire;
    t->event = e; e->refcount++;
    tg_timer_add(tg_timer_mgr(a->real), t);
    return t->o.id;
}
static void kill_timer(TGAct *a, unsigned id)
{
    TimerMgr *g = tg_timer_mgr(a->real); TGTimer *t = (TGTimer *)tg_find(id);
    (void)g;
    /* TGTimerManager::DeleteTimer: unlink and delete; the id lookup works for either manager */
    if (t) { TimerMgr *m[2]; int i; m[0] = tg_timer_mgr(0); m[1] = tg_timer_mgr(1);
        for (i = 0; i < 2; i++) if (m[i] && m[i]->list) {
            TGTimer *p = m[i]->list->head, *prev = NULL;
            while (p && p != t) { prev = p; p = p->link; }
            if (p) { if (prev) prev->link = p->link; else m[i]->list->head = p->link; if (m[i]->list->tail == p) m[i]->list->tail = prev; p->link = NULL; m[i]->list->count--; }
        }
        if (t->event && t->event->refcount) t->event->refcount--;
        t->event = NULL; t->o.dead = 1; }
}

/* ---- TGAction ---- */
static void base_play(TGAct *a) { set_start(a); }
static void base_restart(TGAct *a, float t) { a->start = (float)now_of(a) - t; }          /* FUN_006fe830 */
static void fire_events(TGAct *a)                                                           /* FUN_006fe8e0 */
{
    int i;
    for (i = 0; i < a->nev; i++) { tg_add_event(a->ev[i]); if (a->ev[i]->refcount) a->ev[i]->refcount--; }
    a->nev = 0; free(a->ev); a->ev = NULL;
}
static void seq_remove(TGAct *s, TGAct *a);
static void trace(const char *what, TGAct *a)
{
    static int on = -1;
    if (on < 0) on = getenv("APPC_ACTION_TRACE") != NULL;
    if (on) fprintf(stderr, "[act %8.2f] %-8s %s#%u %s%s%s\n", tg_game_time, what, a->o.cls->name, a->o.id, a->mod ? a->mod : "", a->fn ? "." : "", a->fn ? a->fn : "");
}
static void base_complete(TGAct *a)                                                         /* TGAction::FinishAction 006fe7c0 */
{
    trace("done", a);
    fire_events(a);
    if (a->seq && alive(a->seq)) seq_remove(a->seq, a);
    a->o.dead = 1;                                                                          /* the original deletes the action here */
}
static void base_clear_events(TGAct *a)                                                     /* 006fe980 */
{
    int i;
    for (i = 0; i < a->nev; i++) if (a->ev[i]->refcount) a->ev[i]->refcount--;
    free(a->ev); a->ev = NULL; a->nev = 0;
}
static void base_abort(TGAct *a)                                                            /* FUN_006fe860 */
{
    a->vt->clear_events(a);
    if (a->seq && alive(a->seq)) { a->seq->vt->abort(a->seq); return; }
    a->seq = NULL;
    a->vt->complete(a);
}
static const struct act_vt base_vt = {base_play, base_restart, base_complete, base_abort, base_complete, base_clear_events};

/* every action is in the manager's list from its constructor on (TGAction::TGAction 006fe5a0 -> insertSortedEvent) */
static TGAct **all_acts; static int n_acts, cap_acts;
static void list_add(TGAct *a)
{
    int i, j;
    if (n_acts == cap_acts) {                                   /* drop finished actions before growing */
        for (i = j = 0; i < n_acts; i++) if (alive(all_acts[i])) all_acts[j++] = all_acts[i];
        n_acts = j;
        if (n_acts == cap_acts) { cap_acts = cap_acts ? cap_acts * 2 : 256; all_acts = realloc(all_acts, cap_acts * sizeof *all_acts); }
    }
    all_acts[n_acts++] = a;
}
static TGAct *act_new(const char *cls, const struct act_vt *vt)
{
    TGAct *a = tg_new(cls, sizeof(TGAct), 0);
    a->vt = vt; a->start = -1.0f; list_add(a); return a;
}
static void act_add_event(TGAct *a, TGEvent *e) { a->ev = realloc(a->ev, (a->nev + 1) * sizeof *a->ev); a->ev[a->nev++] = e; e->refcount++; }

/* ---- TGTimedAction ---- */
static void timed_cancel(TGAct *a) { if (a->timer_id) { kill_timer(a, a->timer_id); a->timer_id = 0; } }
static void timed_play(TGAct *a)                                                            /* 007022f0 */
{
    TGEvent *e;
    if (a->duration < 0.0f) { a->vt->complete(a); return; }
    e = mk_event(ET_ACTION_COMPLETED, a, 0.0f);
    a->timer_id = start_timer(a, now_of(a) + a->duration, e);
    set_start(a);
}
static void timed_restart(TGAct *a, float t)                                                /* 007023e0 */
{
    TGEvent *e = mk_event(ET_ACTION_COMPLETED, a, 0.0f);
    a->timer_id = start_timer(a, (a->duration - t) + now_of(a), e);
    set_start(a);
}
static void timed_complete(TGAct *a) { timed_cancel(a); base_complete(a); }                 /* 007024c0 */
static void timed_skip(TGAct *a) { timed_cancel(a); base_complete(a); }                     /* 00702500 -> FUN_006fe8a0 */
static const struct act_vt timed_vt = {timed_play, timed_restart, timed_skip, base_abort, timed_complete, base_clear_events};

/* ---- TGScriptAction: calls Module.Function(pAction, *args); the function returns 0 when done, nonzero to keep the action open ---- */
static void script_play(TGAct *a)                                                           /* 006ff840 */
{
    PyObject *m, *fn, *self, *tup, *r; int n, i;
    char mod[256]; char *p;
    strncpy(mod, a->mod, 255); mod[255] = 0;
    for (p = mod; *p; p++) if (*p == '\\' || *p == '/') *p = '.';
    m = PyImport_ImportModule(mod);
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }                       /* getPythonModuleAttr failed: the action just stays open */
    fn = PyObject_GetAttrString(m, a->fn); Py_DECREF(m);
    if (!fn) { PyErr_Clear(); return; }
    self = appc_pyobj(a, "TGScriptAction");
    n = PyList_Size(a->args);
    tup = PyTuple_New(n + 1);
    PyTuple_SetItem(tup, 0, self);
    for (i = 0; i < n; i++) { PyObject *x = PyList_GetItem(a->args, i); Py_INCREF(x); PyTuple_SetItem(tup, i + 1, x); }
    r = PyEval_CallObject(fn, tup);
    Py_DECREF(fn); Py_DECREF(tup);
    if (!r || PyErr_Occurred()) {
        char msg[300];
        PyErr_Print(); PyErr_Clear(); Py_XDECREF(r);
        sprintf(msg, "TGScriptAction faulted attempting to call %s.%s", a->mod, a->fn);
        fprintf(stderr, "%s\n", msg);
        a->vt->complete(a);
        return;
    }
    if (PyInt_Check(r) ? PyInt_AsLong(r) == 0 : 0) { Py_DECREF(r); a->vt->complete(a); return; }
    Py_DECREF(r);
}
static const struct act_vt script_vt = {script_play, base_restart, base_complete, base_abort, base_complete, base_clear_events};

/* ---- TGSequence ---- */
static void seq_add_timer(TGAct *s, unsigned id) { s->timers = realloc(s->timers, (s->ntimers + 1) * sizeof *s->timers); s->timers[s->ntimers++] = id; }
static void seq_clear_timers(TGAct *s)
{
    int i;
    for (i = 0; i < s->ntimers; i++) kill_timer(s, s->timers[i]);
    free(s->timers); s->timers = NULL; s->ntimers = 0;
}
static void seq_play(TGAct *s)                                                              /* 007008e0 */
{
    int i; double base;
    set_start(s); base = tg_game_time;
    if (s->nents == 0) { s->vt->complete(s); return; }
    for (i = 0; i < s->nents; i++) {
        struct ent *e = &s->ents[i];
        if (!e->action) continue;
        if (!alive(e->action)) { e->action = NULL; continue; }
        if (e->after == NULL) {
            if (e->delay == 0.0f) { trace("play", e->action); e->action->vt->play(e->action); }
            else {
                TGEvent *ev = mk_event(ET_PLAY_ACTION, e->action, 0.0f);
                double t = (e->action->real ? tg_real_time : tg_game_time) + e->delay;
                seq_add_timer(s, start_timer(e->action, t, ev));
            }
        }
    }
    (void)base;
}
static void seq_complete(TGAct *s)                                                          /* 00700a70 */
{
    int i; s->completed = 1;
    for (i = 0; i < s->nents; i++) if (s->ents[i].action && alive(s->ents[i].action)) { TGAct *x = s->ents[i].action; x->vt->skip(x); }
    seq_clear_timers(s);
    base_complete(s);
}
static void seq_skip(TGAct *s)                                                              /* 00700b40 */
{
    int i;
    seq_clear_timers(s);
    for (i = 0; i < s->nents; i++) if (s->ents[i].action && alive(s->ents[i].action)) { TGAct *x = s->ents[i].action; x->vt->clear_events(x); x->vt->skip(x); }
}
static void seq_remove(TGAct *s, TGAct *a)                                                  /* 00700af0 */
{
    int i, all_null = 1;
    for (i = 0; i < s->nents; i++) { if (s->ents[i].action == a) s->ents[i].action = NULL; if (s->ents[i].action) all_null = 0; }
    if (s->nents && !all_null) return;
    if (!s->completed) s->vt->complete(s);
}
static const struct act_vt seq_vt = {seq_play, base_restart, seq_skip, base_abort, seq_complete, base_clear_events};

static void add_entry(TGAct *s, TGAct *a, TGAct *after, float delay)
{
    s->ents = realloc(s->ents, (s->nents + 1) * sizeof *s->ents);
    s->ents[s->nents].action = a; s->ents[s->nents].after = after; s->ents[s->nents].delay = delay; s->nents++;
    a->seq = s;                                   /* vslot 0x58 */
}
/* when `after` finishes, the manager plays `a` (at once, or after the delay) */
static void chain_after(TGAct *after, TGAct *a, float delay)
{
    TGEvent *e = mk_event(delay <= 0.0f ? ET_PLAY_ACTION : ET_PLAY_ACTION_DELAYED, a, delay);
    act_add_event(after, e);
}

/* ---- manager events ---- */
static TGAct *by_id(TGEvent *e) { TGObject *o = tg_find(e->ref_id); return o && tg_is_type(o, 0x701) ? (TGAct *)o : NULL; }
static void on_completed(TGEvent *e) { TGAct *a = by_id(e); if (a) a->vt->complete(a); }          /* ActionCompletedHandler 006ff2d0 */
static void on_skip(TGEvent *e) { TGAct *a = by_id(e); if (a) a->vt->skip(a); }                    /* ActionSkipHandler 006ff2f0 */
static void on_play(TGEvent *e) { TGAct *a = by_id(e); if (a) { trace("play", a); a->vt->play(a); } }                   /* PlaySequenceActionHandler 006ff310 */
static void on_play_delayed(TGEvent *e)                                                            /* 006ff330 */
{
    TGAct *a = by_id(e); TGEvent *ev; unsigned id;
    if (!a) return;
    ev = mk_event(ET_PLAY_ACTION, a, 0.0f);
    id = start_timer(a, now_of(a) + e->fval, ev);
    if (a->seq && alive(a->seq)) seq_add_timer(a->seq, id);
}
void tg_actions_init(void)
{
    extern void *eng_global_get(const struct appc_global *g);
    const struct appc_global *g; manager = NULL;
    for (g = appc_globals; g->name; g++) if (!strcmp(g->name, "g_kTGActionManager")) manager = eng_global_get(g);
    if (!manager) return;
    tg_register_native(manager, ET_ACTION_COMPLETED, on_completed);
    tg_register_native(manager, ET_ACTION_SKIP, on_skip);
    tg_register_native(manager, ET_PLAY_ACTION, on_play);
    tg_register_native(manager, ET_PLAY_ACTION_DELAYED, on_play_delayed);
}

/* ======================================================= Python-facing functions ======================================================= */
/* TGActionManager::RegisterAction 006fed60: name the action's list entry (None -> "") */
void eng_TGActionManager_RegisterAction(void *p, char *name)
{
    TGAct *a = act(p);
    if (!alive(a)) return;
    free(a->reg_name); a->reg_name = strdup(name ? name : "");
}
/* TGActionManager::killActionsByName 006fee80: every registered action whose name matches (an empty name matches all registered
   actions) first loses its completion events (vslot 0x5c ClearEvents), then is skipped (vslot 0x48) */
void eng_TGActionManager_KillActions(char *name)
{
    int i, n = n_acts, k = 0; TGAct **m;
    if (!name) name = "";
    m = malloc((n ? n : 1) * sizeof *m);
    for (i = 0; i < n; i++) { TGAct *a = all_acts[i]; if (alive(a) && a->reg_name && (!*name || !strcmp(a->reg_name, name))) m[k++] = a; }
    for (i = 0; i < k; i++) m[i]->vt->clear_events(m[i]);
    for (i = 0; i < k; i++) if (alive(m[i])) m[i]->vt->skip(m[i]);
    free(m);
}
void *eng_TGAction_CreateNull(void) { return NULL; }       /* py_TGAction_CreateNull returns NewPointerObj(0): the null action is a NULL pointer */
void *eng_TGAction_Cast(void *p) { return act(p); }
long eng_TGAction_IsPlaying(void *p) { TGAct *a = act(p); return a && a->start != -1.0f; }                  /* vslot 0x30 (FUN_00402770) */
void eng_TGAction_ConstructDescription(void *p, void *a, void *b) { }
long eng_TGAction_SetupEditControls(void *p, char *s) { return 1; }
long eng_TGAction_ExtractControlValues(void *p, char *s) { return 1; }
void eng_TGAction_Play(void *p) { TGAct *a = act(p); if (a) { trace("play", a); a->vt->play(a); } }
void eng_TGAction_Restart(void *p, float t) { TGAct *a = act(p); if (a) a->vt->restart(a, t); }
void eng_TGAction_Skip(void *p) { TGAct *a = act(p); if (a) a->vt->skip(a); }
void eng_TGAction_Abort(void *p) { TGAct *a = act(p); if (a) a->vt->abort(a); }
void eng_TGAction_Completed(void *p) { TGAct *a = act(p); if (a) a->vt->complete(a); }
long eng_TGAction_IsSkippable(void *p) { TGAct *a = act(p); return a && a->skippable; }
void eng_TGAction_SetSkippable(void *p, int v) { TGAct *a = act(p); if (a) a->skippable = v != 0; }
void eng_TGAction_AddCompletedEvent(void *p, PyObject *evo)
{
    TGAct *a = act(p); void *e = NULL;
    if (evo && evo != Py_None) { if (!appc_to_ptr(evo, &e, "void")) PyErr_Clear(); }
    if (a && e && tg_is_ours(e) && tg_is_type(e, 0x101)) act_add_event(a, e);
}
long eng_TGAction_IsPartOfSequence(void *p, void *s) { TGAct *a = act(p); TGAct *x; if (!a) return 0; for (x = a->seq; ; x = x->seq) { if (x == (TGAct *)s) return 1; if (!x) return 0; } }
void *eng_TGAction_GetSequence(void *p) { TGAct *a = act(p); return a ? a->seq : NULL; }
void eng_TGAction_SetUseRealTime(void *p, int v) { TGAct *a = act(p); if (a) a->real = v != 0; }
long eng_TGAction_IsUseRealTime(void *p) { TGAct *a = act(p); return a && a->real; }
void eng_TGAction_SetSurviveGlobalAbort(void *p, int v) { TGAct *a = act(p); if (a) a->survive = v != 0; }
long eng_TGAction_IsGlobalAbortSurvivor(void *p) { TGAct *a = act(p); return a && a->survive; }

void *eng_TGTimedAction_dummy(void) { return NULL; }
void eng_TGTimedAction_SetDuration(void *p, float d) { TGAct *a = act(p); if (a) a->duration = d; }
double eng_TGTimedAction_GetDuration(void *p) { TGAct *a = act(p); return a ? a->duration : 0.0; }

void *eng_TGSequence_Create(void) { return act_new("TGSequence", &seq_vt); }
/* a TGSequence subclass whose class code runs around Play and Completed (WarpSequence, tg_warp.c): pre_play returning 0 skips the
   sequence instead of playing it (WarpSequence Play 004d66b0 calls vslot 0x48); on_complete runs before the completion events fire */
static void subseq_play(TGAct *s)
{
    if (s->pre_play && !s->pre_play(s, s->user)) { s->vt->skip(s); return; }
    seq_play(s);
    if (s->post_play && alive(s)) s->post_play(s, s->user);
}
static void subseq_complete(TGAct *s) { if (s->completed) return; if (s->on_complete) s->on_complete(s, s->user); seq_complete(s); }
static const struct act_vt subseq_vt = {subseq_play, base_restart, seq_skip, base_abort, subseq_complete, base_clear_events};
void *tg_seq_sub_new(const char *cls, int (*pre)(void *, void *), void (*post)(void *, void *), void (*done)(void *, void *), void *user)
{ TGAct *s = act_new(cls, &subseq_vt); s->pre_play = pre; s->post_play = post; s->on_complete = done; s->user = user; return s; }
void tg_action_add_event(void *a, void *e) { TGAct *x = act(a); if (x && e) act_add_event(x, e); }
/* an action that does its work when played and then completes at once (ChangeRenderedSetAction::Play 00401270 ends with Completed) */
static void instant_play(TGAct *a) { set_start(a); if (a->on_play) a->on_play(a, a->user); if (alive(a)) a->vt->complete(a); }
static const struct act_vt instant_vt = {instant_play, base_restart, base_complete, base_abort, base_complete, base_clear_events};
void *tg_instant_action_new(const char *cls, void (*fn)(void *, void *), void *user) { TGAct *a = act_new(cls, &instant_vt); a->on_play = fn; a->user = user; return a; }
void *eng_TGSequence_Cast(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x707) ? p : NULL; }
void eng_TGSequence_AddAction(void *sp, void *ap, void *after, float delay)                 /* 00700710 */
{
    TGAct *s = eng_TGSequence_Cast(sp), *a = act(ap), *af = after ? act(after) : NULL; int i, found = 0;
    if (!s || !a) return;
    if (!af) { add_entry(s, a, NULL, delay); return; }
    for (i = 0; i < s->nents; i++) if (s->ents[i].action == af) found = 1;
    if (!found) { a->o.dead = 1; return; }                    /* an action whose predecessor is not in the sequence is deleted */
    chain_after(af, a, delay);
    add_entry(s, a, af, delay);
}
void eng_TGSequence_AppendAction(void *sp, void *ap, float delay)                           /* 007007f0 */
{
    TGAct *s = eng_TGSequence_Cast(sp), *a = act(ap), *after = NULL; int i;
    if (!s || !a) return;
    if (delay < 0.0f) delay = 0.0f;
    for (i = s->nents - 1; i >= 0; i--) if (s->ents[i].action) { after = s->ents[i].action; break; }
    if (after) chain_after(after, a, delay);
    add_entry(s, a, after, delay);
}
void eng_TGSequence_Play(void *p) { TGAct *s = eng_TGSequence_Cast(p); if (s) { trace("play", s); s->vt->play(s); } }
void eng_TGSequence_Skip(void *p) { TGAct *s = eng_TGSequence_Cast(p); if (s) s->vt->skip(s); }
long eng_TGSequence_GetNumActions(void *p) { TGAct *s = eng_TGSequence_Cast(p); return s ? s->nents : 0; }
void *eng_TGSequence_GetAction(void *p, int i) { TGAct *s = eng_TGSequence_Cast(p); return s && i >= 0 && i < s->nents ? s->ents[i].action : NULL; }

/* TGScriptAction.Create(module, function, *args): the first two items are strings; the rest are kept for the call */
PyObject *eng_TGScriptAction_Create(PyObject *args)
{
    int n = PyTuple_Size(args), i; TGAct *a; char *mod, *fn;
    if (n < 2 || !PyString_Check(PyTuple_GetItem(args, 0)) || !PyString_Check(PyTuple_GetItem(args, 1))) { if (n >= 2) { PyErr_Print(); PyErr_Clear(); } Py_INCREF(Py_None); return Py_None; }
    mod = PyString_AsString(PyTuple_GetItem(args, 0)); fn = PyString_AsString(PyTuple_GetItem(args, 1));
    a = act_new("TGScriptAction", &script_vt);
    a->mod = strdup(mod); a->fn = strdup(fn); a->args = PyList_New(0);
    for (i = 2; i < n; i++) PyList_Append(a->args, PyTuple_GetItem(args, i));
    return appc_pyobj(a, "TGScriptAction");
}
void eng_TGScriptAction_Play(void *p) { TGAct *a = act(p); if (a) a->vt->play(a); }

/* Sound, animation, movie and credit actions are timed actions in the original. Without an audio/animation layer they get no real duration:
   duration 0 makes them complete on the next tick. The real durations arrive with those layers. */
static void *timed_new(const char *cls) { TGAct *a = act_new(cls, &timed_vt); a->duration = 0.0f; return a; }
/* a timed action whose on_play callback runs first and can set the duration (tg_action_set_duration) */
static void hooked_play(TGAct *a) { if (a->on_play) a->on_play(a, a->user); timed_play(a); }
static const struct act_vt hooked_vt = {hooked_play, timed_restart, timed_skip, base_abort, timed_complete, base_clear_events};
/* EffectAction (004de640..004de860): a timed action that starts its particle controller when it plays (on_play sets the duration, the controller's
   effect life time) and stops it when it completes or is skipped (EffectAction::Completed / Skip) */
static void eff_complete(TGAct *a) { if (a->on_complete) a->on_complete(a, a->user); timed_complete(a); }
static void eff_skip(TGAct *a) { if (a->on_complete) a->on_complete(a, a->user); timed_skip(a); }
static const struct act_vt eff_vt = {hooked_play, timed_restart, eff_skip, base_abort, eff_complete, base_clear_events};
void *tg_effect_action_new(void (*on_play)(void *, void *), void (*on_end)(void *, void *), void *user)
{ TGAct *a = act_new("EffectAction", &eff_vt); a->duration = 0.0f; a->on_play = on_play; a->on_complete = on_end; a->user = user; return a; }
void *tg_hooked_action_new(const char *cls, void (*on_play)(void *, void *), void *user) { TGAct *a = act_new(cls, &hooked_vt); a->duration = 0.0f; a->on_play = on_play; a->user = user; return a; }
void tg_action_set_duration(void *p, float d) { TGAct *a = act(p); if (a) a->duration = d; }
/* An action that stays open until something completes it (events added to other actions, or tg_action_finish): CharacterAction of a spoken line, which is done when
   its sound action is done. on_skip runs when the action is skipped (the line's sound action is skipped with it); the skip then finishes the action. */
static void open_play(TGAct *a) { set_start(a); if (a->on_play) a->on_play(a, a->user); }
static void open_skip(TGAct *a) { if (a->on_complete) a->on_complete(a, a->user); base_complete(a); }
static const struct act_vt open_vt = {open_play, base_restart, open_skip, base_abort, base_complete, base_clear_events};
void *tg_open_action_new(const char *cls, void (*on_play)(void *, void *), void (*on_skip)(void *, void *), void *user)
{ TGAct *a = act_new(cls, &open_vt); a->on_play = on_play; a->on_complete = on_skip; a->user = user; return a; }
void tg_action_finish(void *p) { TGAct *a = act(p); if (alive(a)) a->vt->complete(a); }            /* vslot 0x50 */
void tg_action_play_now(void *p) { TGAct *a = act(p); if (alive(a)) { trace("play", a); a->vt->play(a); } }   /* vslot 0x40 */
void tg_action_skip_now(void *p) { TGAct *a = act(p); if (alive(a)) a->vt->skip(a); }               /* vslot 0x48 */
int tg_action_exists(void *p) { return alive(act(p)); }                                              /* FUN_006ff040 != -1: the action manager still lists it */
void *tg_action_completion_event(void *p) { TGAct *a = act(p); return a ? mk_event(ET_ACTION_COMPLETED, a, 0.0f) : NULL; }
void *tg_action_user(void *p) { TGAct *a = act(p); return a ? a->user : NULL; }
/* TGSoundAction 00702fa0 (Create), 00703140 (Play), 00703270 (Restart), 007033b0 (Completed), 00703400 (ClearEvents), 00703440 (Skip):
   plays the named sound; the action is done when the sound ends (the sound's end event is this action's completion event, so the manager completes it).
   Flags: 1 rewind, 2 allow duplicates (else the copies of that sound already playing are stopped), 4 loop, 8 let the sound go on when the action
   completes (not together with looping), 16 voice. The volume, loop and voice flags are set on the named (original) sound, as the exe does. */
typedef struct { char *name, *region; unsigned flags; float volume, prio; int prio_set; void *node; long slot; } SndAct;
extern void *eng_TGSoundManager_GetSound(void *, char *); extern long eng_TGSound_PlayAndNotify(void *, void *, int); extern long eng_TGSoundManager_StopSound(void *, char *);
extern void *eng_TGSoundRegion_GetRegion(char *); extern long tg_region_add(void *, void *); extern long eng_TGSound_SetVolume(void *, float);
extern void eng_TGSound_SetLooping(void *, int); extern void eng_TGSound_SetVoice(void *, int); extern long eng_TGSound_SetPriority(void *, float);
extern long eng_TGSound_AttachToNode(void *, void *); extern void *eng_TGSoundManager_GetPlayingSound(void *, long); extern long eng_TGSound_SetEndEvent(void *, void *);
extern long eng_TGSoundManager_StopPlayingSound(void *, long); extern long eng_TGSound_SetPlayPositionSeconds(void *, float);
static SndAct *sact(TGAct *a) { return a->user; }
static int sound_start(TGAct *a)
{
    SndAct *x = sact(a); void *snd; TGEvent *e; long slot;
    if (x->slot) { eng_TGSoundManager_StopPlayingSound(NULL, x->slot); x->slot = 0; }
    snd = eng_TGSoundManager_GetSound(NULL, x->name);
    if (!snd) { a->vt->complete(a); return 0; }
    e = mk_event(ET_ACTION_COMPLETED, a, 0.0f);
    if (x->region) { void *r = eng_TGSoundRegion_GetRegion(x->region); if (r) tg_region_add(r, snd); }
    eng_TGSound_SetVolume(snd, x->volume);
    eng_TGSound_SetLooping(snd, (x->flags >> 2) & 1);
    eng_TGSound_SetVoice(snd, (x->flags >> 4) & 1);
    if (x->prio_set) eng_TGSound_SetPriority(snd, x->prio);
    if (x->node) eng_TGSound_AttachToNode(snd, x->node);
    if (!(x->flags & 2)) eng_TGSoundManager_StopSound(NULL, x->name);
    slot = eng_TGSound_PlayAndNotify(snd, e, (int)(x->flags & 1));
    x->slot = slot;
    set_start(a);
    return 1;
}
static void sound_play(TGAct *a) { sound_start(a); }
static void sound_restart(TGAct *a, float t)
{
    SndAct *x = sact(a);
    if (sound_start(a)) { void *p = eng_TGSoundManager_GetPlayingSound(NULL, x->slot); if (p) eng_TGSound_SetPlayPositionSeconds(p, t); base_restart(a, t); }
}
static void sound_complete(TGAct *a)
{
    SndAct *x = sact(a);
    if (x->slot && !(x->flags & 8)) {
        void *p = eng_TGSoundManager_GetPlayingSound(NULL, x->slot);
        if (p) eng_TGSound_SetEndEvent(p, NULL);
        eng_TGSoundManager_StopPlayingSound(NULL, x->slot); x->slot = 0;
    }
    base_complete(a);
}
static void sound_skip(TGAct *a) { SndAct *x = sact(a); eng_TGSoundManager_StopPlayingSound(NULL, x->slot); x->slot = 0; }
static void sound_clear_events(TGAct *a)
{
    SndAct *x = sact(a);
    if (x->slot) { void *p = eng_TGSoundManager_GetPlayingSound(NULL, x->slot); if (p) eng_TGSound_SetEndEvent(p, NULL); }
    base_clear_events(a);
}
static const struct act_vt sound_vt = {sound_play, sound_restart, sound_skip, base_abort, sound_complete, sound_clear_events};
void *eng_TGSoundAction_Create(char *name, int flags, char *region, float volume)
{
    TGAct *a = act_new("TGSoundAction", &sound_vt); SndAct *x = calloc(1, sizeof *x);
    x->name = strdup(name ? name : ""); x->region = region ? strdup(region) : NULL; x->volume = volume;
    if ((flags & 8) && (flags & 4)) flags &= ~8;
    x->flags = (unsigned)flags; x->prio = 0.5f; a->user = x; a->duration = 0.0f; return a;
}
void eng_TGSoundAction_SetPriority(void *p, float v) { TGAct *a = act(p); if (a && a->vt == &sound_vt) { sact(a)->prio = v; sact(a)->prio_set = 1; } }
void eng_TGSoundAction_SetNode(void *p, void *n) { TGAct *a = act(p); if (a && a->vt == &sound_vt) sact(a)->node = n; }

/* TGCreditAction: shows a line of text in a pane for its duration (the credits of the main menu). Approximations: no fade in or out, the
   font is the default font, and an x of 0 centres the line. */
extern void tg_ui_set_color(void *p, const float *c);
typedef struct { char *text; void *pane; float x, y; void *para; } CreditX;
static float credit_col[4] = {0.65f, 0.65f, 1.0f, 1.0f};
void eng_TGCreditAction_SetDefaultColor(float r, float g, float b, float a) { credit_col[0] = r; credit_col[1] = g; credit_col[2] = b; credit_col[3] = a; }
static void credit_play(void *a, void *user)
{
    extern void *eng_TGParagraph_Create(char *, float, PyObject *, char *, float, int); extern void eng_TGPane_AddChild(void *, void *, float, float, int);
    extern double eng_TGUIObject_GetWidth(void *);
    CreditX *c = user; float x = c->x;
    c->para = eng_TGParagraph_Create(c->text, 0.0f, NULL, NULL, 0, 0);
    if (!c->para) return;
    tg_ui_set_color(c->para, credit_col);
    if (x == 0.0f) x = (float)((eng_TGUIObject_GetWidth(c->pane) - eng_TGUIObject_GetWidth(c->para)) * 0.5);
    eng_TGPane_AddChild(c->pane, c->para, x, c->y, 0);
    tg_action_set_duration(a, ((TGAct *)a)->duration);
}
static void credit_complete(TGAct *a)
{
    extern void eng_TGPane_DeleteChild(void *, void *, int);
    CreditX *c = a->user;
    if (c && c->para && c->pane) { eng_TGPane_DeleteChild(c->pane, c->para, 0); c->para = NULL; }
    timed_complete(a);
}
static const struct act_vt credit_vt = {hooked_play, timed_restart, credit_complete, base_abort, credit_complete, base_clear_events};
void *eng_TGCreditAction_CreateSTR(char *text, void *pane, float x, float y, float dur, float fi, float fo, int fs, int a8, int a9)
{
    TGAct *a = act_new("TGCreditAction", &credit_vt); CreditX *c = calloc(1, sizeof *c);
    c->text = strdup(text ? text : ""); c->pane = pane; c->x = x; c->y = y;
    a->duration = dur; a->on_play = credit_play; a->user = c; return a;
}
void *eng_TGCreditAction_Create(void *text, void *pane, float x, float y, float dur, float fi, float fo, int fs, int a8, int a9)
{ extern const char *eng_TGString_GetCString(void *); return eng_TGCreditAction_CreateSTR((char *)(text ? eng_TGString_GetCString(text) : ""), pane, x, y, dur, fi, fo, fs, a8, a9); }
void *eng_TGAnimAction_Create(PyObject *a, char *b, int c, int d, int e, int f) { return timed_new("TGAnimAction"); }
void eng_TGSoundAction_Play(void *p) { TGAct *a = act(p); if (a) a->vt->play(a); }
void eng_TGSoundAction_Restart(void *p, float t) { TGAct *a = act(p); if (a) a->vt->restart(a, t); }
void eng_TGSoundAction_Completed(void *p) { TGAct *a = act(p); if (a) a->vt->complete(a); }
void eng_TGSoundAction_Skip(void *p) { TGAct *a = act(p); if (a) a->vt->skip(a); }
void eng_TGAnimAction_Play(void *p) { TGAct *a = act(p); if (a) a->vt->play(a); }
void eng_TGAnimAction_Restart(void *p, float t) { TGAct *a = act(p); if (a) a->vt->restart(a, t); }
void eng_TGAnimAction_Completed(void *p) { TGAct *a = act(p); if (a) a->vt->complete(a); }
void eng_TGAnimAction_Skip(void *p) { TGAct *a = act(p); if (a) a->vt->skip(a); }
