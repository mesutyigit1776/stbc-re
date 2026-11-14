/* Value types of the engine that scripts create and read: NiColor, NiColorA/TGColorA, NiPoint2.
   Layouts from the original constructors: NiColor 3 floats (0xc bytes), NiColorA/TGColorA 4 floats (0x10), NiPoint2 2 floats (8).
   Every function below replaces a weak stub of the generated Appc layer (eng_<Class>_<Method>). */
#include <stdlib.h>
#include <math.h>
#include "appc_runtime.h"

typedef struct { float r, g, b; } Color3;
typedef struct { float r, g, b, a; } Color4;
typedef struct { float x, y; } Point2;
typedef struct { float x, y, z; } Point3;

/* ---- NiColor ---- */
void eng_NiColor_r_set(void *p, float v) { ((Color3 *)p)->r = v; }
double eng_NiColor_r_get(void *p) { return ((Color3 *)p)->r; }
void eng_NiColor_g_set(void *p, float v) { ((Color3 *)p)->g = v; }
double eng_NiColor_g_get(void *p) { return ((Color3 *)p)->g; }
void eng_NiColor_b_set(void *p, float v) { ((Color3 *)p)->b = v; }
double eng_NiColor_b_get(void *p) { return ((Color3 *)p)->b; }
void *eng_new_NiColor(float r, float g, float b) { Color3 *c = malloc(sizeof *c); if (c) { c->r = r; c->g = g; c->b = b; } return c; }
void eng_delete_NiColor(void *p) { free(p); }

/* ---- NiColorA (TGColorA is the same layout) ---- */
void eng_NiColorA_r_set(void *p, float v) { ((Color4 *)p)->r = v; }
double eng_NiColorA_r_get(void *p) { return ((Color4 *)p)->r; }
void eng_NiColorA_g_set(void *p, float v) { ((Color4 *)p)->g = v; }
double eng_NiColorA_g_get(void *p) { return ((Color4 *)p)->g; }
void eng_NiColorA_b_set(void *p, float v) { ((Color4 *)p)->b = v; }
double eng_NiColorA_b_get(void *p) { return ((Color4 *)p)->b; }
void eng_NiColorA_a_set(void *p, float v) { ((Color4 *)p)->a = v; }
double eng_NiColorA_a_get(void *p) { return ((Color4 *)p)->a; }
void *eng_new_NiColorA(float r, float g, float b, float a) { Color4 *c = malloc(sizeof *c); if (c) { c->r = r; c->g = g; c->b = b; c->a = a; } return c; }
void eng_delete_NiColorA(void *p) { free(p); }

/* ---- TGColorA ---- */
void *eng_new_TGColorA(void) { return eng_new_NiColorA(0, 0, 0, 0); }
void eng_delete_TGColorA(void *p) { free(p); }
double eng_TGColorA_GetR(void *p) { return ((Color4 *)p)->r; }
double eng_TGColorA_GetG(void *p) { return ((Color4 *)p)->g; }
double eng_TGColorA_GetB(void *p) { return ((Color4 *)p)->b; }
double eng_TGColorA_GetA(void *p) { return ((Color4 *)p)->a; }
void eng_TGColorA_SetR(void *p, float v) { ((Color4 *)p)->r = v; }
void eng_TGColorA_SetG(void *p, float v) { ((Color4 *)p)->g = v; }
void eng_TGColorA_SetB(void *p, float v) { ((Color4 *)p)->b = v; }
void eng_TGColorA_SetA(void *p, float v) { ((Color4 *)p)->a = v; }
void eng_TGColorA_SetRGBA(void *p, float r, float g, float b, float a) { Color4 *c = p; c->r = r; c->g = g; c->b = b; c->a = a; }
/* TGColorA::Copy 005eed10 stores four floats; the Python wrapper passes the source object's four floats. */
void eng_TGColorA_Copy(void *p, void *src) { Color4 *c = p; const Color4 *s = src; c->r = s->r; c->g = s->g; c->b = s->b; c->a = s->a; }
/* py_TGColorA_ScaleRGB 005eed30 scales r, g and b only */
void eng_TGColorA_ScaleRGB(void *p, float f) { Color4 *c = p; c->r *= f; c->g *= f; c->b *= f; }
double eng_TGColorA_r_get(void *p) { return ((Color4 *)p)->r; }
void eng_TGColorA_r_set(void *p, float v) { ((Color4 *)p)->r = v; }
double eng_TGColorA_g_get(void *p) { return ((Color4 *)p)->g; }
void eng_TGColorA_g_set(void *p, float v) { ((Color4 *)p)->g = v; }
double eng_TGColorA_b_get(void *p) { return ((Color4 *)p)->b; }
void eng_TGColorA_b_set(void *p, float v) { ((Color4 *)p)->b = v; }
double eng_TGColorA_a_get(void *p) { return ((Color4 *)p)->a; }
void eng_TGColorA_a_set(void *p, float v) { ((Color4 *)p)->a = v; }

/* ---- NiPoint2 ---- */
void eng_NiPoint2_x_set(void *p, float v) { ((Point2 *)p)->x = v; }
double eng_NiPoint2_x_get(void *p) { return ((Point2 *)p)->x; }
void eng_NiPoint2_y_set(void *p, float v) { ((Point2 *)p)->y = v; }
double eng_NiPoint2_y_get(void *p) { return ((Point2 *)p)->y; }
void *eng_new_NiPoint2(float x, float y) { Point2 *q = malloc(sizeof *q); if (q) { q->x = x; q->y = y; } return q; }
void eng_delete_NiPoint2(void *p) { free(p); }
double eng_NiPoint2_Length(void *p) { Point2 *q = p; return sqrtf(q->y * q->y + q->x * q->x); }
double eng_NiPoint2_SqrLength(void *p) { Point2 *q = p; return q->y * q->y + q->x * q->x; }
double eng_NiPoint2_Dot(void *a, void *b) { Point2 *p = a, *q = b; return p->x * q->x + p->y * q->y; }
/* py_NiPoint2_Unitize 005bdd30: length above 1e-6 -> scale by 1/length and return the length; otherwise zero the vector and return 0 */
double eng_NiPoint2_Unitize(void *a)
{
    Point2 *q = a; float len = sqrtf(q->y * q->y + q->x * q->x);
    if (len > 9.999999974752427e-07f) { float s = 1.0f / len; q->x = s * q->x; q->y = s * q->y; return len; }
    q->x = 0.0f; q->y = 0.0f; return 0.0;
}

/* ---- TGPoint3 member accessors (App.py: x, y, z) ---- */
double eng_TGPoint3_x_get(void *p) { return ((Point3 *)p)->x; }
void eng_TGPoint3_x_set(void *p, float v) { ((Point3 *)p)->x = v; }
double eng_TGPoint3_y_get(void *p) { return ((Point3 *)p)->y; }
void eng_TGPoint3_y_set(void *p, float v) { ((Point3 *)p)->y = v; }
double eng_TGPoint3_z_get(void *p) { return ((Point3 *)p)->z; }
void eng_TGPoint3_z_set(void *p, float v) { ((Point3 *)p)->z = v; }

/* TGPoint3 construction and component access (12 bytes: x, y, z floats) */
void *eng_new_TGPoint3(void) { float *p = calloc(1, 12); return p; }
void eng_delete_TGPoint3(void *p) { free(p); }
double eng_TGPoint3_GetX(void *p) { return ((Point3 *)p)->x; }
double eng_TGPoint3_GetY(void *p) { return ((Point3 *)p)->y; }
double eng_TGPoint3_GetZ(void *p) { return ((Point3 *)p)->z; }
void eng_TGPoint3_SetX(void *p, float v) { ((Point3 *)p)->x = v; }
void eng_TGPoint3_SetY(void *p, float v) { ((Point3 *)p)->y = v; }
void eng_TGPoint3_SetZ(void *p, float v) { ((Point3 *)p)->z = v; }
void eng_TGPoint3_SetXYZ(void *p, float x, float y, float z) { Point3 *q = p; q->x = x; q->y = y; q->z = z; }

/* ---- NiPoint3 (12 bytes, same layout as TGPoint3) ---- */
void *eng_new_NiPoint3(float x, float y, float z) { Point3 *p = malloc(sizeof *p); if (p) { p->x = x; p->y = y; p->z = z; } return p; }
void eng_delete_NiPoint3(void *p) { free(p); }
double eng_NiPoint3_x_get(void *p) { return ((Point3 *)p)->x; }
void eng_NiPoint3_x_set(void *p, float v) { ((Point3 *)p)->x = v; }
double eng_NiPoint3_y_get(void *p) { return ((Point3 *)p)->y; }
void eng_NiPoint3_y_set(void *p, float v) { ((Point3 *)p)->y = v; }
double eng_NiPoint3_z_get(void *p) { return ((Point3 *)p)->z; }
void eng_NiPoint3_z_set(void *p, float v) { ((Point3 *)p)->z = v; }
double eng_NiPoint3_SqrLength(void *p) { Point3 *q = p; return q->z * q->z + q->y * q->y + q->x * q->x; }
double eng_NiPoint3_Length(void *p) { Point3 *q = p; return sqrtf(q->z * q->z + q->y * q->y + q->x * q->x); }
double eng_NiPoint3_Dot(void *a, void *b) { Point3 *p = a, *q = b; return p->x * q->x + p->y * q->y + p->z * q->z; }
/* py_NiPoint3_Unitize 005be350: length above 1e-6 -> scale by 1/length and return the length; otherwise zero the vector and return 0 */
double eng_NiPoint3_Unitize(void *a)
{
    Point3 *q = a; float len = sqrtf(q->z * q->z + q->y * q->y + q->x * q->x);
    if (len > 9.999999974752427e-07f) { float s = 1.0f / len; q->x = s * q->x; q->y = s * q->y; q->z = s * q->z; return len; }
    q->x = q->y = q->z = 0.0f; return 0.0;
}
static void *cross_new(const Point3 *a, const Point3 *b)      /* a x b, new object (005be430, TGPoint3::Cross 0045c1a0) */
{
    Point3 *r = malloc(sizeof *r);
    if (!r) return NULL;
    r->x = a->y * b->z - b->y * a->z; r->y = a->z * b->x - a->x * b->z; r->z = a->x * b->y - b->x * a->y;
    return r;
}
static void *unitcross_new(const Point3 *a, const Point3 *b)  /* normalized, zero when the length is not above 1e-6 (005be520, 00581e60) */
{
    Point3 *r = cross_new(a, b); float len;
    if (!r) return NULL;
    len = sqrtf(r->x * r->x + r->y * r->y + r->z * r->z);
    if (len > 9.999999974752427e-07f) { float s = 1.0f / len; r->x *= s; r->y *= s; r->z *= s; } else r->x = r->y = r->z = 0.0f;
    return r;
}
void *eng_NiPoint3_Cross(void *a, void *b) { return cross_new(a, b); }
void *eng_NiPoint3_UnitCross(void *a, void *b) { return unitcross_new(a, b); }
void *eng_TGPoint3_Cross(void *a, void *b) { return cross_new(a, b); }
void *eng_TGPoint3_UnitCross(void *a, void *b) { return unitcross_new(a, b); }
/* NiPoint3::Perpendicular (005be680) picks one of three constant axes (NI statics at 0x9a2840..0x9a2870 that are filled at start-up and not yet
   decoded) and returns the normalized cross product; this version uses the coordinate axis least aligned with the vector. */
void *eng_NiPoint3_Perpendicular(void *p)
{
    Point3 *v = p, ax = {0, 0, 0};
    float x = fabsf(v->x), y = fabsf(v->y), z = fabsf(v->z);
    if (y <= x && y <= z) ax.y = 1.0f; else if (x <= z) ax.x = 1.0f; else ax.z = 1.0f;
    return unitcross_new(v, &ax);
}
void *eng_TGPoint3_GetRandomUnitVector(void)        /* 0045e7b0: three rand() values scaled to [-1,1], normalized; repeat while the squared length is below 0.5 */
{
    Point3 *r = malloc(sizeof *r); float x, y, z, len;
    if (!r) return NULL;
    do {
        x = (float)rand() * 6.103701889514923e-05f - 1.0f; y = (float)rand() * 6.103701889514923e-05f - 1.0f; z = (float)rand() * 6.103701889514923e-05f - 1.0f;
        len = sqrtf(x * x + z * z + y * y);
        if (len <= 9.999999974752427e-07f) { x = z = 0.0f; y = 0.0f; } else { float s = 1.0f / len; x *= s; z *= s; y *= s; }
    } while (x * x + y * y + z * z < 0.5f);
    r->x = x; r->y = y; r->z = z; return r;
}
/* 0045e980: projection of this onto the argument; the zero-length argument case returns a global that is not decoded yet (zeros here) */
void *eng_TGPoint3_GetAlignedComponent(void *a, void *b)
{
    Point3 *p = a, *q = b, *r = calloc(1, sizeof *r); float d = q->x * p->x + p->z * q->z + p->y * q->y, l2 = q->z * q->z + q->y * q->y + q->x * q->x;
    if (l2 != 0.0f) { float s = 1.0f / l2; r->x = s * q->x * d; r->z = s * q->z * d; r->y = s * q->y * d; }
    return r;
}
void *eng_TGPoint3_GetPerpendicularComponent(void *a, void *b)     /* 0045ea50: this - aligned component */
{
    Point3 *p = a, *al = eng_TGPoint3_GetAlignedComponent(a, b), *r = malloc(sizeof *r);
    r->x = p->x - al->x; r->y = p->y - al->y; r->z = p->z - al->z; free(al); return r;
}
void eng_TGPoint3_Add(void *a, void *b) { Point3 *p = a, *q = b; p->x += q->x; p->y += q->y; p->z += q->z; }
void eng_TGPoint3_Subtract(void *a, void *b) { Point3 *p = a, *q = b; p->x -= q->x; p->y -= q->y; p->z -= q->z; }
void eng_TGPoint3_Scale(void *a, float f) { Point3 *p = a; p->x *= f; p->y *= f; p->z *= f; }
void eng_TGPoint3_Set(void *a, void *b) { Point3 *p = a, *q = b; p->x = q->x; p->y = q->y; p->z = q->z; }
double eng_TGPoint3_VectorLength(void *a) { return eng_NiPoint3_Length(a); }
/* py_TGPoint3_UnitizeVector 005ee540 scales by 1/sqrt(length^2) taken from a fast-sqrt lookup table (about 0.4% error) and by 0 for a zero vector;
   this version uses the exact value. The table (DAT_009a284c) is built at start-up and is not ported yet. */
void eng_TGPoint3_UnitizeVector(void *a)
{
    Point3 *p = a; float l2 = p->z * p->z + p->y * p->y + p->x * p->x, s = 0.0f;
    if (l2 != 0.0f) s = 1.0f / sqrtf(l2);
    p->x *= s; p->y *= s; p->z *= s;
}
void eng_TGPoint3_UnitizeVectors(void *a, int n) { Point3 *p = a; int i; for (i = 0; i < n; i++) eng_TGPoint3_UnitizeVector(p + i); }
