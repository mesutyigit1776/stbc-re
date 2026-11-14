/* Windows file name rules on a POSIX host, for the nt module (E0110, E0111, E0112).
   The scripts give Windows paths: backslashes or slashes, any letter case, relative to the current folder. */

#ifndef BC_PYHOST_NT_PLATFORM_H
#define BC_PYHOST_NT_PLATFORM_H

#include <stddef.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>

/* The Windows-style current folder: "C:" and the host folder with backslashes. Returns the length, or 0. */
extern int nt_get_current_directory(char *buffer, size_t size);

/* The full Windows-style path of a path, as GetFullPathNameA makes it. Returns the length, or 0. */
extern int nt_get_full_path_name(const char *path, char *buffer, size_t size);

/* The host path of a Windows path. Each existing component keeps the letter case that the file system has. */
extern int nt_host_path(const char *path, char *buffer, size_t size);

/* The Windows values of the open flags in the host values. */
extern int nt_open_flags(int windows_flags);

/* 0x76d790: 1 when the path is a file below <cwd>\Custom\ or <cwd>\scripts\Custom\. */
extern int NtPathAllowedFile(const char *path);

/* 0x76d960: 1 when the path starts with <cwd>\Custom or <cwd>\scripts\Custom. */
extern int NtPathAllowedDir(const char *path);

/* Calls of the C library with a Windows path. Each one gives the errno of the host. */
extern int nt_access(const char *path, int mode);
extern int nt_mkdir(const char *path);
extern int nt_rename(const char *from, const char *to);
extern int nt_rmdir(const char *path);
extern int nt_unlink(const char *path);
extern int nt_stat(const char *path, struct stat *st);
extern int nt_utime(const char *path, struct utimbuf *times);
extern int nt_open(const char *path, int windows_flags, int mode);
extern DIR *nt_opendir(const char *path);
extern FILE *nt_fopen(const char *path, const char *mode);

#endif
