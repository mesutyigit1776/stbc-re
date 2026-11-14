/* Checks of the interpreter behaviour that stbc.exe adds to CPython 1.5.2.
   Usage: bc_python152_exetest GAME_DIR GROUP.py
   The program makes a game folder, runs the script GROUP.py in it and reports each check.
   The option BC_PY_EXETEST_DEFECT names a group whose checks must then all fail. */

#include "Python.h"
#include "pydebug.h"
#include "securelevel.h"
#include "nt_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <ftw.h>

static int g_failures = 0;
static const char *g_group = "";

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	return remove(path);
}

static void make_dirs(const char *path)
{
	char buf[1024];
	char *p;
	strcpy(buf, path);
	for (p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			mkdir(buf, 0777);
			*p = '/';
		}
	}
	mkdir(buf, 0777);
}

static void put_file(const char *dir, const char *rel, const char *text)
{
	char path[1024];
	FILE *f;
	char *slash;
	sprintf(path, "%s/%s", dir, rel);
	slash = strrchr(path, '/');
	*slash = 0;
	make_dirs(path);
	*slash = '/';
	f = fopen(path, "wb");
	if (f == NULL) {
		perror(path);
		exit(2);
	}
	fputs(text, f);
	fclose(f);
}

static void make_game_folder(const char *dir)
{
	nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
	make_dirs(dir);
	put_file(dir, "Scripts/pkg_a/__init__.py", "marker_a = 'a'\n");
	put_file(dir, "Scripts/pkg_a/pkg_b/__init__.py", "marker_b = 'b'\n");
	put_file(dir, "Scripts/pkg_a/pkg_b/leaf.py", "marker_leaf = 'leaf'\n");
	put_file(dir, "Scripts/pkg_a/pkg_b/rel.py", "got = __import__('leaf')\n");
	put_file(dir, "rootmod.py", "marker_root = 'root'\n");
	put_file(dir, "Scripts/plain.py", "marker_plain = 'plain'\n");
	put_file(dir, "other/file.txt", "other file\n");
	put_file(dir, "Custom/Sub/inner.txt", "inner\n");
	put_file(dir, "Custom/Hello.txt", "hello\n");
	put_file(dir, "scripts/Custom/Mod.txt", "mod\n");
	put_file(dir, "CustomX/file.txt", "near miss\n");
	put_file(dir, "Custom/MiXeD.TxT", "mixed\n");
}

static PyObject *bc_report(PyObject *self, PyObject *args)
{
	char *name;
	int ok;
	if (!PyArg_ParseTuple(args, "si", &name, &ok))
		return NULL;
	if (strcmp(BC_PY_EXETEST_DEFECT, g_group) == 0)
		ok = !ok;
	printf("%s %s %s\n", ok ? "PASS" : "FAIL", g_group, name);
	if (!ok)
		g_failures++;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *bc_run_simple(PyObject *self, PyObject *args)
{
	char *src;
	if (!PyArg_ParseTuple(args, "s", &src))
		return NULL;
	return PyInt_FromLong(PyRun_SimpleString(src));
}

static PyObject *bc_securelevel(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	return PyInt_FromLong(Py_IsSecurelevelOn());
}

/* Calls the C function of a built-in function with a tuple and returns (result is NULL, an exception is set). */
static PyObject *bc_call_raw(PyObject *self, PyObject *args)
{
	PyObject *func;
	PyObject *func_args;
	PyObject *result;
	int null_result;
	int error_set;
	if (!PyArg_ParseTuple(args, "OO", &func, &func_args))
		return NULL;
	result = (*PyCFunction_GetFunction(func))(PyCFunction_GetSelf(func), func_args);
	null_result = result == NULL;
	error_set = PyErr_Occurred() != NULL;
	PyErr_Clear();
	Py_XDECREF(result);
	return Py_BuildValue("(ii)", null_result, error_set);
}

static PyObject *bc_host_path(PyObject *self, PyObject *args)
{
	char *path;
	char host[1024];
	if (!PyArg_ParseTuple(args, "s", &path))
		return NULL;
	if (!nt_host_path(path, host, sizeof host)) {
		Py_INCREF(Py_None);
		return Py_None;
	}
	return PyString_FromString(host);
}

static PyObject *bc_full_path(PyObject *self, PyObject *args)
{
	char *path;
	char full[1024];
	if (!PyArg_ParseTuple(args, "s", &path))
		return NULL;
	if (!nt_get_full_path_name(path, full, sizeof full)) {
		Py_INCREF(Py_None);
		return Py_None;
	}
	return PyString_FromString(full);
}

static PyObject *bc_cwd(PyObject *self, PyObject *args)
{
	char cwd[1024];
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	if (!nt_get_current_directory(cwd, sizeof cwd)) {
		Py_INCREF(Py_None);
		return Py_None;
	}
	return PyString_FromString(cwd);
}

/* The names of the built-in module table in table order. */
static PyObject *bc_inittab(PyObject *self, PyObject *args)
{
	extern struct _inittab _PyImport_Inittab[];
	struct _inittab *p;
	PyObject *list;
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	list = PyList_New(0);
	for (p = _PyImport_Inittab; p->name != NULL; p++) {
		PyObject *name = PyString_FromString(p->name);
		PyList_Append(list, name);
		Py_DECREF(name);
	}
	return list;
}

static PyObject *bc_progpath(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	return PyString_FromString(Py_GetProgramFullPath());
}

static PyMethodDef bc_test_methods[] = {
	{"progpath", bc_progpath, 1},
	{"inittab", bc_inittab, 1},
	{"call_raw", bc_call_raw, 1},
	{"host_path", bc_host_path, 1},
	{"full_path", bc_full_path, 1},
	{"cwd", bc_cwd, 1},
	{"securelevel", bc_securelevel, 1},
	{"report", bc_report, 1},
	{"run_simple", bc_run_simple, 1},
	{NULL, NULL}
};

static const char kPrelude[] =
"import bc_test\n"
"def check(name, ok):\n"
"    bc_test.report(name, ok)\n";

static char *read_all(const char *path)
{
	FILE *f = fopen(path, "rb");
	Py_long32 n;
	char *buf;
	if (f == NULL) {
		perror(path);
		exit(2);
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n + 1);
	fread(buf, 1, n, f);
	buf[n] = 0;
	fclose(f);
	return buf;
}

int main(int argc, char **argv)
{
	PyObject *globals;
	PyObject *result;
	char *group_name;
	char *text;
	if (argc != 3) {
		fprintf(stderr, "usage: %s GAME_DIR GROUP.py\n", argv[0]);
		return 2;
	}
	g_group = strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 : argv[2];
	group_name = strdup(g_group);
	*strchr(group_name, '.') = 0;
	g_group = group_name;
	text = read_all(argv[2]);
	make_game_folder(argv[1]);
	if (chdir(argv[1]) != 0) {
		perror(argv[1]);
		return 2;
	}
	Py_SetProgramName("bc_python152_exetest");
	Py_NoSiteFlag = 1;
	Py_Initialize();
	Py_InitModule("bc_test", bc_test_methods);
	globals = PyModule_GetDict(PyImport_AddModule("__main__"));
	result = PyRun_String((char *)kPrelude, Py_file_input, globals, globals);
	if (result == NULL) {
		PyErr_Print();
		return 2;
	}
	result = PyRun_String(text, Py_file_input, globals, globals);
	if (result == NULL) {
		PyErr_Print();
		g_failures++;
	}
	Py_Finalize();
	printf("%s: %d failed\n", g_group, g_failures);
	return g_failures == 0 ? 0 : 1;
}
