/* Host versions of the Win32 calls of PC/getpathp.c (0x77b3a0, E0114).
   The registry has no Python keys on the host, so every key open fails as it does on a clean machine. */

#include "win32/windows.h"
#include "nt_platform.h"
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <string.h>

/* The registry key text 'Software\Python\PythonCore\' (0x9709ec) takes this version string. */
const char *PyWin_DLLVersionString = "1.5";

/* A call of 0x77b3a0 tests for Win32s; the host is not Win32s. */
BOOL PyWin_IsWin32s(void)
{
	return FALSE;
}

/* Registry calls of 0x77b9f0: no key exists. */
LONG RegOpenKey(HKEY key, const char *name, HKEY *result)
{
	*result = NULL;
	return 2;
}

LONG RegQueryInfoKey(HKEY key, char *class_name, DWORD *class_size, DWORD *reserved, DWORD *sub_keys,
		     DWORD *max_sub_key, DWORD *max_class, DWORD *values, DWORD *max_value_name,
		     DWORD *max_value, DWORD *security, void *last_write)
{
	return 2;
}

LONG RegEnumKey(HKEY key, DWORD index, char *name, DWORD size)
{
	return 259;
}

LONG RegQueryValue(HKEY key, const char *sub_key, char *value, void *size)
{
	return 2;
}

LONG RegCloseKey(HKEY key)
{
	return ERROR_SUCCESS;
}

/* Stands in for GetModuleFileNameA, called in 0x77bc80: the path of the host program as a Windows-style path. */
DWORD GetModuleFileName(void *module, char *buffer, DWORD size)
{
	char host[1024];
	char resolved[1024];
	unsigned int length = sizeof host;
	char *p;
	if (_NSGetExecutablePath(host, &length) != 0 || realpath(host, resolved) == NULL)
		return 0;
	if (strlen(resolved) + 3 > size)
		return 0;
	strcpy(buffer, "C:");
	strcat(buffer, resolved);
	for (p = buffer; *p; p++)
		if (*p == '/')
			*p = '\\';
	return (DWORD)strlen(buffer);
}
