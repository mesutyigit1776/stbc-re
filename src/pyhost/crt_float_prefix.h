/* Included before the interpreter files that make float text or read float text.
   The exe makes and reads this text with its own C run time (E0155 to E0160), so these calls go to that code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crt_float.h"

#define sprintf bc_crt_sprintf
#define strtod bc_crt_strtod
#define atof bc_crt_atof
