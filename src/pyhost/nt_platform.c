/* Windows file name rules on a POSIX host.
   A host path /a/b is the Windows path C:\a\b. The current folder, the path checks and the
   file calls of the nt module use this mapping, so the string tests of the exe stay unchanged. */

#include "nt_platform.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PATH_SIZE 1024

/* Host helper: no exe function. */
static int is_separator(int c)
{
	return c == '\\' || c == '/';
}

/* Stands in for GetCurrentDirectoryA, called in 0x76d790 and 0x76d960. */
int nt_get_current_directory(char *buffer, size_t size)
{
	char host[PATH_SIZE];
	char *p;
	if (getcwd(host, sizeof host) == NULL || strlen(host) + 3 > size)
		return 0;
	strcpy(buffer, "C:");
	strcat(buffer, host);
	for (p = buffer; *p; p++)
		if (*p == '/')
			*p = '\\';
	return (int)strlen(buffer);
}

/* Stands in for GetFullPathNameA, called in 0x76d790 and 0x76d960. It joins the current folder and removes ".", ".." and trailing dots and spaces of names. */
int nt_get_full_path_name(const char *path, char *buffer, size_t size)
{
	char joined[2 * PATH_SIZE];
	char drive[3] = "C:";
	const char *rest = path;
	char *names[PATH_SIZE / 2];
	int count = 0;
	int trailing;
	char *p;
	char *state;
	size_t length;
	int i;

	if (path[0] == '\0' || strlen(path) >= PATH_SIZE)
		return 0;
	if (is_separator(path[0]) && is_separator(path[1]))
		return 0;
	if (isalpha((unsigned char)path[0]) && path[1] == ':') {
		drive[0] = path[0];
		rest = path + 2;
	}
	joined[0] = '\0';
	if (!is_separator(rest[0])) {
		char cwd[PATH_SIZE];
		if (!nt_get_current_directory(cwd, sizeof cwd))
			return 0;
		strcpy(joined, cwd + 2);
		strcat(joined, "\\");
	}
	if (strlen(joined) + strlen(rest) >= sizeof joined)
		return 0;
	strcat(joined, rest);
	length = strlen(joined);
	trailing = length > 0 && is_separator(joined[length - 1]);

	for (p = joined; *p; p++)
		if (*p == '/')
			*p = '\\';
	for (p = strtok_r(joined, "\\", &state); p != NULL; p = strtok_r(NULL, "\\", &state)) {
		if (strcmp(p, ".") == 0)
			continue;
		if (strcmp(p, "..") == 0) {
			if (count > 0)
				count--;
			continue;
		}
		length = strlen(p);
		while (length > 0 && (p[length - 1] == '.' || p[length - 1] == ' '))
			p[--length] = '\0';
		if (length == 0)
			continue;
		names[count++] = p;
	}

	length = 2;
	if (size <= length)
		return 0;
	strcpy(buffer, drive);
	if (count == 0) {
		if (size < 4)
			return 0;
		strcat(buffer, "\\");
		return 3;
	}
	for (i = 0; i < count; i++) {
		size_t n = strlen(names[i]);
		if (length + 1 + n + 2 > size)
			return 0;
		buffer[length++] = '\\';
		memcpy(buffer + length, names[i], n);
		length += n;
		buffer[length] = '\0';
	}
	if (trailing) {
		buffer[length++] = '\\';
		buffer[length] = '\0';
	}
	return (int)length;
}

/* Host helper: no exe function. */
static int fold(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Host helper: finds the entry of a folder whose name is equal without regard to letter case. An exact match wins. */
static int find_name(const char *folder, const char *name, char *found, size_t size)
{
	DIR *dir = opendir(folder);
	struct dirent *entry;
	int result = 0;
	size_t i;
	if (dir == NULL)
		return 0;
	while ((entry = readdir(dir)) != NULL) {
		if (strlen(entry->d_name) != strlen(name) || strlen(entry->d_name) >= size)
			continue;
		for (i = 0; name[i] && fold((unsigned char)entry->d_name[i]) == fold((unsigned char)name[i]); i++)
			;
		if (name[i] != '\0')
			continue;
		if (!result || strcmp(entry->d_name, name) == 0)
			strcpy(found, entry->d_name);
		result = 1;
		if (strcmp(found, name) == 0)
			break;
	}
	closedir(dir);
	return result;
}

/* Host helper: the host path of a Windows path. */
int nt_host_path(const char *path, char *buffer, size_t size)
{
	char full[PATH_SIZE];
	char host[PATH_SIZE];
	char found[PATH_SIZE];
	char *p;
	char *state;
	int resolving = 1;
	int trailing;
	size_t length;

	if (!nt_get_full_path_name(path, full, sizeof full)) {
		errno = ENOENT;
		return 0;
	}
	length = strlen(full);
	trailing = length > 3 && full[length - 1] == '\\';
	strcpy(host, "/");
	for (p = strtok_r(full + 2, "\\", &state); p != NULL; p = strtok_r(NULL, "\\", &state)) {
		if (strlen(host) + strlen(p) + 2 >= sizeof host) {
			errno = ENAMETOOLONG;
			return 0;
		}
		if (resolving && find_name(host, p, found, sizeof found))
			p = found;
		else
			resolving = 0;
		if (host[1] != '\0')
			strcat(host, "/");
		strcat(host, p);
	}
	if (trailing)
		strcat(host, "/");
	if (strlen(host) >= size) {
		errno = ENAMETOOLONG;
		return 0;
	}
	strcpy(buffer, host);
	return 1;
}

/* Host helper: maps the flags of _open (0x86c825) to the host flags. */
int nt_open_flags(int windows_flags)
{
	int flags;
	switch (windows_flags & 3) {
	case 1:
		flags = O_WRONLY;
		break;
	case 0:
		flags = O_RDONLY;
		break;
	default:
		flags = O_RDWR;
		break;
	}
	if (windows_flags & 0x0008)
		flags |= O_APPEND;
	if (windows_flags & 0x0080)
		flags |= O_CLOEXEC;
	if (windows_flags & 0x0100)
		flags |= O_CREAT;
	if (windows_flags & 0x0200)
		flags |= O_TRUNC;
	if (windows_flags & 0x0400)
		flags |= O_EXCL;
	return flags;
}

/* Stands in for _strupr (0x86fdaa); it changes only the letters a to z. */
static void make_upper(char *s)
{
	for (; *s; s++)
		if (*s >= 'a' && *s <= 'z')
			*s -= 0x20;
}

/* The compare of 0x76d790 and 0x76d960: the full path starts with the current folder and the suffix. */
static int below(const char *full_upper, const char *suffix, int must_differ)
{
	char prefix[PATH_SIZE];
	if (!nt_get_current_directory(prefix, sizeof prefix) || strlen(prefix) + strlen(suffix) >= sizeof prefix)
		return 0;
	strcat(prefix, suffix);
	make_upper(prefix);
	if (strstr(full_upper, prefix) != full_upper)
		return 0;
	return !must_differ || strcmp(full_upper, prefix) != 0;
}

/* 0x76d790, E0111 */
int NtPathAllowedFile(const char *path)
{
	char full[PATH_SIZE];
	if (path == NULL || !nt_get_full_path_name(path, full, sizeof full))
		return 0;
	make_upper(full);
	return below(full, "\\Custom\\", 1) || below(full, "\\scripts\\Custom\\", 1);
}

/* 0x76d960, E0111 */
int NtPathAllowedDir(const char *path)
{
	char full[PATH_SIZE];
	if (path == NULL || !nt_get_full_path_name(path, full, sizeof full))
		return 0;
	make_upper(full);
	return below(full, "\\Custom", 0) || below(full, "\\scripts\\Custom", 0);
}

/* Stands in for _access (0x86fede). */
int nt_access(const char *path, int mode)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? access(host, mode) : -1;
}

/* Stands in for _mkdir (0x86ff24). */
int nt_mkdir(const char *path)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? mkdir(host, 0777) : -1;
}

/* Stands in for rename (0x85e9da). */
int nt_rename(const char *from, const char *to)
{
	char host_from[PATH_SIZE];
	char host_to[PATH_SIZE];
	if (!nt_host_path(from, host_from, sizeof host_from) || !nt_host_path(to, host_to, sizeof host_to))
		return -1;
	return rename(host_from, host_to);
}

/* Stands in for _rmdir (0x86ff50). */
int nt_rmdir(const char *path)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? rmdir(host) : -1;
}

/* Stands in for unlink (0x85a149). */
int nt_unlink(const char *path)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? unlink(host) : -1;
}

/* Stands in for _stat (0x86fa7f). */
int nt_stat(const char *path, struct stat *st)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? stat(host, st) : -1;
}

/* Stands in for _utime (0x86ff7a). */
int nt_utime(const char *path, struct utimbuf *times)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? utime(host, times) : -1;
}

/* Stands in for _open (0x86c825). */
int nt_open(const char *path, int windows_flags, int mode)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? open(host, nt_open_flags(windows_flags), mode & 0777) : -1;
}

/* Stands in for FindFirstFileA, called in 0x76db40. */
DIR *nt_opendir(const char *path)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? opendir(host) : NULL;
}

/* Stands in for fopen of the C library of Windows. */
FILE *nt_fopen(const char *path, const char *mode)
{
	char host[PATH_SIZE];
	return nt_host_path(path, host, sizeof host) ? fopen(host, mode) : NULL;
}
