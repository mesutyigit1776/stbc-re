#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Arithmetic as the x87 unit of stbc.exe does it. Each add, subtract, multiply, divide and square
// root rounds its result to the mantissa width in the precision control field of the control word:
// 53 bits (control word 0x027F, set by the C run time) or 24 bits (0x007F). Loads are exact. A store
// to float or double memory rounds to that format. BC_X87_PRECISION selects the width.

#ifndef BC_X87_PRECISION
#define BC_X87_PRECISION 53
#endif
static_assert(BC_X87_PRECISION == 53 || BC_X87_PRECISION == 24, "BC_X87_PRECISION must be 53 or 24");

namespace x87 {

constexpr int kPrecision = BC_X87_PRECISION;

// Rounds to kPrecision mantissa bits, to nearest even. The exponent range stays that of a double,
// which is wider than the float range, as the extended exponent range of the x87 registers is.
// Subnormal doubles are not handled as the x87 does. The functions of the engine never reach them.
inline double RoundToPrecision(double value)
{
    if (kPrecision == 53)
        return value;
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    if (((bits >> 52) & 0x7ff) == 0x7ff)
        return value;
    bits += 0x0fffffffull + ((bits >> 29) & 1);
    bits &= ~0x1fffffffull;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// One x87 register value.
class Reg {
public:
    Reg(float memory) : value_(memory) {}
    Reg(double memory) : value_(memory) {}
    Reg(int integerMemory) : value_(integerMemory) {}

    friend Reg operator+(Reg a, Reg b) { return Rounded(a.value_ + b.value_); }
    friend Reg operator-(Reg a, Reg b) { return Rounded(a.value_ - b.value_); }
    friend Reg operator*(Reg a, Reg b) { return Rounded(a.value_ * b.value_); }
    friend Reg operator/(Reg a, Reg b) { return Rounded(a.value_ / b.value_); }
    friend Reg operator-(Reg a) { return Reg(-a.value_, Exact{}); }
    friend Reg Sqrt(Reg a) { return Rounded(std::sqrt(a.value_)); }
    friend Reg Abs(Reg a) { return Reg(std::fabs(a.value_), Exact{}); }

    friend bool operator<(Reg a, Reg b) { return a.value_ < b.value_; }
    friend bool operator>(Reg a, Reg b) { return a.value_ > b.value_; }
    friend bool operator<=(Reg a, Reg b) { return a.value_ <= b.value_; }
    friend bool operator>=(Reg a, Reg b) { return a.value_ >= b.value_; }
    friend bool operator==(Reg a, Reg b) { return a.value_ == b.value_; }
    friend bool operator!=(Reg a, Reg b) { return a.value_ != b.value_; }

    friend float StoreF(Reg a) { return static_cast<float>(a.value_); }
    friend double StoreD(Reg a) { return a.value_; }

private:
    struct Exact {};
    Reg(double value, Exact) : value_(value) {}
    static Reg Rounded(double exact) { return Reg(RoundToPrecision(exact), Exact{}); }

    double value_;
};

}  // namespace x87
