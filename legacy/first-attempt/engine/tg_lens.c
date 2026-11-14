/* Lens flares. The stock Tactical/LensFlares.py builds one per sun-like source: App.LensFlare_Create(set), SetSource(object,
   direction), AddFlare(wedges, texture, position, size, freq, amp) for each flare, Build(). The flares sit on the line from the source
   through the screen centre: position 0 at the source, 1 at the centre, 2 as far as 0 on the other side. The renderer draws them
   (port/platform/render.c, fx_build).
   The exe (LensFlare::Build 004e31b0, the node's update FUN_004e22f0, FUN_004e2970, FUN_004e1d30): each flare is a fan of `wedges` thin triangles
   (each wedge shows the whole texture, centre at u 0.5 v 1, rim at v 0) on a plane 5 units from the camera toward the source, facing the camera.
   The source direction is the object's own axis (types 0 to 5) or, for type 6 (objects), the direction from the camera to it. Flare k has
   radius 5 * dot * size and sits at (1 - pos) of the source's offset from the view axis; dot is the cosine between the direction and the
   view axis, and the flare only shows when it is above 0. Blend: source alpha, one; no depth test. A ray from the camera to the source
   fades the flares by 0.15 a frame when an object other than a backdrop blocks it, and brings them back at the same rate. The rim wobbles:
   radius 1 + amp * sin(freq * time + angle).
   Approximation (marked): the ray test uses ObjectClass::LineCollides on each object of the set (the exe picks along the ray with NiPick). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "tg_fx.h"

void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
extern void *eng_BaseObjectClass_GetContainingSet(void *);

#define NLENS 64
#define NFLARE 16
typedef struct { int wedges; char *tex; float pos, size, freq, amp; } Flare;
typedef struct { TGObject o; void *set, *src; int dir, n; Flare fl[NFLARE]; float fade; } LensFlare;
static LensFlare *lenses[NLENS]; static int nlenses;

void *eng_LensFlare_Create(void *set)
{
    LensFlare *l = tg_new_plain("LensFlare", sizeof(LensFlare));
    l->set = set;
    /* a new set (mission) takes the place of the oldest flares when the table is full */
    if (nlenses < NLENS) lenses[nlenses++] = l; else { memmove(lenses, lenses + 1, sizeof(lenses[0]) * (NLENS - 1)); lenses[NLENS - 1] = l; }
    return l;
}
void eng_LensFlare_SetSource(void *o, void *src, int dir) { LensFlare *l = o; if (l) { l->src = src; l->dir = dir; } }
void eng_LensFlare_AddFlare(void *o, int wedges, char *tex, float pos, float size, float freq, float amp)
{
    LensFlare *l = o; Flare *f;
    if (!l || l->n >= NFLARE) return;
    f = &l->fl[l->n++]; f->wedges = wedges; f->tex = tex ? strdup(tex) : NULL; f->pos = pos; f->size = size; f->freq = freq; f->amp = amp;
}
void eng_LensFlare_Build(void *o) { (void)o; }

/* the flares of a set seen from a camera (position cp, axes cf forward, cu up, cr right); called once a frame by the renderer (the exe updates
   the node each frame: FUN_004e22f0). Fills the direction to the source in `dir` and the fade. */
extern long eng_ObjectClass_LineCollides(void *o, void *ap, void *bp);
extern void tg_space_each(unsigned ct, void (*fn)(void *));
static float ray_a[3], ray_b[3]; static void *ray_skip, *ray_set; static int ray_hit;
static void ray_one(void *o)
{
    if (o == ray_skip || ray_hit || tg_is_type(o, 0x800b)) return;
    if (ray_set && eng_BaseObjectClass_GetContainingSet(o) != ray_set) return;
    if (eng_BaseObjectClass_GetContainingSet(o) != NULL && eng_ObjectClass_LineCollides(o, ray_a, ray_b)) ray_hit = 1;
}
int tg_fx_blocked(void *set, const float *a, const float *b)
{
    memcpy(ray_a, a, 12); memcpy(ray_b, b, 12); ray_skip = NULL; ray_set = set; ray_hit = 0; tg_space_each(0x8002, ray_one); ray_set = NULL;
    return ray_hit;
}
int tg_fx_lenses(void *set, const float *cp, const float *cf, TgFxLens *out, int max)
{
    int i, k, n = 0;
    for (i = 0; i < nlenses && n < max; i++) {
        LensFlare *l = lenses[i]; TgFxLens *d; float sp[3], sf[3], su[3], sr[3], dir[3], len, dot; TGObject *src;
        if (!l || l->set != set || !l->src || !(src = tg_find(((TGObject *)l->src)->id)) || eng_BaseObjectClass_GetContainingSet(l->src) != set) continue;
        tg_space_pose(l->src, sp, sf, su, sr, NULL);
        switch (l->dir) {
        case 0: memcpy(dir, sr, 12); break;                            /* the object's x axis (right), y (forward), z (up) */
        case 1: memcpy(dir, sf, 12); break;
        case 2: memcpy(dir, su, 12); break;
        case 3: for (k = 0; k < 3; k++) dir[k] = -sr[k]; break;
        case 4: for (k = 0; k < 3; k++) dir[k] = -sf[k]; break;
        case 5: for (k = 0; k < 3; k++) dir[k] = -su[k]; break;
        default: for (k = 0; k < 3; k++) dir[k] = sp[k] - cp[k]; len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]); if (len > 1e-6f) for (k = 0; k < 3; k++) dir[k] /= len; else dir[0] = dir[1] = dir[2] = 0; break;
        }
        dot = dir[0] * cf[0] + dir[1] * cf[1] + dir[2] * cf[2];
        if (dot <= 0.0f) continue;                                     /* the exe skips the update (the flares stay hidden) */
        {   float step = -0.15f;                                       /* nothing in the way: the fade goes down */
            for (k = 0; k < 3; k++) { ray_a[k] = cp[k]; ray_b[k] = l->dir == 6 ? sp[k] : cp[k] + dir[k] * 1.0e6f; }
            ray_skip = l->src; ray_hit = 0; tg_space_each(0x8002, ray_one);
            if (ray_hit) step = 0.15f;
            l->fade += step; if (l->fade < 0.0f) l->fade = 0.0f; if (l->fade > 1.0f) l->fade = 1.0f; }
        d = &out[n];
        memcpy(d->dir, dir, 12); d->dot = dot; d->alpha = 1.0f - l->fade; d->time = (float)tg_game_time;
        d->n = l->n < TG_FX_FLARES ? l->n : TG_FX_FLARES;
        for (k = 0; k < d->n; k++) { d->tex[k] = l->fl[k].tex; d->pos[k] = l->fl[k].pos; d->size[k] = l->fl[k].size; d->wedges[k] = l->fl[k].wedges; d->freq[k] = l->fl[k].freq; d->amp[k] = l->fl[k].amp; }
        if (d->alpha > 0.0f) n++;
    }
    return n;
}
