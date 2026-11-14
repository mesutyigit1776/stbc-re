/* Engineering panels: EngPowerDisplay 0054f9b0 (the power grid of the engineering menu) and the helpers of EngPowerCtrl and EngRepairPane.
   The display is a TGPane whose contents the script Bridge.PowerDisplay builds: the constructor calls PowerDisplay.Init(self); the exe keeps
   the one display in DAT_0098b044 (GetPowerDisplay). */
#include "tg_ui.h"

extern void *eng_Game_GetCurrentPlayer(void); extern void *eng_ShipClass_GetPowerSubsystem(void *);
extern void *eng_new_TGString(char *); extern void eng_delete_TGString(void *); extern int tg_is_stylized_window(const void *);
UI *tg_battery_gauge(int kind, void *power);
static UI *as_c(void *p, unsigned ct) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }

extern void *eng_GraphicsModeInfo_GetCurrentMode(void); extern const char *eng_GraphicsModeInfo_GetLcarsString(void *); extern void *eng_TGIcon_Create(char *, int, PyObject *);
#define CT_POWERDISPLAY 0x8108
static UI *epd;
static void power_py_call(const char *func, UI *self)
{
    PyObject *m = PyImport_ImportModule("Bridge.PowerDisplay"), *f, *a, *r;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)func); Py_DECREF(m); if (!f) { PyErr_Clear(); return; }
    a = self ? Py_BuildValue("(O)", appc_pyobj(self, "EngPowerDisplay")) : PyTuple_New(0);
    r = PyEval_CallObject(f, a); Py_DECREF(f); Py_XDECREF(a);
    if (!r) { PyErr_Print(); PyErr_Clear(); } Py_XDECREF(r);
}
void *eng_EngPowerDisplay_Create(float w, float h)
{
    UI *u = pane_new("EngPowerDisplay", w, h); epd = u; power_py_call("Init", u); return u;                /* the constructor runs Bridge.PowerDisplay.Init */
}
void *eng_new_EngPowerDisplay(float w, float h) { return eng_EngPowerDisplay_Create(w, h); }
void eng_delete_EngPowerDisplay(void *p) { if (epd == p) epd = NULL; }
void *eng_EngPowerDisplay_GetPowerDisplay(void) { return epd; }
void *eng_EngPowerDisplay_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_POWERDISPLAY); }
void *eng_EngPowerDisplay_CreateBatteryGauge(void *self, int kind)                                             /* 0054fb70 */
{
    void *pl = eng_Game_GetCurrentPlayer(), *ps = pl ? eng_ShipClass_GetPowerSubsystem(pl) : NULL;
    return ps ? tg_battery_gauge(kind, ps) : NULL;
}

/* ====================================================== EngPowerCtrl (0x8107) ======================================================
   The power allocation sliders of the power display (EngPowerCtrl 0054cfe0, RebuildPowerBars 0054d410, AddSubsystemBar 0054d940,
   HandlePowerChange 0054dde0, Refresh 0054e420). One STNumericBar (range 0 to 1.25, marker at 1.0, key interval 0.1) per group of the
   player's ship: weapons (phasers, torpedoes, pulse weapons), engines (impulse, warp), shields, sensors. Moving a bar sends
   ET_SUBSYSTEM_POWER_CHANGED (a float event with the bar as source) to the control, which sets the wanted power percentage of every
   subsystem of the group (on above 0, off at 0) and tells the ship. The control refreshes the bars once a second from the subsystems.
   Approximations (marked): the bar label is the group name from the localization file; the divider ticks between the bars (12 icons) are
   not made; the layout (bar height 1.75 x label height, gap 0.003) is read from the exe, the bar width is the control's width. */
#define CT_POWERCTRL 0x8107
extern void *eng_STNumericBar_Create(float, float); extern void eng_STNumericBar_SetRange(void *, float, float), eng_STNumericBar_SetValue(void *, float);
extern void eng_STNumericBar_SetMarkerValue(void *, float), eng_STNumericBar_SetUseMarker(void *, int), eng_STNumericBar_SetUseButtons(void *, int);
extern void eng_STNumericBar_SetKeyInterval(void *, float), eng_STNumericBar_SetNormalColor(void *, PyObject *), eng_STNumericBar_SetEmptyColor(void *, PyObject *);
extern void eng_STNumericBar_SetUpdateEvent(void *, void *); extern void *eng_STNumericBar_GetText(void *);
extern void *eng_ShipClass_GetPhaserSystem(void *), *eng_ShipClass_GetTorpedoSystem(void *), *eng_ShipClass_GetPulseWeaponSystem(void *), *eng_ShipClass_GetImpulseEngineSubsystem(void *);
extern void *eng_ShipClass_GetWarpEngineSubsystem(void *), *eng_ShipClass_GetShields(void *), *eng_ShipClass_GetSensorSubsystem(void *);
extern double eng_PoweredSubsystem_GetPowerPercentageWanted(void *); extern void eng_PoweredSubsystem_SetPowerPercentageWanted(void *, float);
extern long eng_PoweredSubsystem_IsOn(void *); extern void eng_PoweredSubsystem_TurnOn(void *), eng_PoweredSubsystem_TurnOff(void *);
extern void *eng_TGLocalizationManager_Load(void *, char *, int); extern void eng_TGLocalizationManager_Unload(void *, void *);
extern void *eng_TGLocalizationDatabase_GetString(void *, char *); extern void eng_TGParagraph_SetStringW(void *, void *, int);
extern void eng_TGPane_DeleteChild(void *, void *, int); extern void eng_TGParagraph_SetString(void *, char *, int);
void tg_numeric_colours(UI *, const float *, const float *); void tg_numeric_show_pct(UI *); extern double tg_game_time;
#define EPC_MAXBARS 4
typedef struct { UI *bar; unsigned subs[3]; int nsubs; char group; } EpcBar;
typedef struct { EpcBar bars[EPC_MAXBARS]; int n; } EpcX;
static UI *epc;
static EpcX *epx(UI *u) { return u ? u->ext : NULL; }
static void *ship_now(void) { void *p = eng_Game_GetCurrentPlayer(); return p && tg_is_ours(p) && tg_is_type(p, 0x8008) ? p : NULL; }
static void epc_add_sub(EpcBar *b, void *s) { if (s && b->nsubs < 3) b->subs[b->nsubs++] = ((TGObject *)s)->id; }
static const float *epc_colour(const char *name) { const float *c = tg_global(name); return c ? c : NULL; }
static float epc_wanted_avg(EpcBar *b, int *any_on)
{
    float sum = 0; int n = 0, i; *any_on = 0;
    for (i = 0; i < b->nsubs; i++) { void *s = tg_find(b->subs[i]); if (!s) continue; sum += (float)eng_PoweredSubsystem_GetPowerPercentageWanted(s); n++; if (eng_PoweredSubsystem_IsOn(s)) *any_on = 1; }
    return n ? sum / n : 0;
}
static void epc_send_change(void *sub, float v)                                                               /* SendPowerChangeEvent 0054e690 */
{
    TGEvent *e = (TGEvent *)tg_ev_new("TGFloatEvent", 0); extern void *eng_ShipSubsystem_GetParentShip(void *);
    e->type = (unsigned)tg_const("ET_SUBSYSTEM_POWER_CHANGED"); e->source = sub; e->dest = eng_ShipSubsystem_GetParentShip(sub); e->v.f = v; tg_add_event(e);
}
static void epc_add_bar(UI *u, const char *label_en, const char *gcolour, void *a, void *b, void *c)
{
    EpcX *x = epx(u); EpcBar *eb; UI *bar; void *db, *s; static const float empty[4] = {0.458f, 0.458f, 0.458f, 1.0f}; float h, y = 0, th;
    void *tx;
    if (!(a || b || c) || x->n >= EPC_MAXBARS) return;
    eb = &x->bars[x->n++]; memset(eb, 0, sizeof *eb); epc_add_sub(eb, a); epc_add_sub(eb, b); epc_add_sub(eb, c);
    bar = eng_STNumericBar_Create(u->w > 0 ? u->w : 0.3f, 0.0f); eb->bar = bar;
    eng_STNumericBar_SetRange(bar, 0, 1.25f); eng_STNumericBar_SetMarkerValue(bar, 1.0f); eng_STNumericBar_SetUseMarker(bar, 1);
    eng_STNumericBar_SetUseButtons(bar, 0); eng_STNumericBar_SetKeyInterval(bar, 0.1f);
    tg_numeric_colours(bar, epc_colour(gcolour), empty); tg_numeric_show_pct(bar);
    tx = eng_STNumericBar_GetText(bar);
    db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Bridge Menus.tgl", 1);
    s = db ? eng_TGLocalizationDatabase_GetString(db, (char *)label_en) : NULL;
    if (s && tx) eng_TGParagraph_SetStringW(tx, s, 1); else if (tx) eng_TGParagraph_SetString(tx, (char *)label_en, 1);
    if (db) eng_TGLocalizationManager_Unload(NULL, db);
    th = tx ? ((UI *)tx)->h : 0.02f; h = th * 1.75f; if (h < 0.02f) h = 0.02f;
    ui_set_size(bar, u->w, h, 0);
    { int i; for (i = 0; i < x->n - 1; i++) y += x->bars[i].bar->h + 0.003f; }
    pane_add(u, u->nkids, bar, 0, y, 0);
    { TGEvent *e = (TGEvent *)tg_ev_new("TGFloatEvent", 0); e->type = (unsigned)tg_const("ET_SUBSYSTEM_POWER_CHANGED"); e->dest = &u->o; eng_STNumericBar_SetUpdateEvent(bar, e); }
}
static void epc_rebuild(UI *u, void *ship)
{
    EpcX *x = epx(u); void *ph, *to, *pu; float y = 0; int i;
    while (u->nkids) eng_TGPane_DeleteChild(u, u->kids[0], 0);
    x->n = 0;
    ph = eng_ShipClass_GetPhaserSystem(ship); to = eng_ShipClass_GetTorpedoSystem(ship); pu = eng_ShipClass_GetPulseWeaponSystem(ship);
    epc_add_bar(u, "Weapons", "g_kEngineeringWeaponsColor", ph, to, pu);
    epc_add_bar(u, "Engines", "g_kEngineeringEnginesColor", eng_ShipClass_GetImpulseEngineSubsystem(ship), eng_ShipClass_GetWarpEngineSubsystem(ship), NULL);
    epc_add_bar(u, "Shields", "g_kEngineeringShieldsColor", eng_ShipClass_GetShields(ship), NULL, NULL);
    epc_add_bar(u, "Sensors", "g_kEngineeringSensorsColor", eng_ShipClass_GetSensorSubsystem(ship), NULL, NULL);
    for (i = 0; i < x->n; i++) y += x->bars[i].bar->h + 0.003f;
    if (x->n) {                                                           /* 12 divider lines across the bars every 0.1 of the value (RebuildPowerBars 0054d410); every fifth is twice as wide */
        UI *first = x->bars[0].bar, *last = x->bars[x->n - 1].bar; const char *g = eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode());
        static const float dark[4] = {0, 0, 0, 1};
        for (i = 1; i <= 12; i++) {
            UI *t = eng_TGIcon_Create((char *)g, 200, NULL); float tw = (i % 5 == 0 ? 2.0f : 1.0f) / ui_scr_w();
            memcpy(t->color, dark, 16); ui_set_size(t, tw, last->y + last->h - first->y, 0);
            pane_add(u, u->nkids, t, first->x + first->w * (i * 0.08f) - tw * 0.5f, first->y, 0);
        }
    }
    ui_set_size(u, u->w, y, 0);
}
static void epc_refresh(UI *u)                                                                                /* Refresh 0054e420 */
{
    EpcX *x = epx(u); int i;
    for (i = 0; i < x->n; i++) {
        EpcBar *b = &x->bars[i]; int on; float v = epc_wanted_avg(b, &on); if (!on) v = 0;
        eng_STNumericBar_SetValue(b->bar, v);
    }
}
static void epc_on_change(TGEvent *e)                                                                         /* HandlePowerChange 0054dde0 */
{
    UI *u = (UI *)e->dest; EpcX *x = epx(u); int i, j;
    if (!x) return;
    for (i = 0; i < x->n; i++) if ((void *)x->bars[i].bar == (void *)e->source) {
        EpcBar *b = &x->bars[i];
        for (j = 0; j < b->nsubs; j++) {
            void *s = tg_find(b->subs[j]); if (!s) continue;
            eng_PoweredSubsystem_SetPowerPercentageWanted(s, e->v.f);
            if (e->v.f <= 0) { if (eng_PoweredSubsystem_IsOn(s)) eng_PoweredSubsystem_TurnOff(s); } else if (!eng_PoweredSubsystem_IsOn(s)) eng_PoweredSubsystem_TurnOn(s);
        }
        if (b->nsubs) epc_send_change(tg_find(b->subs[0]), e->v.f);
        return;
    }
}
static void epc_on_set_player(TGEvent *e)
{
    void *s = ship_now(); if (epc && s) epc_rebuild(epc, s);
}
static double epc_timer; 
void tg_eng_ui_tick(void)
{
    double t; if (!epc) return;
    t = tg_game_time; if (t < epc_timer) return; epc_timer = t + 1.0;
    epc_refresh(epc);
}
void *eng_EngPowerCtrl_Create(float w)
{
    UI *u = pane_new("EngPowerCtrl", w, 0); void *s; u->ext = calloc(1, sizeof(EpcX)); epc = u;
    tg_register_native(u, (int)tg_const("ET_SUBSYSTEM_POWER_CHANGED"), epc_on_change);
    { static int once; if (!once) { once = 1; tg_register_native_broadcast((int)tg_const("ET_SET_PLAYER"), epc_on_set_player); } }
    s = ship_now(); if (s) { epc_rebuild(u, s); epc_refresh(u); }
    return u;
}
void *eng_new_EngPowerCtrl(float w) { return eng_EngPowerCtrl_Create(w); }
void eng_delete_EngPowerCtrl(void *p) { if (epc == p) epc = NULL; }
void *eng_EngPowerCtrl_GetPowerCtrl(void) { return epc; }
void eng_EngPowerCtrl_Refresh(void *p) { if (epc) epc_refresh(epc); }
void *eng_EngPowerCtrl_GetBarForSubsystem(void *p, void *sub)                                                  /* 0054dc90: the same group counts */
{
    EpcX *x = epx(epc); int i, j; unsigned id = sub ? ((TGObject *)sub)->id : 0;
    if (!x || !id) return NULL;
    for (i = 0; i < x->n; i++) for (j = 0; j < x->bars[i].nsubs; j++) if (x->bars[i].subs[j] == id) return x->bars[i].bar;
    return NULL;
}

/* ====================================================== EngRepairPane (0x8109) ======================================================
   The repair lists of the engineering menu (EngRepairPane 00550770, Refresh 005512e0, LayoutSubPanes 00551000, ResizeToContents 00551cc0).
   Eight children in this order (the script's EngRepairPane_* indices): 0 "Repair team assignments:" label, 1 repair area, 2 "Damaged
   systems:" label, 3 waiting area, 4 "Destroyed systems:" label, 5 destroyed area, 6 and 7 divider icons (icon 200 of the LCARS group).
   The first NumRepairTeams entries of the repair queue of the player's ship are in the repair area, the rest in the waiting area, the
   destroyed subsystems in the destroyed area; a click on a button sends ET_REPAIR_INCREASE_PRIORITY (a TGObjPtrEvent) for its subsystem,
   which the repair subsystem answers (tg_ship.c). A button shows the subsystem name and its condition gauge (FUN_005519e0).
   Approximations (marked): the exe updates the buttons from the repair subsystem's events; here the pane compares the lists twice a
   second and rebuilds the buttons when they changed; the destroyed list is every subsystem at condition 0; the vertical gaps are the
   indent (0.003) between the parts. */
#define CT_REPAIRPANE 0x8109
extern void *eng_STSubPane_Create(float, float, int); extern void *eng_GraphicsModeInfo_GetCurrentMode(void); extern const char *eng_GraphicsModeInfo_GetLcarsString(void *);
extern void *eng_TGIcon_Create(char *, int, PyObject *); extern void *eng_TGParagraph_CreateW(void *, float, PyObject *, char *, float, int);
extern void *eng_ShipSubsystem_GetDisplayName(void *);
extern void eng_STStylizedWindow_InteriorChangedSize(void *, int); extern double eng_STStylizedWindow_GetMaximumInteriorWidth(void *);
extern void eng_STSubPane_ResizeToContents(void *, int);
extern int tg_repair_queue(void *ship, void **out, int max, int *teams); extern int tg_destroyed_subsystems(void *ship, void **out, int max);
UI *tg_button_new(void *name, void *ev); void tg_button_gauge(UI *b, float f); int tg_damage_icon_type(void *sub); void tg_menu_resized(UI *m);
#define ERP_MAX 40
typedef struct { int teams; unsigned lists[3][ERP_MAX]; int n[3]; double next; } ErpX;
static UI *erp;
static ErpX *erx(UI *u) { return u ? u->ext : NULL; }
static UI *erp_area(UI *u, int which) { return u->nkids > 2 * which + 1 ? u->kids[2 * which + 1] : NULL; }
static void erp_layout(UI *u)                                                                                  /* LayoutSubPanes 00551000 */
{
    static const int order[8] = {0, 1, 6, 2, 3, 7, 4, 5}; float y = 0, gap = 0.003f; int i;
    for (i = 0; i < 8 && i < u->nkids; i++) { UI *c = u->kids[order[i]]; ui_set_pos(c, 0, y, 0); y += c->h + gap; }
    ui_set_size(u, u->w, y, 0);
}
static float erp_width(UI *u)                                                                                  /* ResizeToContents 00551cc0: the window's inside */
{
    UI *p; for (p = u->parent; p; p = p->parent) if (tg_is_stylized_window(p)) return (float)eng_STStylizedWindow_GetMaximumInteriorWidth(p);
    return u->w;
}
static void erp_resize(UI *u)
{
    int i; float w = erp_width(u);
    for (i = 0; i < 3 && 2 * i + 1 < u->nkids; i++) {
        UI *a = u->kids[2 * i + 1]; int k;
        if (a->w != w) ui_set_size(a, w, a->h, 0);
        for (k = 0; k < a->nkids; k++) ui_set_size(a->kids[k], w, a->kids[k]->h, 0);
        eng_STSubPane_ResizeToContents(a, 0);
        for (k = 0; k < a->nkids; k++) { UI *b = a->kids[k]; if (b->nkids) { UI *ic = b->kids[0]; float bh = b->h > 0 ? b->h : 0.0417f; ui_set_pos(ic, b->w - ic->w - 0.006f, (bh - ic->h) * 0.5f, 0); } }   /* the subsystem icon at the right end */
    }
    if (u->kids && u->nkids > 7) { ui_set_size(u->kids[6], w, u->kids[6]->h, 0); ui_set_size(u->kids[7], w, u->kids[7]->h, 0); }
    ui_set_size(u, w, u->h, 0); erp_layout(u);
    { UI *p; for (p = u->parent; p; p = p->parent) if (tg_is_stylized_window(p)) { eng_STStylizedWindow_InteriorChangedSize(p, 0); break; } }
}
static void erp_button_look(UI *b, void *s)                                                                    /* the condition body and the subsystem icon (StRepairButton::UpdateIconColor 0052d0e0) */
{
    extern double eng_ShipSubsystem_GetCombinedConditionPercentage(void *), eng_ShipSubsystem_GetCondition(void *); extern long eng_ShipSubsystem_IsDisabled(void *);
    UI *ic = b->nkids ? b->kids[0] : NULL; const float *c;
    tg_button_gauge(b, (float)eng_ShipSubsystem_GetCombinedConditionPercentage(s));
    if (!ic) return;
    c = tg_global(eng_ShipSubsystem_GetCondition(s) <= 0 ? "g_kDamageDisplayDestroyedColor" : eng_ShipSubsystem_IsDisabled(s) ? "g_kDamageDisplayDisabledColor" : "g_kDamageDisplayDamagedColor");
    if (c) memcpy(ic->color, c, 16);
}
static void erp_button(UI *area, void *s)                                                                      /* AddSubsystemButton 00551870 */
{
    void *nm = eng_ShipSubsystem_GetDisplayName(s); TGEvent *e = (TGEvent *)tg_ev_new("TGObjPtrEvent", 0); UI *b, *ic;
    e->type = (unsigned)tg_const("ET_REPAIR_INCREASE_PRIORITY"); e->ref_id = ((TGObject *)s)->id; e->v.p = s;
    b = tg_button_new(nm, e); if (nm) eng_delete_TGString(nm);
    ic = eng_TGIcon_Create("DamageIcons", tg_damage_icon_type(s), NULL); pane_add(b, b->nkids, ic, 0, 0, 0);
    erp_button_look(b, s);
    pane_add(area, area->nkids, b, 0, 0, 0);
}
static void erp_refresh(UI *u)                                                                                 /* Refresh 005512e0 */
{
    ErpX *x = erx(u); void *ship = eng_Game_GetCurrentPlayer(), *q[ERP_MAX], *d[ERP_MAX]; int nq, nd, teams = 0, i, w; unsigned ids[3][ERP_MAX]; int cnt[3];
    if (!ship || !tg_is_ours(ship) || !tg_is_type(ship, 0x8008)) return;
    nq = tg_repair_queue(ship, q, ERP_MAX, &teams); nd = tg_destroyed_subsystems(ship, d, ERP_MAX); if (!teams) teams = x->teams;
    cnt[0] = nq < teams ? nq : teams; cnt[1] = nq - cnt[0]; cnt[2] = nd;
    for (i = 0; i < cnt[0]; i++) ids[0][i] = ((TGObject *)q[i])->id;
    for (i = 0; i < cnt[1]; i++) ids[1][i] = ((TGObject *)q[teams + i])->id;
    for (i = 0; i < nd; i++) ids[2][i] = ((TGObject *)d[i])->id;
    for (w = 0; w < 3; w++) if (cnt[w] != x->n[w] || memcmp(ids[w], x->lists[w], cnt[w] * sizeof(unsigned))) break;
    if (w == 3) return;
    for (w = 0; w < 3; w++) {
        UI *a = erp_area(u, w); if (!a) continue;
        while (a->nkids) eng_TGPane_DeleteChild(a, a->kids[0], 0);
        for (i = 0; i < cnt[w]; i++) { void *s = tg_find(ids[w][i]); if (s) erp_button(a, s); }
        x->n[w] = cnt[w]; memcpy(x->lists[w], ids[w], cnt[w] * sizeof(unsigned));
    }
    erp_resize(u);
}
void tg_eng_repair_tick(void)
{
    extern double tg_game_time; ErpX *x = erx(erp); int w, k;
    if (!x || tg_game_time < x->next) return; x->next = tg_game_time + 0.5; erp_refresh(erp);
    for (w = 0; w < 3; w++) { UI *a = erp_area(erp, w); for (k = 0; a && k < x->n[w] && k < a->nkids; k++) { void *s = tg_find(x->lists[w][k]); if (s) erp_button_look(a->kids[k], s); } }
}
static void erp_on_event(TGEvent *e) { if (erp && !erp->o.dead) erp_refresh(erp); }                          /* the repair events of RepairSubsystem 00565980..00565b50 */
static UI *erp_new(float fw, float fh, int teams)
{
    static int registered;
    if (!registered) {
        static const char *evs[] = {"ET_REPAIR_COMPLETED", "ET_REPAIR_CANNOT_BE_COMPLETED", "ET_ADD_TO_REPAIR_LIST", "ET_REPAIR_INCREASE_PRIORITY", "ET_SUBSYSTEM_DAMAGED", "ET_SUBSYSTEM_REBUILT", "ET_SUBSYSTEM_COMPLETELY_DESTROYED", "ET_SET_PLAYER"}; int i;
        registered = 1; for (i = 0; i < 8; i++) tg_register_native_broadcast((int)tg_const(evs[i]), erp_on_event);
    }
    UI *u = pane_new("EngRepairPane", fw, fh), *k[8]; void *db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Bridge Menus.tgl", 1);
    static char *lab[3] = {"Repair team assignments:", "Damaged systems:", "Destroyed systems:"}; static const float white[4] = {1, 1, 1, 1};
    char grp[32]; int i; ErpX *x = calloc(1, sizeof *x);
    u->ext = x; x->teams = teams; erp = u;
    snprintf(grp, sizeof grp, "%s", eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode()));
    for (i = 0; i < 3; i++) {
        void *s = db ? eng_TGLocalizationDatabase_GetString(db, lab[i]) : NULL, *t = s ? NULL : eng_new_TGString(lab[i]);
        k[2 * i] = eng_TGParagraph_CreateW(s ? s : t, 0, NULL, NULL, 0, 0); if (t) eng_delete_TGString(t);
        memcpy(k[2 * i]->color, white, 16);
        k[2 * i + 1] = eng_STSubPane_Create(fw, 0, 1);
    }
    if (db) eng_TGLocalizationManager_Unload(NULL, db);
    k[6] = eng_TGIcon_Create(grp, 200, NULL); k[7] = eng_TGIcon_Create(grp, 200, NULL);
    for (i = 0; i < 8; i++) pane_add(u, u->nkids, k[i], 0, 0, 0);
    ui_set_size(k[6], fw, 0.002f, 0); ui_set_size(k[7], fw, 0.002f, 0);
    erp_layout(u);
    return u;
}
void *eng_EngRepairPane_Create(float fw, float fh, int teams) { return erp_new(fw, fh, teams); }
void *eng_new_EngRepairPane(float fw, float fh, int teams) { return erp_new(fw, fh, teams); }
void *eng_EngRepairPane_Cast(PyObject *o) { return as_c(ui_arg_ptr(o), CT_REPAIRPANE); }
void *eng_EngRepairPane_GetRepairPane(void) { return erp; }
void eng_EngRepairPane_ResizeToContents(void *p) { UI *u = as_c(p, CT_REPAIRPANE); if (u) erp_resize(u); }
void *eng_EngRepairPane_GetSubsystemButton(void *p, void *sub)
{
    UI *u = as_c(p, CT_REPAIRPANE); int w, k; unsigned id = sub ? ((TGObject *)sub)->id : 0; ErpX *x = erx(u);
    if (!u || !id) return NULL;
    for (w = 0; w < 3; w++) for (k = 0; k < x->n[w]; k++) if (x->lists[w][k] == id) { UI *a = erp_area(u, w); return a && k < a->nkids ? a->kids[k] : NULL; }
    return NULL;
}
