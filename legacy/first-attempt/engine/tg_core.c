/* Totally Games core classes: TGObject ids, TGString, TGEvent family, event handlers, event manager, timers.
   Behavior follows stbc.exe; the addresses in the comments are the functions this code replaces.
   Not byte-compatible with the original structures (scripts only see opaque pointers). */
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "tg_core.h"

/* ------------------------------------------------------------------------------------------------ */
/* Class table. CT ids come from the IsTypeOf chains (006d5cf0, 006dd040, 004371e0, ...). */
static const struct cls classes[] = {
    {"TGObject",               2,     NULL,           {2}},
    {"TGEvent",                0x101, "TGObject",     {0x101, 2}},
    {"TGEventHandlerObject",   0x102, "TGObject",     {0x102, 4, 3, 2}},
    {"TGTimer",                0x103, "TGObject",     {0x103, 2}},
    {"TGBoolEvent",            0x104, "TGEvent",      {0x104, 0x101, 2}},
    {"TGCharEvent",            0x105, "TGEvent",      {0x105, 0x101, 2}},
    {"TGShortEvent",           0x106, "TGEvent",      {0x106, 0x101, 2}},
    {"TGIntEvent",             0x107, "TGEvent",      {0x107, 0x101, 2}},
    {"TGFloatEvent",           0x108, "TGEvent",      {0x108, 0x101, 2}},
    {"TGStringEvent",          0x109, "TGEvent",      {0x109, 0x101, 2}},
    {"TGVoidPtrEvent",         0x10a, "TGEvent",      {0x10a, 0x101, 2}},
    {"TGPythonInstanceWrapper",0x10b, "TGEventHandlerObject", {0x10b, 0x102, 4, 3, 2}},
    {"TGObjPtrEvent",          0x10c, "TGEvent",      {0x10c, 0x101, 2}},
    {"TGAction",               0x701, "TGObject",     {0x701, 2}},
    {"TGActionManager",        0x702, "TGEventHandlerObject", {0x702, 0x102, 4, 3, 2}},
    {"TGCondition",            0x705, "TGObject",     {0x705, 2}},
    {"TGConditionAction",      0x706, "TGAction",     {0x706, 0x701, 2}},
    {"TGSequence",             0x707, "TGAction",     {0x707, 0x701, 2}},
    {"TGTimedAction",          0x708, "TGAction",     {0x708, 0x701, 2}},
    {"TGScriptAction",         0x70a, "TGAction",     {0x70a, 0x701, 2}},
    {"TGSoundAction",          0x70c, "TGTimedAction",{0x70c, 0x708, 0x701, 2}},
    {"TGAnimAction",           0x709, "TGTimedAction",{0x709, 0x708, 0x701, 2}},
    {"ScriptObject",           0x801, "TGEventHandlerObject", {0x801, 0x102, 4, 3, 2}},
    {"Game",                   0x802, "ScriptObject", {0x802, 0x801, 0x102, 4, 3, 2}},
    {"Episode",                0x803, "ScriptObject", {0x803, 0x801, 0x102, 4, 3, 2}},
    {"Mission",                0x804, "ScriptObject", {0x804, 0x801, 0x102, 4, 3, 2}},
    {"TGLocalizationDatabase", 0xf001, NULL,          {0xf001}},
    {"ConditionScript",        0x8037, "TGCondition", {0x8037, 0x705, 2}},
    {"ConditionEventCreator",  0x8038, "TGObject",    {0x8038, 2}},
    {"ObjectGroup",            0x805, "TGEventHandlerObject", {0x805, 0x102, 4, 3, 2}},
    {"ObjectGroupWithInfo",    0x806, "ObjectGroup",  {0x806, 0x805, 0x102, 4, 3, 2}},
    {NULL, 0, NULL, {0}}
};
/* classes without an IsTypeOf in the exe (not TGObjects); synthetic CT ids from 0x10000 */
static const struct cls plain_classes[] = {
    {"TGSound",             0x10001, NULL, {0x10001}},
    {"TimeSliceProcess",    0x10002, NULL, {0x10002}},
    {"PythonMethodProcess", 0x10003, "TimeSliceProcess", {0x10003, 0x10002}},
    {"FuzzyLogic",          0x10004, NULL, {0x10004}},
    {"LODModel",            0x10005, NULL, {0x10005}},
    {"TGFontGroup",         0x10006, NULL, {0x10006}},
    {"TGIconGroup",         0x10007, NULL, {0x10007}},
    {"TGUITheme",           0x10008, NULL, {0x10008}},
    {"GraphicsModeInfo",    0x10009, NULL, {0x10009}},
    {"TGModelPropertySet",  0x1000a, NULL, {0x1000a}},
    {"TGModelPropertyList", 0x1000b, NULL, {0x1000b}},
    {"TGModelPropertyInstance", 0x1000c, NULL, {0x1000c}},
    {"TGWindow",            0x1000d, "TGPane", {0x1000d, 0x20c, 0x204, 0x201, 0x102, 0x4, 0x3, 0x2}},
    {"ArtificialIntelligence", 0x1000e, NULL, {0x1000e}},
    {"TorpedoAmmoType",     0x1000f, NULL, {0x1000f}},       /* all AI kinds (tg_ai.c); not in the exe class table */
    {"FloatRangeWatcher",   0x10020, NULL, {0x10020}},       /* subsystem condition range checks (tg_ship.c) */
    {"UtopiaApp",           0x10021, NULL, {0x10021}},       /* the application object (tg_services.c) */
    {"TextureAnimContainer", 0x10022, NULL, {0x10022}},      /* animated texture sheets (tg_services.c) */
    {"TextureAnimTrack",    0x10023, NULL, {0x10023}},
    {"EffectAction",        0x10024, "TGTimedAction", {0x10024, 0x708, 0x701, 2}},   /* particle effects (tg_effects.c) */
    {"NiNode",              0x10025, NULL, {0x10025}},       /* effect root, object node and model point handles */
    {"AnimTSParticleController", 0x10026, NULL, {0x10026}},
    {"SparkParticleController", 0x10027, NULL, {0x10027}},
    {"ExplosionPlumeController", 0x10028, NULL, {0x10028}},
    {NULL, 0, NULL, {0}}
};
static const struct cls gen_classes[] = {
#include "classes_gen.inc"
    {NULL, 0, NULL, {0}}
};
static const struct cls *cls_by_name(const char *n)
{
    const struct cls *c;
    for (c = classes; c->name; c++) if (!strcmp(c->name, n)) return c;
    for (c = gen_classes; c->name; c++) if (!strcmp(c->name, n)) return c;
    for (c = plain_classes; c->name; c++) if (!strcmp(c->name, n)) return c;
    return NULL;
}

/* ---- object id table (TGObject::TGObject 006f0a70, addObjectToIDTable) ---- */
static unsigned id_counter = 1;          /* DAT_0095b078 starts at 1 */
static int id_increment = 1;             /* DAT_0095b07d starts at 1 */
#define IDTAB 4093
static struct idn { TGObject *o; struct idn *next; } *idtab[IDTAB];

/* set of all object pointers (open addressing) so that tg_is_ours is O(1) */
static void **ptab; static size_t ptab_cap, ptab_n;
static size_t ptr_hash(void *p) { size_t x = (size_t)p; x ^= x >> 4; x *= 0x9E3779B97F4A7C15ull; return x >> 20; }
static void ptr_add(void *p)
{
    size_t i;
    if ((ptab_n + 1) * 2 > ptab_cap) {
        void **old = ptab; size_t oc = ptab_cap, k;
        ptab_cap = ptab_cap ? ptab_cap * 2 : 4096; ptab = calloc(ptab_cap, sizeof *ptab); ptab_n = 0;
        for (k = 0; k < oc; k++) if (old[k]) { for (i = ptr_hash(old[k]) & (ptab_cap - 1); ptab[i]; i = (i + 1) & (ptab_cap - 1)) ; ptab[i] = old[k]; ptab_n++; }
        free(old);
    }
    for (i = ptr_hash(p) & (ptab_cap - 1); ptab[i]; i = (i + 1) & (ptab_cap - 1)) ;
    ptab[i] = p; ptab_n++;
}

void *tg_new(const char *clsname, size_t size, long want_id)
{
    TGObject *o = calloc(1, size); struct idn *n; unsigned id;
    o->cls = cls_by_name(clsname);
    if (want_id == 0) { id = id_counter; id_counter = id + 1; }
    else { id = (unsigned)want_id; if (id_increment && (int)id >= (int)id_counter) id_counter = id + 1; }
    o->id = id;
    n = malloc(sizeof *n); n->o = o; n->next = idtab[id % IDTAB]; idtab[id % IDTAB] = n;
    ptr_add(o);
    return o;
}
void *tg_str_tmp(const char *s) { extern void *eng_new_TGString(char *); return eng_new_TGString((char *)(s ? s : "")); }   /* for provisional constructors (not freed) */
int tg_class_is(const char *clsname, unsigned ct) { TGObject tmp; memset(&tmp, 0, sizeof tmp); tmp.cls = cls_by_name(clsname); return tmp.cls && tg_is_type(&tmp, ct); }
/* an object that is not a TGObject: registered as ours, no id, not in the id table */
void *tg_new_plain(const char *clsname, size_t size)
{
    TGObject *o = calloc(1, size); o->cls = cls_by_name(clsname); ptr_add(o); return o;
}
TGObject *tg_find(unsigned id)
{
    struct idn *n;
    if (!id) return NULL;
    for (n = idtab[id % IDTAB]; n; n = n->next) if (n->o->id == id && !n->o->dead) return n->o;
    return NULL;
}
int tg_is_ours(void *p)
{
    size_t i;
    if (!p || !ptab_n) return 0;
    for (i = ptr_hash(p) & (ptab_cap - 1); ptab[i]; i = (i + 1) & (ptab_cap - 1)) if (ptab[i] == p) return 1;
    return 0;
}
int tg_is_type(TGObject *o, unsigned ct)
{
    int i; if (!o || !o->cls) return 0;
    for (i = 0; i < 12 && o->cls->chain[i]; i++) if (o->cls->chain[i] == ct) return 1;
    return 0;
}
static const char *type_name(void *p)
{
    const char *m;
    if (!p) return "TGObject";
    m = appc_memo_get(p);
    if (m) return m;
    return tg_is_ours(p) ? ((TGObject *)p)->cls->name : "TGObject";
}
static void *arg_ptr(PyObject *o)           /* None / instance / pointer string -> pointer; errors become NULL */
{
    void *p = NULL;
    if (!o || o == Py_None) return NULL;
    if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; }
    return p;
}

long eng_TGObject_GetObjType(void *o) { return tg_is_ours(o) ? ((TGObject *)o)->cls->ct : 0; }
long eng_TGObject_IsTypeOf(void *o, int ct) { return tg_is_ours(o) && tg_is_type((TGObject *)o, ct); }
long eng_TGObject_GetObjID(void *o) { return tg_is_ours(o) ? ((TGObject *)o)->id : 0; }
void eng_TGObject_Destroy(void *o) { if (tg_is_ours(o)) ((TGObject *)o)->dead = 1; }
void eng_TGObject_SetIncrementID(int v) { id_increment = v != 0; }
long eng_TGObject_IsIncrementID(void) { return id_increment; }
void *eng_TGObject_GetTGObjectPtr(long id) { return tg_find((unsigned)id); }
const char *eng_TGObject_GetIndentString(int n) { static char b[64]; int i; if (n < 0) n = 0; if (n > 60) n = 60; for (i = 0; i < n; i++) b[i] = ' '; b[n] = 0; return b; }

/* ------------------------------------------------------------------------------------------------ */
/* TGString: 16-bit characters (std::wstring in the original). Narrow text widens by zero extension (SetString 006f47c0);
   GetCString (006f56f0) narrows and replaces characters above 0xff by 1. */
typedef struct { unsigned short *buf; int len; } TGStr;
static void str_set(TGStr *s, const char *c)
{
    int n = c ? (int)strlen(c) : 0, i;
    free(s->buf); s->buf = malloc((n + 1) * 2);
    for (i = 0; i < n; i++) s->buf[i] = (unsigned char)c[i];
    s->buf[n] = 0; s->len = n;
}
static const unsigned short empty16[1] = {0};
static const unsigned short *str_data(TGStr *s) { return s->buf ? s->buf : empty16; }
static void *str_new(const char *c) { TGStr *s = calloc(1, sizeof *s); str_set(s, c); return s; }
static int wcmp(const unsigned short *a, const unsigned short *b, int fold)
{
    for (;; a++, b++) {
        unsigned x = *a, y = *b;
        if (fold) { if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; }
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}
static int str_find(TGStr *h, TGStr *n, int start, int case_sensitive)   /* TGString::Find 006f4260 */
{
    const unsigned short *a = str_data(h), *b = str_data(n); int i, j;
    if (start < 0) return -1;
    for (i = start; i <= h->len; i++) {
        for (j = 0; j < n->len; j++) {
            if (i + j >= h->len) break;
            if (wcmp(a + i + j, b + j, !case_sensitive) != 0 && ((a[i + j] | (!case_sensitive ? 32 : 0)) != (b[j] | (!case_sensitive ? 32 : 0)))) break;
            if (a[i + j] != b[j] && case_sensitive) break;
        }
        if (j == n->len) return i;
    }
    return -1;
}
void *eng_new_TGString(char *c) { return str_new(c); }
/* a TGString from UTF-16 data (used by the localization databases) */
void *tg_str_new16(const unsigned short *w, int len) { TGStr *s = calloc(1, sizeof *s); s->buf = malloc((len + 1) * 2); memcpy(s->buf, w, len * 2); s->buf[len] = 0; s->len = len; return s; }
void *tg_mem_dup(const void *p, size_t n) { void *q; if (!p) return NULL; q = malloc(n); memcpy(q, p, n); return q; }
void *tg_str_copy(void *p) { TGStr *a = p; return a ? tg_str_new16(a->buf ? a->buf : empty16, a->len) : str_new(""); }
void tg_str_assign(void *dst, void *src) { TGStr *d = dst, *a = src; if (!d) return; free(d->buf); if (!a) { d->buf = NULL; d->len = 0; return; } d->buf = malloc((a->len + 1) * 2); memcpy(d->buf, a->buf ? a->buf : empty16, a->len * 2); d->buf[a->len] = 0; d->len = a->len; }
void eng_delete_TGString(void *p) { TGStr *s = p; if (s) { free(s->buf); free(s); } }
long eng_TGString_GetLength(void *p) { return ((TGStr *)p)->len; }
void eng_TGString_SetString(void *p, char *c) { str_set(p, c); }
const char *eng_TGString_GetCString(void *p)
{
    static char buf[4][1024]; static int k; char *o = buf[k++ & 3]; TGStr *s = p; int i, n = s->len > 1022 ? 1022 : s->len;
    for (i = 0; i < n; i++) o[i] = s->buf[i] > 0xff ? 1 : (char)s->buf[i];
    o[n] = 0; return o;
}
/* FindC 006f41e0 always passes flag 0 to Find (the Python flag argument is ignored): case-insensitive search. */
long eng_TGString_FindC(void *p, PyObject *str, int start, int flag)
{
    char *c = NULL; TGStr tmp = {0, 0}; int r;
    if (!appc_to_cstr(str, &c)) { PyErr_Clear(); return -1; }
    str_set(&tmp, c); r = str_find(p, &tmp, start, 0); free(tmp.buf); return r;
}
/* Find: flag nonzero = case sensitive, 0 = case insensitive (lowercases both strings) */
long eng_TGString_Find(void *p, PyObject *other, int start, int flag)
{
    void *o = arg_ptr(other); if (!o) return -1;
    return str_find(p, o, start, flag != 0);
}
/* Compare 006f4450: flag 0 = wcscmp (exact), nonzero = case-insensitive (_wcsicmp). CompareC 006f43e0 always uses flag 0. */
long eng_TGString_CompareC(void *p, char *c, int flag) { TGStr tmp = {0, 0}; int r; str_set(&tmp, c); r = wcmp(str_data(p), str_data(&tmp), 0); free(tmp.buf); return r; }
long eng_TGString_Compare(void *p, void *q, int flag) { return wcmp(str_data(p), str_data(q), flag != 0); }
/* Append 005c2270 returns a new string: this + other (std::wstring operator+, 00416a60) */
void *eng_TGString_Append(void *p, void *q)
{
    TGStr *a = p, *b = q, *r = calloc(1, sizeof *r);
    r->len = a->len + b->len; r->buf = malloc((r->len + 1) * 2);
    if (a->len) memcpy(r->buf, a->buf, a->len * 2);
    if (b->len) memcpy(r->buf + a->len, b->buf, b->len * 2);
    r->buf[r->len] = 0; return r;
}

/* ------------------------------------------------------------------------------------------------ */
/* Events (TGEvent 006d5c00 and subclasses) */
void *tg_ev_new(const char *cn, long id) { TGEvent *e = tg_new(cn, sizeof(TGEvent), id); e->timestamp = -1.0f; return e; }
static TGEvent *ev(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x101) ? p : NULL; }

void *eng_new_TGEvent(long id) { return tg_ev_new("TGEvent", id); }
void *eng_TGEvent_Create(void) { return tg_ev_new("TGEvent", 0); }
void eng_delete_TGEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
void *eng_TGEvent_Cast(PyObject *o) { void *p = arg_ptr(o); return ev(p); }
long eng_TGEvent_GetEventType(void *p) { return ev(p) ? (int)ev(p)->type : 0; }
void eng_TGEvent_SetEventType(void *p, int t) { if (ev(p)) ev(p)->type = (unsigned)t; }
PyObject *eng_TGEvent_GetSource(void *p) { TGEvent *e = ev(p); if (!e || !e->source) { Py_INCREF(Py_None); return Py_None; } return appc_from_ptr(e->source, type_name(e->source)); }
PyObject *eng_TGEvent_GetDestination(void *p)
{
    TGEvent *e = ev(p);
    if (!e || !e->dest) { Py_INCREF(Py_None); return Py_None; }
    return appc_from_ptr(e->dest, type_name(e->dest));
}
void eng_TGEvent_SetSource(void *p, PyObject *o) { TGEvent *e = ev(p); if (e) e->source = arg_ptr(o); }
void eng_TGEvent_SetDestination(void *p, PyObject *o) { TGEvent *e = ev(p); if (e) e->dest = arg_ptr(o); }
double eng_TGEvent_GetTimestamp(void *p) { return ev(p) ? ev(p)->timestamp : 0.0; }
void eng_TGEvent_SetTimestamp(void *p, float t) { if (ev(p)) ev(p)->timestamp = t; }
static void flag_set(void *p, unsigned f, int on) { TGEvent *e = ev(p); if (e) { if (on) e->flags |= f; else e->flags &= ~f; } }
void eng_TGEvent_SetLogged(void *p, int v) { flag_set(p, EVF_LOGGED, v); }
long eng_TGEvent_IsLogged(void *p) { return ev(p) && (ev(p)->flags & EVF_LOGGED) ? 1 : 0; }
void eng_TGEvent_SetPrivate(void *p, int v) { flag_set(p, EVF_PRIVATE, v); }
long eng_TGEvent_IsPrivate(void *p) { return ev(p) && (ev(p)->flags & EVF_PRIVATE) ? 1 : 0; }
void eng_TGEvent_SetNotSaved(void *p, int v) { flag_set(p, EVF_NOTSAVED, v); }
long eng_TGEvent_IsNotSaved(void *p) { return ev(p) && (ev(p)->flags & EVF_NOTSAVED) ? 1 : 0; }
void eng_TGEvent_IncRefCount(void *p) { if (ev(p)) ev(p)->refcount++; }
void eng_TGEvent_DecRefCount(void *p) { TGEvent *e = ev(p); if (e && e->refcount > 0) e->refcount--; }   /* TGEvent::DecRefCount 006d6300 clamps at 0; the memory stays valid for script references */
PyObject *eng_TGEvent_GetRefCount(void *p) { return PyInt_FromLong(ev(p) ? ev(p)->refcount : 0); }

/* CopyFrom (vslot 0x30, 006d6230): type, source, destination, timestamp, flags; subclasses add their payload */
static void ev_copy(TGEvent *d, TGEvent *s)
{
    d->type = s->type; d->source = s->source; d->dest = s->dest; d->timestamp = s->timestamp; d->flags = s->flags;
    d->v = s->v; d->mx = s->mx; d->my = s->my; d->ukey = s->ukey; d->kdata = s->kdata; d->kstate = s->kstate;
    if (s->str) { free(d->str); d->str = strdup(s->str); }
}
long eng_TGEvent_Copy(void *to, void *from) { TGEvent *a = ev(to), *b = ev(from); if (!a || !b) return 0; ev_copy(a, b); return 1; }
void *eng_TGEvent_Duplicate(void *p) { TGEvent *s = ev(p), *d; if (!s) return NULL; d = tg_ev_new(s->o.cls->name, 0); ev_copy(d, s); return d; }
static TGEvent *ev_clone(TGEvent *s) { TGEvent *d = tg_ev_new(s->o.cls->name, 0); ev_copy(d, s); return d; }

#define EVCLASS(Cls, Get, Set, field, gettype, settype)                                                        \
    void *eng_new_##Cls(long id) { return tg_ev_new(#Cls, id); }                                                  \
    void *eng_##Cls##_Create(void) { return tg_ev_new(#Cls, 0); }                                                 \
    void eng_delete_##Cls(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }                                   \
    void *eng_##Cls##_Cast(PyObject *o) { void *p = arg_ptr(o); return (tg_is_ours(p) && tg_is_type(p, ((TGObject *)p)->cls->ct) && !strcmp(((TGObject *)p)->cls->name, #Cls)) || (ev(p) && cls_by_name(#Cls) && tg_is_type(p, cls_by_name(#Cls)->ct)) ? p : NULL; } \
    gettype eng_##Cls##_##Get(void *p) { return ev(p) ? ev(p)->field : 0; }                                   \
    void eng_##Cls##_##Set(void *p, settype v) { if (ev(p)) ev(p)->field = v; }
EVCLASS(TGCharEvent, GetChar, SetChar, v.i, long, int)
EVCLASS(TGShortEvent, GetShort, SetShort, v.i, long, int)
EVCLASS(TGIntEvent, GetInt, SetInt, v.i, long, int)
EVCLASS(TGFloatEvent, GetFloat, SetFloat, v.f, double, float)
/* TGIEvent (input events): bit 0x100 of the flags is "handled" (byte +0x19 bit 0 in the exe), TGMouseEvent keeps its flags in the same field
   (MEF_BUTTON_DOWN 0x200, UP 0x400, left 0x800, right 0x1000, middle 0x1800) and a position, TGKeyboardEvent a key, key data and state */
long eng_TGIEvent_EventHandled(void *p) { TGEvent *e = ev(p); return e && (e->flags & 0x100) != 0; }
void eng_TGIEvent_SetHandled(void *p) { TGEvent *e = ev(p); if (e) e->flags |= 0x100; }
void *eng_new_TGIEvent(void) { return tg_ev_new("TGIEvent", 0); }
void *eng_new_TGKeyboardEvent(void) { return tg_ev_new("TGKeyboardEvent", 0); }
void *eng_TGKeyboardEvent_Create(int unicode, long keydata, int state) { TGEvent *e = tg_ev_new("TGKeyboardEvent", 0); e->type = (unsigned)tg_const("ET_KEYBOARD"); e->ukey = unicode; e->kdata = (int)keydata; e->kstate = state; return e; }
void eng_delete_TGKeyboardEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
long eng_TGKeyboardEvent_GetUnicode(void *p) { return ev(p) ? ev(p)->ukey : 0; }
void eng_TGKeyboardEvent_SetUnicode(void *p, int v) { if (ev(p)) ev(p)->ukey = v; }
long eng_TGKeyboardEvent_GetKeyData(void *p) { return ev(p) ? ev(p)->kdata : 0; }
void eng_TGKeyboardEvent_SetKeyData(void *p, long v) { if (ev(p)) ev(p)->kdata = (int)v; }
long eng_TGKeyboardEvent_GetKeyState(void *p) { return ev(p) ? ev(p)->kstate : 0; }
void eng_TGKeyboardEvent_SetKeyState(void *p, int v) { if (ev(p)) ev(p)->kstate = v; }
long eng_TGKeyboardEvent_Copy(void *to, PyObject *from) { TGEvent *a = ev(to), *b = ev(arg_ptr(from)); if (!a || !b) return 0; ev_copy(a, b); return 1; }
void *eng_new_TGMouseEvent(void) { return tg_ev_new("TGMouseEvent", 0); }
void *eng_TGMouseEvent_Create(float x, float y, int flags) { TGEvent *e = tg_ev_new("TGMouseEvent", 0); e->type = (unsigned)tg_const("ET_MOUSE"); e->mx = x; e->my = y; e->flags = (unsigned short)flags; return e; }
void eng_delete_TGMouseEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
long eng_TGMouseEvent_GetFlags(void *p) { return ev(p) ? ev(p)->flags : 0; }
void eng_TGMouseEvent_SetFlags(void *p, int v) { if (ev(p)) ev(p)->flags = (unsigned short)v; }
long eng_TGMouseEvent_IsButtonEvent(void *p) { return ev(p) && (ev(p)->flags & 0x600) != 0; }
long eng_TGMouseEvent_GetButtonNum(void *p) { return ev(p) ? (ev(p)->flags & 0x1800) : 0; }
double eng_TGMouseEvent_GetX(void *p) { return ev(p) ? ev(p)->mx : 0.0; }
double eng_TGMouseEvent_GetY(void *p) { return ev(p) ? ev(p)->my : 0.0; }
long eng_TGMouseEvent_Copy(void *to, PyObject *from) { TGEvent *a = ev(to), *b = ev(arg_ptr(from)); if (!a || !b) return 0; ev_copy(a, b); return 1; }
void *eng_new_TGBoolEvent(long id) { return tg_ev_new("TGBoolEvent", id); }
void *eng_TGBoolEvent_Create(void) { return tg_ev_new("TGBoolEvent", 0); }
void eng_delete_TGBoolEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
void *eng_TGBoolEvent_Cast(PyObject *o) { void *p = arg_ptr(o); return ev(p) && tg_is_type(p, 0x104) ? p : NULL; }
long eng_TGBoolEvent_GetBool(void *p) { return ev(p) ? ev(p)->v.i != 0 : 0; }
void eng_TGBoolEvent_SetBool(void *p, int v) { if (ev(p)) ev(p)->v.i = v != 0; }
void eng_TGBoolEvent_ToggleBool(void *p) { if (ev(p)) ev(p)->v.i = !ev(p)->v.i; }
void *eng_new_TGStringEvent(long id) { return tg_ev_new("TGStringEvent", id); }
void *eng_TGStringEvent_Create(void) { return tg_ev_new("TGStringEvent", 0); }
void eng_delete_TGStringEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
void *eng_TGStringEvent_Cast(PyObject *o) { void *p = arg_ptr(o); return ev(p) && tg_is_type(p, 0x109) ? p : NULL; }
/* GetString(self, TGString-out): copies the event's string into the TGString object given */
void eng_TGStringEvent_GetString(void *p, PyObject *out) { TGEvent *e = ev(p); void *s = arg_ptr(out); if (e && s) str_set(s, e->str ? e->str : ""); }
void eng_TGStringEvent_SetString(void *p, char *c) { TGEvent *e = ev(p); if (e) { free(e->str); e->str = strdup(c ? c : ""); } }
void *eng_new_TGVoidPtrEvent(long id) { return tg_ev_new("TGVoidPtrEvent", id); }
void *eng_TGVoidPtrEvent_Create(void) { return tg_ev_new("TGVoidPtrEvent", 0); }
void eng_delete_TGVoidPtrEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
PyObject *eng_TGVoidPtrEvent_GetVoidPtr(void *p) { TGEvent *e = ev(p); return appc_from_ptr(e ? e->v.p : NULL, "void"); }
void eng_TGVoidPtrEvent_SetVoidPtr(void *p, char *c) { TGEvent *e = ev(p); if (e) e->v.p = c; }
void *eng_new_TGObjPtrEvent(long id) { return tg_ev_new("TGObjPtrEvent", id); }
void *eng_TGObjPtrEvent_Create(void) { return tg_ev_new("TGObjPtrEvent", 0); }
void eng_delete_TGObjPtrEvent(void *p) { if (ev(p)) ((TGObject *)p)->dead = 1; }
void *eng_TGObjPtrEvent_Cast(PyObject *o) { void *p = arg_ptr(o); return ev(p) && tg_is_type(p, 0x10c) ? p : NULL; }
/* TGObjPtrEvent keeps the object ID at +0x28 (py_TGObjPtrEvent_SetObjPtr 005c80e0); GetObjPtr (005c8070) looks it up again, so a
   freed object reads as None, and the action manager handlers (006ff2d0) find the action by the same field. Objects that are only
   marked dead (removed from a set, SetDead) are still here: the event may be handled after the removal (ET_EXITED_SET). */
PyObject *eng_TGObjPtrEvent_GetObjPtr(void *p)
{
    TGEvent *e = ev(p); void *o = NULL; struct idn *n;
    if (e && e->ref_id) { for (n = idtab[e->ref_id % IDTAB]; n && !o; n = n->next) if (n->o->id == e->ref_id) o = n->o; }
    else if (e) o = e->v.p;
    if (!o) { Py_INCREF(Py_None); return Py_None; }
    return appc_from_ptr(o, type_name(o));
}
void eng_TGObjPtrEvent_SetObjPtr(void *p, PyObject *o)
{
    TGEvent *e = ev(p); void *x = arg_ptr(o);
    if (e) { e->v.p = x; e->ref_id = x && tg_is_ours(x) ? ((TGObject *)x)->id : 0; }
}

/* ------------------------------------------------------------------------------------------------ */
/* Handlers. Instance handlers (addHandlerToHandlerMap 006d7eb0) are inserted at the HEAD of the per-type chain; HandleEvent
   (006d9240) runs the head and CallNextHandler (006d90e0 -> 006d83e0) runs the following one. Broadcast handlers (FUN_006db380)
   carry a destination filter: ANY_TARGET fires always, NULL when the event has no destination, an object when it is the destination. */
static struct handler *inst_head;     /* all instance handlers; per object/type chains are walked in list order (newest first) */
static struct handler *bcast_head, *bcast_tail;

static struct handler *hnew(int type, void *target, const char *name, int method)
{
    struct handler *h = calloc(1, sizeof *h);
    h->type = type; h->target = target; h->name = strdup(name ? name : ""); h->is_method = method;
    return h;
}
long eng_TGEventHandlerObject_AddPythonFuncHandlerForInstance(void *self, int type, char *name)
{ struct handler *h = hnew(type, self, name, 0); h->owner = self; h->next = inst_head; inst_head = h; return 1; }
long eng_TGEventHandlerObject_AddPythonMethodHandlerForInstance(void *self, int type, char *name)
{ struct handler *h = hnew(type, self, name, 1); h->owner = self; h->next = inst_head; inst_head = h; return 1; }
long eng_TGEventHandlerObject_RemoveHandlerForInstance(void *self, int type, char *name)
{
    struct handler *h;
    for (h = inst_head; h; h = h->next)
        if (!h->deleted && h->owner == self && h->type == type && !strcmp(h->name, name ? name : "")) { h->deleted = 1; return 1; }
    return 0;
}
long eng_TGEventHandlerObject_RemoveAllInstanceHandlers(void *self)
{ struct handler *h; for (h = inst_head; h; h = h->next) if (h->owner == self) h->deleted = 1; return 1; }
void eng_delete_TGEventHandlerObject(void *p) { if (tg_is_ours(p)) ((TGObject *)p)->dead = 1; }
void *eng_TGEventHandlerObject_Cast(PyObject *o) { void *p = arg_ptr(o); return p; }

/* Call a Python handler like TGHandler::HandleEvent (006e0c30): function handlers get (target, event) through
   callPythonHandlerByName (006e0d40, "Module.Function"), method handlers call target.<name>(event) (006f8cf0). Errors are printed and cleared. */
static PyObject *target_pyobj(void *p);
static void call_handler(struct handler *h, TGEvent *e)
{
    PyObject *t = NULL, *evo = NULL, *fn = NULL, *args = NULL, *r = NULL;
    if (h->native) { h->native(e); return; }
    t = target_pyobj(h->target); evo = appc_pyobj(e, e->o.cls->name);
    if (!t || !evo) goto done;
    if (!h->is_method) {
        char *dot = strrchr(h->name, '.'), mod[256]; PyObject *m;
        const char *fname = h->name;
        if (dot) { size_t n = (size_t)(dot - h->name); if (n > 255) n = 255; memcpy(mod, h->name, n); mod[n] = 0; fname = dot + 1; } else strcpy(mod, "__main__");
        m = PyImport_ImportModule(mod); if (!m) goto done;
        fn = PyObject_GetAttrString(m, (char *)fname); Py_DECREF(m); if (!fn) goto done;
        args = Py_BuildValue("(OO)", t, evo); if (!args) goto done;
        r = PyEval_CallObject(fn, args);
    } else {
        fn = PyObject_GetAttrString(t, h->name); if (!fn) goto done;
        args = Py_BuildValue("(O)", evo); if (!args) goto done;
        r = PyEval_CallObject(fn, args);
    }
done:
    if (PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); }
    Py_XDECREF(r); Py_XDECREF(args); Py_XDECREF(fn); Py_XDECREF(t); Py_XDECREF(evo);
}
static struct handler *next_inst(struct handler *from, void *owner, int type)
{ for (; from; from = from->next) if (!from->deleted && from->owner == owner && from->type == type) return from; return NULL; }

/* TGEventHandlerObject::HandleEvent 006d9240 (vslot 0x50, ProcessEvent): run the first handler of the event type on this object */
static long handle_event(void *obj, TGEvent *e)
{
    struct handler *saved = e->cur, *h = next_inst(inst_head, obj, (int)e->type);
    if (h) { e->cur = h; call_handler(h, e); }
    e->cur = saved; return 1;
}
long tg_event_process(void *obj, TGEvent *e) { return handle_event(obj, e); }
int tg_has_instance_handler(void *obj, int type) { return next_inst(inst_head, obj, type) != NULL; }                    /* runs the instance handlers of obj for e */
long eng_TGEventHandlerObject_ProcessEvent(void *self, PyObject *evo) { TGEvent *e = ev(arg_ptr(evo)); return e ? handle_event(self, e) : 0; }
/* CallNextHandler: only meaningful while an instance handler runs (broadcast handlers run with an empty chain) */
long eng_TGEventHandlerObject_CallNextHandler(void *self, PyObject *evo)
{
    TGEvent *e = ev(arg_ptr(evo)); struct handler *cur, *n;
    if (!e || !(cur = e->cur) || cur->owner == NULL) return 1;
    n = next_inst(cur->next, cur->owner, (int)e->type);
    if (n) { e->cur = n; call_handler(n, e); e->cur = cur; }
    return 1;
}

/* ---- broadcast handlers ---- */
static long bcast_add(int type, void *target, char *name, void *filter, int method)
{
    struct handler *h = hnew(type, target, name, method);
    h->filter = filter; h->owner = NULL;
    if (bcast_tail) bcast_tail->next = h; else bcast_head = h;
    bcast_tail = h; return 1;
}
long eng_TGEventManager_AddBroadcastPythonFuncHandler(void *m, int type, void *target, char *name, void *filter) { return bcast_add(type, target, name, filter, 0); }
long eng_TGEventManager_AddBroadcastPythonMethodHandler(void *m, int type, void *target, char *name, void *filter) { return bcast_add(type, target, name, filter, 1); }
long eng_TGEventManager_RemoveBroadcastHandler(void *m, int type, void *target, char *name, void *filter)
{
    struct handler *h;
    for (h = bcast_head; h; h = h->next)
        if (!h->deleted && h->type == type && h->target == target && !strcmp(h->name, name ? name : "")) { h->deleted = 1; return 1; }
    return 0;
}
long eng_TGEventManager_RemoveAllBroadcastHandlersForObject(void *m, void *obj)
{ struct handler *h; for (h = bcast_head; h; h = h->next) if (h->target == obj) h->deleted = 1; return 1; }

/* ---- event queue and dispatch (TGEventManager::AddEvent 006da2a0, FUN_006da2c0, FUN_006da300, FUN_006e21d0) ---- */
static int mgr_up;
static TGEvent *q_head, *q_tail; static int q_count;
long eng_TGEventManager_AddEvent(void *m, PyObject *evo)
{
    TGEvent *e = ev(arg_ptr(evo));
    if (!mgr_up || !e) return 0;
    e->refcount++; e->next = NULL;
    if (q_tail) q_tail->next = e; else q_head = e;
    q_tail = e; q_count++; return 1;
}
int tg_add_event(TGEvent *e) { if (!mgr_up) return 0; e->refcount++; e->next = NULL; if (q_tail) q_tail->next = e; else q_head = e; q_tail = e; q_count++; return 1; }

static void broadcast(TGEvent *e)
{
    struct handler *h, *last = bcast_tail; void *dest = e->dest;                    /* handlers added by a handler wait for the next event */
    int any_dest = dest != NULL && dest != ANY_TARGET;
    /* pass 1: handlers whose filter is ANY, or NULL when the event has no destination */
    for (h = bcast_head; h; h = h->next) {
        if (!h->deleted && h->type == (int)e->type && (h->filter == ANY_TARGET || (h->filter == NULL && !any_dest))) { e->cur = NULL; call_handler(h, e); }
        if (h == last) break;
    }
    /* pass 2: handlers bound to the destination object */
    if (any_dest)
        for (h = bcast_head; h; h = h->next) {
            if (!h->deleted && h->type == (int)e->type && h->filter == dest) { e->cur = NULL; call_handler(h, e); }
            if (h == last) break;
        }
    e->cur = NULL;
}
static void dispatch(TGEvent *e)
{
    if (e->dest && e->dest != (TGObject *)ANY_TARGET) handle_event(e->dest, e);           /* destination pass */
    if (!(e->flags & EVF_PRIVATE)) broadcast(e);                                          /* broadcast pass */
}
/* EventManager::ProcessEvent now (FUN_006da300): the handlers run at once, as Game::SetPlayer needs */
void tg_event_dispatch_now(TGEvent *e) { e->refcount++; dispatch(e); if (e->refcount > 0) e->refcount--; }
static void drain(void)
{
    while (q_count) {
        TGEvent *e = q_head;
        e->refcount++;
        q_head = e->next; if (!q_head) q_tail = NULL; q_count--; e->next = NULL; if (e->refcount > 0) e->refcount--;
        dispatch(e);
        if (e->refcount > 0) e->refcount--;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* Timers (TGTimer 006dcf50, TGTimerManager 006dc240). A timer is a sorted-list node: +8 next fire time, +0xc delay, +0x10 duration, +0x14 start. */
static TGTimer *tm(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x103) ? p : NULL; }

void *eng_new_TGTimer(long id) { return tg_new("TGTimer", sizeof(TGTimer), id); }
void *eng_TGTimer_Create(void) { return tg_new("TGTimer", sizeof(TGTimer), 0); }
void eng_delete_TGTimer(void *p) { TGTimer *t = tm(p); if (t) { if (t->event && t->event->refcount) t->event->refcount--; t->event = NULL; t->o.dead = 1; } }
void eng_TGTimer_SetTimerStart(void *p, float v) { TGTimer *t = tm(p); if (t) { t->next = v; t->start = v; } }       /* stores into +8 and +0x14 */
void eng_TGTimer_SetDelay(void *p, float v) { TGTimer *t = tm(p); if (t) t->delay = v; }
void eng_TGTimer_SetDuration(void *p, float v) { TGTimer *t = tm(p); if (t) t->duration = v; }
double eng_TGTimer_GetTimerStart(void *p) { return tm(p) ? tm(p)->start : 0; }
double eng_TGTimer_GetDelay(void *p) { return tm(p) ? tm(p)->delay : 0; }
double eng_TGTimer_GetDuration(void *p) { return tm(p) ? tm(p)->duration : 0; }
void *eng_TGTimer_GetEvent(void *p) { return tm(p) ? tm(p)->event : NULL; }
void *eng_TGTimer_GetTGTimerPtr(long id) { TGObject *o = tg_find((unsigned)id); return o && tg_is_type(o, 0x103) ? o : NULL; }
void eng_TGTimer_SetEvent(void *p, PyObject *evo)      /* TGTimer::SetEvent 006dd270: refcount the new event, release the old one */
{
    TGTimer *t = tm(p); TGEvent *e = ev(arg_ptr(evo));
    if (!t || t->event == e) return;
    if (t->event && t->event->refcount) t->event->refcount--;
    if (e) e->refcount++;
    t->event = e;
}
static void tl_insert(TimerList *l, TGTimer *t)         /* insertTimerSorted 006df000: before the first timer that is not earlier */
{
    TGTimer *p = l->head, *prev = NULL;
    while (p && t->next > p->next) { prev = p; p = p->link; }
    t->link = p;
    if (prev) prev->link = t; else l->head = t;
    if (!p) l->tail = t;
    l->count++;
}
static int tl_remove(TimerList *l, unsigned id)         /* removeTimerFromList 006df0e0 */
{
    TGTimer *p = l->head, *prev = NULL;
    while (p && p->o.id != id) { prev = p; p = p->link; }
    if (!p) return 0;
    if (prev) prev->link = p->link; else l->head = p->link;
    if (l->tail == p) l->tail = prev;
    p->link = NULL; l->count--; return 1;
}
long eng_TGTimerManager_AddTimer(void *m, void *tp) { TimerMgr *g = m; TGTimer *t = tm(tp); if (!g || !g->list || !t) return 0; tl_insert(g->list, t); return 1; }
long eng_TGTimerManager_RemoveTimer(void *m, long id) { TimerMgr *g = m; if (!g || !g->list) return 0; return tl_remove(g->list, (unsigned)id); }
long eng_TGTimerManager_DeleteTimer(void *m, long id)   /* FUN_006df170: unlink, then delete the object */
{
    TimerMgr *g = m; TGTimer *t;
    if (!g || !g->list || !tl_remove(g->list, (unsigned)id)) return 0;
    t = eng_TGTimer_GetTGTimerPtr(id); if (!t) return 0;
    eng_delete_TGTimer(t); return 1;
}
/* TGTimer::Update 006dd2c0: true when the timer goes on (it is re-inserted), false when it is finished */
static int timer_update(TGTimer *t, float now)
{
    if (t->duration != -1.0f) {
        if (now <= t->next) return 0;
        if (!(t->start + t->duration > now)) return 0;
    }
    t->next = t->delay + t->next; return 1;
}
/* TGTimerManager::Update 006dc490 */
static int timers_update(TimerMgr *g, float now)
{
    int ok = 1; TGTimer *t;
    if (!g || !g->list) return 0;
    if (now < g->current) g->current = now;
    while ((t = g->list->head) != NULL && t->next <= now) {
        TGEvent *e;
        g->list->head = t->link; if (--g->list->count == 0) g->list->tail = NULL;     /* FUN_006df0a0 */
        t->link = NULL;
        if (!t->event) { t->o.dead = 1; continue; }
        e = t->duration == 0.0f ? t->event : ev_clone(t->event);   /* one-shot timers send their own event, repeating ones a copy */
        e->timestamp = t->next;
        if (!tg_add_event(e)) ok = 0;
        if (!timer_update(t, now)) { eng_delete_TGTimer(t); }
        else tl_insert(g->list, t);
    }
    g->current = now;
    return ok;
}

/* ------------------------------------------------------------------------------------------------ */
/* TGPythonInstanceWrapper: an event handler object that scripts can create; SetPyWrapper (006dfcb0) stores the Python object it stands for (+0x14) */
typedef struct { TGObject o; PyObject *wrapped; } PyWrap;
void *eng_new_TGPythonInstanceWrapper(void) { return tg_new("TGPythonInstanceWrapper", sizeof(PyWrap), 0); }
void *eng_TGPythonInstanceWrapper_Create(void) { return tg_new("TGPythonInstanceWrapper", sizeof(PyWrap), 0); }
void eng_TGPythonInstanceWrapper_SetPyWrapper(void *p, PyObject *o) { if (tg_is_ours(p) && tg_is_type(p, 0x10b)) { PyWrap *w = p; Py_XINCREF(o); Py_XDECREF(w->wrapped); w->wrapped = o; } }
static PyObject *target_pyobj(void *p) { if (tg_is_ours(p) && tg_is_type(p, 0x10b) && ((PyWrap *)p)->wrapped) { Py_INCREF(((PyWrap *)p)->wrapped); return ((PyWrap *)p)->wrapped; } return tg_obj_pyobj(p); }

/* Shadow object of an engine object by its own class (the handler target of the exe is the object itself, so every
   method of its class works); falls back to the base class when App.py has no shadow class of that name. */
PyObject *tg_obj_pyobj(void *p)
{
    const char *n = NULL, *b = NULL; PyObject *app, *c; char name[96];
    if (!tg_is_ours(p)) return appc_pyobj(p, NULL);
    n = ((TGObject *)p)->cls->name; b = ((TGObject *)p)->cls->base;
    app = PyImport_ImportModule("App");
    if (app) { snprintf(name, sizeof name, "%sPtr", n); c = PyObject_GetAttrString(app, name);
        if (!c) { PyErr_Clear(); n = b ? b : NULL; } else Py_DECREF(c); Py_DECREF(app); }
    else PyErr_Clear();
    return appc_pyobj(p, n);
}
void eng_delete_TGPythonInstanceWrapper(void *p) { if (tg_is_ours(p)) ((TGObject *)p)->dead = 1; }

/* ------------------------------------------------------------------------------------------------ */
/* Host functions (the engine's main loop calls these): engine_init, engine_tick(game_time, real_time), engine_run_events */
extern void *eng_global_get(const struct appc_global *g);
static struct appc_global *find_global(const char *name) { const struct appc_global *g; for (g = appc_globals; g->name; g++) if (!strcmp(g->name, name)) return (struct appc_global *)g; return NULL; }
static TimerMgr *timer_mgr(const char *name) { struct appc_global *g = find_global(name); return g ? eng_global_get(g) : NULL; }
static PyObject *host_init(PyObject *self, PyObject *args)
{
    const char *n[2] = {"g_kTimerManager", "g_kRealtimeTimerManager"}; int i;
    for (i = 0; i < 2; i++) { TimerMgr *g = timer_mgr(n[i]); if (g && !g->list) g->list = calloc(1, sizeof(TimerList)); }
    mgr_up = 1; tg_actions_init(); tg_mission_init(); { extern void tg_sound_init(void); tg_sound_init(); } { extern void tg_ui_init(void); tg_ui_init(); } { extern void tg_ship_init(void); tg_ship_init(); } return PyInt_FromLong(1);
}
static PyObject *host_tick(PyObject *self, PyObject *args)
{
    double gt, rt;
    if (!PyArg_ParseTuple(args, "dd:engine_tick", &gt, &rt)) return NULL;
    { extern void tg_ui_input_tick(double); static double last_rt; double dt = rt - last_rt; last_rt = rt; if (dt > 0 && dt < 0.25) tg_ui_input_tick(dt); }
    tg_game_time = gt; tg_real_time = rt;
    timers_update(timer_mgr("g_kTimerManager"), (float)gt);
    timers_update(timer_mgr("g_kRealtimeTimerManager"), (float)rt);
    drain();
    { extern void tg_process_run(void); tg_process_run(); }
    tg_mission_idle();
    drain();
    return PyInt_FromLong(q_count);
}
static PyObject *host_run_events(PyObject *self, PyObject *args) { drain(); Py_INCREF(Py_None); return Py_None; }
void engine_register_host(PyObject *dict)
{
    static PyMethodDef m[] = {{"engine_init", host_init, 1}, {"engine_tick", host_tick, 1}, {"engine_run_events", host_run_events, 1}, {NULL, NULL}};
    PyMethodDef *d; for (d = m; d->ml_name; d++) { PyObject *f = PyCFunction_New(d, NULL); PyDict_SetItemString(dict, (char *)d->ml_name, f); Py_DECREF(f); }
    { extern void tg_mission_register_host(PyObject *); tg_mission_register_host(dict); }
    { extern void tg_ui_st_register_host(PyObject *); tg_ui_st_register_host(dict); }
    { extern void tg_ui_input_register_host(PyObject *); tg_ui_input_register_host(dict); }
    { extern void tg_services_register_host(PyObject *); tg_services_register_host(dict); }
    { extern void platform_register_host(PyObject *); platform_register_host(dict); }
}

/* ---- shared services for the action code ---- */
double tg_game_time, tg_real_time;
void tg_register_native(void *owner, int type, native_handler fn)
{
    struct handler *h = hnew(type, owner, "native", 0);
    h->owner = owner; h->native = fn; h->next = inst_head; inst_head = h;
}
/* native broadcast handler (filter ANY_TARGET: it runs for every event of the type, in the first broadcast pass) */
void tg_register_native_broadcast(int type, native_handler fn)
{ struct handler *h = hnew(type, NULL, "native", 0); h->native = fn; h->filter = ANY_TARGET; if (bcast_tail) bcast_tail->next = h; else bcast_head = h; bcast_tail = h; }
TimerMgr *tg_timer_mgr(int real_time) { return timer_mgr(real_time ? "g_kRealtimeTimerManager" : "g_kTimerManager"); }
int tg_timer_add(TimerMgr *g, TGTimer *t) { if (!g || !g->list || !t) return 0; tl_insert(g->list, t); return 1; }

void *tg_global(const char *name) { struct appc_global *g = find_global(name); return g ? eng_global_get(g) : NULL; }

const char *eng_TGStringEvent_GetCString(void *p) { TGEvent *e = ev(p); return e && e->str ? e->str : ""; }

/* callPythonModuleFunction 006f8ab0: import module, call func() without arguments, convert the result with a Py_BuildValue-like letter:
   'f' float, 'i' int, 'z' string (NULL for None), 'O' object (new reference). Returns 0 on success, -1 on any error (the error is cleared). */
int tg_py_call_module(const char *module, const char *func, char rfmt, void *out)
{
    PyObject *m, *f, *r; int ok = -1;
    if (!module || !func) return -1;
    m = PyImport_ImportModule((char *)module); if (!m) { PyErr_Clear(); return -1; }
    f = PyObject_GetAttrString(m, (char *)func); Py_DECREF(m); if (!f) { PyErr_Clear(); return -1; }
    r = PyObject_CallObject(f, NULL); Py_DECREF(f); if (!r) { PyErr_Print(); PyErr_Clear(); return -1; }
    switch (rfmt) {
    case 'f': if (PyFloat_Check(r)) { *(float *)out = (float)PyFloat_AsDouble(r); ok = 0; } else if (PyInt_Check(r)) { *(float *)out = (float)PyInt_AsLong(r); ok = 0; } break;
    case 'i': if (PyInt_Check(r)) { *(int *)out = (int)PyInt_AsLong(r); ok = 0; } break;
    case 'z': if (PyString_Check(r)) { *(char **)out = strdup(PyString_AsString(r)); ok = 0; } else if (r == Py_None) { *(char **)out = NULL; ok = 0; } break;
    case 'O': Py_INCREF(r); *(PyObject **)out = r; ok = 0; break;
    }
    Py_DECREF(r); if (PyErr_Occurred()) PyErr_Clear(); return ok;
}
/* IsNull 006f9730: 1 when the argument is None, a NULL pointer or not a pointer at all; otherwise None (false) */
PyObject *eng_IsNull(PyObject *o)
{
    void *p = NULL;
    if (!o || o == Py_None || !appc_to_ptr(o, &p, "void") || !p) { PyErr_Clear(); return PyInt_FromLong(1); }
    Py_INCREF(Py_None); return Py_None;
}
/* the value of an Appc integer constant (event types etc.), cached; 0 when unknown */
long tg_const(const char *name)
{
    static PyObject *d; PyObject *v;
    if (!d) { PyObject *m = PyImport_ImportModule("Appc"); if (!m) { PyErr_Clear(); return 0; } d = PyModule_GetDict(m); }
    v = d ? PyDict_GetItemString(d, (char *)name) : NULL;
    return v && PyInt_Check(v) ? PyInt_AsLong(v) : 0;
}
