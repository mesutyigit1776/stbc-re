/* Game file lookup for scripts that name data files (models, textures, sounds) with Windows paths.
   Order: the roots in APPC_DATA_ROOTS (':'-separated, first wins; a mod folder goes before the game folder), then the current folder
   (the game folder). '\\' becomes '/', and each path part matches without regard to case, because mods name files in any case.
   Also TGModelManager (g_kModelManager): a record of the loaded model files (no NIF loader yet). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "tg_core.h"

static int find_ci(const char *root, const char *rel, char *out, size_t n)
{
    char cur[1024], part[256]; const char *p = rel; struct stat st;
    snprintf(cur, sizeof cur, "%s", root);
    while (*p) {
        size_t k = 0; DIR *d; struct dirent *e; int found = 0;
        while (*p == '/' || *p == '\\') p++;
        while (*p && *p != '/' && *p != '\\' && k < sizeof part - 1) part[k++] = *p++;
        part[k] = 0; if (!k) break;
        if (!(d = opendir(cur))) return 0;
        while ((e = readdir(d))) if (!strcasecmp(e->d_name, part)) { size_t l = strlen(cur); snprintf(cur + l, sizeof cur - l, "/%s", e->d_name); found = 1; break; }
        closedir(d);
        if (!found) return 0;
    }
    if (stat(cur, &st) != 0) return 0;
    snprintf(out, n, "%s", cur); return 1;
}
/* the real path of a game file, or NULL; the result stays valid until the next call */
const char *tg_asset_resolve(const char *path)
{
    static char out[1024]; const char *roots = getenv("APPC_DATA_ROOTS"); char r[1024];
    if (!path || !*path) return NULL;
    if (roots) {
        const char *s = roots;
        while (*s) { size_t k = strcspn(s, ":"); snprintf(r, sizeof r, "%.*s", (int)k, s); if (k && find_ci(r, path, out, sizeof out)) return out; s += k; if (*s) s++; }
    }
    return find_ci(".", path, out, sizeof out) ? out : NULL;
}

typedef struct { char *name, *file, *env; int ok; } Model;
static Model *models; static int nmodels;
static Model *mfind(const char *n) { int i; if (n) for (i = 0; i < nmodels; i++) if (!strcasecmp(models[i].name, n)) return &models[i]; return NULL; }
long eng_TGModelManager_LoadModel(void *m, char *file, char *name, char *env)
{
    Model *x; const char *key = name ? name : file;
    if (!file) return 0;
    if (!(x = mfind(key))) { models = realloc(models, (nmodels + 1) * sizeof *models); x = &models[nmodels++]; memset(x, 0, sizeof *x); x->name = strdup(key); }
    free(x->file); free(x->env); x->file = strdup(file); x->env = env ? strdup(env) : NULL; x->ok = tg_asset_resolve(file) != NULL;
    if (!x->ok && getenv("APPC_TRACE")) fprintf(stderr, "[Appc] missing model file: %s\n", file);
    return x->ok;
}
void eng_TGModelManager_LoadModelIncremental(void *m, char *file, char *name, char *env) { eng_TGModelManager_LoadModel(m, file, name, env); }
long eng_TGModelManager_IsModelLoaded(void *m, char *n) { Model *x = mfind(n); return x && x->ok; }
long eng_TGModelManager_AddModel(void *m, void *node, char *n) { return 1; }
void *eng_TGModelManager_GetModel(void *m, char *n) { return NULL; }          /* no scene graph yet */
void *eng_TGModelManager_CloneModel(void *m, char *n) { return NULL; }
void *eng_TGModelManager_CopyModel(void *m, char *n) { return NULL; }
PyObject *eng_TGModelManager_GetCamera(void *m, char *n) { Py_INCREF(Py_None); return Py_None; }
PyObject *eng_TGModelManager_CloneCamera(void *m, char *n) { Py_INCREF(Py_None); return Py_None; }
void eng_TGModelManager_FreeModel(void *m, char *n) { Model *x = mfind(n); if (x) x->ok = 0; }
void eng_TGModelManager_FreeAllModels(void *m) { int i; for (i = 0; i < nmodels; i++) models[i].ok = 0; }
void eng_TGModelManager_Refer(void *m, char *n) { }
void eng_TGModelManager_Unrefer(void *m, char *n) { }
void eng_TGModelManager_UnreferAll(void *m, char *n) { }
void eng_TGModelManager_Purge(void *m) { }
long eng_TGModelManager_DoIncrementalLoad(void *m) { return 0; }
void eng_TGModelManager_ClearIncrementalLoadQueue(void *m) { }
