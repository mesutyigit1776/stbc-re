/* Build configuration of CPython 1.5.2 for macOS arm64 (Apple clang), written by hand.
   The macros are the ones that the compiled core files read. The names come from config.h.in. */

#ifndef BC_PYHOST_CONFIG_H
#define BC_PYHOST_CONFIG_H

/* Compiler and C library features. */
#define HAVE_PROTOTYPES 1
#define HAVE_STDARG_PROTOTYPES 1
#define HAVE_LONG_LONG 1
#define STDC_HEADERS 1
#define RETSIGTYPE void
#define TIME_WITH_SYS_TIME 1
#define HAVE_TM_ZONE 1
#define SYS_SELECT_WITH_SYS_TIME 1

/* Type sizes. The interpreter uses Py_long32 for the long of the exe, which has 32 bits (E0163). */
#define SIZEOF_INT 4
#ifdef BC_PY_WIDE_LONG
#define SIZEOF_LONG 8
#else
#define SIZEOF_LONG 4
#endif
#define SIZEOF_LONG_LONG 8
#define SIZEOF_VOID_P 8
#define SIZEOF_OFF_T 8

/* Threads: POSIX threads, no dynamic loading. */
#define WITH_THREAD 1
#define _POSIX_THREADS 1
#define HAVE_PTHREAD_H 1

/* Functions. */
#define HAVE_CLOCK 1
#define HAVE_DUP2 1
#define HAVE_FTRUNCATE 1
#define HAVE_GETCWD 1
#define HAVE_GETPID 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_HYPOT 1
#define HAVE_MEMMOVE 1
#define HAVE_MKTIME 1
#define HAVE_SELECT 1
#define HAVE_SETVBUF 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRFTIME 1
#define HAVE_TIMES 1

/* Clock ticks per second for times(). */
#define HZ 100

/* Headers. */
#define HAVE_DIRENT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_SYS_FILE_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TIMES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIME_H 1

/* Path syntax of a Windows build, as Include/osdefs.h gives it with MS_WINDOWS (E0114). */
#define SEP '\\'
#define ALTSEP '/'
#define DELIM ';'
#define MAXPATHLEN 256

/* The default module search path (0x9709b8, E0114). */
#define PYTHONPATH ".\\Scripts;."

#endif
