/* Tactical interface displays, part 2: STFillGauge, ShipDisplay (the shields and target shields panels), ShieldsDisplay,
   DamageDisplay and DamageIcon. The Python modules Tactical.Interface.* build the children; the native part below is what the exe does:
   STFillGauge 00522160.., ShipDisplay 00546a50.., ShieldsDisplay 005457e0.., DamageDisplay 00540e90.., DamageIcon 00540610...
   Approximations (marked in place): the exe watches the shield levels with range checks (ET_SHIELD_LEVEL_CHANGE) and the damage icons with
   timers; this port polls once per frame (tg_tac_displays_tick), which gives the same pictures. */
#include "tg_ui.h"

extern void *eng_new_TGString(char *);
extern void *eng_TGIcon_Create(char *, int, PyObject *);
extern void eng_TGPane_DeleteChild(void *, void *, int);
extern void *eng_Game_GetCurrentPlayer(void);
extern void *eng_BaseObjectClass_GetContainingSet(void *);
extern void *eng_ShipClass_GetShields(void *), *eng_ShipClass_GetHull(void *), *eng_ShipClass_GetTarget(void *), *eng_ShipClass_GetShipProperty(void *);
extern double eng_ShieldClass_GetSingleShieldPercentage(void *, int);
extern double eng_ShieldClass_GetCurShields(void *, int), eng_ShieldClass_GetMaxShields(void *, int);
extern long eng_PoweredSubsystem_IsOn(void *);
extern double eng_ShipSubsystem_GetCombinedConditionPercentage(void *), eng_ShipSubsystem_GetConditionPercentage(void *), eng_ShipSubsystem_GetCondition(void *);
extern void *eng_ShipClass_GetTarget(void *);
extern long eng_ShipSubsystem_IsDisabled(void *), eng_ShipSubsystem_IsTargetable(void *), eng_ShipSubsystem_GetNumChildSubsystems(void *);
extern void *eng_ShipSubsystem_GetChildSubsystem(void *, int);
extern void *eng_ShipSubsystem_GetProperty(void *);
extern long eng_ShipProperty_GetSpecies(void *);
extern long eng_GraphicsModeInfo_GetWidth(void *), eng_GraphicsModeInfo_GetHeight(void *);
extern const char *eng_GraphicsModeInfo_GetLcarsString(void *);
extern void *eng_GraphicsModeInfo_GetCurrentMode(void);
extern long eng_GraphicsModeInfo_GetResolutionIndex(void *);
extern void *eng_TGUIObject_GetConceptualParent(void *);
extern void *eng_TGUIObject_GetObjID(void *);
int tg_ship_top_subsystems(void *ship, void ***out);                  /* tg_ship.c */
int tg_icon_size(const char *group, int num, float *w, float *h);        /* tg_services.c: size of an icon artwork (fraction of the screen) */
extern void eng_SubsystemProperty_GetPosition2D(void *, void *, void *);

static UI *as_c(void *p, unsigned ct) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
static void *player(void) { return eng_Game_GetCurrentPlayer(); }
/* getShipById (the exe looks the id up in the player's set); a dead or removed object is no ship */
static void *ship_of(unsigned id)
{
    TGObject *o; void *pl = player(), *set;
    if (!id) return NULL;
    o = tg_find(id);
    if (!o || o->dead || !tg_is_ours(o) || !tg_is_type(o, 0x8008)) return NULL;
    set = pl ? eng_BaseObjectClass_GetContainingSet(pl) : NULL;
    if (set && eng_BaseObjectClass_GetContainingSet(o) != set) return NULL;
    return o;
}
static PyObject *py_ui(UI *u) { if (!u) { Py_INCREF(Py_None); return Py_None; } return appc_pyobj(u, u->o.cls->name); }
static void call_mod(const char *module, const char *func, UI *w)           /* module.func(wrapper of w) */
{
    PyObject *m = PyImport_ImportModule((char *)module), *f, *self, *args, *r;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)func); Py_DECREF(m); if (!f) { PyErr_Print(); PyErr_Clear(); return; }
    self = py_ui(w); args = Py_BuildValue("(O)", self); Py_DECREF(self);
    r = args ? PyEval_CallObject(f, args) : NULL; Py_XDECREF(args); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); } Py_XDECREF(r);
}
static void call_mod_size(const char *module, const char *func, UI *w, float a, float b)     /* module.func(wrapper of w, a, b) */
{
    PyObject *m = PyImport_ImportModule((char *)module), *f, *self, *args, *r;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)func); Py_DECREF(m); if (!f) { PyErr_Print(); PyErr_Clear(); return; }
    self = py_ui(w); args = Py_BuildValue("(Off)", self, a, b); Py_DECREF(self);
    r = args ? PyEval_CallObject(f, args) : NULL; Py_XDECREF(args); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); } Py_XDECREF(r);
}
static UI *kid(UI *p, int i) { return p && i >= 0 && i < p->nkids ? p->kids[i] : NULL; }
static void show(UI *u, int layout) { if (u) ui_show(u, layout); }
static void hide(UI *u, int layout) { if (u) ui_hide(u, layout); }

/* ====================================================== STFillGauge (0x80f0) ======================================================
   A pane of two icons (0 fill, 1 empty) from an icon group; the fill icon takes the share of the pane that the value gives. The value
   comes from the watched object through a native getter (the ship display's health gauge uses ShipSubsystem::GetCombinedConditionPercentage
   and no maximum) or, when SetPythonMethods was called, from module functions (value, maximum, alternate flag). UpdateGauge 00522ba0. */
typedef struct GaugeX {
    unsigned obj; int dir;                                                   /* LEFT_TO_RIGHT 0, RIGHT_TO_LEFT 1, BOTTOM_TO_TOP 2, TOP_TO_BOTTOM 3 */
    double (*value)(void *), (*maxv)(void *); int (*alt)(void *);
    char *pymod, *pyval, *pymax, *pyalt;
    float fill[4], empty[4], altc[4]; struct GaugeX *next; UI *self;
} GaugeX;
static GaugeX *gauges;
#define CT_GAUGE 0x80f0
static GaugeX *gx(UI *u) { return u ? u->ext : NULL; }
static void gauge_colors(UI *u)
{
    GaugeX *x = gx(u); UI *f = kid(u, 0), *e = kid(u, 1);
    if (f) memcpy(f->color, x->fill, 16);
    if (e) memcpy(e->color, x->empty, 16);
}
void eng_STFillGauge_UpdateGauge(void *p)
{
    UI *u = as_c(p, CT_GAUGE), *fi, *ei; GaugeX *x; TGObject *o; double cur = 0, max = 1; int useAlt = 0; float f, W, H;
    if (!u || !(x = gx(u)) || !(u->flags & UF_VISIBLE)) return;
    o = x->obj ? tg_find(x->obj) : NULL; if (o && o->dead) o = NULL;
    if (x->pymod && x->pyval) {                                               /* module functions: value(obj), maximum(obj), alternate(obj) */
        PyObject *arg = o ? appc_pyobj(o, o->cls->name) : (Py_INCREF(Py_None), Py_None), *m = PyImport_ImportModule(x->pymod), *fn, *r;
        if (m) {
            if ((fn = PyObject_GetAttrString(m, x->pyval))) { r = PyObject_CallFunction(fn, "(O)", arg); if (r) { cur = PyFloat_AsDouble(r); Py_DECREF(r); } else PyErr_Clear(); Py_DECREF(fn); } else PyErr_Clear();
            if (x->pymax && (fn = PyObject_GetAttrString(m, x->pymax))) { r = PyObject_CallFunction(fn, "(O)", arg); if (r) { max = PyFloat_AsDouble(r); Py_DECREF(r); } else PyErr_Clear(); Py_DECREF(fn); } else PyErr_Clear();
            if (x->pyalt && (fn = PyObject_GetAttrString(m, x->pyalt))) { r = PyObject_CallFunction(fn, "(O)", arg); if (r) { useAlt = PyInt_AsLong(r) == 1; Py_DECREF(r); } else PyErr_Clear(); Py_DECREF(fn); } else PyErr_Clear();
            Py_DECREF(m);
        } else PyErr_Clear();
        Py_DECREF(arg);
    } else if (o) {
        if (x->value) cur = x->value(o);
        max = x->maxv ? x->maxv(o) : 1.0;
        if (x->alt) useAlt = x->alt(o) == 1;
    }
    f = max > 0 ? (float)(cur / max) : (float)(cur / 1.0); if (f < 0) f = 0; if (f > 1) f = 1;
    fi = kid(u, 0); ei = kid(u, 1); if (!fi || !ei) return;
    W = u->w; H = u->h;
    if (x->dir == 0 || x->dir == 1) {                                         /* the fill takes f of the width, the empty part the rest */
        ui_set_size(fi, W * f, H, 0); ui_set_size(ei, W - W * f, H, 0);
        if (x->dir == 0) { ui_set_pos(fi, 0, 0, 0); ui_set_pos(ei, fi->w, 0, 0); } else { ui_set_pos(ei, 0, 0, 0); ui_set_pos(fi, ei->w, 0, 0); }
    } else {
        ui_set_size(fi, W, H * f, 0); ui_set_size(ei, W, H - H * f, 0);
        if (x->dir == 3) { ui_set_pos(fi, 0, 0, 0); ui_set_pos(ei, 0, fi->h, 0); } else { ui_set_pos(ei, 0, 0, 0); ui_set_pos(fi, 0, ei->h, 0); }
    }
    memcpy(fi->color, useAlt ? x->altc : x->fill, 16);
    fi->flags = (f > 0 ? (fi->flags | UF_VISIBLE) : (fi->flags & ~UF_VISIBLE)) | (fi->flags & ~UF_VISIBLE);
    if (f <= 0) fi->flags &= ~UF_VISIBLE; else fi->flags |= UF_VISIBLE;
    if (f >= 1.0f) ei->flags &= ~UF_VISIBLE; else ei->flags |= UF_VISIBLE;
}
static UI *gauge_new(int num, const char *group, int vertical)
{
    UI *u = pane_new("STFillGauge", 0, 0); GaugeX *x = calloc(1, sizeof *x); char g[32]; int i;
    static const float emptyc[4] = {0, 0, 0, 1}, fillc[4] = {1, 1, 1, 1};
    if (!group) { snprintf(g, sizeof g, "%s", eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode())); group = g; }
    memcpy(x->fill, fillc, 16); memcpy(x->empty, emptyc, 16); memcpy(x->altc, fillc, 16);
    u->ext = x; x->self = u; x->next = gauges; gauges = x; x->dir = vertical ? 2 : 0;
    for (i = 0; i < 2; i++) { UI *ic = eng_TGIcon_Create((char *)group, num, NULL); ic->w = ic->h = 0; pane_add(u, u->nkids, ic, 0, 0, 0); }
    gauge_colors(u);
    return u;
}
void *eng_STFillGauge_Create(int num, char *group, int vertical) { return gauge_new(num, group, vertical); }
void *eng_new_STFillGauge(int num, char *group, int vertical) { return gauge_new(num, group, vertical); }
void *eng_STFillGauge_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_GAUGE); }
void eng_STFillGauge_SetObject(void *p, PyObject *o)
{
    UI *u = as_c(p, CT_GAUGE); void *t = ui_arg_ptr(o);
    if (!u) return;
    gx(u)->obj = t && tg_is_ours(t) ? ((TGObject *)t)->id : 0;
    if (gx(u)->obj && !gx(u)->value && !gx(u)->pymod) { gx(u)->value = eng_ShipSubsystem_GetCombinedConditionPercentage; }   /* the health gauge: ShipDisplay ctor 00546a50 */
    eng_STFillGauge_UpdateGauge(u);
}
void *eng_STFillGauge_GetObject(void *p) { UI *u = as_c(p, CT_GAUGE); return u && gx(u)->obj ? tg_find(gx(u)->obj) : NULL; }
static void set_c(float *dst, void *c) { if (c) memcpy(dst, c, 16); }
void eng_STFillGauge_SetFillColor(void *p, void *c) { UI *u = as_c(p, CT_GAUGE); if (u) { set_c(gx(u)->fill, c); gauge_colors(u); } }
void eng_STFillGauge_SetEmptyColor(void *p, void *c) { UI *u = as_c(p, CT_GAUGE); if (u) { set_c(gx(u)->empty, c); gauge_colors(u); } }
void eng_STFillGauge_SetAlternateColor(void *p, void *c) { UI *u = as_c(p, CT_GAUGE); if (u) set_c(gx(u)->altc, c); }
void eng_STFillGauge_SetFillDirection(void *p, int d) { UI *u = as_c(p, CT_GAUGE); if (u) { gx(u)->dir = d; eng_STFillGauge_UpdateGauge(u); } }
void eng_STFillGauge_SetPythonMethods(void *p, char *mod, char *val, char *max, char *alt)      /* 00522630 */
{
    UI *u = as_c(p, CT_GAUGE); GaugeX *x; if (!u) return; x = gx(u);
    free(x->pymod); free(x->pyval); free(x->pymax); free(x->pyalt);
    x->pymod = mod ? strdup(mod) : NULL; x->pyval = val ? strdup(val) : NULL; x->pymax = max ? strdup(max) : NULL; x->pyalt = alt ? strdup(alt) : NULL;
}
/* EngPowerDisplay::CreateBatteryGauge 0054fb70: a vertical gauge (BOTTOM_TO_TOP) on the player's power subsystem; MAIN the main battery power over
   its limit, BACKUP the backup battery over its limit, WARP_CORE the condition over the maximum condition. */
extern double eng_PowerSubsystem_GetMainBatteryPower(void *), eng_PowerSubsystem_GetBackupBatteryPower(void *), eng_ShipSubsystem_GetCondition(void *);
extern double eng_PowerProperty_GetMainBatteryLimit(void *), eng_PowerProperty_GetBackupBatteryLimit(void *), eng_SubsystemProperty_GetMaxCondition(void *);
extern void *tg_sub_prop(void *);
static double bat_main_max(void *s) { void *pr = tg_sub_prop(s); return pr ? eng_PowerProperty_GetMainBatteryLimit(pr) : 1.0; }
static double bat_back_max(void *s) { void *pr = tg_sub_prop(s); return pr ? eng_PowerProperty_GetBackupBatteryLimit(pr) : 1.0; }
static double bat_core_max(void *s) { void *pr = tg_sub_prop(s); return pr ? eng_SubsystemProperty_GetMaxCondition(pr) : 1.0; }
UI *tg_battery_gauge(int kind, void *power)
{
    UI *u; GaugeX *x; char *cols[3] = {"g_kEngineeringMainPowerColor", "g_kEngineeringBackupPowerColor", "g_kEngineeringWarpCoreColor"};
    extern void *tg_global(const char *);
    if (!power || kind < 0 || kind > 2) return NULL;
    u = gauge_new(200, NULL, 1); x = gx(u);
    x->dir = 2; x->obj = ((TGObject *)power)->id;
    x->value = kind == 0 ? eng_PowerSubsystem_GetMainBatteryPower : kind == 1 ? eng_PowerSubsystem_GetBackupBatteryPower : eng_ShipSubsystem_GetCondition;
    x->maxv = kind == 0 ? bat_main_max : kind == 1 ? bat_back_max : bat_core_max;
    { const float *c = tg_global(cols[kind]); if (c) memcpy(x->fill, c, 16); }
    gauge_colors(u); eng_STFillGauge_UpdateGauge(u);
    return u;
}
/* the condition gauge inside a repair button (EngRepairPane FUN_005519e0): value ShipSubsystem::GetCombinedConditionPercentage, alternate colour when disabled */
UI *tg_condition_gauge(void *subsystem)
{
    UI *u = gauge_new(200, NULL, 0); GaugeX *x = gx(u); extern long eng_ShipSubsystem_IsDisabled(void *);
    x->obj = subsystem ? ((TGObject *)subsystem)->id : 0; x->value = eng_ShipSubsystem_GetCombinedConditionPercentage;
    return u;
}
/* the interface module walks the gauge list every frame (callers of UpdateGauge: Update 00555b10) */
static void gauges_tick(void)
{
    GaugeX *x, **pp = &gauges;
    while ((x = *pp)) { if (x->self->o.dead) { *pp = x->next; continue; } eng_STFillGauge_UpdateGauge(x->self); pp = &x->next; }
}

/* ====================================================== ShipDisplay (0x811d) ======================================================
   A stylized window ("Shields" for the player, "Target Shields" for the target) with a health gauge, a damage display and a shields
   display. SetShipID runs the Python SetShipID (which fills the three) and keeps the destroyed / exited-set handlers for its ship. */
#define CT_SHIPDISP 0x811d
typedef struct { unsigned ship; UI *shields, *damage, *gauge; int follow_target; } SdX;
static UI *ship_displays[8]; static int n_ship_displays;
static SdX *sdx(UI *u) { return u ? u->ext : NULL; }
UI *tg_ship_display_new(void)
{
    extern UI *stw_new(const char *, const char *, const char *, void *, float, float, UI *, int, float, float, const float *);
    UI *w = stw_new("ShipDisplay", "StylizedWindow", "NormalStyle", NULL, 0, 0, NULL, 0, 0, 0, tg_global("g_kInterfaceBorderColor"));
    UI *in = w->nkids ? w->kids[0] : NULL;
    w->ext = calloc(1, sizeof(SdX));
    if (in && in->nkids) { UI *s = in->kids[0]; if (s) { s->flags &= ~UF_BATCHCHILD; } }
    call_mod("Tactical.Interface.ShipDisplay", "Create", w);
    w->flags |= UF_ALWAYSEVENTS | UF_NOFOCUS;
    if (n_ship_displays < 8) ship_displays[n_ship_displays++] = w;
    return w;
}
void *eng_ShipDisplay_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_SHIPDISP); }
void eng_ShipDisplay_SetShipIDVar(void *p, long id) { UI *u = as_c(p, CT_SHIPDISP); if (u) sdx(u)->ship = (unsigned)id; }
long eng_ShipDisplay_GetShipID(void *p) { UI *u = as_c(p, CT_SHIPDISP); return u ? (long)sdx(u)->ship : 0; }
void eng_ShipDisplay_SetShieldsDisplay(void *p, void *d) { UI *u = as_c(p, CT_SHIPDISP); if (u) sdx(u)->shields = as_ui(d); }
void eng_ShipDisplay_SetDamageDisplay(void *p, void *d) { UI *u = as_c(p, CT_SHIPDISP); if (u) sdx(u)->damage = as_ui(d); }
void eng_ShipDisplay_SetHealthGauge(void *p, void *d) { UI *u = as_c(p, CT_SHIPDISP); if (u) sdx(u)->gauge = as_ui(d); }
void *eng_ShipDisplay_GetShieldsDisplay(void *p) { UI *u = as_c(p, CT_SHIPDISP); return u ? sdx(u)->shields : NULL; }
void *eng_ShipDisplay_GetDamageDisplay(void *p) { UI *u = as_c(p, CT_SHIPDISP); return u ? sdx(u)->damage : NULL; }
void *eng_ShipDisplay_GetHealthGauge(void *p) { UI *u = as_c(p, CT_SHIPDISP); return u ? sdx(u)->gauge : NULL; }
void eng_ShipDisplay_SetShipID(void *p, long id)                              /* 00546cd0 */
{
    UI *u = as_c(p, CT_SHIPDISP);
    PyObject *m, *f, *self, *r;
    if (!u) return;
    m = PyImport_ImportModule("Tactical.Interface.ShipDisplay");
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, "SetShipID"); Py_DECREF(m);
    if (!f) { PyErr_Clear(); return; }
    self = py_ui(u); r = PyObject_CallFunction(f, "(Ol)", self, id); Py_DECREF(self); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); } else Py_DECREF(r);
}
void eng_ShipDisplay_AddTargetChangeEvent(void *p) { UI *u = as_c(p, CT_SHIPDISP); if (u) sdx(u)->follow_target = 1; }    /* 00546dc0: ET_TARGET_WAS_CHANGED -> HandleChangeTarget */
void eng_ShipDisplay_ResizeUI(void *p) { UI *u = as_c(p, CT_SHIPDISP); if (u) call_mod("Tactical.Interface.ShipDisplay", "ResizeUI", u); }
void eng_ShipDisplay_RepositionUI(void *p) { UI *u = as_c(p, CT_SHIPDISP); if (u) call_mod("Tactical.Interface.ShipDisplay", "RepositionUI", u); }
void *eng_ShipDisplay_Create(void) { return tg_ship_display_new(); }
void *eng_new_ShipDisplay(void) { return tg_ship_display_new(); }
/* broadcast handlers (the exe adds them per display for its ship): ShipDisplay::HandleShipDestroyed 00546e40, HandleShipExitedSet 00546e60,
   HandleChangeTarget 00546ec0 */
static void sd_on_destroyed(TGEvent *e)
{
    int i;
    for (i = 0; i < n_ship_displays; i++) { SdX *x = sdx(ship_displays[i]); if (x->ship && e->dest && ((TGObject *)e->dest)->id == x->ship) eng_ShipDisplay_SetShipID(ship_displays[i], 0); }
}
static void sd_on_exited(TGEvent *e)
{
    void *pl = player(); int i;
    for (i = 0; i < n_ship_displays; i++) {
        SdX *x = sdx(ship_displays[i]); TGObject *o; if (!x->ship) continue;
        o = tg_find(x->ship);
        if (!pl || !o || o->dead || eng_BaseObjectClass_GetContainingSet(pl) != eng_BaseObjectClass_GetContainingSet(o)) eng_ShipDisplay_SetShipID(ship_displays[i], 0);
    }
}
static void sd_on_target(TGEvent *e)
{
    void *pl = player(), *t; int i;
    if (!pl || e->dest != pl) return;
    t = eng_ShipClass_GetTarget(pl); if (t && !(tg_is_ours(t) && tg_is_type(t, 0x8008))) t = NULL;     /* a ship target only (ShipClass::Cast) */
    for (i = 0; i < n_ship_displays; i++) if (sdx(ship_displays[i])->follow_target) eng_ShipDisplay_SetShipID(ship_displays[i], t ? (long)((TGObject *)t)->id : 0);
}

/* ====================================================== ShieldsDisplay (0x80ff) ====================================================
   Children (Tactical.Interface.ShieldsDisplay.Create): 0 display pane (0 top pane [top, front, rear, left, right shield icons], 1 ship icon,
   2 bottom pane [bottom shield icon], 3 "No Target", 4 "Unknown Object"). The shield icons take the colour of their facing's level. */
#define CT_SHIELDS 0x80ff
typedef struct { int active; int last[6]; } ShX;
static UI *shield_displays[8]; static int n_shield_displays;
/* the colour table at 00988418 (20 entries, index = trunc(19 x fraction): dark red, red, orange, yellow, green); entry 0 is clear */
static const float shield_colors[20][4] = {
    {0, 0, 0, 0}, {.61f, 0, 0, 1}, {.61f, 0, 0, 1}, {1, .25f, 0, 1}, {1, .25f, 0, 1}, {1, .41f, 0, 1}, {1, .41f, 0, 1}, {1, .68f, 0, 1}, {1, .86f, 0, 1}, {1, 1, 0, 1},
    {1, 1, 0, 1}, {1, 1, 0, 1}, {.91f, 1, 0, 1}, {.91f, 1, 0, 1}, {.91f, 1, 0, 1}, {.72f, 1, 0, 1}, {.72f, 1, 0, 1}, {.72f, 1, 0, 1}, {0, 1, 0, 1}, {0, 1, 0, 1}};
static UI *sh_parent(UI *s) { UI *p = eng_TGUIObject_GetConceptualParent(s); return as_c(p, CT_SHIPDISP); }
static UI *shield_icon(UI *s, int facing)                                      /* ShieldsDisplay::SetShieldIcon 00545e60: the icon of a facing */
{
    UI *dp = kid(s, 0), *top = kid(dp, 0), *bottom = kid(dp, 2);
    switch (facing) {
    case 0: return kid(top, 1);          /* front */
    case 1: return kid(top, 2);          /* rear */
    case 2: return kid(top, 0);          /* top */
    case 3: return kid(bottom, 0);       /* bottom */
    case 4: return kid(top, 3);          /* left */
    case 5: return kid(top, 4);          /* right */
    }
    return NULL;
}
static int shield_index(void *shields, int facing)
{
    double mx = eng_ShieldClass_GetMaxShields(shields, facing), frac;
    if (!(mx > 0) || !eng_PoweredSubsystem_IsOn(shields)) return 0;
    frac = eng_ShieldClass_GetSingleShieldPercentage(shields, facing);
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    return (int)(frac * 19.0);
}
static void shield_update(UI *s)                                              /* ShieldsDisplay::Update 00545d50 */
{
    UI *par = sh_parent(s), *dp = kid(s, 0); ShX *x = s->ext; void *ship, *shields; int i;
    if (!par || !dp) return;
    if (!sdx(par)->ship) { call_mod("Tactical.Interface.ShieldsDisplay", "RepositionUI", s); return; }
    if (!player()) { hide(dp, 1); return; }
    ship = ship_of(sdx(par)->ship);
    if (!ship || !(shields = eng_ShipClass_GetShields(ship))) { hide(dp, 1); return; }
    for (i = 0; i < 6; i++) {
        UI *ic = shield_icon(s, i); int idx = shield_index(shields, i);
        if (ic) memcpy(ic->color, shield_colors[idx], 16);
        if (x) x->last[i] = idx;
    }
}
UI *tg_shields_display_new(void)
{
    UI *u = pane_new("ShieldsDisplay", 0, 0); ShX *x = calloc(1, sizeof *x); int i;
    u->ext = x; for (i = 0; i < 6; i++) x->last[i] = -1;
    if (n_shield_displays < 8) shield_displays[n_shield_displays++] = u;
    call_mod("Tactical.Interface.ShieldsDisplay", "Create", u);
    return u;
}
void *eng_ShieldsDisplay_Create(float fw, float fh) { return tg_shields_display_new(); }      /* 005457e0 ignores the size */
void *eng_new_ShieldsDisplay(float fw, float fh) { return tg_shields_display_new(); }
void *eng_ShieldsDisplay_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_SHIELDS); }
void eng_ShieldsDisplay_ResizeUI(void *p) { UI *u = as_c(p, CT_SHIELDS); if (u) call_mod("Tactical.Interface.ShieldsDisplay", "ResizeUI", u); }
void eng_ShieldsDisplay_RepositionUI(void *p) { UI *u = as_c(p, CT_SHIELDS); if (u) call_mod("Tactical.Interface.ShieldsDisplay", "RepositionUI", u); }
void eng_ShieldsDisplay_RemoveEvents(void *p) { UI *u = as_c(p, CT_SHIELDS); if (u && u->ext) { int i; ((ShX *)u->ext)->active = 0; for (i = 0; i < 6; i++) ((ShX *)u->ext)->last[i] = -1; } }   /* 00545af0 */
void eng_ShieldsDisplay_UpdateForNewShip(void *p)                            /* 00545ca0 */
{
    UI *u = as_c(p, CT_SHIELDS), *par; ShX *x;
    if (!u) return; x = u->ext; par = sh_parent(u);
    if (par && sdx(par)->ship) {
        call_mod("Tactical.Interface.ShieldsDisplay", "SetShipIcon", u);
        x->active = 1; { int i; for (i = 0; i < 6; i++) x->last[i] = -1; }
        show(kid(u, 0), 1);
    }
    shield_update(u);
}
/* ShieldsDisplay::ShipIdentified 00545d30: the sensors name the ship, the icon is chosen again */
static void sh_on_identified(TGEvent *e)
{
    int i; for (i = 0; i < n_shield_displays; i++) if (((ShX *)shield_displays[i]->ext)->active) call_mod("Tactical.Interface.ShieldsDisplay", "SetShipIcon", shield_displays[i]);
}
static void shields_tick(void)
{
    int i;
    for (i = 0; i < n_shield_displays; i++) {
        UI *u = shield_displays[i]; ShX *x = u->ext; UI *par = sh_parent(u); void *ship, *shields; int k, changed = 0;
        if (!x->active || !par || !sdx(par)->ship || !(ship = ship_of(sdx(par)->ship)) || !(shields = eng_ShipClass_GetShields(ship))) continue;
        for (k = 0; k < 6; k++) if (shield_index(shields, k) != x->last[k]) changed = 1;
        if (changed) shield_update(u);
    }
}

/* ====================================================== DamageDisplay (0x8100) and DamageIcon (0x8101) ===============================
   The damage display is a pane over the ship icon with one DamageIcon per damageable subsystem (group "DamageIcons", the icon number from
   the subsystem class, getDamageIconTypeForSubsystem 00541660). An icon shows when the subsystem is hurt: yellow damaged, gray disabled,
   red destroyed; a damage event makes it blink for 6 seconds (DamageIcon::Refresh 00540aa0). Position: the subsystem property's icon
   position (pixels of the ship icon art, scaled 0.85) from the left and top of the ship icon (ResetPosition 005408b0). */
#define CT_DMGDISP 0x8100
#define CT_DMGICON 0x8101
typedef struct { unsigned sub; int blinking; double expire_at, blink_at; } DiX;
static UI *damage_displays[8]; static int n_damage_displays;
static UI *dd_parent(UI *d) { UI *p = eng_TGUIObject_GetConceptualParent(d); return as_c(p, CT_SHIPDISP); }
void *eng_DamageDisplay_GetShip(void *p) { UI *d = as_c(p, CT_DMGDISP), *par = d ? dd_parent(d) : NULL; return par ? ship_of(sdx(par)->ship) : NULL; }
static void *sub_of(UI *ic) { TGObject *o = ((DiX *)ic->ext)->sub ? tg_find(((DiX *)ic->ext)->sub) : NULL; return o && !o->dead ? o : NULL; }
void *eng_DamageIcon_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_DMGICON); }
void *eng_DamageIcon_GetSubsystem(void *p) { UI *u = as_c(p, CT_DMGICON); return u ? sub_of(u) : NULL; }
void eng_DamageIcon_ResetPosition(void *p)                                    /* 005408b0 */
{
    UI *ic = as_c(p, CT_DMGICON), *d; void *sub, *prop, *ship; float px, py, iw, ih, mw, mh, X, Y;
    if (!ic || !(sub = sub_of(ic)) || !(prop = eng_ShipSubsystem_GetProperty(sub))) return;
    d = ic->parent; if (!d) return;
    ship = eng_DamageDisplay_GetShip(d); if (!ship) return;
    px = py = 0; eng_SubsystemProperty_GetPosition2D(prop, &px, &py);
    { void *sp = eng_ShipClass_GetShipProperty(ship); int species = sp ? (int)eng_ShipProperty_GetSpecies(sp) : 0; float w = 0, h = 0;
      if (!sp || !tg_icon_size("ShipIcons", species, &w, &h)) return; iw = w * 0.85f; ih = h * 0.85f; }
    mw = (float)eng_GraphicsModeInfo_GetWidth(NULL); mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    X = (d->w - iw) * 0.5f + 0.85f * px / mw; Y = (d->h - ih) * 0.5f + 0.85f * py / mh;
    ui_set_pos(ic, X - ic->w * 0.5f, Y - ic->h * 0.5f, 0);
}
static void icon_refresh(UI *ic, int blink)                                   /* DamageIcon::Refresh 00540aa0 */
{
    DiX *x = ic->ext; void *sub = sub_of(ic); double f; const float *c;
    if (!sub) { hide(ic, 0); return; }
    f = eng_ShipSubsystem_GetConditionPercentage(sub);
    if (f >= 1.0 || !eng_ShipSubsystem_IsTargetable(sub)) { hide(ic, 0); return; }
    if (!x->blinking) show(ic, 1);
    if (f <= 0) { c = tg_global("g_kDamageDisplayDestroyedColor"); if (c) memcpy(ic->color, c, 16); return; }
    c = eng_ShipSubsystem_IsDisabled(sub) == 1 ? tg_global("g_kDamageDisplayDisabledColor") : tg_global("g_kDamageDisplayDamagedColor");
    if (c) memcpy(ic->color, c, 16);
    if (!blink) return;
    { int had = x->expire_at > 0; x->expire_at = tg_game_time + 6.0;           /* the expire timer restarts at every hit */
      if (had || x->blinking) return;
      x->blink_at = tg_game_time + 1.0; x->blinking = 1; }
}
int tg_damage_icon_type(void *sub);
static int icon_type(void *sub)                                               /* getDamageIconTypeForSubsystem 00541660 */
{
    if (tg_is_type(sub, 0x8027)) return 0;            /* hull */
    if (tg_is_type(sub, 0x802c)) return 2;            /* phaser */
    if (tg_is_type(sub, 0x8022)) return 3;            /* power */
    if (tg_is_type(sub, 0x8023)) return 4;            /* sensors */
    if (tg_is_type(sub, 0x8028)) return 5;            /* shields */
    if (tg_is_type(sub, 0x802f)) return 7;            /* torpedo tube */
    if (tg_is_type(sub, 0x802d)) return 9;            /* pulse weapon */
    if (tg_is_type(sub, 0x8025)) return 8;            /* warp engine (an engine property with a warp flag) */
    if (tg_is_type(sub, 0x8026)) return 1;            /* impulse engine */
    return 6;
}
int tg_damage_icon_type(void *sub) { return icon_type(sub); }
static void dd_add_icon(UI *d, void *sub)                                     /* DamageDisplay::AddSubsystemIcon 00541550 */
{
    UI *ic; DiX *x; int i, n;
    ic = ui_new("DamageIcon"); ic->icon_group = strdup("DamageIcons"); ic->icon_num = icon_type(sub);
    { UI *t = eng_TGIcon_Create("DamageIcons", icon_type(sub), NULL); ic->w = t->w; ic->h = t->h; t->o.dead = 1; }
    ic->flags |= UF_NOFOCUS;
    x = calloc(1, sizeof *x); x->sub = ((TGObject *)sub)->id; ic->ext = x;
    pane_add(d, d->nkids, ic, 0, 0, 0);
    eng_DamageIcon_ResetPosition(ic); icon_refresh(ic, 0);
    n = (int)eng_ShipSubsystem_GetNumChildSubsystems(sub);
    for (i = 0; i < n; i++) { void *c = eng_ShipSubsystem_GetChildSubsystem(sub, i); if (c) dd_add_icon(d, c); }
}

static UI *dd_make(void)
{
    UI *u = pane_new("DamageDisplay", 0, 0);
    u->flags |= UF_NOFOCUS; u->flags &= ~UF_BATCHCHILD;
    if (n_damage_displays < 8) damage_displays[n_damage_displays++] = u;
    return u;
}
void *eng_DamageDisplay_Create(float fw, float fh) { UI *u = dd_make(); ui_set_size(u, fw, fh, 0); return u; }
void *eng_new_DamageDisplay(float fw, float fh) { return eng_DamageDisplay_Create(fw, fh); }
void *eng_DamageDisplay_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_DMGDISP); }
void eng_DamageDisplay_ResizeUI(void *p) { UI *u = as_c(p, CT_DMGDISP); if (u) call_mod("Tactical.Interface.DamageDisplay", "ResizeUI", u); }
void eng_DamageDisplay_RepositionUI(void *p)                                  /* 005412d0 */
{
    UI *u = as_c(p, CT_DMGDISP); int i; if (!u) return;
    for (i = 0; i < u->nkids; i++) if (as_c(u->kids[i], CT_DMGICON)) eng_DamageIcon_ResetPosition(u->kids[i]);
    ui_layout(u);
}
static void dd_clear(UI *d) { while (d->nkids) eng_TGPane_DeleteChild(d, d->kids[d->nkids - 1], 0); }
void eng_DamageDisplay_RemoveEvents(void *p) { UI *u = as_c(p, CT_DMGDISP); if (u) u->ext = NULL; }       /* 005411e0: handlers and refresh timer; the port polls while the display has a ship */
void eng_DamageDisplay_UpdateForNewShip(void *p)                              /* 00541420 */
{
    UI *d = as_c(p, CT_DMGDISP), *par; void *ship; void **v; int n, i;
    if (!d) return;
    par = dd_parent(d); if (!par) return;
    ui_layout(d);
    dd_clear(d);
    if (!sdx(par)->ship) { hide(d, 0); return; }
    ship = ship_of(sdx(par)->ship); if (!ship) return;
    n = tg_ship_top_subsystems(ship, &v);
    for (i = 0; i < n; i++) dd_add_icon(d, v[i]);
    free(v);
    d->ext = d;                                                                  /* events on (marks a display that polls) */
}
static void dd_on_damaged(TGEvent *e)                                         /* DamageDisplay::HandleSubsystemEvents 00541360 */
{
    int i, k;
    for (i = 0; i < n_damage_displays; i++) {
        UI *d = damage_displays[i];
        if (!d->ext || d->o.dead) continue;
        for (k = 0; k < d->nkids; k++) { UI *ic = as_c(d->kids[k], CT_DMGICON); if (ic && ((DiX *)ic->ext)->sub == e->ref_id) icon_refresh(ic, 1); }
    }
}
static void damage_tick(void)
{
    static double next_refresh; int i, k, refresh = tg_game_time >= next_refresh;
    if (refresh) next_refresh = tg_game_time + 1.0;                               /* ET_DAMAGE_DISPLAY_REFRESH once a second */
    for (i = 0; i < n_damage_displays; i++) {
        UI *d = damage_displays[i];
        if (!d->ext || d->o.dead) continue;
        for (k = 0; k < d->nkids; k++) {
            UI *ic = as_c(d->kids[k], CT_DMGICON); DiX *x; if (!ic) continue; x = ic->ext;
            if (x->expire_at > 0 && tg_game_time >= x->expire_at) {            /* HandleExpire 00540800 */
                x->expire_at = 0; x->blink_at = 0; x->blinking = 0; icon_refresh(ic, 0);
                if (sub_of(ic) && eng_ShipSubsystem_GetConditionPercentage(sub_of(ic)) <= 1.0) show(ic, 1); else hide(ic, 0);
            } else if (x->blinking && tg_game_time >= x->blink_at) {           /* HandleBlink 00540870 */
                int was = (ic->flags & UF_VISIBLE) != 0; x->blink_at += 1.0; icon_refresh(ic, 0);
                if (was) hide(ic, 0); else show(ic, 1);
            } else if (refresh) icon_refresh(ic, 0);
        }
    }
}

/* ====================================================== WeaponsDisplay (0x8102) ====================================================
   A stylized window titled "Speed ..." with one display pane (Tactical.Interface.WeaponsDisplay.Create): 0 torpedo pane (one icon per tube),
   1 upper phaser pane, 2 upper phaser indicator pane, 3 upper disruptor pane, 4 upper disruptor indicator pane, 5 ship icon, 6 lower phaser
   indicator pane, 7 lower phaser pane, 8 lower disruptor indicator pane, 9 lower disruptor pane. The native part colours the strips by
   weapon charge (UpdateChargeColorsForSystem 0054bfd0), the tube icons by readiness (UpdateTorpedoIconColors 0054c110), shows an
   indicator while the target is inside the weapon's arc (0054bc30), and writes the speed into the title (updateShipSpeedText 0054c200).
   The exe refreshes on events and every half second (PeriodicUpdate 0054b210); this port refreshes the colours every frame. */
#define CT_WEAPONS 0x8102
typedef struct { unsigned ship; } WdX;
static UI *weapons_display;
extern UI *stw_new(const char *, const char *, const char *, void *, float, float, UI *, int, float, float, const float *);
extern long eng_UtopiaModule_ConvertGameUnitsToKilometers_l(void);
extern double eng_UtopiaModule_ConvertGameUnitsToKilometers(float);
extern void *eng_ShipClass_GetPhaserSystem(void *), *eng_ShipClass_GetPulseWeaponSystem(void *), *eng_ShipClass_GetTorpedoSystem(void *);
extern double eng_EnergyWeapon_GetChargePercentage(void *);
extern long eng_TorpedoTube_GetNumReady(void *), eng_ShipClass_IsReverse(void *);
extern double eng_ShipClass_GetImpulse(void *);
extern void *eng_BaseObjectClass_GetWorldLocation(void *);
extern void *eng_BaseObjectClass_GetWorldForwardTG(void *);
extern void *eng_PhysicsObjectClass_GetVelocityTG(void *);
extern long eng_WeaponProperty_IsIconAboveShip(void *);
int tg_weapon_point_in_arc(void *ship, void *weapon, const float *world);
extern void *eng_STStylizedWindow_GetNameParagraph(void *);
extern void eng_TGParagraph_SetString(void *, char *, int);
extern void *eng_TGLocalizationManager_Load(void *, char *, int); extern void eng_TGLocalizationManager_Unload(void *, void *);
extern void *eng_TGLocalizationDatabase_GetString(void *, char *); extern const char *eng_TGString_GetCString(void *);
extern void *eng_ShipClass_GetTargetOffset(void *);
static WdX *wdx(UI *u) { return u ? u->ext : NULL; }
static UI *wd_pane(UI *w) { return kid(w && w->nkids ? w->kids[0] : NULL, 0); }       /* the window's first child is its scroll window, whose first child is the interior pane */
static UI *wd_display_pane(UI *w)                                               /* GetNthChild(0) of the window: forwarded to the interior (the display pane) */
{ UI *in = fwd(w); return in && in->nkids ? in->kids[0] : NULL; }
static void wd_create_ui(UI *w) { call_mod_size("Tactical.Interface.WeaponsDisplay", "Create", w, w->w, w->h); }
UI *tg_weapons_display_new(float fw, float fh)                                  /* 0054aea0 */
{
    void *t = eng_new_TGString("Speed"); UI *w = stw_new("WeaponsDisplay", "StylizedWindow", "WeaponsDisplay", t, 0, 0, NULL, 0, 0, 0, tg_global("g_kInterfaceBorderColor"));
    UI *in = w->nkids ? fwd(w) : NULL;
    w->ext = calloc(1, sizeof(WdX));
    if (in && in->nkids) in->kids[0]->flags &= ~UF_BATCHCHILD;
    call_mod_size("Tactical.Interface.WeaponsDisplay", "Create", w, fw, fh);
    weapons_display = w;
    return w;
}
void *eng_WeaponsDisplay_Create(float fw, float fh) { return tg_weapons_display_new(fw, fh); }
void *eng_new_WeaponsDisplay(float fw, float fh) { return tg_weapons_display_new(fw, fh); }
void *eng_WeaponsDisplay_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_WEAPONS); }
long eng_WeaponsDisplay_GetShipID(void *p) { UI *u = as_c(p, CT_WEAPONS); return u ? (long)wdx(u)->ship : 0; }
static void wd_update_charge(UI *dp, void *ship);
static void wd_update_tubes(UI *dp, void *ship);
static void wd_update_indicators(UI *dp, void *ship);
static void wd_update(UI *w)                                                    /* WeaponsDisplay::Update 0054ba30 */
{
    UI *dp = wd_display_pane(w); void *ship = ship_of(wdx(w)->ship);
    if (!dp || !ship) return;
    wd_update_charge(dp, ship); wd_update_tubes(dp, ship); wd_update_indicators(dp, ship);
}
void eng_WeaponsDisplay_SetShipID(void *p, long id)                             /* 0054b950 */
{
    UI *w = as_c(p, CT_WEAPONS), *dp; if (!w) return;
    wdx(w)->ship = (unsigned)id;
    wd_create_ui(w);
    if (!id) return;
    call_mod("Tactical.Interface.WeaponsDisplay", "SetShipIcon", w);               /* CallSetShipIcon 0054b9f0 */
    dp = wd_display_pane(w); show(dp, 1);
    wd_update(w);
}
static void *wd_system(void *ship, int which) { return which == 0 ? eng_ShipClass_GetPhaserSystem(ship) : eng_ShipClass_GetPulseWeaponSystem(ship); }
/* the two icon panes of a weapon system: phasers use children 1 (upper) and 7 (lower), disruptors 3 and 9; indicators 2, 6 and 4, 8 */
static void wd_panes(UI *dp, int which, int indicator, UI **upper, UI **lower)
{
    static const int idx[2][2][2] = {{{1, 7}, {2, 6}}, {{3, 9}, {4, 8}}};
    *upper = kid(dp, idx[which][indicator][0]); *lower = kid(dp, idx[which][indicator][1]);
}
static void wd_charge_system(void *sys, UI *upper, UI *lower)                    /* UpdateChargeColorsForSystem 0054bfd0 */
{
    int i, n = sys ? (int)eng_ShipSubsystem_GetNumChildSubsystems(sys) : 0, up = 0, lo = 0;
    for (i = 0; i < n; i++) {
        void *wp = eng_ShipSubsystem_GetChildSubsystem(sys, i), *prop; UI *ic; double ch; int above;
        if (!wp || !tg_is_type(wp, 0x802b) || !(prop = eng_ShipSubsystem_GetProperty(wp))) continue;
        above = (int)eng_WeaponProperty_IsIconAboveShip(prop) == 1;
        ic = above ? kid(upper, up++) : kid(lower, lo++);
        if (!ic) continue;
        ch = eng_EnergyWeapon_GetChargePercentage(wp);
        if (ch > 0) {
            if (eng_ShipSubsystem_IsDisabled(wp) == 1) { static const float gray[4] = {.25f, .25f, .25f, 1}; memcpy(ic->color, gray, 16); }
            else { int idx = (int)(ch * 19.0); if (idx > 19) idx = 19; memcpy(ic->color, shield_colors[idx], 16); }
        } else memcpy(ic->color, shield_colors[0], 16);
    }
}
static void wd_update_charge(UI *dp, void *ship)
{
    UI *u, *l; wd_panes(dp, 0, 0, &u, &l); wd_charge_system(eng_ShipClass_GetPhaserSystem(ship), u, l);
    wd_panes(dp, 1, 0, &u, &l); wd_charge_system(eng_ShipClass_GetPulseWeaponSystem(ship), u, l);
}
static void wd_update_tubes(UI *dp, void *ship)                                  /* UpdateTorpedoIconColors 0054c110 */
{
    void *sys = eng_ShipClass_GetTorpedoSystem(ship); UI *pane = kid(dp, 0); int i, n;
    if (!sys || !pane) return;
    n = (int)eng_ShipSubsystem_GetNumChildSubsystems(sys);
    for (i = 0; i < n; i++) {
        void *tube = eng_ShipSubsystem_GetChildSubsystem(sys, i); UI *ic = kid(pane, i); const float *c;
        if (!tube || !ic) continue;
        if (eng_ShipSubsystem_GetCondition(tube) > 0) {
            if (eng_ShipSubsystem_IsDisabled(tube) == 1) { static const float gray[4] = {.25f, .25f, .25f, 1}; c = gray; }
            else if (eng_TorpedoTube_GetNumReady(tube) == 0 || !eng_PoweredSubsystem_IsOn(sys)) c = tg_global("g_kTIPhotonNotReadyColor");
            else c = tg_global("g_kTIPhotonReadyColor");
        } else { static const float black[4] = {0, 0, 0, 1}; c = black; }
        if (c) memcpy(ic->color, c, 16);
    }
}
static void wd_ind_system(void *ship, void *sys, UI *upper, UI *lower)           /* the arc indicators, FUN_0054bc30 */
{
    void *tgt = eng_ShipClass_GetTarget(ship), *tp = NULL; int i, n = sys ? (int)eng_ShipSubsystem_GetNumChildSubsystems(sys) : 0, up = 0, lo = 0;
    if (!sys) return;
    if (tgt) tp = eng_BaseObjectClass_GetWorldLocation(tgt);
    for (i = 0; i < n; i++) {
        void *wp = eng_ShipSubsystem_GetChildSubsystem(sys, i), *prop; UI *ic; int in = 0;
        if (!wp || !tg_is_type(wp, 0x802b) || !(prop = eng_ShipSubsystem_GetProperty(wp))) continue;
        ic = eng_WeaponProperty_IsIconAboveShip(prop) == 1 ? kid(upper, up++) : kid(lower, lo++);
        if (tp && (tg_is_type(wp, 0x802c) || tg_is_type(wp, 0x802d))) in = tg_weapon_point_in_arc(ship, wp, tp);
        if (!ic) continue;
        if (in && eng_PoweredSubsystem_IsOn(sys) && !eng_ShipSubsystem_IsDisabled(wp)) show(ic, 0); else hide(ic, 0);
    }
    free(tp);
}
static void wd_update_indicators(UI *dp, void *ship)
{
    UI *u, *l; wd_panes(dp, 0, 1, &u, &l); wd_ind_system(ship, eng_ShipClass_GetPhaserSystem(ship), u, l);
    wd_panes(dp, 1, 1, &u, &l); wd_ind_system(ship, eng_ShipClass_GetPulseWeaponSystem(ship), u, l);
}
/* the title: "Speed" + (R when reversing) + " <impulse> : <kph> kph"; 9+ when the impulse setting is the highest (updateShipSpeedText 0054c200) */
static void wd_speed_text(void *ship)
{
    UI *w = weapons_display; void *para, *db; float *vel, *fwd_v; char buf[96], num[48]; double kph, imp; int rev, impi;
    extern void *eng_PhysicsObjectClass_GetVelocityTG(void *);
    if (!w || w->o.dead || !ship) return;
    para = eng_STStylizedWindow_GetNameParagraph(w); if (!para) return;
    vel = eng_PhysicsObjectClass_GetVelocityTG(ship); if (!vel) return;
    kph = sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]) * eng_UtopiaModule_ConvertGameUnitsToKilometers(1.0f) * 3600.0;
    rev = (int)eng_ShipClass_IsReverse(ship);
    if (rev) { fwd_v = eng_BaseObjectClass_GetWorldForwardTG(ship); rev = fwd_v && vel[0] * fwd_v[0] + vel[1] * fwd_v[1] + vel[2] * fwd_v[2] < 0; free(fwd_v); }
    free(vel);
    imp = eng_ShipClass_GetImpulse(ship); impi = (int)(imp * 9.0 + 0.1);
    if (rev) snprintf(num, sizeof num, " : -%d ", (int)(kph + 0.5)); else snprintf(num, sizeof num, "%d : %d ", impi, (int)(kph + 0.5));
    db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Bridge Menus.tgl", 1);
    { void *sp = db ? eng_TGLocalizationDatabase_GetString(db, "Speed") : NULL, *kp = db ? eng_TGLocalizationDatabase_GetString(db, "kph") : NULL;
      snprintf(buf, sizeof buf, "%s %s%s%s", sp ? eng_TGString_GetCString(sp) : "Speed", rev ? "R" : "", num, kp ? eng_TGString_GetCString(kp) : "kph"); }
    if (db) eng_TGLocalizationManager_Unload(NULL, db);
    eng_TGParagraph_SetString(para, buf, 1);
}
static void weapons_tick(void)
{
    void *pl = player();
    if (!weapons_display || weapons_display->o.dead) return;
    if (pl) wd_speed_text(pl);
    wd_update(weapons_display);
}

/* ====================================================== common =================================================================== */
void tg_tac_displays_init(void)
{
    static int done;
    if (done) return; done = 1;
    tg_register_native_broadcast((int)tg_const("ET_OBJECT_DESTROYED"), sd_on_destroyed);
    tg_register_native_broadcast((int)tg_const("ET_EXITED_SET"), sd_on_exited);
    tg_register_native_broadcast((int)tg_const("ET_TARGET_WAS_CHANGED"), sd_on_target);
    tg_register_native_broadcast((int)tg_const("ET_SENSORS_SHIP_IDENTIFIED"), sh_on_identified);
    tg_register_native_broadcast((int)tg_const("ET_SUBSYSTEM_DAMAGED"), dd_on_damaged);
}
void tg_tac_displays_tick(void)
{
    tg_tac_displays_init();
    { extern void tg_tac_ctrl_tick(void); tg_tac_ctrl_tick(); }
    { extern void tg_eng_ui_tick(void), tg_eng_repair_tick(void); tg_eng_ui_tick(); tg_eng_repair_tick(); }
    gauges_tick(); shields_tick(); damage_tick(); weapons_tick();
}
