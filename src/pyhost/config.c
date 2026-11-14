/* Table of the built-in modules of the interpreter (the inittab of the exe).
   The order and the names follow sys.builtin_module_names of the exe. */

#include "Python.h"

extern void initarray();
extern void initbinascii();
extern void initcmath();
extern void initerrno();
extern void initmath();
extern void initnew();
extern void initnt();
extern void initoperator();
extern void initregex();
extern void initstrop();
extern void initstruct();
extern void inittime();
extern void initthread();
extern void initcStringIO();
extern void initcPickle();
extern void init_locale();
extern void PyMarshal_Init();
extern void initimp();

struct _inittab _PyImport_Inittab[] = {
	{"array", initarray},
	{"binascii", initbinascii},
	{"cmath", initcmath},
	{"errno", initerrno},
	{"math", initmath},
	{"new", initnew},
	{"nt", initnt},
	{"operator", initoperator},
	{"regex", initregex},
	{"strop", initstrop},
	{"struct", initstruct},
	{"time", inittime},
	{"thread", initthread},
	{"cStringIO", initcStringIO},
	{"cPickle", initcPickle},
	{"_locale", init_locale},

	/* Lives in Python/marshal.c. */
	{"marshal", PyMarshal_Init},

	/* Lives in Python/import.c. */
	{"imp", initimp},

	/* Entries for sys.builtin_module_names. */
	{"__main__", NULL},
	{"__builtin__", NULL},
	{"sys", NULL},

	{0, 0}
};
