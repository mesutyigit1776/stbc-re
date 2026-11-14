/* Engine services without a scene: GraphicsModeInfo, process scheduler (TimeSliceProcess / PythonMethodProcess), FuzzyLogic,
   fonts, icon groups, UI themes, key bindings, LOD model registry.
   Behavior from stbc.exe: scheduler 0046f380..0046f930, FuzzyLogic 0047cd10..0047d0b0, GraphicsModeInfo 004621e0.., py_ wrappers 005fc360..005fcd50.
   Approximations (marked): no time budget in the scheduler (every due process runs once per frame); font metrics; the music and sound classes only keep state. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appc_runtime.h"
#include "compile.h"
#include "frameobject.h"
#include "tg_core.h"

static char *dup(const char *s) { return s ? strdup(s) : NULL; }
static void *arg_ptr(PyObject *o) { void *p = NULL; if (!o || o == Py_None) return NULL; if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }

/* ================================================= GraphicsModeInfo ================================================= */
typedef struct { TGObject o; char *device; void *desc; int windowed, width, height, depth, high_z; } GMode;
static GMode *cur_mode;
static GMode *mode_get(void)
{
    extern void *eng_new_TGString(char *);
    if (!cur_mode) {
        cur_mode = tg_new_plain("GraphicsModeInfo", sizeof(GMode));
        cur_mode->device = strdup("Metal"); cur_mode->desc = eng_new_TGString("Metal"); cur_mode->windowed = 1;
        cur_mode->width = 1024; cur_mode->height = 768; cur_mode->depth = 32; cur_mode->high_z = 1;
    }
    return cur_mode;
}
int tg_mode_standard_width(void) { int w = mode_get()->width; return w == 640 || w == 800 || w == 1024 || w == 1280 || w == 1600; }
static GMode *as_mode(void *p) { return p && tg_is_ours(p) ? (GMode *)p : mode_get(); }
void *eng_new_GraphicsModeInfo(void) { return tg_new_plain("GraphicsModeInfo", sizeof(GMode)); }
void eng_delete_GraphicsModeInfo(void *p) { }
void *eng_GraphicsModeInfo_GetCurrentMode(void) { return mode_get(); }
const char *eng_GraphicsModeInfo_GetDevice(void *p) { return as_mode(p)->device; }
void *eng_GraphicsModeInfo_GetDesc(void *p) { return as_mode(p)->desc; }
long eng_GraphicsModeInfo_IsWindowed(void *p) { return as_mode(p)->windowed; }
long eng_GraphicsModeInfo_GetWidth(void *p) { return as_mode(p)->width; }
long eng_GraphicsModeInfo_GetHeight(void *p) { return as_mode(p)->height; }
long eng_GraphicsModeInfo_GetColorDepth(void *p) { return as_mode(p)->depth; }
long eng_GraphicsModeInfo_IsHighZBuffer(void *p) { return as_mode(p)->high_z; }
double eng_GraphicsModeInfo_GetPixelWidth(void *p) { return 1.0f / (float)as_mode(p)->width; }       /* 005fc690: 1 / width */
double eng_GraphicsModeInfo_GetPixelHeight(void *p) { return 1.0f / (float)as_mode(p)->height; }
/* 004624a0: the resolution index picks the LCARS module (artwork and layout sizes). The original keys it on the width of the four 4:3 modes
   (640, 800, 1024, 1280, 1600). The port also has wide modes, so the index follows the height: 480, 600, 768, 1024 and 1200 give 0 to 4. */
static int res_index_hw(int w, int h) { (void)w; return h <= 480 ? 0 : h <= 600 ? 1 : h <= 768 ? 2 : h <= 1024 ? 3 : 4; }
#define res_index(w) res_index_hw(w, mode_get()->height)
long eng_GraphicsModeInfo_GetCurrentResolution(void *p) { return res_index(as_mode(p)->width); }
/* tables at 0x8db068 / 0x8db07c */
const char *eng_GraphicsModeInfo_GetLcarsString(void *p) { static const char *t[] = {"LCARS_640", "LCARS_800", "LCARS_1024", "LCARS_1280", "LCARS_1600"}; return t[res_index(as_mode(p)->width)]; }
const char *eng_GraphicsModeInfo_GetLcarsModule(void *p) { static const char *t[] = {"Icons.LCARS_640", "Icons.LCARS_800", "Icons.LCARS_1024", "Icons.LCARS_1280", "Icons.LCARS_1600"}; return t[res_index(as_mode(p)->width)]; }
static PyObject *host_set_display(PyObject *self, PyObject *args)
{
    GMode *m = mode_get(); int w, h, win = 1;
    if (!PyArg_ParseTuple(args, "ii|i:engine_set_display", &w, &h, &win)) return NULL;
    m->width = w; m->height = h; m->windowed = win != 0; Py_INCREF(Py_None); return Py_None;
}

/* ================================================= process scheduler ================================================= */
/* TimeSliceProcess layout of the original: +8 priority, +0x10 delay (double), +0x18 byte, +0x19 delay uses game time (default 1), +0x20 start time (double), +0x30 instance, +0x34 function name */
typedef struct Proc { TGObject o; int priority; double delay, start; int uses_game_time, ready, active; PyObject *inst; char *func; } Proc;
#define NPRIO 4
static Proc **plist[NPRIO]; static int pn[NPRIO], pcap[NPRIO];
static void sched_add(Proc *p) { int k = p->priority; if (pn[k] == pcap[k]) { pcap[k] = pcap[k] ? pcap[k] * 2 : 8; plist[k] = realloc(plist[k], pcap[k] * sizeof **plist); } plist[k][pn[k]++] = p; p->active = 1; }
static void sched_remove(Proc *p)
{
    int k = p->priority, i; if (!p->active) return;
    for (i = 0; i < pn[k]; i++) if (plist[k][i] == p) { memmove(plist[k] + i, plist[k] + i + 1, (pn[k] - i - 1) * sizeof **plist); pn[k]--; break; }
    p->active = 0;
}
static Proc *as_proc(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10002) ? p : NULL; }
static double proc_now(Proc *p) { return p->uses_game_time ? tg_game_time : tg_real_time; }
void *eng_new_PythonMethodProcess(void)                                  /* 0046f380: priority 2, uses game time */
{
    Proc *p = tg_new_plain("PythonMethodProcess", sizeof(Proc)); p->priority = 2; p->uses_game_time = 1; sched_add(p); return p;
}
void eng_delete_PythonMethodProcess(void *x) { Proc *p = as_proc(x); if (p) { sched_remove(p); Py_XDECREF(p->inst); p->inst = NULL; } }
void eng_TimeSliceProcess_SetPriority(void *x, int pr) { Proc *p = as_proc(x); if (!p || pr < 0 || pr >= NPRIO) return; sched_remove(p); p->priority = pr; sched_add(p); }
long eng_TimeSliceProcess_GetPriority(void *x) { Proc *p = as_proc(x); return p ? p->priority : 0; }
void eng_TimeSliceProcess_SetDelay(void *x, double d) { Proc *p = as_proc(x); if (p) p->delay = d; }
double eng_TimeSliceProcess_GetDelay(void *x) { Proc *p = as_proc(x); return p ? p->delay : 0.0; }
void eng_TimeSliceProcess_SetDelayUsesGameTime(void *x, int v) { Proc *p = as_proc(x); if (p) p->uses_game_time = v != 0; }
long eng_TimeSliceProcess_GetDelayUsesGameTime(void *x) { Proc *p = as_proc(x); return p ? p->uses_game_time : 0; }
void eng_PythonMethodProcess_SetFunction(void *x, char *name) { Proc *p = as_proc(x); if (p) { free(p->func); p->func = dup(name); } }
void eng_PythonMethodProcess_SetInstance(void *x, PyObject *inst) { Proc *p = as_proc(x); if (!p) return; Py_XDECREF(p->inst); p->inst = inst; Py_XINCREF(inst); }
static void proc_run(Proc *p, double budget)                              /* FUN_0046f930: reset the timer, call instance.func(budget); errors are printed */
{
    PyObject *f, *r;
    p->ready = 1; p->start = proc_now(p);
    if (!p->inst || !p->func) return;
    f = PyObject_GetAttrString(p->inst, p->func); if (!f) { PyErr_Print(); PyErr_Clear(); return; }
    r = PyObject_CallFunction(f, "(d)", budget); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); } else Py_DECREF(r);
}
void eng_TimeSliceProcess_Update(void *x, double budget) { Proc *p = as_proc(x); if (p) proc_run(p, budget); }
/* one scheduler pass per frame: every due process runs once, priorities in order (UNSTOPPABLE .. LOW). The original also limits the work to a per-frame time budget. */
void tg_process_run(void)
{
    int k, i;
    for (k = 0; k < NPRIO; k++)
        for (i = 0; i < pn[k]; i++) {
            Proc *p = plist[k][i];
            if (!p || p->o.dead) continue;
            if (proc_now(p) - p->start < p->delay) continue;          /* FUN_0046f740: due when now - start >= delay */
            proc_run(p, 0.01);
        }
}

/* ================================================= FuzzyLogic ================================================= */
typedef struct { TGObject o; int max, n; struct { int a, b; float conf, pct; } *r; } Fuzzy;
static Fuzzy *as_fuzzy(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10004) ? p : NULL; }
void *eng_new_FuzzyLogic(void) { return tg_new_plain("FuzzyLogic", sizeof(Fuzzy)); }
void eng_delete_FuzzyLogic(void *p) { Fuzzy *f = as_fuzzy(p); if (f) { free(f->r); f->r = NULL; } }
void eng_FuzzyLogic_SetMaxRules(void *p, int n) { Fuzzy *f = as_fuzzy(p); if (f) { f->max = n; free(f->r); f->r = calloc(n > 0 ? n : 1, sizeof *f->r); } }
long eng_FuzzyLogic_GetMaxRules(void *p) { Fuzzy *f = as_fuzzy(p); return f ? f->max : 0; }
long eng_FuzzyLogic_AddRule(void *p, int a, int b, float conf)           /* 0047cd50: -1 when full */
{
    Fuzzy *f = as_fuzzy(p); if (!f || f->n >= f->max) return -1;
    f->r[f->n].a = a; f->r[f->n].b = b; f->r[f->n].conf = conf; f->r[f->n].pct = 0; return f->n++;
}
long eng_FuzzyLogic_GetRule(void *p, PyObject *oa, PyObject *ob, void *oc, int i)
{
    Fuzzy *f = as_fuzzy(p); int *pa = arg_ptr(oa), *pb = arg_ptr(ob); float *pc = oc;
    if (!f || i < 0 || i >= f->n || !pa || !pb || !pc) return 0;
    *pa = f->r[i].a; *pb = f->r[i].b; *pc = f->r[i].conf; return 1;
}
long eng_FuzzyLogic_RemoveRule(void *p, int i) { Fuzzy *f = as_fuzzy(p); if (!f || i < 0 || i >= f->n) return 0; memmove(f->r + i, f->r + i + 1, (f->n - i - 1) * sizeof *f->r); f->n--; return 1; }
long eng_FuzzyLogic_SetRuleConfidence(void *p, int i, float c) { Fuzzy *f = as_fuzzy(p); if (!f || i < 0 || i >= f->n) return 0; f->r[i].conf = c; return 1; }
long eng_FuzzyLogic_SetPercentageInSet(void *p, int set, float pct)      /* every rule whose first field is the set */
{
    Fuzzy *f = as_fuzzy(p); int i, ok = 0; if (!f) return 0;
    for (i = 0; i < f->n; i++) if (f->r[i].a == set) { f->r[i].pct = pct; ok = 1; }
    return ok;
}
double eng_FuzzyLogic_GetResultBySet(void *p, int set)                   /* 0047d0b0: sum of confidence * percentage over the rules that output the set */
{
    Fuzzy *f = as_fuzzy(p); float sum = 0; int i; if (!f) return 0;
    for (i = 0; i < f->n; i++) if (f->r[i].b == set) sum += f->r[i].conf * f->r[i].pct;
    return sum;
}
/* BreakIntoSets 0047ce60 (disassembled to settle the interpolation base): result has one entry per breakpoint.
   value <= b[0] -> [1,0,..]; value >= b[n-1] -> [..,0,1]; otherwise the first i >= 1 with b[i] > b[0] and b[i] >= value gets t = (value - b[0]) / (b[i] - b[0])
   (the base is always b[0], not b[i-1]), item[i] = t, item[i-1] = 1 - t, everything else 0. */
PyObject *eng_FuzzyLogic_BreakIntoSets(double value, PyObject *breaks)
{
    int n, i, j; double *b; PyObject *res;
    if (!breaks || !PyTuple_Check(breaks)) { PyErr_SetString(PyExc_TypeError, "BreakIntoSets expects a tuple"); return NULL; }
    n = PyTuple_Size(breaks); if (n < 1) { PyErr_SetString(PyExc_ValueError, "empty tuple"); return NULL; }
    b = malloc(n * sizeof *b);
    for (i = 0; i < n; i++) b[i] = PyFloat_AsDouble(PyTuple_GetItem(breaks, i));
    { double *v = calloc(n, sizeof *v);
      if (b[0] >= value) v[0] = 1.0;
      else if (b[n - 1] <= value) v[n - 1] = 1.0;
      else for (i = 1; i < n; i++) {
          if (b[i] <= b[0] || b[i] < value) continue;
          { double t = (value - b[0]) / (b[i] - b[0]); v[i] = t; v[i - 1] = 1.0 - t; }
          break;
      }
      res = PyTuple_New(n);
      for (j = 0; j < n; j++) PyTuple_SetItem(res, j, PyFloat_FromDouble(v[j]));
      free(v); }
    free(b); return res;
}

/* the music is in tg_music.c */

/* ================================================= fonts ================================================= */
/* Glyphs: a font group and an icon group keep the same table, filled by SetIconLocation (by character code for
   fonts) and LoadIconTexture (the atlas files). The icon functions therefore also work on font groups. Loaders run on first use. */
typedef struct { float l, t, w, h; long tex; int a, b, set; } IconRect;
#define GLYPH_FIELDS TGObject o; char *name; char **tex; int ntex; IconRect *icons; int nicon; int iter; char *loader_mod, *loader_fn; int loaded;
typedef struct { GLYPH_FIELDS } IconGroup;
typedef struct { GLYPH_FIELDS float size, space, tab, hspace, vspace; } FontGroup;
static FontGroup **fgroups; static int nfg; static char *def_font_name; static float def_font_size = 12.0f;
static FontGroup *font_group(const char *name, float size, int create)
{
    int i;
    for (i = 0; i < nfg; i++) if (!strcmp(fgroups[i]->name, name) && fgroups[i]->size == size) return fgroups[i];
    if (!create) return NULL;
    { FontGroup *g = tg_new_plain("TGFontGroup", sizeof(FontGroup)); g->name = dup(name); g->size = size; g->space = 0.33f * size; g->tab = 4 * g->space; g->hspace = 0; g->vspace = 0;
      fgroups = realloc(fgroups, (nfg + 1) * sizeof *fgroups); fgroups[nfg++] = g; return g; }
}
/* the loader module.function of a font (RegisterFont) runs once, with no arguments; it creates the group and fills its glyphs */
static void font_load(FontGroup *g)
{
    PyObject *mod, *fn, *r;
    if (!g || g->loaded || !g->loader_mod || !g->loader_fn) return;
    g->loaded = 1;
    mod = PyImport_ImportModule(g->loader_mod);
    if (!mod) { PyErr_Print(); PyErr_Clear(); return; }
    fn = PyObject_GetAttrString(mod, g->loader_fn); Py_DECREF(mod);
    if (!fn) { PyErr_Print(); PyErr_Clear(); return; }
    r = PyEval_CallObject(fn, (PyObject *)NULL); Py_DECREF(fn);
    if (!r) PyErr_Print();
    Py_XDECREF(r); PyErr_Clear();
}
void *eng_new_TGFontManager(void) { return calloc(1, 64); }
void eng_delete_TGFontManager(void *m) { }
void eng_TGFontManager_RegisterFont(void *m, char *name, float size, char *mod, char *fn)
{ FontGroup *g = font_group(name, size, 1); free(g->loader_mod); free(g->loader_fn); g->loader_mod = dup(mod); g->loader_fn = dup(fn); g->loaded = 0; }
void *eng_TGFontManager_CreateFontGroup(void *m, char *name, float size) { return font_group(name, size, 1); }
void eng_TGFontManager_AddFontGroup(void *m, void *g) { }
void *eng_TGFontManager_GetDefaultFont(void *m) { FontGroup *g = def_font_name ? font_group(def_font_name, def_font_size, 1) : NULL; font_load(g); return g; }
/* The menu scripts pick the font size from the screen width (640, 800, 1024, 1280 or 1600; any other width falls to the smallest size). A wide
   mode has none of those widths, so a request for the smallest size is raised to the size of the mode's resolution index (6, 9, 9, 12, 15). */
extern int tg_mode_standard_width(void);
void eng_TGFontManager_SetDefaultFont(void *m, char *name, float size)
{
    static const float small[5] = {6, 6, 9, 12, 15}, flight[5] = {5, 6, 6, 6, 6}; extern long eng_GraphicsModeInfo_GetCurrentResolution(void *);
    /* The tactical scripts (Tactical.*, Bridge.*) pick the size with the mode's real resolution index, so their request is taken as it is. */
    int by_index = 0;
    { PyFrameObject *f = PyEval_GetFrame(); PyObject *n = f && f->f_globals ? PyDict_GetItemString(f->f_globals, "__name__") : NULL;
      if (n && PyString_Check(n) && (!strncmp(PyString_AsString(n), "Tactical", 8) || !strncmp(PyString_AsString(n), "Bridge", 6))) by_index = 1; }
    if (!by_index) {                                     /* a mainmenu that found the mode's real resolution index (Orion Delta) asks for the right sizes already */
        PyObject *mods = PyImport_GetModuleDict(), *mm = mods ? PyDict_GetItemString(mods, "MainMenu.mainmenu") : NULL, *ir = mm ? PyObject_GetAttrString(mm, "g_iRes") : NULL;
        if (ir && PyInt_Check(ir) && PyInt_AsLong(ir) != 0) by_index = 1;
        Py_XDECREF(ir); PyErr_Clear();
    }
    if (!by_index && !tg_mode_standard_width()) {          /* the script asked for the size of resolution index 0: take the size of the mode's index */
        int idx = (int)eng_GraphicsModeInfo_GetCurrentResolution(NULL);
        if (size == small[0] && idx > 0) size = small[idx];
        else if (size == flight[0] && idx > 0) size = flight[idx];
    }
    free(def_font_name); def_font_name = dup(name); def_font_size = size; font_group(name, size, 1);
}
void tg_font_default_get(char **name, float *size) { *name = def_font_name ? dup(def_font_name) : NULL; *size = def_font_size; }
void tg_font_default_set(const char *name, float size) { free(def_font_name); def_font_name = name ? dup(name) : NULL; def_font_size = size; if (name) font_group(name, size, 1); }
void *eng_TGFontManager_GetFontGroup(void *m, char *name, float size) { FontGroup *g = font_group(name, size, 0); font_load(g); return g; }
void eng_TGFontManager_Purge(void *m) { }
void eng_TGFontManager_RemoveGroup(void *m, void *g) { }
static FontGroup *as_fg(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10006) ? p : NULL; }
extern double eng_TGIconManager_GetScreenWidth(void *);
extern double eng_TGIconManager_GetScreenHeight(void *);
const char *eng_TGFontGroup_GetFontName(void *p) { FontGroup *g = as_fg(p); return g ? g->name : NULL; }
double eng_TGFontGroup_GetFontSize(void *p) { FontGroup *g = as_fg(p); return g ? g->size : 0.0; }
long eng_TGFontGroup_MapChar(void *p, int c) { return c; }
void eng_TGFontGroup_SetSpaceWidth(void *p, float v) { FontGroup *g = as_fg(p); if (g) g->space = v; }
double eng_TGFontGroup_GetPixelSpaceWidth(void *p) { FontGroup *g = as_fg(p); return g ? g->space : 0.0; }
double eng_TGFontGroup_GetScreenSpaceWidth(void *p) { FontGroup *g = as_fg(p); return g ? g->space / eng_TGIconManager_GetScreenWidth(NULL) : 0.0; }
void eng_TGFontGroup_SetTabWidth(void *p, float v) { FontGroup *g = as_fg(p); if (g) g->tab = v; }
double eng_TGFontGroup_GetPixelTabWidth(void *p) { FontGroup *g = as_fg(p); return g ? g->tab : 0.0; }
double eng_TGFontGroup_GetScreenTabWidth(void *p) { FontGroup *g = as_fg(p); return g ? g->tab / eng_TGIconManager_GetScreenWidth(NULL) : 0.0; }
void eng_TGFontGroup_SetHorizontalSpacing(void *p, float v) { FontGroup *g = as_fg(p); if (g) g->hspace = v; }
double eng_TGFontGroup_GetPixelHorizontalSpacing(void *p) { FontGroup *g = as_fg(p); return g ? g->hspace : 0.0; }
double eng_TGFontGroup_GetScreenHorizontalSpacing(void *p) { FontGroup *g = as_fg(p); return g ? g->hspace / eng_TGIconManager_GetScreenWidth(NULL) : 0.0; }
void eng_TGFontGroup_SetVerticalSpacing(void *p, float v) { FontGroup *g = as_fg(p); if (g) g->vspace = v; }
double eng_TGFontGroup_GetPixelVerticalSpacing(void *p) { FontGroup *g = as_fg(p); return g ? g->vspace : 0.0; }
double eng_TGFontGroup_GetScreenVerticalSpacing(void *p) { FontGroup *g = as_fg(p); return g ? g->vspace / eng_TGIconManager_GetScreenHeight(NULL) : 0.0; }
double eng_TGFontGroup_GetPixelMaxHeight(void *p) { FontGroup *g = as_fg(p); return g ? g->size * 1.25f : 0.0; }       /* approximation: the real value comes from the font file */
double eng_TGFontGroup_GetScreenMaxHeight(void *p) { FontGroup *g = as_fg(p); return g ? g->size * 1.25f / eng_TGIconManager_GetScreenHeight(NULL) : 0.0; }

/* ================================================= texture animations ================================================= */
/* Animated texture sheets (explosions, effects). The frames are stored as given; drawing them is the renderer's job.
   Approximation: only the data the scripts set is kept. */
typedef struct { TGObject o; char *file; int ntracks; TGObject **tracks; } TexCont;
typedef struct { TGObject o; int nframes; float fr[64][4]; } TexTrack;   /* frames beyond 64 are not kept */
static TexCont *tex_cont(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10022) ? p : NULL; }
static TexTrack *tex_track(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10023) ? p : NULL; }
/* the registry the particle effects look their sheets up in (AnimTSParticleController::CreateTarget, hash map at 0x986938 keyed by the texture
   name); a name added twice keeps the newest container */
static TexCont **tex_reg; static int ntex_reg;
static int same_tex_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) { char x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b; if (tolower((unsigned char)x) != tolower((unsigned char)y)) return 0; }
    return !*a && !*b;
}
void *eng_TextureAnimManager_AddContainer(void *m, char *file)
{
    TexCont *c = tg_new_plain("TextureAnimContainer", sizeof(TexCont)); int i; c->file = dup(file ? file : "");
    for (i = 0; i < ntex_reg; i++) if (same_tex_name(tex_reg[i]->file, c->file)) { tex_reg[i] = c; return c; }
    tex_reg = realloc(tex_reg, (ntex_reg + 1) * sizeof *tex_reg); tex_reg[ntex_reg++] = c;
    return c;
}
/* the sheet of a texture name: its track count; track i has nframes frames, each a (u0, v0, u1, v1) rectangle. NULL when no container has the name. */
int tg_texanim_tracks(const char *file)
{
    int i; for (i = 0; i < ntex_reg; i++) if (same_tex_name(tex_reg[i]->file, file)) return tex_reg[i]->ntracks;
    return -1;
}
const float (*tg_texanim_track(const char *file, int track, int *nframes))[4]
{
    int i;
    for (i = 0; i < ntex_reg; i++) if (same_tex_name(tex_reg[i]->file, file)) {
        TexTrack *t; if (track < 0 || track >= tex_reg[i]->ntracks) return NULL;
        t = (TexTrack *)tex_reg[i]->tracks[track]; if (nframes) *nframes = t->nframes < 64 ? t->nframes : 64; return t->fr;
    }
    return NULL;
}
void *eng_TextureAnimContainer_AddTextureTrack(void *p, int n)
{
    TexCont *c = tex_cont(p); TexTrack *t;
    if (!c) return NULL;
    t = tg_new_plain("TextureAnimTrack", sizeof(TexTrack));
    c->tracks = realloc(c->tracks, (c->ntracks + 1) * sizeof *c->tracks); c->tracks[c->ntracks++] = (TGObject *)t;
    t->nframes = n;
    return t;
}
void eng_TextureAnimTrack_SetFrame(void *p, int i, float u0, float v0, float u1, float v1)
{
    TexTrack *t = tex_track(p);
    if (t && i >= 0 && i < 64) { t->fr[i][0] = u0; t->fr[i][1] = v0; t->fr[i][2] = u1; t->fr[i][3] = v1; }
}

/* ================================================= UtopiaApp ================================================= */
/* The application object (App.UtopiaApp_GetApp): the window size from the graphics mode, the rest as the windowed Mac build has it. */
static TGObject *utopia_app;
void *eng_UtopiaApp_GetApp(void) { if (!utopia_app) utopia_app = tg_new_plain("UtopiaApp", sizeof(TGObject)); return utopia_app; }
void *eng_new_UtopiaApp(void) { return eng_UtopiaApp_GetApp(); }
long eng_UtopiaApp_GetWidth(void *p) { extern long eng_GraphicsModeInfo_GetWidth(void *); return eng_GraphicsModeInfo_GetWidth(NULL); }
long eng_UtopiaApp_GetHeight(void *p) { extern long eng_GraphicsModeInfo_GetHeight(void *); return eng_GraphicsModeInfo_GetHeight(NULL); }
long eng_UtopiaApp_IsFullscreen(void *p) { return 0; }
long eng_UtopiaApp_GetBitDepth(void *p) { return 32; }
const char *eng_UtopiaApp_GetDeviceDesc(void *p) { return "macOS arm64 (port)"; }
long eng_UtopiaApp_IsDirectStart(void *p) { return 0; }
static long test_menu_state;
long eng_UtopiaApp_GetTestMenuState(void *p) { return test_menu_state; }
void eng_UtopiaApp_SetTestMenuState(void *p, int v) { test_menu_state = v; }
void eng_UtopiaApp_SetFrameRate(void *p, float f) { }

/* ================================================= icons ================================================= */
/* loader: RegisterIconGroup(name, module, function); the first GetIconGroup calls module.function(group) to fill it in */
static IconGroup **igroups; static int nig; static float icon_scale = 1.0f, pixel_adj;
static IconGroup *icon_group(const char *name, int create)
{
    int i;
    for (i = 0; i < nig; i++) if (!strcmp(igroups[i]->name, name)) return igroups[i];
    if (!create) return NULL;
    { IconGroup *g = tg_new_plain("TGIconGroup", sizeof(IconGroup)); g->name = dup(name); igroups = realloc(igroups, (nig + 1) * sizeof *igroups); igroups[nig++] = g; return g; }
}
static IconGroup *as_ig(void *p) { return p && tg_is_ours(p) && (tg_is_type(p, 0x10007) || tg_is_type(p, 0x10006)) ? p : NULL; }
void eng_TGIconManager_RegisterIconGroup(void *m, char *name, char *mod, char *fn) { IconGroup *g = icon_group(name, 1); free(g->loader_mod); free(g->loader_fn); g->loader_mod = dup(mod); g->loader_fn = dup(fn); g->loaded = 0; }
void *eng_TGIconManager_CreateIconGroup(void *m, char *name) { return icon_group(name, 1); }
void eng_TGIconManager_AddIconGroup(void *m, void *g) { }
long eng_TGIconManager_DeleteIconGroup(void *m, char *name) { return icon_group(name, 0) != NULL; }
/* the loader runs once, on the first request (the exe's icon manager does the same); the group is passed to the loader */
void *eng_TGIconManager_GetIconGroup(void *m, char *name)
{
    IconGroup *g = icon_group(name, 0); PyObject *mod, *fn, *arg, *r;
    if (!g || g->loaded || !g->loader_mod || !g->loader_fn) return g;
    g->loaded = 1;
    mod = PyImport_ImportModule(g->loader_mod);
    if (!mod) { PyErr_Print(); PyErr_Clear(); return g; }
    fn = PyObject_GetAttrString(mod, g->loader_fn); Py_DECREF(mod);
    if (!fn) { PyErr_Print(); PyErr_Clear(); return g; }
    arg = appc_pyobj(g, "TGIconGroup");
    r = PyObject_CallFunction(fn, "(O)", arg); Py_DECREF(fn); Py_XDECREF(arg);
    if (!r) PyErr_Print();
    Py_XDECREF(r); PyErr_Clear();
    return g;
}
double eng_TGIconManager_GetScreenWidth(void *m) { extern long eng_GraphicsModeInfo_GetWidth(void *); return (double)eng_GraphicsModeInfo_GetWidth(NULL); }
double eng_TGIconManager_GetScreenHeight(void *m) { extern long eng_GraphicsModeInfo_GetHeight(void *); return (double)eng_GraphicsModeInfo_GetHeight(NULL); }
void eng_TGIconManager_SetScreenWidth(void *m, float v) { }
void eng_TGIconManager_SetScreenHeight(void *m, float v) { }
void eng_TGIconManager_Purge(void *m) { }
void eng_TGIconManager_SetScale(void *m, float s) { icon_scale = s; }
double eng_TGIconManager_GetScale(void *m) { return icon_scale; }
void eng_TGIconManager_SetPixelCenterAdj(void *m, float v) { pixel_adj = v; }
double eng_TGIconManager_GetPixelCenterAdj(void *m) { return pixel_adj; }
void eng_TGIconManager_SetDisplayInfo(void *m, PyObject *cam, float f) { }
PyObject *eng_TGIconManager_GetCamera(void *m) { Py_INCREF(Py_None); return Py_None; }
void eng_TGIconManager_SetCamera(void *m, PyObject *cam) { }
void eng_TGIconManager_Draw(void *m) { }
void *eng_new_TGIconGroup(char *name) { return icon_group(name ? name : "", 1); }
void eng_delete_TGIconGroup(void *p) { }
void eng_TGIconGroup_Reset(void *p) { IconGroup *g = as_ig(p); if (g) { g->nicon = 0; g->ntex = 0; } }
void eng_TGIconGroup_SetName(void *p, char *n) { IconGroup *g = as_ig(p); if (g) { free(g->name); g->name = dup(n); } }
const char *eng_TGIconGroup_GetName(void *p) { IconGroup *g = as_ig(p); return g ? g->name : NULL; }
long eng_TGIconGroup_LoadIconTexture(void *p, char *file, int flag) { IconGroup *g = as_ig(p); if (!g) return -1; g->tex = realloc(g->tex, (g->ntex + 1) * sizeof *g->tex); g->tex[g->ntex] = dup(file); return g->ntex++; }
void eng_TGIconGroup_SetIconLocation(void *p, int num, long tex, float l, float t, float w, float h, int a, int b)
{
    IconGroup *g = as_ig(p); if (!g || num < 0) return;
    if (num >= g->nicon) { g->icons = realloc(g->icons, (num + 1) * sizeof *g->icons); memset(g->icons + g->nicon, 0, (num + 1 - g->nicon) * sizeof *g->icons); g->nicon = num + 1; }
    g->icons[num].l = l; g->icons[num].t = t; g->icons[num].w = w; g->icons[num].h = h; g->icons[num].tex = tex; g->icons[num].a = a; g->icons[num].b = b; g->icons[num].set = 1;
}
double eng_TGIconGroup_GetIconPixelWidth(void *p, int n) { IconGroup *g = as_ig(p); return g && n >= 0 && n < g->nicon ? g->icons[n].w : 0.0; }
double eng_TGIconGroup_GetIconPixelHeight(void *p, int n) { IconGroup *g = as_ig(p); return g && n >= 0 && n < g->nicon ? g->icons[n].h : 0.0; }
double eng_TGIconGroup_GetIconScreenWidth(void *p, int n) { return icon_scale * eng_TGIconGroup_GetIconPixelWidth(p, n) / eng_TGIconManager_GetScreenWidth(NULL); }     /* 0072c8a0 */
double eng_TGIconGroup_GetIconScreenHeight(void *p, int n) { return icon_scale * eng_TGIconGroup_GetIconPixelHeight(p, n) / eng_TGIconManager_GetScreenHeight(NULL); }
/* size of an icon in screen units; 0 when the group or the number is unknown (TGIcon::SizeToArtwork 0073d4d0) */
int tg_icon_size(const char *group, int num, float *w, float *h)
{
    IconGroup *g;
    if (group) eng_TGIconManager_GetIconGroup(NULL, (char *)group);        /* runs the group's loader the first time */
    g = group ? icon_group(group, 0) : NULL;
    if (!g || num < 0 || num >= g->nicon || !g->icons[num].set) return 0;
    *w = (float)eng_TGIconGroup_GetIconScreenWidth(g, num); *h = (float)eng_TGIconGroup_GetIconScreenHeight(g, num); return 1;
}
PyObject *eng_TGIconGroup_GetPyWrapper(void *p) { Py_INCREF(Py_None); return Py_None; }
long eng_TGIconGroup_IsNumValid(void *p, int n) { IconGroup *g = as_ig(p); return g && n >= 0 && n < g->nicon && g->icons[n].set; }
void eng_TGIconGroup_GoToFirstPos(void *p) { IconGroup *g = as_ig(p); if (g) g->iter = 0; }
long eng_TGIconGroup_GetCount(void *p) { IconGroup *g = as_ig(p); return g ? g->nicon : 0; }
long eng_TGIconGroup_GetNext(void *p) { IconGroup *g = as_ig(p); if (!g) return -1; while (g->iter < g->nicon) { int i = g->iter++; if (g->icons[i].set) return i; } return -1; }

/* glyphs for the draw list (tg_ui_draw.c): the atlas file as the script gave it, and the source rectangle in pixels. 0 when the
   number is not set. tg_icon_glyph runs the group's loader first. */
int tg_glyph(void *p, int num, const char **tex, float *l, float *t, float *w, float *h)
{
    IconGroup *g = as_ig(p); long k;
    if (!g || num < 0 || num >= g->nicon || !g->icons[num].set) return 0;
    k = g->icons[num].tex;
    if (k < 0 || k >= g->ntex) return 0;
    *tex = g->tex[k]; *l = g->icons[num].l; *t = g->icons[num].t; *w = g->icons[num].w; *h = g->icons[num].h;
    return 1;
}
/* the rotation (0..3, clockwise quarter turns) and mirror (1 horizontal, 2 vertical; applied after the turn) of an icon, as set by SetIconLocation */
int tg_icon_flags(const char *group, int num, int *rot, int *mirror)
{
    IconGroup *g;
    *rot = *mirror = 0;
    if (!group) return 0;
    g = icon_group(group, 0);
    if (!g || num < 0 || num >= g->nicon || !g->icons[num].set) return 0;
    *rot = g->icons[num].a & 3; *mirror = g->icons[num].b & 3;
    return 1;
}
/* the rotation and mirror of a font glyph (as tg_icon_flags for an icon group) */
int tg_glyph_flags(void *p, int num, int *rot, int *mirror)
{
    IconGroup *g = as_ig(p); *rot = *mirror = 0;
    if (!g || num < 0 || num >= g->nicon || !g->icons[num].set) return 0;
    *rot = g->icons[num].a & 3; *mirror = g->icons[num].b & 3; return 1;
}
int tg_icon_glyph(const char *group, int num, const char **tex, float *l, float *t, float *w, float *h)
{
    IconGroup *g;
    if (!group) return 0;
    eng_TGIconManager_GetIconGroup(NULL, (char *)group);
    g = icon_group(group, 0);
    return g && tg_glyph(g, num, tex, l, t, w, h);
}
/* the default font group (NULL when none is set) */
void *tg_default_font(void) { return eng_TGFontManager_GetDefaultFont(NULL); }
/* text metrics of a font group: space and tab width, horizontal and vertical spacing, and the line height (the tallest glyph,
   or 1.25 x size when the font has no glyphs). All zero for NULL. */
void tg_font_metrics(void *p, float *space, float *hspace, float *vspace, float *height)
{
    FontGroup *g = p && tg_is_ours(p) && tg_is_type(p, 0x10006) ? p : NULL; float hmax = 0; int i;
    if (!g) { *space = *hspace = *vspace = *height = 0; return; }
    for (i = 0; i < g->nicon; i++) if (g->icons[i].set) { float eh = (g->icons[i].a & 1) ? g->icons[i].w : g->icons[i].h; if (eh > hmax) hmax = eh; }     /* a glyph turned a quarter has its width as height */
    *space = g->space; *hspace = g->hspace; *vspace = g->vspace; *height = hmax > 0 ? hmax : g->size * 1.25f;
}

/* ================================================= UI themes ================================================= */
typedef struct { TGObject o; char *name, *window_frame, *button_frame, *dialog_frame; float window_scale, button_scale, dialog_scale; } Theme;
static Theme **themes; static int nthemes; static Theme *cur_theme;
static Theme *as_theme(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10008) ? p : NULL; }
void *eng_TGUITheme_Create(char *name) { Theme *t = tg_new_plain("TGUITheme", sizeof(Theme)); t->name = dup(name); t->window_scale = t->button_scale = t->dialog_scale = 1.0f; return t; }
void *eng_new_TGUITheme(char *name) { return eng_TGUITheme_Create(name); }
void eng_delete_TGUITheme(void *p) { }
void eng_TGUITheme_SetThemeName(void *p, char *n) { Theme *t = as_theme(p); if (t) { free(t->name); t->name = dup(n); } }
const char *eng_TGUITheme_GetThemeName(void *p) { Theme *t = as_theme(p); return t ? t->name : NULL; }
void eng_TGUITheme_Copy(void *p, void *q)
{
    Theme *t = as_theme(p), *s = as_theme(q); if (!t || !s) return;
    free(t->window_frame); free(t->button_frame); free(t->dialog_frame);
    t->window_frame = dup(s->window_frame); t->button_frame = dup(s->button_frame); t->dialog_frame = dup(s->dialog_frame);
    t->window_scale = s->window_scale; t->button_scale = s->button_scale; t->dialog_scale = s->dialog_scale;
}
void eng_TGUITheme_SetWindowFrameName(void *p, char *n) { Theme *t = as_theme(p); if (t) { free(t->window_frame); t->window_frame = dup(n); } }
void eng_TGUITheme_SetButtonFrameName(void *p, char *n) { Theme *t = as_theme(p); if (t) { free(t->button_frame); t->button_frame = dup(n); } }
void eng_TGUITheme_SetDialogFrameName(void *p, char *n) { Theme *t = as_theme(p); if (t) { free(t->dialog_frame); t->dialog_frame = dup(n); } }
void eng_TGUITheme_SetWindowFrameScale(void *p, float v) { Theme *t = as_theme(p); if (t) t->window_scale = v; }
void eng_TGUITheme_SetButtonFrameScale(void *p, float v) { Theme *t = as_theme(p); if (t) t->button_scale = v; }
void eng_TGUITheme_SetDialogFrameScale(void *p, float v) { Theme *t = as_theme(p); if (t) t->dialog_scale = v; }
const char *eng_TGUITheme_GetWindowFrameName(void *p) { Theme *t = as_theme(p); return t ? t->window_frame : NULL; }
const char *eng_TGUITheme_GetButtonFrameName(void *p) { Theme *t = as_theme(p); return t ? t->button_frame : NULL; }
const char *eng_TGUITheme_GetDialogFrameName(void *p) { Theme *t = as_theme(p); return t ? t->dialog_frame : NULL; }
double eng_TGUITheme_GetWindowFrameScale(void *p) { Theme *t = as_theme(p); return t ? t->window_scale : 1.0; }
double eng_TGUITheme_GetButtonFrameScale(void *p) { Theme *t = as_theme(p); return t ? t->button_scale : 1.0; }
double eng_TGUITheme_GetDialogFrameScale(void *p) { Theme *t = as_theme(p); return t ? t->dialog_scale : 1.0; }
void *eng_TGUIThemeManager_FindTheme(void *m, char *name) { int i; for (i = 0; i < nthemes; i++) if (!strcmp(themes[i]->name, name)) return themes[i]; return NULL; }
long eng_TGUIThemeManager_SetCurrentTheme(void *m, char *name) { Theme *t = eng_TGUIThemeManager_FindTheme(m, name); if (!t) return 0; cur_theme = t; return 1; }
void eng_TGUIThemeManager_AddTheme(void *m, void *t) { Theme *x = as_theme(t); if (x) { themes = realloc(themes, (nthemes + 1) * sizeof *themes); themes[nthemes++] = x; } }
void *eng_TGUIThemeManager_GetCurrentTheme(void *m) { return cur_theme; }

/* ================================================= LOD models ================================================= */
typedef struct { TGObject o; char *name, *share_path; int nlod; int loaded; char *files[8]; float scale; char *glow_sfx[8], *filter[8], *spec_sfx[8]; } LODModel;   /* per LOD: the glow suffix, the texture name filter and the specular suffix (AddLOD strings 1 to 3, LODEntry fields 2 to 4) */
static LODModel **lods; static int nlods;
static LODModel *as_lod(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x10005) ? p : NULL; }
/* LODModelManager::Create(name, ...): registers (or replaces) the model record; Load builds the scene graph from the NIF files later */
void *eng_LODModelManager_Create(void *m, char *name, int a, int b, int c, float d, char *e)
{
    int i; LODModel *l;
    for (i = 0; i < nlods; i++) if (!strcmp(lods[i]->name, name)) return lods[i];
    l = tg_new_plain("LODModel", sizeof(LODModel)); l->name = dup(name); l->scale = d;   /* d: model scale, default 0.01 (wrapper default; AsteroidTile::CreateAsteroid 0041c6c0 passes 0.01 too) */
    lods = realloc(lods, (nlods + 1) * sizeof *lods); lods[nlods++] = l; return l;
}
long eng_LODModelManager_Contains(void *m, char *name) { int i; for (i = 0; i < nlods; i++) if (!strcmp(lods[i]->name, name)) return 1; return 0; }
void eng_LODModelManager_Unrefer(void *m, char *name) { }
void eng_LODModelManager_ForgetAll(void *m) { nlods = 0; }
void eng_LODModelManager_Purge(void *m) { }
void eng_LODModel_AddLOD(void *p, PyObject *file, int a, float b, float c, float d, float e, float f, PyObject *g1, PyObject *g2, PyObject *g3, PyObject *g4, PyObject *g5, PyObject *g6, PyObject *g7, PyObject *g8, PyObject *g9, PyObject *g10)
{   /* the LOD file name: a missing file is logged with APPC_TRACE (the NIF loader comes later) */
    extern const char *tg_asset_resolve(const char *); LODModel *l = as_lod(p); char *fn = NULL;
    if (!l) return;
    if (file && PyString_Check(file)) fn = PyString_AsString(file); else if (file && file != Py_None && !appc_to_cstr(file, &fn)) { PyErr_Clear(); fn = NULL; }
    if (fn && l->nlod < 8) {
        char *sv[3] = {NULL, NULL, NULL}; PyObject *go[3]; int q;
        l->files[l->nlod] = dup(fn);
        go[0] = g1; go[1] = g2; go[2] = g3;
        for (q = 0; q < 3; q++) if (go[q] && PyString_Check(go[q])) sv[q] = PyString_AsString(go[q]);
        l->glow_sfx[l->nlod] = sv[0] && *sv[0] ? dup(sv[0]) : NULL; l->filter[l->nlod] = sv[1] && *sv[1] ? dup(sv[1]) : NULL; l->spec_sfx[l->nlod] = sv[2] && *sv[2] ? dup(sv[2]) : NULL;
    }
    if (fn && !tg_asset_resolve(fn) && getenv("APPC_TRACE")) fprintf(stderr, "[Appc] missing model file: %s\n", fn);
    l->nlod++;
}
int tg_lod_files(const char *name, const char **out, int max)            /* for tests: the LOD files of a model record */
{ int i, k; for (i = 0; i < nlods; i++) if (!strcmp(lods[i]->name, name)) { for (k = 0; k < lods[i]->nlod && k < max; k++) out[k] = lods[i]->files[k]; return k; } return 0; }
/* the glow suffix, texture filter and specular suffix of the first LOD (the one the renderer draws); NULL when the script gave none */
void tg_lod_map_names(const char *name, const char **glow, const char **filter, const char **spec)
{ int i; *glow = *filter = *spec = NULL; for (i = 0; i < nlods; i++) if (!strcmp(lods[i]->name, name) && lods[i]->nlod > 0) { *glow = lods[i]->glow_sfx[0]; *filter = lods[i]->filter[0]; *spec = lods[i]->spec_sfx[0]; return; } }
float tg_lod_scale(const char *name) { int i; for (i = 0; i < nlods; i++) if (!strcmp(lods[i]->name, name)) return lods[i]->scale; return 1.0f; }
const char *tg_lod_share_path(const char *name) { int i; for (i = 0; i < nlods; i++) if (!strcmp(lods[i]->name, name)) return lods[i]->share_path; return NULL; }
void eng_LODModel_SetTextureSharePath(void *p, char *s) { LODModel *l = as_lod(p); if (l) { free(l->share_path); l->share_path = dup(s); } }
void eng_LODModel_AddGlowPassTexture(void *p, char *a, char *b) { }
void eng_LODModel_Load(void *p) { LODModel *l = as_lod(p); if (l) l->loaded = 1; }
void eng_LODModel_LoadIncremental(void *p) { LODModel *l = as_lod(p); if (l) l->loaded = 1; }

/* ================================================= key bindings ================================================= */
struct kbind { int key, flags, a, b; float f; long l; int c; struct kbind *next; };
static struct kbind *kbinds; static int kmax = 64;
long eng_KeyboardBinding_BindKey(void *k, int key, int a, int b, int c, float f, long l, int d)
{ struct kbind *x = calloc(1, sizeof *x); x->key = key; x->flags = a; x->a = b; x->b = c; x->f = f; x->l = l; x->c = d; x->next = kbinds; kbinds = x; return 1; }
long eng_KeyboardBinding_BindKeyChar(void *k, int key, int a, int b, int c, float f, long l, int d) { return eng_KeyboardBinding_BindKey(k, key, a, b, c, f, l, d); }
long eng_KeyboardBinding_GetNumKeys(void *k, int a, int b, float f) { struct kbind *x; long n = 0; for (x = kbinds; x; x = x->next) if (x->flags == a && x->a == b && x->f == f) n++; return n; }
long eng_KeyboardBinding_FindKey(void *k, int a, int b, float f, int idx) { struct kbind *x; for (x = kbinds; x; x = x->next) if (x->flags == a && x->a == b && x->f == f && idx-- == 0) return x->key; return -1; }
void eng_KeyboardBinding_FindKeyString(void *k, void *out, int a, int b, float f, int c, int d, int e) { }
void eng_KeyboardBinding_ClearBinding(void *k, int a, int b, float f) { }
/* a key press (or mouse button) looks up the bindings of its WC code and key state. KS_KEYDOWN fires the
   KeyDown bindings and the Normal ones (a key that acts once on press); KS_KEYUP fires only the KeyUp ones. Each match posts the
   bound event to the window as destination, so the instance handlers of the tactical window run (steering, fire, targets). The
   data of the event follows the bind mode: GET_EVENT none, GET_INT_EVENT int, GET_BOOL_EVENT bool, GET_FLOAT_EVENT float. */
enum { KS_NORMAL_STATE = 0, KS_DOWN_STATE = 1, KS_UP_STATE = 2 };
static int kb_post(struct kbind *x, void *win)
{
    static const char *cls[] = {"TGEvent", "TGIntEvent", "TGBoolEvent", "TGFloatEvent"};
    TGEvent *e = tg_ev_new(cls[x->b >= 0 && x->b <= 3 ? x->b : 0], 0);
    if (!e) return 0;
    e->type = (unsigned)x->a;
    if (x->b == 1) e->v.i = (long)x->f;
    else if (x->b == 2) e->v.i = x->f != 0;
    else if (x->b == 3) e->v.f = x->f;
    e->dest = win;
    tg_add_event(e);
    return 1;
}
/* The input handlers of the tactical window (TacticalControlHandlers, TacticalInterfaceHandlers) are added to the window the first
   time key input reaches it. Approximation: the exe adds them in its window code, which this port does not run yet. */
static void *input_wired;
static void wire_input_handlers(void *win)
{
    static const char *mods[] = {"TacticalControlHandlers", "TacticalInterfaceHandlers"};
    int i;
    if (!win || win == input_wired) return;
    input_wired = win;
    for (i = 0; i < 2; i++) {
        PyObject *m = PyImport_ImportModule((char *)mods[i]), *f, *a, *r;
        if (!m) { PyErr_Print(); PyErr_Clear(); continue; }
        f = PyObject_GetAttrString(m, "Initialize"); Py_DECREF(m);
        if (!f) { PyErr_Clear(); continue; }
        a = Py_BuildValue("(N)", tg_obj_pyobj(win));
        r = PyObject_CallObject(f, a); Py_DECREF(f); Py_DECREF(a);
        if (!r) PyErr_Print(); else Py_DECREF(r);
    }
    PyErr_Clear();
}
long tg_key_event(int key, int state, void *win)           /* returns the number of events posted */
{
    struct kbind *x; long n = 0;
    wire_input_handlers(win);
    for (x = kbinds; x; x = x->next) {
        if (x->key != key) continue;
        if (x->flags == state || (state == KS_DOWN_STATE && x->flags == KS_NORMAL_STATE)) n += kb_post(x, win);
    }
    return n;
}
long eng_KeyboardBinding_LaunchEvent(void *k, int unicode, int state, void *win, PyObject *d, int e) { return tg_key_event(unicode, state, win) > 0; }
static PyObject *host_input_key(PyObject *self, PyObject *args)          /* engine_input_key(wc_code, state): the platform path, for tests */
{
    int key, state; void *win;
    extern void *eng_TacticalControlWindow_GetTacticalControlWindow(void);
    if (!PyArg_ParseTuple(args, "ii:engine_input_key", &key, &state)) return NULL;
    win = eng_TacticalControlWindow_GetTacticalControlWindow();
    return PyInt_FromLong(tg_key_event(key, state, win));
}
/* KeyboardBinding::RebuildMappingFromFile: the mapping is the Initialize() of a binding script (DefaultKeyboardBinding.py calls
   BindKey for every key). The old bindings are dropped first. The stock game also writes a mapping file; the port keeps none. */
void eng_KeyboardBinding_RebuildMappingFromFile(void *k, char *file)
{
    struct kbind *x, *n; PyObject *m, *f, *r;
    for (x = kbinds; x; x = n) { n = x->next; free(x); }
    kbinds = NULL;
    if (!file) return;
    m = PyImport_ImportModule(file);
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, "Initialize"); Py_DECREF(m);
    if (!f) { PyErr_Clear(); return; }
    r = PyObject_CallObject(f, NULL); Py_DECREF(f);
    if (!r) PyErr_Print(); else Py_DECREF(r);
    PyErr_Clear();
}
void eng_KeyboardBinding_GenerateMappingFile(void *k) { }
void eng_KeyboardBinding_SetMaxBoundKeys(void *k, int n) { kmax = n; }

void tg_services_register_host(PyObject *dict)
{
    static PyMethodDef m[] = {{"engine_set_display", host_set_display, 1}, {"engine_input_key", host_input_key, 1}, {NULL, NULL}};
    PyMethodDef *d; for (d = m; d->ml_name; d++) { PyObject *f = PyCFunction_New(d, NULL); PyDict_SetItemString(dict, (char *)d->ml_name, f); Py_DECREF(f); }
}

/* TGSound position: a NiPoint3 kept in the property store. Set copies the caller's point and Get returns a fresh copy (App.py takes ownership). */
void prop_set_p(void *, const char *, void *); void *prop_get_p(void *, const char *);
