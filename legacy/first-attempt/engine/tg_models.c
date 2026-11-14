/* Model data for the game layer: bounding radius of a model, loaded with the NIF reader (nif.c) and kept in a cache.
   A model is named by a .nif path (placements, bridge, viewscreen; no scale known yet) or by an LOD model name (ships: LODModelManager record,
   first LOD file, times the record's scale). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "nif.h"

#include <stdio.h>
#include "tg_models.h"

const char *tg_asset_resolve(const char *path); int tg_lod_files(const char *name, const char **out, int max); float tg_lod_scale(const char *name);
const char *tg_lod_share_path(const char *name); void tg_lod_map_names(const char *name, const char **glow, const char **filter, const char **spec);
typedef struct { char *name; float radius, center[3]; int ok; float *tris; int ntris; TgMesh *meshes; int nmeshes; TgVoxel vox; int vox_tried; } MRec;   /* tris: model space, for LineCollides */

/* The real path of a texture that a NIF names. The original looks in the model's folder and the LOD model's texture share path
   (SetTextureSharePath, e.g. data/Models/SharedTextures/FedShips), each with the detail folder (/High, /Medium, /Low strings next to
   "data/Models/SharedTextures" at 0088b6f4 in stbc.exe). Order used here: model folder/High, model folder, share/High, share, the name
   as stored (when it has a folder), then the same folders with the other extensions (.dds, .tga, .bmp) for packs that converted their
   textures. Approximation: always the High detail (the texture detail option is not read); the exact order of the original search
   is not decoded. Every part of the path matches without regard to case (tg_asset_resolve). */
static char *texture_path(const char *nif, const char *share, const char *name)
{
    static const char *ext[] = {".dds", ".tga", ".bmp"};
    char dir[512], base[256], stem[256], q[1024]; const char *s, *r, *dirs[4]; int nd = 0, i, e, has_dir; size_t k;
    for (s = r = name; *s; s++) if (*s == '/' || *s == '\\') r = s + 1;
    snprintf(base, sizeof base, "%s", r); has_dir = r != name;
    k = 0; for (s = nif; *s; s++) if (*s == '/' || *s == '\\') k = (size_t)(s - nif);
    snprintf(dir, sizeof dir, "%.*s", (int)k, nif);
    snprintf(stem, sizeof stem, "%s", base); { char *dot = strrchr(stem, '.'); if (dot) *dot = 0; }
    dirs[nd++] = dir; if (share && *share) dirs[nd++] = share;
    for (i = 0; i < nd; i++) {
        snprintf(q, sizeof q, "%s/High/%s", dirs[i], base); if ((r = tg_asset_resolve(q))) return strdup(r);
        snprintf(q, sizeof q, "%s/%s", dirs[i], base); if ((r = tg_asset_resolve(q))) return strdup(r);
    }
    if (has_dir && (r = tg_asset_resolve(name))) return strdup(r);
    for (i = 0; i < nd; i++) for (e = 0; e < 3; e++) {
        snprintf(q, sizeof q, "%s/High/%s%s", dirs[i], stem, ext[e]); if ((r = tg_asset_resolve(q))) return strdup(r);
        snprintf(q, sizeof q, "%s/%s%s", dirs[i], stem, ext[e]); if ((r = tg_asset_resolve(q))) return strdup(r);
    }
    return NULL;
}
static MRec *recs; static int nrecs;
static MRec *model(const char *name)
{
    int i, j; const char *path = name, *lod[8], *share = NULL; char err[256], rel[1024]; NifFile *f; MRec *m;
    for (i = 0; i < nrecs; i++) if (!strcasecmp(recs[i].name, name)) return &recs[i];
    recs = realloc(recs, (nrecs + 1) * sizeof *recs); m = &recs[nrecs++]; memset(m, 0, sizeof *m); m->name = strdup(name);
    float scale = 1.0f;
    if (!strcasestr(name, ".nif") && tg_lod_files(name, lod, 8) > 0 && lod[0]) { path = lod[0]; scale = tg_lod_scale(name); share = tg_lod_share_path(name); }   /* LOD models carry a scale (0.01 by default) */
    snprintf(rel, sizeof rel, "%s", path);
    if ((path = tg_asset_resolve(path)) && (f = nif_load(path, err, sizeof err))) { m->radius = nif_radius(f, m->center) * scale; m->center[0] *= scale; m->center[1] *= scale; m->center[2] *= scale; m->ok = 1;
        m->tris = nif_triangles(f, &m->ntris); for (i = 0; i < m->ntris * 9; i++) m->tris[i] *= scale;
        {   NifMesh *nm = nif_meshes(f, &m->nmeshes);
            m->meshes = calloc(m->nmeshes ? m->nmeshes : 1, sizeof *m->meshes);
            for (i = 0; i < m->nmeshes; i++) {
                TgMesh *t = &m->meshes[i];
                t->v = nm[i].v; nm[i].v = NULL; t->nverts = nm[i].nverts;
                for (j = 0; j < t->nverts; j++) { t->v[8 * j] *= scale; t->v[8 * j + 1] *= scale; t->v[8 * j + 2] *= scale; }
                t->texture = nm[i].texture ? texture_path(rel, share, nm[i].texture) : NULL;
                /* The maps, as the exe sets them up when the model loads:
                   - slot 2 (gloss): LODEntry loader FUN_00452f50 gives every textured shape (whose texture name holds the LOD's filter, if any) a gloss map: the
                     picture "<name without glow suffix><specular suffix>.<ext>" when the script gave a specular suffix and the file loads, else the shape's own
                     base picture. SetSpecularKs scales it per ship.
                   - slot 4 (glow alpha): UpdateGlowMaps (applyGlowMapRecursive_inst2 0045c290) sets the shape's own base picture there when its texture name holds
                     the LOD's glow suffix and glow maps are on (DisableGlowAlphaMaps removes it).
                   - slots 2 to 4 that the NIF stores itself (NiMultiTextureProperty) are kept as they are. */
                {   const char *gs, *fl, *sp; tg_lod_map_names(name, &gs, &fl, &sp);
                    t->glow = nm[i].glow ? texture_path(rel, share, nm[i].glow) : NULL;
                    t->gloss = nm[i].gloss ? texture_path(rel, share, nm[i].gloss) : NULL;
                    t->glowa = nm[i].glowa ? texture_path(rel, share, nm[i].glowa) : NULL;
                    if (nm[i].texture && t->texture && (!fl || strcasestr(nm[i].texture, fl))) {
                        if (!t->gloss && sp) {
                            char stem[256], q[300], *dot, *at; snprintf(stem, sizeof stem, "%s", nm[i].texture); dot = strrchr(stem, '.'); if (dot) *dot = 0;
                            if (gs && (at = strcasestr(stem, gs))) *at = 0;
                            snprintf(q, sizeof q, "%s%s%s", stem, sp, dot ? nm[i].texture + (dot - stem) : "");
                            t->gloss = texture_path(rel, share, q);
                        }
                        if (!t->gloss) t->gloss = strdup(t->texture);
                    }
                    if (!t->glowa && gs && nm[i].texture && t->texture && strcasestr(nm[i].texture, gs)) t->glowa = strdup(t->texture);
                }
                if (nm[i].texture && !t->texture && getenv("APPC_TRACE")) fprintf(stderr, "[Appc] missing texture %s (model %s)\n", nm[i].texture, rel);
                memcpy(t->diffuse, nm[i].diffuse, sizeof t->diffuse); memcpy(t->emissive, nm[i].emissive, sizeof t->emissive); t->alpha = nm[i].alpha; memcpy(t->specular, nm[i].specular, sizeof t->specular); t->shininess = nm[i].shininess;
            }
            nif_meshes_free(nm, m->nmeshes);
        }
        nif_free(f); }
    return m;
}
/* the voxel file of a model: the first LOD file with "_vox" before the extension; its solid grid (the exe keeps it as NiBinaryVoxelData next to the model) */
const TgVoxel *tg_model_voxel(const char *name)
{
    MRec *m = name && *name ? model(name) : NULL; const char *lod[8], *path; char rel[1024], err[256]; NifFile *f; const NifBlock *vb; char *dot; int i, bit; float scale;
    if (!m || !m->ok) return NULL;
    if (m->vox_tried) return m->vox.solid ? &m->vox : NULL;
    m->vox_tried = 1;
    if (strcasestr(name, ".nif") || tg_lod_files(name, lod, 8) <= 0 || !lod[0]) return NULL;
    snprintf(rel, sizeof rel, "%s", lod[0]); dot = strrchr(rel, '.'); if (!dot) return NULL;
    snprintf(dot, sizeof rel - (size_t)(dot - rel), "_vox%s", strrchr(lod[0], '.'));
    if (!(path = tg_asset_resolve(rel)) || !(f = nif_load(path, err, sizeof err))) return NULL;
    if (!(vb = nif_voxel(f))) { nif_free(f); return NULL; }
    scale = tg_lod_scale(name);
    m->vox.nx = vb->vox_dim[0] - 1; m->vox.ny = vb->vox_dim[1] - 1; m->vox.nz = vb->vox_dim[2] - 1; m->vox.cell = vb->vox_cell * scale;
    for (i = 0; i < 3; i++) m->vox.org[i] = vb->vox_org[i] * scale;
    {   unsigned char *u = calloc((size_t)vb->vox_bytes, 1); int g, q; for (g = 0; g < 7; g++) for (q = 0; q < vb->vox_bytes; q++) u[q] |= vb->vox_bits[g][q]; m->vox.solid = u; }   /* the union of the seven grids: the whole body (grid 0 is its core) */
    for (i = 0, bit = 0; i < m->vox.nx * m->vox.ny * m->vox.nz; i++) if (m->vox.solid[i >> 3] >> (i & 7) & 1) bit++;
    m->vox.nsolid = bit;
    nif_free(f);
    return &m->vox;
}
/* nearest hit of a segment with the model triangles (Moller-Trumbore) */
int tg_model_ray_first(const char *name, const float *a, const float *b, float *tout, float *normal)
{
    MRec *m = name && *name ? model(name) : NULL; float d[3], best = 2.0f; int i, k, found = 0;
    if (!m || !m->ok || !m->tris) return 0;
    for (k = 0; k < 3; k++) d[k] = b[k] - a[k];
    for (i = 0; i < m->ntris; i++) {
        const float *v0 = m->tris + 9 * i, *v1 = v0 + 3, *v2 = v0 + 6; float e1[3], e2[3], p[3], q[3], s[3], det, u, v, t;
        for (k = 0; k < 3; k++) { e1[k] = v1[k] - v0[k]; e2[k] = v2[k] - v0[k]; s[k] = a[k] - v0[k]; }
        p[0] = d[1] * e2[2] - d[2] * e2[1]; p[1] = d[2] * e2[0] - d[0] * e2[2]; p[2] = d[0] * e2[1] - d[1] * e2[0];
        det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (det > -1e-12f && det < 1e-12f) continue;
        u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det; if (u < 0.0f || u > 1.0f) continue;
        q[0] = s[1] * e1[2] - s[2] * e1[1]; q[1] = s[2] * e1[0] - s[0] * e1[2]; q[2] = s[0] * e1[1] - s[1] * e1[0];
        v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det; if (v < 0.0f || u + v > 1.0f) continue;
        t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
        if (t >= 0.0f && t <= 1.0f && t < best) {
            float nn[3], l; best = t; found = 1;
            nn[0] = e1[1] * e2[2] - e1[2] * e2[1]; nn[1] = e1[2] * e2[0] - e1[0] * e2[2]; nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
            l = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]); if (l < 1e-12f) l = 1;
            if (nn[0] * d[0] + nn[1] * d[1] + nn[2] * d[2] > 0) l = -l;           /* the normal faces the segment's start */
            for (k = 0; k < 3; k++) normal[k] = nn[k] / l;
        }
    }
    if (found && tout) *tout = best;
    return found;
}
/* the model's bounding radius in model units; 0 when the model is not known or not found */
double tg_model_radius(const char *name) { MRec *m = name && *name ? model(name) : NULL; return m && m->ok ? m->radius : 0.0; }


/* the model's axis-aligned box (model space, scaled): centre and half extents; 0 when the model is not known. The shield effect's ellipsoid
   is this box's extents times sqrt(3) (ShipClass FUN_005abac0: the bound of the model node, times sqrt(3.0), times the node scales). */
int tg_model_box(const char *name, float centre[3], float half[3])
{
    MRec *m = name && *name ? model(name) : NULL; int i, j, k; float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    if (!m || !m->ok) return 0;
    for (i = 0; i < m->nmeshes; i++) for (j = 0; j < m->meshes[i].nverts; j++) for (k = 0; k < 3; k++) {
        float v = m->meshes[i].v[8 * j + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
    if (lo[0] > hi[0]) return 0;
    for (k = 0; k < 3; k++) { centre[k] = 0.5f * (lo[k] + hi[k]); half[k] = 0.5f * (hi[k] - lo[k]); }
    return 1;
}
/* a random vertex of the model with its normal (ObjectClass::GetRandomPointOnModel 004356a0 picks one: pickRandomModelVertex); 0 when unknown */
int tg_model_random_vertex(const char *name, float pos[3], float nrm[3])
{
    MRec *m = name && *name ? model(name) : NULL; long total = 0, pick; int i, j;
    if (!m || !m->ok) return 0;
    for (i = 0; i < m->nmeshes; i++) total += m->meshes[i].nverts;
    if (total <= 0) return 0;
    pick = rand() % total;
    for (i = 0; i < m->nmeshes; i++) {
        if (pick < m->meshes[i].nverts) { const float *v = m->meshes[i].v + 8 * pick; for (j = 0; j < 3; j++) { pos[j] = v[j]; nrm[j] = v[3 + j]; } return 1; }
        pick -= m->meshes[i].nverts;
    }
    return 0;
}

/* segment a-b (model space) against the model's triangles (Moller-Trumbore); 1 when it hits. NiPick in ObjectClass::LineCollides 00435ec0. */
int tg_model_line_hit(const char *name, const float *a, const float *b)
{
    MRec *m = name && *name ? model(name) : NULL; float d[3]; int i, k;
    if (!m || !m->ok || !m->tris) return 0;
    for (k = 0; k < 3; k++) d[k] = b[k] - a[k];
    for (i = 0; i < m->ntris; i++) {
        const float *v0 = m->tris + 9 * i, *v1 = v0 + 3, *v2 = v0 + 6; float e1[3], e2[3], p[3], q[3], s[3], det, u, v, t;
        for (k = 0; k < 3; k++) { e1[k] = v1[k] - v0[k]; e2[k] = v2[k] - v0[k]; s[k] = a[k] - v0[k]; }
        p[0] = d[1] * e2[2] - d[2] * e2[1]; p[1] = d[2] * e2[0] - d[0] * e2[2]; p[2] = d[0] * e2[1] - d[1] * e2[0];
        det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (det > -1e-9f && det < 1e-9f) continue;
        u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det; if (u < 0.0f || u > 1.0f) continue;
        q[0] = s[1] * e1[2] - s[2] * e1[1]; q[1] = s[2] * e1[0] - s[0] * e1[2]; q[2] = s[0] * e1[1] - s[1] * e1[0];
        v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det; if (v < 0.0f || u + v > 1.0f) continue;
        t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
        if (t >= 0.0f && t <= 1.0f) return 1;
    }
    return 0;
}

/* model-space triangles (9 floats each, scaled like the collision data) for the renderer; NULL when the model did not load */
const float *tg_model_tris(const char *name, int *n)
{
    MRec *m = name && *name ? model(name) : NULL;
    if (n) *n = 0;
    if (!m || !m->ok || !m->tris) return NULL;
    if (n) *n = m->ntris;
    return m->tris;
}

/* the model's drawable shapes (model space, scaled like tg_model_tris; textures as real paths) for the renderer; NULL when it did not load */
const TgMesh *tg_model_meshes(const char *name, int *n)
{
    MRec *m = name && *name ? model(name) : NULL;
    if (n) *n = 0;
    if (!m || !m->ok) return NULL;
    if (n) *n = m->nmeshes;
    return m->meshes;
}
