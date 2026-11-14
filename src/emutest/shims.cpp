// Plain C entry points for the emulator tests. Each shim takes the logical argument list of
// the original function; pointers to point types are pointers to arrays of floats.
#include <cstdint>
#include <cstring>

#include "ColorA.h"
#include "Crt.h"
#include "NiPoint3.h"
#include "TGMatrix3.h"
#include "TGPoint3.h"

namespace {

NiPoint3& AsNi(float* v) { return *reinterpret_cast<NiPoint3*>(v); }
const NiPoint3& AsNi(const float* v) { return *reinterpret_cast<const NiPoint3*>(v); }
TGPoint3& AsTG(float* v) { return *reinterpret_cast<TGPoint3*>(v); }
const TGPoint3& AsTG(const float* v) { return *reinterpret_cast<const TGPoint3*>(v); }
TGColorA& AsColor(float* v) { return *reinterpret_cast<TGColorA*>(v); }

void LoadUnits(const float* units)
{
    std::memcpy(&NiPoint3::unitX, units, 12);
    std::memcpy(&NiPoint3::unitY, units + 3, 12);
    std::memcpy(&NiPoint3::unitZ, units + 6, 12);
}

void EnsureFastSqrtTable()
{
    if (NiPoint3::fastSqrtTable == nullptr)
        NiPoint3::InitFastSqrtTable();
}

}  // namespace

extern "C" {

int bc_crt_rand(std::uint32_t* seed)
{
    crt::SetRandSeed(*seed);
    int value = crt::rand();
    *seed = crt::GetRandSeed();
    return value;
}

void bc_NiPoint3_InitUnitX(float* out)
{
    NiPoint3::unitX = NiPoint3(9.0f, 9.0f, 9.0f);
    NiPoint3::InitUnitX();
    std::memcpy(out, &NiPoint3::unitX, 12);
}

void bc_NiPoint3_InitUnitY(float* out)
{
    NiPoint3::unitY = NiPoint3(9.0f, 9.0f, 9.0f);
    NiPoint3::InitUnitY();
    std::memcpy(out, &NiPoint3::unitY, 12);
}

void bc_NiPoint3_InitUnitZ(float* out)
{
    NiPoint3::unitZ = NiPoint3(9.0f, 9.0f, 9.0f);
    NiPoint3::InitUnitZ();
    std::memcpy(out, &NiPoint3::unitZ, 12);
}

void bc_NiPoint3_InitZero(float* out)
{
    NiPoint3::zero = NiPoint3(9.0f, 9.0f, 9.0f);
    NiPoint3::InitZero();
    std::memcpy(out, &NiPoint3::zero, 12);
}

void bc_NiPoint2_InitZero(float* out)
{
    NiPoint2::zero = NiPoint2{9.0f, 9.0f};
    NiPoint2::InitZero();
    std::memcpy(out, &NiPoint2::zero, 8);
}

void bc_NiPoint3_InitFastSqrtTable(std::uint32_t* out)
{
    NiPoint3::InitFastSqrtTable();
    std::memcpy(out, NiPoint3::fastSqrtTable, 0x400);
    NiPoint3::FreeFastSqrtTable();
}

int bc_NiPoint3_FreeFastSqrtTable()
{
    NiPoint3::fastSqrtTable = static_cast<std::uint32_t*>(operator_new(0x400));
    NiPoint3::FreeFastSqrtTable();
    return NiPoint3::fastSqrtTable == nullptr;
}

#define MODEL_SHIMS(Name, Member)                                                     \
    void bc_TGPoint3_Init##Name(float* out)                                           \
    {                                                                                 \
        TGPoint3::Member = TGPoint3(9.0f, 9.0f, 9.0f);                                \
        TGPoint3::Init##Name();                                                       \
        std::memcpy(out, &TGPoint3::Member, 12);                                      \
    }                                                                                 \
    void bc_TGPoint3_Get##Name(const float* global, float* out)                       \
    {                                                                                 \
        std::memcpy(&TGPoint3::Member, global, 12);                                   \
        TGPoint3::Get##Name(&AsTG(out));                                              \
    }

#define INIT_ONLY(Name, Member)                                                       \
    void bc_TGPoint3_Init##Name(float* out)                                           \
    {                                                                                 \
        TGPoint3::Member = TGPoint3(9.0f, 9.0f, 9.0f);                                \
        TGPoint3::Init##Name();                                                       \
        std::memcpy(out, &TGPoint3::Member, 12);                                      \
    }

MODEL_SHIMS(ModelForward, modelForward)
MODEL_SHIMS(ModelBackward, modelBackward)
MODEL_SHIMS(ModelUp, modelUp)
MODEL_SHIMS(ModelDown, modelDown)
MODEL_SHIMS(ModelRight, modelRight)
MODEL_SHIMS(ModelLeft, modelLeft)
INIT_ONLY(Zero, zero)

void bc_NiPoint3_NiPoint3(float* out, float x, float y, float z) { AsNi(out) = NiPoint3(x, y, z); }

double bc_NiPoint3_Length(const float* v) { return AsNi(v).Length(); }
double bc_NiPoint3_SqrLength(const float* v) { return AsNi(v).SqrLength(); }
double bc_NiPoint3_Dot(const float* a, const float* b) { return AsNi(a).Dot(AsNi(b)); }
double bc_NiPoint3_Unitize(float* v) { return AsNi(v).Unitize(); }
void bc_NiPoint3_Cross(const float* a, const float* b, float* out) { AsNi(out) = AsNi(a).Cross(AsNi(b)); }

void bc_NiPoint3_DivideByScalar(const float* v, float* out, float s) { AsNi(v).DivideByScalar(&AsNi(out), s); }

void bc_NiPoint3_UnitCross(const float* a, float* out, const float* b)
{
    AsNi(a).UnitCross(&AsNi(out), &AsNi(b));
}

void bc_NiPoint3_Perpendicular(const float* units, const float* v, float* out)
{
    LoadUnits(units);
    AsNi(out) = AsNi(v).Perpendicular();
}

void bc_TGMatrix3_MatrixTimesPoint(const float* m, float* out, const float* p)
{
    reinterpret_cast<const TGMatrix3*>(m)->MatrixTimesPoint(&AsTG(out), &AsTG(p));
}

void bc_TGMatrix3_PointTimesMatrix(float* out, const float* p, const float* m)
{
    TGMatrix3::PointTimesMatrix(&AsTG(out), &AsTG(p), reinterpret_cast<const TGMatrix3*>(m));
}

void bc_TGPoint3_GetRandomUnitVector(std::uint32_t* seed, float* out)
{
    crt::SetRandSeed(*seed);
    TGPoint3::GetRandomUnitVector(&AsTG(out));
    *seed = crt::GetRandSeed();
}

void bc_TGPoint3_Cross(const float* a, float* out, const float* b) { AsTG(a).Cross(&AsTG(out), &AsTG(b)); }
void bc_TGPoint3_UnitCross(const float* a, float* out, const float* b) { AsTG(a).UnitCross(&AsTG(out), &AsTG(b)); }

void bc_TGPoint3_GetAlignedComponent(const float* zeroVector, const float* a, float* out, const float* b)
{
    std::memcpy(&TGPoint3::zero, zeroVector, 12);
    AsTG(a).GetAlignedComponent(&AsTG(out), &AsTG(b));
}

void bc_TGPoint3_GetPerpendicularComponent(const float* zeroVector, const float* a, float* out, const float* b)
{
    std::memcpy(&TGPoint3::zero, zeroVector, 12);
    AsTG(a).GetPerpendicularComponent(&AsTG(out), &AsTG(b));
}

void bc_TGPoint3_MultMatrix(float* v, const float* m) { AsTG(v).MultMatrix(reinterpret_cast<const TGMatrix3*>(m)); }
void bc_TGPoint3_MultMatrixLeft(float* v, const float* m)
{
    AsTG(v).MultMatrixLeft(reinterpret_cast<const TGMatrix3*>(m));
}

void bc_TGPoint3_Set(float* v, float x, float y, float z) { AsTG(v).Set(x, y, z); }

void bc_TGPoint3_UnitizeVectors(float* vectors, unsigned count)
{
    EnsureFastSqrtTable();
    TGPoint3::UnitizeVectors(reinterpret_cast<TGPoint3*>(vectors), count);
}

void bc_TGColorA_Copy(float* c, float r, float g, float b, float a) { AsColor(c).Copy(NiColorA(r, g, b, a)); }

// Members that the Appc wrappers perform inline.
float bc_TGPoint3_GetX(const float* v) { return AsTG(v).GetX(); }
float bc_TGPoint3_GetY(const float* v) { return AsTG(v).GetY(); }
float bc_TGPoint3_GetZ(const float* v) { return AsTG(v).GetZ(); }
void bc_TGPoint3_SetX(float* v, float f) { AsTG(v).SetX(f); }
void bc_TGPoint3_SetY(float* v, float f) { AsTG(v).SetY(f); }
void bc_TGPoint3_SetZ(float* v, float f) { AsTG(v).SetZ(f); }
void bc_TGPoint3_SetXYZ(float* v, float x, float y, float z) { AsTG(v).SetXYZ(x, y, z); }
void bc_TGPoint3_Add(float* a, const float* b) { AsTG(a).Add(AsTG(b)); }
void bc_TGPoint3_Subtract(float* a, const float* b) { AsTG(a).Subtract(AsTG(b)); }
void bc_TGPoint3_Scale(float* a, float s) { AsTG(a).Scale(s); }
void bc_TGPoint3_UnitizeVector(float* a)
{
    EnsureFastSqrtTable();
    AsTG(a).UnitizeVector();
}
double bc_TGPoint3_VectorLength(const float* a)
{
    EnsureFastSqrtTable();
    return AsTG(a).VectorLength();
}

void bc_TGColorA_new(float* out) { AsColor(out) = TGColorA(); }
void bc_NiColorA_new(float r, float g, float b, float a, float* out)
{
    *reinterpret_cast<NiColorA*>(out) = NiColorA(r, g, b, a);
}
void bc_TGColorA_SetRGBA(float* c, float r, float g, float b, float a) { AsColor(c).SetRGBA(r, g, b, a); }
void bc_TGColorA_ScaleRGB(float* c, float s) { AsColor(c).ScaleRGB(s); }

#define COLOR_ACCESSORS(Letter, Index)                                                                 \
    float bc_TGColorA_Get##Letter(const float* c) { return reinterpret_cast<const TGColorA*>(c)->Get##Letter(); } \
    void bc_TGColorA_Set##Letter(float* c, float f) { AsColor(c).Set##Letter(f); }

COLOR_ACCESSORS(R, 0)
COLOR_ACCESSORS(G, 1)
COLOR_ACCESSORS(B, 2)
COLOR_ACCESSORS(A, 3)

}  // extern "C"

// Field access and construction for the wrapper tests.
extern "C" {

#define FIELD_SHIMS(Prefix, Field, Index)                                                    \
    float bc_##Prefix##_##Field##_get(const float* v) { return v[Index]; }                         \
    void bc_##Prefix##_##Field##_set(float* v, float f) { v[Index] = f; }

FIELD_SHIMS(NiColorA, r, 0)
FIELD_SHIMS(NiColorA, g, 1)
FIELD_SHIMS(NiColorA, b, 2)
FIELD_SHIMS(NiColorA, a, 3)
FIELD_SHIMS(NiPoint3, x, 0)
FIELD_SHIMS(NiPoint3, y, 1)
FIELD_SHIMS(NiPoint3, z, 2)

void bc_NiPoint3_new(float x, float y, float z, float* out) { AsNi(out) = NiPoint3(x, y, z); }

void bc_NiPoint3_UnitCross_new(const float* a, const float* b, float* out)
{
    AsNi(a).UnitCross(&AsNi(out), &AsNi(b));
}

void bc_NiPoint3_Perpendicular_new(const float* v, float* out)
{
    NiPoint3::InitUnitX();
    NiPoint3::InitUnitY();
    NiPoint3::InitUnitZ();
    AsNi(out) = AsNi(v).Perpendicular();
}

}  // extern "C"
