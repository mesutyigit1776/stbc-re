/* Warp travel between sets: WarpSequence (a TGSequence subclass), the "warp" set, ChangeRenderedSetAction and WarpEvent.
   From stbc.exe: WarpSequence::ctor_wrap 004d5b00, Play 004d66b0, NotifyExitedWarp 004d6230 (vslot Completed), GetWarpSequencePiece 004d60e0,
   AddAction{Before,During,PostDuring,After}Warp 004d6430..004d64c0 (AppendAction on the piece), GetWarpSet 004d6390, GetDestinationSet 004d6650,
   SetupSequence 004d60a0 (Python WarpSequence.SetupSequence(pWS)), SetPlacement 004d65e0, SetField_0x80 004d65b0 (event destination);
   ChangeRenderedSetAction::Play 00401270; WarpEvent accessors 0061c6d0...
   The sequence is built in the constructor from script actions (Actions.EffectScriptActions.SetWarpStage, Actions.ShipScriptActions.
   MoveBetweenSetsAction) and four piece sequences; WarpSequence.py links them when the sequence plays. The warp engine states are in tg_ship.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

void *tg_seq_sub_new(const char *cls, int (*pre)(void *, void *), void (*post)(void *, void *), void (*done)(void *, void *), void *user);
void *tg_instant_action_new(const char *cls, void (*fn)(void *, void *), void *user);
void tg_action_add_event(void *a, void *e); void *tg_action_user(void *a);
void *tg_space_new_set(const char *cls);
extern void *eng_TGSequence_Create(void); extern void *eng_TGSequence_Cast(void *); extern void *eng_TGAction_Cast(void *);
extern void eng_TGSequence_AddAction(void *, void *, void *, float); extern void eng_TGSequence_AppendAction(void *, void *, float);
extern PyObject *eng_TGScriptAction_Create(PyObject *);
extern void *eng_SetManager_GetSet(void *, char *); extern void eng_SetManager_AddSet(void *, void *, char *); extern void eng_SetManager_MakeRenderedSet(void *, char *);
extern const char *eng_SetClass_GetName(void *); extern void *eng_BaseObjectClass_GetContainingSet(void *);
extern void *eng_ShipClass_GetWarpEngineSubsystem(void *); extern long eng_ShipSubsystem_IsDisabled(void *);
extern long eng_DamageableObject_IsDead(void *); extern double eng_DamageableObject_GetLifeTime(void *);

enum { PRE, DURING, POST_DURING, POST, WARP_BEGIN, WARP_END, DEWARP_BEGIN, DEWARP_END, WARP_ENTER, DEWARP_FINISH, MOVE_1, MOVE_2, NPIECE };
typedef struct WS {
    unsigned ship;                    /* +0x34 ship id */
    void *origin;                     /* +0x38 the ship's set when the sequence was made (cleared when it plays) */
    char *module, *placement;         /* +0x3c destination region module, +0x40 placement name ("Player Start") */
    float time;                       /* +0x44 time to warp */
    char *episode, *mission;          /* +0x48, +0x4c */
    unsigned piece[NPIECE];           /* +0x50 .. +0x7c object ids of the pieces */
    unsigned ev_dest;                 /* +0x80 destination of ET_EXITED_WARP */
    float exit[3];                    /* +0x84 exit point */
} WS;
static WS *ws(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80fe) ? tg_action_user(p) : NULL; }
static char *dupz(const char *s) { return s ? strdup(s) : NULL; }
static void *live(unsigned id) { TGObject *o = id ? tg_find(id) : NULL; return o && !o->dead ? o : NULL; }
static void *to_ptr(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }

/* the "warp" set: made and added to the set manager the first time it is needed (004d6390) */
void *eng_WarpSequence_GetWarpSet(void)
{
    void *s = eng_SetManager_GetSet(NULL, "warp");
    if (!s) { s = tg_space_new_set("WarpSet"); eng_SetManager_AddSet(NULL, s, "warp"); }
    return s;
}
extern void *eng_SetClass_Cast(PyObject *);
void *eng_WarpSet_Cast(PyObject *o) { return eng_SetClass_Cast(o); }                 /* the warp set is a set (approximation: no class check) */
/* TGScriptAction(module, function, args...) as a pointer */
static void *script_action(const char *mod, const char *fn, PyObject *rest)
{
    int n = rest ? PyTuple_Size(rest) : 0, i; PyObject *t = PyTuple_New(n + 2), *r; void *a;
    PyTuple_SetItem(t, 0, PyString_FromString((char *)mod)); PyTuple_SetItem(t, 1, PyString_FromString((char *)fn));
    for (i = 0; i < n; i++) { PyObject *x = PyTuple_GetItem(rest, i); Py_INCREF(x); PyTuple_SetItem(t, i + 2, x); }
    r = eng_TGScriptAction_Create(t); Py_DECREF(t); Py_XDECREF(rest);
    a = to_ptr(r); Py_XDECREF(r); return a;
}
static unsigned id_of(void *p) { return p ? ((TGObject *)p)->id : 0; }
static void *stage_action(unsigned ship, int stage) { return script_action("Actions.EffectScriptActions", "SetWarpStage", Py_BuildValue("(ii)", (int)ship, stage)); }

/* Python module function with arguments (callPythonModuleFunction 006f8ab0 with a format); returns a new reference or NULL */
static PyObject *call_mod(const char *mod, const char *fn, PyObject *args)
{
    PyObject *m = PyImport_ImportModule((char *)mod), *f, *r = NULL;
    if (m) { f = PyObject_GetAttrString(m, (char *)fn); Py_DECREF(m); if (f) { r = PyEval_CallObject(f, args); Py_DECREF(f); } }
    Py_XDECREF(args);
    if (!r || PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); }
    return r;
}

/* Play 004d66b0: a dead or dying ship, or disabled warp engines, skip the sequence. Otherwise tell the warp engines which sequence runs
   (ET_SET_WARP_SEQUENCE now, and again with no source when the sequence completes), let WarpSequence.SetupSequence link the pieces,
   play, and send ET_START_WARP_NOTIFY (a WarpEvent) to the ship. */
static int ws_pre_play(void *seq, void *user)
{
    WS *w = user; void *ship = live(w->ship), *eng; TGEvent *e; PyObject *r;
    if (!ship) return 0;
    eng = eng_ShipClass_GetWarpEngineSubsystem(ship);
    if (eng && eng_ShipSubsystem_IsDisabled(eng)) return 0;
    if (eng_DamageableObject_IsDead(ship) || (float)eng_DamageableObject_GetLifeTime(ship) < 3.4028235e38f) return 0;      /* dying: lifetime below FLT_MAX (DAT_008e5c18) */
    e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const("ET_SET_WARP_SEQUENCE"); e->source = seq; e->dest = eng; tg_add_event(e);
    e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const("ET_SET_WARP_SEQUENCE"); e->source = NULL; e->dest = eng; tg_action_add_event(seq, e);
    eng_WarpSequence_GetWarpSet();
    r = call_mod("WarpSequence", "SetupSequence", Py_BuildValue("(N)", tg_obj_pyobj(seq)));     /* SetupSequence 004d60a0 */
    Py_XDECREF(r);
    return 1;
}
static void ws_post_play(void *seq, void *user)
{
    WS *w = user; void *ship = live(w->ship); TGEvent *e;
    if (ship) {
        e = tg_ev_new("WarpEvent", 0); e->type = (unsigned)tg_const("ET_START_WARP_NOTIFY"); e->dest = ship;
        e->str = dupz(w->module); e->v.s = dupz(w->placement); e->fval = w->time;
        tg_add_event(e);
    }
    w->origin = NULL;
}
static void ws_completed(void *seq, void *user)                                   /* NotifyExitedWarp 004d6230 */
{
    WS *w = user; void *d = live(w->ev_dest); TGEvent *e;
    if (!d) return;
    e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const("ET_EXITED_WARP"); e->source = live(w->ship); e->dest = d;
    tg_action_add_event(seq, e);
}

/* ctor_wrap 004d5b00 */
void *tg_warp_sequence_new(void *ship, const char *module, float time, const char *placement)
{
    WS *w = calloc(1, sizeof *w); void *s, *x; const char *dot, *dest;
    if (!ship || !tg_is_ours(ship)) { free(w); return NULL; }
    s = tg_seq_sub_new("WarpSequence", ws_pre_play, ws_post_play, ws_completed, w);
    w->ship = id_of(ship); w->origin = eng_BaseObjectClass_GetContainingSet(ship);
    w->module = dupz(module); w->time = time; w->placement = strdup(placement ? placement : "Player Start");
    x = eng_TGSequence_Create(); w->piece[PRE] = id_of(x); eng_TGSequence_AddAction(s, x, NULL, 0.0f);
    w->piece[WARP_BEGIN] = id_of(stage_action(w->ship, 1));
    w->piece[WARP_END] = id_of(stage_action(w->ship, 3));
    w->piece[WARP_ENTER] = id_of(stage_action(w->ship, 4));
    w->piece[DURING] = id_of(eng_TGSequence_Create());
    w->piece[POST_DURING] = id_of(eng_TGSequence_Create());
    dest = NULL; if (w->module) { dot = strrchr(w->module, '.'); dest = dot ? dot + 1 : w->module; }
    w->piece[MOVE_1] = id_of(script_action("Actions.ShipScriptActions", "MoveBetweenSetsAction", Py_BuildValue("(is)", (int)w->ship, eng_SetClass_GetName(eng_WarpSequence_GetWarpSet()))));
    w->piece[MOVE_2] = id_of(script_action("Actions.ShipScriptActions", "MoveBetweenSetsAction", Py_BuildValue("(iz)", (int)w->ship, dest)));
    w->piece[DEWARP_BEGIN] = id_of(stage_action(w->ship, 5));
    w->piece[DEWARP_END] = id_of(stage_action(w->ship, 7));
    w->piece[DEWARP_FINISH] = id_of(stage_action(w->ship, 0));
    w->piece[POST] = id_of(eng_TGSequence_Create());
    return s;
}
void *eng_WarpSequence_Create(void *ship, char *module, float time, char *placement) { return tg_warp_sequence_new(ship, module, time, placement); }
void *eng_WarpSequence_Cast(PyObject *o) { void *p = to_ptr(o); return ws(p) ? p : NULL; }
void eng_WarpSequence_Skip(void *p) { extern void eng_TGAction_Skip(void *); if (ws(p)) eng_TGAction_Skip(p); }
static void add_to_piece(void *p, int piece, void *a, float delay) { WS *w = ws(p); void *s = w ? eng_TGSequence_Cast(live(w->piece[piece])) : NULL; if (s && a) eng_TGSequence_AppendAction(s, a, delay); }
void eng_WarpSequence_AddActionBeforeWarp(void *p, void *a, float d) { add_to_piece(p, PRE, a, d); }
void eng_WarpSequence_AddActionDuringWarp(void *p, void *a, float d) { add_to_piece(p, DURING, a, d); }
void eng_WarpSequence_AddActionPostDuringWarp(void *p, void *a, float d) { add_to_piece(p, POST_DURING, a, d); }
void eng_WarpSequence_AddActionAfterWarp(void *p, void *a, float d) { add_to_piece(p, POST, a, d); }
void *eng_WarpSequence_GetWarpSequencePiece(void *p, int i)
{
    WS *w = ws(p); void *x;
    if (!w || i < 0 || i >= NPIECE) return NULL;
    x = live(w->piece[i]);
    return i <= POST ? eng_TGSequence_Cast(x) : eng_TGAction_Cast(x);
}
void *eng_WarpSequence_GetShip(void *p) { WS *w = ws(p); return w ? live(w->ship) : NULL; }
void *eng_WarpSequence_GetOrigin(void *p) { WS *w = ws(p); return w && w->origin && tg_is_ours(w->origin) && !((TGObject *)w->origin)->dead ? w->origin : NULL; }
const char *eng_WarpSequence_GetDestination(void *p) { WS *w = ws(p); return w ? w->module : NULL; }
void *eng_WarpSequence_GetDestinationSet(void *p)                                 /* 004d6650: module.GetSet() */
{
    WS *w = ws(p); PyObject *r; void *s;
    if (!w || !w->module) return NULL;
    r = call_mod(w->module, "GetSet", PyTuple_New(0)); s = to_ptr(r); Py_XDECREF(r);
    return s;
}
double eng_WarpSequence_GetTimeToWarp(void *p) { WS *w = ws(p); return w ? w->time : 0.0; }
const char *eng_WarpSequence_GetPlacementName(void *p) { WS *w = ws(p); return w ? w->placement : NULL; }
const char *eng_WarpSequence_GetDestinationMission(void *p) { WS *w = ws(p); return w ? w->mission : NULL; }
const char *eng_WarpSequence_GetDestinationEpisode(void *p) { WS *w = ws(p); return w ? w->episode : NULL; }
void tg_warp_sequence_set_episode(void *p, const char *ep, const char *mi) { WS *w = ws(p); if (!w) return; if (ep) { free(w->episode); w->episode = strdup(ep); } if (mi) { free(w->mission); w->mission = strdup(mi); } }
void eng_WarpSequence_SetExitPoint(void *p, void *pt) { WS *w = ws(p); if (w && pt) memcpy(w->exit, pt, 12); }
void *eng_WarpSequence_GetExitPoint(void *p) { WS *w = ws(p); return w ? tg_mem_dup(w->exit, 12) : NULL; }
void eng_WarpSequence_SetPlacement(void *p, char *n) { WS *w = ws(p); if (w) { free(w->placement); w->placement = dupz(n); } }
void eng_WarpSequence_SetEventDestination(void *p, PyObject *o) { WS *w = ws(p); void *d = to_ptr(o); if (w) w->ev_dest = d && tg_is_ours(d) ? id_of(d) : 0; }
void tg_warp_sequence_set_event_dest(void *p, void *d) { WS *w = ws(p); if (w) w->ev_dest = d && tg_is_ours(d) ? id_of(d) : 0; }

/* WarpEvent: +0x28 destination set (module), +0x2c placement name, +0x30 exit point, +0x3c duration */
static TGEvent *wev(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x812a) ? p : NULL; }
const char *eng_WarpEvent_GetDestinationSetName(void *p) { TGEvent *e = wev(p); return e ? e->str : NULL; }
const char *eng_WarpEvent_GetPlacementName(void *p) { TGEvent *e = wev(p); return e ? e->v.s : NULL; }
void *eng_WarpEvent_GetExitPoint(void *p) { static const float z[3]; return wev(p) ? tg_mem_dup(z, 12) : NULL; }
double eng_WarpEvent_GetDuration(void *p) { TGEvent *e = wev(p); return e ? e->fval : 0.0; }

/* ChangeRenderedSetAction (00401270): the set named at creation (CreateFromSet), else module.GetSet(); MakeRenderedSet; then completed */
typedef struct { char *set, *module; } CRS;
static void crs_play(void *a, void *user)
{
    CRS *c = user; void *s = c->set ? eng_SetManager_GetSet(NULL, c->set) : NULL;
    if (!s && c->module) { PyObject *r = call_mod(c->module, "GetSet", PyTuple_New(0)); s = to_ptr(r); Py_XDECREF(r); }
    if (s) eng_SetManager_MakeRenderedSet(NULL, (char *)eng_SetClass_GetName(s));
}
void *eng_ChangeRenderedSetAction_Create(char *module) { CRS *c = calloc(1, sizeof *c); c->module = dupz(module); return tg_instant_action_new("ChangeRenderedSetAction", crs_play, c); }
void *eng_ChangeRenderedSetAction_CreateFromSet(void *s) { CRS *c = calloc(1, sizeof *c); c->set = dupz(s ? eng_SetClass_GetName(s) : NULL); return tg_instant_action_new("ChangeRenderedSetAction", crs_play, c); }
void eng_ChangeRenderedSetAction_SetName(void *p, char *n) { CRS *c = p && tg_is_ours(p) && tg_is_type(p, 0x80fb) ? tg_action_user(p) : NULL; if (c) { free(c->set); c->set = dupz(n); } }
