/* Model data the renderer reads from the game layer (tg_models.c). No Python and no platform code. */
#ifndef TG_MODELS_H
#define TG_MODELS_H
typedef struct TgMesh {
    float *v; int nverts;                              /* triangle list, 8 floats a vertex: position, normal, UV; model space, scaled */
    char *texture;                                     /* real path of the base texture, or NULL (none named, or not found) */
    char *glow;                                        /* real path of the glow map (multi-texture slot 3, drawn additively), or NULL */
    char *glowa;                                       /* real path of the glow alpha map (slot 4: the picture's alpha channel marks what glows), or NULL */
    char *gloss;                                       /* real path of the gloss map (slot 2; it scales the specular highlight), or NULL */
    float diffuse[3], emissive[3], alpha;              /* NiMaterialProperty colours */
    float specular[3], shininess;                      /* its specular colour and shininess (D3D power; 0 switches the highlight off) */
} TgMesh;
const TgMesh *tg_model_meshes(const char *name, int *n);
/* The solid cells of the model's voxel file (<model file>_vox.nif, NiBinaryVoxelData 004a5890), in mesh space: cell (x, y, z) spans org + (i .. i + 1) * cell, with
   index x + y * nx + z * nx * ny; bit set in `solid` for a cell of the hull body (the union of the seven grids of the file; grid 0, loader offset 0x48, is only its core). NULL when the model has no voxel file. */
typedef struct { int nx, ny, nz; float cell, org[3]; const unsigned char *solid; int nsolid; } TgVoxel;
const TgVoxel *tg_model_voxel(const char *name);
/* the first hit of the segment a to b (mesh space) with the model's triangles: its fraction along the segment, the face normal (unit); 0 when there is none */
int tg_model_ray_first(const char *name, const float *a, const float *b, float *t, float *normal);
const float *tg_model_tris(const char *name, int *n);
int tg_model_box(const char *name, float centre[3], float half[3]);
int tg_model_random_vertex(const char *name, float pos[3], float nrm[3]);
#endif
