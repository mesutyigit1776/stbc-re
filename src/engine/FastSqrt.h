#pragma once
#include <cstdint>
#include <cstring>

// Table-driven square root shared by TGPoint3::UnitizeVector, TGPoint3::VectorLength
// and TGPoint3::UnitizeVectors. Input and result are IEEE single-precision bit patterns.
// The table pointer is the static written by NiPoint3::InitFastSqrtTable (0x9a284c).

namespace fastsqrt {

inline std::uint32_t Float2Bits(float v)
{
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return bits;
}

inline float Bits2Float(std::uint32_t bits)
{
    float v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

// inline in 0x5ee540, 0x5ee640, 0x821830; bits must not be zero
inline std::uint32_t SqrtBits(const std::uint32_t* table, std::uint32_t bits)
{
    std::uint32_t exponent = (bits >> 23) - 0x7f;
    std::uint32_t mantissa = bits & 0x7fffff;
    if (exponent & 1)
        mantissa |= 0x800000;
    std::int32_t halfExponent = static_cast<std::int16_t>(exponent) >> 1;
    std::uint32_t biased = static_cast<std::uint32_t>(halfExponent + 0x7f);
    return table[mantissa >> 16] | (biased << 23);
}

// 0.0f for a zero input, otherwise the approximate square root
inline float Sqrt(const std::uint32_t* table, float value)
{
    std::uint32_t bits = Float2Bits(value);
    return bits == 0 ? 0.0f : Bits2Float(SqrtBits(table, bits));
}

}  // namespace fastsqrt
