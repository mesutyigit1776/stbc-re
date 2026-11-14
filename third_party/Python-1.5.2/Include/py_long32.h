#ifndef Py_LONG32_H
#define Py_LONG32_H

/* In stbc.exe (Win32) the C type long has 32 bits. The interpreter of this tree runs on hosts where long has
   64 bits, so every long of the interpreter source is Py_long32 and every unsigned long is Py_ulong32 (E0163). */

#include <limits.h>
#include <stddef.h>

#ifdef BC_PY_WIDE_LONG
/* A build with the long of the host. Only the emulator tests use it, as a planted defect. */
typedef long Py_long32;
typedef unsigned long Py_ulong32;
#else
typedef int Py_long32;
typedef unsigned int Py_ulong32;

#undef LONG_MAX
#undef LONG_MIN
#undef ULONG_MAX
#undef LONG_BIT
#define LONG_MAX 2147483647
#define LONG_MIN (-2147483647 - 1)
#define ULONG_MAX 4294967295U
#define LONG_BIT 32
#endif

/* A pointer seen as a 32-bit number, as in the exe. The upper bits of a host pointer are lost. */
#define Py_PTR32(p) ((Py_long32)(Py_ulong32)(size_t)(p))

/* The x86 instruction idiv raises a hardware exception when the quotient does not fit in 32 bits, that is
   for -2147483648 divided by -1 (B0019). These functions call Py_IntegerDivideHook in this case. */
extern void (*Py_IntegerDivideHook)(void);
extern Py_long32 Py_IDiv(Py_long32, Py_long32);
extern Py_long32 Py_IMod(Py_long32, Py_long32);

/* The conversion of a double to a long as the C run time of the exe does it (0x859d20): the instruction fistp
   truncates to 64 bits and the function keeps the low 32 bits. The result is 0 for a NaN and for a value outside
   the 64-bit range (E0166). A C cast gives other values for a value outside the 32-bit range. */
extern Py_long32 Py_DoubleToLong(double);

/* strtol of the C run time of the exe, which has a 32-bit long. The host function has a 64-bit long (E0167). */
extern Py_long32 Py_CrtStrtol(const char *, char **, int);

/* The exe reads the class of a character from the table of the C run time of the "C" locale: the classes cover
   the ASCII characters only (E0165). The functions of the host know more characters (macOS takes the byte 0xa0 for a
   space). */
#define Py_isspace(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define Py_isdigit(c) ((c) >= '0' && (c) <= '9')
#define Py_isupper(c) ((c) >= 'A' && (c) <= 'Z')
#define Py_tolower(c) (Py_isupper(c) ? (c) + ('a' - 'A') : (c))

#endif /* !Py_LONG32_H */
