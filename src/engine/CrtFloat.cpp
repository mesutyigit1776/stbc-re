#include "CrtFloat.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrtFloatTables.inc"

namespace crt {
namespace {

// A 12-byte extended number of the C run time: five mantissa words, then the sign and exponent word.
using Ld12 = std::uint8_t[12];

// Memory access to the words of an extended number, as the code of the exe reads them.
inline std::uint16_t Get16(const std::uint8_t* b, int offset)
{
    std::uint16_t v;
    std::memcpy(&v, b + offset, sizeof v);
    return v;
}
inline std::uint32_t Get32(const std::uint8_t* b, int offset)
{
    std::uint32_t v;
    std::memcpy(&v, b + offset, sizeof v);
    return v;
}
inline void Set16(std::uint8_t* b, int offset, std::uint16_t v) { std::memcpy(b + offset, &v, sizeof v); }
inline void Set32(std::uint8_t* b, int offset, std::uint32_t v) { std::memcpy(b + offset, &v, sizeof v); }

// 0x86db16. Shifts the 96 bit value left by one bit.
void Shl1(std::uint8_t* x)
{
    std::uint32_t d0 = Get32(x, 0), d1 = Get32(x, 4), d2 = Get32(x, 8);
    Set32(x, 0, d0 << 1);
    Set32(x, 4, (d1 << 1) | (d0 >> 31));
    Set32(x, 8, (d2 << 1) | (d1 >> 31));
}

// 0x86db44. Shifts the 96 bit value right by one bit.
void Shr1(std::uint8_t* x)
{
    std::uint32_t d0 = Get32(x, 0), d1 = Get32(x, 4), d2 = Get32(x, 8);
    Set32(x, 4, (d1 >> 1) | (d2 << 31));
    Set32(x, 0, (d0 >> 1) | (d1 << 31));
    Set32(x, 8, d2 >> 1);
}

// 0x86dab8. Adds the 96 bit value src to dst.
void Add96(std::uint8_t* dst, const std::uint8_t* src)
{
    std::uint64_t sum = static_cast<std::uint64_t>(Get32(dst, 0)) + Get32(src, 0);
    Set32(dst, 0, static_cast<std::uint32_t>(sum));
    sum = static_cast<std::uint64_t>(Get32(dst, 4)) + Get32(src, 4) + (sum >> 32);
    Set32(dst, 4, static_cast<std::uint32_t>(sum));
    Set32(dst, 8, Get32(dst, 8) + Get32(src, 8) + static_cast<std::uint32_t>(sum >> 32));
}

// 0x86decb. Multiplies a by b. The low words of the product are dropped.
void Ld12Mul(std::uint8_t* a, const std::uint8_t* b)
{
    const std::uint16_t signExpA = Get16(a, 10), signExpB = Get16(b, 10);
    const std::uint32_t sign = (signExpA ^ signExpB) & 0x8000;
    const std::uint32_t expA = signExpA & 0x7fff, expB = signExpB & 0x7fff;
    std::uint32_t e = expA + expB;
    const auto overflow = [&]() {
        Set32(a, 4, 0);
        Set32(a, 0, 0);
        Set32(a, 8, (sign != 0 ? 0x80000000u : 0u) + 0x7fff8000u);
    };
    const auto zero = [&]() {
        Set32(a, 8, 0);
        Set32(a, 4, 0);
        Set32(a, 0, 0);
    };
    if (expA >= 0x7fff || expB >= 0x7fff || static_cast<std::uint16_t>(e) > 0xbffd) {
        overflow();
        return;
    }
    if (static_cast<std::uint16_t>(e) <= 0x3fbf) {
        zero();
        return;
    }
    if (expA == 0) {
        ++e;
        if ((Get32(a, 8) & 0x7fffffff) == 0 && Get32(a, 4) == 0 && Get32(a, 0) == 0) {
            Set16(a, 10, 0);
            return;
        }
    }
    if (expB == 0) {
        ++e;
        if ((Get32(b, 8) & 0x7fffffff) == 0 && Get32(b, 4) == 0 && Get32(b, 0) == 0) {
            zero();
            return;
        }
    }

    std::uint8_t r[16] = {};
    for (int i = 0; i < 5; ++i) {
        for (int k = 0; k < 5 - i; ++k) {
            const std::uint32_t product = static_cast<std::uint32_t>(Get16(a, 2 * (i + k))) * Get16(b, 2 * (4 - k));
            const std::uint32_t before = Get32(r, 2 * i);
            const std::uint32_t after = before + product;
            Set32(r, 2 * i, after);
            if (after < before)
                Set16(r, 2 * (i + 2), static_cast<std::uint16_t>(Get16(r, 2 * (i + 2)) + 1));
        }
    }

    e += 0xc002;
    std::uint32_t sticky = 0;
    if (static_cast<std::int16_t>(e) > 0) {
        do {
            if ((r[11] & 0x80) != 0)
                break;
            Shl1(r);
            e += 0xffff;
        } while (static_cast<std::int16_t>(e) > 0);
    }
    if (!(static_cast<std::int16_t>(e) > 0)) {
        e += 0xffff;
        if (static_cast<std::int16_t>(e) < 0) {
            std::int32_t count = -static_cast<std::int16_t>(e);
            e += count;
            while (count-- > 0) {
                if ((r[0] & 1) != 0)
                    ++sticky;
                Shr1(r);
            }
            if (sticky != 0)
                r[0] |= 1;
        }
    }

    const std::uint16_t low = Get16(r, 0);
    if (low > 0x8000 || (Get32(r, 0) & 0x1ffff) == 0x18000) {
        if (Get32(r, 2) != 0xffffffffu) {
            Set32(r, 2, Get32(r, 2) + 1);
        } else {
            Set32(r, 2, 0);
            if (Get32(r, 6) != 0xffffffffu) {
                Set32(r, 6, Get32(r, 6) + 1);
            } else {
                Set32(r, 6, 0);
                if (Get16(r, 10) == 0xffff) {
                    ++e;
                    Set16(r, 10, 0x8000);
                } else {
                    Set16(r, 10, static_cast<std::uint16_t>(Get16(r, 10) + 1));
                }
            }
        }
    }
    if (static_cast<std::uint16_t>(e) >= 0x7fff) {
        overflow();
        return;
    }
    Set16(a, 0, Get16(r, 2));
    Set32(a, 2, Get32(r, 4));
    Set32(a, 6, Get32(r, 8));
    Set16(a, 10, static_cast<std::uint16_t>(e | sign));
}

// 0x86e0eb. Multiplies the value by 10 to the power of pow. With tag 0 the lowest word is cleared first.
void MultTenPow12(std::uint8_t* ld, int pow, int tag)
{
    if (pow == 0)
        return;
    const unsigned char(*table)[12] = kPow10Pos;
    if (pow < 0) {
        table = kPow10Neg;
        pow = -pow;
    }
    if (tag == 0)
        Set16(ld, 0, 0);
    for (int group = 0; pow != 0; ++group) {
        const int index = pow & 7;
        pow >>= 3;
        if (index == 0)
            continue;
        const std::uint8_t* entry = table[7 * group + index - 1];
        std::uint8_t copy[12];
        if (Get16(entry, 0) >= 0x8000) {
            std::memcpy(copy, entry, 12);
            Set32(copy, 2, Get32(copy, 2) - 1);
            entry = copy;
        }
        Ld12Mul(ld, entry);
    }
}

// The decimal digits of a number, as $I10_OUTPUT makes them: the decimal point position, the sign, the digit count and the text.
struct OutWork {
    std::int16_t decpt;
    char sign;
    std::uint8_t nbytes;
    char digits[28];
};

// Inline in 0x86dc38 (0x86dcaf).
void SetZeroWork(OutWork& w)
{
    w.decpt = 0;
    w.sign = ' ';
    w.nbytes = 1;
    w.digits[0] = '0';
    w.digits[1] = 0;
}

// 0x86dc38. The call of the exe (from 0x86c611) asks for 17 digits and uses no flags.
int I10Output(std::uint64_t mantissa, std::uint16_t signExp, OutWork& w)
{
    static const int kDigits = 17;
    static const std::uint8_t kOneTenth[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xfb, 0x3f};
    const std::uint32_t lo = static_cast<std::uint32_t>(mantissa), hi = static_cast<std::uint32_t>(mantissa >> 32);
    const std::uint32_t sign = signExp & 0x8000, exp = signExp & 0x7fff;
    w.sign = sign != 0 ? '-' : ' ';
    if (exp == 0 && hi == 0 && lo == 0) {
        SetZeroWork(w);
        return 1;
    }
    if (exp == 0x7fff) {
        w.decpt = 1;
        const char* text;
        if (!(hi == 0x80000000u && lo == 0) && (hi & 0x40000000u) == 0) {
            text = "1#SNAN";
        } else if (sign != 0 && hi == 0xc0000000u && lo == 0) {
            text = "1#IND";
        } else if (sign != 0 && hi == 0xc0000000u) {
            text = "1#QNAN";
        } else if (hi == 0x80000000u && lo == 0) {
            text = "1#INF";
        } else {
            text = "1#QNAN";
        }
        std::strcpy(w.digits, text);
        w.nbytes = static_cast<std::uint8_t>(std::strlen(text));
        return 0;
    }

    std::uint32_t estimate = ((exp >> 8) + (hi >> 24) * 2) * 0x4d + exp * 0x4d10 + 0xecbced0cu;
    std::int32_t decimal = static_cast<std::int32_t>(estimate) >> 16;
    Ld12 ld = {};
    Set32(ld, 2, lo);
    Set32(ld, 6, hi);
    Set16(ld, 10, static_cast<std::uint16_t>(exp));
    MultTenPow12(ld, -static_cast<std::int16_t>(decimal), 1);
    if (Get16(ld, 10) >= 0x3fff) {
        ++decimal;
        Ld12Mul(ld, kOneTenth);
    }
    w.decpt = static_cast<std::int16_t>(decimal);
    std::int32_t shift = static_cast<std::int32_t>(Get16(ld, 10)) - 0x3ffe;
    Set16(ld, 10, 0);
    for (int i = 0; i < 8; ++i)
        Shl1(ld);
    if (shift < 0) {
        std::uint32_t count = static_cast<std::uint32_t>(-shift) & 0xff;
        while (count > 0) {
            Shr1(ld);
            --count;
        }
    }

    char* p = w.digits;
    for (int i = 0; i < kDigits + 1; ++i) {
        Ld12 copy;
        std::memcpy(copy, ld, 12);
        Shl1(ld);
        Shl1(ld);
        Add96(ld, copy);
        Shl1(ld);
        *p++ = static_cast<char>(ld[11] + '0');
        ld[11] = 0;
    }

    char* start = w.digits;
    char* q = p - 2;
    if (p[-1] >= '5') {  // B0016
        for (;;) {
            if (q < start) {
                ++q;
                ++w.decpt;
                break;
            }
            if (*q != '9')
                break;
            *q = '0';
            --q;
        }
        ++*q;
    } else {
        while (q >= start && *q == '0')
            --q;
        if (q < start) {
            SetZeroWork(w);
            return 1;
        }
    }
    w.nbytes = static_cast<std::uint8_t>(q - start + 1);
    w.digits[w.nbytes] = 0;
    return 1;
}

// 0x86c66d. Makes the extended number of a double. The sign of a zero is lost.
void DoubleToExtended(std::uint64_t bits, std::uint64_t& mantissa, std::uint16_t& signExp)
{
    const std::uint32_t high = static_cast<std::uint32_t>(bits >> 32), low = static_cast<std::uint32_t>(bits);
    const std::uint32_t sign = (high >> 16) & 0x8000;
    const std::uint32_t exp = (high >> 20) & 0x7ff;
    const std::uint32_t fraction = high & 0xfffff;
    std::uint32_t implicit = 0x80000000u, newExp;
    if (exp == 0) {
        if (fraction == 0 && low == 0) {  // B0015
            mantissa = 0;
            signExp = 0;
            return;
        }
        newExp = 0x3c01;
        implicit = 0;
    } else if (exp == 0x7ff) {
        newExp = 0x7fff;
    } else {
        newExp = exp + 0x3c00;
    }
    std::uint32_t hi = (low >> 21) | (fraction << 11) | implicit;
    std::uint32_t lo = low << 11;
    while ((hi & 0x80000000u) == 0) {
        hi = (hi << 1) | (lo >> 31);
        lo <<= 1;
        newExp += 0xffff;
    }
    mantissa = (static_cast<std::uint64_t>(hi) << 32) | lo;
    signExp = static_cast<std::uint16_t>(sign | (newExp & 0xffff));
}

// 0x861b2b. Moves the text right by dist characters.
void Shift(char* text, int dist)
{
    if (dist != 0)
        std::memmove(text + dist, text, std::strlen(text) + 1);
}

// 0x86c59a. Rounds the digits of the record to the given digit count and puts the text at buffer.
void FptoStr(char* buffer, int digits, StrFlt* flt)
{
    const char* source = flt->mantissa;
    buffer[0] = '0';
    char* p = buffer + 1;
    int rounding = digits;
    if (digits > 0) {
        rounding = 0;
        for (int i = digits; i > 0; --i) {
            char c = *source;
            if (c != 0)
                ++source;
            else
                c = '0';
            *p++ = c;
        }
    }
    *p = 0;  // B0018: at most 17 digits come from the digit routine
    if (rounding >= 0 && *source >= '5') {
        char* q = p - 1;
        while (*q == '9') {
            *q = '0';
            --q;
        }
        ++*q;
    }
    if (buffer[0] == '1')
        ++flt->decpt;
    else
        std::memmove(buffer, buffer + 1, std::strlen(buffer + 1) + 1);
}

// 0x861889. Writes the exponent form. With the flag the digits are already in place.
char* Cftoe2(char* buffer, int ndec, int caps, const StrFlt* flt, bool afterRounding)
{
    const bool negative = flt->sign == '-';
    if (afterRounding)
        Shift(buffer + (negative ? 1 : 0), ndec > 0 ? 1 : 0);
    char* p = buffer;
    if (negative) {
        *p = '-';
        ++p;
    }
    if (ndec > 0) {
        p[0] = p[1];
        ++p;
        *p = '.';
    }
    char* tail = p + (afterRounding ? 0 : 1) + ndec;
    std::strcpy(tail, "e+000");
    if (caps != 0)
        *tail = 'E';
    char* c = tail + 1;
    if (flt->mantissa[0] != '0') {
        int exponent = flt->decpt - 1;
        if (exponent < 0) {
            exponent = -exponent;
            *c = '-';
        }
        ++c;
        if (exponent >= 100) {
            *c = static_cast<char>(*c + exponent / 100);
            exponent %= 100;
        }
        ++c;
        if (exponent >= 10) {
            *c = static_cast<char>(*c + exponent / 10);
            exponent %= 10;
        }
        c[1] = static_cast<char>(c[1] + exponent);
    }
    return buffer;
}

// 0x8619a0. Writes the fixed form.
char* Cftof2(char* buffer, int ndec, StrFlt* flt, bool afterRounding)
{
    const bool negative = flt->sign == '-';
    const int exponent = flt->decpt - 1;
    if (afterRounding && exponent == ndec) {
        char* z = buffer + exponent + (negative ? 1 : 0);
        z[0] = '0';
        z[1] = 0;
    }
    char* p = buffer;
    if (negative) {
        *p = '-';
        ++p;
    }
    if (flt->decpt <= 0) {
        Shift(p, 1);
        *p = '0';
        ++p;
    } else {
        p += flt->decpt;
    }
    if (ndec > 0) {
        Shift(p, 1);
        *p = '.';
        ++p;
        if (flt->decpt < 0) {
            int count = -flt->decpt;
            if (!afterRounding && ndec < count)
                count = ndec;
            Shift(p, count);
            std::memset(p, '0', static_cast<std::size_t>(count));
        }
    }
    return buffer;
}

// 0x861828.
void Cftoe(const double* arg, char* buffer, int ndec, int caps)
{
    StrFlt flt;
    char work[32];
    FltOut2(*arg, &flt, work);
    FptoStr(buffer + (flt.sign == '-' ? 1 : 0) + (ndec > 0 ? 1 : 0), ndec + 1, &flt);
    Cftoe2(buffer, ndec, caps, &flt, false);
}

// 0x86194b.
void Cftof(const double* arg, char* buffer, int ndec)
{
    StrFlt flt;
    char work[32];
    FltOut2(*arg, &flt, work);
    FptoStr(buffer + (flt.sign == '-' ? 1 : 0), flt.decpt + ndec, &flt);
    Cftof2(buffer, ndec, &flt, false);
}

// 0x861a47.
void Cftog(const double* arg, char* buffer, int precision, int caps)
{
    StrFlt flt;
    char work[32];
    FltOut2(*arg, &flt, work);
    const int before = flt.decpt - 1;
    char* digits = buffer + (flt.sign == '-' ? 1 : 0);
    FptoStr(digits, precision, &flt);
    const int exponent = flt.decpt - 1;
    const bool carried = before < exponent;
    if (exponent >= -4 && exponent < precision) {
        if (carried) {
            char* q = digits;
            while (*q++ != 0) {
            }
            q[-2] = 0;
        }
        Cftof2(buffer, precision, &flt, true);
    } else {
        Cftoe2(buffer, precision, caps, &flt, true);
    }
}

// 0x861784. Removes the zeros at the end of the fraction and the decimal point that is left.
void CropZeros(char* text)
{
    const char point = '.';
    while (*text != 0 && *text != point)
        ++text;
    if (*text == 0)
        return;
    ++text;
    char* tail = text;
    while (*tail != 0 && *tail != 'e' && *tail != 'E')
        ++tail;
    char* last = tail - 1;
    while (*last == '0')
        --last;
    if (*last == point)
        --last;
    for (;;) {
        char c = *tail;
        ++last;
        ++tail;
        *last = c;
        if (c == 0)
            break;
    }
}

// 0x86172a. Puts a decimal point after the digits.
void ForceDecimalPoint(char* text)
{
    char* p = text;
    if (!(*p == 'e' || *p == 'E')) {
        do {
            ++p;
        } while (*p >= '0' && *p <= '9');
    }
    char previous = *p;
    *p++ = '.';
    for (;;) {
        const char next = *p;
        *p = previous;
        previous = next;
        ++p;
        if (p[-1] == 0)
            break;
    }
}

// 0x86c258. True when all 96 bits are zero.
bool IsZero96(const std::uint32_t* x) { return x[0] == 0 && x[1] == 0 && x[2] == 0; }

// 0x86c106. True when the bits after the bit at the position are zero. The bit counts from the top.
bool TailIsZero(const std::uint32_t* x, int position)
{
    const int word = position / 32, rest = position % 32;
    const std::uint32_t mask = ~(0xffffffffu << (31 - rest));  // B0014
    if ((x[word] & mask) != 0)
        return false;
    for (int i = word + 1; i < 3; ++i) {
        if (x[i] != 0)
            return false;
    }
    return true;
}

// 0x86c14f. Adds one at the bit position (counted from the top) and gives the carry out of the top word.
int AddOneAt(std::uint32_t* x, int position)
{
    const int word = position / 32, rest = position % 32;
    const std::uint32_t one = 1u << (31 - rest);
    std::uint32_t sum = x[word] + one;
    int carry = sum < x[word] ? 1 : 0;
    x[word] = sum;
    for (int i = word - 1; i >= 0 && carry != 0; --i) {
        sum = x[i] + 1;
        carry = sum < x[i] ? 1 : 0;
        x[i] = sum;
    }
    return carry;
}

// 0x86c1a5. Rounds the 96 bit value to the number of bits. An exact half stays down.
int RoundBits(std::uint32_t* x, int bits)
{
    const int word = bits / 32, rest = bits % 32;
    const std::uint32_t roundBit = 1u << (31 - rest);
    int carry = 0;
    if ((x[word] & roundBit) != 0 && !TailIsZero(x, bits + 1))  // B0014
        carry = AddOneAt(x, bits - 1);
    x[word] &= 0xffffffffu << (31 - rest);
    for (int i = word + 1; i < 3; ++i)
        x[i] = 0;
    return carry;
}

// 0x86c273. Shifts the 96 bit value right by the number of bits. The first word is the top word.
void ShiftRight96(std::uint32_t* x, int count)
{
    const int words = count / 32, rest = count % 32;
    std::uint32_t carry = 0;
    const std::uint32_t mask = ~(0xffffffffu << rest);
    for (int i = 0; i < 3; ++i) {
        const std::uint32_t value = x[i];
        const std::uint32_t lost = value & mask;
        x[i] = (value >> rest) | carry;
        carry = rest == 0 ? 0 : lost << (32 - rest);
    }
    for (int i = 2; i >= 0; --i)
        x[i] = i < words ? 0 : x[i - words];
}

// 0x86c300 with the table of the double format at 0x97d818. Gives 0 for a number, 1 for an overflow and 2 for an underflow.
int Ld12ToDouble(const std::uint8_t* ld, double* out)
{
    static const int kMaxExp = 0x400, kMinExp = -1023, kMantissaBits = 0x35, kExpBits = 11, kBias = 0x3ff;
    const std::uint16_t signExp = Get16(ld, 10);
    const std::uint32_t sign = signExp & 0x8000;
    int e = (signExp & 0x7fff) - 0x3fff;
    std::uint32_t x[3] = {Get32(ld, 6), Get32(ld, 2), static_cast<std::uint32_t>(Get16(ld, 0)) << 16};
    std::uint32_t exponentField = 0;
    int result = 0;
    if (e == -0x3fff) {
        if (!IsZero96(x)) {
            x[0] = x[1] = x[2] = 0;
            result = 2;
        }
    } else {
        std::uint32_t saved[3] = {x[0], x[1], x[2]};
        if (RoundBits(x, kMantissaBits) != 0)
            ++e;
        if (e < kMinExp - kMantissaBits) {
            x[0] = x[1] = x[2] = 0;
            result = 2;
        } else if (e <= kMinExp) {
            const int shift = kMinExp - e;
            x[0] = saved[0];
            x[1] = saved[1];
            x[2] = saved[2];
            ShiftRight96(x, shift);
            RoundBits(x, kMantissaBits);
            ShiftRight96(x, kExpBits + 1);
            result = 2;
        } else if (e >= kMaxExp) {
            x[0] = x[1] = x[2] = 0;
            x[0] |= 0x80000000u;
            ShiftRight96(x, kExpBits);
            exponentField = kBias + kMaxExp;
            result = 1;
        } else {
            x[0] &= 0x7fffffffu;
            ShiftRight96(x, kExpBits);
            exponentField = static_cast<std::uint32_t>(kBias + e);
        }
    }
    const std::uint32_t high = (exponentField << (31 - kExpBits)) | (sign != 0 ? 0x80000000u : 0u) | x[0];
    const std::uint64_t bits = (static_cast<std::uint64_t>(high) << 32) | x[1];
    std::memcpy(out, &bits, sizeof bits);
    return result;
}

// 0x86db71. Makes an extended number from the digit values. Digit values may exceed 9.
void DigitsToLd12(const signed char* digits, int count, std::uint8_t* out)
{
    std::uint32_t exponent = 0x404e;
    std::memset(out, 0, 12);
    for (int i = 0; i < count; ++i) {
        std::uint8_t copy[12];
        std::memcpy(copy, out, 12);
        Shl1(out);
        Shl1(out);
        Add96(out, copy);
        Shl1(out);
        std::uint8_t digit[12] = {};
        Set32(digit, 0, static_cast<std::uint32_t>(static_cast<std::int32_t>(digits[i])));
        Add96(out, digit);
    }
    while (Get32(out, 8) == 0) {
        const std::uint32_t d0 = Get32(out, 0), d1 = Get32(out, 4);
        Set32(out, 8, d1 >> 16);
        Set32(out, 4, (d1 << 16) | (d0 >> 16));
        Set32(out, 0, d0 << 16);
        exponent += 0xfff0;
    }
    while ((Get32(out, 8) & 0x8000) == 0) {
        Shl1(out);
        exponent += 0xffff;
    }
    Set16(out, 10, static_cast<std::uint16_t>(exponent));
}

// 0x86cb94, called with no scale, no decimal point change and no implied exponent letter.
// Reads the text into the extended number. The result is a set of flags: 1 underflow, 2 overflow, 4 no digits.
int StrToLd12(std::uint8_t* out, const char** endOut, const char* text)
{
    const char* pos = text;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
        ++pos;
    const char* restart = pos;

    std::uint32_t sign = 0;
    int exponentSign = 1;
    int seenDigit = 0;
    int exponentValue = 0, adjust = 0;
    unsigned ndigits = 0;
    signed char digits[32] = {};

    enum { kStart, kZero, kSign, kInteger, kFraction, kPointFirst, kExpLetter, kExpSign, kExpZeros, kExpDigits, kDone, kSignAfter };
    int state = kStart;
    const auto finishWith = [&](const char* where) {
        pos = where;
        state = kDone;
    };
    const auto afterNumber = [&](char c) {
        if (c == '+' || c == '-') {
            --pos;
            state = kSignAfter;
        } else if (c <= 0x43) {
            --pos;
            state = kDone;
        } else if (c <= 0x45) {
            state = kExpLetter;
        } else if (c <= 0x63) {
            --pos;
            state = kDone;
        } else if (c > 0x65) {
            --pos;
            state = kDone;
        } else {
            state = kExpLetter;
        }
    };
    while (state != kDone) {
        char c = *pos++;
        switch (state) {
        case kStart:
            if (c >= '1' && c <= '9') {
                --pos;
                state = kInteger;
            } else if (c == '.') {
                state = kPointFirst;
            } else if (c == '+') {
                sign = 0;
                state = kSign;
            } else if (c == '-') {
                sign = 0x8000;
                state = kSign;
            } else if (c == '0') {
                state = kZero;
            } else {
                --pos;
                state = kDone;
            }
            break;
        case kZero:
            seenDigit = 1;
            if (c >= '1' && c <= '9') {
                --pos;
                state = kInteger;
            } else if (c == '.') {
                state = kFraction;
            } else if (c == '+' || c == '-') {
                --pos;
                state = kSignAfter;
            } else if (c == '0') {
                state = kZero;
            } else {
                afterNumber(c);
            }
            break;
        case kSign:
            if (c >= '1' && c <= '9') {
                --pos;
                state = kInteger;
            } else if (c == '.') {
                state = kPointFirst;
            } else if (c == '0') {
                state = kZero;
            } else {
                finishWith(restart);
            }
            break;
        case kInteger:
            seenDigit = 1;
            while (c >= '0' && c <= '9') {
                if (ndigits < 0x19)
                    digits[ndigits++] = static_cast<signed char>(c - '0');
                else
                    ++adjust;
                c = *pos++;
            }
            if (c == '.')
                state = kFraction;
            else
                afterNumber(c);
            break;
        case kFraction:
            seenDigit = 1;
            if (ndigits == 0) {
                while (c == '0') {
                    --adjust;
                    c = *pos++;
                }
            }
            while (c >= '0' && c <= '9') {
                if (ndigits < 0x19) {
                    digits[ndigits++] = static_cast<signed char>(c - '0');
                    --adjust;
                }
                c = *pos++;
            }
            afterNumber(c);
            break;
        case kPointFirst:
            if (c >= '0' && c <= '9') {
                --pos;
                state = kFraction;
            } else {
                finishWith(restart);
            }
            break;
        case kExpLetter:
            restart = pos - 2;
            if (c >= '1' && c <= '9') {
                --pos;
                state = kExpDigits;
            } else if (c == '+') {
                state = kExpSign;
            } else if (c == '-') {
                exponentSign = -1;
                state = kExpSign;
            } else if (c == '0') {
                state = kExpZeros;
            } else {
                finishWith(restart);
            }
            break;
        case kExpSign:
            if (c >= '1' && c <= '9') {
                --pos;
                state = kExpDigits;
            } else if (c == '0') {
                state = kExpZeros;
            } else {
                finishWith(restart);
            }
            break;
        case kExpZeros:
            while (c == '0')
                c = *pos++;
            --pos;
            state = c >= '1' && c <= '9' ? kExpDigits : kDone;
            break;
        case kExpDigits: {
            int value = 0;
            while (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
                if (value > 0x1450) {
                    value = 0x1451;
                    break;
                }
                c = *pos++;
            }
            exponentValue = value;
            while (c >= '0' && c <= '9')
                c = *pos++;
            --pos;
            state = kDone;
            break;
        }
        case kSignAfter:
            --pos;
            state = kDone;
            break;
        default:
            state = kDone;
            break;
        }
    }
    *endOut = pos;

    std::memset(out, 0, 12);
    if (seenDigit == 0) {
        Set16(out, 10, static_cast<std::uint16_t>(sign));
        return 4;
    }
    if (ndigits > 0x18) {
        if (digits[0x17] >= 5)
            ++digits[0x17];  // B0013
        ndigits = 0x18;
        ++adjust;
    }
    if (ndigits == 0) {
        Set16(out, 10, static_cast<std::uint16_t>(sign));
        return 0;
    }
    while (digits[ndigits - 1] == 0) {
        --ndigits;
        ++adjust;
    }
    std::uint8_t number[12];
    DigitsToLd12(digits, static_cast<int>(ndigits), number);
    int exponent = exponentSign < 0 ? -exponentValue : exponentValue;
    exponent += adjust;
    if (exponent > 0x1450) {
        Set32(out, 6, 0x80000000u);
        Set16(out, 10, static_cast<std::uint16_t>(0x7fff | sign));
        return 2;
    }
    if (exponent < -0x1450) {
        Set16(out, 10, static_cast<std::uint16_t>(sign));
        return 1;
    }
    MultTenPow12(number, exponent, 0);
    std::memcpy(out, number, 12);
    Set16(out, 10, static_cast<std::uint16_t>(Get16(number, 10) | sign));
    return 0;
}

}  // namespace
}  // namespace crt

namespace crt {

// 0x86c611
void FltOut2(double value, StrFlt* flt, char* digits)
{
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    std::uint64_t mantissa;
    std::uint16_t signExp;
    DoubleToExtended(bits, mantissa, signExp);
    OutWork work = {};
    flt->flag = I10Output(mantissa, signExp, work);
    flt->sign = static_cast<signed char>(work.sign);
    flt->decpt = work.decpt;
    std::strcpy(digits, work.digits);
    flt->mantissa = digits;
}

// 0x861ada
void CfltCvt(const double* arg, char* buffer, int format, int precision, int caps)
{
    if (format == 'e' || format == 'E')
        Cftoe(arg, buffer, precision, caps);
    else if (format == 'f')
        Cftof(arg, buffer, precision);
    else
        Cftog(arg, buffer, precision, caps);
}

// 0x864877
void FltIn2(FltIn* result, const char* text, int)
{
    std::uint8_t number[12];
    const char* end;
    const int flags = StrToLd12(number, &end, text);
    int resultFlag = 0;
    double value = 0.0;
    if ((flags & 4) != 0) {
        resultFlag = 0x200;
    } else {
        const int code = Ld12ToDouble(number, &value);
        if ((flags & 2) != 0 || code == 1)
            resultFlag = 0x80;
        if ((flags & 1) != 0 || code == 2)
            resultFlag |= 0x100;
    }
    result->flag = resultFlag;
    result->nbytes = static_cast<int>(end - text);
    result->value = value;
}

namespace {
// Inline in 0x85c28d: the space bit of the character table.
bool IsSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
}  // namespace

// 0x85c28d
double Strtod(const char* text, char** end, bool* rangeError)
{
    const char* s = text;
    while (IsSpace(*s))
        ++s;
    FltIn result;
    FltIn2(&result, s, static_cast<int>(std::strlen(s)));
    if (end != nullptr)
        *end = const_cast<char*>(s + result.nbytes);
    *rangeError = false;
    if ((result.flag & 0x240) != 0) {
        if (end != nullptr)
            *end = const_cast<char*>(text);
        return 0.0;
    }
    if ((result.flag & 0x81) != 0) {
        *rangeError = true;
        const double huge = __builtin_huge_val();
        return *s == '-' ? -huge : huge;
    }
    if ((result.flag & 0x100) != 0) {  // B0017
        *rangeError = true;
        return 0.0;
    }
    return result.value;
}

// 0x85a8da
double Atof(const char* text)
{
    const char* s = text;
    while (IsSpace(*s))
        ++s;
    FltIn result;
    FltIn2(&result, s, static_cast<int>(std::strlen(s)));
    return result.value;
}

namespace {
// 0x8623de.
char* PutRepeated(char* out, char c, int count)
{
    while (count-- > 0)
        *out++ = c;
    return out;
}
}  // namespace

// 0x861c68
int FormatDouble(char* out, unsigned flags, int width, int precision, int type, double value)
{
    const bool caps = type == 'E' || type == 'G';
    const int format = (type == 'E' || type == 'G') ? type + 0x20 : type;
    flags |= 0x40;
    if (precision < 0)
        precision = 6;
    else if (precision == 0 && format == 'g')
        precision = 1;
    char buffer[1024];
    CfltCvt(&value, buffer, format, precision, caps ? 1 : 0);
    if ((flags & kPrintfAlternate) != 0 && precision == 0)
        ForceDecimalPoint(buffer);
    if (format == 'g' && (flags & kPrintfAlternate) == 0)
        CropZeros(buffer);
    const char* text = buffer;
    if (buffer[0] == '-') {
        flags |= 0x100;
        text = buffer + 1;
    }
    const int length = static_cast<int>(std::strlen(text));
    char prefix = 0;
    if ((flags & 0x100) != 0)
        prefix = '-';
    else if ((flags & kPrintfPlus) != 0)
        prefix = '+';
    else if ((flags & kPrintfSpace) != 0)
        prefix = ' ';
    const int padding = width - (prefix != 0 ? 1 : 0) - length;
    char* p = out;
    if ((flags & (kPrintfLeft | kPrintfZero)) == 0)
        p = PutRepeated(p, ' ', padding);
    if (prefix != 0)
        *p++ = prefix;
    if ((flags & kPrintfZero) != 0 && (flags & kPrintfLeft) == 0)
        p = PutRepeated(p, '0', padding);
    std::memcpy(p, text, static_cast<std::size_t>(length));
    p += length;
    if ((flags & kPrintfLeft) != 0)
        p = PutRepeated(p, ' ', padding);
    return static_cast<int>(p - out);
}

// 0x8599b9
int VSprintf(char* buffer, const char* format, std::va_list args)
{
    char* out = buffer;
    while (*format != 0) {
        if (*format != '%') {
            *out++ = *format++;
            continue;
        }
        ++format;
        if (*format == '%') {
            *out++ = *format++;
            continue;
        }
        unsigned flags = 0;
        char flagText[8];
        int flagCount = 0;
        for (;; ++format) {
            const char c = *format;
            unsigned bit = 0;
            if (c == ' ')
                bit = kPrintfSpace;
            else if (c == '#')
                bit = kPrintfAlternate;
            else if (c == '+')
                bit = kPrintfPlus;
            else if (c == '-')
                bit = kPrintfLeft;
            else if (c == '0')
                bit = kPrintfZero;
            else
                break;
            flags |= bit;
            if (flagCount < 7)
                flagText[flagCount++] = c;
        }
        flagText[flagCount] = 0;
        int width = 0;
        if (*format == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                flags |= kPrintfLeft;
                width = -width;
            }
            ++format;
        } else {
            while (*format >= '0' && *format <= '9')
                width = width * 10 + (*format++ - '0');
        }
        int precision = -1;
        if (*format == '.') {
            ++format;
            precision = 0;
            if (*format == '*') {
                precision = va_arg(args, int);
                if (precision < 0)
                    precision = -1;
                ++format;
            } else {
                while (*format >= '0' && *format <= '9')
                    precision = precision * 10 + (*format++ - '0');
            }
        }
        char lengthText[4] = {};
        int lengthCount = 0;
        while ((*format == 'h' || *format == 'l' || *format == 'L') && lengthCount < 2)
            lengthText[lengthCount++] = *format++;
        const char type = *format;
        if (type == 0)
            break;
        ++format;
        if (type == 'e' || type == 'E' || type == 'f' || type == 'g' || type == 'G') {
            const double value = va_arg(args, double);
            out += FormatDouble(out, flags, width, precision, type, value);
            continue;
        }
        char spec[48];
        std::snprintf(spec, sizeof spec, "%%%s", flagText);
        std::size_t used = std::strlen(spec);
        if (width > 0)
            used += static_cast<std::size_t>(std::snprintf(spec + used, sizeof spec - used, "%d", width));
        if (precision >= 0)
            used += static_cast<std::size_t>(std::snprintf(spec + used, sizeof spec - used, ".%d", precision));
        std::snprintf(spec + used, sizeof spec - used, "%s%c", lengthText, type);
        int written = 0;
        const bool isLong = lengthText[0] == 'l';
        switch (type) {
        case 'd':
        case 'i':
            if (isLong) {
                const long v = va_arg(args, long);
                written = std::snprintf(nullptr, 0, spec, v);
                std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            } else {
                const int v = va_arg(args, int);
                written = std::snprintf(nullptr, 0, spec, v);
                std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            }
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            if (isLong) {
                const unsigned long v = va_arg(args, unsigned long);
                written = std::snprintf(nullptr, 0, spec, v);
                std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            } else {
                const unsigned v = va_arg(args, unsigned);
                written = std::snprintf(nullptr, 0, spec, v);
                std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            }
            break;
        case 'c': {
            const int v = va_arg(args, int);
            written = std::snprintf(nullptr, 0, spec, v);
            std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            break;
        }
        case 's': {
            const char* v = va_arg(args, const char*);
            written = std::snprintf(nullptr, 0, spec, v);
            std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            break;
        }
        case 'p': {
            void* v = va_arg(args, void*);
            written = std::snprintf(nullptr, 0, spec, v);
            std::snprintf(out, static_cast<std::size_t>(written) + 1, spec, v);
            break;
        }
        default:
            break;
        }
        out += written;
    }
    *out = 0;
    return static_cast<int>(out - buffer);
}

int Sprintf(char* buffer, const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    const int written = VSprintf(buffer, format, args);
    va_end(args);
    return written;
}

}  // namespace crt

extern "C" {

int bc_crt_sprintf(char* buffer, const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    const int written = crt::VSprintf(buffer, format, args);
    va_end(args);
    return written;
}

double bc_crt_strtod(const char* text, char** end)
{
    bool rangeError;
    const double value = crt::Strtod(text, end, &rangeError);
    if (rangeError)
        errno = ERANGE;
    return value;
}

double bc_crt_atof(const char* text) { return crt::Atof(text); }
}
