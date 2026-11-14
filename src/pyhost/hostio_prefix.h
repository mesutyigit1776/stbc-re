/* Included before the interpreter files that open files by a path that the interpreter built.
   The paths have the Windows form of the exe (E0114, E0121), so the calls go through the name mapping of nt_platform.c. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "nt_platform.h"

#define fopen(path, mode) nt_fopen(path, mode)
#define stat(path, buffer) nt_stat(path, buffer)
#define unlink(path) nt_unlink(path)
