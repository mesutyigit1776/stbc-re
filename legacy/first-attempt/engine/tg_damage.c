/* Hull damage: the exe's damage volumes, the triangles they break, the breach drawing, and the chunks thrown off a ship.
   Platform-free: it reads the model's meshes and voxel file (tg_models.c), is fed by the weapon hits and the death script (tg_ship.c) and gives the
   renderer buffers (tg_fx.h: tg_damage_geom, tg_fx_debris). Everything below follows the exe; where it does not, the comment says so.

   Volumes. DamageableObject::AddDamageVolume 00594020 turns a hit (object-space point, radius, damage) into a DamageVolume (004bbde0): a sphere in
   model space (centre, radius, radius squared, strength = the damage). A hull hit makes one (WeaponHitHandler 005af010, FUN_005af420): radius twice the
   event radius, strength half the event's damage, weapon types 0 and 1 only. The death script adds more (Effects.DeathExplosionDamage: AddDamage at a
   random point of the model, radius / 4, damage 600), scripts can add their own (AddObjectDamageVolume).
   Triangle damage (the "damage geometry" option, DAT_008e5c1c; TriDamage record, ctor FUN_004b46d0; thresholds 0.8 at +8, 0.5 at +0xc, 0.1 at +0x14):
   - every triangle that a volume's sphere touches is marked (FUN_004b4b40; the exe finds them with the OBB tree of each shape, leaves under
     10 triangles, and tests each triangle against the sphere exactly) and is removed from the shape's index list (FUN_004b4380);
   - the damage field of a vertex is the sum over the volumes of strength * (1 - d^2 / r^2)^2 for d < r (FUN_004b4e50);
   - a marked triangle is clipped by that field (FUN_004b71e0 with the tables at 0088c620 / 0088c638): the part below 0.5 stays as hull, with vertex
     colour ((0.5 - field) / 0.5)^2 (so it darkens toward the hole, black at the cut) and a crossing point on each cut edge found by linear interpolation
     of the field along the edge; the rest is clipped again at 0.8 (FUN_004b8420): the band between 0.5 and 0.8 is drawn with the damage texture
     (data/Textures/Effects/Damage.tga, LODEntry fields 5 and 6; uv times 5), what is above 0.8 is gone (the hole).
   Not done: FUN_004b5ed0, the special case for triangles that touch an earlier cut (the exe re-cuts those with their neighbours' edge points); here a
   marked triangle is always cut from the original, so repeated hits on one spot give the same cut as the sum of their volumes would.
   Chunks (the "breakable components" option, DAT_008e5c1e, and the voxel damage mesh of the "volume damage" option 008e5c1d). The voxel file of a model
   holds seven grids; the union of them is the occupied voxel body (grid 0 alone is only its core: the Galaxy's nacelles are separate in it, one body in the
   union). A volume carves the cells whose centre lies where the field reaches 0.8 (the same hole as above). After each volume the connected pieces of
   what is left are counted (6-neighbour, FUN_004b14f0 list, cell count at +0x3c); every piece except the largest with more than 9 cells is thrown off as
   debris (spawnVisibleDamageDebris 00592850, SpawnDamageDebris 00591ee0: a new object with the piece's share of the mass, set going away from the
   ship's centre with some random jitter, living as long as the ship's wreck, 120 s when the ship is still alive). Approximations: the piece's surface is the ship's own triangles that lie in its
   cells (the exe re-meshes the piece from the voxels, marching cubes: voxelGridEmitCellTriangles 004bf700); the debris is a drawn piece that flies
   and spins, not a set object (no collisions, no weapon hits). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "tg_fx.h"
#include "tg_models.h"

extern double tg_game_time;
extern const char *tg_space_model_of(void *o);
extern void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
extern int tg_dmg_flag(int which);                                  /* tg_ship.c: 0 damage geometry, 1 volume damage geometry, 2 breakable components */
extern int tg_ship_motion(void *o, float *vel, float *life);        /* tg_ship.c: velocity and remaining life of a ship */

#define ISO_HOLE 0.8f                                               /* TriDamage +8 */
#define ISO_SCORCH 0.5f                                             /* TriDamage +0xc */
#define MIN_CELLS 10                                                /* a piece needs more than 9 cells to become debris (RefreshVisibleDamage 00592580) */
#define MAX_VOLS 600

typedef struct { float c[3], r, r2, s; } Vol;
typedef struct { float *v; int n, cap; } Buf;                       /* n floats */
typedef struct { int ntri; unsigned char *marked, *gone; Buf base, ring; int touched; } MeshDmg;
typedef struct Debris {
    unsigned id; float t0, life, p[3], v[3], ax[3], w, f[3], u[3], r[3], scale; const char *model;
    int nparts; struct { int mesh; float *v; int nv; } *parts; struct Debris *next;
} Debris;
typedef struct DmgShip {
    unsigned obj_id; void *obj; const char *model; const TgMesh *meshes; int nmesh; MeshDmg *md;
    Vol *vols; int nvols, cap; int version;
    const TgVoxel *vox; unsigned char *alive; int nalive0; int *label;
    TgDamageGeom geom; struct DmgShip *next;
} DmgShip;
static DmgShip *ships; static Debris *debris_list; static unsigned debris_id = 1, rng = 22695477u;

void tg_damage_forget(void *obj);
static float frand(void) { rng = rng * 1103515245u + 12345u; return (float)((rng >> 8) & 0xffff) / 65535.0f; }
static void buf_push(Buf *b, const float *vtx)                       /* one vertex of 8 floats */
{
    if (b->n + 8 > b->cap) { b->cap = b->cap ? b->cap * 2 : 1024; b->v = realloc(b->v, sizeof(float) * (size_t)b->cap); }
    memcpy(b->v + b->n, vtx, 32); b->n += 8;
}

/* ---- geometry helpers ---- */
static float closest_d2(const float *p, const float *a, const float *b, const float *c)      /* squared distance from p to triangle abc (Ericson) */
{
    float ab[3], ac[3], ap[3], d1, d2, va, vb, vc, bp[3], cp[3], d3, d4, d5, d6, v, w, q[3]; int k;
    for (k = 0; k < 3; k++) { ab[k] = b[k] - a[k]; ac[k] = c[k] - a[k]; ap[k] = p[k] - a[k]; }
    d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2]; d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
    if (d1 <= 0 && d2 <= 0) { memcpy(q, a, 12); goto done; }
    for (k = 0; k < 3; k++) bp[k] = p[k] - b[k];
    d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2]; d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
    if (d3 >= 0 && d4 <= d3) { memcpy(q, b, 12); goto done; }
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { v = d1 / (d1 - d3); for (k = 0; k < 3; k++) q[k] = a[k] + v * ab[k]; goto done; }
    for (k = 0; k < 3; k++) cp[k] = p[k] - c[k];
    d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2]; d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
    if (d6 >= 0 && d5 <= d6) { memcpy(q, c, 12); goto done; }
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { w = d2 / (d2 - d6); for (k = 0; k < 3; k++) q[k] = a[k] + w * ac[k]; goto done; }
    va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); for (k = 0; k < 3; k++) q[k] = b[k] + w * (c[k] - b[k]); goto done; }
    { float den = 1.0f / (va + vb + vc); v = vb * den; w = vc * den; for (k = 0; k < 3; k++) q[k] = a[k] + ab[k] * v + ac[k] * w; }
done:
    return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
}
static float field(const DmgShip *d, const float *p)                  /* FUN_004b4e50: sum of strength * (1 - d^2 / r^2)^2 */
{
    float sum = 0; int i;
    for (i = 0; i < d->nvols; i++) {
        const Vol *v = &d->vols[i]; float dx = p[0] - v->c[0], dy = p[1] - v->c[1], dz = p[2] - v->c[2], d2 = dx * dx + dy * dy + dz * dz, x;
        if (d2 < v->r2) { x = 1.0f - d2 / v->r2; sum += x * x * v->s; }
    }
    return sum;
}

/* ---- the clipped triangle (FUN_004b71e0 and FUN_004b8420) ---- */
typedef struct { float p[3], n[3], uv[2], d; } Vx;                    /* a polygon corner: position, normal, uv, damage field */
static void lerp_vx(Vx *o, const Vx *a, const Vx *b, float iso)       /* the crossing point of the edge a to b where the field is iso */
{
    float t = fabsf(b->d - a->d) < 1e-6f ? 0.5f : (iso - a->d) / (b->d - a->d); int k;
    for (k = 0; k < 3; k++) { o->p[k] = a->p[k] + t * (b->p[k] - a->p[k]); o->n[k] = a->n[k] + t * (b->n[k] - a->n[k]); }
    for (k = 0; k < 2; k++) o->uv[k] = a->uv[k] + t * (b->uv[k] - a->uv[k]);
    o->d = iso;
}
/* splits a convex polygon at the field value iso: `lo` gets the part with the field below it, `hi` the rest (crossing corners belong to both, field = iso) */
static void split_poly(const Vx *in, int n, float iso, Vx *lo, int *nlo, Vx *hi, int *nhi)
{
    int i; *nlo = *nhi = 0;
    for (i = 0; i < n; i++) {
        const Vx *a = &in[i], *b = &in[(i + 1) % n]; int al = a->d < iso, bl = b->d < iso;
        if (al) lo[(*nlo)++] = *a; else hi[(*nhi)++] = *a;
        if (al != bl) { Vx x; lerp_vx(&x, a, b, iso); lo[(*nlo)++] = x; hi[(*nhi)++] = x; }
    }
}
static void emit_fan(Buf *b, const Vx *poly, int n, int shade, int ring)
{
    int i, k;
    for (i = 1; i + 1 < n; i++) {
        const Vx *t[3] = {&poly[0], &poly[i], &poly[i + 1]};
        for (k = 0; k < 3; k++) {
            float v[8], s = 1.0f;
            if (shade) { s = (ISO_SCORCH - t[k]->d) / ISO_SCORCH; s = s * s; if (s > 1.0f) s = 1.0f; if (s < 0.0f) s = 0.0f; }
            memcpy(v, t[k]->p, 12);
            if (ring) { v[3] = v[4] = v[5] = 0.0f; v[6] = t[k]->uv[0] * 5.0f; v[7] = t[k]->uv[1] * 5.0f; }     /* no normals, uv times 5 (0088bd58) */
            else { v[3] = t[k]->n[0] * s; v[4] = t[k]->n[1] * s; v[5] = t[k]->n[2] * s; v[6] = t[k]->uv[0]; v[7] = t[k]->uv[1]; }  /* the vertex colour rides in the normal's length */
            buf_push(b, v);
        }
    }
}

static void rebuild_mesh(DmgShip *d, int mi)
{
    const TgMesh *m = &d->meshes[mi]; MeshDmg *md = &d->md[mi]; int t, k;
    md->base.n = 0; md->ring.n = 0;
    for (t = 0; t < md->ntri; t++) {
        const float *v = m->v + 24 * t;
        if (md->gone[t]) continue;
        if (!md->marked[t]) { for (k = 0; k < 3; k++) buf_push(&md->base, v + 8 * k); continue; }
        {   Vx tri[3], lo[8], hi[8], ring[8], dump[8]; int nlo, nhi, nr, nd;
            for (k = 0; k < 3; k++) { memcpy(tri[k].p, v + 8 * k, 12); memcpy(tri[k].n, v + 8 * k + 3, 12); tri[k].uv[0] = v[8 * k + 6]; tri[k].uv[1] = v[8 * k + 7]; tri[k].d = field(d, tri[k].p); }
            split_poly(tri, 3, ISO_SCORCH, lo, &nlo, hi, &nhi);
            if (nlo >= 3) emit_fan(&md->base, lo, nlo, 1, 0);                               /* the hull that is left, darkening toward the cut */
            if (nhi >= 3) {
                split_poly(hi, nhi, ISO_HOLE, ring, &nr, dump, &nd);                        /* 0.5 to 0.8: the charred band; above 0.8 is the hole */
                if (nr >= 3) emit_fan(&md->ring, ring, nr, 0, 1);
            }
        }
    }
}
static void publish(DmgShip *d)
{
    int i; TgDamageGeom *g = &d->geom;
    for (i = 0; i < d->nmesh; i++) {
        g->mesh[i].base = d->md[i].base.v; g->mesh[i].nbase = d->md[i].base.n / 8; g->mesh[i].ring = d->md[i].ring.v; g->mesh[i].nring = d->md[i].ring.n / 8;
        g->mesh[i].touched = d->md[i].touched;
    }
    g->version = ++d->version;
}

static DmgShip *ship_of(void *obj, int create)
{
    DmgShip *d; const char *model; int i, n; const TgMesh *meshes; unsigned id = ((TGObject *)obj)->id;
    for (d = ships; d; d = d->next) if (d->obj_id == id && d->obj == obj) return d;
    if (create) { DmgShip *q, *nx; for (q = ships; q; q = nx) { nx = q->next; if (!tg_find(q->obj_id)) tg_damage_forget(q->obj); } }   /* the records of ships that are gone */
    if (!create || !(model = tg_space_model_of(obj)) || !(meshes = tg_model_meshes(model, &n)) || n <= 0) return NULL;
    d = calloc(1, sizeof *d); d->obj = obj; d->obj_id = id; d->model = model; d->meshes = meshes; d->nmesh = n;
    d->md = calloc((size_t)n, sizeof *d->md); d->geom.mesh = calloc((size_t)n, sizeof *d->geom.mesh); d->geom.nmesh = n;
    for (i = 0; i < n; i++) { d->md[i].ntri = meshes[i].nverts / 3; d->md[i].marked = calloc((size_t)d->md[i].ntri + 1, 1); d->md[i].gone = calloc((size_t)d->md[i].ntri + 1, 1); }
    d->vox = tg_model_voxel(model);
    if (getenv("APPC_FX_TRACE")) { float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}; int a, b, k; for (a = 0; a < n; a++) for (b = 0; b < meshes[a].nverts; b++) for (k = 0; k < 3; k++) { float x = meshes[a].v[8 * b + k]; if (x < lo[k]) lo[k] = x; if (x > hi[k]) hi[k] = x; } fprintf(stderr, "[damage] %s: %d shapes, box %.2f..%.2f %.2f..%.2f %.2f..%.2f, voxels %s\n", model, n, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], d->vox ? "yes" : "no"); }
    if (d->vox) {
        int cells = d->vox->nx * d->vox->ny * d->vox->nz; d->alive = calloc((size_t)cells, 1); d->label = calloc((size_t)cells, sizeof(int));
        for (i = 0; i < cells; i++) d->alive[i] = (d->vox->solid[i >> 3] >> (i & 7)) & 1;
    }
    d->next = ships; ships = d;
    return d;
}
const TgDamageGeom *tg_damage_geom(void *obj)
{
    DmgShip *d = obj && tg_is_ours(obj) ? ship_of(obj, 0) : NULL;
    return d ? &d->geom : NULL;
}

/* ---- the voxel body: carving, pieces, debris ---- */
/* the cell of a mesh-space point, or -1 outside the grid */
static int cell_at(const TgVoxel *v, const float *p, int *cx, int *cy, int *cz)
{
    int x = (int)floorf((p[0] - v->org[0]) / v->cell), y = (int)floorf((p[1] - v->org[1]) / v->cell), z = (int)floorf((p[2] - v->org[2]) / v->cell);
    if (x < 0 || y < 0 || z < 0 || x >= v->nx || y >= v->ny || z >= v->nz) return -1;
    if (cx) { *cx = x; *cy = y; *cz = z; }
    return x + y * v->nx + z * v->nx * v->ny;
}
static int occupied(const TgVoxel *v, const unsigned char *bits, int i) { return (bits[i >> 3] >> (i & 7)) & 1; }
static void spawn_piece(DmgShip *d, int comp, int ncells)
{
    Debris *db = calloc(1, sizeof *db); float sp[3], sf[3], su[3], sr[3], sc, vel[3] = {0, 0, 0}, life = -1.0f, cen[3] = {0, 0, 0}, cnt = 0, dir[3], dl, radius; int mi, t, k, np = 0;
    const TgVoxel *vx = d->vox;
    tg_space_pose(d->obj, sp, sf, su, sr, &sc); if (sc <= 0) sc = 1;
    tg_ship_motion(d->obj, vel, &life);
    db->parts = calloc((size_t)d->nmesh, sizeof *db->parts);
    /* the piece's triangles: the centroid's cell (or the nearest occupied cell around it) belongs to the piece */
    for (mi = 0; mi < d->nmesh; mi++) {
        const TgMesh *m = &d->meshes[mi]; MeshDmg *md = &d->md[mi]; float *vs = NULL; int nv = 0, cap = 0;
        for (t = 0; t < md->ntri; t++) {
            const float *v = m->v + 24 * t; float c[3]; int cell, cx, cy, cz, dx, dy, dz, found = -1;
            if (md->gone[t]) continue;
            for (k = 0; k < 3; k++) c[k] = (v[k] + v[8 + k] + v[16 + k]) / 3.0f;
            cell = cell_at(vx, c, &cx, &cy, &cz);
            if (cell >= 0 && d->label[cell] > 0) found = d->label[cell];
            else if (cell >= 0) {                                      /* a triangle in an empty cell: the nearest labelled cell within two cells */
                float best = 1e30f;
                for (dz = -2; dz <= 2; dz++) for (dy = -2; dy <= 2; dy++) for (dx = -2; dx <= 2; dx++) {
                    int x = cx + dx, y = cy + dy, z = cz + dz, j, dd = dx * dx + dy * dy + dz * dz;
                    if (x < 0 || y < 0 || z < 0 || x >= vx->nx || y >= vx->ny || z >= vx->nz) continue;
                    j = x + y * vx->nx + z * vx->nx * vx->ny;
                    if (d->label[j] > 0 && (float)dd < best) { best = (float)dd; found = d->label[j]; }
                }
            }
            if (found != comp) continue;
            md->gone[t] = 1; md->touched = 1;
            if (nv + 24 > cap) { cap = cap ? cap * 2 : 3000; vs = realloc(vs, sizeof(float) * (size_t)cap); }
            memcpy(vs + nv, v, 96); nv += 24;
            for (k = 0; k < 3; k++) { cen[k] += c[k]; } cnt += 1.0f;
        }
        if (nv) { db->parts[np].mesh = mi; db->parts[np].v = vs; db->parts[np].nv = nv / 8; np++; }
    }
    db->nparts = np;
    if (np == 0) { free(db->parts); free(db); return; }
    for (k = 0; k < 3; k++) cen[k] /= cnt;
    for (mi = 0; mi < np; mi++) { float *vs = db->parts[mi].v; int i; for (i = 0; i < db->parts[mi].nv; i++) for (k = 0; k < 3; k++) vs[8 * i + k] -= cen[k]; }   /* the piece's own origin is its centre */
    {   float rad2 = 0.0f; for (mi = 0; mi < np; mi++) { const float *vs = db->parts[mi].v; int i; for (i = 0; i < db->parts[mi].nv; i++) { float q = vs[8 * i] * vs[8 * i] + vs[8 * i + 1] * vs[8 * i + 1] + vs[8 * i + 2] * vs[8 * i + 2]; if (q > rad2) rad2 = q; } }
        radius = sqrtf(rad2) * sc; }
    /* where the piece starts, and how it is thrown (SpawnDamageDebris 00591ee0): from the ship's centre through the piece's, plus a random vector of the piece's
       radius (each part a random number in -1 to 1), as a force of 0.2 times the piece's mass (DAT_0088bea4) on a free body: the speed it gains is 0.2 a second
       (the exe's ApplyForce is not decoded further); on top of the ship's own velocity */
    for (k = 0; k < 3; k++) { db->p[k] = sp[k] + (cen[0] * sr[k] + cen[1] * sf[k] + cen[2] * su[k]) * sc; dir[k] = db->p[k] - sp[k] + (frand() * 2.0f - 1.0f) * radius; }
    dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]); if (dl < 1e-6f) { dir[0] = 0; dir[1] = 0; dir[2] = 1; dl = 1; }
    for (k = 0; k < 3; k++) db->v[k] = vel[k] + dir[k] / dl * 0.2f;
    db->ax[0] = frand() * 2 - 1; db->ax[1] = frand() * 2 - 1; db->ax[2] = frand() * 2 - 1;
    dl = sqrtf(db->ax[0] * db->ax[0] + db->ax[1] * db->ax[1] + db->ax[2] * db->ax[2]); if (dl < 1e-4f) { db->ax[2] = 1; dl = 1; }
    for (k = 0; k < 3; k++) db->ax[k] /= dl;
    db->w = (frand() - 0.5f) * 0.8f;
    memcpy(db->f, sf, 12); memcpy(db->u, su, 12); memcpy(db->r, sr, 12); db->scale = sc; db->model = d->model;
    db->t0 = (float)tg_game_time; db->life = life > 0.0f ? life : 120.0f; db->id = debris_id++;
    db->next = debris_list; debris_list = db;
    if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[damage] debris %u: %d cells, %d parts\n", db->id, ncells, np);
}
static void update_body(DmgShip *d)
{
    const TgVoxel *vx = d->vox; int cells, i, ncomp = 0, *stack, sp, best = 0, bestn = 0, *cnt; int nb[6];
    if (!vx || !tg_dmg_flag(1) || !tg_dmg_flag(2)) return;
    cells = vx->nx * vx->ny * vx->nz;
    for (i = 0; i < d->nvols; i++) {                                    /* carve: cells whose centre is in the hole (field >= 0.8) */
        const Vol *v = &d->vols[i]; int x0, x1, y0, y1, z0, z1, x, y, z;
        x0 = (int)floorf((v->c[0] - v->r - vx->org[0]) / vx->cell); x1 = (int)floorf((v->c[0] + v->r - vx->org[0]) / vx->cell);
        y0 = (int)floorf((v->c[1] - v->r - vx->org[1]) / vx->cell); y1 = (int)floorf((v->c[1] + v->r - vx->org[1]) / vx->cell);
        z0 = (int)floorf((v->c[2] - v->r - vx->org[2]) / vx->cell); z1 = (int)floorf((v->c[2] + v->r - vx->org[2]) / vx->cell);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (z0 < 0) z0 = 0; if (x1 >= vx->nx) x1 = vx->nx - 1; if (y1 >= vx->ny) y1 = vx->ny - 1; if (z1 >= vx->nz) z1 = vx->nz - 1;
        for (z = z0; z <= z1; z++) for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) {
            int c = x + y * vx->nx + z * vx->nx * vx->ny; float p[3];
            if (!d->alive[c]) continue;
            p[0] = vx->org[0] + (x + 0.5f) * vx->cell; p[1] = vx->org[1] + (y + 0.5f) * vx->cell; p[2] = vx->org[2] + (z + 0.5f) * vx->cell;
            if (field(d, p) >= ISO_HOLE) d->alive[c] = 0;
        }
    }
    stack = malloc(sizeof(int) * (size_t)cells); cnt = calloc((size_t)cells + 1, sizeof(int));
    memset(d->label, 0, sizeof(int) * (size_t)cells);
    nb[0] = 1; nb[1] = -1; nb[2] = vx->nx; nb[3] = -vx->nx; nb[4] = vx->nx * vx->ny; nb[5] = -vx->nx * vx->ny;
    for (i = 0; i < cells; i++) {
        if (!d->alive[i] || d->label[i]) continue;
        ncomp++; sp = 0; stack[sp++] = i; d->label[i] = ncomp;
        while (sp) {
            int c = stack[--sp], x = c % vx->nx, y = (c / vx->nx) % vx->ny, z = c / (vx->nx * vx->ny), k; cnt[ncomp]++;
            for (k = 0; k < 6; k++) {
                int j = c + nb[k];
                if ((k == 0 && x + 1 >= vx->nx) || (k == 1 && x == 0) || (k == 2 && y + 1 >= vx->ny) || (k == 3 && y == 0) || (k == 4 && z + 1 >= vx->nz) || (k == 5 && z == 0)) continue;
                if (d->alive[j] && !d->label[j]) { d->label[j] = ncomp; stack[sp++] = j; }
            }
        }
    }
    for (i = 1; i <= ncomp; i++) if (cnt[i] > bestn) { bestn = cnt[i]; best = i; }
    for (i = 1; i <= ncomp; i++) {
        int c;
        if (i == best || cnt[i] < MIN_CELLS) continue;
        spawn_piece(d, i, cnt[i]);
        for (c = 0; c < cells; c++) if (d->label[c] == i) d->alive[c] = 0;          /* gone from the ship for good */
    }
    free(stack); free(cnt);
}

/* ---- the entry points ---- */
/* a damage volume in the object's mesh space (centre, radius), strength = the damage (AddDamageVolume 00594020) */
void tg_damage_add_volume(void *obj, const float *centre, float radius, float strength)
{
    DmgShip *d; int i, mi, t, changed = 0; Vol v;
    if (!obj || radius <= 0.0f || !tg_dmg_flag(0)) return;
    if (!(d = ship_of(obj, 1))) return;
    memcpy(v.c, centre, 12); v.r = radius; v.r2 = radius * radius; v.s = strength;
    for (i = 0; i < d->nvols; i++) {                                    /* a repeat at the same spot adds its strength (the beams add a volume every tick) */
        Vol *o = &d->vols[i]; float dx = o->c[0] - v.c[0], dy = o->c[1] - v.c[1], dz = o->c[2] - v.c[2];
        if (dx * dx + dy * dy + dz * dz < 0.0625f * v.r2 && fabsf(o->r - v.r) < 0.2f * v.r) { o->s += v.s; goto added; }
    }
    if (d->nvols >= MAX_VOLS) return;
    if (d->nvols == d->cap) { d->cap = d->cap ? d->cap * 2 : 16; d->vols = realloc(d->vols, sizeof(Vol) * (size_t)d->cap); }
    d->vols[d->nvols++] = v;
added:
    for (mi = 0; mi < d->nmesh; mi++) {
        const TgMesh *m = &d->meshes[mi]; MeshDmg *md = &d->md[mi]; int mine = 0;
        for (t = 0; t < md->ntri; t++) {
            const float *tv = m->v + 24 * t;
            if (md->marked[t] || md->gone[t]) continue;
            if (closest_d2(v.c, tv, tv + 8, tv + 16) <= v.r2) { md->marked[t] = 1; mine = 1; }
        }
        if (mine) { md->touched = 1; changed = 1; }
    }
    if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[damage] volume at %.2f %.2f %.2f r %.3f strength %.1f (%d volumes, marked now %d)\n", v.c[0], v.c[1], v.c[2], v.r, v.s, d->nvols, d->md[0].ntri ? (int)(changed) : 0);
    update_body(d);                                                     /* may throw pieces off (their triangles go) */
    for (mi = 0; mi < d->nmesh; mi++) if (d->md[mi].touched) { rebuild_mesh(d, mi); changed = 1; }
    if (changed) publish(d);
}
void tg_damage_forget(void *obj)
{
    DmgShip **pp, *d; int i;
    for (pp = &ships; (d = *pp); pp = &d->next) if (d->obj == obj) {
        *pp = d->next;
        for (i = 0; i < d->nmesh; i++) { free(d->md[i].marked); free(d->md[i].gone); free(d->md[i].base.v); free(d->md[i].ring.v); }
        free(d->md); free(d->geom.mesh); free(d->vols); free(d->alive); free(d->label); free(d);
        return;
    }
}

/* debris of a set, posed for the current game time: flies straight and turns about its axis; gone when its life is over */
int tg_fx_debris(void *set, TgFxDebris *out, int max)
{
    Debris *d, **pp = &debris_list; int n = 0; float now = (float)tg_game_time;
    extern void *eng_BaseObjectClass_GetContainingSet(void *);
    while ((d = *pp)) {
        float age = now - d->t0;
        if (age > d->life || age < 0.0f) { *pp = d->next; { int i; for (i = 0; i < d->nparts; i++) free(d->parts[i].v); } free(d->parts); free(d); continue; }
        pp = &d->next;
    }
    (void)set;
    for (d = debris_list; d && n < max; d = d->next) {
        TgFxDebris *o = &out[n]; float age = now - d->t0, a = d->w * age, c = cosf(a), s = sinf(a), *ax = d->ax; int k, i;
        float R[9] = {c + ax[0] * ax[0] * (1 - c), ax[0] * ax[1] * (1 - c) - ax[2] * s, ax[0] * ax[2] * (1 - c) + ax[1] * s,
                      ax[1] * ax[0] * (1 - c) + ax[2] * s, c + ax[1] * ax[1] * (1 - c), ax[1] * ax[2] * (1 - c) - ax[0] * s,
                      ax[2] * ax[0] * (1 - c) - ax[1] * s, ax[2] * ax[1] * (1 - c) + ax[0] * s, c + ax[2] * ax[2] * (1 - c)};
        /* the piece's axes: the ship's axes at the throw, turned about the piece's own axis (rotating the axes: new_i = R * old_i) */
        const float *src[3] = {d->r, d->f, d->u}; float *dst[3] = {o->r, o->f, o->u};
        for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) dst[i][k] = R[3 * k] * src[i][0] + R[3 * k + 1] * src[i][1] + R[3 * k + 2] * src[i][2];
        for (k = 0; k < 3; k++) o->p[k] = d->p[k] + d->v[k] * age;
        o->scale = d->scale; o->id = d->id; o->model = d->model; o->nparts = d->nparts; o->fade = age > d->life - 1.0f ? (d->life - age) : 1.0f;
        for (i = 0; i < d->nparts && i < TG_FX_DEBRIS_PARTS; i++) { o->parts[i].mesh = d->parts[i].mesh; o->parts[i].v = d->parts[i].v; o->parts[i].nv = d->parts[i].nv; }
        if (o->nparts > TG_FX_DEBRIS_PARTS) o->nparts = TG_FX_DEBRIS_PARTS;
        n++;
    }
    return n;
}
