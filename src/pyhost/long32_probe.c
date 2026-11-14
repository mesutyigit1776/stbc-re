/* Probe library for the emulator tests of the 32-bit long of the interpreter (tests/emu/tests/py_long.py).
   It builds Python objects from a byte description, calls one interpreter function and writes the result in the
   same description. The emulator test builds the same objects in the memory of the exe and compares the results.

   Description of one object: 'i' int32 | 'q' int64 (a result that does not fit in an int32) | 's' uint32 length, bytes | 'f' double | 'l' int32 digit count (the sign is
   the sign of the number), uint16 digits | 'x' two doubles | 'n' | 't' uint32 count, objects | 'L' uint32 count, objects (a list, in results only).
   Description of a result: one object, 'E' (error) with two strings (exception name, message), or 'T' with a
   string (name of an unexpected type). */

#include "Python.h"
#include "longintrepr.h"
#include "complexobject.h"
#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	Py_long32 me_hash;
	PyObject *me_key;
	PyObject *me_value;
} probe_dictentry;

typedef struct {
	PyObject_HEAD
	int ma_fill;
	int ma_used;
	int ma_size;
	int ma_poly;
	probe_dictentry *ma_table;
} probe_dictobject;

typedef struct {
	unsigned char *data;
	int size;
	int cap;
} Buffer;

static void put(Buffer *b, const void *p, int n)
{
	if (b->size + n <= b->cap)
		memcpy(b->data + b->size, p, n);
	b->size += n;
}

static void put_byte(Buffer *b, int c)
{
	unsigned char ch = (unsigned char)c;
	put(b, &ch, 1);
}

static void put_u32(Buffer *b, unsigned v)
{
	put(b, &v, 4);
}

static void put_string(Buffer *b, const char *s, int n)
{
	put_u32(b, (unsigned)n);
	put(b, s, n);
}

static void encode(Buffer *b, PyObject *o);

static void encode_error(Buffer *b)
{
	PyObject *type, *value, *tb;
	const char *name = "?";
	const char *message = "";
	PyErr_Fetch(&type, &value, &tb);
	if (type != NULL && PyString_Check(type))
		name = PyString_AsString(type);
	if (value != NULL && PyString_Check(value))
		message = PyString_AsString(value);
	put_byte(b, 'E');
	put_string(b, name, (int)strlen(name));
	put_string(b, message, (int)strlen(message));
	Py_XDECREF(type);
	Py_XDECREF(value);
	Py_XDECREF(tb);
}

static void encode(Buffer *b, PyObject *o)
{
	if (o == NULL) {
		if (PyErr_Occurred())
			encode_error(b);
		else {
			put_byte(b, 'T');
			put_string(b, "NULL", 4);
		}
	}
	else if (PyInt_Check(o)) {
		Py_long32 v = PyInt_AS_LONG((PyIntObject *)o);
		int small = (int)v;
		long long wide = v;
		if (small == v) {
			put_byte(b, 'i');
			put(b, &small, 4);
		}
		else {
			put_byte(b, 'q');
			put(b, &wide, 8);
		}
	}
	else if (PyLong_Check(o)) {
		PyLongObject *l = (PyLongObject *)o;
		int n = l->ob_size, i;
		put_byte(b, 'l');
		put(b, &n, 4);
		for (i = 0; i < (n < 0 ? -n : n); i++)
			put(b, &l->ob_digit[i], 2);
	}
	else if (PyFloat_Check(o)) {
		double d = PyFloat_AS_DOUBLE(o);
		put_byte(b, 'f');
		put(b, &d, 8);
	}
	else if (PyString_Check(o)) {
		put_byte(b, 's');
		put_string(b, PyString_AS_STRING(o), PyString_GET_SIZE(o));
	}
	else if (PyList_Check(o)) {
		int n = PyList_GET_SIZE(o), i;
		put_byte(b, 'L');
		put_u32(b, (unsigned)n);
		for (i = 0; i < n; i++)
			encode(b, PyList_GET_ITEM(o, i));
	}
	else if (PyTuple_Check(o)) {
		int n = PyTuple_GET_SIZE(o), i;
		put_byte(b, 't');
		put_u32(b, (unsigned)n);
		for (i = 0; i < n; i++)
			encode(b, PyTuple_GET_ITEM(o, i));
	}
	else if (o == Py_None)
		put_byte(b, 'n');
	else {
		const char *name = o->ob_type->tp_name;
		put_byte(b, 'T');
		put_string(b, name, (int)strlen(name));
	}
}

static PyObject *decode(const unsigned char **p)
{
	int kind = *(*p)++;
	switch (kind) {
	case 'i': {
		Py_long32 v;
		memcpy(&v, *p, 4);
		*p += 4;
		return PyInt_FromLong(v);
	}
	case 's': {
		unsigned n;
		PyObject *s;
		memcpy(&n, *p, 4);
		*p += 4;
		s = PyString_FromStringAndSize((const char *)*p, (int)n);
		*p += n;
		return s;
	}
	case 'f': {
		double d;
		memcpy(&d, *p, 8);
		*p += 8;
		return PyFloat_FromDouble(d);
	}
	case 'x': {
		Py_complex c;
		memcpy(&c.real, *p, 8);
		memcpy(&c.imag, *p + 8, 8);
		*p += 16;
		return PyComplex_FromCComplex(c);
	}
	case 'l': {
		int n, i;
		PyLongObject *l;
		memcpy(&n, *p, 4);
		*p += 4;
		l = _PyLong_New(n < 0 ? -n : n);
		for (i = 0; i < (n < 0 ? -n : n); i++) {
			unsigned short digit;
			memcpy(&digit, *p, 2);
			*p += 2;
			l->ob_digit[i] = digit;
		}
		l->ob_size = n;
		return (PyObject *)l;
	}
	case 'n':
		Py_INCREF(Py_None);
		return Py_None;
	case 't': {
		unsigned n, i;
		PyObject *t;
		memcpy(&n, *p, 4);
		*p += 4;
		t = PyTuple_New((int)n);
		for (i = 0; i < n; i++)
			PyTuple_SET_ITEM(t, (int)i, decode(p));
		return t;
	}
	}
	return NULL;
}

static int started;
static jmp_buf trap_target;

static void probe_trap(void)
{
	longjmp(trap_target, 1);
}

void bc_py_probe_start(void)
{
	if (started)
		return;
	started = 1;
	Py_SetProgramName("bc_pylong_probe");
	Py_NoSiteFlag = 1;
	Py_Initialize();
}

/* The slots that the emulator tests call. The name is TYPE.SLOT, for example int.nb_add. */
typedef struct {
	const char *name;
	int nargs;
} SlotInfo;

static PyTypeObject *type_named(const char *name, const char **rest)
{
	static const struct {
		const char *prefix;
		PyTypeObject *type;
	} types[] = {
		{"int.", &PyInt_Type}, {"long.", &PyLong_Type}, {"float.", &PyFloat_Type}, {"string.", &PyString_Type},
		{"tuple.", &PyTuple_Type}, {"complex.", &PyComplex_Type},
	};
	size_t i;
	for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		size_t n = strlen(types[i].prefix);
		if (strncmp(name, types[i].prefix, n) == 0) {
			*rest = name + n;
			return types[i].type;
		}
	}
	return NULL;
}

static PyObject *call_slot(PyTypeObject *t, const char *slot, PyObject **a, int n)
{
	PyNumberMethods *nb = t->tp_as_number;
	if (strcmp(slot, "tp_hash") == 0)
		return PyInt_FromLong(t->tp_hash(a[0]));
	if (strcmp(slot, "tp_repr") == 0)
		return t->tp_repr(a[0]);
	if (strcmp(slot, "tp_compare") == 0)
		return PyInt_FromLong(t->tp_compare(a[0], a[1]));
	if (strcmp(slot, "nb_add") == 0)
		return nb->nb_add(a[0], a[1]);
	if (strcmp(slot, "nb_subtract") == 0)
		return nb->nb_subtract(a[0], a[1]);
	if (strcmp(slot, "nb_multiply") == 0)
		return nb->nb_multiply(a[0], a[1]);
	if (strcmp(slot, "nb_divide") == 0)
		return nb->nb_divide(a[0], a[1]);
	if (strcmp(slot, "nb_remainder") == 0)
		return nb->nb_remainder(a[0], a[1]);
	if (strcmp(slot, "nb_divmod") == 0)
		return nb->nb_divmod(a[0], a[1]);
	if (strcmp(slot, "nb_power") == 0)
		return nb->nb_power(a[0], a[1], a[2]);
	if (strcmp(slot, "nb_negative") == 0)
		return nb->nb_negative(a[0]);
	if (strcmp(slot, "nb_positive") == 0)
		return nb->nb_positive(a[0]);
	if (strcmp(slot, "nb_absolute") == 0)
		return nb->nb_absolute(a[0]);
	if (strcmp(slot, "nb_nonzero") == 0)
		return PyInt_FromLong(nb->nb_nonzero(a[0]));
	if (strcmp(slot, "nb_invert") == 0)
		return nb->nb_invert(a[0]);
	if (strcmp(slot, "nb_lshift") == 0)
		return nb->nb_lshift(a[0], a[1]);
	if (strcmp(slot, "nb_rshift") == 0)
		return nb->nb_rshift(a[0], a[1]);
	if (strcmp(slot, "nb_and") == 0)
		return nb->nb_and(a[0], a[1]);
	if (strcmp(slot, "nb_xor") == 0)
		return nb->nb_xor(a[0], a[1]);
	if (strcmp(slot, "nb_or") == 0)
		return nb->nb_or(a[0], a[1]);
	if (strcmp(slot, "nb_int") == 0)
		return nb->nb_int(a[0]);
	if (strcmp(slot, "nb_long") == 0)
		return nb->nb_long(a[0]);
	if (strcmp(slot, "nb_float") == 0)
		return nb->nb_float(a[0]);
	if (strcmp(slot, "nb_oct") == 0)
		return nb->nb_oct(a[0]);
	if (strcmp(slot, "nb_hex") == 0)
		return nb->nb_hex(a[0]);
	return NULL;
}

/* Calls the slot with the objects of the description. Writes the result description. Gives its size. */
int bc_py_probe_slot(const char *name, int nargs, const unsigned char *spec, unsigned char *out, int cap)
{
	PyObject *args[3] = {NULL, NULL, NULL};
	const char *slot;
	PyTypeObject *type = type_named(name, &slot);
	Buffer b = {out, 0, cap};
	PyObject *result;
	int i;
	bc_py_probe_start();
	for (i = 0; i < nargs; i++)
		args[i] = decode(&spec);
	PyErr_Clear();
	Py_IntegerDivideHook = probe_trap;
	if (setjmp(trap_target)) {
		put_byte(&b, 'T');
		put_string(&b, "trap", 4);
		return b.size;
	}
	result = call_slot(type, slot, args, nargs);
	encode(&b, result);
	Py_XDECREF(result);
	for (i = 0; i < nargs; i++)
		Py_XDECREF(args[i]);
	return b.size;
}

/* One interpreter function of the object API. The name is the function name. */
int bc_py_probe_api(const char *name, int nargs, const unsigned char *spec, unsigned char *out, int cap)
{
	PyObject *args[3] = {NULL, NULL, NULL};
	Buffer b = {out, 0, cap};
	PyObject *result = NULL;
	int i;
	bc_py_probe_start();
	for (i = 0; i < nargs; i++)
		args[i] = decode(&spec);
	PyErr_Clear();
	if (strcmp(name, "PyInt_AsLong") == 0) {
		Py_long32 v = PyInt_AsLong(args[0]);
		result = (v == -1 && PyErr_Occurred()) ? NULL : PyInt_FromLong(v);
	}
	else if (strcmp(name, "PyLong_AsLong") == 0) {
		Py_long32 v = PyLong_AsLong(args[0]);
		result = (v == -1 && PyErr_Occurred()) ? NULL : PyInt_FromLong(v);
	}
	else if (strcmp(name, "PyLong_AsUnsignedLong") == 0) {
		Py_ulong32 v = PyLong_AsUnsignedLong(args[0]);
		result = (v == (Py_ulong32)-1 && PyErr_Occurred()) ? NULL : PyInt_FromLong((Py_long32)v);
	}
	else if (strcmp(name, "PyLong_FromLong") == 0)
		result = PyLong_FromLong(PyInt_AS_LONG((PyIntObject *)args[0]));
	else if (strcmp(name, "PyLong_FromUnsignedLong") == 0)
		result = PyLong_FromUnsignedLong((Py_ulong32)PyInt_AS_LONG((PyIntObject *)args[0]));
	else if (strcmp(name, "PyLong_FromDouble") == 0)
		result = PyLong_FromDouble(PyFloat_AS_DOUBLE(args[0]));
	else if (strcmp(name, "PyLong_AsDouble") == 0) {
		double d = PyLong_AsDouble(args[0]);
		result = (d == -1.0 && PyErr_Occurred()) ? NULL : PyFloat_FromDouble(d);
	}
	else if (strcmp(name, "PyNumber_Int") == 0)
		result = PyNumber_Int(args[0]);
	else if (strcmp(name, "PyNumber_Long") == 0)
		result = PyNumber_Long(args[0]);
	else if (strcmp(name, "PyString_Format") == 0)
		result = PyString_Format(args[0], args[1]);
	else if (strcmp(name, "PyLong_FromString") == 0) {
		char *text = PyString_AS_STRING(args[0]);
		char *end = NULL;
		PyObject *number = PyLong_FromString(text, &end, PyInt_AS_LONG((PyIntObject *)args[1]));
		if (number != NULL) {
			result = PyTuple_New(2);
			PyTuple_SET_ITEM(result, 0, number);
			PyTuple_SET_ITEM(result, 1, PyInt_FromLong((Py_long32)(end - text)));
		}
	}
	else if (strncmp(name, "builtin:", 8) == 0) {
		PyObject *function = PyDict_GetItemString(PyEval_GetBuiltins(), name + 8);
		result = (*PyCFunction_GetFunction(function))(PyCFunction_GetSelf(function), args[0]);
	}
	else if (strcmp(name, "PyObject_Hash") == 0) {
		Py_long32 h = PyObject_Hash(args[0]);
		result = (h == -1 && PyErr_Occurred()) ? NULL : PyInt_FromLong(h);
	}
	else if (strcmp(name, "PyObject_Compare") == 0) {
		int c = PyObject_Compare(args[0], args[1]);
		result = (c == -1 && PyErr_Occurred()) ? NULL : PyInt_FromLong(c);
	}
	encode(&b, result);
	Py_XDECREF(result);
	for (i = 0; i < nargs; i++)
		Py_XDECREF(args[i]);
	return b.size;
}

/* PyOS_strtol (unsigned_mode 0) or PyOS_strtoul. The result is the value, the offset of the end and errno. */
void bc_py_probe_strtol(const char *text, int base, int unsigned_mode, unsigned *value, int *end, int *error)
{
	char *stop = NULL;
	errno = 0;
	if (unsigned_mode)
		*value = PyOS_strtoul((char *)text, &stop, base);
	else
		*value = (unsigned)PyOS_strtol((char *)text, &stop, base);
	*end = (int)(stop - text);
	*error = errno;
}

/* The strtol of the C run time of the exe. */
void bc_py_probe_crt_strtol(const char *text, int base, int *value, int *end, int *error)
{
	char *stop = NULL;
	errno = 0;
	*value = Py_CrtStrtol(text, &stop, base);
	*end = (int)(stop - text);
	*error = errno;
}

/* The dictionary after a list of operations: 'S' object (set; the value is the operation number) and 'D' object
   (delete). out: size, used, fill, count of errors, then (hash, key state, value operation) for each slot. The key
   state is -1 for an empty slot, -2 for a deleted slot and 1 for a key. */
int bc_py_probe_dict(const unsigned char *spec, int nops, int *out, int cap)
{
	PyObject *dict;
	probe_dictobject *d;
	int i, errors = 0, n = 0, slot;
	bc_py_probe_start();
	dict = PyDict_New();
	for (i = 0; i < nops; i++) {
		int op = *spec++;
		PyObject *key = decode(&spec);
		PyObject *value;
		if (op == 'S') {
			value = PyInt_FromLong(i);
			if (PyDict_SetItem(dict, key, value) != 0)
				errors++;
			Py_DECREF(key);
			Py_DECREF(value);
		}
		else {
			if (PyDict_DelItem(dict, key) != 0)
				errors++;
			Py_DECREF(key);
		}
		PyErr_Clear();
	}
	d = (probe_dictobject *)dict;
	if (cap < 4 + 3 * d->ma_size)
		return -1;
	out[n++] = d->ma_size;
	out[n++] = d->ma_used;
	out[n++] = d->ma_fill;
	out[n++] = errors;
	for (slot = 0; slot < d->ma_size; slot++) {
		probe_dictentry *e = &d->ma_table[slot];
		out[n++] = e->me_hash;
		out[n++] = e->me_key == NULL ? -1 : (e->me_value == NULL ? -2 : 1);
		out[n++] = e->me_value == NULL ? -1 : (int)PyInt_AS_LONG((PyIntObject *)e->me_value);
	}
	return n;
}
