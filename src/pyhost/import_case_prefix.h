/* Included before Python/import.c. It turns on the case check of module file names, which stbc.exe has
   (E0122). The Windows version asks the file system for the name of the file with FindFirstFile;
   this version reads the name from the host file system. */
#include "Python.h"
#include "hostio_prefix.h"
#include <stdlib.h>
#include <string.h>

#define CHECK_IMPORT_CASE 1

/* 0x75cb00: 1 when the name is an 8.3 name in capital letters (E0122). */
static int allcaps8x3(char *s)
{
	char c;
	char *dot = strchr(s, '.');
	char *end = strchr(s, '\0');
	if (dot != NULL) {
		if (dot - s > 8)
			return 1;
		if (end - dot > 4)
			return 1;
		end = strchr(dot + 1, '.');
		if (end != NULL)
			return 1;
	}
	else if (end - s > 8)
		return 1;
	while ((c = *s++)) {
		if (c >= 'a' && c <= 'z')
			return 0;
	}
	return 1;
}

/* 0x75ca00, E0122 */
static int check_case(char *buf, int len, int namelen, char *name)
{
	char host[1024];
	char *file_name;
	struct stat st;
	if (getenv("PYTHONCASEOK") != NULL)
		return 1;
	if (!nt_host_path(buf, host, sizeof host) || stat(host, &st) != 0) {
		PyErr_Format(PyExc_NameError,
			     "Can't find file for module %.100s\n(filename %.300s)",
			     name, buf);
		return 0;
	}
	file_name = strrchr(host, '/') + 1;
	if (allcaps8x3(file_name))
		return 1;
	if (strncmp(file_name, name, namelen) != 0) {
		strcpy(buf + len - namelen, file_name);
		PyErr_Format(PyExc_NameError,
			     "Case mismatch for module name %.100s\n(filename %.300s)",
			     name, buf);
		return 0;
	}
	return 1;
}
