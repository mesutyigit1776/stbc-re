/* The shield hit effect: what ShipClass::WeaponHitHandler 005af010 does for a hit on a shield (FUN_005af160).
   The exe keeps one effect node per ship (ShipClass +0x244), made on the ship's first shield hit with the shield glow colour times the
   shield's remaining fraction (FUN_004e9c60; the colour stays that of the first hit). Each hit is a patch of that node (FUN_004ea1e0, a
   NiTriShape): a cap of the ellipsoid that encloses the model's bounding box (the box half extents times sqrt(3), ShipClass FUN_005abac0),
   turned toward the hit direction, with the texture data/sphere.tga and standard alpha blending (FUN_004ea690 builds the cap: polar angle
   0.8 pi wide, longitude 0.8 pi wide, a grid of resolution / 2 points each way; FUN_004ea4a0 turns and scales it). A new hit within the cosine
   0.9 of an existing patch restarts that patch (FUN_004e9fb0); a hit with no shield left hides the patches near its direction
   (FUN_004ea170). For 3 seconds (FUN_004ea3e0, the constant at 0x88d658) the vertex alpha is a new random number each frame times
   (1 - age / 3) times the colour's alpha, so the patch flickers and fades. A hit only draws when the shield keeps more than 0.1 of its maximum
   (event +0x50 = shield after the hit / maximum).
   Approximations (marked): the grid is 64 by 64 (the exe: 128 by 128 at the default detail level 3; the renderer makes the mesh each frame);
   the hit direction is the radial direction of the hit point (the exe takes the surface normal of the model; the port's hits are on the
   bounding sphere); the ellipsoid uses the model's vertex box where the exe uses the bound stored in the NIF node. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "tg_fx.h"
#include "tg_models.h"

void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
extern void *eng_BaseObjectClass_GetContainingSet(void *);
extern const char *tg_space_model_of(void *o);

#define PATCHES 16
#define NSHIPS 48
typedef struct { float dir[3]; double t0; int culled, used; } Patch;
typedef struct { unsigned id; float col[4]; Patch p[PATCHES]; int next; } ShipFx;
static ShipFx fx[NSHIPS];

static ShipFx *find(void *ship, int create, const float *col)
{
    unsigned id = ((TGObject *)ship)->id; int i, free_i = -1;
    for (i = 0; i < NSHIPS; i++) { if (fx[i].id == id) return &fx[i]; if (!fx[i].id && free_i < 0) free_i = i; }
    if (!create) return NULL;
    if (free_i < 0) free_i = 0;                                  /* all in use: the first loses its effect */
    memset(&fx[free_i], 0, sizeof fx[free_i]); fx[free_i].id = id; memcpy(fx[free_i].col, col, 16);
    return &fx[free_i];
}
/* FUN_005af160: dir is the hit direction in the ship's model space (the event's ObjectHitNormal), frac the shield's fraction after the hit */
void tg_shieldfx_hit(void *ship, const float *dir, float frac, const float *glow)
{
    ShipFx *s; int i; float d[3], l;
    if (!ship || !((TGObject *)ship)->id) return;
    l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (l > 1e-6f) { d[0] = dir[0] / l; d[1] = dir[1] / l; d[2] = dir[2] / l; } else { d[0] = d[1] = 0; d[2] = 1; }
    if (frac <= 0.0f) {                                          /* the shield is gone: patches near this direction go (FUN_004ea170) */
        if ((s = find(ship, 0, NULL))) for (i = 0; i < PATCHES; i++) if (s->p[i].used && d[0] * s->p[i].dir[0] + d[1] * s->p[i].dir[1] + d[2] * s->p[i].dir[2] > 0.9f) s->p[i].culled = 1;
        return;
    }
    if (frac <= 0.1f) return;
    {   float col[4] = {glow[0] * frac, glow[1] * frac, glow[2] * frac, glow[3] * frac};
        s = find(ship, 1, col); }
    for (i = 0; i < PATCHES; i++) if (s->p[i].used && d[0] * s->p[i].dir[0] + d[1] * s->p[i].dir[1] + d[2] * s->p[i].dir[2] > 0.9f) break;
    if (i == PATCHES) { i = s->next; s->next = (s->next + 1) % PATCHES; }      /* a new patch, or the oldest is reused */
    memcpy(s->p[i].dir, d, 12); s->p[i].t0 = tg_game_time; s->p[i].culled = 0; s->p[i].used = 1;
}
void tg_shieldfx_clear(void *ship)                               /* ShipClass::Explode: the effect pointer is dropped */
{ ShipFx *s; if (ship && ((TGObject *)ship)->id && (s = find(ship, 0, NULL))) memset(s, 0, sizeof *s); }

#define PATCH_SECONDS 3.0f
int tg_fx_shield_patches(void *set, TgFxShieldPatch *out, int max)
{
    int n = 0, i, j;
    for (i = 0; i < NSHIPS && n < max; i++) {
        TGObject *o; float centre[3], half[3]; int have_box = 0;
        if (!fx[i].id || !(o = tg_find(fx[i].id)) || eng_BaseObjectClass_GetContainingSet(o) != set) continue;
        for (j = 0; j < PATCHES && n < max; j++) {
            Patch *p = &fx[i].p[j]; TgFxShieldPatch *q; float age, sc, k;
            if (!p->used || p->culled) continue;
            age = (float)(tg_game_time - p->t0); if (age < 0 || age >= PATCH_SECONDS) continue;
            if (!have_box) { have_box = 1; if (!tg_model_box(tg_space_model_of(o), centre, half)) { centre[0] = centre[1] = centre[2] = 0; half[0] = half[1] = half[2] = 1.0f; } }
            q = &out[n++];
            tg_space_pose(o, q->p, q->f, q->u, q->r, &sc); q->scale = sc;
            k = 1.7320508f * sc;                                 /* sqrt(3) times the node scale */
            q->radii[0] = half[0] * k; q->radii[1] = half[1] * k; q->radii[2] = half[2] * k;
            q->centre[0] = centre[0] * sc; q->centre[1] = centre[1] * sc; q->centre[2] = centre[2] * sc;
            memcpy(q->dir, p->dir, 12); memcpy(q->rgba, fx[i].col, 16); q->fade = 1.0f - age / PATCH_SECONDS;
            q->seed = (unsigned)(fx[i].id * 2654435761u) ^ (unsigned)j;
        }
    }
    return n;
}
