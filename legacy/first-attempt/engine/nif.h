/* NetImmerse 3.x (.nif) reader: the scene graph as data (no drawing). No Python and no platform code, so the renderer and tools can use it.
   Block layouts come from the exe's LoadBinary functions; re/nif_scan.py is the prototype and the checker (all game and mod files). */
#ifndef NIF_H
#define NIF_H

typedef struct NifBlock {
    char type[40]; unsigned link;                      /* NiObject: the object's address in the original process, used as a reference id */
    char *name; unsigned extra, ctrl;                  /* NiObjectNET */
    unsigned short flags; float t[3], rot[9], scale;   /* NiAVObject (rot: row-major 3x3) */
    unsigned *props; int nprops;
    unsigned *kids; int nkids;                         /* NiNode children */
    unsigned data;                                     /* NiGeometry data, controller data, NiTextureProperty image */
    /* NiMultiTextureProperty as stored (loader 008029d0): the count word; per slot a present flag (a link), then the NiImage link, clamp and filter mode and
       the UV set, and from 3.04 on two shorts and two bytes. The slots are named in the exe's viewer strings (GetViewerStrings 00802f40): 0 dark, 1 detail,
       2 gloss, 3 glow, 4 glow alpha, 5 and up decals. A file with 4 in the count has no slot 4. */
    unsigned mt_count, mt_slot[6]; unsigned short mt_s[6][2]; unsigned char mt_b[6][2]; unsigned mt_u[6][3];   /* mt_u[k][0]: the image link; [1] clamp, [2] filter */
    unsigned next, target;                             /* NiTimeController */
    /* NiGeometryData / NiTriShapeData */
    int nverts, nuvsets, ntris; float *verts, *normals, *colors, *uv, bound[4]; unsigned short *tris;
    /* NiImage */
    char *file;
    /* NiMaterialProperty: ambient, diffuse, specular, emissive (3 each), glossiness, alpha */
    float mat[14];
    char *string;                                      /* NiStringExtraData */
    /* NiBinaryVoxelData (loader 004a5890): lattice size (cells are one less each way), cell size, origin, and the seven bit grids (cell index x + y * (nx - 1) + z * (nx - 1) * (ny - 1)); grid 0 marks the solid cells */
    unsigned short vox_dim[3]; float vox_cell, vox_org[3]; unsigned char *vox_bits[7]; int vox_bytes;
    struct NifBlock *parent;
} NifBlock;

typedef struct NifFile {
    char version[16];
    NifBlock **b; int n;
    NifBlock **roots; int nroots;                      /* top level objects */
} NifFile;

NifFile *nif_load(const char *path, char *err, int errlen);   /* NULL on failure; err says why */
NifFile *nif_parse(const unsigned char *buf, long len, char *err, int errlen);
void nif_free(NifFile *f);
const NifBlock *nif_voxel(const NifFile *f);                  /* the NiBinaryVoxelData block of a voxel file, or NULL */
NifBlock *nif_ref(const NifFile *f, unsigned link);           /* a reference id -> block (NULL for 0 or unknown) */
NifBlock *nif_find(const NifFile *f, const char *name);       /* first block with that name (case-insensitive) */
float nif_radius(const NifFile *f, float center_out[3]);     /* bounding sphere of all meshes in the root's space */
float *nif_triangles(const NifFile *f, int *count);          /* all mesh triangles in the root's space (9 floats each); free() the result */

/* one drawable shape: a triangle list in the root's space, 8 floats a vertex (position, normal, first UV set), with the base texture
   file name and the material colours that apply to it (the nearest property on the shape or its parents) */
typedef struct NifMesh {
    float *v; int nverts;
    char *texture;                                     /* NiImage file name as stored (no path search), or NULL */
    char *dark, *detail, *gloss, *glow, *glowa;        /* the multi-texture slots 0 to 4 (file names as stored), or NULL */
    float diffuse[3], emissive[3], alpha;              /* from NiMaterialProperty; diffuse 1 and alpha 1 when there is none */
    float specular[3], shininess;                      /* the material's specular colour and shininess (D3D power); specular 0 when there is no material */
} NifMesh;
NifMesh *nif_meshes(const NifFile *f, int *count);           /* visible NiTriShapes only (no hidden flag on the shape or a parent) */
void nif_meshes_free(NifMesh *m, int count);
#endif
