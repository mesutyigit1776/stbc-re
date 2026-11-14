/* Mission layer: UtopiaModule, VarManager, TGConfigMapping, TGSystemWrapper, ScriptObject / Game / Episode / Mission, ObjectGroup.
   Behavior from stbc.exe: ScriptObject 0040a030..0040a4e0, Game 00405ad0..00407580, Episode 004040a0..00404d90, Mission 00408070..00409270,
   VarManagerClass 0044b310.., TGConfigMapping 0071cae.., UtopiaModule::Update 0043b4f0 (frame order is driven by engine_tick). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

extern double tg_game_time, tg_real_time;
static char *dup(const char *s) { return s ? strdup(s) : NULL; }
static void *arg_ptr2(PyObject *o) { void *p = NULL; if (!o || o == Py_None) return NULL; if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }
static int streq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

/* ================================================= UtopiaModule ================================================= */
static struct {
    float time_rate, time_scale; int paused, direct_start, test_menu_state;
    int is_client, multiplayer, is_host, processing_packets, modem, loading, player_number, ignore_client_id; long unused_client_id;
    char *game_name, *game_path, *data_path, *save_file, *load_file, *captain; 
    int max_torp[4], starbase_torp[4];
    float ff_tolerance, ff_current, ff_warning, tractor_time, tractor_warning, tractor_max;
    float unit_factor;
} U = {1.0f, 1.0f, 0, 0, 0};
double eng_UtopiaModule_GetGameTime(void *u) { return tg_game_time; }
void eng_UtopiaModule_SetGameTime(void *u, float t) { tg_game_time = t; }
double eng_UtopiaModule_GetRealTime(void *u) { return tg_real_time; }
long eng_UtopiaModule_IsGamePaused(void *u) { return U.paused == 2 || U.paused == 3; }
void eng_UtopiaModule_SetTimeRate(void *u, float v) { U.time_rate = v; }
void eng_UtopiaModule_SetTimeScale(void *u, float v) { U.time_scale = v; }
long eng_UtopiaModule_IsDirectStart(void *u) { return U.direct_start; }
void tg_sound_pause_all(int on);
/* UtopiaModule::SetPaused 0043bbd0 -> UtopiaApp::SetPaused 00438e60: the SFX and voice sounds pause with the game and go on when it goes on (music does not) */
void eng_UtopiaModule_Pause(void *u, int p) { int was = U.paused == 2 || U.paused == 3; U.paused = p ? 2 : 0; if (p && !was) tg_sound_pause_all(1); else if (!p && was) tg_sound_pause_all(0); }
void eng_UtopiaModule_ForceUnpause(void *u) { int was = U.paused == 2 || U.paused == 3; U.paused = 0; if (was) tg_sound_pause_all(0); }
long eng_UtopiaModule_LoadEpisodeSounds(void *u, char *a, char *b) { return 1; }
long eng_UtopiaModule_CDCheck(void *u, char *s) { return 1; }
void eng_UtopiaModule_SaveToFile(void *u, char *s) { }
void eng_UtopiaModule_LoadFromFile(void *u, char *s) { }
void eng_UtopiaModule_SetLoadFromFileName(void *u, char *s) { free(U.load_file); U.load_file = dup(s); }
void eng_UtopiaModule_SetInternalLoadFileName(void *u, char *s) { free(U.save_file); U.save_file = dup(s); }
const char *eng_UtopiaModule_GetSaveFilename(void *u) { return U.save_file; }
const char *eng_UtopiaModule_GetLoadFilename(void *u) { return U.load_file; }
long eng_UtopiaModule_SaveMissionState(void *u, char *s) { return 0; }
long eng_UtopiaModule_LoadMissionState(void *u, char *s) { return 0; }
void eng_UtopiaModule_SetIsClient(void *u, int v) { U.is_client = v != 0; }
long eng_UtopiaModule_IsClient(void *u) { return U.is_client; }
void eng_UtopiaModule_SetMultiplayer(void *u, int v) { U.multiplayer = v != 0; }
long eng_UtopiaModule_IsMultiplayer(void *u) { return U.multiplayer; }
void eng_UtopiaModule_SetIsHost(void *u, int v) { U.is_host = v != 0; }
long eng_UtopiaModule_IsHost(void *u) { return U.is_host; }
long eng_UtopiaModule_DetermineModemConnection(void *u) { return 0; }
long eng_UtopiaModule_IsModemConnection(void *u) { return U.modem; }
void eng_UtopiaModule_SetProcessingPackets(void *u, int v) { U.processing_packets = v != 0; }
long eng_UtopiaModule_IsProcessingPackets(void *u) { return U.processing_packets; }
void eng_UtopiaModule_SetPlayerNumber(void *u, int v) { U.player_number = v; }
long eng_UtopiaModule_GetPlayerNumber(void *u) { return U.player_number; }
const char *eng_UtopiaModule_GetGameVersion(void) { return "1.1"; }          /* the original reads a string at 0x8d9af4 (not decoded) */
long eng_UtopiaModule_IsLoading(void *u) { return U.loading; }
long eng_UtopiaModule_InitializeNetwork(void *u, char *a, void *b, int c) { return 0; }
void eng_UtopiaModule_TerminateNetwork(void *u) { }
void *eng_UtopiaModule_GetNetwork(void *u) { return NULL; }
void eng_UtopiaModule_CreateGameSpy(void *u) { }
void *eng_UtopiaModule_GetGameSpy(void *u) { return NULL; }
void eng_UtopiaModule_TerminateGameSpy(void *u) { }
void eng_UtopiaModule_SetUnusedClientID(void *u, long v) { U.unused_client_id = v; }
long eng_UtopiaModule_GetUnusedClientID(void *u) { return U.unused_client_id; }
void eng_UtopiaModule_IncrementClientID(void *u) { U.unused_client_id++; }
void eng_UtopiaModule_SetIgnoreClientIDForObjectCreation(void *u, int v) { U.ignore_client_id = v != 0; }
long eng_UtopiaModule_IsIgnoreClientIDForObjectCreation(void *u) { return U.ignore_client_id; }
void eng_UtopiaModule_SetMaxTorpedoLoad(void *u, int i, int v) { if (i >= 0 && i < 4) U.max_torp[i] = v; }
long eng_UtopiaModule_GetMaxTorpedoLoad(void *u, int i) { return i >= 0 && i < 4 ? U.max_torp[i] : 0; }
void eng_UtopiaModule_SetCurrentStarbaseTorpedoLoad(void *u, int i, int v) { if (i >= 0 && i < 4) U.starbase_torp[i] = v; }
long eng_UtopiaModule_GetCurrentStarbaseTorpedoLoad(void *u, int i) { return i >= 0 && i < 4 ? U.starbase_torp[i] : 0; }
void eng_UtopiaModule_SetGameName(void *u, char *s) { free(U.game_name); U.game_name = dup(s); }
void eng_UtopiaModule_SetGamePath(void *u, char *s) { free(U.game_path); U.game_path = dup(s); }
void eng_UtopiaModule_SetDataPath(void *u, char *s) { free(U.data_path); U.data_path = dup(s); }
const char *eng_UtopiaModule_GetGameName(void *u) { return U.game_name ? U.game_name : ""; }
const char *eng_UtopiaModule_GetGamePath(void *u) { return U.game_path ? U.game_path : ""; }
const char *eng_UtopiaModule_GetDataPath(void *u) { return U.data_path ? U.data_path : ""; }
double eng_UtopiaModule_GetFriendlyFireTolerance(void *u) { return U.ff_tolerance; }
void eng_UtopiaModule_SetMaxFriendlyFire(void *u, float v) { U.ff_tolerance = v; }
double eng_UtopiaModule_GetCurrentFriendlyFire(void *u) { return U.ff_current; }
void eng_UtopiaModule_SetCurrentFriendlyFire(void *u, float v) { U.ff_current = v; }
double eng_UtopiaModule_GetFriendlyFireWarningPoints(void *u) { return U.ff_warning; }
void eng_UtopiaModule_SetFriendlyFireWarningPoints(void *u, float v) { U.ff_warning = v; }
double eng_UtopiaModule_GetFriendlyTractorTime(void *u) { return U.tractor_time; }
void eng_UtopiaModule_SetFriendlyTractorTime(void *u, float v) { U.tractor_time = v; }
double eng_UtopiaModule_GetFriendlyTractorWarning(void *u) { return U.tractor_warning; }
void eng_UtopiaModule_SetFriendlyTractorWarning(void *u, float v) { U.tractor_warning = v; }
double eng_UtopiaModule_GetMaxFriendlyTractorTime(void *u) { return U.tractor_max; }
void eng_UtopiaModule_SetMaxFriendlyTractorTime(void *u, float v) { U.tractor_max = v; }
long eng_UtopiaModule_GetTestMenuState(void *u) { return U.test_menu_state; }
double eng_UtopiaModule_ConvertGameUnitsToKilometers(float v) { return v * (U.unit_factor ? U.unit_factor : 1.0f); }
double eng_UtopiaModule_ConvertKilometersToGameUnits(float v) { return v / (U.unit_factor ? U.unit_factor : 1.0f); }
void eng_UtopiaModule_SetGameUnitConversionFactor(float v) { U.unit_factor = v; }
void eng_UtopiaModule_RenderDefaultCamera(void *u) { }
void *eng_UtopiaModule_GetCamera(void *u) { return NULL; }
/* the captain name is a TGString copy */
void eng_UtopiaModule_SetCaptainName(void *u, void *s)
{
    extern const char *eng_TGString_GetCString(void *); extern void *eng_new_TGString(char *);
    free(U.captain); U.captain = s ? strdup(eng_TGString_GetCString(s)) : NULL;
}
void *eng_UtopiaModule_GetCaptainName(void *u) { extern void *eng_new_TGString(char *); return eng_new_TGString(U.captain ? U.captain : ""); }

/* ================================================= VarManager ================================================= */
struct vvar { char *name; int is_str; double f; char *s; struct vvar *next; };
struct vscope { char *name; struct vvar *vars; struct vscope *next; };
static struct vscope *vscopes;
static struct vscope *vscope_find(const char *n, int create)
{
    struct vscope *s; if (!n) n = "";
    for (s = vscopes; s; s = s->next) if (!strcmp(s->name, n)) return s;
    if (!create) return NULL;
    s = calloc(1, sizeof *s); s->name = strdup(n); s->next = vscopes; vscopes = s; return s;
}
static struct vvar *vvar_find(struct vscope *s, const char *n, int is_str, int create)
{
    struct vvar *v; if (!n) n = "";
    for (v = s->vars; v; v = v->next) if (v->is_str == is_str && !strcmp(v->name, n)) return v;
    if (!create) return NULL;
    v = calloc(1, sizeof *v); v->name = strdup(n); v->is_str = is_str; v->next = s->vars; s->vars = v; return v;
}
void *eng_new_VarManagerClass(void) { return calloc(1, 64); }
void eng_delete_VarManagerClass(void *p) { }
void eng_VarManagerClass_SetFloatVariable(void *m, char *sc, char *n, float v) { vvar_find(vscope_find(sc, 1), n, 0, 1)->f = v; }
double eng_VarManagerClass_GetFloatVariable(void *m, char *sc, char *n) { struct vscope *s = vscope_find(sc, 0); struct vvar *v = s ? vvar_find(s, n, 0, 0) : NULL; return v ? (float)v->f : 0.0; }
void eng_VarManagerClass_SetStringVariable(void *m, char *sc, char *n, char *val) { struct vvar *v = vvar_find(vscope_find(sc, 1), n, 1, 1); free(v->s); v->s = dup(val ? val : ""); }
const char *eng_VarManagerClass_GetStringVariable(void *m, char *sc, char *n) { struct vscope *s = vscope_find(sc, 0); struct vvar *v = s ? vvar_find(s, n, 1, 0) : NULL; return v ? v->s : NULL; }
void eng_VarManagerClass_DeleteAllVariables(void *m) { vscopes = NULL; }
void eng_VarManagerClass_DeleteAllScopedVariables(void *m, char *sc) { }       /* the original body is empty (0044b750) */
long eng_VarManagerClass_MakeEpisodeEventType(void *m, int n) { return 0x80c001 + n; }

/* ================================================= TGConfigMapping ================================================= */
/* typed values: a value is only returned by the getter of its own type (0 float, 1 int, 2 string); HasValue accepts any type */
struct cval { char *key; int type; double f; long i; char *s; struct cval *next; };
struct csec { char *name; struct cval *vals; struct csec *next; };
static struct csec *csecs;
static struct csec *csec_find(const char *n, int create)
{
    struct csec *s; if (!n) n = "";
    for (s = csecs; s; s = s->next) if (!strcmp(s->name, n)) return s;
    if (!create) return NULL;
    s = calloc(1, sizeof *s); s->name = strdup(n); s->next = csecs; csecs = s; return s;
}
static struct cval *cval_find(struct csec *s, const char *k) { struct cval *v; if (!k) k = ""; for (v = s ? s->vals : NULL; v; v = v->next) if (!strcmp(v->key, k)) return v; return NULL; }
static struct cval *cval_set(const char *sec, const char *k, int type)
{
    struct csec *s = csec_find(sec, 1); struct cval *v = cval_find(s, k);
    if (!v) { v = calloc(1, sizeof *v); v->key = strdup(k ? k : ""); v->next = s->vals; s->vals = v; }
    v->type = type; free(v->s); v->s = NULL; return v;
}
double eng_TGConfigMapping_GetFloatValue(void *m, char *sec, char *k) { struct cval *v = cval_find(csec_find(sec, 0), k); return v && v->type == 0 ? v->f : 0.0; }
long eng_TGConfigMapping_GetIntValue(void *m, char *sec, char *k) { struct cval *v = cval_find(csec_find(sec, 0), k); return v && v->type == 1 ? v->i : 0; }
const char *eng_TGConfigMapping_GetStringValue(void *m, char *sec, char *k) { struct cval *v = cval_find(csec_find(sec, 0), k); return v && v->type == 2 ? v->s : NULL; }
void *eng_TGConfigMapping_GetTGStringValue(void *m, char *sec, char *k) { extern void *eng_new_TGString(char *); struct cval *v = cval_find(csec_find(sec, 0), k); return v && v->type == 2 ? eng_new_TGString(v->s) : NULL; }
void eng_TGConfigMapping_SetFloatValue(void *m, char *sec, char *k, float f) { cval_set(sec, k, 0)->f = f; }
void eng_TGConfigMapping_SetIntValue(void *m, char *sec, char *k, int i) { cval_set(sec, k, 1)->i = i; }
void eng_TGConfigMapping_SetStringValue(void *m, char *sec, char *k, char *s) { cval_set(sec, k, 2)->s = dup(s ? s : ""); }
void eng_TGConfigMapping_SetTGStringValue(void *m, char *sec, char *k, void *t) { extern const char *eng_TGString_GetCString(void *); cval_set(sec, k, 2)->s = dup(t ? eng_TGString_GetCString(t) : ""); }
long eng_TGConfigMapping_HasValue(void *m, char *sec, char *k) { return cval_find(csec_find(sec, 0), k) != NULL; }
/* File format (our own, the original layout is not decoded yet): "[Section]" lines, then "key = t:value" with t = i (int),
   f (float) or s (string). Load merges into the current values and returns 1 if the file was read, 0 if it is missing. */
long eng_TGConfigMapping_LoadConfigFile(void *m, char *path)
{
    FILE *f; char line[1024], sec[256] = ""; char *p, *eq, *val, *e; size_t n;
    f = path ? fopen(path, "r") : NULL;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        p = line; while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') { e = strchr(p, ']'); if (e) { *e = 0; strncpy(sec, p + 1, sizeof sec - 1); sec[sizeof sec - 1] = 0; } continue; }
        eq = strchr(p, '='); if (!eq) continue;
        *eq = 0; for (e = eq - 1; e >= p && (*e == ' ' || *e == '\t'); e--) *e = 0;
        val = eq + 1; while (*val == ' ' || *val == '\t') val++;
        if (val[0] && val[1] == ':') {
            switch (val[0]) {
            case 'i': cval_set(sec, p, 1)->i = atol(val + 2); break;
            case 'f': cval_set(sec, p, 0)->f = atof(val + 2); break;
            case 's': cval_set(sec, p, 2)->s = dup(val + 2); break;
            }
        }
    }
    fclose(f);
    return 1;
}
long eng_TGConfigMapping_SaveConfigFile(void *m, char *path)
{
    FILE *f; struct csec *s; struct cval *v;
    f = path ? fopen(path, "w") : NULL;
    if (!f) return 0;
    for (s = csecs; s; s = s->next) {
        fprintf(f, "[%s]\n", s->name);
        for (v = s->vals; v; v = v->next) {
            if (v->type == 0) fprintf(f, "%s = f:%.9g\n", v->key, v->f);
            else if (v->type == 1) fprintf(f, "%s = i:%ld\n", v->key, v->i);
            else fprintf(f, "%s = s:%s\n", v->key, v->s ? v->s : "");
        }
    }
    fclose(f);
    return 1;
}

/* ================================================= TGSystemWrapper ================================================= */
double eng_TGSystemWrapperClass_GetTimeSinceFrameStart(void *s) { return 0.0; }
double eng_TGSystemWrapperClass_GetTimeInSeconds(void *s) { return tg_real_time; }
long eng_TGSystemWrapperClass_GetTimeInMilliseconds(void *s) { return (long)(tg_real_time * 1000.0); }
double eng_TGSystemWrapperClass_GetTimeElapsedInSeconds(void *s) { return 0.0; }
long eng_TGSystemWrapperClass_GetTimeElapsedInMilliseconds(void *s) { return 0; }
long eng_TGSystemWrapperClass_GetUpdateNumber(void *s) { return 0; }
double eng_TGSystemWrapperClass_GetVerticalAspectRatio(void *s) { return 0.75; }
long eng_TGSystemWrapperClass_IsForeground(void *s) { return 1; }
void eng_TGSystemWrapperClass_SetRandomSeed(void *s, int v) { srand((unsigned)v); }
void eng_TGSystemWrapperClass_TakeScreenshot(void *s, int a, char *b) { }
void eng_TGSystemWrapperClass_SetDefaultScreenShotPath(void *s, char *p) { }
long eng_TGSystemWrapperClass_GetIniInt(void *s, char *a, char *b, int d, char *f) { return d; }
double eng_TGSystemWrapperClass_GetIniFloat(void *s, char *a, char *b, float d, char *f) { return d; }
long eng_TGSystemWrapperClass_GetIniString(void *s, char *a, char *b, char *d, char *o, int n, char *f) { return 0; }

/* ================================================= ObjectGroup (names only until the object layer exists) ================================================= */
typedef struct { TGObject o; char **names; int n; unsigned flags; PyObject **infos; } OGroup;
static OGroup *grp(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x805) ? p : NULL; }
static int g_index(OGroup *g, const char *n) { int i; for (i = 0; i < g->n; i++) if (streq(g->names[i], n)) return i; return -1; }
static OGroup **all_groups; static int n_groups;
static OGroup *grp_add(OGroup *g) { all_groups = realloc(all_groups, (n_groups + 1) * sizeof *all_groups); all_groups[n_groups++] = g; return g; }
void *eng_new_ObjectGroup(void) { return grp_add(tg_new("ObjectGroup", sizeof(OGroup), 0)); }
/* ObjectGroup::EnteredSet 0059d140 / ExitedSet 0059d1d0 (the ctor 0059c7b0 binds ET_ENTERED_SET; SetEventFlag 4 binds ET_EXITED_SET):
   an object whose name is in the group entered or left a set -> ET_OBJECT_GROUP_OBJECT_ENTERED_SET (flag 2) / _EXITED_SET (flag 4) to the group,
   a TGObjPtrEvent with the object's id. Here one native broadcast handler serves all groups. */
static void group_set_event(TGEvent *e, unsigned flag, const char *et)
{
    extern const char *eng_BaseObjectClass_GetName(void *);
    const char *n; int i, k = n_groups;
    if (!e->dest || !tg_is_ours(e->dest) || !(n = eng_BaseObjectClass_GetName(e->dest))) return;
    for (i = 0; i < k; i++) {
        OGroup *g = all_groups[i]; TGEvent *x;
        if (g->o.dead || !(g->flags & flag) || g_index(g, n) < 0) continue;
        x = tg_ev_new("TGObjPtrEvent", 0); x->type = (unsigned)tg_const(et); x->dest = &g->o; x->v.p = e->dest; x->ref_id = ((TGObject *)e->dest)->id; tg_add_event(x);
    }
}
static void on_group_entered(TGEvent *e) { group_set_event(e, 2, "ET_OBJECT_GROUP_OBJECT_ENTERED_SET"); }
static void on_group_exited(TGEvent *e) { group_set_event(e, 4, "ET_OBJECT_GROUP_OBJECT_EXITED_SET"); }
void eng_delete_ObjectGroup(void *p) { if (grp(p)) ((TGObject *)p)->dead = 1; }
void eng_ObjectGroup_SetEventFlag(void *p, int f) { OGroup *g = grp(p); if (g) g->flags |= (unsigned)f; }
void eng_ObjectGroup_ClearEventFlag(void *p, int f) { OGroup *g = grp(p); if (g) g->flags &= ~(unsigned)f; }
long eng_ObjectGroup_IsEventFlagSet(void *p, int f) { OGroup *g = grp(p); return g && (g->flags & (unsigned)f) ? 1 : 0; }
void eng_ObjectGroup_AddName(void *p, char *n)
{
    OGroup *g = grp(p); int i;
    if (!g || !n || g_index(g, n) >= 0) return;
    g->names = realloc(g->names, (g->n + 1) * sizeof *g->names); g->names[g->n] = strdup(n);
    g->infos = realloc(g->infos, (g->n + 1) * sizeof *g->infos); g->infos[g->n] = NULL; g->n++; (void)i;
}
long eng_ObjectGroup_RemoveName(void *p, char *n)
{
    OGroup *g = grp(p); int i = g ? g_index(g, n) : -1;
    if (i < 0) return 0;
    free(g->names[i]); Py_XDECREF(g->infos[i]);
    memmove(g->names + i, g->names + i + 1, (g->n - i - 1) * sizeof *g->names);
    memmove(g->infos + i, g->infos + i + 1, (g->n - i - 1) * sizeof *g->infos); g->n--; return 1;
}
long eng_ObjectGroup_RemoveAllNames(void *p) { OGroup *g = grp(p); if (!g) return 0; while (g->n) eng_ObjectGroup_RemoveName(g, g->names[0]); return 1; }
long eng_ObjectGroup_IsNameInGroup(void *p, char *n) { OGroup *g = grp(p); return g && g_index(g, n) >= 0; }
long eng_ObjectGroup_GetNumActiveObjects(void *p) { return 0; }          /* active objects need the set layer */
PyObject *eng_ObjectGroup_GetNameTuple(void *p)
{
    OGroup *g = grp(p); PyObject *t; int i;
    if (!g) { Py_INCREF(Py_None); return Py_None; }
    t = PyTuple_New(g->n); for (i = 0; i < g->n; i++) PyTuple_SetItem(t, i, PyString_FromString(g->names[i]));
    return t;
}
/* ObjectGroup::GetActiveObjectTuple 0059ce40 / InSet 0059cee0: the group's objects that exist in a set (the exe keeps this list from
   enter/exit-set events; here it is looked up by name) */
static PyObject *active_tuple(void *p, void *set)
{
    extern void *tg_space_find_active(const char *, void *); OGroup *g = grp(p); PyObject *l = PyList_New(0), *t; int i;
    if (g) for (i = 0; i < g->n; i++) { void *o = tg_space_find_active(g->names[i], set); if (o) { PyObject *x = appc_pyobj(o, ((TGObject *)o)->cls->name); if (x) { PyList_Append(l, x); Py_DECREF(x); } else PyErr_Clear(); } }
    t = PyList_AsTuple(l); Py_DECREF(l); return t;
}
PyObject *eng_ObjectGroup_GetActiveObjectTuple(void *p) { return active_tuple(p, NULL); }
PyObject *eng_ObjectGroup_GetActiveObjectTupleInSet(void *p, void *set) { return set ? active_tuple(p, set) : PyTuple_New(0); }
/* ForceToGroup(x) (py_ObjectGroup_ForceToGroup 0059d5b0): a string becomes a new group with that name; a group instance stays itself;
   a sequence of one item is ForceToGroup(item); a longer sequence becomes a new group with every string and the names of every item
   that forces to a group. Anything else gives None. */
static void *force_group(PyObject *x)
{
    OGroup *g; void *p; int i, n;
    if (!x || x == Py_None) return NULL;
    if (PyString_Check(x)) { g = eng_new_ObjectGroup(); eng_ObjectGroup_AddName(g, PyString_AsString(x)); return g; }
    if (PyInstance_Check(x) || !PySequence_Check(x)) { p = arg_ptr2(x); return grp(p) ? p : NULL; }   /* (1.5.2 instances pass PySequence_Check) */
    n = PySequence_Length(x);
    if (n == 1) { PyObject *it = PySequence_GetItem(x, 0); p = force_group(it); Py_XDECREF(it); return p; }
    g = eng_new_ObjectGroup();
    for (i = 0; i < n; i++) {
        PyObject *it = PySequence_GetItem(x, i); OGroup *h; int k;
        if (it && PyString_Check(it)) eng_ObjectGroup_AddName(g, PyString_AsString(it));
        else if ((h = grp(force_group(it)))) for (k = 0; k < h->n; k++) eng_ObjectGroup_AddName(g, h->names[k]);
        Py_XDECREF(it);
    }
    return g;
}
PyObject *eng_ObjectGroup_ForceToGroup(PyObject *x)                         /* returns the Python instance itself for a group instance */
{
    void *g; PyObject *y = x;
    while (y && !PyInstance_Check(y) && PySequence_Check(y) && !PyString_Check(y) && PySequence_Length(y) == 1) { PyObject *it = PySequence_GetItem(y, 0); Py_XDECREF(it); y = it; }
    if (y && PyInstance_Check(y) && grp(arg_ptr2(y))) { Py_INCREF(y); return y; }
    if (!(g = force_group(x))) { Py_INCREF(Py_None); return Py_None; }
    return appc_pyobj(g, "ObjectGroup");
}
void *eng_ObjectGroup_FromModule(char *mod, char *name)
{
    PyObject *m, *a; void *g = NULL;
    m = PyImport_ImportModule(mod); if (!m) { PyErr_Clear(); return NULL; }
    a = PyObject_GetAttrString(m, name); Py_DECREF(m); if (!a) { PyErr_Clear(); return NULL; }
    g = force_group(a); Py_DECREF(a); return g;
}
void *eng_new_ObjectGroupWithInfo(void) { return grp_add(tg_new("ObjectGroupWithInfo", sizeof(OGroup), 0)); }
void eng_delete_ObjectGroupWithInfo(void *p) { eng_delete_ObjectGroup(p); }
void *eng_ObjectGroupWithInfo_Cast(PyObject *o) { void *p = arg_ptr2(o); return tg_is_ours(p) && tg_is_type(p, 0x806) ? p : NULL; }
void eng_ObjectGroupWithInfo_AddNameAndInfo(void *p, char *n, PyObject *info)
{
    OGroup *g = grp(p); int i;
    if (!g || !n) return;
    eng_ObjectGroup_AddName(g, n); i = g_index(g, n);
    if (i >= 0) { Py_XDECREF(g->infos[i]); Py_XINCREF(info); g->infos[i] = info; }
}
void eng_ObjectGroupWithInfo_SetInfoForName(void *p, char *n, PyObject *info) { OGroup *g = grp(p); int i = g ? g_index(g, n) : -1; if (i >= 0) { Py_XDECREF(g->infos[i]); Py_XINCREF(info); g->infos[i] = info; } }
PyObject *eng_ObjectGroupWithInfo_GetInfo(void *p, char *n)
{
    OGroup *g = grp(p); int i = g ? g_index(g, n) : -1;
    if (i < 0 || !g->infos[i]) { Py_INCREF(Py_None); return Py_None; }
    Py_INCREF(g->infos[i]); return g->infos[i];
}

/* ================================================= ScriptObject / Game / Episode / Mission ================================================= */
#define ET_NEW_GAME_  0
enum { ET_DELETE_ME = 1, ET_LOAD_EPISODE = 0x80000c, ET_LOAD_MISSION = 0x80000d, ET_SET_PLAYER = 0x80000e };   /* values from re/appc_consts.tsv */

typedef struct SObj {
    TGObject o; char *module, *onidle; unsigned counter; int assets_loaded;
    char **baseline; int nbase; char **persist; int npersist; TGEvent *init_ev, *term_ev; void *database;
} SObj;
typedef struct MisObj { SObj s; OGroup *groups[4]; } MisObj;
typedef struct EpObj { SObj s; MisObj *mission; TGEvent *done_ev; char **goals; int ngoals; } EpObj;
typedef struct GameObj {
    SObj s; long score, rating, kills, torps_fired, torps_hit; char *last_saved; void *player; OGroup *player_group; int god;
    TGEvent *preload_ev, *ended_ev; EpObj *episode; TGEvent *done_ev; int camera_id;
} GameObj;
static GameObj *current_game;
static float difficulty_mult[6] = {1.25f, 1.25f, 1.0f, 1.0f, 0.75f, 0.75f}, default_mult[6] = {1.25f, 1.25f, 1.0f, 1.0f, 0.75f, 0.75f};
static int difficulty = 1; static char *hardpoint_file;

static SObj *sobj(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x801) ? p : NULL; }

/* modules loaded when the object is created are not purged when it ends (InitLoadedModuleNames 0040a160 / purgePythonModules 006f80c0) */
static void snapshot_modules(SObj *s)
{
    PyObject *d = PyImport_GetModuleDict(), *keys; int i, n;
    keys = PyDict_Keys(d); n = PyList_Size(keys);
    s->baseline = calloc(n ? n : 1, sizeof *s->baseline); s->nbase = 0;
    for (i = 0; i < n; i++) { PyObject *k = PyList_GetItem(keys, i); if (PyString_Check(k)) s->baseline[s->nbase++] = strdup(PyString_AsString(k)); }
    Py_DECREF(keys);
}
static int in_list(char **l, int n, const char *name) { int i; for (i = 0; i < n; i++) if (!strcmp(l[i], name)) return 1; return 0; }
static void purge_modules(SObj *s)
{
    PyObject *d = PyImport_GetModuleDict(), *keys; int i, n;
    keys = PyDict_Keys(d); n = PyList_Size(keys);
    for (i = 0; i < n; i++) {
        PyObject *k = PyList_GetItem(keys, i); char *name;
        if (!PyString_Check(k)) continue;
        name = PyString_AsString(k);
        if (in_list(s->baseline, s->nbase, name) || in_list(s->persist, s->npersist, name)) continue;
        PyDict_DelItemString(d, name);
    }
    Py_DECREF(keys); PyErr_Clear();
}
static void *sobj_new(const char *cls, size_t size, const char *module, unsigned base_event)
{
    SObj *s = tg_new(cls, size, 0);
    s->module = dup(module); s->counter = base_event; snapshot_modules(s);
    return s;
}
/* ScriptObject::CallScriptFunction 0040a110: Module.name(pSelf); a missing function is ignored, errors are printed */
static int script_call(SObj *s, const char *fname)
{
    PyObject *m, *f, *self, *args, *r; int ok = 0;
    if (!s->module) return 0;
    m = PyImport_ImportModule(s->module);
    if (!m) { PyErr_Print(); PyErr_Clear(); return 0; }
    f = PyObject_GetAttrString(m, (char *)fname); Py_DECREF(m);
    if (!f) { PyErr_Clear(); return 0; }
    self = appc_pyobj(s, s->o.cls->name);
    if (self) { args = Py_BuildValue("(O)", self); r = PyEval_CallObject(f, args); Py_DECREF(args); Py_DECREF(self); Py_XDECREF(r); ok = r != NULL; }
    if (PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); }
    Py_DECREF(f); return ok;
}
static void py_call_mod(const char *mod, const char *fn)           /* callPythonModuleFunction(mod, fn) without arguments */
{
    PyObject *m = PyImport_ImportModule(mod), *f, *r;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)fn); Py_DECREF(m);
    if (!f) { PyErr_Clear(); return; }
    r = PyEval_CallObject(f, NULL); Py_DECREF(f); Py_XDECREF(r);
    if (PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); }
}

/* ---- ScriptObject python API ---- */
void *eng_new_ScriptObject(char *module, int base) { return sobj_new("ScriptObject", sizeof(SObj), module, (unsigned)base); }
void eng_delete_ScriptObject(void *p) { if (sobj(p)) ((TGObject *)p)->dead = 1; }
long eng_ScriptObject_GetInstanceNextEventType(void *p) { SObj *s = sobj(p); return s ? (long)(++s->counter) : 0; }
const char *eng_ScriptObject_GetScript(void *p) { SObj *s = sobj(p); return s ? s->module : NULL; }
void eng_ScriptObject_SetEventCount(void *p, int v) { SObj *s = sobj(p); if (s) s->counter = (unsigned)v; }
void eng_ScriptObject_SetOnIdleFunc(void *p, char *n) { SObj *s = sobj(p); if (s) { free(s->onidle); s->onidle = dup(n); } }
void eng_ScriptObject_SetAssetsLoaded(void *p, int v) { SObj *s = sobj(p); if (s) s->assets_loaded = v != 0; }
long eng_ScriptObject_AreAssetsLoaded(void *p) { SObj *s = sobj(p); return s && s->assets_loaded; }
extern void *tg_localization_load(const char *path);
void *eng_ScriptObject_SetDatabase(void *p, char *n) { SObj *s = sobj(p); if (!s || !n) return NULL; s->database = tg_localization_load(n); return s->database; }
void *eng_ScriptObject_GetDatabase(void *p) { SObj *s = sobj(p); return s ? s->database : NULL; }
void eng_ScriptObject_AddPersistentModule(void *p, char *n)
{
    SObj *s = sobj(p); if (!s || !n) return;
    s->persist = realloc(s->persist, (s->npersist + 1) * sizeof *s->persist); s->persist[s->npersist++] = strdup(n);
}
static void sobj_preload(SObj *s) { script_call(s, "PreLoadAssets"); }                       /* vslot 0x54 */
static void sobj_initialize(SObj *s, TGEvent *ev)                                             /* ScriptObject::Initialize 0040a030 */
{
    script_call(s, "Initialize");
    s->init_ev = ev; if (ev) ev->refcount++;
    if (s->init_ev) { tg_add_event(s->init_ev); if (s->init_ev->refcount) s->init_ev->refcount--; s->init_ev = NULL; }
}
static void sobj_terminate(SObj *s, TGEvent *ev)                                              /* ScriptObject::Terminate 0040a090 */
{
    TGEvent *d;
    s->term_ev = ev; if (ev) ev->refcount++;
    script_call(s, "Terminate");
    { extern void eng_TGSoundManager_DeleteAllSoundsInGroup(void *, char *); eng_TGSoundManager_DeleteAllSoundsInGroup(NULL, s->module); }   /* the sounds the script loaded (0040a090) */
    d = tg_ev_new("TGEvent", 0); d->type = ET_DELETE_ME; d->dest = (TGObject *)s;         /* the delete event ends the object */
    tg_add_event(d);
}
static void sobj_destroy(SObj *s)                                                             /* ~ScriptObject 00409ed0 */
{
    purge_modules(s);
    if (s->term_ev) { tg_add_event(s->term_ev); if (s->term_ev->refcount) s->term_ev->refcount--; s->term_ev = NULL; }
    s->o.dead = 1;
}
static void on_delete_me(TGEvent *e) { SObj *s = sobj(e->dest); if (s) sobj_destroy(s); }
static void sobj_register(SObj *s) { tg_register_native(s, ET_DELETE_ME, on_delete_me); }

static void sobj_virtual(void *p, int slot, PyObject *evo)    /* the Python wrappers call vslots 0x54..0x68 */
{
    SObj *s = sobj(p); TGEvent *ev = NULL; void *ep = arg_ptr2(evo);
    if (!s) return;
    if (ep && tg_is_ours(ep) && tg_is_type(ep, 0x101)) ev = ep;
    switch (slot) {
    case 0x54: sobj_preload(s); break;
    case 0x58: sobj_initialize(s, ev); break;
    case 0x64: script_call(s, "Unload"); break;
    case 0x68: sobj_terminate(s, ev); break;
    }
}
void eng_ScriptObject_PreLoadAssets(void *p) { sobj_virtual(p, 0x54, NULL); }
void eng_ScriptObject_Initialize(void *p, PyObject *ev) { sobj_virtual(p, 0x58, ev); }
void eng_ScriptObject_Load(void *p) { }
void eng_ScriptObject_OnIdle(void *p) { SObj *s = sobj(p); if (s && s->onidle) script_call(s, "OnIdle"); }
void eng_ScriptObject_Unload(void *p) { sobj_virtual(p, 0x64, NULL); }
void eng_ScriptObject_Terminate(void *p, PyObject *ev) { sobj_virtual(p, 0x68, ev); }

/* ---- string events carry a name (TGStringEvent) ---- */
static TGEvent *name_event(unsigned type, const char *name, void *dest)
{
    TGEvent *e = tg_ev_new("TGStringEvent", 0);
    e->type = type; e->str = strdup(name ? name : ""); e->dest = dest; return e;
}

/* ---- Mission ---- */
static MisObj *mis(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x804) ? p : NULL; }
static MisObj *mission_new(const char *module)
{
    MisObj *m = sobj_new("Mission", sizeof(MisObj), module, 0x80c001); int i;
    for (i = 0; i < 4; i++) m->groups[i] = eng_new_ObjectGroup();
    sobj_register(&m->s); return m;
}
long eng_Mission_GetNextEventType(void) { MisObj *m = current_game && current_game->episode ? current_game->episode->mission : NULL; return m ? (long)(++m->s.counter) : 0; }
void eng_delete_Mission(void *p) { if (mis(p)) ((TGObject *)p)->dead = 1; }
static PyObject *group_obj(MisObj *m, int i) { return m && m->groups[i] ? appc_pyobj(m->groups[i], "ObjectGroup") : (Py_INCREF(Py_None), Py_None); }
PyObject *eng_Mission_GetFriendlyGroup(void *p) { return group_obj(mis(p), 0); }       /* extras: the original returns the stored Python objects (+0x50..+0x5c) */
PyObject *eng_Mission_GetEnemyGroup(void *p) { return group_obj(mis(p), 1); }
PyObject *eng_Mission_GetNeutralGroup(void *p) { return group_obj(mis(p), 2); }
PyObject *eng_Mission_GetTractorGroup(void *p) { return group_obj(mis(p), 3); }
long eng_Mission_AddPrecreatedShip(void *p, char *name, int n) { return 1; }            /* ships arrive with the object layer */
void *eng_Mission_GetPrecreatedShip(void *p, char *name) { return NULL; }
static void mission_initialize(MisObj *m, TGEvent *ev)                                       /* Mission::Initialize 00408790 */
{
    sobj_initialize(&m->s, ev);
    py_call_mod("loadsplash", "RemoveSplashScreen");                                         /* Game::RemoveSplashScreen */
}
static void mission_terminate(MisObj *m, TGEvent *ev)                                        /* Mission::Terminate 00408930 */
{
    sobj_terminate(&m->s, ev);
    py_call_mod("MissionLib", "DeleteAllMissionTimers");
}

/* ---- Episode ---- */
static EpObj *epi(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x803) ? p : NULL; }
static void episode_load_mission(EpObj *ep, const char *name, TGEvent *done);
static void on_load_mission(TGEvent *e) { EpObj *ep = epi(e->dest); if (ep && e->str) episode_load_mission(ep, e->str, NULL); }   /* LoadMissionHandler 004047b0 */
static EpObj *episode_new(const char *module)
{
    EpObj *ep = sobj_new("Episode", sizeof(EpObj), module, 0x808001);
    sobj_register(&ep->s); tg_register_native(ep, ET_LOAD_MISSION, on_load_mission); return ep;
}
long eng_Episode_GetNextEventType(void) { return current_game && current_game->episode ? (long)(++current_game->episode->s.counter) : 0; }
void eng_delete_Episode(void *p) { if (epi(p)) ((TGObject *)p)->dead = 1; }
void *eng_Episode_GetCurrentMission(void *p) { EpObj *ep = epi(p); return ep ? ep->mission : NULL; }
void eng_Episode_RegisterGoal(void *p, char *g)
{
    EpObj *ep = epi(p); int i;
    if (!ep || !g) return;
    for (i = 0; i < ep->ngoals; i++) if (!strcmp(ep->goals[i], g)) return;
    ep->goals = realloc(ep->goals, (ep->ngoals + 1) * sizeof *ep->goals); ep->goals[ep->ngoals++] = strdup(g);
}
void eng_Episode_RemoveGoal(void *p, char *g)
{
    EpObj *ep = epi(p); int i;
    if (!ep || !g) return;
    for (i = 0; i < ep->ngoals; i++) if (!strcmp(ep->goals[i], g)) { free(ep->goals[i]); memmove(ep->goals + i, ep->goals + i + 1, (ep->ngoals - i - 1) * sizeof *ep->goals); ep->ngoals--; return; }
}
/* Episode::LoadMission 00404390 (see the notes in the report: first call creates the mission and schedules the "preload done" event,
   the ET_LOAD_MISSION event calls it again to initialize, a later call for another mission terminates the current one first) */
static void game_set_preload(GameObj *g, TGEvent *e) { if (e) { g->preload_ev = e; e->refcount++; } }
static void episode_load_mission(EpObj *ep, const char *name, TGEvent *done)
{
    MisObj *m = ep->mission;
    if (!m) {
        m = mission_new(name); ep->mission = m;
        if (!ep->done_ev) { ep->done_ev = done; if (done) done->refcount++; }
        sobj_preload(&m->s);
        if (current_game) game_set_preload(current_game, name_event(ET_LOAD_MISSION, name, ep));
    } else if (m->s.assets_loaded) {
        if (streq(m->s.module, name)) { if (done) tg_add_event(done); }
        else {
            TGEvent *e = name_event(ET_LOAD_MISSION, name, ep);
            mission_terminate(m, e); ep->mission = NULL;
            ep->done_ev = done; if (done) done->refcount++;
        }
    } else {
        TGEvent *d = ep->done_ev;
        m->s.assets_loaded = 1;
        mission_initialize(m, d);
        if (ep->done_ev) { if (ep->done_ev->refcount) ep->done_ev->refcount--; ep->done_ev = NULL; }
    }
}
void eng_Episode_LoadMission(void *p, char *name, PyObject *evo)
{
    EpObj *ep = epi(p); void *ev = arg_ptr2(evo);
    if (ep && name) episode_load_mission(ep, name, ev && tg_is_ours(ev) && tg_is_type(ev, 0x101) ? ev : NULL);
}
static void episode_terminate(EpObj *ep, TGEvent *ev)                                         /* Episode::Terminate 00404330 */
{
    if (ep->mission) { mission_terminate(ep->mission, NULL); ep->mission = NULL; }
    py_call_mod("MissionLib", "DeleteAllEpisodeTimers");
    sobj_terminate(&ep->s, ev);
}

/* ---- Game ---- */
static GameObj *gam(void *p) { return tg_is_ours(p) && tg_is_type(p, 0x802) ? p : NULL; }
static void game_load_episode(GameObj *g, const char *name, TGEvent *done);
static void on_load_episode(TGEvent *e) { GameObj *g = gam(e->dest); if (g && e->str) game_load_episode(g, e->str, NULL); }          /* LoadEpisodeHandler 00406ca0 */
static void on_set_player(TGEvent *e);
static void game_start(GameObj *g)                                                            /* Game::StartGame 00406040 */
{
    PyObject *m, *f, *r;
    py_call_mod("MainMenu.mainmenu", "GameStarted");
    m = PyImport_ImportModule("Camera");
    if (m) { f = PyObject_GetAttrString(m, "MakePlayerCamera"); Py_DECREF(m);
        if (f) { r = PyEval_CallObject(f, NULL); Py_DECREF(f); if (r && PyInt_Check(r)) g->camera_id = (int)PyInt_AsLong(r); Py_XDECREF(r); }
        else PyErr_Clear(); }
    if (PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); }
}
static GameObj *game_create(const char *module)                                               /* Game::Create 00405c10 (windows are not modeled) */
{
    GameObj *g = sobj_new("Game", sizeof(GameObj), module, 0x804001);
    current_game = g;
    { extern void tg_ui_game_windows(void); tg_ui_game_windows(); }
    g->player_group = eng_new_ObjectGroup();
    sobj_register(&g->s);
    tg_register_native(g, ET_LOAD_EPISODE, on_load_episode);
    tg_register_native(g, ET_SET_PLAYER, on_set_player);
    game_start(g);
    return g;
}
/* FUN_004060c0: the part of the start-up that runs once the CD check is done */
static void game_init_stage(GameObj *g)
{
    py_call_mod("MainMenu.mainmenu", "SetupGameFont");
    sobj_initialize(&g->s, NULL);
    py_call_mod("MissionLib", "ResetViewscreen");
}
void *eng_new_Game(char *module) { return game_create(module); }
void *eng_Game_Create(char *module) { return game_create(module); }
void eng_delete_Game(void *p) { if (gam(p)) ((TGObject *)p)->dead = 1; }
long eng_Game_GetNextEventType(void) { return current_game ? (long)(++current_game->s.counter) : 0; }
void *eng_Game_GetCurrentGame(void) { return current_game; }
void *eng_Game_GetCurrentPlayer(void) { return current_game ? current_game->player : NULL; }
void *eng_Game_GetCurrentEpisode(void *p) { GameObj *g = gam(p); return g ? g->episode : NULL; }
long eng_Game_GetScore(void *p) { GameObj *g = gam(p); return g ? g->score : 0; }
long eng_Game_GetRating(void *p) { GameObj *g = gam(p); return g ? g->rating : 0; }
long eng_Game_GetKills(void *p) { GameObj *g = gam(p); return g ? g->kills : 0; }
long eng_Game_GetTorpsFired(void *p) { GameObj *g = gam(p); return g ? g->torps_fired : 0; }
long eng_Game_GetTorpsHit(void *p) { GameObj *g = gam(p); return g ? g->torps_hit : 0; }
const char *eng_Game_GetLastSavedGame(void *p) { GameObj *g = gam(p); return g ? g->last_saved : NULL; }
void eng_Game_SetLastSavedGame(void *p, char *s) { GameObj *g = gam(p); if (g) { free(g->last_saved); g->last_saved = dup(s); } }
void *eng_Game_GetPlayer(void *p) { GameObj *g = gam(p); return g ? g->player : NULL; }
void *eng_Game_GetPlayerGroup(void *p) { GameObj *g = gam(p); return g ? g->player_group : NULL; }
void *eng_BaseObjectClass_GetContainingSet(void *);
void *eng_Game_GetPlayerSet(void *p) { GameObj *g = gam(p); return g && g->player ? eng_BaseObjectClass_GetContainingSet(g->player) : NULL; }   /* 004069c0: the player's set (+0x54 -> +0x20) */
void *eng_Game_GetPlayerCamera(void *p)                                                       /* 004069d0: by id (+0x5c); Camera.MakePlayerCamera() again when it is gone */
{
    GameObj *g = gam(p); TGObject *c;
    if (!g) return NULL;
    c = g->camera_id ? tg_find((unsigned)g->camera_id) : NULL;
    if (!c || c->dead || !tg_is_type(c, 0x80be)) {
        int id = 0; if (tg_py_call_module("Camera", "MakePlayerCamera", 'i', &id) == 0) g->camera_id = id;
        c = g->camera_id ? tg_find((unsigned)g->camera_id) : NULL;
    }
    return c && !c->dead && tg_is_type(c, 0x80be) ? c : NULL;
}
void eng_Game_SetUIShipID(void *p) { extern void tg_tcw_set_ui_ship(unsigned); GameObj *g = gam(p); tg_tcw_set_ui_ship(g && g->player ? ((TGObject *)g->player)->id : 0); }
void eng_Game_SetGodMode(void *p, int v) { GameObj *g = gam(p); if (g) g->god = v != 0; }
long eng_Game_InGodMode(void *p) { GameObj *g = gam(p); return g && g->god; }
long eng_Game_GetDifficulty(void) { return difficulty; }
void eng_Game_SetDifficulty(int d) { if (!current_game) difficulty = d; }                      /* only before a game exists (004067e0) */
void eng_Game_SetDifficultyReallyIMeanIt(int d) { difficulty = d; }
void eng_Game_SetDifficultyMultipliers(float a, float b, float c, float d, float e, float f) { difficulty_mult[0] = a; difficulty_mult[1] = b; difficulty_mult[2] = c; difficulty_mult[3] = d; difficulty_mult[4] = e; difficulty_mult[5] = f; }
void eng_Game_SetDefaultDifficultyMultipliers(float a, float b, float c, float d, float e, float f) { default_mult[0] = a; default_mult[1] = b; default_mult[2] = c; default_mult[3] = d; default_mult[4] = e; default_mult[5] = f; }
double eng_Game_GetOffensiveDifficultyMultiplier(void) { return difficulty == 0 ? difficulty_mult[0] : difficulty == 1 ? difficulty_mult[2] : difficulty == 2 ? difficulty_mult[4] : 1.0; }
double eng_Game_GetDefensiveDifficultyMultiplier(void) { return difficulty == 0 ? difficulty_mult[1] : difficulty == 1 ? difficulty_mult[3] : difficulty == 2 ? difficulty_mult[5] : 1.0; }
void eng_Game_SetPlayerHardpointFileName(char *s) { free(hardpoint_file); hardpoint_file = dup(s); }
const char *eng_Game_GetPlayerHardpointFileName(void) { return hardpoint_file; }
void eng_Game_SetPreLoadDoneEvent(void *p, PyObject *evo) { GameObj *g = gam(p); void *e = arg_ptr2(evo); if (g && e && tg_is_ours(e) && tg_is_type(e, 0x101)) game_set_preload(g, e); }
/* Game::SetPlayer 004066d0 dispatches the "set player" event at once (FUN_006da300): destination = the game */
static void on_set_player(TGEvent *e)                                                          /* Game::HandleSetPlayer 00406ae0 (bridge/UI parts omitted) */
{
    GameObj *g = gam(e->dest); TGObject *ship;
    if (!g) return;
    while (g->player_group && g->player_group->n) eng_ObjectGroup_RemoveName(g->player_group, g->player_group->names[0]);   /* the player group holds only the player */
    ship = tg_find(e->ref_id); g->player = ship;
    if (ship && g->player_group) { extern const char *eng_BaseObjectClass_GetName(void *); const char *n = eng_BaseObjectClass_GetName(ship); if (n) eng_ObjectGroup_AddName(g->player_group, (char *)n); }
    if (ship) {                                                                                 /* the UI follows the new player (00406ae0) */
        extern void *eng_SetManager_GetSet(void *, char *); void *bridge; PyObject *m, *f, *r;
        eng_Game_SetUIShipID(g);
        bridge = eng_SetManager_GetSet(NULL, "bridge");
        if (bridge && (m = PyImport_ImportModule("LoadBridge"))) {
            if ((f = PyObject_GetAttrString(m, "ConfigureForShip"))) { r = PyObject_CallFunction(f, "(NN)", appc_pyobj(bridge, ((TGObject *)bridge)->cls->name), appc_pyobj(ship, ship->cls->name)); if (!r) PyErr_Print(); Py_XDECREF(r); Py_DECREF(f); }
            else PyErr_Clear();
            Py_DECREF(m);
        } else PyErr_Clear();
        if ((m = PyImport_ImportModule("Bridge.EngineerMenuHandlers"))) {
            if ((f = PyObject_GetAttrString(m, "ConfigureForShip"))) { r = PyObject_CallFunction(f, "(N)", appc_pyobj(ship, ship->cls->name)); if (!r) PyErr_Print(); Py_XDECREF(r); Py_DECREF(f); }
            else PyErr_Clear();
            Py_DECREF(m);
        } else PyErr_Clear();
    }
}
void eng_Game_SetPlayer(void *p, void *ship)
{
    GameObj *g = gam(p); TGEvent *e;
    if (!g) return;
    e = tg_ev_new("TGObjPtrEvent", 0); e->type = ET_SET_PLAYER; e->source = g->player; e->dest = (TGObject *)g;
    e->ref_id = ship && tg_is_ours(ship) ? ((TGObject *)ship)->id : 0; e->v.p = ship;
    g->player = ship;
    { extern void tg_event_dispatch_now(TGEvent *); tg_event_dispatch_now(e); }
}
static void game_load_episode(GameObj *g, const char *name, TGEvent *done)                     /* Game::LoadEpisode 00406460 */
{
    EpObj *ep = g->episode;
    if (!ep) {
        ep = episode_new(name); g->episode = ep;
        if (!g->done_ev) { g->done_ev = done; if (done) done->refcount++; }
        sobj_preload(&ep->s);
        game_set_preload(g, name_event(ET_LOAD_EPISODE, name, g));
    } else if (!ep->s.assets_loaded) {
        ep->s.assets_loaded = 1; sobj_initialize(&ep->s, NULL);
    } else if (streq(ep->s.module, name)) {
        if (done) tg_add_event(done);
    } else {
        TGEvent *e = name_event(ET_LOAD_EPISODE, name, g);
        episode_terminate(ep, e); g->episode = NULL;
        g->done_ev = done; if (done) done->refcount++;
    }
}
void eng_Game_LoadEpisode(void *p, char *name, PyObject *evo)
{
    GameObj *g = gam(p); void *ev = arg_ptr2(evo);
    if (g && name) game_load_episode(g, name, ev && tg_is_ours(ev) && tg_is_type(ev, 0x101) ? ev : NULL);
}
void eng_Game_Terminate(void *p, PyObject *evo)                                                /* Game::Terminate/ReallyTerminate 004062b0 */
{
    GameObj *g = gam(p); void *ev = arg_ptr2(evo);
    if (!g) return;
    if (g->episode) { episode_terminate(g->episode, NULL); g->episode = NULL; }
    py_call_mod("LoadBridge", "Terminate");
    py_call_mod("Camera", "ClearGlobals");
    sobj_terminate(&g->s, ev && tg_is_ours(ev) && tg_is_type(ev, 0x101) ? ev : NULL);
    if (current_game == g) current_game = NULL;
    py_call_mod("MainMenu.mainmenu", "GameEnded");
    py_call_mod("MissionLib", "ResetViewscreen");
}

/* ---- the frame: Game::OnIdle 004061d0 -> Episode::OnIdle 004041e0 -> Mission (ScriptObject::OnIdle) ---- */
void tg_mission_idle(void)
{
    extern void tg_sound_idle(void); extern void tg_ship_tick(double); static double last = -1;
    tg_sound_idle();
    { extern void tg_character_tick(void); tg_character_tick(); }                      /* CharacterClass::Update: the speaking queues */
    if (last >= 0 && tg_game_time > last) tg_ship_tick(tg_game_time - last);   /* object motion uses game time */
    { extern void tg_camera_tick(double); if (last >= 0 && tg_game_time > last) tg_camera_tick(tg_game_time - last); }   /* camera modes after the motion */
    { extern void tg_effects_tick(void); tg_effects_tick(); }                    /* particle effects follow their objects after the motion */
    { extern void tg_tactical_tick(void); tg_tactical_tick(); }                  /* TacticalControlWindow::Update (radar) */
    last = tg_game_time;
    GameObj *g = current_game;
    if (!g) return;
    if (g->s.onidle) script_call(&g->s, "OnIdle");
    if (g->episode) {
        if (g->episode->s.onidle) script_call(&g->episode->s, "OnIdle");
        if (g->episode->mission && g->episode->mission->s.onidle) script_call(&g->episode->mission->s, "OnIdle");
    }
    if (g->preload_ev) {                     /* nothing to load incrementally in the headless engine: the preload is done at once */
        TGEvent *e = g->preload_ev; g->preload_ev = NULL;
        tg_add_event(e); if (e->refcount) e->refcount--;
    }
}
/* Mission::PlayerEnteredSet 004090d0 / PlayerExitedSet 00409170 (bound to the player for ET_ENTERED_SET / ET_EXITED_SET, AddPlayerSetEventHandlers 00409080):
   the player's new set is heard (filter 2 while the bridge is rendered, else 0); the set the player left goes quiet unless it is rendered */
static void on_player_entered_set(TGEvent *e)
{
    extern void *eng_TGSoundRegion_GetRegion(char *); extern long eng_TGSoundRegion_SetFilter(void *, int);
    extern void *tg_render_set(void); extern const char *eng_SetClass_GetName(void *); extern void *eng_BaseObjectClass_GetContainingSet(void *);
    void *set, *r, *rs; const char *n;
    if (!current_game || !e->dest || e->dest != current_game->player || !(set = eng_BaseObjectClass_GetContainingSet(e->dest))) return;
    n = eng_SetClass_GetName(set); r = n ? eng_TGSoundRegion_GetRegion((char *)n) : NULL;
    if (!r) return;
    rs = tg_render_set();
    eng_TGSoundRegion_SetFilter(r, rs && eng_SetClass_GetName(rs) && !strcmp(eng_SetClass_GetName(rs), "bridge") ? 2 : 0);
}
static void on_player_exited_set(TGEvent *e)
{
    extern void *eng_TGSoundRegion_GetRegion(char *); extern long eng_TGSoundRegion_SetFilter(void *, int);
    extern void *tg_render_set(void); extern const char *eng_SetClass_GetName(void *);
    void *rs, *r;
    if (!current_game || !e->dest || e->dest != current_game->player || !e->str) return;
    rs = tg_render_set();
    if (rs && eng_SetClass_GetName(rs) && !strcmp(eng_SetClass_GetName(rs), e->str)) return;
    r = eng_TGSoundRegion_GetRegion(e->str);
    if (r) eng_TGSoundRegion_SetFilter(r, 1);
}
void tg_mission_init(void)
{
    tg_register_native_broadcast((int)tg_const("ET_ENTERED_SET"), on_player_entered_set);
    tg_register_native_broadcast((int)tg_const("ET_EXITED_SET"), on_player_exited_set);
    tg_register_native_broadcast((int)tg_const("ET_ENTERED_SET"), on_group_entered);
    tg_register_native_broadcast((int)tg_const("ET_EXITED_SET"), on_group_exited);
}

/* ---- host functions ---- */
static PyObject *host_new_game(PyObject *self, PyObject *args)       /* OptionsWindow::NewGameHandler 0050ee90: AI.Setup.GameInit, then Game::Create; the CD check passes at once */
{
    char *module; GameObj *g;
    if (!PyArg_ParseTuple(args, "s:engine_new_game", &module)) return NULL;
    if (current_game) { eng_Game_Terminate(current_game, NULL); }
    py_call_mod("AI.Setup", "GameInit");
    g = game_create(module);
    game_init_stage(g);
    return appc_from_ptr(g, "Game");
}
void tg_mission_register_host(PyObject *dict)
{
    static PyMethodDef m[] = {{"engine_new_game", host_new_game, 1}, {NULL, NULL}};
    PyMethodDef *d; for (d = m; d->ml_name; d++) { PyObject *f = PyCFunction_New(d, NULL); PyDict_SetItemString(dict, (char *)d->ml_name, f); Py_DECREF(f); }
}
