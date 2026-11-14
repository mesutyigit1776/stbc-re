/* Backdrops: the star and nebula spheres of a set. Systems/*.py make them with App.StarSphere_Create and App.BackdropSphere_Create
   (name, texture, tiles, spans, radius, alpha blend modes) and add them to the set; the exe builds a sphere mesh around the camera for each
   (BackdropSphere::BuildGeometry 0058db00, StarSphere::BuildGeometry 0058e520) and draws them first, with no depth test and no depth write.
   A BackdropSphere covers `hspan` of the circle around (0.25 by default) and `vspan` of the half circle from pole to pole (0.5 by default); u runs
   with the longitude times HTile, v with the latitude times VTile. The default blend is (one, one), set by SetAlphaBlendModes. A StarSphere covers the
   whole sphere and v only runs 0 to 0.5.
   Approximations (marked): the grid comes from TargetPolyCount (256 gives 17 by 9 points); the exe sizes it by GetTextureDimensions; the detail
   texture (SetDetailTexture*) is not drawn; Decimate is ignored. */
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "tg_fx.h"

void *tg_space_new_obj(const char *cls, const char *name); void **tg_space_obj_ext(void *o);
void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
extern void *eng_BaseObjectClass_GetContainingSet(void *); extern void eng_BaseObjectClass_SetName(void *, char *);
void tg_space_each(unsigned ct, void (*fn)(void *));
const char *tg_asset_resolve(const char *path);

typedef struct { char *tex, *model; float htile, vtile, hspan, vspan, radius; int poly, sblend, dblend, star; } Bd;
static Bd *bd(void *o)
{
    void **e;
    if (!o || !tg_is_ours(o) || !tg_is_type(o, 0x800b) || !(e = tg_space_obj_ext(o))) return NULL;
    return (Bd *)*e;
}
static void *bd_new(const char *cls, int star)
{
    void *o = tg_space_new_obj(cls, NULL); void **e = tg_space_obj_ext(o); Bd *b = calloc(1, sizeof *b);
    b->htile = b->vtile = 1.0f; b->hspan = 0.25f; b->vspan = 0.5f; b->radius = 300.0f; b->poly = 256; b->sblend = b->dblend = 0; b->star = star;   /* BackdropSphere_load 0058d2a0 */
    if (star) { b->hspan = 1.0f; b->vspan = 1.0f; }
    *e = b; return o;
}
void *eng_Backdrop_Create(void) { return bd_new("Backdrop", 0); }
void *eng_BackdropSphere_Create(void) { return bd_new("BackdropSphere", 0); }
void *eng_StarSphere_Create(void) { return bd_new("StarSphere", 1); }
static void *cast(PyObject *o, unsigned ct) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
void *eng_Backdrop_Cast(PyObject *o) { return cast(o, 0x800b); }
void *eng_BackdropSphere_Cast(PyObject *o) { return cast(o, 0x800c); }
void *eng_StarSphere_Cast(PyObject *o) { return cast(o, 0x800d); }
void eng_Backdrop_SetName(void *o, char *n) { eng_BaseObjectClass_SetName(o, n); }
void eng_Backdrop_SetModelName(void *o, char *n) { Bd *b = bd(o); if (b) { free(b->model); b->model = n ? strdup(n) : NULL; } }
const char *eng_Backdrop_GetModelName(void *o) { Bd *b = bd(o); return b ? b->model : NULL; }
void eng_Backdrop_SetContainingSet(void *o, void *s) { }
void eng_delete_Backdrop(void *o) { }
void eng_delete_BackdropSphere(void *o) { }
void eng_delete_StarSphere(void *o) { }
void eng_BackdropSphere_SetTextureFileName(void *o, char *n) { Bd *b = bd(o); if (b) { free(b->tex); b->tex = n ? strdup(n) : NULL; } }
void eng_BackdropSphere_SetTextureHTile(void *o, float v) { Bd *b = bd(o); if (b) b->htile = v; }
void eng_BackdropSphere_SetTextureVTile(void *o, float v) { Bd *b = bd(o); if (b) b->vtile = v; }
void eng_BackdropSphere_SetHorizontalSpan(void *o, float v) { Bd *b = bd(o); if (b) b->hspan = v; }
void eng_BackdropSphere_SetVerticalSpan(void *o, float v) { Bd *b = bd(o); if (b) b->vspan = v; }
void eng_BackdropSphere_SetSphereRadius(void *o, float v) { Bd *b = bd(o); if (b) b->radius = v; }
void eng_BackdropSphere_SetTargetPolyCount(void *o, int v) { Bd *b = bd(o); if (b) b->poly = v; }
void eng_BackdropSphere_SetAlphaBlendModes(void *o, int s, int d) { Bd *b = bd(o); if (b) { b->sblend = s; b->dblend = d; } }
void eng_BackdropSphere_SetDecimate(void *o, int v) { }
void eng_BackdropSphere_Rebuild(void *o) { }
void eng_BackdropSphere_SetDetailTextureFileName(void *o, char *n) { }
void eng_BackdropSphere_SetDetailTextureHTile(void *o, float v) { }
void eng_BackdropSphere_SetDetailTextureVTile(void *o, float v) { }

static void *bd_set; static TgFxBackdrop *bd_out; static int bd_n, bd_max;
static void bd_one(void *o)
{
    Bd *b = bd(o); TgFxBackdrop *d; float p[3], sc;
    if (!b || !b->tex || bd_n >= bd_max || eng_BaseObjectClass_GetContainingSet(o) != bd_set) return;
    d = &bd_out[bd_n++]; tg_space_pose(o, p, d->f, d->u, d->r, &sc);
    d->tex = b->tex; d->htile = b->htile; d->vtile = b->vtile; d->hspan = b->hspan; d->vspan = b->vspan; d->radius = b->radius; d->poly = b->poly;
    d->sblend = b->sblend; d->dblend = b->dblend; d->star = b->star;
}
int tg_fx_backdrops(void *set, TgFxBackdrop *out, int max) { bd_set = set; bd_out = out; bd_n = 0; bd_max = max; tg_space_each(0x800b, bd_one); return bd_n; }
