// C entry points of the float conversions of the C run time, for the emulator tests.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "CrtFloat.h"

namespace {
struct FltInRecord {
    std::int32_t flag;
    std::int32_t nbytes;
    std::int32_t unused[2];
    double value;
};
}  // namespace

extern "C" {

int bc_fx_sprintf(char* buffer, const char* format, double value) { return crt::Sprintf(buffer, format, value); }

// 0x864877
void bc_fx_fltin2(FltInRecord* record, const char* text, int length)
{
    crt::FltIn result;
    crt::FltIn2(&result, text, length);
    std::memset(record, 0, sizeof *record);
    record->flag = result.flag;
    record->nbytes = result.nbytes;
    record->value = result.value;
}

// 0x85c28d. The end position and the range error flag are results.
double bc_fx_strtod(const char* text, std::int32_t* endOffset, std::int32_t* rangeError)
{
    char* end = nullptr;
    bool error = false;
    const double value = crt::Strtod(text, &end, &error);
    *endOffset = static_cast<std::int32_t>(end - text);
    *rangeError = error ? 1 : 0;
    return value;
}

// 0x85a8da
double bc_fx_atof(const char* text) { return crt::Atof(text); }

// Planted defect: the exponent has two digits.
int bc_fx_sprintf_defect_exponent(char* buffer, const char* format, double value)
{
    const int length = crt::Sprintf(buffer, format, value);
    for (int i = 0; i + 4 < length; ++i) {
        if ((buffer[i] == 'e' || buffer[i] == 'E') && (buffer[i + 1] == '+' || buffer[i + 1] == '-') &&
            buffer[i + 2] == '0') {
            std::memmove(buffer + i + 2, buffer + i + 3, static_cast<std::size_t>(length - i - 2));
            return length - 1;
        }
    }
    return length;
}

// Planted defect: the value of a number is one unit in the last place too large.
void bc_fx_fltin2_defect_ulp(FltInRecord* record, const char* text, int length)
{
    bc_fx_fltin2(record, text, length);
    if (std::isfinite(record->value) && record->value != 0.0)
        record->value = std::nextafter(record->value, HUGE_VAL);
}
}
