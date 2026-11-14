// Deliberately wrong variants. The emulator tests must reject them.
#include <cmath>

#include "X87.h"

extern "C" {

// Rounds the sum to float before the root, as float-only code would.
double bc_NiPoint3_Length_defect(const float* v)
{
    float sum = static_cast<float>((static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1]) +
                                   static_cast<double>(v[2]) * v[2]);
    return std::sqrt(static_cast<double>(sum));
}

// Swaps the operands of the y term, which flips its sign.
void bc_TGPoint3_Cross_defect(const float* a, float* out, const float* b)
{
    double cx = static_cast<double>(a[1]) * b[2] - static_cast<double>(b[1]) * a[2];
    double cy = static_cast<double>(a[0]) * b[2] - static_cast<double>(a[2]) * b[0];
    double cz = static_cast<double>(b[1]) * a[0] - static_cast<double>(a[1]) * b[0];
    out[0] = static_cast<float>(cx);
    out[1] = static_cast<float>(cy);
    out[2] = static_cast<float>(cz);
}

// Adds the last square with a plain double addition, so the 24-bit rounding of that sum is skipped.
// The result equals the original at 53 bits and differs at 24 bits.
double bc_NiPoint3_Length_defect_p24(const float* v)
{
    using x87::Reg;
    Reg dx = v[0], dy = v[1], dz = v[2];
    double partial = StoreD(dx * dx + dy * dy);
    double sum = partial + StoreD(dz * dz);
    return StoreD(Sqrt(Reg(sum)));
}

}  // extern "C"
