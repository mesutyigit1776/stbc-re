/* TGModelPropertySet (hardpoint name -> property instances), TGModelPropertyList (iteration), TGModelPropertyInstance,
   and the template registry of TGModelPropertyManager (local and global templates: LOCAL_TEMPLATES = 1, GLOBAL_TEMPLATES = 0).
   Approximations (marked): an instance refers to its template (no copy-on-write; UseOriginalTemplate does nothing),
   the filters (RegisterFilter, AddFilter, ApplyFilters) only keep their state. */
#include <stdlib.h>
#include "props.h"

extern const char *eng_TGString_GetCString(void *);
typedef struct { TGObject o; char *hp; void *prop, *orig; } Inst;
typedef struct { TGObject o; Inst **v; int n, cap; } PSet;
typedef struct { TGObject o; Inst **v; int n, it; } PList;
static int is_cls(void *p, const char *c) { return p && tg_is_ours(p) && ((TGObject *)p)->cls && !strcmp(((TGObject *)p)->cls->name, c); }
static PSet *as_set(void *p) { return is_cls(p, "TGModelPropertySet") ? p : NULL; }
static PList *as_list(void *p) { return is_cls(p, "TGModelPropertyList") ? p : NULL; }
static Inst *as_inst(void *p) { return is_cls(p, "TGModelPropertyInstance") ? p : NULL; }

void *eng_TGModelPropertySet_Create(void) { return tg_new("TGModelPropertySet", sizeof(PSet), 0); }
void *eng_new_TGModelPropertySet(void) { return eng_TGModelPropertySet_Create(); }
void eng_delete_TGModelPropertySet(void *p) { }
void *eng_new_TGModelPropertyInstance(void *prop, char *hp)
{ Inst *i = tg_new("TGModelPropertyInstance", sizeof(Inst), 0); i->prop = i->orig = prop; i->hp = strdup(hp ? hp : ""); return i; }
void eng_delete_TGModelPropertyInstance(void *p) { }
long eng_TGModelPropertySet_AddToSet(void *p, char *hp, void *prop)
{
    PSet *s = as_set(p); if (!s || !prop) return 0;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->v = realloc(s->v, s->cap * sizeof *s->v); }
    s->v[s->n++] = eng_new_TGModelPropertyInstance(prop, hp); return 1;
}
static PList *list_new(void) { return tg_new("TGModelPropertyList", sizeof(PList), 0); }
static void list_add(PList *l, Inst *i) { l->v = realloc(l->v, (l->n + 1) * sizeof *l->v); l->v[l->n++] = i; }
void *eng_TGModelPropertySet_GetPropertyList(void *p, char *hp)
{ PSet *s = as_set(p); PList *l = list_new(); int i; if (s) for (i = 0; i < s->n; i++) if (!hp || !strcmp(s->v[i]->hp, hp)) list_add(l, s->v[i]); return l; }
void *eng_TGModelPropertySet_GetPropertiesByType(void *p, int ct)
{ PSet *s = as_set(p); PList *l = list_new(); int i; if (s) for (i = 0; i < s->n; i++) if (tg_is_type(s->v[i]->prop, (unsigned)ct)) list_add(l, s->v[i]); return l; }
void *eng_new_TGModelPropertyList(void) { return list_new(); }
void eng_delete_TGModelPropertyList(void *p) { }
void eng_TGModelPropertyList_TGBeginIteration(void *p) { PList *l = as_list(p); if (l) l->it = 0; }
void *eng_TGModelPropertyList_TGGetNext(void *p) { PList *l = as_list(p); return l && l->it < l->n ? l->v[l->it++] : NULL; }
long eng_TGModelPropertyList_TGDoneIterating(void *p) { PList *l = as_list(p); return !l || l->it >= l->n; }
void eng_TGModelPropertyList_TGDestroy(void *p) { PList *l = as_list(p); if (l) { free(l->v); l->v = NULL; l->n = l->it = 0; } }
long eng_TGModelPropertyList_TGGetNumItems(void *p) { PList *l = as_list(p); return l ? l->n : 0; }
void eng_TGModelPropertyInstance_UseOriginalTemplate(void *p) { }
const char *eng_TGModelPropertyInstance_GetHardpointName(void *p) { Inst *i = as_inst(p); return i ? i->hp : NULL; }
void *eng_TGModelPropertyInstance_GetProperty(void *p) { Inst *i = as_inst(p); return i ? i->prop : NULL; }
void *eng_TGModelPropertyInstance_GetOriginalTemplate(void *p) { Inst *i = as_inst(p); return i ? i->orig : NULL; }

/* ---- template registry ---- */
static struct { void **v; int n; } tpl[2];                                     /* [0] global, [1] local */
static long tpl_has(int k, void *p) { int i; for (i = 0; i < tpl[k].n; i++) if (tpl[k].v[i] == p) return 1; return 0; }
static long tpl_add(int k, void *p) { if (!p || tpl_has(k, p)) return 0; tpl[k].v = realloc(tpl[k].v, (tpl[k].n + 1) * sizeof(void *)); tpl[k].v[tpl[k].n++] = p; return 1; }
long eng_TGModelPropertyManager_IsLocalTemplate(void *m, void *p) { return tpl_has(1, p); }
long eng_TGModelPropertyManager_IsGlobalTemplate(void *m, void *p) { return tpl_has(0, p); }
long eng_TGModelPropertyManager_RegisterLocalTemplate(void *m, void *p) { return tpl_add(1, p); }
long eng_TGModelPropertyManager_RegisterGlobalTemplate(void *m, void *p) { return tpl_add(0, p); }
void eng_TGModelPropertyManager_ClearGlobalTemplates(void *m) { tpl[0].n = 0; }
void eng_TGModelPropertyManager_ClearLocalTemplates(void *m) { tpl[1].n = 0; }
long eng_TGModelPropertyManager_RemoveTemplate(void *m, int a, void *p, int b)
{ int k, i; for (k = 0; k < 2; k++) for (i = 0; i < tpl[k].n; i++) if (tpl[k].v[i] == p) { tpl[k].v[i] = tpl[k].v[--tpl[k].n]; return 1; } return 0; }
static int name_is(void *p, const char *n) { PropObj *q = pobj(p); const char *s = q && q->name ? eng_TGString_GetCString(q->name) : NULL; return s && n && !strcmp(s, n); }
void *eng_TGModelPropertyManager_FindByNameAndType(void *m, char *name, int ct, int which)
{ int k = which ? 1 : 0, i; for (i = 0; i < tpl[k].n; i++) if (name_is(tpl[k].v[i], name) && (!ct || tg_is_type(tpl[k].v[i], (unsigned)ct))) return tpl[k].v[i]; return NULL; }
void *eng_TGModelPropertyManager_FindByName(void *m, char *name, int which) { return eng_TGModelPropertyManager_FindByNameAndType(m, name, 0, which); }
void eng_TGModelPropertyManager_ClearRegisteredFilters(void *m) { }
void eng_TGModelPropertyManager_RegisterFilter(void *m, PyObject *f, char *n) { }
void eng_TGModelPropertyManager_AddFilter(void *m, PyObject *f) { }
long eng_TGModelPropertyManager_ApplyFilters(void *m, void *s) { return 1; }
void eng_TGModelPropertyManager_ClearCurrentFilters(void *m) { }
long eng_TGModelPropertyManager_RemoveFilter(void *m, PyObject *f) { return 0; }
/* for SetupProperties (tg_ship.c): the properties of a set in insertion order */
int tg_propset_count(void *p) { PSet *s = as_set(p); return s ? s->n : 0; }
void *tg_propset_prop(void *p, int i) { PSet *s = as_set(p); return s && i >= 0 && i < s->n ? s->v[i]->prop : NULL; }
