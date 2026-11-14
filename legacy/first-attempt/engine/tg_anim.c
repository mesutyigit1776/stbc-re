/* Animation state: TGAnimationManagerClass (g_kAnimationManager: name -> keyframe file) and TGAnimNode (per-node animation state).
   Approximations (marked): no keyframe (.KF/.NIF) data is read yet, so GetAnimationLength is 0 and animations finish at once;
   TGAnimNode keeps only the names and the blend time; BlendAnimation / PositionUsingAnimation do nothing. */
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

void prop_set_s(void *, const char *, const char *); const char *prop_get_s(void *, const char *);
void prop_set_d(void *, const char *, double); double prop_get_d(void *, const char *); void prop_set_i(void *, const char *, long); long prop_get_i(void *, const char *);
void *tg_hooked_action_new(const char *cls, void (*on_play)(void *, void *), void *user);

typedef struct { char *name, *file; } Anim;
static Anim *anims; static int nanims;
static int find(const char *n) { int i; if (n) for (i = 0; i < nanims; i++) if (!strcmp(anims[i].name, n)) return i; return -1; }
long eng_TGAnimationManagerClass_LoadAnimation(void *m, char *file, char *name)
{
    int i = find(name ? name : file);
    if (i < 0) { anims = realloc(anims, (nanims + 1) * sizeof *anims); i = nanims++; anims[i].name = strdup(name ? name : file); anims[i].file = NULL; }
    free(anims[i].file); anims[i].file = strdup(file ? file : ""); return 1;
}
long eng_TGAnimationManagerClass_IsLoaded(void *m, char *n) { return find(n) >= 0; }
void eng_TGAnimationManagerClass_FreeAnimation(void *m, char *n) { int i = find(n); if (i >= 0) { free(anims[i].name); free(anims[i].file); anims[i] = anims[--nanims]; } }
void eng_TGAnimationManagerClass_ClearAnimations(void *m) { while (nanims) { nanims--; free(anims[nanims].name); free(anims[nanims].file); } }
double eng_TGAnimationManagerClass_GetAnimationLength(void *m, char *n) { return 0.0; }
long eng_TGAnimationManagerClass_MapDefaultAnimation(void *m, PyObject *node, char *n) { return find(n) >= 0; }
long eng_TGAnimationManagerClass_UseAnimation(void *m, PyObject *node, char *n, int a, float b, float c) { return find(n) >= 0; }
long eng_TGAnimationManagerClass_BlendAnimation(void *m, PyObject *node, float a, float b, int c, int d, int e, char *n) { return find(n) >= 0; }
long eng_TGAnimationManagerClass_UseDefault(void *m, PyObject *node, char *n) { return 1; }
long eng_TGAnimationManagerClass_PositionUsingAnimation(void *m, PyObject *node, char *n) { return find(n) >= 0; }

void eng_TGAnimNode_UseAnimation(void *p, char *n, int a, float b, float c, PyObject *e, char *f) { if (p) prop_set_s(p, "Exclusive", n); }
void eng_TGAnimNode_SetExclusiveAnimation(void *p, char *n, float b, int a, PyObject *e, char *f) { if (p) prop_set_s(p, "Exclusive", n); }
void eng_TGAnimNode_SetExclusiveAnimationUseDefault(void *p, char *n, float b, int a, PyObject *e, char *f) { if (p) prop_set_s(p, "Exclusive", n); }
void eng_TGAnimNode_SetNonExclusiveAnimation(void *p, char *n, float b, int a, PyObject *e, char *f) { if (p) prop_set_s(p, "NonExclusive", n); }
void eng_TGAnimNode_StopNonExclusiveAnimation(void *p, char *n) { if (p) prop_set_s(p, "NonExclusive", NULL); }
void eng_TGAnimNode_Stop(void *p) { if (p) { prop_set_s(p, "Exclusive", NULL); prop_set_s(p, "NonExclusive", NULL); } }
void eng_TGAnimNode_Copy(void *p, void *q) { if (p && q) { prop_set_s(p, "Exclusive", prop_get_s(q, "Exclusive")); prop_set_d(p, "BlendTime", prop_get_d(q, "BlendTime")); } }
long eng_TGAnimNode_IsAnimate(void *p) { return 0; }
void eng_TGAnimNode_UseAnimationPosition(void *p, char *n) { }
void eng_TGAnimNode_SetRootNode(void *p, char *n) { if (p) prop_set_s(p, "RootNode", n); }
PyObject *eng_TGAnimNode_GetRootNode(void *p) { Py_INCREF(Py_None); return Py_None; }
PyObject *eng_TGAnimNode_FindNode(void *p, char *n) { Py_INCREF(Py_None); return Py_None; }
void eng_TGAnimNode_SetBlendTime(void *p, float t) { if (p) prop_set_d(p, "BlendTime", t); }
double eng_TGAnimNode_GetBlendTime(void *p) { return p ? prop_get_d(p, "BlendTime") : 0.0; }

/* TGAnimPosition: a timed action that places a node at the start of an animation; finishes at once */
void *eng_TGAnimPosition_Create(PyObject *node, char *n) { return tg_hooked_action_new("TGAnimPosition", NULL, NULL); }
void eng_TGAnimPosition_Play(void *p) { extern void eng_TGAction_Play(void *); eng_TGAction_Play(p); }
