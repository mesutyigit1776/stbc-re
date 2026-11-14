/* nifcheck: parse NIF files with port/engine/nif.c and decode texture files (.tga, .bmp, .dds) with port/engine/image.c;
   report failures and model sizes.
   Build: cc -O1 -g -fsanitize=address -Iport/engine port/tools/nifcheck.c port/engine/nif.c port/engine/image.c -o port/tools/nifcheck
   Use:   port/tools/nifcheck FILE_OR_DIR... [-v] */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "nif.h"
#include "image.h"
static int ok, bad, verbose, iok, ibad;
static void img(const char *p)
{
    char err[256]; int w, h; unsigned char *px = img_load(p, &w, &h, err, sizeof err);
    if (!px) { ibad++; if (ibad <= 20) printf("FAIL %s: %s\n", p, err); return; }
    iok++; free(px);
}
static void one(const char *p)
{
    char err[256]; NifFile *f = nif_load(p, err, sizeof err); float c[3];
    if (!f) { bad++; if (bad <= 20) printf("FAIL %s: %s\n", p, err); return; }
    ok++;
    if (verbose) { float r = nif_radius(f, c); int i, tris = 0; for (i = 0; i < f->n; i++) tris += f->b[i]->ntris; printf("%s: version %s, %d blocks, %d triangles, radius %.3f\n", p, f->version, f->n, tris, r);
        {   int nm, k; NifMesh *m = nif_meshes(f, &nm);           /* drawn shapes: base texture and material colours */
            for (k = 0; k < nm; k++) printf("  shape %d: %d vertices, texture %s, diffuse %.2f %.2f %.2f, emissive %.2f %.2f %.2f\n", k, m[k].nverts,
                m[k].texture ? m[k].texture : "-", m[k].diffuse[0], m[k].diffuse[1], m[k].diffuse[2], m[k].emissive[0], m[k].emissive[1], m[k].emissive[2]);
            nif_meshes_free(m, nm); } }
    nif_free(f);
}
static void walk(const char *p)
{
    struct stat st; DIR *d; struct dirent *e; char q[2048];
    if (stat(p, &st) || !S_ISDIR(st.st_mode)) { size_t n = strlen(p); if (n > 4 && !strcasecmp(p + n - 4, ".nif")) one(p);
        else if (n > 4 && (!strcasecmp(p + n - 4, ".tga") || !strcasecmp(p + n - 4, ".bmp") || !strcasecmp(p + n - 4, ".dds"))) img(p);
        return; }
    if (!(d = opendir(p))) return;
    while ((e = readdir(d))) if (e->d_name[0] != '.') { snprintf(q, sizeof q, "%s/%s", p, e->d_name); walk(q); }
    closedir(d);
}
int main(int argc, char **argv)
{
    int i; for (i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1;
    for (i = 1; i < argc; i++) if (strcmp(argv[i], "-v")) walk(argv[i]);
    printf("%d NIF files parsed, %d failed; %d textures decoded, %d failed\n", ok, bad, iok, ibad); return bad || ibad;
}
