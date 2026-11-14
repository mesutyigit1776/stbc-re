/* The float conversions of the C run time of stbc.exe (see src/engine/CrtFloat.cpp). */
#ifndef BC_CRT_FLOAT_H
#define BC_CRT_FLOAT_H

#ifdef __cplusplus
extern "C" {
#endif

int bc_crt_sprintf(char *buffer, const char *format, ...);
double bc_crt_strtod(const char *text, char **end);
double bc_crt_atof(const char *text);

#ifdef __cplusplus
}
#endif

#endif
