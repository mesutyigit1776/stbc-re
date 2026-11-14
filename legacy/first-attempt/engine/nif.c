/* NetImmerse 3.x (.nif) reader. See nif.h. Layout sources (stbc.exe): NiObject 007d8820, NiObjectNET 007db5f0, NiAVObject 007dd480,
   NiNode, NiGeometry 007ee6c0, NiGeometryData 007ed1a0, NiTriBasedGeomData 007eed10, NiTriShapeData 007f1930, NiProperty 007dbcd0,
   NiTextureProperty 0080a480, NiTextureModeProperty, NiMaterialProperty 00800ba0, NiVertexColorProperty 0080fb00, NiAlphaProperty 007ec4c0,
   NiImage 007e16b0, NiRawImageData 007e0400, NiExtraData 007d9510, NiStringExtraData 00808660, NiBinaryVoxelExtraData 004ac200,
   NiBinaryVoxelData 004a5890, NiTimeController 007da460, NiKeyframeController 00793370, NiKeyframeData 00792320, NiFlipController 00794030,
   NiVisController 007b6870, NiSkinController, NiDynamicEffect 007e2540, NiLight 007f38f0, NiPointLight 00803c40, NiSpotLight 00807070,
   NiMultiTextureProperty 008029d0. Version checks compare the version strings like the exe ("3.04", "3.05").
   From the files only (marked): NiCamera tail, NiLookAtController, NiRollController, NiVisData, NiFloatData key layout, multi-texture slot tail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "nif.h"

typedef struct { const unsigned char *b; long n, o; int bad; const char *ver; } R;
static unsigned u8(R *r) { if (r->o + 1 > r->n) { r->bad = 1; return 0; } return r->b[r->o++]; }
static unsigned u16(R *r) { unsigned v; if (r->o + 2 > r->n) { r->bad = 1; return 0; } v = r->b[r->o] | r->b[r->o + 1] << 8; r->o += 2; return v; }
static unsigned u32(R *r) { unsigned v; if (r->o + 4 > r->n) { r->bad = 1; return 0; } memcpy(&v, r->b + r->o, 4); r->o += 4; return v; }
static float f32(R *r) { float v; if (r->o + 4 > r->n) { r->bad = 1; return 0; } memcpy(&v, r->b + r->o, 4); r->o += 4; return v; }
static void skip(R *r, long k) { if (k < 0 || r->o + k > r->n) { r->bad = 1; r->o = r->n; return; } r->o += k; }
static void floats(R *r, float *out, int k) { int i; for (i = 0; i < k; i++) out[i] = f32(r); }
static char *str(R *r)
{
    unsigned k = u32(r); char *s;
    if (k > 4096 || r->o + k > r->n) { r->bad = 1; return NULL; }
    s = malloc(k + 1); memcpy(s, r->b + r->o, k); s[k] = 0; r->o += k; return s;
}
static unsigned *links(R *r, int *n) { unsigned k = u32(r), i, *v; if (k > 1000000) { r->bad = 1; *n = 0; return NULL; } v = k ? malloc(k * 4) : NULL; for (i = 0; i < k; i++) v[i] = u32(r); *n = (int)k; return v; }
static int ver_ge(R *r, const char *v) { return strcmp(r->ver, v) >= 0; }

static void obj(R *r, NifBlock *b) { b->link = u32(r); }
static void objnet(R *r, NifBlock *b) { obj(r, b); b->name = str(r); b->extra = u32(r); b->ctrl = u32(r); }
static void avobj(R *r, NifBlock *b)
{
    objnet(r, b); b->flags = (unsigned short)u16(r); floats(r, b->t, 3); floats(r, b->rot, 9); b->scale = f32(r); skip(r, 12);   /* velocity */
    b->props = links(r, &b->nprops);
    if (u32(r)) r->bad = 1;                                   /* a bounding volume (FUN_008220f0); no game or mod file has one */
}
static void node(R *r, NifBlock *b) { int ne; unsigned *e; avobj(r, b); b->kids = links(r, &b->nkids); e = links(r, &ne); free(e); }   /* children, effects */
static void prop(R *r, NifBlock *b) { objnet(r, b); b->flags = (unsigned short)u16(r); }
static void timectrl(R *r, NifBlock *b) { obj(r, b); b->next = u32(r); u16(r); skip(r, 16); b->target = u32(r); }   /* flags, frequency, phase, start, stop */
static void extra(R *r, NifBlock *b, int raw) { unsigned k; obj(r, b); b->next = u32(r); k = u32(r); if (raw && k) skip(r, k); }
static void geomdata(R *r, NifBlock *b)
{
    int n; obj(r, b); n = b->nverts = (int)u16(r);
    if (u32(r)) { b->verts = malloc(12 * (n ? n : 1)); floats(r, b->verts, 3 * n); }
    if (u32(r)) { b->normals = malloc(12 * (n ? n : 1)); floats(r, b->normals, 3 * n); }
    floats(r, b->bound, 4);
    if (u32(r)) { b->colors = malloc(16 * (n ? n : 1)); floats(r, b->colors, 4 * n); }
    b->nuvsets = (int)u16(r);
    if (u32(r)) { b->uv = malloc(8 * (n * b->nuvsets ? n * b->nuvsets : 1)); floats(r, b->uv, 2 * n * b->nuvsets); }
}
static void trishapedata(R *r, NifBlock *b)
{
    unsigned k, i, g; geomdata(r, b); b->ntris = (int)u16(r); k = u32(r);
    if (k > 3000000) { r->bad = 1; return; }
    b->tris = malloc(2 * (k ? k : 1)); for (i = 0; i < k; i++) b->tris[i] = (unsigned short)u16(r);
    if (k != (unsigned)b->ntris * 3) b->ntris = (int)(k / 3);
    if (strcmp(r->ver, "3.05") > 0) for (g = u16(r); g; g--) skip(r, 2 * (long)u16(r));    /* match groups */
}
static int key_size(int kind, unsigned t) { static const int s[3][4] = {{0, 20, 20, 32}, {0, 16, 40, 28}, {0, 8, 16, 20}}; return t >= 1 && t <= 3 ? s[kind][t] : 0; }
static void keys(R *r, int kind)                              /* kind 0 rotation, 1 vector, 2 float: count, type, keys (time, value, tangents/TBC) */
{
    unsigned n = u32(r), t; int i;
    if (!n) return;
    t = u32(r);
    if (kind == 0 && t == 4) { for (i = 0; i < 3; i++) keys(r, 2); return; }        /* XYZ rotation: three float groups */
    if (!key_size(kind, t)) { r->bad = 1; return; }
    skip(r, (long)n * key_size(kind, t));
}
static void image(R *r, NifBlock *b)
{
    obj(r, b);
    if (u8(r)) b->file = str(r); else b->data = u32(r);       /* external file name, or a NiRawImageData link */
    u32(r);                                                    /* pixel layout */
    if (ver_ge(r, "3.04")) u32(r);
}
static void dyneffect(R *r, NifBlock *b) { int n; unsigned *v; avobj(r, b); v = links(r, &n); free(v); }
static void light(R *r, NifBlock *b) { dyneffect(r, b); skip(r, 4 + 36); }   /* dimmer, ambient, diffuse, specular */

static int read_block(R *r, NifBlock *b)
{
    const char *t = b->type;
    if (!strcmp(t, "NiNode") || !strcmp(t, "NiBone")) node(r, b);
    else if (!strcmp(t, "NiTriShape")) { avobj(r, b); b->data = u32(r); }
    else if (!strcmp(t, "NiTriShapeData")) trishapedata(r, b);
    else if (!strcmp(t, "NiZBufferProperty")) prop(r, b);
    else if (!strcmp(t, "NiVertexColorProperty")) { prop(r, b); skip(r, 8); }
    else if (!strcmp(t, "NiMaterialProperty")) { prop(r, b); floats(r, b->mat, 14); }
    else if (!strcmp(t, "NiTextureModeProperty")) { prop(r, b); if (ver_ge(r, "3.04")) skip(r, 4); }
    else if (!strcmp(t, "NiAlphaProperty")) { prop(r, b); u8(r); }
    else if (!strcmp(t, "NiTextureProperty")) { prop(r, b); b->data = u32(r); if (!ver_ge(r, "3.04")) skip(r, 8); }
    else if (!strcmp(t, "NiMultiTextureProperty")) {
        int i; prop(r, b); b->mt_count = u32(r);
        for (i = 0; i < 5; i++) {
            unsigned img = u32(r); b->mt_slot[i] = img;
            if (img) { b->mt_u[i][0] = u32(r); if (!b->data) b->data = b->mt_u[i][0]; b->mt_u[i][1] = u32(r); b->mt_u[i][2] = u32(r); u32(r);
                       if (ver_ge(r, "3.04")) { b->mt_s[i][0] = u16(r); b->mt_s[i][1] = u16(r); b->mt_b[i][0] = u8(r); b->mt_b[i][1] = u8(r); } } }
    }
    else if (!strcmp(t, "NiImage")) image(r, b);
    else if (!strcmp(t, "NiRawImageData")) { unsigned w, h, k; obj(r, b); w = u32(r); h = u32(r); k = u32(r); skip(r, (long)w * h * (k == 2 ? 4 : 3)); }
    else if (!strcmp(t, "NiExtraData")) extra(r, b, 1);
    else if (!strcmp(t, "NiStringExtraData")) { extra(r, b, 0); b->string = str(r); }
    else if (!strcmp(t, "NiBinaryVoxelExtraData")) { extra(r, b, 0); b->data = u32(r); }
    else if (!strcmp(t, "NiBinaryVoxelData")) {
        unsigned a, c, d; long m; int g; obj(r, b); a = u16(r); c = u16(r); d = u16(r); b->vox_cell = f32(r); floats(r, b->vox_org, 3); skip(r, 12);
        b->vox_dim[0] = (unsigned short)a; b->vox_dim[1] = (unsigned short)c; b->vox_dim[2] = (unsigned short)d;
        m = ((long)d - 1) * ((long)a - 1) * ((long)c - 1); m = (m + (m < 0 ? 7 : 0)) / 8 + 1;
        if (m <= 0 || m > 1 << 24 || r->o + 7 * m > r->n) { r->bad = 1; return 0; }
        b->vox_bytes = (int)m;
        for (g = 0; g < 7; g++) { b->vox_bits[g] = malloc((size_t)m); memcpy(b->vox_bits[g], r->b + r->o, (size_t)m); skip(r, m); }
        skip(r, 16L * u32(r)); skip(r, (long)u32(r)); skip(r, 20);
    }
    else if (!strcmp(t, "NiKeyframeController") || !strcmp(t, "NiVisController") || !strcmp(t, "NiLookAtController") || !strcmp(t, "NiRollController")) { timectrl(r, b); b->data = u32(r); }
    else if (!strcmp(t, "NiKeyframeData")) { obj(r, b); keys(r, 0); keys(r, 1); keys(r, 2); }
    else if (!strcmp(t, "NiFloatData")) { obj(r, b); keys(r, 2); }
    else if (!strcmp(t, "NiVisData")) { obj(r, b); skip(r, 5L * u32(r)); }
    else if (!strcmp(t, "NiFlipController")) { timectrl(r, b); u32(r); f32(r); skip(r, 4L * u32(r)); }
    else if (!strcmp(t, "NiTriShapeSkinController")) {
        unsigned nb, i; long tot = 0; timectrl(r, b); nb = u32(r);
        if (nb > 10000) { r->bad = 1; return 0; }
        for (i = 0; i < nb; i++) tot += u32(r);
        skip(r, 4L * nb); skip(r, 18 * tot);                    /* bone links; per vertex: weight, index, offset */
    }
    else if (!strcmp(t, "NiAmbientLight") || !strcmp(t, "NiDirectionalLight")) light(r, b);
    else if (!strcmp(t, "NiPointLight")) { light(r, b); skip(r, 12); }
    else if (!strcmp(t, "NiSpotLight")) { light(r, b); skip(r, 20); }
    else if (!strcmp(t, "NiCamera")) { avobj(r, b); skip(r, 44); u32(r); skip(r, 4L * u32(r)); u32(r); }
    else return 0;
    return !r->bad;
}

NifFile *nif_parse(const unsigned char *buf, long len, char *err, int errlen)
{
    static const char tlo[] = "\x10\x00\x00\x00Top Level Object";
    R r; NifFile *f; const unsigned char *p; long i; int cap = 0, rcap = 0, top = 0;
    memset(&r, 0, sizeof r); r.b = buf; r.n = len;
    if (len < 40 || memcmp(buf, "NetImmerse File Format, Version ", 32)) { snprintf(err, errlen, "not a NetImmerse file"); return NULL; }
    f = calloc(1, sizeof *f);
    for (i = 0; i < 15 && 32 + i < len && buf[32 + i] != '\n'; i++) f->version[i] = (char)buf[32 + i];
    r.ver = f->version;
    for (p = NULL, i = 0; i + 20 <= len; i++) if (!memcmp(buf + i, tlo, 20)) { p = buf + i; break; }
    if (!p) { snprintf(err, errlen, "no top level object"); nif_free(f); return NULL; }
    r.o = p - buf;
    for (;;) {
        char *t = str(&r); NifBlock *b;
        if (!t) { snprintf(err, errlen, "bad block type string at %#lx", r.o); nif_free(f); return NULL; }
        if (!strcmp(t, "Top Level Object")) { free(t); top = 1; continue; }
        if (!strcmp(t, "End Of File")) { free(t); break; }
        b = calloc(1, sizeof *b); snprintf(b->type, sizeof b->type, "%s", t); free(t); b->scale = 1.0f;
        if (f->n == cap) { cap = cap ? cap * 2 : 64; f->b = realloc(f->b, cap * sizeof *f->b); }
        f->b[f->n++] = b;
        if (!read_block(&r, b)) { snprintf(err, errlen, "%s %s at %#lx", r.bad ? "bad data in" : "unknown block type", b->type, r.o); nif_free(f); return NULL; }
        if (top) { if (f->nroots == rcap) { rcap = rcap ? rcap * 2 : 4; f->roots = realloc(f->roots, rcap * sizeof *f->roots); } f->roots[f->nroots++] = b; top = 0; }
    }
    for (i = 0; i < f->n; i++) { NifBlock *b = f->b[i]; int k; for (k = 0; k < b->nkids; k++) { NifBlock *c = nif_ref(f, b->kids[k]); if (c) c->parent = b; } }
    return f;
}
NifFile *nif_load(const char *path, char *err, int errlen)
{
    FILE *fp = fopen(path, "rb"); long n; unsigned char *buf; NifFile *f;
    if (!fp) { snprintf(err, errlen, "cannot open %s", path); return NULL; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = malloc(n ? n : 1); if (fread(buf, 1, n, fp) != (size_t)n) n = 0; fclose(fp);
    f = nif_parse(buf, n, err, errlen); free(buf); return f;
}
const NifBlock *nif_voxel(const NifFile *f) { int i; for (i = 0; i < f->n; i++) if (!strcmp(f->b[i]->type, "NiBinaryVoxelData") && f->b[i]->vox_bytes) return f->b[i]; return NULL; }
void nif_free(NifFile *f)
{
    int i; if (!f) return;
    for (i = 0; i < f->n; i++) { NifBlock *b = f->b[i]; free(b->name); free(b->props); free(b->kids); free(b->verts); free(b->normals); free(b->colors); free(b->uv); free(b->tris); free(b->file); free(b->string); { int g; for (g = 0; g < 7; g++) free(b->vox_bits[g]); } free(b); }
    free(f->b); free(f->roots); free(f);
}
NifBlock *nif_ref(const NifFile *f, unsigned link) { int i; if (!link) return NULL; for (i = 0; i < f->n; i++) if (f->b[i]->link == link) return f->b[i]; return NULL; }
NifBlock *nif_find(const NifFile *f, const char *name) { int i; if (name) for (i = 0; i < f->n; i++) if (f->b[i]->name && !strcasecmp(f->b[i]->name, name)) return f->b[i]; return NULL; }

/* bounding sphere: every mesh's data bound moved by the node transforms up to the root (rotation, translation, uniform scale) */
static void xform(const NifBlock *b, float p[3], float *s)
{
    for (; b; b = b->parent) {
        float q[3]; int i;
        for (i = 0; i < 3; i++) q[i] = (b->rot[3 * i] * p[0] + b->rot[3 * i + 1] * p[1] + b->rot[3 * i + 2] * p[2]) * b->scale + b->t[i];
        memcpy(p, q, sizeof q); *s *= b->scale;
    }
}
float nif_radius(const NifFile *f, float c[3])
{
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}, cen[3], r = 0; int i, k, any = 0;
    float (*sp)[4] = malloc(sizeof *sp * (f->n ? f->n : 1)); int ns = 0;
    for (i = 0; i < f->n; i++) {
        NifBlock *g = f->b[i], *d; float p[3], s = 1;
        if (strcmp(g->type, "NiTriShape") || !(d = nif_ref(f, g->data))) continue;
        memcpy(p, d->bound, 12); xform(g, p, &s);
        sp[ns][0] = p[0]; sp[ns][1] = p[1]; sp[ns][2] = p[2]; sp[ns][3] = d->bound[3] * s; ns++;
        for (k = 0; k < 3; k++) { if (p[k] - sp[ns - 1][3] < lo[k]) lo[k] = p[k] - sp[ns - 1][3]; if (p[k] + sp[ns - 1][3] > hi[k]) hi[k] = p[k] + sp[ns - 1][3]; }
        any = 1;
    }
    if (!any) { if (c) c[0] = c[1] = c[2] = 0; free(sp); return 0; }
    for (k = 0; k < 3; k++) cen[k] = (lo[k] + hi[k]) * 0.5f;
    for (i = 0; i < ns; i++) { float dx = sp[i][0] - cen[0], dy = sp[i][1] - cen[1], dz = sp[i][2] - cen[2], d = sqrtf(dx * dx + dy * dy + dz * dz) + sp[i][3]; if (d > r) r = d; }
    if (c) memcpy(c, cen, sizeof cen);
    free(sp); return r;
}

/* all mesh triangles in the root's space (9 floats each: three corners), for picking; the caller frees the array */
float *nif_triangles(const NifFile *f, int *count)
{
    float *out = NULL; int n = 0, cap = 0, i, t, k;
    for (i = 0; i < f->n; i++) {
        NifBlock *g = f->b[i], *d; float *w;
        if (strcmp(g->type, "NiTriShape") || !(d = nif_ref(f, g->data)) || !d->verts || !d->tris) continue;
        w = malloc(sizeof(float) * 3 * (d->nverts ? d->nverts : 1));
        for (k = 0; k < d->nverts; k++) { float s = 1; memcpy(w + 3 * k, d->verts + 3 * k, 12); xform(g, w + 3 * k, &s); }
        for (t = 0; t < d->ntris; t++) {
            unsigned short *ix = d->tris + 3 * t;
            if (ix[0] >= d->nverts || ix[1] >= d->nverts || ix[2] >= d->nverts) continue;
            if (n == cap) { cap = cap ? cap * 2 : 1024; out = realloc(out, sizeof(float) * 9 * cap); }
            for (k = 0; k < 3; k++) memcpy(out + 9 * n + 3 * k, w + 3 * ix[k], 12);
            n++;
        }
        free(w);
    }
    *count = n; return out;
}

/* the nearest property of a type on the shape or its parents (NetImmerse property inheritance) */
static NifBlock *prop_of(const NifFile *f, const NifBlock *g, const char *t1, const char *t2)
{
    int i;
    for (; g; g = g->parent) for (i = 0; i < g->nprops; i++) { NifBlock *p = nif_ref(f, g->props[i]); if (p && (!strcmp(p->type, t1) || (t2 && !strcmp(p->type, t2)))) return p; }
    return NULL;
}
static void rotate_up(const NifBlock *b, float n[3])
{
    for (; b; b = b->parent) { float q[3]; int i; for (i = 0; i < 3; i++) q[i] = b->rot[3 * i] * n[0] + b->rot[3 * i + 1] * n[1] + b->rot[3 * i + 2] * n[2]; memcpy(n, q, sizeof q); }
}
NifMesh *nif_meshes(const NifFile *f, int *count)
{
    NifMesh *out = NULL; int n = 0, i, t, k;
    for (i = 0; i < f->n; i++) {
        NifBlock *g = f->b[i], *d, *a, *tp, *mp; float *w, *wn; NifMesh *m; int hidden = 0, nv = 0;
        if (strcmp(g->type, "NiTriShape") || !(d = nif_ref(f, g->data)) || !d->verts || !d->tris) continue;
        for (a = g; a; a = a->parent) if (a->flags & 1) hidden = 1;          /* NiAVObject flag bit 0: hidden (marker and helper shapes) */
        if (hidden) continue;
        w = malloc(sizeof(float) * 6 * (d->nverts ? d->nverts : 1)); wn = w + 3 * d->nverts;
        for (k = 0; k < d->nverts; k++) {
            float s = 1, *nn = wn + 3 * k, l;
            memcpy(w + 3 * k, d->verts + 3 * k, 12); xform(g, w + 3 * k, &s);
            if (d->normals) { memcpy(nn, d->normals + 3 * k, 12); rotate_up(g, nn); l = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]); if (l > 1e-12f) { nn[0] /= l; nn[1] /= l; nn[2] /= l; } }
        }
        out = realloc(out, sizeof *out * (n + 1)); m = &out[n++]; memset(m, 0, sizeof *m);
        m->v = malloc(sizeof(float) * 8 * 3 * (d->ntris ? d->ntris : 1));
        for (t = 0; t < d->ntris; t++) {
            unsigned short *ix = d->tris + 3 * t; float fn[3] = {0, 0, 1};
            if (ix[0] >= d->nverts || ix[1] >= d->nverts || ix[2] >= d->nverts) continue;
            if (!d->normals) {                                                 /* no normals in the file: the face normal */
                const float *p0 = w + 3 * ix[0], *p1 = w + 3 * ix[1], *p2 = w + 3 * ix[2]; float e1[3], e2[3], l;
                for (k = 0; k < 3; k++) { e1[k] = p1[k] - p0[k]; e2[k] = p2[k] - p0[k]; }
                fn[0] = e1[1] * e2[2] - e1[2] * e2[1]; fn[1] = e1[2] * e2[0] - e1[0] * e2[2]; fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                l = sqrtf(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]); if (l > 1e-12f) { fn[0] /= l; fn[1] /= l; fn[2] /= l; }
            }
            for (k = 0; k < 3; k++) {
                float *o = m->v + 8 * nv++;
                memcpy(o, w + 3 * ix[k], 12);
                memcpy(o + 3, d->normals ? wn + 3 * ix[k] : fn, 12);
                if (d->uv && d->nuvsets > 0) { o[6] = d->uv[2 * ix[k]]; o[7] = d->uv[2 * ix[k] + 1]; } else o[6] = o[7] = 0;
            }
        }
        m->nverts = nv; free(w);
        tp = prop_of(f, g, "NiTextureProperty", NULL);
        if (tp && (a = nif_ref(f, tp->data)) && a->file && *a->file) m->texture = strdup(a->file);
        tp = prop_of(f, g, "NiMultiTextureProperty", NULL);
        if (tp) {
            char **dst[5]; int q; dst[0] = &m->dark; dst[1] = &m->detail; dst[2] = &m->gloss; dst[3] = &m->glow; dst[4] = &m->glowa;
            for (q = 0; q < 5; q++) if (tp->mt_slot[q] && (a = nif_ref(f, tp->mt_u[q][0])) && a->file && *a->file) *dst[q] = strdup(a->file);
            if (!m->texture && m->dark) { m->texture = m->dark; m->dark = NULL; }   /* a shape with only a multi-texture property (bridge sets): its first map is the picture */
        }
        mp = prop_of(f, g, "NiMaterialProperty", NULL);
        if (mp) { memcpy(m->diffuse, mp->mat + 3, 12); memcpy(m->emissive, mp->mat + 9, 12); memcpy(m->specular, mp->mat + 6, 12); m->shininess = mp->mat[12]; m->alpha = mp->mat[13]; }
        else { m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = 1; m->alpha = 1; }
    }
    *count = n; return out;
}
void nif_meshes_free(NifMesh *m, int count) { int i; for (i = 0; i < count; i++) { free(m[i].v); free(m[i].texture); free(m[i].dark); free(m[i].detail); free(m[i].glow); free(m[i].gloss); free(m[i].glowa); } free(m); }
