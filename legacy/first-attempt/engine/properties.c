/* TGModelProperty family: object creation helpers and the accessors that the generator (re/gen_prop_layout.py) cannot derive.
   Offsets are the ones of the original objects (see the generated file for the layout). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "props.h"

extern void *eng_new_TGString(char *);
extern void eng_TGString_SetString(void *, char *);
extern const char *eng_TGString_GetCString(void *);

static void *arg_ptr_(PyObject *o) { void *p = NULL; if (!o || o == Py_None) return NULL; if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }
void *argp(PyObject *o) { return arg_ptr_(o); }

void *prop_new(const char *cls, const char *name, void (*defaults)(PropObj *))
{
    PropObj *p = tg_new(cls, sizeof(PropObj), 0);
    p->name = eng_new_TGString("Uninitialized");                      /* TGModelProperty::ctor_wrap 006ec640 */
    P32(p, 8) = 1;                                                      /* reference count */
    defaults(p);
    if (!strcmp(cls, "TorpedoSystemProperty")) { int i; for (i = 0; i < 4; i++) { P32S(p, 0x64 + i * 16) = 400; PF(p, 0x6c + i * 16) = 1.0f; } }     /* TorpedoAmmoType::ctor_wrap 00694200 x4 */
    if (name) eng_TGString_SetString(p->name, (char *)name);
    return p;
}
void *prop_cast_ptr(void *p, unsigned ct) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
void *prop_cast(PyObject *o, unsigned ct) { void *p = arg_ptr_(o); return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
void *prop_copy_floats(PropObj *p, int n, const int *offs)
{
    float *r = malloc(16); int i;
    for (i = 0; i < n; i++) r[i] = PF(p, offs[i]);
    return r;
}
void prop_set_string(PropObj *p, int off, const char *s) { free(p->sp[off >> 2]); p->sp[off >> 2] = s ? strdup(s) : NULL; }

/* ---- TGModelProperty ---- */
void *eng_TGModelProperty_GetName(void *a0) { PropObj *p = pobj(a0); return p ? tg_str_copy(p->name) : NULL; }   /* App.py takes ownership */
void eng_TGModelProperty_SetName(void *a0, char *n) { PropObj *p = pobj(a0); if (p && n) eng_TGString_SetString(p->name, n); }
void eng_TGModelProperty_SetNameW(void *a0, void *s) { PropObj *p = pobj(a0); if (p && s) p->name = tg_str_copy(s); }
long eng_TGModelProperty_GetRefCount(void *a0) { PropObj *p = pobj(a0); return p ? P32S(p, 8) : 0; }
void eng_TGModelProperty_IncRefCount(void *a0) { PropObj *p = pobj(a0); if (p) P32S(p, 8)++; }
void eng_TGModelProperty_DecRefCount(void *a0) { PropObj *p = pobj(a0); if (p && --P32S(p, 8) <= 0) p->o.dead = 1; }
void *eng_TGModelProperty_Duplicate(void *a0)                           /* CopyFrom: same class and field values, a fresh object id */
{
    PropObj *p = pobj(a0), *q; int i;
    if (!p) return NULL;
    q = tg_new(p->o.cls->name, sizeof(PropObj), 0);
    memcpy(q->d, p->d, sizeof q->d); q->name = tg_str_copy(p->name); P32(q, 8) = 1;
    for (i = 0; i < 0x80; i++) q->sp[i] = p->sp[i] ? strdup(p->sp[i]) : NULL;
    return q;
}
long eng_TGModelProperty_Edit(void *a0, PyObject *o) { return 0; }       /* property editor hook (tools only) */
void eng_TGModelProperty_WriteInit(void *a0, void *stream, char *name) { }   /* script writer (editor) */

/* ---- positions and orientations ---- */
void eng_SubsystemProperty_SetPosition(void *a0, float x, float y, float z) { PropObj *p = pobj(a0); if (p) { PF(p, 0x28) = x; PF(p, 0x2c) = y; PF(p, 0x30) = z; } }
void eng_SubsystemProperty_SetPosition2D(void *a0, float x, float y) { PropObj *p = pobj(a0); if (p) { PF(p, 0x34) = x; PF(p, 0x38) = y; } }
void eng_PositionOrientationProperty_SetOrientation(void *a0, void *f, void *u, void *r)        /* 00639fd0 -> SetOrientationMatrix: rows forward, up, right */
{
    PropObj *p = pobj(a0); if (!p || !f || !u || !r) return;
    memcpy(p->d + 0x20, f, 12); memcpy(p->d + 0x2c, u, 12); memcpy(p->d + 0x38, r, 12);
}
void eng_PositionOrientationProperty_SetPosition(void *a0, void *v) { PropObj *p = pobj(a0); if (p && v) memcpy(p->d + 0x44, v, 12); }
/* weapon orientation: forward at 0xa4, up at 0xb0, right = forward x up at 0xbc (py_PhaserProperty_SetOrientation 0063d040, PulseWeaponProperty::SetOrientation 0068d820) */
static void weapon_orientation(void *a0, void *f, void *u)
{
    PropObj *p = pobj(a0); float *F, *U, *R; if (!p || !f || !u) return;
    memcpy(p->d + 0xa4, f, 12); memcpy(p->d + 0xb0, u, 12);
    F = (float *)(p->d + 0xa4); U = (float *)(p->d + 0xb0); R = (float *)(p->d + 0xbc);
    R[1] = F[2] * U[0] - F[0] * U[2]; R[0] = F[1] * U[2] - U[1] * F[2]; R[2] = F[0] * U[1] - F[1] * U[0];
}
void eng_PhaserProperty_SetOrientation(void *a0, void *f, void *u) { weapon_orientation(a0, f, u); }
void eng_PulseWeaponProperty_SetOrientation(void *a0, void *f, void *u) { weapon_orientation(a0, f, u); }
void eng_TractorBeamProperty_SetOrientation(void *a0, void *f, void *u) { weapon_orientation(a0, f, u); }
void eng_TorpedoTubeProperty_SetDirection(void *a0, void *v) { PropObj *p = pobj(a0); if (p && v) memcpy(p->d + 0x6c, v, 12); }
void eng_TorpedoTubeProperty_SetRight(void *a0, void *v) { PropObj *p = pobj(a0); if (p && v) memcpy(p->d + 0x78, v, 12); }
void eng_ShieldProperty_SetShieldGlowColor(void *a0, void *c) { PropObj *p = pobj(a0); if (p && c) memcpy(p->d + 0x4c, c, 16); }
void eng_BlinkingLightProperty_SetColor(void *a0, void *c) { PropObj *p = pobj(a0); if (p && c) memcpy(p->d + 0x50, c, 16); }

/* ---- weapons ---- */
long eng_WeaponProperty_IsMemberOfGroup(void *a0, int g) { PropObj *p = pobj(a0); if (!p) return 0; return g == 0 ? 1 : (P32(p, 0x50) & (1u << ((g - 1) & 31))) != 0; }  /* 0069a480 */
void *eng_WeaponSystemProperty_GetFiringChainString(void *a0) { PropObj *p = pobj(a0); return p && p->sp[0x7f] ? tg_str_copy(p->sp[0x7f]) : NULL; }   /* App.py takes ownership */
void eng_WeaponSystemProperty_SetFiringChainString(void *a0, void *s) { PropObj *p = pobj(a0); if (p && s) p->sp[0x7f] = (char *)tg_str_copy(s); }
/* TorpedoSystemProperty: four TorpedoAmmoType records of 16 bytes from 0x64: max count, script, launch speed, launch sound (00694200, 00694240) */
long eng_TorpedoSystemProperty_GetMaxTorpedoes(void *a0, int i) { PropObj *p = pobj(a0); return p && i >= 0 && i < 4 ? P32S(p, 0x64 + i * 16) : 0; }
void eng_TorpedoSystemProperty_SetMaxTorpedoes(void *a0, int i, int n) { PropObj *p = pobj(a0); if (p && i >= 0 && i < 4) P32S(p, 0x64 + i * 16) = n; }
const char *eng_TorpedoSystemProperty_GetTorpedoScript(void *a0, int i) { PropObj *p = pobj(a0); return p && i >= 0 && i < 4 ? p->sp[(0x68 + i * 16) >> 2] : NULL; }
void eng_TorpedoSystemProperty_SetTorpedoScript(void *a0, int i, PyObject *s)
{
    PropObj *p = pobj(a0); char *c = NULL; int base = 0x64 + i * 16; float speed = 1.0f; char *snd = NULL;
    if (!p || i < 0 || i >= 4) return;
    if (s && s != Py_None && !appc_to_cstr(s, &c)) { PyErr_Clear(); return; }
    prop_set_string(p, base + 4, c); PF(p, base + 8) = 1.0f; free(p->sp[(base + 12) >> 2]); p->sp[(base + 12) >> 2] = NULL;
    if (c) {                                                           /* the script module supplies GetLaunchSpeed() and GetLaunchSound() */
        if (tg_py_call_module(c, "GetLaunchSpeed", 'f', &speed) == 0) PF(p, base + 8) = speed;
        if (tg_py_call_module(c, "GetLaunchSound", 'z', &snd) == 0) p->sp[(base + 12) >> 2] = snd;
    }
}
