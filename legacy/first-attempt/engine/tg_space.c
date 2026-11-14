/* Space layer: SetManager, SetClass, BridgeSet, and the object classes that live in a set
   (BaseObjectClass, ObjectClass, LightObjectClass, CameraObjectClass, SpaceCamera, ViewScreenObject, PlacementObject).
   Behavior from stbc.exe: SetManager 00417e30..00418530, SetClass 0040b220..00416230, BaseObjectClass 00430830..004319d0,
   LightObjectClass 00432a50..00433840, CameraObjectClass 00429a60..0042c1f0, BridgeSet 00665cc0..00666090.
   Not byte-compatible with the original structures. There is no scene graph yet: an object keeps a translation, a rotation matrix and a scale.
   Approximations (marked): name tables are case-sensitive; Rotate; MakeRenderedSet (bridge special case); the camera hand-over in AddSet. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

typedef struct { float m[9]; } Mat3;
typedef struct Set Set;
typedef struct Obj {
    TGObject o;
    char *name; void *display_name;               /* display name: a TGString */
    Set *set; int hidden, delete_me;
    float pos[3]; Mat3 rot; float scale;
    int hailable, scannable, collision_flags, is_static, nav_point;
    struct Obj *attached_to;                      /* AttachObject parent (used for proximity spheres) */
    char *model; void *propset; void *ext;        /* ext: subclass data (CharacterClass: tg_character.c) */                   /* TGModelPropertySet (hardpoints), created on first use */
    /* light */
    float color[4], intensity; int lit_all;
    /* camera */
    float frustum[7]; int space_dust;
    /* viewscreen */
    struct Obj *remote_cam; int vs_on, vs_static_on, vs_menu_up; void *vs_menu;
    /* camera modes */
    struct CamMode **mode_stack; int nstack; struct CamMode *current; void *current_swept;   /* current_swept: the mode tg_camera_tick last placed the camera with */
    struct { char *name; struct CamMode *m; } *modes; int nmodes;
    struct { char *child, *parent; } *hier; int nhier;
} Obj;

struct Set {
    TGObject o;
    char *name;
    Obj **objs; int nobj, cap_o;                  /* sorted by object id (SetClass::AddObjectToSet 0040ec90) */
    Obj **cams; int ncam, cap_c;
    Obj **lights; int nlight, cap_l;
    Obj *active_cam;
    int rendered; char *region;
    /* BridgeSet */
    Obj *viewscreen; char *config;
};

#define ET_TORPEDO_ENTERED_SET 8388700
#define ET_ENTERED_SET 8388701
#define ET_TORPEDO_EXITED_SET 8388702
#define ET_EXITED_SET 8388703
#define ET_HAILABLE_CHANGE 8388872

static char *dup(const char *s) { return s ? strdup(s) : NULL; }
static void *arg_ptr(PyObject *o) { void *p = NULL; if (!o || o == Py_None) return NULL; if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }
static Set *as_set(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80d1) ? p : NULL; }
static Obj *as_obj(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x8002) ? p : NULL; }

/* ---- sorted pointer arrays (by id) ---- */
static void arr_insert(Obj ***a, int *n, int *cap, Obj *o)
{
    int i;
    if (*n == *cap) { *cap = *cap ? *cap * 2 : 2; *a = realloc(*a, *cap * sizeof **a); }
    for (i = 0; i < *n && (*a)[i]->o.id < o->o.id; i++) ;
    memmove(*a + i + 1, *a + i, (*n - i) * sizeof **a); (*a)[i] = o; (*n)++;
}
static int arr_remove(Obj ***a, int *n, Obj *o)
{
    int i;
    for (i = 0; i < *n; i++) if ((*a)[i] == o) { memmove(*a + i, *a + i + 1, (*n - i - 1) * sizeof **a); (*n)--; return 1; }
    return 0;
}

/* ---- events ---- */
static TGEvent *new_event(const char *cls, unsigned type, void *dest, void *source)
{
    TGEvent *e = tg_ev_new(cls, 0);
    e->type = type; e->dest = dest; e->source = source; return e;
}

/* ================================================= base objects ================================================= */
static void obj_init(Obj *o, const char *name)
{
    o->name = dup(name); o->scale = 1.0f; o->rot.m[0] = o->rot.m[4] = o->rot.m[8] = 1.0f;      /* NiNode default: identity, scale 1 */
    o->hailable = 0; o->scannable = 0;
    o->intensity = 1.0f;
    if (tg_is_type(&o->o, 0x8003) && !tg_is_type(&o->o, 0x80de) && !tg_is_type(&o->o, 0x80e2) && !tg_is_type(&o->o, 0x800b))
        o->collision_flags = 0x7f;                    /* ObjectClass InitMembers 00435090; placements, grids and backdrops set 0 (004a1280, 00498e20, 0058b380) */
}
static Obj *obj_new(const char *cls, const char *name) { Obj *o = tg_new(cls, sizeof(Obj), 0); obj_init(o, name); return o; }

/* the name registry of a set: objects, cameras and lights share one namespace (SetClass hash at +0x80) */
static Obj *set_find_any(Set *s, const char *name)
{
    int i; if (!name) return NULL;
    for (i = 0; i < s->nobj; i++) if (s->objs[i]->name && !strcmp(s->objs[i]->name, name)) return s->objs[i];
    for (i = 0; i < s->ncam; i++) if (s->cams[i]->name && !strcmp(s->cams[i]->name, name)) return s->cams[i];
    for (i = 0; i < s->nlight; i++) if (s->lights[i]->name && !strcmp(s->lights[i]->name, name)) return s->lights[i];
    return NULL;
}
static void obj_set_name(Obj *o, const char *name) { free(o->name); o->name = dup(name); }

void *eng_BaseObjectClass_Create(void *x) { return obj_new("BaseObjectClass", NULL); }
void *eng_BaseObjectClass_Cast(PyObject *o) { return as_obj(arg_ptr(o)); }
void eng_BaseObjectClass_Update(void *o, float dt) { }
void eng_BaseObjectClass_UpdateNodeOnly(void *o) { }
void eng_BaseObjectClass_AttachObject(void *o, void *c) { Obj *p = as_obj(o), *k = as_obj(c); if (k && k != p) k->attached_to = p; }   /* no scene graph yet: the child follows the parent's position */
void eng_BaseObjectClass_DetachObject(void *o, void *c) { Obj *k = as_obj(c); if (k && k->attached_to == as_obj(o)) k->attached_to = NULL; }
int tg_space_world_pos(void *o, float out[3])
{
    Obj *b = as_obj(o); int i, depth = 0;
    if (!b) return 0;
    for (i = 0; i < 3; i++) out[i] = b->pos[i];
    while (b->attached_to && depth++ < 8) { b = b->attached_to; for (i = 0; i < 3; i++) out[i] += b->pos[i]; }
    return 1;
}
void *eng_BaseObjectClass_GetContainingSet(void *o) { Obj *b = as_obj(o); return b ? b->set : NULL; }
void *eng_BaseObjectClass_GetNiObject(void *o) { return NULL; }
void *eng_BaseObjectClass_GetAnimNode(void *o) { return as_obj(o); }   /* the object stands in for its NiNode; TGAnimNode state lives in the property store (tg_anim.c) */
void eng_BaseObjectClass_SetDeleteMe(void *o, int v) { Obj *b = as_obj(o); if (b) b->delete_me = v != 0; }
void eng_BaseObjectClass_SetHidden(void *o, int v) { Obj *b = as_obj(o); if (b) b->hidden = v != 0; }
long eng_BaseObjectClass_IsHidden(void *o) { Obj *b = as_obj(o); return b ? b->hidden : 0; }
const char *eng_BaseObjectClass_GetName(void *o) { Obj *b = as_obj(o); return b ? b->name : NULL; }
void eng_BaseObjectClass_SetDisplayName(void *o, void *s) { Obj *b = as_obj(o); if (b) b->display_name = s ? tg_str_copy(s) : NULL; }   /* the caller keeps its own string */
void *eng_BaseObjectClass_GetDisplayName(void *o)
{
    extern void *eng_new_TGString(char *); Obj *b = as_obj(o);
    if (!b) return NULL;
    if (!b->display_name) b->display_name = eng_new_TGString(b->name ? b->name : "<unspecified display name>");
    return tg_str_copy(b->display_name);                         /* App.py takes ownership of the result */
}
void eng_BaseObjectClass_SetTranslateXYZ(void *o, float x, float y, float z) { Obj *b = as_obj(o); if (b) { b->pos[0] = x; b->pos[1] = y; b->pos[2] = z; } }
void eng_BaseObjectClass_SetTranslate(void *o, void *p) { Obj *b = as_obj(o); if (b && p) memcpy(b->pos, p, 12); }
void *eng_BaseObjectClass_GetTranslate(void *o) { Obj *b = as_obj(o); return b ? tg_mem_dup(b->pos, 12) : NULL; }   /* fresh copies: App.py takes ownership */
void *eng_BaseObjectClass_GetWorldLocation(void *o) { float w[3]; return tg_space_world_pos(o, w) ? tg_mem_dup(w, 12) : NULL; }   /* parent positions added; no parent rotation yet */
void eng_BaseObjectClass_SetMatrixRotation(void *o, void *m) { Obj *b = as_obj(o); if (b && m) memcpy(&b->rot, m, 36); }
void *eng_BaseObjectClass_GetRotation(void *o) { Obj *b = as_obj(o); return b ? tg_mem_dup(&b->rot, 36) : NULL; }
void *eng_BaseObjectClass_GetWorldRotation(void *o) { Obj *b = as_obj(o); return b ? tg_mem_dup(&b->rot, 36) : NULL; }
void eng_BaseObjectClass_SetScale(void *o, float s) { Obj *b = as_obj(o); if (b) b->scale = s > 0.0f ? s : -s; }    /* 00430eb0 stores |s| */
double eng_BaseObjectClass_GetScale(void *o) { Obj *b = as_obj(o); return b ? b->scale : 1.0; }
extern void eng_TGMatrix3_MakeRotation(void *, float, void *);
void eng_BaseObjectClass_SetAngleAxisRotation(void *o, float a, float x, float y, float z)
{
    Obj *b = as_obj(o); float ax[3]; ax[0] = x; ax[1] = y; ax[2] = z;
    if (b) eng_TGMatrix3_MakeRotation(&b->rot, a, ax);
}
extern void *eng_TGMatrix3_MultMatrix(void *, void *);
extern long eng_TGMatrix3_Reorthogonalize(void *);
void eng_BaseObjectClass_Rotate(void *o, void *m)           /* 00430fb0: the exact product order is not decoded; rotation = m * rotation, then reorthogonalized */
{
    Obj *b = as_obj(o); Mat3 *r; if (!b || !m) return;
    r = eng_TGMatrix3_MultMatrix(m, &b->rot); b->rot = *r; free(r); eng_TGMatrix3_Reorthogonalize(&b->rot);
}
static void *axis_of(Obj *b, int which)                       /* column-vector products with the model axes; a fresh TGPoint3 */
{
    static const float ax[6][3] = {{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};
    float *r = malloc(12); const float *m = b->rot.m; const float *a = ax[which];
    r[0] = m[0] * a[0] + m[1] * a[1] + m[2] * a[2]; r[1] = m[3] * a[0] + m[4] * a[1] + m[5] * a[2]; r[2] = m[6] * a[0] + m[7] * a[1] + m[8] * a[2];
    return r;
}
void *eng_BaseObjectClass_GetWorldForwardTG(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 0) : NULL; }
void *eng_BaseObjectClass_GetWorldBackwardTG(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 1) : NULL; }
void *eng_BaseObjectClass_GetWorldUpTG(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 2) : NULL; }
void *eng_BaseObjectClass_GetWorldDownTG(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 3) : NULL; }
void *eng_BaseObjectClass_GetWorldRightTG(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 4) : NULL; }
void *eng_BaseObjectClass_GetWorldLeftTG(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 5) : NULL; }
void eng_BaseObjectClass_AlignToVectors(void *o, void *fwd, void *up)   /* alignVectorsToRotation: columns are (right, forward, up) */
{
    Obj *b = as_obj(o); const float *f = fwd, *u = up; float r[3], l; if (!b || !f || !u) return;
    r[0] = f[1] * u[2] - f[2] * u[1]; r[1] = f[2] * u[0] - f[0] * u[2]; r[2] = f[0] * u[1] - f[1] * u[0];
    l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); if (l > 1e-6f) { r[0] /= l; r[1] /= l; r[2] /= l; }
    /* rows of the matrix are the images of the model axes: m * (1,0,0) = right, m * (0,1,0) = forward, m * (0,0,1) = up */
    b->rot.m[0] = r[0]; b->rot.m[3] = r[1]; b->rot.m[6] = r[2];
    b->rot.m[1] = f[0]; b->rot.m[4] = f[1]; b->rot.m[7] = f[2];
    b->rot.m[2] = u[0]; b->rot.m[5] = u[1]; b->rot.m[8] = u[2];
}
void eng_BaseObjectClass_SetName(void *o, char *n)
{
    Obj *b = as_obj(o); if (!b) return;
    if (b->set && n && b->name && strcmp(b->name, n)) {          /* SetClass::RenameObjectEntry: the set sees the new name at once (names are read from the object here) */
    }
    obj_set_name(b, n); b->display_name = NULL;
}
void *eng_BaseObjectClass_GetObject(void *o, char *n) { return NULL; }
long eng_BaseObjectClass_GetNumClassObjects(void *o) { return 0; }

/* class object lists: all live objects of a class, by name or id (BaseObjectClass 004306a0..004307a0) */
static Obj *find_class_obj(const char *cn_ct, const char *name, long id, unsigned ct);
static struct reg { Obj *o; } *registry; static int nreg, capreg;
static void reg_add(Obj *o) { if (nreg == capreg) { capreg = capreg ? capreg * 2 : 64; registry = realloc(registry, capreg * sizeof *registry); } registry[nreg++].o = o; }
static Obj *find_class_obj(const char *unused, const char *name, long id, unsigned ct)
{
    int i;
    for (i = 0; i < nreg; i++) {
        Obj *o = registry[i].o;
        if (o->o.dead || !tg_is_type(&o->o, ct)) continue;
        if (name ? (o->name && !strcmp(o->name, name)) : o->o.id == (unsigned)id) return o;
    }
    return NULL;
}
static long count_class(unsigned ct) { int i; long n = 0; for (i = 0; i < nreg; i++) if (!registry[i].o->o.dead && tg_is_type(&registry[i].o->o, ct)) n++; return n; }
/* Cls::GetObject(set, name) (e.g. CameraObjectClass 004299d0 -> FUN_0040fe00): with a set, only that set; without one, every set */
static Obj *set_class_obj(void *x, const char *name, long id, unsigned ct)
{
    Set *s = as_set(x); Obj *o = NULL; int i;
    if (!s) {                                       /* ObjectClass::GetObject 00434e70: no set = look in each set, so an object that is in no set is not found */
        int k; for (k = 0; k < nreg; k++) {
            Obj *q = registry[k].o;
            if (q->o.dead || !q->set || !tg_is_type(&q->o, ct)) continue;
            if (name ? (q->name && !strcmp(q->name, name)) : q->o.id == (unsigned)id) return q;
        }
        return NULL;
    }
    if (name) o = set_find_any(s, name);
    else for (i = 0; i < s->nobj && !o; i++) if (s->objs[i]->o.id == (unsigned)id) o = s->objs[i];
    return o && !o->o.dead && tg_is_type(&o->o, ct) ? o : NULL;
}
#define CLASS_LOOKUPS(Cls, ct)                                                                      \
    void *eng_##Cls##_GetObject(void *x, char *n) { return n ? set_class_obj(x, n, 0, ct) : NULL; }  \
    long eng_##Cls##_GetNumClassObjects(void *x) { return count_class(ct); }
#define CLASS_BYID(Cls, ct, T) void *eng_##Cls##_GetObjectByID(void *x, T id) { return set_class_obj(x, NULL, (long)id, ct); }
CLASS_LOOKUPS(ObjectClass, 0x8003)
CLASS_LOOKUPS(CameraObjectClass, 0x80be)
CLASS_LOOKUPS(SpaceCamera, 0x80bf)
CLASS_LOOKUPS(LightObjectClass, 0x80bc)
CLASS_LOOKUPS(ViewScreenObject, 0x801a)
CLASS_LOOKUPS(PlacementObject, 0x80de)
CLASS_LOOKUPS(CharacterClass, 0x8016)
CLASS_LOOKUPS(ShipClass, 0x8008) CLASS_BYID(ShipClass, 0x8008, int) CLASS_LOOKUPS(DamageableObject, 0x8007) CLASS_BYID(DamageableObject, 0x8007, int)
CLASS_LOOKUPS(PhysicsObjectClass, 0x8006) CLASS_LOOKUPS(Torpedo, 0x8009) CLASS_BYID(Torpedo, 0x8009, long) CLASS_LOOKUPS(Planet, 0x8004)
CLASS_LOOKUPS(Nebula, 0x800e) CLASS_BYID(Nebula, 0x800e, long) CLASS_LOOKUPS(AsteroidField, 0x8014) CLASS_LOOKUPS(Backdrop, 0x800b)
CLASS_LOOKUPS(BackdropSphere, 0x800c) CLASS_LOOKUPS(BridgeObjectClass, 0x8019) CLASS_LOOKUPS(GridClass, 0x80e2) CLASS_LOOKUPS(ProximityCheck, 0x8125)
void *eng_AsteroidFieldPlacement_GetObject(void *x, char *n) { return n ? set_class_obj(x, n, 0, 0x80df) : NULL; }
void *eng_ObjectClass_GetObjectByID(void *x, long id) { return find_class_obj(NULL, NULL, id, 0x8003); }
void *eng_PlacementObject_GetObjectBySetName(char *set, char *name)
{
    extern void *eng_SetManager_GetSet(void *, char *); Set *s = as_set(eng_SetManager_GetSet(NULL, set)); Obj *o;
    if (!s) return NULL; o = set_find_any(s, name); return o && tg_is_type(&o->o, 0x80de) ? o : NULL;
}

/* ObjectClass */
void *eng_ObjectClass_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x8003) ? p : NULL; }
void *eng_ObjectClass_Create(PyObject *model) { Obj *o = obj_new("ObjectClass", NULL); reg_add(o); return o; }
double eng_ObjectClass_GetRadius(void *o)                               /* model bounding radius x object scale (1 when no model is known) */
{ extern double tg_model_radius(const char *); Obj *b = as_obj(o); double r; if (!b) return 0.0; r = tg_model_radius(b->model); return (r > 0 ? r : 1.0) * b->scale; }
/* ObjectClass::LineCollides 00435ec0: NiPick along start -> end against the model; the segment goes into model space (M^T (p - pos) / scale) */
long eng_ObjectClass_LineCollides(void *o, void *ap, void *bp)
{
    extern int tg_model_line_hit(const char *, const float *, const float *); Obj *b = as_obj(o); float w[3], l[2][3]; const float *pt[2] = {ap, bp}; int i, k;
    if (!b || !ap || !bp || !b->model || !tg_space_world_pos(o, w)) return 0;
    for (i = 0; i < 2; i++) for (k = 0; k < 3; k++)
        l[i][k] = (b->rot.m[0 * 3 + k] * (pt[i][0] - w[0]) + b->rot.m[1 * 3 + k] * (pt[i][1] - w[1]) + b->rot.m[2 * 3 + k] * (pt[i][2] - w[2])) / (b->scale ? b->scale : 1.0f);
    return tg_model_line_hit(b->model, l[0], l[1]);
}
void tg_space_set_model(void *o, const char *m) { Obj *b = as_obj(o); if (b) { free(b->model); b->model = m ? dup(m) : NULL; } }
void eng_ObjectClass_PlaceObjectByName(void *o, char *name)           /* 00435220: copy the placement's transform */
{
    Obj *b = as_obj(o), *p; if (!b || !b->set) return;
    p = set_find_any(b->set, name);
    if (p && tg_is_type(&p->o, 0x80de)) { memcpy(b->pos, p->pos, 12); b->rot = p->rot; }
}
void eng_ObjectClass_RandomOrientation(void *o) { }
void eng_ObjectClass_AlignToObject(void *o, void *t) { Obj *b = as_obj(o), *c = as_obj(t); if (b && c) b->rot = c->rot; }
long eng_ObjectClass_IsTargetable(void *o) { return 0; }
long eng_ObjectClass_CanTargetObject(void *o, void *t) { return 0; }
void eng_ObjectClass_SetHailable(void *o, int v)                      /* 004364a0 */
{
    Obj *b = as_obj(o); TGEvent *e; if (!b) return;
    b->hailable = (char)v != 0;
    e = new_event("TGBoolEvent", ET_HAILABLE_CHANGE, b, b); e->v.i = b->hailable;
    tg_add_event(e);
}
long eng_ObjectClass_IsHailable(void *o) { Obj *b = as_obj(o); return b ? b->hailable : 0; }
void eng_ObjectClass_SetScannable(void *o, int v) { Obj *b = as_obj(o); if (b) b->scannable = (char)v != 0; }
long eng_ObjectClass_IsScannable(void *o) { Obj *b = as_obj(o); return b ? b->scannable : 0; }
static int max_lights = 8;
void eng_ObjectClass_SetMaximumNumberOfLights(int n) { max_lights = n; }
long eng_ObjectClass_GetMaximumNumberOfLights(void) { return max_lights; }
void eng_ObjectClass_SetCollisionFlags(void *o, int f) { Obj *b = as_obj(o); if (b) b->collision_flags = f; }
long eng_ObjectClass_GetCollisionFlags(void *o) { Obj *b = as_obj(o); return b ? b->collision_flags : 0; }
void eng_ObjectClass_ReplaceTexture(void *o, char *a, char *b, int c) { }
void eng_ObjectClass_RefreshReplacedTextures(void *o) { }

/* PlacementObject */
void *eng_PlacementObject_Create(char *name, char *set, char *model)
{
    extern void *eng_SetManager_GetSet(void *, char *); Obj *o = obj_new("PlacementObject", name); Set *s;
    reg_add(o); o->model = dup(model);
    s = as_set(eng_SetManager_GetSet(NULL, set));
    if (s) { extern long eng_SetClass_AddObjectToSet(void *, void *, char *); eng_SetClass_AddObjectToSet(s, o, name); }
    return o;
}
void *eng_PlacementObject_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x80de) ? p : NULL; }
void eng_PlacementObject_Update(void *o, float dt) { }
void eng_PlacementObject_SetModel(void *o, char *m) { Obj *b = as_obj(o); if (b) { free(b->model); b->model = dup(m); } }
const char *eng_PlacementObject_GetModelName(void *o) { Obj *b = as_obj(o); return b ? b->model : NULL; }
void eng_PlacementObject_SetStatic(void *o, int v) { Obj *b = as_obj(o); if (b) b->is_static = v != 0; }
long eng_PlacementObject_IsStatic(void *o) { Obj *b = as_obj(o); return b ? b->is_static : 0; }
void eng_PlacementObject_SetNavPoint(void *o, int v) { Obj *b = as_obj(o); if (b) b->nav_point = v != 0; }
long eng_PlacementObject_IsNavPoint(void *o) { Obj *b = as_obj(o); return b ? b->nav_point : 0; }
void *eng_PlacementObject_FindContainingSet(void *o) { Obj *b = as_obj(o); return b ? b->set : NULL; }

/* ================================================= lights ================================================= */
void *eng_LightObjectClass_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x80bc) ? p : NULL; }
void eng_LightObjectClass_SetIntensity(void *o, float v) { Obj *b = as_obj(o); if (b) b->intensity = v; }
double eng_LightObjectClass_GetIntensity(void *o) { Obj *b = as_obj(o); return b ? b->intensity : 0.0; }
void eng_LightObjectClass_AddIlluminatedObject(void *o, PyObject *t) { }
void eng_LightObjectClass_RemoveIlluminatedObject(void *o, PyObject *t) { }
void eng_LightObjectClass_IlluminateEntireSet(void *o) { Obj *b = as_obj(o); if (b && b->set) b->lit_all = 1; }
void eng_LightObjectClass_UnilluminateEntireSet(void *o) { Obj *b = as_obj(o); if (b) b->lit_all = 0; }   /* 00433810 clears the light's object list */

/* ================================================= cameras ================================================= */
static Obj *camera_new(const char *cls, const char *name, float x, float y, float z, float angle, float ax, float ay, float az)
{
    Obj *c = obj_new(cls, name); float axis[3]; reg_add(c);
    c->pos[0] = x; c->pos[1] = y; c->pos[2] = z; axis[0] = ax; axis[1] = ay; axis[2] = az;
    eng_TGMatrix3_MakeRotation(&c->rot, angle, axis);
    return c;
}
void *eng_CameraObjectClass_Create(float x, float y, float z, float a, float ax, float ay, float az, char *name) { return camera_new("CameraObjectClass", name, x, y, z, a, ax, ay, az); }
void *eng_CameraObjectClass_CreateFromNiCamera(void *ni, char *name) { return camera_new("CameraObjectClass", name, 0, 0, 0, 0, 0, 0, 1); }
void *eng_CameraObjectClass_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x80be) ? p : NULL; }
void *eng_SpaceCamera_Create(char *name) { return camera_new("SpaceCamera", name, 0, 0, 0, 0, 0, 0, 1); }
void eng_CameraObjectClass_SetFrustumValues(void *o, float a, float b, float c) { Obj *x = as_obj(o); if (x) { x->frustum[0] = a; x->frustum[1] = b; x->frustum[2] = c; } }
void eng_CameraObjectClass_SetFrustum(void *o, float a, float b, float c, float d, float e, float f)
{ Obj *x = as_obj(o); if (x) { x->frustum[0] = a; x->frustum[1] = b; x->frustum[2] = c; x->frustum[3] = d; x->frustum[4] = e; x->frustum[5] = f; } }
void eng_CameraObjectClass_SetNearAndFarDistance(void *o, float n, float f) { Obj *x = as_obj(o); if (x) { x->frustum[4] = n; x->frustum[5] = f; } }
void eng_CameraObjectClass_Zoom(void *o, float f) { }
void eng_CameraObjectClass_LookToward(void *o, void *fwd, void *up, float t) { eng_BaseObjectClass_AlignToVectors(o, fwd, up); }
void *eng_CameraObjectClass_GetWorldForward(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 0) : NULL; }
void *eng_CameraObjectClass_GetWorldUp(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 2) : NULL; }
void *eng_CameraObjectClass_GetWorldRight(void *o) { Obj *b = as_obj(o); return b ? axis_of(b, 4) : NULL; }
static int space_dust_game = 1;
long eng_SpaceCamera_IsSpaceDustEnabledInGame(void) { return space_dust_game; }
void eng_SpaceCamera_SetSpaceDustInGame(int v) { space_dust_game = v != 0; }
long eng_SpaceCamera_IsSpaceDustEnabled(void *o) { Obj *b = as_obj(o); return b ? b->space_dust : 0; }
void eng_SpaceCamera_SetSpaceDustForCamera(void *o, int v) { Obj *b = as_obj(o); if (b) b->space_dust = v != 0; }

/* ================================================= view screen ================================================= */
void *eng_ViewScreenObject_Create(char *name) { Obj *o = obj_new("ViewScreenObject", name); reg_add(o); return o; }
void *eng_ViewScreenObject_CreateNull(void) { return NULL; }
void *eng_ViewScreenObject_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x801a) ? p : NULL; }
void eng_ViewScreenObject_SetModel(void *o, char *m) { Obj *b = as_obj(o); if (b) { free(b->model); b->model = dup(m); } }
void eng_ViewScreenObject_SetRemoteCam(void *o, void *c) { Obj *b = as_obj(o); if (b) b->remote_cam = as_obj(c); }
void *eng_ViewScreenObject_GetRemoteCam(void *o) { Obj *b = as_obj(o); return b ? b->remote_cam : NULL; }
void eng_ViewScreenObject_ToggleRemoteCam(void *o, int v) { }
long eng_ViewScreenObject_RenderingRemoteCam(void *o) { Obj *b = as_obj(o); return b && b->remote_cam && b->vs_on; }
void eng_ViewScreenObject_SetIsOn(void *o, int v) { Obj *b = as_obj(o); if (b) b->vs_on = v != 0; }
long eng_ViewScreenObject_IsOn(void *o) { Obj *b = as_obj(o); return b ? b->vs_on : 0; }
void eng_ViewScreenObject_SetStaticIsOn(void *o, int v) { Obj *b = as_obj(o); if (b) b->vs_static_on = v != 0; }
long eng_ViewScreenObject_IsStaticOn(void *o) { Obj *b = as_obj(o); return b ? b->vs_static_on : 0; }
void eng_ViewScreenObject_SetStaticVariation(void *o, float a, float b) { }
void eng_ViewScreenObject_SetOffTexture(void *o, char *t) { }
void eng_ViewScreenObject_SetStaticTextureIconGroup(void *o, char *t) { }
void eng_ViewScreenObject_SetMenu(void *o, void *m) { Obj *b = as_obj(o); if (b) b->vs_menu = m; }
void eng_ViewScreenObject_ClearMenu(void *o) { Obj *b = as_obj(o); if (b) { b->vs_menu = NULL; b->vs_menu_up = 0; } }
void *eng_ViewScreenObject_GetMenu(void *o) { Obj *b = as_obj(o); return b ? b->vs_menu : NULL; }
void eng_ViewScreenObject_MenuUp(void *o) { Obj *b = as_obj(o); if (b) b->vs_menu_up = 1; }
void eng_ViewScreenObject_MenuDown(void *o) { Obj *b = as_obj(o); if (b) b->vs_menu_up = 0; }
long eng_ViewScreenObject_IsMenuUp(void *o) { Obj *b = as_obj(o); return b ? b->vs_menu_up : 0; }
void eng_ViewScreenObject_LookTowardsSpace(void *o) { }

/* ================================================= SetClass ================================================= */
static Set *set_new(const char *cls) { Set *s = tg_new(cls, sizeof(Set), 0); s->rendered = 0; return s; }
void *eng_SetClass_Create(void) { return set_new("SetClass"); }
void *eng_SetClass_GetNull(void) { return NULL; }
void *eng_SetClass_Cast(PyObject *o) { return as_set(arg_ptr(o)); }
void eng_delete_SetClass(void *s) { if (as_set(s)) ((TGObject *)s)->dead = 1; }
static int glow_enabled, split_large_z;
void eng_SetClass_SetGlowEnabled(int v) { glow_enabled = v != 0; }
long eng_SetClass_IsGlowEnabled(void) { return glow_enabled; }
long eng_SetClass_CanGlowBeEnabled(void) { return 0; }               /* depends on the video hardware (0040cdb0); no renderer yet */
void eng_SetClass_SetGlowIntensity(void *s, float v) { }
void eng_SetClass_SetSplitLargeZRenders(int v) { split_large_z = v != 0; }
long eng_SetClass_GetSplitLargeZRenders(void) { return split_large_z; }
void eng_SetClass_SetProximityManagerActive(void *s, int v) { }

void eng_SetClass_SetRegionModule(void *s, char *m) { Set *x = as_set(s); if (x) { free(x->region); x->region = dup(m); } }
const char *eng_SetClass_GetRegionModule(void *s) { Set *x = as_set(s); return x ? x->region : NULL; }
long eng_SetClass_IsRendered(void *s) { Set *x = as_set(s); return x ? x->rendered : 0; }
void eng_SetClass_CalcIsRendered(void *s) { }
const char *eng_SetClass_GetName(void *s) { Set *x = as_set(s); return x ? x->name : NULL; }
void eng_SetClass_SetName(void *s, char *n) { Set *x = as_set(s); if (x) { free(x->name); x->name = dup(n); } }
void *eng_SetClass_MakeDisplayName(char *n) { extern void *eng_new_TGString(char *); return eng_new_TGString(n ? n : ""); }
void eng_SetClass_GetDisplayName(void *s, void *out) { }
void *eng_SetClass_GetSceneRoot(void *s) { return NULL; }
void *eng_SetClass_GetBackdropRoot(void *s) { return NULL; }
void *eng_SetClass_GetForegroundRoot(void *s) { return NULL; }
void *eng_SetClass_GetNebula(void *s) { return NULL; }

long eng_SetClass_AddObjectToSet(void *sp, void *op, char *name)       /* 0040ec90: false when the name is taken */
{
    Set *s = as_set(sp); Obj *o = as_obj(op); TGEvent *e; int torpedo;
    if (!s || !o) return 0;
    if (name && set_find_any(s, name)) return 0;
    if (name) obj_set_name(o, name);
    arr_insert(&s->objs, &s->nobj, &s->cap_o, o);
    o->set = s;
    torpedo = tg_is_type(&o->o, 0x8009);
    e = new_event("TGEvent", torpedo ? ET_TORPEDO_ENTERED_SET : ET_ENTERED_SET, o, NULL);
    tg_add_event(e);
    return 1;
}
long eng_SetClass_AddBackdropToSet(void *sp, void *op, char *name) { return eng_SetClass_AddObjectToSet(sp, op, name); }
long eng_SetClass_RemoveBackdropFromSet(void *sp, void *op)
{
    Set *s = as_set(sp); Obj *o = as_obj(op); if (!s || !o) return 0;
    if (!arr_remove(&s->objs, &s->nobj, o)) return 0;
    o->set = NULL; return 1;
}
long eng_SetClass_SetBackgroundModel(void *s, char *model, float a, float b, float c) { return 1; }
long eng_SetClass_RemoveBackgroundModel(void *s) { return 1; }
static Obj *remove_from_set(Set *s, Obj *o)                              /* 0040f070 */
{
    TGEvent *e; int torpedo = tg_is_type(&o->o, 0x8009);
    /* a TGStringEvent: the set's name (GetCString: Starbase12_S.ExitSet, Mission::PlayerExitedSet 00409170) and the object in the destination; the object group's
       own event (ET_OBJECT_GROUP_OBJECT_EXITED_SET) is the one that carries the object pointer (ConditionInRange.ExitedSet) */
    e = new_event(torpedo ? "TGEvent" : "TGStringEvent", torpedo ? ET_TORPEDO_EXITED_SET : ET_EXITED_SET, o, NULL);
    if (!torpedo) { e->str = dup(s->name ? s->name : ""); e->ref_id = o->o.id; }
    tg_add_event(e);
    arr_remove(&s->objs, &s->nobj, o);
    o->set = NULL;
    return o;
}
void *eng_SetClass_RemoveObjectFromSet(void *sp, char *name)
{
    Set *s = as_set(sp); Obj *o;
    if (!s || !(o = set_find_any(s, name))) return NULL;
    for (int i = 0; i < s->nobj; i++) if (s->objs[i] == o) return remove_from_set(s, o);
    return NULL;
}
void eng_SetClass_DeleteObjectFromSet(void *sp, char *name)              /* 0040f920 -> DeleteObject: remove, then destroy */
{
    Set *s = as_set(sp); Obj *o = s ? set_find_any(s, name) : NULL;
    if (!o) return;
    for (int i = 0; i < s->nobj; i++) if (s->objs[i] == o) { remove_from_set(s, o); o->o.dead = 1; return; }
}
void *eng_SetClass_GetObject(void *sp, char *name)
{
    Set *s = as_set(sp); Obj *o = s ? set_find_any(s, name) : NULL; int i;
    if (!o) return NULL;
    for (i = 0; i < s->nobj; i++) if (s->objs[i] == o) return o;
    return NULL;
}
void *eng_SetClass_GetObjectByID(void *sp, int id)
{
    Set *s = as_set(sp); int i; if (!s) return NULL;
    for (i = 0; i < s->nobj; i++) if (s->objs[i]->o.id == (unsigned)id) return s->objs[i];
    return NULL;
}
void *eng_SetClass_GetFirstObject(void *sp) { Set *s = as_set(sp); return s && s->nobj ? s->objs[0] : NULL; }
void *eng_SetClass_GetNextObject(void *sp, int id)                       /* 0040fe80: the object after id, wrapping to the first */
{
    Set *s = as_set(sp); int i; if (!s || s->nobj <= 0) return NULL;
    for (i = 0; i < s->nobj; i++) if (s->objs[i]->o.id == (unsigned)id) break;
    i = i < s->nobj ? i : -1;
    i++; if (i >= s->nobj) i = 0;
    return s->objs[i];
}
void *eng_SetClass_GetPreviousObject(void *sp, int id)                   /* 0040ff00: when id is not found it starts from the end */
{
    Set *s = as_set(sp); int i; if (!s || s->nobj <= 0) return NULL;
    for (i = 0; i < s->nobj; i++) if (s->objs[i]->o.id == (unsigned)id) break;
    if (i >= s->nobj) i = s->nobj;
    i--; if (i < 0) i = s->nobj - 1;
    return s->objs[i];
}
long eng_SetClass_IsLocationEmptyTG(void *sp, void *p, float r, int flag) { return 1; }
PyObject *eng_SetClass_GetClassObjectList(void *sp, int ct)
{
    Set *s = as_set(sp); PyObject *l = PyList_New(0); int i;
    if (s) for (i = 0; i < s->nobj; i++) if (tg_is_type(&s->objs[i]->o, (unsigned)ct)) { PyObject *w = appc_pyobj(s->objs[i], s->objs[i]->o.cls->name); PyList_Append(l, w); Py_DECREF(w); }
    return l;
}
PyObject *eng_SetClass_GetNavPoints(void *sp)
{
    Set *s = as_set(sp); PyObject *l = PyList_New(0); int i;
    if (s) for (i = 0; i < s->nobj; i++) if (s->objs[i]->nav_point) { PyObject *w = appc_pyobj(s->objs[i], s->objs[i]->o.cls->name); PyList_Append(l, w); Py_DECREF(w); }
    return l;
}
/* cameras */
long eng_SetClass_AddCameraToSet(void *sp, void *cp, char *name)          /* 004128b0: the first camera becomes the active one */
{
    Set *s = as_set(sp); Obj *c = as_obj(cp);
    if (!s || !c) return 0;
    if (name && set_find_any(s, name)) return 0;
    if (name) obj_set_name(c, name);
    arr_insert(&s->cams, &s->ncam, &s->cap_c, c); c->set = s;
    if (s->ncam == 1) s->active_cam = c;
    return 1;
}
long eng_SetClass_CreateCamera(void *sp, float x, float y, float z, float a, float ax, float ay, float az, char *name)
{
    Set *s = as_set(sp); Obj *c;
    if (!s || (name && set_find_any(s, name))) return 0;
    c = camera_new("CameraObjectClass", name, x, y, z, a, ax, ay, az);
    return eng_SetClass_AddCameraToSet(s, c, name);
}
void *eng_SetClass_GetCamera(void *sp, char *name) { Set *s = as_set(sp); Obj *o = s ? set_find_any(s, name) : NULL; return o && tg_is_type(&o->o, 0x80be) ? o : NULL; }
void eng_SetClass_RemoveCameraFromSet(void *sp, char *name)
{
    Set *s = as_set(sp); Obj *c = s ? set_find_any(s, name) : NULL;
    if (!c || !arr_remove(&s->cams, &s->ncam, c)) return;
    c->set = NULL; if (s->active_cam == c) s->active_cam = s->ncam ? s->cams[0] : NULL;
}
void eng_SetClass_DeleteCameraFromSet(void *sp, char *name)
{
    Set *s = as_set(sp); Obj *c = s ? set_find_any(s, name) : NULL;
    if (!c || !tg_is_type(&c->o, 0x80be)) return;
    eng_SetClass_RemoveCameraFromSet(sp, name); c->o.dead = 1;
}
long eng_SetClass_SetActiveCamera(void *sp, char *name)
{
    Set *s = as_set(sp); Obj *c = s ? set_find_any(s, name) : NULL;
    if (!c || !tg_is_type(&c->o, 0x80be)) return 0;
    s->active_cam = c; return 1;
}
void *eng_SetClass_GetActiveCamera(void *sp) { Set *s = as_set(sp); return s ? s->active_cam : NULL; }
/* lights */
static Obj *light_new(Set *s, const char *name, int directional, float r, float g, float b, float intensity)
{
    Obj *l;
    if (name && set_find_any(s, name)) return NULL;
    l = obj_new("LightObjectClass", name); reg_add(l);
    l->color[0] = r; l->color[1] = g; l->color[2] = b; l->intensity = intensity; l->lit_all = directional;
    arr_insert(&s->lights, &s->nlight, &s->cap_l, l); l->set = s;
    return l;
}
long eng_SetClass_CreateAmbientLight(void *sp, float r, float g, float b, float i, char *name)
{ Set *s = as_set(sp); return s && light_new(s, name, 0, r, g, b, i) ? 1 : 0; }
long eng_SetClass_CreateDirectionalLight(void *sp, float dx, float dy, float dz, float r, float g, float b, float i, char *name)
{
    Set *s = as_set(sp); Obj *l; if (!s || !(l = light_new(s, name, 1, r, g, b, i))) return 0;
    l->pos[0] = dx; l->pos[1] = dy; l->pos[2] = dz; return 1;
}
void *eng_SetClass_GetLight(void *sp, char *name) { Set *s = as_set(sp); Obj *o = s ? set_find_any(s, name) : NULL; return o && tg_is_type(&o->o, 0x80bc) ? o : NULL; }
void eng_SetClass_DeleteLightFromSet(void *sp, char *name)
{
    Set *s = as_set(sp); Obj *l = s ? set_find_any(s, name) : NULL;
    if (!l || !arr_remove(&s->lights, &s->nlight, l)) return;
    l->set = NULL; l->o.dead = 1;
}
/* picking and geometry queries (need the renderer / collision) */
void eng_SetClass_StartPick(void *s, float x, float y, int f) { }
void *eng_SetClass_GetPickedObject(void *s) { return NULL; }
double eng_SetClass_GetDotProductToObject(void *s, void *o) { return 0.0; }
double eng_SetClass_GetDotProductToVector(void *s, void *v) { return 0.0; }

/* ================================================= BridgeSet ================================================= */
void *eng_new_BridgeSet(long id) { return set_new("BridgeSet"); }
void eng_delete_BridgeSet(void *s) { eng_delete_SetClass(s); }
void *eng_BridgeSet_Create(void) { return set_new("BridgeSet"); }
void *eng_BridgeSet_Cast(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80d2) ? p : NULL; }
void eng_BridgeSet_SetViewScreen(void *sp, void *vs, char *name)         /* 00665f00: remember it and add it to the set under name */
{
    Set *s = as_set(sp); if (!s) return;
    s->viewscreen = as_obj(vs); eng_SetClass_AddObjectToSet(s, vs, name);
}
void *eng_BridgeSet_GetViewScreen(void *sp) { Set *s = as_set(sp); return s ? s->viewscreen : NULL; }
void eng_BridgeSet_SetConfig(void *sp, char *c) { Set *s = as_set(sp); if (s) { free(s->config); s->config = dup(c); } }
const char *eng_BridgeSet_GetConfig(void *sp) { Set *s = as_set(sp); return s ? s->config : NULL; }
long eng_BridgeSet_IsSameConfig(void *sp, char *c) { Set *s = as_set(sp); return s && c && s->config && !strcmp(c, s->config); }   /* 00665e90 */

/* ================================================= SetManager ================================================= */
static Set **sets; static int nsets, capsets; static Set *rendered_set;
static int set_index(const char *name)
{
    int i; if (!name) return -1;
    for (i = 0; i < nsets; i++) if (sets[i]->name && !strcmp(sets[i]->name, name)) return i;
    return -1;
}
void *eng_SetManager_GetSet(void *m, char *name) { int i = set_index(name); return i >= 0 ? sets[i] : NULL; }
long eng_SetManager_GetNumSets(void *m) { return nsets; }
PyObject *eng_SetManager_GetAllSets(void *m)
{
    PyObject *l = PyList_New(0); int i;
    for (i = 0; i < nsets; i++) { PyObject *w = appc_pyobj(sets[i], sets[i]->o.cls->name); PyList_Append(l, w); Py_DECREF(w); }
    return l;
}
static void destroy_set(Set *s)
{
    int i;
    if (rendered_set == s) rendered_set = NULL;
    while (s->nobj) { Obj *o = s->objs[0]; o->set = NULL; o->o.dead = 1; arr_remove(&s->objs, &s->nobj, o); }
    for (i = 0; i < s->ncam; i++) { s->cams[i]->set = NULL; s->cams[i]->o.dead = 1; }
    for (i = 0; i < s->nlight; i++) { s->lights[i]->set = NULL; s->lights[i]->o.dead = 1; }
    s->ncam = s->nlight = 0; s->active_cam = NULL; s->o.dead = 1;
}
static void drop_set(int i, int destroy)
{
    Set *s = sets[i]; memmove(sets + i, sets + i + 1, (nsets - i - 1) * sizeof *sets); nsets--;
    if (rendered_set == s) rendered_set = NULL;
    if (destroy) destroy_set(s);
}
void eng_SetManager_DeleteSet(void *m, char *name) { int i = set_index(name); if (i >= 0) drop_set(i, 1); }       /* 004181e0 */
void eng_SetManager_RemoveSet(void *m, char *name) { int i = set_index(name); if (i >= 0) drop_set(i, 0); }       /* 004180e0: keeps the set object alive */
void eng_SetManager_AddSet(void *m, void *sp, char *name)                                                          /* 00417f00: an old set of that name is deleted; sets stay sorted by name */
{
    Set *s = as_set(sp); int i;
    if (!s || !name) return;
    if ((i = set_index(name)) >= 0) { if (sets[i] == s) return; drop_set(i, 1); }
    eng_SetClass_SetName(s, name);
    if (nsets == capsets) { capsets = capsets ? capsets * 2 : 8; sets = realloc(sets, capsets * sizeof *sets); }
    for (i = 0; i < nsets && strcmp(sets[i]->name, name) < 0; i++) ;
    memmove(sets + i + 1, sets + i, (nsets - i) * sizeof *sets); sets[i] = s; nsets++;
    { extern void *eng_TGSoundRegion_Create(char *); extern long eng_TGSoundRegion_SetFilter(void *, int);   /* each set has a sound region of its name, muted (filter 1) */
      eng_TGSoundRegion_SetFilter(eng_TGSoundRegion_Create(name), 1); }
}
void eng_SetManager_DeleteAllSets(void *m) { while (nsets) drop_set(nsets - 1, 1); }
void eng_SetManager_Terminate(void *m) { eng_SetManager_DeleteAllSets(m); }
void eng_SetManager_ClearRenderedSet(void *m) { if (rendered_set) rendered_set->rendered = 0; rendered_set = NULL; }
void *eng_SetManager_GetRenderedSet(void *m) { return rendered_set; }
void eng_SetManager_MakeRenderedSet(void *m, char *name)                  /* 004182f0 */
{
    int i = set_index(name); if (i < 0) return;
    {   /* the sound region filters (004182f0): the set that is left goes quiet (filter 1) unless it is the bridge, or the player's set (filter 2);
           the set that is rendered now is audible (filter 0) */
        extern void *eng_TGSoundRegion_GetRegion(char *); extern long eng_TGSoundRegion_SetFilter(void *, int);
        extern void *eng_Game_GetCurrentGame(void), *eng_Game_GetPlayerSet(void *);
        Set *old = rendered_set; void *r = old && old->name ? eng_TGSoundRegion_GetRegion(old->name) : NULL; void *g = eng_Game_GetCurrentGame();
        if (old && r && g) {
            if (as_set(eng_Game_GetPlayerSet(g)) != old) { if (strcmp(old->name, "bridge")) eng_TGSoundRegion_SetFilter(r, 1); }
            else eng_TGSoundRegion_SetFilter(r, 2);
        }
        r = eng_TGSoundRegion_GetRegion(sets[i]->name);
        if (r) eng_TGSoundRegion_SetFilter(r, 0);
    }
    if (rendered_set) rendered_set->rendered = 0;
    rendered_set = sets[i]; rendered_set->rendered = 1;
    /* the player camera follows the view: Camera.PlayerCameraAsViewscreen for "bridge", Camera.PlayerCameraAsSpace for the player's set */
    if (!strcmp(name, "bridge")) tg_py_call_module("Camera", "PlayerCameraAsViewscreen", 0, NULL);
    else {
        extern void *eng_Game_GetCurrentGame(void), *eng_Game_GetPlayerSet(void *); void *g = eng_Game_GetCurrentGame(); Set *ps = g ? as_set(eng_Game_GetPlayerSet(g)) : NULL;
        if (ps && ps->name && !strcmp(ps->name, name)) tg_py_call_module("Camera", "PlayerCameraAsSpace", 0, NULL);
    }
}

/* ================================================= camera modes ================================================= */
/* CameraMode 0041e810: attribute maps (float, id, point) by name. Modes are created by the Python module CameraModes (one function per mode name)
   the first time CameraObjectClass::GetNamedCameraMode asks for them (0042b070). Stack: PushCameraMode / PopCameraMode (0042acd0, 0042ae50). */
typedef struct CamMode {
    TGObject o; char *name; Obj *camera; int refs;
    struct { char *k; float v; } *f; int nf; struct { char *k; long v; } *id; int nid; struct { char *k; float v[3]; } *pt; int npt;
} CamMode;
static CamMode *as_mode(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80c2) ? p : NULL; }
static const char *mode_class(const char *n)
{
    static const char *t[][2] = {{"Chase", "ChaseCameraMode"}, {"Target", "TargetCameraMode"}, {"Map", "MapCameraMode"}, {"TorpCam", "TorpCameraMode"},
        {"ZoomTarget", "ZoomTargetMode"}, {"DropAndWatch", "DropAndWatchMode"}, {"PlacementWatch", "PlacementWatchMode"}, {"Locked", "LockedPositionMode"},
        {"PlaceByDirection", "PlaceByDirectionMode"}};
    unsigned i; for (i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(t[i][0], n)) return t[i][1];
    return NULL;
}
void *eng_CameraMode_Create(char *name, void *cam)
{
    const char *cn = name ? mode_class(name) : NULL; CamMode *m; if (!cn) return NULL;
    m = tg_new(cn, sizeof(CamMode), 0); m->camera = as_obj(cam); m->refs = 0; m->name = NULL;
    m->id = realloc(NULL, sizeof *m->id); m->id[0].k = strdup("Target"); m->id[0].v = 0; m->nid = 1;       /* SetAttrID("Target", 0) in the constructor */
    return m;
}
void eng_delete_CameraMode(void *p) { CamMode *m = as_mode(p); if (m) m->o.dead = 1; }
const char *eng_CameraMode_GetName(void *p) { CamMode *m = as_mode(p); return m ? m->name : NULL; }
long eng_CameraMode_IsValid(void *p) { return as_mode(p) != NULL; }
void eng_CameraMode_Update(void *p) { }
void eng_CameraMode_Reset(void *p) { }
void eng_CameraMode_SnapToIdealPosition(void *p) { }
void eng_CameraMode_SetAttrFloat(void *p, char *k, float v)
{
    CamMode *m = as_mode(p); int i; if (!m || !k) return;
    for (i = 0; i < m->nf; i++) if (!strcmp(m->f[i].k, k)) { m->f[i].v = v; return; }
    m->f = realloc(m->f, (m->nf + 1) * sizeof *m->f); m->f[m->nf].k = strdup(k); m->f[m->nf++].v = v;
}
double eng_CameraMode_GetAttrFloat(void *p, char *k) { CamMode *m = as_mode(p); int i; if (m && k) for (i = 0; i < m->nf; i++) if (!strcmp(m->f[i].k, k)) return m->f[i].v; return 0.0; }
void eng_CameraMode_SetAttrID(void *p, char *k, long v)
{
    CamMode *m = as_mode(p); int i; if (!m || !k) return;
    for (i = 0; i < m->nid; i++) if (!strcmp(m->id[i].k, k)) { m->id[i].v = v; return; }
    m->id = realloc(m->id, (m->nid + 1) * sizeof *m->id); m->id[m->nid].k = strdup(k); m->id[m->nid++].v = v;
}
long eng_CameraMode_GetAttrID(void *p, char *k) { CamMode *m = as_mode(p); int i; if (m && k) for (i = 0; i < m->nid; i++) if (!strcmp(m->id[i].k, k)) return m->id[i].v; return 0; }
void eng_CameraMode_SetAttrIDObject(void *p, char *k, void *o) { Obj *b = as_obj(o); eng_CameraMode_SetAttrID(p, k, b ? (long)b->o.id : 0); }
void *eng_CameraMode_GetAttrIDObject(void *p, char *k)              /* 0041f040: looked up by id in the camera's set (0 when unset) */
{
    CamMode *m = as_mode(p); long id; Obj *o; if (!m) return NULL;
    id = eng_CameraMode_GetAttrID(p, k); if (!id) return NULL;
    o = find_class_obj(NULL, NULL, id, 0x8003); if (o) return o;
    if (m->camera && m->camera->set) { extern void *eng_SetClass_GetObjectByID(void *, int); return eng_SetClass_GetObjectByID(m->camera->set, (int)id); }
    return NULL;
}
void eng_CameraMode_SetAttrPoint(void *p, char *k, void *pt)
{
    CamMode *m = as_mode(p); int i; if (!m || !k || !pt) return;
    for (i = 0; i < m->npt; i++) if (!strcmp(m->pt[i].k, k)) { memcpy(m->pt[i].v, pt, 12); return; }
    m->pt = realloc(m->pt, (m->npt + 1) * sizeof *m->pt); m->pt[m->npt].k = strdup(k); memcpy(m->pt[m->npt++].v, pt, 12);
}
void *eng_CameraMode_GetAttrPoint(void *p, char *k)               /* a fresh TGPoint3; zero when unset */
{
    CamMode *m = as_mode(p); float *r = calloc(3, sizeof *r); int i;
    if (m && k) for (i = 0; i < m->npt; i++) if (!strcmp(m->pt[i].k, k)) { memcpy(r, m->pt[i].v, 12); break; }
    return r;
}
static void cam_add_named(Obj *c, const char *name, CamMode *m)  /* 0042b1f0: replaces (and releases) an older mode of that name */
{
    int i;
    for (i = 0; i < c->nmodes; i++) if (!strcmp(c->modes[i].name, name)) { c->modes[i].m->refs--; c->modes[i].m = m; m->refs++; free(m->name); m->name = strdup(name); return; }
    c->modes = realloc(c->modes, (c->nmodes + 1) * sizeof *c->modes); c->modes[c->nmodes].name = strdup(name); c->modes[c->nmodes++].m = m; m->refs++;
    free(m->name); m->name = strdup(name);
}
void eng_CameraObjectClass_AddNamedCameraMode(void *p, char *name, void *mode) { Obj *c = as_obj(p); CamMode *m = as_mode(mode); if (c && name && m) cam_add_named(c, name, m); }
void eng_CameraObjectClass_AddModeHierarchy(void *p, char *child, char *parent)
{
    Obj *c = as_obj(p); int i; if (!c || !child || !parent) return;
    for (i = 0; i < c->nhier; i++) if (!strcmp(c->hier[i].child, child)) { free(c->hier[i].parent); c->hier[i].parent = strdup(parent); return; }
    c->hier = realloc(c->hier, (c->nhier + 1) * sizeof *c->hier); c->hier[c->nhier].child = strdup(child); c->hier[c->nhier++].parent = strdup(parent);
}
void *eng_CameraObjectClass_GetNamedCameraMode(void *p, char *name)   /* 0042b070 */
{
    Obj *c = as_obj(p); int i; PyObject *fn, *mod, *r, *args; void *mp = NULL;
    if (!c || !name) return NULL;
    for (i = 0; i < c->nmodes; i++) if (!strcmp(c->modes[i].name, name)) return c->modes[i].m;
    mod = PyImport_ImportModule("CameraModes"); if (!mod) { PyErr_Clear(); return NULL; }
    fn = PyObject_GetAttrString(mod, name); Py_DECREF(mod); if (!fn) { PyErr_Clear(); return NULL; }
    args = Py_BuildValue("(O)", r = appc_pyobj(c, c->o.cls->name)); Py_DECREF(r);
    r = PyEval_CallObject(fn, args); Py_DECREF(fn); Py_DECREF(args);
    if (!r) { PyErr_Print(); PyErr_Clear(); return NULL; }
    if (r != Py_None && appc_to_ptr(r, &mp, "void") && as_mode(mp)) cam_add_named(c, name, mp); else { PyErr_Clear(); mp = NULL; }
    Py_DECREF(r); return mp && as_mode(mp) ? mp : NULL;
}
void eng_CameraObjectClass_PushCameraMode(void *p, void *mode)       /* 0042acd0 */
{
    Obj *c = as_obj(p); CamMode *m = as_mode(mode); if (!c || !m) return;
    c->mode_stack = realloc(c->mode_stack, (c->nstack + 1) * sizeof *c->mode_stack); c->mode_stack[c->nstack++] = m; m->refs++; c->current = m;
}
void eng_CameraObjectClass_PopCameraMode(void *p, char *name)         /* 0042ae50: no name = the top mode; else the newest mode with that name */
{
    Obj *c = as_obj(p); int i; if (!c || !c->nstack) return;
    if (!name) i = c->nstack - 1; else { for (i = c->nstack - 1; i >= 0; i--) if (c->mode_stack[i]->name && !strcmp(c->mode_stack[i]->name, name)) break; if (i < 0) return; }
    { CamMode *m = c->mode_stack[i]; memmove(c->mode_stack + i, c->mode_stack + i + 1, (c->nstack - i - 1) * sizeof *c->mode_stack); c->nstack--; m->refs--; if (c->current == m) c->current = NULL; }
    if (!c->current && c->nstack) c->current = c->mode_stack[c->nstack - 1];
}
void *eng_CameraObjectClass_GetCurrentCameraMode(void *p, int resolve)  /* 0042ad70: with resolve, invalid modes defer to their hierarchy parent */
{
    Obj *c = as_obj(p); CamMode *m; int guard = 0; if (!c || !c->nstack) return NULL;
    m = c->mode_stack[c->nstack - 1];
    if (resolve) while (m && m->name && guard++ < 16) {
        int i; const char *par = NULL; CamMode *n;
        if (eng_CameraMode_IsValid(m) && !(m->name && !strncmp(m->name, "Invalid", 7))) break;
        for (i = 0; i < c->nhier; i++) if (!strcmp(c->hier[i].child, m->name)) par = c->hier[i].parent;
        if (!par) break;
        n = eng_CameraObjectClass_GetNamedCameraMode(c, (char *)par); if (!n) break; m = n;
    }
    c->current = m; return m;
}

/* Camera mode update: the current mode of the rendered set's active camera places the camera once per frame.
   ChaseCameraMode::ComputeIdealPosition 00422400: target position + target rotation x unit(DefaultPosition) x Distance x target radius;
   ComputeIdealOrientation 00422550: look at target position + rotation x (ViewTargetOffset x radius), up = the target's up.
   TargetCameraMode::ComputeIdealPosition 00422e20: from the Source ship, away from the Target: unit(-dir x BackWatchPos + up x UpWatchPos)
   x Distance x source radius. FUN_0041ff50 then keeps the camera at least (radius + 1) in front of the watched ship along the view.
   Approximations (marked): no position history (GetLaggedObjectPosition 0041fba0 lags by LagPos / MaxLagDist; here the camera eases
   toward the ideal pose with a time constant of SweepTime / 4); TargetCameraMode looks from the camera toward the point LookBetween of the
   way from the source to the target (ComputeIdealOrientation 004235e0 and the RightPush / MaxSourceUpDot terms are not ported); other modes
   (Map, ZoomTarget, DropAndWatch, ...) fall back to the chase computation on their Target or Source object. */
void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
static void v_norm(float *v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
static void v_cross(const float *a, const float *b, float *r) { r[0] = a[1] * b[2] - a[2] * b[1]; r[1] = a[2] * b[0] - a[0] * b[2]; r[2] = a[0] * b[1] - a[1] * b[0]; }
static int cam_ideal(CamMode *m, float *pos, float *look, float *up)
{
    Obj *src = NULL, *tgt = NULL; float sp[3], sf[3], su[3], sr[3], sc, rad, k; int i;
    if (!strcmp(m->o.cls->name, "TargetCameraMode")) { src = eng_CameraMode_GetAttrIDObject(m, "Source"); tgt = eng_CameraMode_GetAttrIDObject(m, "Target"); }
    if (src && tgt && src != tgt) {                                       /* TargetCameraMode */
        float tp[3], tf[3], tu[3], tr[3], d[3], r[3], o[3], back = (float)eng_CameraMode_GetAttrFloat(m, "BackWatchPos"), upw = (float)eng_CameraMode_GetAttrFloat(m, "UpWatchPos");
        float dist = (float)eng_CameraMode_GetAttrFloat(m, "Distance"), lb = (float)eng_CameraMode_GetAttrFloat(m, "LookBetween");
        tg_space_pose(src, sp, sf, su, sr, &sc); tg_space_pose(tgt, tp, tf, tu, tr, &sc);
        for (i = 0; i < 3; i++) d[i] = tp[i] - sp[i];
        v_norm(d); if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 0.5f) memcpy(d, sf, 12);
        v_cross(d, su, r); v_norm(r); v_cross(r, d, up); v_norm(up);
        for (i = 0; i < 3; i++) o[i] = -d[i] * back + up[i] * upw;
        v_norm(o); rad = (float)eng_ObjectClass_GetRadius(src); if (dist <= 0) dist = 4.0f;
        for (i = 0; i < 3; i++) { pos[i] = sp[i] + o[i] * dist * rad; look[i] = sp[i] + (tp[i] - sp[i]) * lb; }
    } else {                                                              /* ChaseCameraMode (and the fallback) */
        float *dp, *vo, dir[3], dist = (float)eng_CameraMode_GetAttrFloat(m, "Distance");
        tgt = eng_CameraMode_GetAttrIDObject(m, "Target"); if (!tgt) tgt = src;
        if (!tgt) return 0;
        tg_space_pose(tgt, sp, sf, su, sr, &sc);
        dp = eng_CameraMode_GetAttrPoint(m, "DefaultPosition"); vo = eng_CameraMode_GetAttrPoint(m, "ViewTargetOffset");
        if (dp[0] == 0 && dp[1] == 0 && dp[2] == 0) { dp[1] = -1.0f; dp[2] = 0.1f; }   /* the ChaseCameraMode constructor's default */
        v_norm(dp); rad = (float)eng_ObjectClass_GetRadius(tgt); if (dist <= 0) dist = 4.0f;
        for (i = 0; i < 3; i++) {
            dir[i] = sr[i] * dp[0] + sf[i] * dp[1] + su[i] * dp[2];
            pos[i] = sp[i] + dir[i] * dist * rad;
            look[i] = sp[i] + (sr[i] * vo[0] + sf[i] * vo[1] + su[i] * vo[2]) * rad;
            up[i] = su[i];
        }
        free(dp); free(vo); src = tgt;
    }
    {   float d[3], l, ahead, need = (float)eng_ObjectClass_GetRadius(src) + 1.0f;   /* FUN_0041ff50: stay out of the watched ship */
        for (i = 0; i < 3; i++) d[i] = look[i] - pos[i];
        l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l < 1e-6f) return 1;
        for (i = 0; i < 3; i++) d[i] /= l;
        ahead = (sp[0] - pos[0]) * d[0] + (sp[1] - pos[1]) * d[1] + (sp[2] - pos[2]) * d[2];
        if (ahead < need) { k = need - ahead; for (i = 0; i < 3; i++) pos[i] -= k * d[i]; }
    }
    return 1;
}
void tg_camera_tick(double dt)
{
    Set *s = rendered_set; Obj *c; CamMode *m; float pos[3], look[3], up[3], f[3], r[3], a, sw; int i;
    if (!s || !(c = s->active_cam) || !(m = eng_CameraObjectClass_GetCurrentCameraMode(c, 1))) return;
    if (!cam_ideal(m, pos, look, up)) return;
    sw = (float)eng_CameraMode_GetAttrFloat(m, "SweepTime"); if (sw <= 0) sw = 1.0f;
    a = m != (CamMode *)c->current_swept ? 1.0f : 1.0f - expf(-(float)dt * 4.0f / sw);   /* first frame of a mode: snap */
    c->current_swept = m;
    for (i = 0; i < 3; i++) c->pos[i] += (pos[i] - c->pos[i]) * a;
    for (i = 0; i < 3; i++) f[i] = look[i] - c->pos[i];
    v_norm(f); v_cross(f, up, r); v_norm(r); v_cross(r, f, up); v_norm(up);
    if (f[0] * f[0] + f[1] * f[1] + f[2] * f[2] > 0.5f) eng_BaseObjectClass_AlignToVectors(c, f, up);
}

void *eng_BridgeObjectClass_GetPropertySet(void *o) { extern void *eng_TGModelPropertySet_Create(void); Obj *b = as_obj(o); if (!b) return NULL; if (!b->propset) b->propset = eng_TGModelPropertySet_Create(); return b->propset; }
void *eng_DamageableObject_GetPropertySet(void *o) { return eng_BridgeObjectClass_GetPropertySet(o); }
void *eng_BridgeObjectClass_Create(char *model) { Obj *o = obj_new("BridgeObjectClass", NULL); o->model = dup(model); reg_add(o); return o; }   /* model loading: NIF loader not done */
void *eng_BridgeObjectClass_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x8019) ? p : NULL; }
/* for the subclasses written in other files */
void *tg_space_new_obj(const char *cls, const char *name) { Obj *o = obj_new(cls, name); reg_add(o); return o; }
void **tg_space_obj_ext(void *o) { Obj *b = as_obj(o); return b ? &b->ext : NULL; }
void *tg_space_find_by_ext(unsigned ct, int (*match)(void *ext, void *arg), void *arg)
{ int i; for (i = 0; i < nreg; i++) { Obj *o = registry[i].o; if (!o->o.dead && tg_is_type(&o->o, ct) && match(o->ext, arg)) return o; } return NULL; }
/* ZoomCameraObjectClass (ctor 00679e50): a camera with zoom limits. Defaults from the constructor: 0.7 (+0x144), 1.0 (+0x148), 1.0 (+0x14c);
   the mapping of these fields to MinZoom / MaxZoom / ZoomTime is assumed (not verified). Zoom and shake have no effect yet. */
void prop_set_d(void *, const char *, double); double prop_get_d(void *, const char *); void prop_set_i(void *, const char *, long); long prop_get_i(void *, const char *);
static void *zoomcam_new(float x, float y, float z, float a, float ax, float ay, float az, char *name)
{ void *c = camera_new("ZoomCameraObjectClass", name, x, y, z, a, ax, ay, az); prop_set_d(c, "MinZoom", 0.7); prop_set_d(c, "MaxZoom", 1.0); prop_set_d(c, "ZoomTime", 1.0); return c; }
void *eng_ZoomCameraObjectClass_Create(float x, float y, float z, float a, float ax, float ay, float az, char *name) { return zoomcam_new(x, y, z, a, ax, ay, az, name); }
void *eng_new_ZoomCameraObjectClass(long id) { return zoomcam_new(0, 0, 0, 0, 0, 0, 1, NULL); }
void *eng_ZoomCameraObjectClass_Cast(PyObject *o) { void *p = arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x80c0) ? p : NULL; }
CLASS_LOOKUPS(ZoomCameraObjectClass, 0x80c0)
double eng_ZoomCameraObjectClass_GetMaxZoom(void *p) { return prop_get_d(p, "MaxZoom"); }
double eng_ZoomCameraObjectClass_GetMinZoom(void *p) { return prop_get_d(p, "MinZoom"); }
double eng_ZoomCameraObjectClass_GetZoomTime(void *p) { return prop_get_d(p, "ZoomTime"); }
void eng_ZoomCameraObjectClass_SetMaxZoom(void *p, float v) { prop_set_d(p, "MaxZoom", v); }
void eng_ZoomCameraObjectClass_SetMinZoom(void *p, float v) { prop_set_d(p, "MinZoom", v); }
void eng_ZoomCameraObjectClass_SetZoomTime(void *p, float v) { prop_set_d(p, "ZoomTime", v); }
long eng_ZoomCameraObjectClass_IsZoomed(void *p) { return prop_get_i(p, "Zoomed"); }
void eng_ZoomCameraObjectClass_ToggleZoom(void *p, float t) { prop_set_i(p, "Zoomed", !prop_get_i(p, "Zoomed")); }   /* 0067a7b0 flips +0x160 */
void eng_ZoomCameraObjectClass_SetShake(void *p, float a, float b) { prop_set_d(p, "Shake", a); }
double eng_ZoomCameraObjectClass_GetShake(void *p) { return prop_get_d(p, "Shake"); }
void eng_ZoomCameraObjectClass_LookForward(void *p) { }
void eng_ZoomCameraObjectClass_UpdateViewFrustum(void *p) { }
void eng_delete_ZoomCameraObjectClass(void *p) { }
/* Waypoint (PlacementObject subclass): a doubly linked path (+0x74 prev, +0x78 next) with a speed.
   InsertAfterObj 004a43c0: obj goes after this (the scripts: "Attaching object B after A" -> A.InsertAfterObj(B)). RebuildPathCurve is not done. */
void prop_set_p(void *, const char *, void *); void *prop_get_p(void *, const char *);
static Obj *placement_new(const char *cls, char *name, char *set, char *model)
{
    extern void *eng_SetManager_GetSet(void *, char *); extern long eng_SetClass_AddObjectToSet(void *, void *, char *);
    Obj *o = obj_new(cls, name); Set *s; reg_add(o); o->model = dup(model);
    if ((s = as_set(eng_SetManager_GetSet(NULL, set)))) eng_SetClass_AddObjectToSet(s, o, name);
    return o;
}
static Obj *as_wp(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80e0) ? p : NULL; }
void *eng_Waypoint_Create(char *name, char *set, char *model) { Obj *o = placement_new("Waypoint", name, set, model); prop_set_d(o, "Speed", 1.0); return o; }
void *eng_new_Waypoint(char *name, char *set, char *model) { return eng_Waypoint_Create(name, set, model); }
void *eng_Waypoint_Cast(PyObject *o) { return as_wp(arg_ptr(o)); }
void eng_Waypoint_SetSpeed(void *p, float v) { if (as_wp(p)) prop_set_d(p, "Speed", v); }
double eng_Waypoint_GetSpeed(void *p) { return as_wp(p) ? prop_get_d(p, "Speed") : 0.0; }
void *eng_Waypoint_GetNext(void *p) { return as_wp(p) ? prop_get_p(p, "Next") : NULL; }
void *eng_Waypoint_GetPrev(void *p) { return as_wp(p) ? prop_get_p(p, "Prev") : NULL; }
void eng_Waypoint_InsertAfterObj(void *p, void *q)
{
    Obj *a = as_wp(p), *b = as_wp(q), *n; if (!a || !b || a == b) return;
    n = prop_get_p(a, "Next");
    prop_set_p(b, "Next", n); if (n) prop_set_p(n, "Prev", b);
    prop_set_p(a, "Next", b); prop_set_p(b, "Prev", a);
}
/* LightPlacement: a placement that creates a light when the set loads; the light kind and values are kept */
static Obj *as_lp(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80e1) ? p : NULL; }
void *eng_LightPlacement_Create(char *name, char *set, char *model) { return placement_new("LightPlacement", name, set, model); }
void *eng_LightPlacement_Cast(PyObject *o) { return as_lp(arg_ptr(o)); }
static void lp_cfg(void *p, int kind, float r, float g, float b, float i) { Obj *o = as_lp(p); if (!o) return; o->color[0] = r; o->color[1] = g; o->color[2] = b; o->color[3] = 1; o->intensity = i; prop_set_i(o, "LightKind", kind); }
void eng_LightPlacement_ConfigAmbientLight(void *p, float r, float g, float b, float i) { lp_cfg(p, 0, r, g, b, i); }
void eng_LightPlacement_ConfigDirectionalLight(void *p, float r, float g, float b, float i) { lp_cfg(p, 1, r, g, b, i); }
void eng_LightPlacement_ConfigPointLight(void *p, float r, float g, float b, float i, float a) { lp_cfg(p, 2, r, g, b, i); }
void eng_LightPlacement_ConfigSpotLight(void *p, float r, float g, float b, float i, float a, float c, float d, float e) { lp_cfg(p, 3, r, g, b, i); }
void tg_space_each(unsigned ct, void (*fn)(void *)) { int i, n = nreg; for (i = 0; i < n && i < nreg; i++) { Obj *o = registry[i].o; if (!o->o.dead && tg_is_type(&o->o, ct)) fn(o); } }
/* ObjectClass::GetRelativePositionInfo 004353f0: (offset TGPoint3, distance, unit direction TGPoint3, angle between the forward axis and
   the direction: acos(dot), pi when dot <= -1, 0 when dot >= 1) */
PyObject *eng_ObjectClass_GetRelativePositionInfo(void *o, void *pt)
{
    Obj *b = as_obj(o); float *off, *dir, *f, d, dot, ang; int i; PyObject *r, *po, *pd; const float *p = pt;
    if (!b || !p) { Py_INCREF(Py_None); return Py_None; }
    off = malloc(12); dir = malloc(12);
    for (i = 0; i < 3; i++) off[i] = p[i] - b->pos[i];
    d = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
    for (i = 0; i < 3; i++) dir[i] = d > 1e-6f ? off[i] / d : 0;
    if (d <= 1e-6f) d = 0;
    f = axis_of(b, 0); dot = f[0] * dir[0] + f[1] * dir[1] + f[2] * dir[2]; free(f);
    ang = dot <= -1.0f ? 3.14159265f : dot >= 1.0f ? 0.0f : acosf(dot);
    po = appc_pyobj(off, "TGPoint3"); pd = appc_pyobj(dir, "TGPoint3");
    { PyObject *one = PyInt_FromLong(1); if (po) PyObject_SetAttrString(po, "thisown", one); if (pd) PyObject_SetAttrString(pd, "thisown", one); Py_DECREF(one); }
    r = Py_BuildValue("(OfOf)", po, d, pd, ang); Py_XDECREF(po); Py_XDECREF(pd); return r;
}
/* the live object of that name that is in a set (and in `set` when given): ObjectGroup active objects */
void *tg_space_find_active(const char *name, void *set)
{ int i; if (!name) return NULL; for (i = 0; i < nreg; i++) { Obj *o = registry[i].o; if (!o->o.dead && o->set && (!set || (void *)o->set == set) && o->name && !strcmp(o->name, name) && tg_is_type(&o->o, 0x8003)) return o; } return NULL; }
void *tg_space_new_set(const char *cls) { return set_new(cls); }
int tg_space_set_objects(void *sp, void ***out) { Set *s = as_set(sp); *out = s ? (void **)s->objs : NULL; return s ? s->nobj : 0; }

/* ---- renderer access: the rendered set, its camera and the objects' poses. Read only. ---- */
void *tg_render_set(void) { return rendered_set; }
void *tg_render_camera(void) { return rendered_set ? rendered_set->active_cam : NULL; }
void *tg_space_set_of(void *o) { Obj *b = as_obj(o); return b ? b->set : NULL; }
int tg_space_hidden(void *o) { Obj *b = as_obj(o); return b ? b->hidden : 1; }
const char *tg_space_model_of(void *o) { Obj *b = as_obj(o); return b ? b->model : NULL; }
/* position, the model's forward, up and right axes in world space (unit vectors), and the scale */
void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale)
{
    Obj *b = as_obj(o); float *a;
    if (!b) return;
    p[0] = b->pos[0]; p[1] = b->pos[1]; p[2] = b->pos[2];
    if (scale) *scale = b->scale;
    a = axis_of(b, 0); memcpy(f, a, 12); free(a);
    a = axis_of(b, 2); memcpy(u, a, 12); free(a);
    a = axis_of(b, 4); memcpy(r, a, 12); free(a);
}
/* the listener of a set's sound region: the pose of the set's active camera (SetClass::Update 0040ffb0 -> TGSoundRegion 0070e2b0); 0 when there is none */
int tg_space_listener(const char *set_name, float pos[3], float f[3], float u[3], float r[3])
{
    int i = set_index(set_name); Obj *c;
    if (i < 0 || !(c = sets[i]->active_cam)) return 0;
    tg_space_pose(c, pos, f, u, r, NULL);
    return 1;
}
/* the rendered set (MakeRenderedSet without the name): the set the views draw */
void tg_space_set_rendered(void *s)
{
    Set *x = s; int i;
    for (i = 0; i < nsets; i++) if (sets[i] == x) { if (rendered_set) rendered_set->rendered = 0; rendered_set = x; x->rendered = 1; return; }
}
