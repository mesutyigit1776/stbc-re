#include "TGPoint3.h"

#include "Crt.h"
#include "FastSqrt.h"
#include "TGMatrix3.h"
#include "TGStream.h"

using x87::Reg;

TGPoint3 TGPoint3::modelForward;
TGPoint3 TGPoint3::modelBackward;
TGPoint3 TGPoint3::modelUp;
TGPoint3 TGPoint3::modelDown;
TGPoint3 TGPoint3::modelRight;
TGPoint3 TGPoint3::modelLeft;
TGPoint3 TGPoint3::zero;

// inline in the Appc wrapper of TGPoint3.SetXYZ (0x5edd40)
void TGPoint3::SetXYZ(float nx, float ny, float nz)
{
    x = nx;
    y = ny;
    z = nz;
}

// inline in the Appc wrapper of TGPoint3.Add (0x5ee0b0)
void TGPoint3::Add(const TGPoint3& other)
{
    x = StoreF(Reg(other.x) + x);
    y = StoreF(Reg(other.y) + y);
    z = StoreF(Reg(other.z) + z);
}

// inline in the Appc wrapper of TGPoint3.Subtract (0x5ee160)
void TGPoint3::Subtract(const TGPoint3& other)
{
    x = StoreF(Reg(x) - other.x);
    y = StoreF(Reg(y) - other.y);
    z = StoreF(Reg(z) - other.z);
}

// inline in the Appc wrapper of TGPoint3.Scale (0x5ee210)
void TGPoint3::Scale(float scale)
{
    x = StoreF(Reg(x) * scale);
    y = StoreF(Reg(y) * scale);
    z = StoreF(Reg(z) * scale);
}

// inline in the Appc wrapper of TGPoint3.UnitizeVector (0x5ee540)
void TGPoint3::UnitizeVector()
{
    float sum = StoreF(SumOfSquares(x, y, z));
    Reg factor = fastsqrt::Float2Bits(sum) == 0 ? Reg(0.0) : 1.0 / Reg(fastsqrt::Sqrt(fastSqrtTable, sum));
    x = StoreF(factor * x);
    y = StoreF(factor * y);
    z = StoreF(factor * z);
}

// inline in the Appc wrapper of TGPoint3.VectorLength (0x5ee640)
double TGPoint3::VectorLength() const
{
    float sum = StoreF(SumOfSquares(x, y, z));
    return fastsqrt::Sqrt(fastSqrtTable, sum);
}

// 0x5ee360
void TGPoint3::Set(float nx, float ny, float nz)
{
    x = nx;
    y = ny;
    z = nz;
}

// 0x45c1a0
TGPoint3* TGPoint3::Cross(TGPoint3* out, const TGPoint3* other) const
{
    Reg cx = Reg(y) * other->z - Reg(other->y) * z;
    Reg cy = Reg(z) * other->x - Reg(x) * other->z;
    Reg cz = Reg(other->y) * x - Reg(y) * other->x;
    out->x = StoreF(cx);
    out->y = StoreF(cy);
    out->z = StoreF(cz);
    return out;
}

// 0x581e60
TGPoint3* TGPoint3::UnitCross(TGPoint3* out, const TGPoint3* other) const
{
    Reg cx = Reg(y) * other->z - Reg(other->y) * z;
    Reg cy = Reg(z) * other->x - Reg(x) * other->z;
    Reg cz = Reg(other->y) * x - Reg(y) * other->x;
    float storedY = StoreF(cy);
    float storedZ = StoreF(cz);
    Reg length = Sqrt((Reg(storedY) * storedY + cz * cz) + cx * cx);
    if (!(length > kNormalizeEpsilon)) {
        out->x = out->y = out->z = 0.0f;
        return out;
    }
    float inverse = StoreF(1.0 / length);
    out->x = StoreF(cx * inverse);
    out->y = StoreF(Reg(storedY) * inverse);
    out->z = StoreF(Reg(storedZ) * inverse);
    return out;
}

// 0x45e980
TGPoint3* TGPoint3::GetAlignedComponent(TGPoint3* out, const TGPoint3* other) const
{
    Reg dot = (Reg(y) * other->y + Reg(z) * other->z) + Reg(other->x) * x;
    Reg bx = other->x, by = other->y, bz = other->z;
    Reg lengthSquared = (bx * bx + by * by) + bz * bz;
    if (lengthSquared == 0.0 || lengthSquared != lengthSquared) {
        *out = zero;
        return out;
    }
    Reg inverse = 1.0 / lengthSquared;
    float unitX = StoreF(inverse * bx);
    float unitY = StoreF(inverse * by);
    Reg unitZ = inverse * bz;
    out->x = StoreF(Reg(unitX) * dot);
    out->y = StoreF(Reg(unitY) * dot);
    out->z = StoreF(unitZ * dot);
    return out;
}

// 0x45ea50
TGPoint3* TGPoint3::GetPerpendicularComponent(TGPoint3* out, const TGPoint3* other) const
{
    TGPoint3 aligned;
    GetAlignedComponent(&aligned, other);
    out->x = StoreF(Reg(x) - aligned.x);
    out->y = StoreF(Reg(y) - aligned.y);
    out->z = StoreF(Reg(z) - aligned.z);
    return out;
}

// 0x45e8d0
void TGPoint3::MultMatrix(const TGMatrix3* m)
{
    TGPoint3 source = *this;
    TGPoint3 product;
    TGMatrix3::PointTimesMatrix(&product, &source, m);
    *this = product;
}

// 0x45e950
void TGPoint3::MultMatrixLeft(const TGMatrix3* m)
{
    TGPoint3 product;
    m->MatrixTimesPoint(&product, this);
    *this = product;
}

// 0x811b10
void TGPoint3::LoadBinary(TGStream* stream)
{
    float* component = &x;
    for (int i = 0; i < 3; ++i)
        stream->reader->Read(component + i, 4);
}

// 0x811b50
void TGPoint3::SaveBinary(TGStream* stream) const
{
    const float* component = &x;
    for (int i = 0; i < 3; ++i)
        stream->writer->Write(component + i, 4);
}

// 0x45e7b0
TGPoint3* TGPoint3::GetRandomUnitVector(TGPoint3* out)
{
    constexpr float kScale = 0x1.0002p-14f;
    Reg leftover = 0.0;
    float rx, ry, rz;
    do {
        rx = StoreF(Reg(crt::rand()) * kScale - 1.0);
        ry = StoreF(Reg(crt::rand()) * kScale - 1.0);
        Reg zExact = Reg(crt::rand()) * kScale - 1.0;
        rz = StoreF(zExact);
        Reg length = Sqrt((Reg(ry) * ry + zExact * zExact) + Reg(rx) * rx);
        if (length > kNormalizeEpsilon) {
            float inverse = StoreF(1.0 / length);
            rx = StoreF(Reg(inverse) * rx);
            leftover = Reg(inverse) * ry;
            rz = StoreF(Reg(inverse) * rz);
        } else {
            leftover = 0.0;
            rx = 0.0f;
            rz = 0.0f;
        }
    } while (!(((Reg(rz) * rz + leftover * leftover) + Reg(rx) * rx) >= 0.5));
    out->x = rx;
    out->y = StoreF(leftover);
    out->z = rz;
    return out;
}

// 0x434cd0
TGPoint3* TGPoint3::GetModelForward(TGPoint3* out)
{
    *out = modelForward;
    return out;
}

// 0x5ed6d0
TGPoint3* TGPoint3::GetModelBackward(TGPoint3* out)
{
    *out = modelBackward;
    return out;
}

// 0x5022e0
TGPoint3* TGPoint3::GetModelUp(TGPoint3* out)
{
    *out = modelUp;
    return out;
}

// 0x5ed7c0
TGPoint3* TGPoint3::GetModelDown(TGPoint3* out)
{
    *out = modelDown;
    return out;
}

// 0x502310
TGPoint3* TGPoint3::GetModelRight(TGPoint3* out)
{
    *out = modelRight;
    return out;
}

// 0x5ed8b0
TGPoint3* TGPoint3::GetModelLeft(TGPoint3* out)
{
    *out = modelLeft;
    return out;
}

// 0x821830
void TGPoint3::UnitizeVectors(TGPoint3* vectors, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        TGPoint3& v = vectors[i];
        float sum = StoreF(SumOfSquares(v.x, v.y, v.z));
        Reg factor =
            fastsqrt::Float2Bits(sum) == 0 ? Reg(0.0) : 1.0 / Reg(fastsqrt::Sqrt(NiPoint3::fastSqrtTable, sum));
        v.x = StoreF(factor * v.x);
        v.y = StoreF(factor * v.y);
        v.z = StoreF(factor * v.z);
    }
}

// 0x45e460
void TGPoint3::InitModelForward() { modelForward = TGPoint3(0.0f, 1.0f, 0.0f); }

// 0x45e4b0
void TGPoint3::InitModelBackward() { modelBackward = TGPoint3(0.0f, -1.0f, 0.0f); }

// 0x45e500
void TGPoint3::InitModelUp() { modelUp = TGPoint3(0.0f, 0.0f, 1.0f); }

// 0x45e550
void TGPoint3::InitModelDown() { modelDown = TGPoint3(0.0f, 0.0f, -1.0f); }

// 0x45e5a0
void TGPoint3::InitModelRight() { modelRight = TGPoint3(1.0f, 0.0f, 0.0f); }

// 0x45e5f0
void TGPoint3::InitModelLeft() { modelLeft = TGPoint3(-1.0f, 0.0f, 0.0f); }

// 0x45e640
void TGPoint3::InitZero() { zero = TGPoint3(0.0f, 0.0f, 0.0f); }
