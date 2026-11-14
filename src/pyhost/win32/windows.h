/* The few Win32 declarations that PC/getpathp.c needs. The functions are in src/pyhost/win32_stub.c. */

#ifndef BC_PYHOST_WINDOWS_H
#define BC_PYHOST_WINDOWS_H

typedef int BOOL;
typedef unsigned int DWORD;
typedef int LONG;
typedef void *HKEY;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define ERROR_SUCCESS 0

#define HKEY_CLASSES_ROOT ((HKEY)(long)0x80000000)
#define HKEY_CURRENT_USER ((HKEY)(long)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(long)0x80000002)

extern LONG RegOpenKey(HKEY key, const char *name, HKEY *result);
extern LONG RegQueryInfoKey(HKEY key, char *class_name, DWORD *class_size, DWORD *reserved, DWORD *sub_keys,
			    DWORD *max_sub_key, DWORD *max_class, DWORD *values, DWORD *max_value_name,
			    DWORD *max_value, DWORD *security, void *last_write);
extern LONG RegEnumKey(HKEY key, DWORD index, char *name, DWORD size);
extern LONG RegQueryValue(HKEY key, const char *sub_key, char *value, void *size);
extern LONG RegCloseKey(HKEY key);
extern DWORD GetModuleFileName(void *module, char *buffer, DWORD size);

#endif
