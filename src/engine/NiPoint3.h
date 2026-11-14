#pragma once
#include <cstddef>
#include <cstdint>

#include "X87.h"

struct NiPoint3 {
    float x, y, z;

    NiPoint3() = default;
    NiPoint3(float x, float y, float z);

    double Length() const;
    double SqrLength() const;
    double Dot(const NiPoint3& other) const;
    double Unitize();
    NiPoint3 Cross(const NiPoint3& other) const;
    NiPoint3* UnitCross(NiPoint3* out, const NiPoint3* other) const;
    NiPoint3* DivideByScalar(NiPoint3* out, float scalar) const;
    NiPoint3 Perpendicular() const;

    static NiPoint3 unitX;
    static NiPoint3 unitY;
    static NiPoint3 unitZ;
    static NiPoint3 zero;
    static std::uint32_t* fastSqrtTable;

    static void InitUnitX();
    static void InitUnitY();
    static void InitUnitZ();
    static void InitZero();
    static void InitFastSqrtTable();
    static void RegisterFastSqrtTableCleanup();
    static void FreeFastSqrtTable();
};

static_assert(sizeof(NiPoint3) == 12, "NiPoint3 size");
static_assert(offsetof(NiPoint3, x) == 0 && offsetof(NiPoint3, y) == 4 && offsetof(NiPoint3, z) == 8,
              "NiPoint3 layout");

// Sum of squares as the x87 code forms it: (x*x + y*y) + z*z in registers.
inline x87::Reg SumOfSquares(float x, float y, float z)
{
    x87::Reg dx = x, dy = y, dz = z;
    return (dx * dx + dy * dy) + dz * dz;
}

// Length below this value counts as zero (constant at 0x888b58).
constexpr float kNormalizeEpsilon = 1.0e-6f;

// A point of two floats.
struct NiPoint2 {
    float x, y;

    static NiPoint2 zero;

    static void InitZero();
};

static_assert(sizeof(NiPoint2) == 8, "NiPoint2 size");
