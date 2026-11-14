/* Particle effects: the App.EffectController family that Effects.py builds, as stbc.exe has it.
   Effects.py (the stock script, and the mods' copies) creates a controller (AnimTSParticleController, SparkParticleController,
   ExplosionPlumeController), sets its keys and emit values, gives it a texture, names the object it emits from and the node it attaches to,
   and wraps it in an EffectAction. This file keeps those objects and runs them every frame, so the scripts run unchanged and the exe's own
   values (not copies of them) decide how an effect looks.
   Behaviour from the decompile: EffectController::Add*Key / Get*AtTime 004ca2a0..004ca760 (keys sorted by time, before the first key the
   first value, after the last key the last value, linear between), EmitterController::AllocateParticleArrays 004cbeb0 (the effect life time is at
   least the particle life; the emitter runs for life time minus particle life; at most rate * life + 1 particles), EmitterController::Update
   004cc8c0 / EmitParticle 004cc310 / UpdateParticle 004cc0c0 (Euler step, gravity, damping; random direction inside a cone whose half angle is
   AngleVariance / 2, uniform in the angle; the emit radius moves the start along that direction), FUN_004cc640 (the emit position follows the
   emit-from object, in the space of the node the effect is attached to; InheritsVelocity adds the object's velocity), AnimTSParticleController
   004c6f10..004c7360 (a random texture track of the sheet for each particle, frame = (frames - 1) * age / life; the sheets are the
   TextureAnimManager containers that Tactical/EffectTextures.py registers), SparkParticleController 004d24b0..004d2980 (points and a tail line of
   TailLength opposite to the old velocity, the tail's end has alpha times TailFade), ExplosionPlumeController 004cf3f0..004cf610 (the emitter
   itself flies with a random velocity), EffectAction 004de640..004de860 (the action lasts the effect life time and stops the controller).
   Approximations (marked): the NetImmerse property flags of the target are read as the standard alpha blend (source alpha, one minus source
   alpha) unless SetTargetAlphaBlendModes names the modes (flags 0xED in the exe's target set-up); a spark's head is a camera-facing quad of the
   particle size and its tail a thin line; ObjectClass.GetRandomPointOnModel picks a random model vertex (the exe's pickRandomModelVertex) and
   keeps the object's orientation; the pick-target collision of the plume (NiPick) is not done (the script never sets one). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "tg_fx.h"
#include "tg_models.h"

void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
extern void *eng_BaseObjectClass_GetContainingSet(void *);
extern const char *tg_space_model_of(void *o);
int tg_ship_velocity(void *o, float v[3]);
int tg_texanim_tracks(const char *file);
const float (*tg_texanim_track(const char *file, int track, int *nframes))[4];
const char *tg_asset_resolve(const char *path);
void *tg_effect_action_new(void (*on_play)(void *, void *), void (*on_end)(void *, void *), void *user);
void tg_action_set_duration(void *p, float d);

#define PI_F 3.14159265f
#define RND() ((float)(rand() & 0x7fff) / 32767.0f)

/* ---------------------------------------------------------------- nodes ---------------------------------------------------------------- */
/* What Effects.py calls a node: the set's effect root, an object's node (BaseObjectClass.GetNode), or a point on an object's model
   (ObjectClass.GetRandomPointOnModel). They are handles that carry the object's id, so a destroyed object leaves its last pose. */
typedef struct AVNode {
    TGObject o; int kind; unsigned obj_id; void *set; float off[3];
    float lp[3], lf[3], lu[3], lr[3], ls; int have;
} AVNode;
static AVNode *new_node(int kind, void *obj, void *set)
{
    AVNode *n = tg_new_plain("NiNode", sizeof *n);
    n->kind = kind; n->obj_id = obj ? ((TGObject *)obj)->id : 0; n->set = set; n->ls = 1.0f;
    return n;
}
static AVNode *as_node(PyObject *o)
{
    void *p = NULL;
    if (!o || o == Py_None) return NULL;
    if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; }
    return p && tg_is_ours(p) && ((TGObject *)p)->cls && !strcmp(((TGObject *)p)->cls->name, "NiNode") ? p : NULL;
}
static AVNode *as_node_ptr(void *p) { return p && tg_is_ours(p) && ((TGObject *)p)->cls && !strcmp(((TGObject *)p)->cls->name, "NiNode") ? p : NULL; }
/* the pose of a node: position, the model axes (forward, up, right) and the scale */
static int node_pose(AVNode *n, float p[3], float f[3], float u[3], float r[3], float *sc)
{
    int i;
    if (n->kind == 0) {
        p[0] = p[1] = p[2] = 0; r[0] = 1; r[1] = r[2] = 0; f[0] = f[2] = 0; f[1] = 1; u[0] = u[1] = 0; u[2] = 1; *sc = 1.0f; return 1;
    }
    {   TGObject *o = n->obj_id ? tg_find(n->obj_id) : NULL;
        if (o) { tg_space_pose(o, n->lp, n->lf, n->lu, n->lr, &n->ls); n->have = 1; } }
    if (!n->have) return 0;
    memcpy(p, n->lp, 12); memcpy(f, n->lf, 12); memcpy(u, n->lu, 12); memcpy(r, n->lr, 12); *sc = n->ls;
    if (n->kind == 2) for (i = 0; i < 3; i++) p[i] += n->ls * (n->off[0] * r[i] + n->off[1] * f[i] + n->off[2] * u[i]);
    return 1;
}
static AVNode *object_node(void *o)                                /* one node per object, kept */
{
    static AVNode *cache[256]; static unsigned ids[256]; unsigned id = ((TGObject *)o)->id; int i = (int)(id & 255);
    if (cache[i] && ids[i] == id && cache[i]->obj_id == id) return cache[i];
    cache[i] = new_node(1, o, eng_BaseObjectClass_GetContainingSet(o)); ids[i] = id; return cache[i];
}
int tg_effects_node_pos(void *pt, float out[3])                    /* the world position of a node (a random model point included); 0 when it is not one */
{
    AVNode *n = as_node_ptr(pt); float p[3], f[3], u[3], r[3], sc;
    if (!n || !node_pose(n, p, f, u, r, &sc)) return 0;
    memcpy(out, p, 12); return 1;
}
void *eng_BaseObjectClass_GetNode(void *o) { return o && tg_is_ours(o) && ((TGObject *)o)->id ? object_node(o) : NULL; }
void *eng_SetClass_GetEffectRoot(void *s)
{
    static void *sets[64]; static AVNode *roots[64]; int i;
    if (!s) return NULL;
    for (i = 0; i < 64 && sets[i]; i++) if (sets[i] == s) return roots[i];
    if (i == 64) i = 63;
    sets[i] = s; roots[i] = new_node(0, NULL, s); return roots[i];
}
void *eng_TGModelUtils_CastNodeToAVObject(PyObject *a0) { return as_node(a0); }
void *eng_ObjectClass_GetRandomPointOnModel(void *o)
{
    AVNode *n; float p[3], nr[3]; const char *m;
    if (!o || !tg_is_ours(o) || !((TGObject *)o)->id) return NULL;
    n = new_node(2, o, eng_BaseObjectClass_GetContainingSet(o));
    m = tg_space_model_of(o);
    if (tg_model_random_vertex(m, p, nr)) memcpy(n->off, p, 12);
    return n;
}

/* ---------------------------------------------------------------- controllers ---------------------------------------------------------------- */
typedef struct { float t, v[3]; } CKey;
typedef struct { float t, v; } FKey;
#define MAXKEYS 64
typedef struct Part {
    float pos[3], vel[3], birth, life, size, col[4], tail[3]; int frame, nfr; const float (*fr)[4];
} Part;
typedef struct Ctl {
    TGObject o; int kind;                                           /* 0 AnimTS, 1 Spark, 2 ExplosionPlume */
    CKey ck[MAXKEYS]; int nck; FKey ak[MAXKEYS]; int nak; FKey sk[MAXKEYS]; int nsk;
    float duration, emit_life, life_var, vel, vel_var, angle_var, freq, freq_var, radius, grav[3], damp;
    float tail_len, tail_fade; int inherits, old_to_new;
    AVNode *emit_obj, *attach; float epos[3], frame[3][3];          /* emit position (emit-object space) and the frame (direction, two perpendiculars) */
    float plume_vel[3];
    char *tex; int sblend, dblend; int has_target, attached, started, done; void *set;
    const char *sheet;                                              /* AnimTS: the registered texture name */
    int ntracks;
    double last, start; float acc; int count, maxp, remaining;
    Part *p;
    float cur_pos[3], cur_frame[3][3], inh_vel[3];                  /* the emit state in attach-node space, from the last update */
} Ctl;
static Ctl *new_ctl(const char *cls, int kind)
{
    Ctl *c = tg_new_plain(cls, sizeof *c); int i;
    c->kind = kind; c->duration = 1.0f; c->emit_life = 1.0f; c->freq = 1.0f; c->tail_len = 1.0f; c->tail_fade = 0.2f; c->old_to_new = 1;
    c->sblend = 6; c->dblend = 7;
    for (i = 0; i < 3; i++) c->frame[i][i] = 1.0f;
    return c;
}
static int is_ctl_class(void *p)
{
    const char *n; if (!p || !tg_is_ours(p) || !((TGObject *)p)->cls) return 0; n = ((TGObject *)p)->cls->name;
    return !strcmp(n, "AnimTSParticleController") || !strcmp(n, "SparkParticleController") || !strcmp(n, "ExplosionPlumeController");
}
#define CTL(p) Ctl *c = is_ctl_class(p) ? (Ctl *)(p) : NULL
#define EFFECT_LEVEL_DEFAULT 3
static int effect_level = EFFECT_LEVEL_DEFAULT;                    /* DAT_008e0d14: 3 (HIGH) until the graphics options change it */
long eng_EffectController_GetEffectLevel(void) { return effect_level; }
void eng_EffectController_SetEffectLevel(int v) { effect_level = v < 0 ? 0 : v > 4 ? 4 : v; }

void *eng_AnimTSParticleController_Create(void) { return new_ctl("AnimTSParticleController", 0); }
void eng_AnimTSParticleController_SetDrawOldToNew(void *p, int v) { CTL(p); if (c) c->old_to_new = v != 0; }
void *eng_SparkParticleController_Create(float a, float b, float f, float d, float e, void *k)
{
    Ctl *c = new_ctl("SparkParticleController", 1);
    c->duration = a; c->emit_life = b; c->freq = f; c->vel = d; c->angle_var = e;     /* the exe's defaults for the last two are 35 and 90 */
    return c;
}
void *eng_ExplosionPlumeController_Create(void) { return new_ctl("ExplosionPlumeController", 2); }
void eng_SparkParticleController_SetTailLength(void *p, float v) { CTL(p); if (c) c->tail_len = v; }
void eng_SparkParticleController_SetTailFade(void *p, float v) { CTL(p); if (c) c->tail_fade = v; }

void eng_EffectController_SetEffectLifeTime(void *p, float v) { CTL(p); if (c) c->duration = v; }
double eng_EffectController_GetEffectLifeTime(void *p) { CTL(p); return c ? c->duration : 0.0; }
void eng_EmitterController_SetEmitLife(void *p, float v) { CTL(p); if (c) c->emit_life = v; }
void eng_EmitterController_SetEmitLifeVariance(void *p, float v) { CTL(p); if (c) c->life_var = v; }
void eng_EmitterController_SetEmitVelocity(void *p, float v) { CTL(p); if (c) c->vel = v; }
void eng_EmitterController_SetEmitVelocityVariance(void *p, float v) { CTL(p); if (c) c->vel_var = v; }
void eng_EmitterController_SetAngleVariance(void *p, float v) { CTL(p); if (c) c->angle_var = v; }
void eng_EmitterController_SetEmitFrequency(void *p, float v) { CTL(p); if (c) c->freq = v; }
void eng_EmitterController_SetEmitFrequencyVariance(void *p, float v) { CTL(p); if (c) c->freq_var = v; }
void eng_EmitterController_SetEmitRadius(void *p, float v) { CTL(p); if (c) c->radius = v; }
void eng_EmitterController_SetGravity(void *p, float x, float y, float z) { CTL(p); if (c) { c->grav[0] = x; c->grav[1] = y; c->grav[2] = z; } }
void eng_EmitterController_SetDamping(void *p, float v) { CTL(p); if (c) c->damp = v; }
void eng_EmitterController_SetInheritsVelocity(void *p, int v) { CTL(p); if (c) c->inherits = v != 0; }
void eng_EmitterController_SetDetachEmitObject(void *p, int v) { }
void eng_EmitterController_SetEmitFromObject(void *p, void *o) { CTL(p); if (c) c->emit_obj = as_node_ptr(o); }
void *eng_EmitterController_GetEmitFromObject(void *p) { CTL(p); return c ? c->emit_obj : NULL; }

/* SetEmitPositionAndDirection 004ccb00: the direction, a unit vector perpendicular to it, and their cross product */
PyObject *eng_EmitterController_SetEmitPositionAndDirection(PyObject *args)
{
    PyObject *so, *po, *dop; void *sp = NULL, *pp = NULL, *dp = NULL; Ctl *c; const float *pos, *dir;
    if (!PyArg_ParseTuple(args, "OOO", &so, &po, &dop)) return NULL;
    if (!appc_to_ptr(so, &sp, "void") || !appc_to_ptr(po, &pp, "void") || !appc_to_ptr(dop, &dp, "void")) { PyErr_Clear(); Py_INCREF(Py_None); return Py_None; }
    c = is_ctl_class(sp) ? sp : NULL; pos = pp; dir = dp;
    if (c && pos && dir) {
        float d[3] = {dir[0], dir[1], dir[2]}, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), ax[3] = {0, 0, 0}, v[3], w[3], vl;
        if (l > 1e-6f) { d[0] /= l; d[1] /= l; d[2] /= l; } else { d[0] = 1; d[1] = d[2] = 0; }
        if (fabsf(d[1]) <= fabsf(d[0])) { if (fabsf(d[2]) < fabsf(d[1])) ax[2] = 1.0f; else ax[1] = 1.0f; } else { if (fabsf(d[0]) <= fabsf(d[2])) ax[0] = 1.0f; else ax[2] = 1.0f; }
        v[0] = d[1] * ax[2] - d[2] * ax[1]; v[1] = d[2] * ax[0] - d[0] * ax[2]; v[2] = d[0] * ax[1] - d[1] * ax[0];
        vl = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (vl > 1e-6f) { v[0] /= vl; v[1] /= vl; v[2] /= vl; }
        w[0] = v[1] * d[2] - v[2] * d[1]; w[1] = v[2] * d[0] - v[0] * d[2]; w[2] = v[0] * d[1] - v[1] * d[0];
        memcpy(c->epos, pos, 12); memcpy(c->frame[0], d, 12); memcpy(c->frame[1], v, 12); memcpy(c->frame[2], w, 12);
    }
    Py_INCREF(Py_None); return Py_None;
}

/* keys: sorted by time, a key at an existing time replaces it (EffectController::AddColorKey 004ca460) */
void eng_EffectController_AddColorKey(void *p, float t, float r, float g, float b)
{
    CTL(p); int i, j; if (!c) return;
    for (i = 0; i < c->nck; i++) if (c->ck[i].t == t) { c->ck[i].v[0] = r; c->ck[i].v[1] = g; c->ck[i].v[2] = b; return; }
    if (c->nck >= MAXKEYS) return;
    for (i = 0; i < c->nck && c->ck[i].t < t; i++) ;
    for (j = c->nck; j > i; j--) c->ck[j] = c->ck[j - 1];
    c->ck[i].t = t; c->ck[i].v[0] = r; c->ck[i].v[1] = g; c->ck[i].v[2] = b; c->nck++;
}
static void add_fkey(FKey *k, int *n, float t, float v)
{
    int i, j;
    for (i = 0; i < *n; i++) if (k[i].t == t) { k[i].v = v; return; }
    if (*n >= MAXKEYS) return;
    for (i = 0; i < *n && k[i].t < t; i++) ;
    for (j = *n; j > i; j--) k[j] = k[j - 1];
    k[i].t = t; k[i].v = v; (*n)++;
}
void eng_EffectController_AddAlphaKey(void *p, float t, float v) { CTL(p); if (c) add_fkey(c->ak, &c->nak, t, v); }
void eng_EffectController_AddSizeKey(void *p, float t, float v) { CTL(p); if (c) add_fkey(c->sk, &c->nsk, t, v); }
void eng_EffectController_RemoveAllColorKeys(void *p) { CTL(p); if (c) c->nck = 0; }
void eng_EffectController_RemoveAllAlphaKeys(void *p) { CTL(p); if (c) c->nak = 0; }
void eng_EffectController_RemoveAllSizeKeys(void *p) { CTL(p); if (c) c->nsk = 0; }
static void eval_f(const FKey *k, int n, float t, float *out)
{
    int i;
    if (n <= 0) { *out = 1.0f; return; }
    for (i = 0; i < n; i++) if (t < k[i].t) {
        if (i == 0) { *out = k[0].v; return; }
        *out = k[i - 1].v + (k[i].v - k[i - 1].v) * ((t - k[i - 1].t) / (k[i].t - k[i - 1].t)); return;
    }
    *out = k[n - 1].v;
}
static void eval_c(const CKey *k, int n, float t, float out[3])
{
    int i, j;
    if (n <= 0) { out[0] = out[1] = out[2] = 1.0f; return; }
    for (i = 0; i < n; i++) if (t < k[i].t) {
        float f;
        if (i == 0) { memcpy(out, k[0].v, 12); return; }
        f = (t - k[i - 1].t) / (k[i].t - k[i - 1].t);
        for (j = 0; j < 3; j++) out[j] = k[i - 1].v[j] + (k[i].v[j] - k[i - 1].v[j]) * f; return;
    }
    memcpy(out, k[n - 1].v, 12);
}

/* CreateTarget: AnimTS (004c7360) needs the texture's sheet in the TextureAnimManager and the picture itself; a spark's points only the picture.
   AllocateParticleArrays 004cbeb0 fixes the particle count here from the values set so far. */
static void alloc_arrays(Ctl *c)
{
    float maxlife = c->emit_life + c->life_var * 0.5f, rate = 1.0f / (c->freq - c->freq_var * 0.5f);
    int emissions;
    if (c->duration < maxlife) c->duration = maxlife;
    c->maxp = (int)(rate * maxlife) + 1;
    emissions = (int)((c->duration - maxlife) * rate) + 1;
    c->remaining = emissions;
    if (c->maxp > emissions) c->maxp = emissions;
    if (c->maxp < 1) c->maxp = 1;
    free(c->p); c->p = calloc((size_t)c->maxp, sizeof *c->p); c->count = 0;
}
void eng_EffectController_CreateTarget(void *p, char *name)
{
    CTL(p); if (!c) return;
    free(c->tex); c->tex = NULL; c->has_target = 0;
    if (!name || !*name) return;
    if (c->kind != 1) {                                             /* AnimTS and the plume: the sheet must be registered, the picture must load */
        c->ntracks = tg_texanim_tracks(name);
        if (c->ntracks < 1 || !tg_asset_resolve(name)) { if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[fx] no sheet for %s (tracks %d, file %s)\n", name, c->ntracks, tg_asset_resolve(name) ? "found" : "missing"); return; }
    } else if (!tg_asset_resolve(name)) return;
    c->tex = strdup(name); c->sheet = c->tex;
    if (c->kind != 1 && getenv("APPC_FX_TRACE") && strstr(getenv("APPC_FX_TRACE"), "sheet")) {
        static char seen[16][128]; int i, t, n; const float (*fr)[4];
        for (i = 0; i < 16 && seen[i][0]; i++) if (!strcmp(seen[i], name)) break;
        if (i < 16 && !seen[i][0]) {
            snprintf(seen[i], sizeof seen[i], "%s", name);
            fprintf(stderr, "[fx] sheet %s: %d tracks\n", name, c->ntracks);
            for (t = 0; t < c->ntracks && t < 3; t++) { fr = tg_texanim_track(name, t, &n);
                fprintf(stderr, "[fx]  track %d: %d frames; first %.3f %.3f %.3f %.3f; last %.3f %.3f %.3f %.3f\n", t, n, fr[0][0], fr[0][1], fr[0][2], fr[0][3], fr[n - 1][0], fr[n - 1][1], fr[n - 1][2], fr[n - 1][3]); }
        }
    }
    alloc_arrays(c); c->has_target = 1;
}
void eng_EffectController_SetTargetAlphaBlendModes(void *p, int s, int d) { CTL(p); if (c) { c->sblend = s; c->dblend = d; } }
void *eng_EffectController_GetTargetCast(void *p) { CTL(p); return c && c->has_target ? c : NULL; }

/* ---- the running set ---- */
#define MAXACTIVE 768
static Ctl *active[MAXACTIVE]; static int nactive;

void eng_EffectController_AttachEffect(void *p, PyObject *node)
{
    CTL(p); AVNode *n = as_node(node); if (!c || !n || !c->has_target) return;
    c->attach = n; c->attached = 1;
    c->set = n->kind == 0 ? n->set : (n->obj_id && tg_find(n->obj_id) ? eng_BaseObjectClass_GetContainingSet(tg_find(n->obj_id)) : n->set);
}
static void ctl_stop(Ctl *c)
{
    int i;
    if (!c->started) return;
    c->started = 0; c->done = 1; c->count = 0;
    for (i = 0; i < nactive; i++) if (active[i] == c) { active[i] = active[--nactive]; break; }
}
void eng_EffectController_DetachEffect(void *p) { CTL(p); if (c) { c->attached = 0; ctl_stop(c); } }

/* the emit position and frame in the space of the attach node: FUN_004cc640 */
static void to_local(const float *ap, const float *af, const float *au, const float *ar, float as, const float *w, float out[3])
{
    float d[3] = {w[0] - ap[0], w[1] - ap[1], w[2] - ap[2]};
    out[0] = (d[0] * ar[0] + d[1] * ar[1] + d[2] * ar[2]) / as; out[1] = (d[0] * af[0] + d[1] * af[1] + d[2] * af[2]) / as; out[2] = (d[0] * au[0] + d[1] * au[1] + d[2] * au[2]) / as;
}
static void vec_to_local(const float *af, const float *au, const float *ar, const float *w, float out[3])
{
    out[0] = w[0] * ar[0] + w[1] * ar[1] + w[2] * ar[2]; out[1] = w[0] * af[0] + w[1] * af[1] + w[2] * af[2]; out[2] = w[0] * au[0] + w[1] * au[1] + w[2] * au[2];
}
static void update_emit(Ctl *c, float dt)
{
    float ap[3], af[3], au[3], ar[3], as, ep[3], ef[3], eu[3], er[3], es, w[3]; int i, k;
    if (!c->attach || !node_pose(c->attach, ap, af, au, ar, &as)) return;
    if (c->kind == 2 && dt > 0) {                                   /* the plume's emitter flies (FUN_004cf610) */
        if (rand() % 200 < 2) {                                     /* now and then it bends: 0.7 of the velocity, 0.3 of a random direction with its length */
            float rd[3], rl, sp = sqrtf(c->plume_vel[0] * c->plume_vel[0] + c->plume_vel[1] * c->plume_vel[1] + c->plume_vel[2] * c->plume_vel[2]);
            for (k = 0; k < 3; k++) rd[k] = RND() * 2.0f - 1.0f;
            rl = sqrtf(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
            if (rl > 1e-6f) for (k = 0; k < 3; k++) c->plume_vel[k] = 0.7f * c->plume_vel[k] + 0.3f * rd[k] / rl * sp;
        }
        for (k = 0; k < 3; k++) { c->epos[k] += c->plume_vel[k] * dt; c->plume_vel[k] += c->grav[k] * dt; }
        if (c->damp > 0) for (k = 0; k < 3; k++) c->plume_vel[k] -= c->plume_vel[k] * (c->damp * dt);
    }
    memset(c->inh_vel, 0, sizeof c->inh_vel);
    if (c->emit_obj && node_pose(c->emit_obj, ep, ef, eu, er, &es)) {
        float ev[3];
        for (i = 0; i < 3; i++) w[i] = ep[i] + es * (c->epos[0] * er[i] + c->epos[1] * ef[i] + c->epos[2] * eu[i]);
        to_local(ap, af, au, ar, as, w, c->cur_pos);
        for (k = 0; k < 3; k++) {
            float v[3]; for (i = 0; i < 3; i++) v[i] = c->frame[k][0] * er[i] + c->frame[k][1] * ef[i] + c->frame[k][2] * eu[i];
            vec_to_local(af, au, ar, v, c->cur_frame[k]);
        }
        if (c->inherits) {
            TGObject *eo = c->emit_obj->obj_id ? tg_find(c->emit_obj->obj_id) : NULL;
            if (eo && tg_ship_velocity(eo, ev)) { float lv[3]; vec_to_local(af, au, ar, ev, lv); for (i = 0; i < 3; i++) c->inh_vel[i] = lv[i] / as; }
        }
    } else {                                                        /* no emit object: the position and direction are in the world */
        to_local(ap, af, au, ar, as, c->epos, c->cur_pos);
        for (k = 0; k < 3; k++) vec_to_local(af, au, ar, c->frame[k], c->cur_frame[k]);
    }
}
static void update_particle(Ctl *c, Part *q, float dt, float t)
{
    float col[3], a; int k;
    eval_c(c->ck, c->nck, t, col); eval_f(c->ak, c->nak, t, &a); eval_f(c->sk, c->nsk, t, &q->size);
    q->col[0] = col[0]; q->col[1] = col[1]; q->col[2] = col[2]; q->col[3] = a;
    if (c->kind == 1) { float l = sqrtf(q->vel[0] * q->vel[0] + q->vel[1] * q->vel[1] + q->vel[2] * q->vel[2]); for (k = 0; k < 3; k++) q->tail[k] = l > 1e-6f ? q->vel[k] / l : 0.0f; }
    for (k = 0; k < 3; k++) q->pos[k] += q->vel[k] * dt;
    for (k = 0; k < 3; k++) q->vel[k] += c->grav[k] * dt;
    { float d = dt * c->damp; for (k = 0; k < 3; k++) q->vel[k] -= d * q->vel[k]; }
    if (c->kind != 1 && q->nfr > 0) { int fr = (int)((float)(q->nfr - 1) * t); q->frame = fr < 0 ? 0 : fr >= q->nfr ? q->nfr - 1 : fr; }
}
static int emit_particle(Ctl *c, double time)
{
    Part *q; float life, th, ph, v[3], l, sp; int k;
    life = (RND() - 0.5f) * c->life_var + c->emit_life;
    if (!(life > 0.0f)) return 0;
    q = &c->p[c->count]; memset(q, 0, sizeof *q);
    memcpy(q->pos, c->cur_pos, 12); q->birth = (float)time; q->life = life;
    th = c->angle_var * (1.0f / 360.0f) * RND() * PI_F; ph = RND() * 2.0f * PI_F;
    for (k = 0; k < 3; k++) v[k] = c->cur_frame[0][k] * cosf(th) + c->cur_frame[1][k] * sinf(th) * cosf(ph) + c->cur_frame[2][k] * sinf(th) * sinf(ph);
    l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (k = 0; k < 3; k++) v[k] /= l; else v[0] = v[1] = v[2] = 0.0f;
    if (c->radius > 0.0f) { float u = RND(); for (k = 0; k < 3; k++) q->pos[k] += u * v[k] * c->radius; }
    sp = (RND() - 0.5f) * c->vel_var + c->vel;
    for (k = 0; k < 3; k++) q->vel[k] = v[k] * sp + (c->inherits ? c->inh_vel[k] : 0.0f);
    if (c->kind != 1 && c->sheet) {
        int tr = c->ntracks > 1 ? (int)(rand() % (c->ntracks - 1)) : 0;   /* GetRandomNumber(tracks - 1): the last track is never picked, as in the exe */
        int n = 0; const float (*fr)[4] = tg_texanim_track(c->sheet, tr, &n);
        q->fr = fr; q->nfr = n;
    }
    c->count++;
    return 1;
}
/* EmitterController::Update 004cc8c0 */
static void ctl_update(Ctl *c, double time)
{
    float dt; int i;
    if (c->last < 0) c->last = time - 0.01;
    dt = (float)(time - c->last);
    if (dt < 0.0f) return;
    update_emit(c, dt);
    for (i = 0; i < c->count; i++) {
        float age = (float)time - c->p[i].birth;
        if (age <= c->p[i].life) update_particle(c, &c->p[i], dt, age / c->p[i].life);
        else { c->p[i] = c->p[c->count - 1]; c->count--; i--; }      /* the last particle takes its place (RemoveParticle 004cc200) */
    }
    c->acc += dt;
    if (c->acc >= c->freq) {
        do {
            if (c->remaining < 1 || c->count >= c->maxp) break;
            c->acc -= (RND() - 0.5f) * c->freq_var + c->freq;
            if (emit_particle(c, time)) { Part *q = &c->p[c->count - 1]; update_particle(c, q, c->acc, 0.0f); }
            c->remaining--;
        } while (c->freq <= c->acc);
    }
    c->last = time;
}

/* the effect starts when its action plays (FUN_004ca220): the keys that are missing get one default key (EffectController::AddDefaultKeysIfEmpty) */
static void ctl_start(Ctl *c)
{
    if (c->started || !c->has_target || !c->attached) return;
    if (!c->nck) eng_EffectController_AddColorKey(c, 0.0f, 1.0f, 1.0f, 1.0f);
    if (!c->nak) eng_EffectController_AddAlphaKey(c, 0.0f, 1.0f);
    if (!c->nsk) eng_EffectController_AddSizeKey(c, 0.0f, 1.0f);
    c->started = 1; c->done = 0; c->start = tg_game_time; c->last = tg_game_time;
    if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[fx] %.2f start %s kind %d dur %.2f life %.2f freq %.3f max %d tex %s\n", tg_game_time, ((TGObject *)c)->cls->name, c->kind, c->duration, c->emit_life, c->freq, c->maxp, c->tex);
    if (nactive < MAXACTIVE) active[nactive++] = c;
}
void tg_effects_tick(void)
{
    int i;
    for (i = 0; i < nactive; i++) ctl_update(active[i], tg_game_time);
}

void eng_ExplosionPlumeController_SetEmitterVelocity(void *p, void *v) { CTL(p); if (c && v) memcpy(c->plume_vel, v, 12); }
/* SetUpRandomVelocity 004cf440: a random direction inside the cone (same rule as an emitted particle) times speed plus a random part of the variance */
void eng_ExplosionPlumeController_SetUpRandomVelocity(void *p, float cone, float speed, float var)
{
    CTL(p); float th, ph, v[3], l; int k;
    if (!c) return;
    th = cone * (1.0f / 360.0f) * RND() * PI_F; ph = RND() * 2.0f * PI_F;
    for (k = 0; k < 3; k++) v[k] = c->frame[0][k] * cosf(th) + c->frame[1][k] * sinf(th) * cosf(ph) + c->frame[2][k] * sinf(th) * sinf(ph);
    l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (k = 0; k < 3; k++) v[k] /= l; else v[0] = v[1] = v[2] = 0.0f;
    speed = RND() * var + speed;
    for (k = 0; k < 3; k++) c->plume_vel[k] = v[k] * speed;
}

/* EffectAction: the action lasts the controller's effect life time; playing it starts the controller, completing or skipping it stops it */
static void eff_play(void *a, void *user)
{
    Ctl *c = user;
    if (!c) { tg_action_set_duration(a, 0.0f); return; }
    tg_action_set_duration(a, c->duration);
    ctl_start(c);
}
static void eff_end(void *a, void *user) { Ctl *c = user; if (c) ctl_stop(c); }
void *eng_EffectAction_Create(PyObject *ctl)
{
    void *p = NULL; Ctl *c = NULL;
    if (ctl && ctl != Py_None) { if (appc_to_ptr(ctl, &p, "void")) c = is_ctl_class(p) ? p : NULL; else PyErr_Clear(); }
    return tg_effect_action_new(eff_play, eff_end, c);
}

/* ---------------------------------------------------------------- what the renderer reads ---------------------------------------------------------------- */
static void world_of(Ctl *c, const float *lp, float out[3], const float *ap, const float *af, const float *au, const float *ar, float as)
{
    int i; for (i = 0; i < 3; i++) out[i] = ap[i] + as * (lp[0] * ar[i] + lp[1] * af[i] + lp[2] * au[i]);
}
int tg_fx_sprites(void *set, TgFxSprite *out, int max)
{
    int n = 0, ci;
    for (ci = 0; ci < nactive && n < max; ci++) {
        Ctl *c = active[ci]; float ap[3], af[3], au[3], ar[3], as; int i, k, cnt;
        if (c->set != set || !c->attach || !node_pose(c->attach, ap, af, au, ar, &as)) continue;
        cnt = c->count;
        for (k = 0; k < cnt && n < max; k++) {
            Part *q; TgFxSprite *s;
            i = c->old_to_new ? k : cnt - 1 - k;
            q = &c->p[i]; s = &out[n++];
            world_of(c, q->pos, s->p, ap, af, au, ar, as);
            s->size = q->size * as; memcpy(s->rgba, q->col, 16);
            if (c->kind != 1 && q->fr && q->nfr > 0) memcpy(s->uv, q->fr[q->frame], 16); else { s->uv[0] = 0; s->uv[1] = 0; s->uv[2] = 1; s->uv[3] = 1; }
            s->tex = c->tex; s->sblend = c->sblend; s->dblend = c->dblend;
        }
    }
    return n;
}
int tg_fx_spark_lines(void *set, TgFxLine *out, int max)
{
    int n = 0, ci;
    for (ci = 0; ci < nactive && n < max; ci++) {
        Ctl *c = active[ci]; float ap[3], af[3], au[3], ar[3], as; int k;
        if (c->kind != 1 || c->set != set || !c->attach || !node_pose(c->attach, ap, af, au, ar, &as)) continue;
        for (k = 0; k < c->count && n < max; k++) {
            Part *q = &c->p[k]; TgFxLine *l = &out[n++]; float tl[3]; int i;
            for (i = 0; i < 3; i++) tl[i] = q->pos[i] - q->tail[i] * c->tail_len;
            world_of(c, q->pos, l->a, ap, af, au, ar, as); world_of(c, tl, l->b, ap, af, au, ar, as);
            memcpy(l->ca, q->col, 16); memcpy(l->cb, q->col, 16); l->cb[3] = q->col[3] * c->tail_fade;
        }
    }
    return n;
}
int tg_fx_effects_active(void) { return nactive; }

/* profiling calls of the scripts: nothing to time here */
long eng_TGProfilingInfo_StartTiming(char *name) { return 0; }
void eng_TGProfilingInfo_StopTiming(long id) { }
