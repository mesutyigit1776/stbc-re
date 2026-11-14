/* TGMatrix3 (stbc.exe 004595b0..0045b710, wrappers 005eb830..005ecb20) and TGPoint3 matrix products (0045e8d0, 0045e950).
   Nine floats, row-major: m[row * 3 + col]. The rotation makers are the TRANSPOSE of the usual column-vector matrices
   (MakeXRotation puts sin at m[5] and -sin at m[7]). MultiplyVector computes M * v; TGPoint3.MultMatrix computes v * M.
   Constants not read from the exe: the "small" epsilons (1e-6) in Inverse, Reorthogonalize and ExtractAngleAndAxis (0x888b58, 0x88b9c8),
   and the Euler "half pi" at 0x9a28cc (set at run time, taken as pi / 2). TransformVertices*, EigenSolveSymmetric are not ported. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "appc_runtime.h"

typedef struct { float m[9]; } Mat3;
#define EPS 1e-6f
#define HALF_PI 1.5707963267948966

static Mat3 *newm(void) { return calloc(1, sizeof(Mat3)); }
void *eng_new_TGMatrix3(void) { return newm(); }
void eng_delete_TGMatrix3(void *p) { free(p); }

void eng_TGMatrix3_GetRow(void *a, int i, void *out) { memcpy(out, &((Mat3 *)a)->m[i * 3], 12); }
void eng_TGMatrix3_SetRow(void *a, int i, void *v) { memcpy(&((Mat3 *)a)->m[i * 3], v, 12); }
void eng_TGMatrix3_GetCol(void *a, int i, void *out) { float *m = ((Mat3 *)a)->m, *o = out; o[0] = m[i]; o[1] = m[3 + i]; o[2] = m[6 + i]; }
void eng_TGMatrix3_SetCol(void *a, int i, void *v) { float *m = ((Mat3 *)a)->m, *o = v; m[i] = o[0]; m[3 + i] = o[1]; m[6 + i] = o[2]; }
double eng_TGMatrix3_GetEntry(void *a, int r, int c) { return ((Mat3 *)a)->m[r * 3 + c]; }
void eng_TGMatrix3_SetEntry(void *a, int r, int c, float v) { ((Mat3 *)a)->m[r * 3 + c] = v; }
void eng_TGMatrix3_MakeZero(void *a) { memset(a, 0, sizeof(Mat3)); }
void eng_TGMatrix3_MakeIdentity(void *a) { float *m = ((Mat3 *)a)->m; memset(m, 0, 36); m[0] = m[4] = m[8] = 1.0f; }
void eng_TGMatrix3_MakeDiagonal(void *a, float x, float y, float z) { float *m = ((Mat3 *)a)->m; memset(m, 0, 36); m[0] = x; m[4] = y; m[8] = z; }
void eng_TGMatrix3_MakeXRotation(void *a, float t) { float *m = ((Mat3 *)a)->m; float c = (float)cos(t), s = (float)sin(t); memset(m, 0, 36); m[0] = 1; m[4] = c; m[5] = s; m[7] = -s; m[8] = c; }
void eng_TGMatrix3_MakeYRotation(void *a, float t) { float *m = ((Mat3 *)a)->m; float c = (float)cos(t), s = (float)sin(t); memset(m, 0, 36); m[4] = 1; m[0] = c; m[2] = -s; m[6] = s; m[8] = c; }
void eng_TGMatrix3_MakeZRotation(void *a, float t) { float *m = ((Mat3 *)a)->m; float c = (float)cos(t), s = (float)sin(t); memset(m, 0, 36); m[8] = 1; m[0] = c; m[1] = s; m[3] = -s; m[4] = c; }
void eng_TGMatrix3_MakeRotation(void *a, float t, void *axis)       /* MakeAxisAngleRotation 004597a0; the axis is a TGPoint3 */
{
    float *m = ((Mat3 *)a)->m, *u = axis; double c = cos(t), s = sin(t), k = 1.0 - c;
    float x = u[0], y = u[1], z = u[2];
    float xy = (float)(k * x * y), xz = (float)(k * x * z), yz = (float)(k * y * z);
    m[0] = (float)(x * x * k + c); m[1] = (float)(s * z + xy);      m[2] = xz - (float)(s * y);
    m[3] = (float)(xy - s * z);    m[4] = (float)(y * y * k + c);   m[5] = (float)(s * x) + yz;
    m[6] = (float)(s * y) + xz;    m[7] = yz - (float)(s * x);      m[8] = (float)(z * z * k + c);
}
void eng_TGMatrix3_Set(void *a, void *src) { memcpy(a, src, sizeof(Mat3)); }

static void mul(const float *A, const float *B, float *R)            /* R = A * B (operator_mul_matrix 00459ad0) */
{
    float t[9]; int i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) t[i * 3 + j] = A[i * 3] * B[j] + A[i * 3 + 1] * B[3 + j] + A[i * 3 + 2] * B[6 + j];
    memcpy(R, t, 36);
}
void *eng_TGMatrix3_MultMatrix(void *a, void *b) { Mat3 *r = newm(); mul(((Mat3 *)a)->m, ((Mat3 *)b)->m, r->m); return r; }
void *eng_TGMatrix3_MultMatrixLeft(void *a, void *b) { Mat3 *r = newm(); mul(((Mat3 *)b)->m, ((Mat3 *)a)->m, r->m); return r; }   /* b * a (0045e950 neighbours: 005ec4a0) */
void *eng_TGMatrix3_TransposeTimes(void *a, void *b)                  /* A^T * B (0045a460) */
{
    const float *A = ((Mat3 *)a)->m, *B = ((Mat3 *)b)->m; Mat3 *r = newm(); int i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) r->m[i * 3 + j] = A[i] * B[j] + A[3 + i] * B[3 + j] + A[6 + i] * B[6 + j];
    return r;
}
void *eng_TGMatrix3_Transpose(void *a)
{
    const float *m = ((Mat3 *)a)->m; Mat3 *r = newm(); int i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) r->m[j * 3 + i] = m[i * 3 + j];
    return r;
}
void *eng_TGMatrix3_Inverse(void *a)                                   /* InvertInto 00459d70; a singular matrix gives zero */
{
    const float *m = ((Mat3 *)a)->m; Mat3 *r = newm(); float *o = r->m, det; int i;
    o[0] = m[4] * m[8] - m[7] * m[5]; o[1] = m[7] * m[2] - m[8] * m[1]; o[2] = m[5] * m[1] - m[4] * m[2];
    o[3] = m[6] * m[5] - m[8] * m[3]; o[4] = m[0] * m[8] - m[6] * m[2]; o[5] = m[3] * m[2] - m[0] * m[5];
    o[6] = m[7] * m[3] - m[4] * m[6]; o[7] = m[6] * m[1] - m[7] * m[0]; o[8] = m[4] * m[0] - m[3] * m[1];
    det = o[3] * m[1] + o[0] * m[0] + o[6] * m[2];
    if (fabsf(det) <= EPS) { memset(o, 0, 36); return r; }
    for (i = 0; i < 9; i++) o[i] *= 1.0f / det;
    return r;
}
long eng_TGMatrix3_Reorthogonalize(void *a)                            /* 0045a2a0: Gram-Schmidt on the columns */
{
    float *m = ((Mat3 *)a)->m, l, d; int i;
    l = sqrtf(m[6] * m[6] + m[3] * m[3] + m[0] * m[0]); if (l < EPS) return 0;
    l = 1.0f / l; m[0] *= l; m[3] *= l; m[6] *= l;
    d = m[3] * m[4] + m[6] * m[7] + m[0] * m[1];
    m[1] -= m[0] * d; m[4] -= m[3] * d; m[7] -= m[6] * d;
    l = sqrtf(m[1] * m[1] + m[4] * m[4] + m[7] * m[7]); if (l < EPS) return 0;
    l = 1.0f / l; m[1] *= l; m[4] *= l; m[7] *= l;
    d = m[2] * m[0] + m[5] * m[3] + m[8] * m[6];
    m[2] -= d * m[0]; m[5] -= d * m[3]; m[8] -= d * m[6];
    d = m[5] * m[4] + m[8] * m[7] + m[2] * m[1];
    m[2] -= d * m[1]; m[5] -= d * m[4]; m[8] -= d * m[7];
    l = sqrtf(m[2] * m[2] + m[5] * m[5] + m[8] * m[8]); if (l < EPS) return 0;
    l = 1.0f / l; for (i = 2; i < 9; i += 3) m[i] *= l;
    return 1;
}
void *eng_TGMatrix3_Congruence(void *a, void *b)                       /* 0045a590: B^T-style double product, result = B * (A * B^T)-like; computed as in the loops */
{
    const float *A = ((Mat3 *)a)->m, *B = ((Mat3 *)b)->m; float T[9]; Mat3 *r = newm(); int i, j, k;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) { float s = 0; for (k = 0; k < 3; k++) s += B[i * 3 + k] * A[k * 3 + j]; T[i * 3 + j] = s; }
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) { float s = 0; for (k = 0; k < 3; k++) s += B[j * 3 + k] * T[i * 3 + k]; r->m[i * 3 + j] = s; }
    return r;
}
/* 00459f90: the angle comes out through the pointers in a2 (angle), a3..a5 (axis); the pointers are SWIG float pointers */
void eng_TGMatrix3_ExtractAngleAndAxis(void *a, void *ang, void *ax, void *ay, void *az)
{
    const float *m = ((Mat3 *)a)->m; float *A = ang, *X = ax, *Y = ay, *Z = az, c, l;
    c = ((m[4] + m[0] + m[8]) - 1.0f) * 0.5f;
    *A = c <= -1.0f ? (float)M_PI : c >= 1.0f ? 0.0f : acosf(c);
    *X = m[5] - m[7]; *Y = m[6] - m[2]; *Z = m[1] - m[3];
    l = sqrtf(*Z * *Z + *X * *X + *Y * *Y);
    if (l > EPS) { l = 1.0f / l; *X *= l; *Y *= l; *Z *= l; return; }
    if (*A <= 1.0f) { *X = 1; *Y = 0; *Z = 0; return; }            /* angle ~ 0 (as in the exe: compared with 1.0) */
    *X = sqrtf((m[0] + 1.0f) * 0.5f); *Y = sqrtf((m[4] + 1.0f) * 0.5f); *Z = sqrtf((m[8] + 1.0f) * 0.5f);
    {   /* pick the sign combination whose axis the matrix leaves fixed (the exe tries (+,+,+), (+,-,+), (+,-,-), (+,+,-)) */
        float sx[4] = {1, 1, 1, 1}, sy[4] = {1, -1, -1, 1}, sz[4] = {1, 1, -1, -1}; int k;
        float bx = *X, by = *Y, bz = *Z;
        for (k = 0; k < 4; k++) {
            float x = bx * sx[k], y = by * sy[k], z = bz * sz[k];
            float e0 = (z * m[2] + y * m[1] + x * m[0]) - x, e1 = (x * m[3] + m[4] * y + z * m[5]) - y, e2 = (x * m[6] + z * m[8] + y * m[7]) - z;
            if (e0 * e0 + e1 * e1 + e2 * e2 < EPS || k == 3) { *X = x; *Y = y; *Z = z; return; }
        }
    }
}

/* ---- Euler angles. Names list the axes in product order: R = R(first) * R(middle) * R(last); outputs are (first, middle, last) ---- */
static float clamp_asin(float v, int neg, double *out) { double a = v <= -1.0f ? -HALF_PI : v >= 1.0f ? HALF_PI : asin(v); *out = neg ? -a : a; return (float)*out; }
/* atan2 pairs are copied from 0045ad70..0045b2a0 */
long eng_TGMatrix3_ToEulerAnglesXYZ(void *a, void *o1, void *o2, void *o3)
{ const float *m = ((Mat3 *)a)->m; double y; float *p1 = o1, *p2 = o2, *p3 = o3; clamp_asin(m[2], 1, &y); *p2 = (float)y;
  if (y < HALF_PI) { if (y > -HALF_PI) { *p1 = (float)-atan2(-m[5], m[8]); *p3 = (float)-atan2(-m[1], m[0]); return 1; } *p1 = (float)atan2(m[3], m[4]); *p3 = 0; return 0; }
  *p1 = (float)-atan2(m[3], m[4]); *p3 = 0; return 0; }
long eng_TGMatrix3_ToEulerAnglesXZY(void *a, void *o1, void *o2, void *o3)
{ const float *m = ((Mat3 *)a)->m; double y; float *p1 = o1, *p2 = o2, *p3 = o3; clamp_asin(m[1], 0, &y); *p2 = (float)y;
  if (y < HALF_PI) { if (y > -HALF_PI) { *p1 = (float)-atan2(m[7], m[4]); *p3 = (float)-atan2(m[2], m[0]); return 1; } *p1 = (float)atan2(-m[6], m[8]); *p3 = 0; return 0; }
  *p1 = (float)-atan2(-m[6], m[8]); *p3 = 0; return 0; }
long eng_TGMatrix3_ToEulerAnglesYXZ(void *a, void *o1, void *o2, void *o3)
{ const float *m = ((Mat3 *)a)->m; double y; float *p1 = o1, *p2 = o2, *p3 = o3; clamp_asin(m[5], 0, &y); *p2 = (float)y;
  if (y < HALF_PI) { if (y > -HALF_PI) { *p1 = (float)-atan2(m[2], m[8]); *p3 = (float)-atan2(m[3], m[4]); return 1; } *p1 = (float)atan2(-m[1], m[0]); *p3 = 0; return 0; }
  *p1 = (float)-atan2(-m[1], m[0]); *p3 = 0; return 0; }
long eng_TGMatrix3_ToEulerAnglesYZX(void *a, void *o1, void *o2, void *o3)
{ const float *m = ((Mat3 *)a)->m; double y; float *p1 = o1, *p2 = o2, *p3 = o3; clamp_asin(m[3], 1, &y); *p2 = (float)y;
  if (y < HALF_PI) { if (y > -HALF_PI) { *p1 = (float)-atan2(-m[6], m[0]); *p3 = (float)-atan2(-m[5], m[4]); return 1; } *p1 = (float)atan2(m[7], m[8]); *p3 = 0; return 0; }
  *p1 = (float)-atan2(m[7], m[8]); *p3 = 0; return 0; }
long eng_TGMatrix3_ToEulerAnglesZXY(void *a, void *o1, void *o2, void *o3)
{ const float *m = ((Mat3 *)a)->m; double y; float *p1 = o1, *p2 = o2, *p3 = o3; clamp_asin(m[7], 1, &y); *p2 = (float)y;
  if (y < HALF_PI) { if (y > -HALF_PI) { *p1 = (float)-atan2(-m[1], m[4]); *p3 = (float)-atan2(-m[6], m[8]); return 1; } *p1 = (float)atan2(m[2], m[0]); *p3 = 0; return 0; }
  *p1 = (float)-atan2(m[2], m[0]); *p3 = 0; return 0; }
long eng_TGMatrix3_ToEulerAnglesZYX(void *a, void *o1, void *o2, void *o3)
{ const float *m = ((Mat3 *)a)->m; double y; float *p1 = o1, *p2 = o2, *p3 = o3; clamp_asin(m[6], 0, &y); *p2 = (float)y;
  if (y < HALF_PI) { if (y > -HALF_PI) { *p1 = (float)-atan2(m[3], m[0]); *p3 = (float)-atan2(m[7], m[8]); return 1; } *p1 = (float)atan2(-m[1], m[2]); *p3 = 0; return 0; }
  *p1 = (float)-atan2(-m[1], m[2]); *p3 = 0; return 0; }

static void rx(float *m, float t) { eng_TGMatrix3_MakeXRotation(m, t); }
static void ry(float *m, float t) { eng_TGMatrix3_MakeYRotation(m, t); }
static void rz(float *m, float t) { eng_TGMatrix3_MakeZRotation(m, t); }
static void euler(void *a, void (*f1)(float *, float), float t1, void (*f2)(float *, float), float t2, void (*f3)(float *, float), float t3)
{
    float A[9], B[9], C[9]; f1(A, t1); f2(B, t2); f3(C, t3); mul(A, B, A); mul(A, C, ((Mat3 *)a)->m);
}
void eng_TGMatrix3_FromEulerAnglesXYZ(void *a, float x, float y, float z) { euler(a, rx, x, ry, y, rz, z); }
void eng_TGMatrix3_FromEulerAnglesXZY(void *a, float x, float z, float y) { euler(a, rx, x, rz, z, ry, y); }
void eng_TGMatrix3_FromEulerAnglesYXZ(void *a, float y, float x, float z) { euler(a, ry, y, rx, x, rz, z); }
void eng_TGMatrix3_FromEulerAnglesYZX(void *a, float y, float z, float x) { euler(a, ry, y, rz, z, rx, x); }
void eng_TGMatrix3_FromEulerAnglesZXY(void *a, float z, float x, float y) { euler(a, rz, z, rx, x, ry, y); }
void eng_TGMatrix3_FromEulerAnglesZYX(void *a, float z, float y, float x) { euler(a, rz, z, ry, y, rx, x); }

/* TGPoint3 products (0045e8d0: v * M = MultiplyBy; 0045e950: M * v = MultiplyVector) */
void eng_TGPoint3_MultMatrix(void *p, void *mat)
{
    float *v = p; const float *m = ((Mat3 *)mat)->m; float x = v[0], y = v[1], z = v[2];
    v[0] = x * m[0] + m[3] * y + m[6] * z; v[1] = m[1] * x + m[4] * y + m[7] * z; v[2] = m[2] * x + m[5] * y + m[8] * z;
}
void eng_TGPoint3_MultMatrixLeft(void *p, void *mat)
{
    float *v = p; const float *m = ((Mat3 *)mat)->m; float x = v[0], y = v[1], z = v[2];
    v[0] = x * m[0] + m[1] * y + m[2] * z; v[1] = m[3] * x + m[4] * y + m[5] * z; v[2] = m[6] * x + m[7] * y + m[8] * z;
}
