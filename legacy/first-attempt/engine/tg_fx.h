/* Combat effect data for the renderer: phaser beams, projectiles (torpedoes and pulse shots), lens flares, the particle
   effects of Effects.py (tg_effects.c) and the shield hit patches (tg_shieldfx.c). The engine fills these from its weapon and effect state; the
   platform renderer (port/platform/render.c) reads them each frame. Textures are named by their data path (resolved by the platform).
   No Python and no GPU code here. */
#ifndef TG_FX_H
#define TG_FX_H
/* a phaser beam (PhaserProperty values times the ship scale; see render.c fx_beam): a: emitter, b: hit point; col: outer shell, inner shell, outer core,
   inner core; sides: columns of the half tube; radius: MainRadius; taper_r: TaperRadius; core_scale; taper_ratio, taper_min, taper_max: the end tapers;
   ltile: texture repeats along a model unit; ptile: around; speed: texture scroll per second; age: seconds since the beam started; scale: the ship scale */
typedef struct { float a[3], b[3], col[4][4], radius, taper_r, core_scale, taper_ratio, taper_min, taper_max, ltile, ptile, speed, age, scale; int sides; const char *tex; const void *key; void *owner; } TgFxBeam;   /* key: the weapon, for the state of the end glows; owner: the ship that fires (the beam's two lights are effects of that ship's node, 004e6e90 / 004e75b0) */
/* A projectile. Kind 1 torpedo (CreateTorpedoModel 004f4630): a camera-facing glow quad (side glow_size, texture glow_tex, colour shell, alpha 0.8) and core quad
   (side core_size, core_tex, colour core), both source alpha / one minus source alpha. Kind 2 disruptor bolt (CreateDisruptorModel, FUN_004dd250): a spindle of
   `sides` columns, length len, radius width, with an inner core of in_r times the radius and in_l times the length, additive; f u r are the bolt's axes. */
typedef struct { float p[3], dir[3], shell[4], core[4], core_size, glow_size, len, width, in_r, in_l, f[3], u[3], r[3]; int kind, sides; const char *core_tex, *glow_tex; } TgFxShot;   /* textures by data path (NULL: soft sprite) */
/* One flare ray of a torpedo (FUN_004f4630 and the controller FUN_004f5780): a quad (corners in world space, uv (0,0) (0,1) (1,1) (1,0)), additive, colour
   with the ray's own alpha. */
typedef struct { float v[4][3], col[4]; const char *tex; } TgFxRay;
int tg_fx_rays(void *set, TgFxRay *out, int max);
int tg_fx_beams(void *set, TgFxBeam *out, int max);
int tg_fx_shots(void *set, TgFxShot *out, int max);
/* Lens flares (tg_lens.c): a flare is a fan of `wedges` triangles on the plane 5 units from the camera toward the source (see tg_lens.c).
   dir is the unit direction to the source, dot its cosine with the view axis, alpha the fade (1 when nothing blocks the source). */
#define TG_FX_FLARES 16
typedef struct { float dir[3], dot, alpha, time; int n, wedges[TG_FX_FLARES]; const char *tex[TG_FX_FLARES]; float pos[TG_FX_FLARES], size[TG_FX_FLARES], freq[TG_FX_FLARES], amp[TG_FX_FLARES]; } TgFxLens;
int tg_fx_blocked(void *set, const float *a, const float *b);   /* an object of the set (not a backdrop) crosses the segment a to b */
int tg_fx_lenses(void *set, const float *cp, const float *cf, TgFxLens *out, int max);
/* Particle effects (tg_effects.c): camera-facing quads, in drawing order. size is the full width; rgba is the vertex colour (the texture is
   multiplied by it); uv is the texture rectangle (u0 v0 u1 v1, v down); sblend and dblend are the NetImmerse alpha modes (0 one, 1 zero, 6 source
   alpha, 7 one minus source alpha). Spark tails are lines with a colour at each end. */
typedef struct { float p[3], size, rgba[4], uv[4]; const char *tex; int sblend, dblend; } TgFxSprite;
typedef struct { float a[3], b[3], ca[4], cb[4]; } TgFxLine;
int tg_fx_sprites(void *set, TgFxSprite *out, int max);
int tg_fx_spark_lines(void *set, TgFxLine *out, int max);
/* Shield hit patches (tg_shieldfx.c): a cap of an ellipsoid on a ship. The ship's pose (position p, axes r f u, scale), the ellipsoid (centre
   offset and radii in model units, times the scale once more at draw time as in the exe), the hit direction in model space, the colour, the
   fade (1 to 0 over 3 seconds). The renderer makes the mesh and the random vertex alpha (seed names the patch). */
typedef struct { float p[3], r[3], f[3], u[3], scale, centre[3], radii[3], dir[3], rgba[4], fade; unsigned seed; } TgFxShieldPatch;
int tg_fx_shield_patches(void *set, TgFxShieldPatch *out, int max);
void tg_shieldfx_hit(void *ship, const float *dir, float frac, const float *glow);
void tg_shieldfx_clear(void *ship);
/* Backdrops (tg_backdrop.c): a sphere around the camera, drawn first with no depth test; f u r are the object's axes; the blend modes are NetImmerse alpha modes. */
typedef struct { const char *tex; float f[3], u[3], r[3], htile, vtile, hspan, vspan, radius; int poly, sblend, dblend, star; } TgFxBackdrop;
int tg_fx_backdrops(void *set, TgFxBackdrop *out, int max);
/* Hull damage (tg_damage.c). tg_damage_add_volume: a sphere in the object's mesh space (centre, radius) with a strength (AddDamageVolume 00594020).
   tg_damage_geom: the mesh buffers of a damaged object, 8 floats a vertex (position, normal, uv), triangle lists: `base` replaces the model mesh when `touched`
   (the normal's length is the vertex colour: the scorch), `ring` is the charred band, drawn with the damage texture. version changes with every edit.
   tg_fx_debris: the pieces thrown off ships: a pose and the vertex buffers of the model meshes (index into tg_model_meshes of the model) they hold. */
typedef struct { const float *base; int nbase; const float *ring; int nring; int touched; } TgDamageMesh;
typedef struct { int version, nmesh; TgDamageMesh *mesh; } TgDamageGeom;
void tg_damage_add_volume(void *obj, const float *centre, float radius, float strength);
void tg_damage_forget(void *obj);
const TgDamageGeom *tg_damage_geom(void *obj);
#define TG_FX_DEBRIS_PARTS 24
typedef struct { int mesh; const float *v; int nv; } TgDebrisPart;
typedef struct { unsigned id; const char *model; float p[3], f[3], u[3], r[3], scale, fade; int nparts; TgDebrisPart parts[TG_FX_DEBRIS_PARTS]; } TgFxDebris;
int tg_fx_debris(void *set, TgFxDebris *out, int max);
void tg_effects_tick(void);
int tg_ship_cloak_view(void *obj, float *v, int *mode, int *is_player);   /* the cloak look of a ship (tg_ship.c): 0 when drawn normally */
#endif
