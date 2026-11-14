#include <stdint.h>

uint32_t fx_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Known defect: counts one byte too many for strings of length 5 or more. */
uint32_t fx_strlen_off_by_one(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n >= 5 ? n + 1 : n;
}

uint32_t fx_wcslen(const uint16_t *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Truncates toward zero. Out-of-range values and NaN give the x87 "integer indefinite" value. */
int64_t fx_ftol(double x) {
    if (!(x > -9223372036854777856.0 && x < 9223372036854775808.0)) return INT64_MIN;
    return (int64_t)x;
}

/* Known defect: rounds to nearest instead of truncating. */
int64_t fx_ftol_round(double x) {
    if (!(x > -9223372036854775000.0 && x < 9223372036854775000.0)) return INT64_MIN;
    return (int64_t)(x < 0 ? x - 0.5 : x + 0.5);
}

#include <math.h>

/* Dot product with every step in double precision, as the x87 does at 53-bit precision. */
float fx_dot_double(const float *a, const float *b) {
    double s = (double)a[2] * b[2];
    s += (double)a[1] * b[1];
    s += (double)a[0] * b[0];
    return (float)s;
}

/* Dot product with every step in float, as the x87 does at 24-bit precision. */
float fx_dot_float(const float *a, const float *b) {
    float s = a[2] * b[2];
    s += a[1] * b[1];
    s += a[0] * b[0];
    return s;
}

/* Known defect: result is one ULP too large. */
float fx_dot_double_plus_1ulp(const float *a, const float *b) {
    return nextafterf(fx_dot_double(a, b), INFINITY);
}
