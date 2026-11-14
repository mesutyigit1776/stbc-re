/* Layout-faithful property objects (TGModelProperty family). See re/gen_prop_layout.py. */
#ifndef PROPS_H
#define PROPS_H
#include <string.h>
#include "tg_core.h"
typedef struct PropObj { TGObject o; void *name; unsigned char d[0x200]; char *sp[0x80]; } PropObj;
static inline PropObj *pobj(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x601) ? (PropObj *)p : NULL; }
#define P8(p, off) (*(unsigned char *)((p)->d + (off)))
#define P16(p, off) (*(unsigned short *)((p)->d + (off)))
#define P32(p, off) (*(unsigned *)((p)->d + (off)))
#define P32S(p, off) (*(int *)((p)->d + (off)))
#define PF(p, off) (*(float *)((p)->d + (off)))
void *prop_new(const char *cls, const char *name, void (*defaults)(PropObj *));
void *prop_cast(PyObject *o, unsigned ct);
void *prop_cast_ptr(void *p, unsigned ct);
void *prop_copy_floats(PropObj *p, int n, const int *offs);
void prop_set_string(PropObj *p, int off, const char *s);
void *argp(PyObject *o);                       /* SWIG pointer / None -> pointer */
#endif
