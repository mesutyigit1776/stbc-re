#include "NiPoint3.h"

#include <cstdlib>

#include "Crt.h"
#include "FastSqrt.h"

NiPoint3 NiPoint3::unitX;
NiPoint3 NiPoint3::unitY;
NiPoint3 NiPoint3::unitZ;
NiPoint3 NiPoint3::zero;
NiPoint2 NiPoint2::zero;
std::uint32_t* NiPoint3::fastSqrtTable;

using x87::Reg;

static bool IsBelow(Reg a, Reg b) { return !(a >= b); }

// 0x414540
NiPoint3::NiPoint3(float nx, float ny, float nz) : x(nx), y(ny), z(nz) {}

// 0x4145b0, also inline in the Appc wrapper of NiPoint3.Length
double NiPoint3::Length() const { return StoreD(Sqrt(SumOfSquares(x, y, z))); }

// inline in the Appc wrapper of NiPoint3.SqrLength (0x5be220)
double NiPoint3::SqrLength() const { return StoreD(SumOfSquares(x, y, z)); }

// inline in the Appc wrapper of NiPoint3.Dot (0x5be2a0)
double NiPoint3::Dot(const NiPoint3& other) const
{
    Reg dz = Reg(z) * other.z;
    Reg dy = Reg(y) * other.y;
    Reg dx = Reg(x) * other.x;
    return StoreD((dz + dy) + dx);
}

// inline in the Appc wrapper of NiPoint3.Unitize (0x5be350)
double NiPoint3::Unitize()
{
    Reg length = Sqrt(SumOfSquares(x, y, z));
    if (!(length > kNormalizeEpsilon)) {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        return 0.0;
    }
    Reg inverse = 1.0 / length;
    x = StoreF(inverse * x);
    y = StoreF(inverse * y);
    z = StoreF(inverse * z);
    return StoreD(length);
}

// inline in the Appc wrapper of NiPoint3.Cross (0x5be430)
NiPoint3 NiPoint3::Cross(const NiPoint3& other) const
{
    Reg cx = Reg(other.z) * y - Reg(other.y) * z;
    Reg cy = Reg(z) * other.x - Reg(x) * other.z;
    Reg cz = Reg(other.y) * x - Reg(other.x) * y;
    return NiPoint3(StoreF(cx), StoreF(cy), StoreF(cz));
}

// 0x422770
NiPoint3* NiPoint3::UnitCross(NiPoint3* out, const NiPoint3* other) const
{
    Reg cx = Reg(y) * other->z - Reg(other->y) * z;
    Reg cy = Reg(z) * other->x - Reg(x) * other->z;
    Reg cz = Reg(other->y) * x - Reg(y) * other->x;
    float storedY = StoreF(cy);
    float storedZ = StoreF(cz);
    Reg length = Sqrt((cz * cz + cy * cy) + cx * cx);
    if (!(length > kNormalizeEpsilon)) {
        out->x = out->y = out->z = 0.0f;
        return out;
    }
    Reg inverse = 1.0 / length;
    out->x = StoreF(inverse * cx);
    out->y = StoreF(storedY * inverse);
    out->z = StoreF(inverse * storedZ);
    return out;
}

// 0x414580
NiPoint3* NiPoint3::DivideByScalar(NiPoint3* out, float scalar) const
{
    Reg inverse = Reg(1.0) / scalar;
    float storedInverse = StoreF(inverse);
    Reg oz = inverse * z;
    Reg oy = Reg(storedInverse) * y;
    Reg ox = Reg(storedInverse) * x;
    out->x = StoreF(ox);
    out->y = StoreF(oy);
    out->z = StoreF(oz);
    return out;
}

// inline in the Appc wrapper of NiPoint3.Perpendicular (0x5be680)
NiPoint3 NiPoint3::Perpendicular() const
{
    NiPoint3 result;
    Reg ax = Abs(Reg(x));
    Reg ay = Abs(Reg(y));
    Reg az = Abs(Reg(z));
    if (IsBelow(ax, ay)) {
        if (IsBelow(az, ax)) {
            Reg cx = Reg(unitZ.z) * y - Reg(unitZ.y) * z;
            Reg cy = Reg(unitZ.x) * z - Reg(unitZ.z) * x;
            Reg cz = Reg(unitZ.y) * x - Reg(unitZ.x) * y;
            NiPoint3 cross(StoreF(cx), StoreF(cy), StoreF(cz));
            Reg length = Sqrt((Reg(cross.x) * cross.x + cz * cz) + cy * cy);
            if (length > kNormalizeEpsilon)
                cross.DivideByScalar(&result, StoreF(length));
            else
                result.x = result.y = result.z = 0.0f;
        } else {
            Reg cx = Reg(unitX.z) * y - Reg(unitX.y) * z;
            Reg cy = Reg(unitX.x) * z - Reg(unitX.z) * x;
            Reg cz = Reg(unitX.y) * x - Reg(unitX.x) * y;
            NiPoint3 cross(StoreF(cx), StoreF(cy), StoreF(cz));
            Reg length = Sqrt((cy * cy + cz * cz) + Reg(cross.x) * cross.x);
            if (length > kNormalizeEpsilon)
                cross.DivideByScalar(&result, StoreF(length));
            else
                result = NiPoint3(0.0f, 0.0f, 0.0f);
        }
    } else if (IsBelow(az, ay)) {
        float cz = StoreF(Reg(unitZ.y) * x - Reg(unitZ.x) * y);
        float cy = StoreF(Reg(unitZ.x) * z - Reg(unitZ.z) * x);
        float cx = StoreF(Reg(unitZ.z) * y - Reg(unitZ.y) * z);
        NiPoint3 cross(cx, cy, cz);
        Reg length = cross.Length();
        if (length > kNormalizeEpsilon)
            cross.DivideByScalar(&result, StoreF(length));
        else
            result = NiPoint3(0.0f, 0.0f, 0.0f);
    } else {
        UnitCross(&result, &unitY);
    }
    return result;
}

// 0x8119e0
void NiPoint3::InitZero() { zero = NiPoint3(0.0f, 0.0f, 0.0f); }

// 0x811a00
void NiPoint3::InitUnitX() { unitX = NiPoint3(1.0f, 0.0f, 0.0f); }

// 0x811a20
void NiPoint3::InitUnitY() { unitY = NiPoint3(0.0f, 1.0f, 0.0f); }

// 0x811a40
void NiPoint3::InitUnitZ() { unitZ = NiPoint3(0.0f, 0.0f, 1.0f); }

// 0x811a60
void NiPoint3::InitFastSqrtTable()
{
    std::uint32_t* table = static_cast<std::uint32_t*>(operator_new(0x400));
    for (std::uint32_t i = 0; i < 0x80; ++i) {
        float low = fastsqrt::Bits2Float((i | 0x3f80) << 16);
        table[i] = fastsqrt::Float2Bits(StoreF(Sqrt(Reg(low)))) & 0x7fffff;
        float high = fastsqrt::Bits2Float((i | 0x4000) << 16);
        table[i + 0x80] = fastsqrt::Float2Bits(StoreF(Sqrt(Reg(high)))) & 0x7fffff;
    }
    fastSqrtTable = table;
}

// 0x811ae0
void NiPoint3::RegisterFastSqrtTableCleanup() { std::atexit(&NiPoint3::FreeFastSqrtTable); }

// 0x811af0
void NiPoint3::FreeFastSqrtTable()
{
    operator_delete(fastSqrtTable);
    fastSqrtTable = nullptr;
}

// 0x820010
void NiPoint2::InitZero() { zero = NiPoint2{0.0f, 0.0f}; }
