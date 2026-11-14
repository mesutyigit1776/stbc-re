/* Proximity checks: ProximityManager (one per set), ProximityCheck (a sphere that sends an event when listed objects go inside or
   outside it) and ProximityEvent. The AI conditions (ConditionInRange and others) and AsteroidField use them.
   From stbc.exe: ProximityCheck::Create 005a5420 (event type + destination), AddObjectToCheckListByID 005a5c30, SetTriggerType 005a5eb0,
   RemoveAndDelete 005a6340, ProximityManager::AddObject 005a7640 / RemoveObject 005a7720 / Update 005a83a0.
   Approximations (marked): the exe finds the pairs with its collision system (ProcessCollisionPairs 005a8740); here every check tests
   its list each frame. CheckProximity 005a61c0 runs for every frame the collision pair overlaps: TT_INSIDE sends the event on each check
   while the object is inside (distance - radii < 0); here TT_OUTSIDE sends it when the object leaves the sphere. Only objects in the
   manager's set are tested. Sphere and objects use positions only (no parent
   rotation).
   Queries (GetNearObjects 005a78e0, GetLineIntersectObjects 005a7cf0, GetNextObject 005a8320): over the objects of the manager's set,
   without its sorted axis lists (same result). Approximation (marked): the planet atmosphere radius (Planet +0x6c) is not added yet. */
#include <Python.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

extern PyObject *appc_from_ptr(void *p, const char *cls);
extern int appc_to_ptr(PyObject *o, void **p, const char *type);
void *tg_space_new_obj(const char *cls, const char *name);
void **tg_space_obj_ext(void *o);
int tg_space_world_pos(void *o, float out[3]);
double eng_ObjectClass_GetRadius(void *);
void *eng_BaseObjectClass_GetContainingSet(void *);

enum { TT_INSIDE, TT_OUTSIDE, TT_INVALID };
typedef struct Entry { unsigned id; int tt, fired; } Entry;           /* fired: last state (1 inside, 0 outside, -1 not known) */
typedef struct Prox {
    unsigned ev_type; void *dest; float radius; int ignore_size;
    Entry *e; int n; unsigned *types; int ntypes; struct Mgr *mgr; void *self;
} Prox;
typedef struct Mgr { TGObject o; void *set; void **checks; int n; } Mgr;

static Mgr **mgrs; static int nmgrs;
static Prox *prox(void *p) { void **x; return p && tg_is_ours(p) && tg_is_type(p, 0x8125) && (x = tg_space_obj_ext(p)) ? *x : NULL; }
static Mgr *mgr(void *p) { int i; for (i = 0; i < nmgrs; i++) if ((void *)mgrs[i] == p) return mgrs[i]; return NULL; }

/* ---- ProximityManager ---- */
void *eng_SetClass_GetProximityManager(void *set)                          /* one manager per set, made on first use */
{
    int i; Mgr *m;
    if (!set) return NULL;
    for (i = 0; i < nmgrs; i++) if (mgrs[i]->set == set) return mgrs[i];
    m = tg_new_plain("ProximityManager", sizeof(Mgr)); m->set = set;
    mgrs = realloc(mgrs, (nmgrs + 1) * sizeof *mgrs); mgrs[nmgrs++] = m;
    return m;
}
void eng_ProximityManager_AddObject(void *mp, void *o)
{
    Mgr *m = mgr(mp); Prox *p = prox(o); int i;
    if (!m || !o) return;
    for (i = 0; i < m->n; i++) if (m->checks[i] == o) return;
    m->checks = realloc(m->checks, (m->n + 1) * sizeof *m->checks); m->checks[m->n++] = o;
    if (p) p->mgr = m;
}
void eng_ProximityManager_RemoveObject(void *mp, void *o)
{
    Mgr *m = mgr(mp); Prox *p = prox(o); int i;
    if (!m) return;
    for (i = 0; i < m->n; i++) if (m->checks[i] == o) { m->checks[i] = m->checks[--m->n]; break; }
    if (p && p->mgr == m) p->mgr = NULL;
}

/* ---- ProximityCheck ---- */
static void *prox_new(unsigned type, void *dest)
{
    void *o = tg_space_new_obj("ProximityCheck", NULL); Prox *p = calloc(1, sizeof *p);
    p->ev_type = type; p->dest = dest; p->self = o; *tg_space_obj_ext(o) = p;
    return o;
}
PyObject *eng_ProximityCheck_Create(PyObject *args)                         /* (event type, destination) */
{
    int type = 0; PyObject *d = NULL; void *dest = NULL;
    if (!PyArg_ParseTuple(args, "i|O", &type, &d)) return NULL;
    if (d && d != Py_None && !appc_to_ptr(d, &dest, "void")) { PyErr_Clear(); dest = NULL; }
    return appc_from_ptr(prox_new((unsigned)type, dest), "ProximityCheck");   /* a pointer string: App.py wraps it in ProximityCheckPtr */
}
void *eng_ProximityCheck_CreateWithEvent(PyObject *ev)
{
    void *e = NULL; TGEvent *t;
    if (ev && ev != Py_None && !appc_to_ptr(ev, &e, "void")) PyErr_Clear();
    t = e && tg_is_ours(e) && tg_is_type(e, 0x101) ? e : NULL;
    return prox_new(t ? t->type : 0, t ? (void *)t->dest : NULL);
}
void *eng_ProximityCheck_Cast(PyObject *o) { void *p = NULL; if (o && o != Py_None && !appc_to_ptr(o, &p, "void")) PyErr_Clear(); return prox(p) ? p : NULL; }
void eng_ProximityCheck_SetRadius(void *o, float r) { Prox *p = prox(o); if (p) p->radius = r; }
void eng_ProximityCheck_SetIgnoreObjectSize(void *o, int v) { Prox *p = prox(o); if (p) p->ignore_size = v != 0; }
long eng_ProximityCheck_GetIgnoreObjectSize(void *o) { Prox *p = prox(o); return p && p->ignore_size; }
static Entry *find(Prox *p, unsigned id) { int i; for (i = 0; i < p->n; i++) if (p->e[i].id == id) return &p->e[i]; return NULL; }
void eng_ProximityCheck_AddObjectToCheckListByID(void *o, long id, int tt)
{
    Prox *p = prox(o); Entry *e;
    if (!p || !id) return;
    if (!(e = find(p, (unsigned)id))) { p->e = realloc(p->e, (p->n + 1) * sizeof *p->e); e = &p->e[p->n++]; e->id = (unsigned)id; }
    e->tt = tt; e->fired = -1;
}
void eng_ProximityCheck_AddObjectToCheckList(void *o, void *x, int tt) { if (x && tg_is_ours(x)) eng_ProximityCheck_AddObjectToCheckListByID(o, (long)((TGObject *)x)->id, tt); }
void eng_ProximityCheck_AddObjectListToCheckList(void *o, PyObject *l, int tt, int unused)
{
    int i, n; if (!l || !PySequence_Check(l)) return;
    n = PySequence_Length(l);
    for (i = 0; i < n; i++) { PyObject *it = PySequence_GetItem(l, i); void *x = NULL; if (it && appc_to_ptr(it, &x, "void")) eng_ProximityCheck_AddObjectToCheckList(o, x, tt); else PyErr_Clear(); Py_XDECREF(it); }
}
void eng_ProximityCheck_AddObjectTypeToCheckList(void *o, int ct)
{
    Prox *p = prox(o); if (!p) return;
    p->types = realloc(p->types, (p->ntypes + 1) * sizeof *p->types); p->types[p->ntypes++] = (unsigned)ct;
}
void eng_ProximityCheck_RemoveObjectTypeFromCheckList(void *o, int ct) { Prox *p = prox(o); int i; if (p) for (i = 0; i < p->ntypes; i++) if (p->types[i] == (unsigned)ct) { p->types[i] = p->types[--p->ntypes]; break; } }
long eng_ProximityCheck_IsObjectInCheckList(void *o, void *x, PyObject *unused) { Prox *p = prox(o); return p && x && tg_is_ours(x) && find(p, ((TGObject *)x)->id) != NULL; }
long eng_ProximityCheck_GetTriggerType(void *o, void *x) { Prox *p = prox(o); Entry *e = p && x && tg_is_ours(x) ? find(p, ((TGObject *)x)->id) : NULL; return e ? e->tt : TT_INVALID; }
void eng_ProximityCheck_SetTriggerType(void *o, void *x, int tt) { Prox *p = prox(o); Entry *e = p && x && tg_is_ours(x) ? find(p, ((TGObject *)x)->id) : NULL; if (e) { e->tt = tt; e->fired = -1; } }
void eng_ProximityCheck_RemoveObjectFromCheckListByID(void *o, long id) { Prox *p = prox(o); Entry *e = p ? find(p, (unsigned)id) : NULL; if (e) *e = p->e[--p->n]; }
void eng_ProximityCheck_RemoveObjectFromCheckList(void *o, void *x) { if (x && tg_is_ours(x)) eng_ProximityCheck_RemoveObjectFromCheckListByID(o, (long)((TGObject *)x)->id); }
void eng_ProximityCheck_ClearCheckList(void *o) { Prox *p = prox(o); if (p) p->n = 0; }

static void send(Prox *p, void *x)
{
    TGEvent *e = tg_ev_new("ProximityEvent", 0);
    e->type = p->ev_type; e->dest = p->dest; e->source = p->self; e->v.p = x;
    tg_add_event(e);
}
static void check_one(Prox *p, Entry *e)
{
    TGObject *x = tg_find(e->id); float a[3], b[3], d2 = 0, r; int i, inside;
    if (!x || x->dead || e->tt == TT_INVALID) return;
    if (p->mgr && eng_BaseObjectClass_GetContainingSet(x) != p->mgr->set) { e->fired = -1; return; }   /* only objects in the check's set (the exe's collision pairs) */
    if (!tg_space_world_pos(p->self, a) || !tg_space_world_pos(x, b)) return;
    for (i = 0; i < 3; i++) d2 += (a[i] - b[i]) * (a[i] - b[i]);
    r = p->radius + (p->ignore_size ? 0.0f : (float)eng_ObjectClass_GetRadius(x));
    inside = d2 < r * r;
    if (e->tt == TT_INSIDE ? inside : (!inside && e->fired == 1)) send(p, x);             /* inside: every check while inside; outside: when it leaves */
    e->fired = inside;
}
void eng_ProximityCheck_CheckProximity(void *o, void *x)
{
    Prox *p = prox(o); Entry *e = p && x && tg_is_ours(x) ? find(p, ((TGObject *)x)->id) : NULL;
    if (e) check_one(p, e);
}
void eng_ProximityCheck_RemoveAndDelete(void *o)                           /* 005a6340: out of its manager, then deleted */
{
    Prox *p = prox(o); TGEvent *e;
    if (!p) return;
    if (p->mgr) eng_ProximityManager_RemoveObject(p->mgr, o);
    p->n = 0; p->dest = NULL;
    e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const("ET_DELETE_OBJECT_PUBLIC"); e->dest = o; e->source = o; tg_add_event(e);
    ((TGObject *)o)->dead = 1;
}

/* ---- ProximityEvent: the check is the source, the object is the payload ---- */
void *eng_new_ProximityEvent(void) { return tg_ev_new("ProximityEvent", 0); }
void eng_ProximityEvent_SetProximityCheck(void *e, void *c) { if (e) ((TGEvent *)e)->source = c; }
void *eng_ProximityEvent_GetProximityCheck(void *e) { return e ? ((TGEvent *)e)->source : NULL; }
void eng_ProximityEvent_SetObject(void *e, void *x) { if (e) ((TGEvent *)e)->v.p = x; }
void *eng_ProximityEvent_GetObject(void *e) { TGEvent *t = e; return t && t->v.p && tg_is_ours(t->v.p) && !((TGObject *)t->v.p)->dead ? t->v.p : NULL; }

/* every check of every manager, once per frame (from tg_ship_tick) */
void tg_prox_tick(void)
{
    int i, j, k;
    for (i = 0; i < nmgrs; i++) {
        Mgr *m = mgrs[i];
        for (j = 0; j < m->n; j++) {
            Prox *p = prox(m->checks[j]);
            if (!p || ((TGObject *)m->checks[j])->dead) continue;
            for (k = 0; k < p->n; k++) check_one(p, &p->e[k]);
        }
    }
}

/* ---- object queries: an iterator holds the matching objects (GetNextObject 005a8320 walks it; EndObjectIteration frees it) ---- */
typedef struct Iter { int pos, n; void **o; } Iter;
int tg_space_set_objects(void *sp, void ***out);
long eng_ObjectClass_GetCollisionFlags(void *);
static int query_skip(void *o)                      /* the manager holds ObjectClass objects with collision flag 1 (AddObject 005a7640), not ProximityChecks */
{ return !tg_is_ours(o) || ((TGObject *)o)->dead || !tg_is_type(o, 0x8003) || tg_is_type(o, 0x8125) || !(eng_ObjectClass_GetCollisionFlags(o) & 1); }
static void iter_add(Iter *it, void *o) { it->o = realloc(it->o, (it->n + 1) * sizeof *it->o); it->o[it->n++] = o; }
static PyObject *iter_obj(Iter *it) { return appc_from_ptr(it, "ProximityManagerIterator"); }
PyObject *eng_ProximityManager_GetNearObjects(void *mp, void *ptp, float r, int planets)          /* distance - bound radius <= r */
{
    Mgr *m = mgr(mp); Iter *it = calloc(1, sizeof *it); const float *c = ptp; void **objs; int n, i; float p[3], d[3];
    if (m && c) for (n = tg_space_set_objects(m->set, &objs), i = 0; i < n; i++) {
        if (query_skip(objs[i]) || !tg_space_world_pos(objs[i], p)) continue;
        d[0] = p[0] - c[0]; d[1] = p[1] - c[1]; d[2] = p[2] - c[2];
        if (sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - (float)eng_ObjectClass_GetRadius(objs[i]) <= r) iter_add(it, objs[i]);
    }
    return iter_obj(it);
}
PyObject *eng_ProximityManager_GetLineIntersectObjects(void *mp, void *ap, void *bp, float r, int planets)   /* bound sphere grown by r touches the segment */
{
    Mgr *m = mgr(mp); Iter *it = calloc(1, sizeof *it); const float *a = ap, *b = bp; void **objs; int n, i, k; float dir[3], len, p[3], q[3], t, rr, d2;
    if (!m || !a || !b) return iter_obj(it);
    for (k = 0; k < 3; k++) dir[k] = b[k] - a[k];
    len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 1e-6f) { dir[0] = dir[1] = dir[2] = 0; len = 0; } else for (k = 0; k < 3; k++) dir[k] /= len;
    for (n = tg_space_set_objects(m->set, &objs), i = 0; i < n; i++) {
        if (query_skip(objs[i]) || !tg_space_world_pos(objs[i], p)) continue;
        t = (p[0] - a[0]) * dir[0] + (p[1] - a[1]) * dir[1] + (p[2] - a[2]) * dir[2];        /* closest point of the segment */
        for (k = 0; k < 3; k++) q[k] = t < 0.0f ? a[k] : t > len ? b[k] : a[k] + dir[k] * t;
        rr = (float)eng_ObjectClass_GetRadius(objs[i]) + r;
        d2 = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
        if (d2 <= rr * rr) iter_add(it, objs[i]);
    }
    return iter_obj(it);
}
static Iter *iter_of(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }
void *eng_ProximityManager_GetNextObject(void *mp, PyObject *io)
{
    Iter *it = iter_of(io);
    while (it && it->pos < it->n) { void *o = it->o[it->pos++]; if (tg_is_ours(o) && !((TGObject *)o)->dead) return o; }
    return NULL;
}
void eng_ProximityManager_EndObjectIteration(void *mp, PyObject *io) { Iter *it = iter_of(io); if (it) { free(it->o); free(it); } }
