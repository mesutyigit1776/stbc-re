/* Storage for the engine globals that scripts reach through Appc.globals (colors, constants, manager singletons).
   Object globals are allocated on first use. Globals with the same original address share storage (TGColorA_WHITE == NiColorA_WHITE).
   Managers are zeroed placeholders of generous size until the real manager classes exist. */
#include <stdlib.h>
#include <string.h>
#include "appc_runtime.h"

#define MAX_OBJ 256
static struct { unsigned long addr; const char *name; void *mem; } objs[MAX_OBJ];
static int nobjs;

static void init_constant(const char *name, void *mem)
{
    float *f = mem;
    if (!strcmp(name, "NiColorA_WHITE") || !strcmp(name, "TGColorA_WHITE")) { f[0] = f[1] = f[2] = f[3] = 1.0f; }
    else if (!strcmp(name, "NiColorA_BLACK") || !strcmp(name, "TGColorA_BLACK")) { f[0] = f[1] = f[2] = 0.0f; f[3] = 1.0f; }
    else if (!strcmp(name, "NiColor_WHITE")) { f[0] = f[1] = f[2] = 1.0f; }
    else if (!strcmp(name, "NiColor_BLACK")) { f[0] = f[1] = f[2] = 0.0f; }
    else if (!strcmp(name, "NiPoint2_UNIT_X")) { f[0] = 1.0f; f[1] = 0.0f; }
    else if (!strcmp(name, "NiPoint2_UNIT_Y")) { f[0] = 0.0f; f[1] = 1.0f; }
}

static struct { const char *name; void *obj; } overrides[16]; static int nover;
void tg_global_override(const char *name, void *obj) { overrides[nover].name = name; overrides[nover++].obj = obj; }
void *eng_global_get(const struct appc_global *g)
{
    int i; void *mem;
    for (i = 0; i < nover; i++) if (!strcmp(overrides[i].name, g->name)) return overrides[i].obj;
    for (i = 0; i < nobjs; i++)
        if ((g->addr && objs[i].addr == g->addr) || (!g->addr && !strcmp(objs[i].name, g->name))) return objs[i].mem;
    if (nobjs >= MAX_OBJ) return NULL;
    mem = calloc(1, g->size ? g->size : 8192);
    init_constant(g->name, mem);
    objs[nobjs].addr = g->addr; objs[nobjs].name = g->name; objs[nobjs].mem = mem;
    nobjs++;
    return mem;
}
