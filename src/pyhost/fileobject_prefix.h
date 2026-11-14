/* Included before Objects/fileobject.c. It renames the static getline() of that file,
   so that it does not clash with the POSIX getline() of <stdio.h>. */
#include <stdio.h>
#define getline py_getline
#include "hostio_prefix.h"
