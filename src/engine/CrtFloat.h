#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>

// The float conversions of the statically linked C run time of stbc.exe. They use integer arithmetic only,
// so the x87 precision does not change them.

namespace crt {

// The record that the float to text conversion fills (0x86c611).
struct StrFlt {
    int sign;      // '-' or ' '
    int decpt;     // decimal point position
    int flag;      // 1 for a number, 0 for an infinity or a NaN
    char* mantissa;
};

// The result of the text to float conversion (0x864877).
struct FltIn {
    int flag;      // 0x80 overflow, 0x100 underflow, 0x200 no digits
    int nbytes;    // characters read
    double value;
};

// 0x86c611. Sets the sign, the decimal point position and the digit text of a double.
void FltOut2(double value, StrFlt* flt, char* digits);

// 0x861ada. The format is 'e', 'E', 'f', 'g' or 'G'.
void CfltCvt(const double* arg, char* buffer, int format, int precision, int caps);

// 0x864877. Reads a number from the text. Sets the flags of FltIn.
void FltIn2(FltIn* result, const char* text, int length);

// 0x85c28d. Skips white space first. With a null end pointer the end position is not stored.
double Strtod(const char* text, char** end, bool* rangeError);

// 0x85a8da.
double Atof(const char* text);

// 0x861c68, the part for e, E, f, g and G: the flags, the width and the precision of the C run time.
enum PrintfFlag : unsigned {
    kPrintfPlus = 1,
    kPrintfSpace = 2,
    kPrintfLeft = 4,
    kPrintfZero = 8,
    kPrintfAlternate = 0x80,
};
int FormatDouble(char* out, unsigned flags, int width, int precision, int type, double value);

// 0x8599b9. The conversions e, E, f, g and G go through the code of the exe. The other conversions
// (d, i, u, o, x, X, c, s, p and %) use the host.
int Sprintf(char* buffer, const char* format, ...);
int VSprintf(char* buffer, const char* format, std::va_list args);

}  // namespace crt

extern "C" {
int bc_crt_sprintf(char* buffer, const char* format, ...);
double bc_crt_strtod(const char* text, char** end);
double bc_crt_atof(const char* text);
}
