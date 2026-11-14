/* TacWeaponsCtrl (class 0x8106): the weapons control panel of the tactical window, a stylized window of the LeftSeparator style
   (TacWeaponsCtrl 00547b10, Init 00547e50, CreateTorpedoControls 00548120, CreatePhaserControls 00548a00, CreateTractorCloakControls
   00548e60, handlers 00549520..0054a380). It holds, one under the other, the torpedo section (label, count, type toggle, spread toggle),
   the phaser section (label, intensity toggle), a divider and the tractor and cloak toggles. The toggles act on the player's ship:
     type   - ApplyTorpedoTypeToggle 00549850: the next ammo type that has torpedoes, else the old one stays
     spread - ApplyTorpedoSpreadToggle 00549940: WeaponSystem::SetFiringChainMode
     phaser intensity - PhaserSettingChange 005497e0: state 0 sets power level 0 (Low), state 1 power level 2 (Full)
     tractor - ApplyOtherBeamToggle 005499a0: state 0 starts the beam at the target, state 1 stops it
     cloak - ApplyCloakToggle 00549a50: state 0 starts cloaking, state 1 stops it
   The refresh functions (RefreshTorpedoSettings 00549f30, RefreshPhaserSettings 00549ed0, RefreshTractorToggle 00549d70,
   RefreshCloakToggle 00549d20) put the state of the ship into the toggles.
   Approximations (marked): the exe refreshes on events and a timer; here the tractor and cloak toggles and the torpedo count are
   refreshed every frame. The layout of the sections (the exe's sizes are not read out of the decompile: it stacks label, toggles and
   divider with the gaps below) follows the manual's picture of the panel. */
#include "tg_ui.h"

extern UI *stw_new(const char *, const char *, const char *, void *, float, float, UI *, int, float, float, const float *);
extern void *eng_new_TGString(char *);
extern void *eng_TGParagraph_CreateW(void *, float, PyObject *, char *, float, int);
extern void *eng_TGIcon_Create(char *, int, PyObject *);
extern void eng_TGPane_DeleteChild(void *, void *, int);
extern void *eng_TGLocalizationManager_Load(void *, char *, int); extern void eng_TGLocalizationManager_Unload(void *, void *);
extern void *eng_TGLocalizationDatabase_GetString(void *, char *); extern long eng_TGLocalizationDatabase_HasString(void *, char *);
extern const char *eng_TGString_GetCString(void *); extern void eng_TGParagraph_SetString(void *, char *, int);
extern void *eng_Game_GetCurrentPlayer(void);
extern void *eng_ShipClass_GetTorpedoSystem(void *), *eng_ShipClass_GetPhaserSystem(void *), *eng_ShipClass_GetTractorBeamSystem(void *), *eng_ShipClass_GetCloakingSubsystem(void *);
extern void *eng_ShipClass_GetTarget(void *), *eng_ShipClass_GetTargetOffsetTG(void *);
extern long eng_TorpedoSystem_GetNumAmmoTypes(void *), eng_TorpedoSystem_GetNumAvailableTorpsToType(void *, int), eng_TorpedoSystem_GetCurrentAmmoTypeNumber(void *);
extern void eng_TorpedoSystem_SetAmmoType(void *, int, int); extern void *eng_TorpedoSystem_GetAmmoType(void *, int);
extern const char *eng_TorpedoAmmoType_GetAmmoName(void *);
extern long eng_WeaponSystem_GetNumFiringChains(void *), eng_WeaponSystem_GetFiringChainMode(void *); extern void eng_WeaponSystem_SetFiringChainMode(void *, int);
extern const char *eng_WeaponSystem_GetFiringChainName(void *, int);
extern long eng_PhaserSystem_GetPowerLevel(void *); extern void eng_PhaserSystem_SetPowerLevel(void *, int);
extern void eng_WeaponSystem_StartFiring(void *, void *, void *); extern void eng_WeaponSystem_StopFiring(void *);
extern long eng_ShipSubsystem_IsDisabled(void *);
extern long eng_CloakingSubsystem_IsTryingToCloak(void *); extern void eng_CloakingSubsystem_StartCloaking(void *), eng_CloakingSubsystem_StopCloaking(void *);
extern long eng_GraphicsModeInfo_GetHeight(void *), eng_GraphicsModeInfo_GetCurrentResolution(void *);
extern const char *eng_GraphicsModeInfo_GetLcarsString(void *), *eng_GraphicsModeInfo_GetLcarsModule(void *); extern void *eng_GraphicsModeInfo_GetCurrentMode(void);
extern void eng_TGFontManager_SetDefaultFont(void *, char *, float); extern void *eng_TGFontManager_GetDefaultFont(void *);
extern const char *eng_TGFontGroup_GetFontName(void *); extern double eng_TGFontGroup_GetFontSize(void *);
void tg_font_default_set(const char *, float);
int tg_tractor_status(void *system);                                                /* tg_ship.c: 0 off, 1 firing, 2 out of range */
UI *tg_toggle_new(void *caption, int def, int n, void **names, void **evs);
void tg_toggle_set_value(UI *b, int i, void *name, void *ev); void tg_toggle_set_caption(UI *b, void *caption);
void tg_button_colours(UI *b, const float *normal, const float *highlighted, const float *selected, const float *text);
long eng_STToggle_GetState(void *); void eng_STToggle_SetState(void *, int, int);

#define CT_TWC 0x8106
static UI *twc; static UI *toggle[5];     /* 0 phaser intensity, 1 torpedo type, 2 torpedo spread, 3 tractor beam, 4 cloak */
static UI *count_para; static int last_count = -1;
static UI *as_twc(void *p) { return p && tg_is_ours(p) && tg_is_type(p, CT_TWC) ? p : NULL; }

/* ---- helpers ---- */
static void *ship(void) { void *p = eng_Game_GetCurrentPlayer(); return p && tg_is_ours(p) && tg_is_type(p, 0x8008) ? p : NULL; }
static float lcars_value(const char *name, float def)
{
    PyObject *m = PyImport_ImportModule((char *)eng_GraphicsModeInfo_GetLcarsModule(eng_GraphicsModeInfo_GetCurrentMode())), *v; float r = def;
    if (!m) { PyErr_Clear(); return def; }
    v = PyObject_GetAttrString(m, (char *)name); Py_DECREF(m);
    if (v) { if (PyFloat_Check(v)) r = (float)PyFloat_AsDouble(v); else if (PyInt_Check(v)) r = (float)PyInt_AsLong(v); Py_DECREF(v); } else PyErr_Clear();
    return r;
}
/* the flight small font is the default font while the panel is built (HandleRepositionUI does the same around Init) */
static void *font_push(void)
{
    PyObject *m = PyImport_ImportModule("MainMenu.mainmenu"), *n, *l; void *old = eng_TGFontManager_GetDefaultFont(NULL); char *name = NULL; float size = 0; int res;
    if (!m) { PyErr_Clear(); return old; }
    n = PyObject_GetAttrString(m, "g_pcFlightSmallFont"); l = PyObject_GetAttrString(m, "g_kFlightSmallFontSize"); Py_DECREF(m);
    res = (int)eng_GraphicsModeInfo_GetCurrentResolution(eng_GraphicsModeInfo_GetCurrentMode());
    if (n && PyString_Check(n)) name = PyString_AsString(n);
    if (l && PyList_Check(l) && res >= 0 && res < PyList_Size(l)) { PyObject *e = PyList_GetItem(l, res); if (e && PyInt_Check(e)) size = (float)PyInt_AsLong(e); else if (e && PyFloat_Check(e)) size = (float)PyFloat_AsDouble(e); }
    if (name && size > 0) tg_font_default_set(name, size);
    Py_XDECREF(n); Py_XDECREF(l); PyErr_Clear();
    return old;
}
static void font_pop(void *old) { if (old) tg_font_default_set(eng_TGFontGroup_GetFontName(old), (float)eng_TGFontGroup_GetFontSize(old)); }
static void *tgl(void *db, const char *key) { void *s = db ? eng_TGLocalizationDatabase_GetString(db, (char *)key) : NULL; return s ? s : eng_new_TGString((char *)key); }
static TGEvent *toggle_event(const char *et)
{
    TGEvent *e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const(et); e->dest = twc; return e;
}
static UI *label_new(void *text)
{
    UI *p = eng_TGParagraph_CreateW(text, 1.0f, NULL, NULL, 0, 0); const float *c; c = tg_global("g_kTacWeaponsCtrlHeaderTextColor");
    if (c) memcpy(p->color, c, 16);
    return p;
}
static UI *toggle_style(UI *t, float w, float h)
{
    static const float menu3[4][4] = {{0.8118f, 0.3765f, 0.6235f, 1}, {0.4059f, 0.1882f, 0.3118f, 1}, {0.9647f, 0.5765f, 0.8f, 1}, {0.25f, 0.25f, 0.25f, 1}};
    static const float black[4] = {0, 0, 0, 1};
    tg_button_colours(t, menu3[0], menu3[2], menu3[1], black);
    ui_set_size(t, w, h, 0);
    return t;
}
static void kill_children(UI *w)
{
    UI *in = fwd(w); if (!in) return;
    while (in->nkids) eng_TGPane_DeleteChild(in, in->kids[in->nkids - 1], 0);
    memset(toggle, 0, sizeof toggle); count_para = NULL; last_count = -1;
}

/* ---- the sections ---- */
static int ammo_count(void *sys) { return sys ? (int)eng_TorpedoSystem_GetNumAvailableTorpsToType(sys, (int)eng_TorpedoSystem_GetCurrentAmmoTypeNumber(sys)) : 0; }
static UI *torpedo_controls(float W, float px, void *db, void *sub_db)
{
    void *sys = ship() ? eng_ShipClass_GetTorpedoSystem(ship()) : NULL; UI *pane = pane_new("TGPane", W, 0), *label, *count, *type, *spread; char buf[32];
    float h, indent = 0.003f; int n, i; void *names[4] = {0}, *evs[4] = {0};
    if (!sys) return pane;
    label = label_new(tgl(db, "Torpedoes")); pane_add(pane, pane->nkids, label, indent, 0, 0);
    snprintf(buf, sizeof buf, "%d", ammo_count(sys));
    count = eng_TGParagraph_CreateW(eng_new_TGString(buf), 1.0f, NULL, NULL, 0, 0); { const float *c = tg_global("g_kTacWeaponsCtrlHeaderTextColor"); if (c) memcpy(count->color, c, 16); }
    pane_add(pane, pane->nkids, count, W - indent - count->w, 0, 0); count_para = count;
    n = (int)eng_TorpedoSystem_GetNumAmmoTypes(sys); if (n > 4) n = 4; if (n < 1) n = 1;
    for (i = 0; i < n; i++) {
        void *am = eng_TorpedoSystem_GetAmmoType(sys, i); const char *nm = am ? eng_TorpedoAmmoType_GetAmmoName(am) : NULL;
        names[i] = nm && sub_db && eng_TGLocalizationDatabase_HasString(sub_db, (char *)nm) ? eng_TGLocalizationDatabase_GetString(sub_db, (char *)nm) : eng_new_TGString((char *)(nm ? nm : ""));
        evs[i] = toggle_event("ET_TORP_TYPE_TOGGLE_CLICKED");
    }
    h = label->h + 2 * px;
    type = tg_toggle_new(tgl(db, "Type:"), (int)eng_TorpedoSystem_GetCurrentAmmoTypeNumber(sys), n, names, evs);
    toggle_style(type, W - 2 * indent, h); pane_add(pane, pane->nkids, type, indent, label->h + px, 0); toggle[1] = type;
    n = (int)eng_WeaponSystem_GetNumFiringChains(sys); if (n > 4) n = 4; if (n < 1) n = 1;
    memset(names, 0, sizeof names);
    for (i = 0; i < n; i++) {
        const char *nm = eng_WeaponSystem_GetFiringChainName(sys, i);
        names[i] = nm && sub_db && eng_TGLocalizationDatabase_HasString(sub_db, (char *)nm) ? eng_TGLocalizationDatabase_GetString(sub_db, (char *)nm) : eng_new_TGString((char *)(nm ? nm : ""));
        evs[i] = toggle_event("ET_TORP_SPREAD_TOGGLE_CLICKED");
    }
    spread = tg_toggle_new(tgl(db, "Spread:"), (int)eng_WeaponSystem_GetFiringChainMode(sys), n, names, evs);
    toggle_style(spread, W - 2 * indent, h); pane_add(pane, pane->nkids, spread, indent, label->h + px + h + px, 0); toggle[2] = spread;
    if (eng_WeaponSystem_GetNumFiringChains(sys) < 2) ui_hide(spread, 0);
    ui_set_size(pane, W, label->h + px + h + px + (spread->flags & UF_VISIBLE ? h + px : 0), 0);
    return pane;
}
static UI *phaser_controls(float W, float px, void *db)
{
    void *sys = ship() ? eng_ShipClass_GetPhaserSystem(ship()) : NULL; UI *pane = pane_new("TGPane", W, 0), *label, *tg; float indent = 0.003f, h;
    void *names[2], *evs[2];
    if (!sys) return pane;
    label = label_new(tgl(db, "Phasers")); pane_add(pane, pane->nkids, label, indent, 0, 0);
    names[0] = tgl(db, "Low"); names[1] = tgl(db, "Full"); evs[0] = toggle_event("ET_PHASER_INTENSITY_TOGGLE_CLICKED"); evs[1] = toggle_event("ET_PHASER_INTENSITY_TOGGLE_CLICKED");
    h = label->h + 2 * px;
    tg = tg_toggle_new(tgl(db, "Intensity:"), eng_PhaserSystem_GetPowerLevel(sys) != 0, 2, names, evs);
    toggle_style(tg, W - 2 * indent, h); pane_add(pane, pane->nkids, tg, indent, label->h + px, 0); toggle[0] = tg;
    ui_set_size(pane, W, label->h + px + h + px, 0);
    return pane;
}
static UI *beam_controls(float W, float px, void *db)
{
    void *ship_ = ship(), *tractor = ship_ ? eng_ShipClass_GetTractorBeamSystem(ship_) : NULL, *cloak = ship_ ? eng_ShipClass_GetCloakingSubsystem(ship_) : NULL;
    UI *pane = pane_new("TGPane", W, 0), *tg; float indent = 0.003f, h = 0, y = 0; void *names[2], *evs[2];
    const float *g = tg_global("g_kTacWeaponsCtrlHeaderTextColor"); (void)g;
    if (tractor) {
        names[0] = tgl(db, "On"); names[1] = tgl(db, "Off"); evs[0] = toggle_event("ET_OTHER_BEAM_TOGGLE_CLICKED"); evs[1] = toggle_event("ET_OTHER_BEAM_TOGGLE_CLICKED");
        tg = tg_toggle_new(tgl(db, "Tractor"), 1, 2, names, evs); h = 16.0f * px;
        toggle_style(tg, W - 2 * indent, h); pane_add(pane, pane->nkids, tg, indent, y, 0); toggle[3] = tg; y += h + px;
    }
    if (cloak) {
        names[0] = tgl(db, "On"); names[1] = tgl(db, "Off"); evs[0] = toggle_event("ET_OTHER_CLOAK_TOGGLE_CLICKED"); evs[1] = toggle_event("ET_OTHER_CLOAK_TOGGLE_CLICKED");
        tg = tg_toggle_new(tgl(db, "Cloak"), 1, 2, names, evs); if (h == 0) h = 16.0f * px;
        toggle_style(tg, W - 2 * indent, h); pane_add(pane, pane->nkids, tg, indent, y, 0); toggle[4] = tg; y += h + px;
    }
    ui_set_size(pane, W, y, 0);
    return pane;
}
/* TacWeaponsCtrl::Init 00547e50 */
static void wc_refresh_all(void);
static void wc_init(UI *win)
{
    float W = lcars_value("WEAPONS_CTRL_PANE_WIDTH", 0.1f), px = 1.0f / (float)eng_GraphicsModeInfo_GetHeight(NULL), y = 0, indent = 0.003f;
    UI *torp, *ph, *div, *beam; void *db, *sub_db, *old;
    old = font_push();
    kill_children(win);
    db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Bridge Menus.tgl", 1); sub_db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Subsystems.tgl", 1);
    torp = torpedo_controls(W, px, db, sub_db); pane_add(fwd(win), fwd(win)->nkids, torp, 0, 0, 0);
    if (torp->h > 0) y = torp->h + 2 * px;
    ph = phaser_controls(W, px, db); pane_add(fwd(win), fwd(win)->nkids, ph, 0, y, 0);
    if (ph->h > 0) y += ph->h;
    div = eng_TGIcon_Create((char *)eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode()), 200, NULL);
    { const float *c = tg_global("g_kLeftSeparatorColor"); if (c) memcpy(div->color, c, 16); }
    ui_set_size(div, W - 2 * indent, 2 * px, 0); pane_add(fwd(win), fwd(win)->nkids, div, indent, y, 0);
    y += div->h + px;
    beam = beam_controls(W, px, db); pane_add(fwd(win), fwd(win)->nkids, beam, 0, y, 0);
    if (beam->h <= 0) ui_hide(div, 0);
    if (db) eng_TGLocalizationManager_Unload(NULL, db); if (sub_db) eng_TGLocalizationManager_Unload(NULL, sub_db);
    font_pop(old);
    wc_refresh_all();
    { void *sh = fwd(win) ? NULL : NULL; (void)sh; }
}

/* ---- refresh functions ---- */
void eng_TacWeaponsCtrl_RefreshPhaserSettings(void *p)                          /* 00549ed0 */
{
    void *sys = ship() ? eng_ShipClass_GetPhaserSystem(ship()) : NULL; long lvl;
    if (!sys || !toggle[0]) return;
    lvl = eng_PhaserSystem_GetPowerLevel(sys);
    if (lvl == 0) eng_STToggle_SetState(toggle[0], 0, 1); else if (lvl > 0 && lvl < 3) eng_STToggle_SetState(toggle[0], 1, 1);
}
void eng_TacWeaponsCtrl_RefreshTorpedoSettings(void *p)                         /* 00549f30 */
{
    void *sys = ship() ? eng_ShipClass_GetTorpedoSystem(ship()) : NULL;
    if (!sys || !toggle[1]) return;
    eng_STToggle_SetState(toggle[1], (int)eng_TorpedoSystem_GetCurrentAmmoTypeNumber(sys), 1);
    if (toggle[2]) eng_STToggle_SetState(toggle[2], (int)eng_WeaponSystem_GetFiringChainMode(sys), 1);
}
void eng_TacWeaponsCtrl_RefreshCloakToggle(void *p)                             /* 00549d20 */
{
    void *cl = ship() ? eng_ShipClass_GetCloakingSubsystem(ship()) : NULL;
    if (!cl || !toggle[4]) return;
    eng_STToggle_SetState(toggle[4], eng_CloakingSubsystem_IsTryingToCloak(cl) ? 0 : 1, 1);
}
void eng_TacWeaponsCtrl_RefreshTractorToggle(void *p)                           /* 00549d70 */
{
    void *sh = ship(), *sys = sh ? eng_ShipClass_GetTractorBeamSystem(sh) : NULL, *tgt; int st;
    void *db; UI *t = toggle[3];
    if (!sys || !t) return;
    tgt = eng_ShipClass_GetTarget(sh); st = tg_tractor_status(sys);
    db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Bridge Menus.tgl", 1);
    if (st == 1 && tgt) { eng_STToggle_SetState(t, 0, 1); tg_toggle_set_value(t, 0, tgl(db, "Firing"), NULL); }
    else if (st == 2) { eng_STToggle_SetState(t, 0, 1); tg_toggle_set_value(t, 0, tgl(db, "Out of Range"), NULL); }
    else { tg_toggle_set_value(t, 0, tgl(db, "On"), NULL); eng_STToggle_SetState(t, 1, 1); }
    if (db) eng_TGLocalizationManager_Unload(NULL, db);
    if (!tgt || eng_ShipSubsystem_IsDisabled(sys) == 1) t->flags &= ~UF_ENABLED; else t->flags |= UF_ENABLED;
}
static void wc_refresh_all(void)
{
    eng_TacWeaponsCtrl_RefreshPhaserSettings(twc); eng_TacWeaponsCtrl_RefreshTorpedoSettings(twc);
    eng_TacWeaponsCtrl_RefreshCloakToggle(twc); eng_TacWeaponsCtrl_RefreshTractorToggle(twc);
}

/* ---- the actions of the toggles ---- */
static void on_type(TGEvent *e)                                                 /* ApplyTorpedoTypeToggle 00549850 */
{
    void *sys = ship() ? eng_ShipClass_GetTorpedoSystem(ship()) : NULL; int n, cur, prev, avail;
    if (!sys || !toggle[1]) return;
    n = (int)eng_TorpedoSystem_GetNumAmmoTypes(sys); if (n <= 0) return;
    cur = (int)eng_STToggle_GetState(toggle[1]); prev = cur - 1; if (prev < 0) prev = n - 1;
    if (cur == prev) { eng_STToggle_SetState(toggle[1], prev, 1); return; }
    avail = (int)eng_TorpedoSystem_GetNumAvailableTorpsToType(sys, cur);
    while (avail < 1) {
        cur = (cur + 1) % n;
        if (cur == prev) { eng_STToggle_SetState(toggle[1], prev, 1); return; }
        avail = (int)eng_TorpedoSystem_GetNumAvailableTorpsToType(sys, cur);
    }
    eng_TorpedoSystem_SetAmmoType(sys, cur, 1); eng_STToggle_SetState(toggle[1], cur, 1);
}
static void on_spread(TGEvent *e) { void *sys = ship() ? eng_ShipClass_GetTorpedoSystem(ship()) : NULL; if (sys && toggle[2]) eng_WeaponSystem_SetFiringChainMode(sys, (int)eng_STToggle_GetState(toggle[2])); }   /* 00549940 */
static void on_intensity(TGEvent *e)                                            /* PhaserSettingChange 005497e0 */
{
    void *sys = ship() ? eng_ShipClass_GetPhaserSystem(ship()) : NULL; long st; if (!sys || !toggle[0]) return;
    st = eng_STToggle_GetState(toggle[0]); if (st == 0) eng_PhaserSystem_SetPowerLevel(sys, 0); else if (st == 1) eng_PhaserSystem_SetPowerLevel(sys, 2);
}
static void on_beam(TGEvent *e)                                                 /* ApplyOtherBeamToggle 005499a0 */
{
    void *sh = ship(), *sys = sh ? eng_ShipClass_GetTractorBeamSystem(sh) : NULL, *tgt;
    if (!sys || !toggle[3]) return;
    if (eng_STToggle_GetState(toggle[3]) != 0) { eng_WeaponSystem_StopFiring(sys); return; }
    tgt = eng_ShipClass_GetTarget(sh);
    if (tgt) { void *off = eng_ShipClass_GetTargetOffsetTG(sh); eng_WeaponSystem_StartFiring(sys, tgt, off); free(off); return; }
    eng_STToggle_SetState(toggle[3], 1, 1);
}
static void on_cloak(TGEvent *e)                                                /* ApplyCloakToggle 00549a50 */
{
    void *cl = ship() ? eng_ShipClass_GetCloakingSubsystem(ship()) : NULL; if (!cl || !toggle[4]) return;
    if (eng_STToggle_GetState(toggle[4]) == 0) eng_CloakingSubsystem_StartCloaking(cl); else eng_CloakingSubsystem_StopCloaking(cl);
}
static void on_player(TGEvent *e) { if (twc && !twc->o.dead) wc_init(twc); }  /* HandlePlayerChanged 0054a120 */

/* ---- the window ---- */
static UI *wc_new(float w, float h)
{
    UI *u = stw_new("TacWeaponsCtrl", "StylizedWindow", "LeftSeparator", NULL, 0, 0, NULL, 0, 0, 0, tg_global("g_kInterfaceBorderColor"));
    twc = u; ui_set_size(u, w, h, 0);
    tg_register_native(u, (int)tg_const("ET_TORP_TYPE_TOGGLE_CLICKED"), on_type);
    tg_register_native(u, (int)tg_const("ET_TORP_SPREAD_TOGGLE_CLICKED"), on_spread);
    tg_register_native(u, (int)tg_const("ET_PHASER_INTENSITY_TOGGLE_CLICKED"), on_intensity);
    tg_register_native(u, (int)tg_const("ET_OTHER_BEAM_TOGGLE_CLICKED"), on_beam);
    tg_register_native(u, (int)tg_const("ET_OTHER_CLOAK_TOGGLE_CLICKED"), on_cloak);
    { static int once; if (!once) { once = 1; tg_register_native_broadcast((int)tg_const("ET_SET_PLAYER"), on_player); } }
    wc_init(u);
    return u;
}
void *eng_TacWeaponsCtrl_Create(float w, float h) { return wc_new(w, h); }
void *eng_new_TacWeaponsCtrl(float w, float h) { return wc_new(w, h); }
void eng_delete_TacWeaponsCtrl(void *p) { if (twc == p) twc = NULL; }
void *eng_TacWeaponsCtrl_GetTacWeaponsCtrl(void) { return twc && !twc->o.dead ? twc : NULL; }
void *eng_TacWeaponsCtrl_Cast(PyObject *o) { return as_twc(ui_arg_ptr(o)); }
void *eng_TacWeaponsCtrl_GetPhaserIntensityToggle(void *p) { return toggle[0]; }
void *eng_TacWeaponsCtrl_GetTorpTypeToggle(void *p) { return toggle[1]; }
void *eng_TacWeaponsCtrl_GetTorpSpreadToggle(void *p) { return toggle[2]; }
void *eng_TacWeaponsCtrl_GetBeamToggle(void *p) { return toggle[3]; }
void *eng_TacWeaponsCtrl_GetCloakToggle(void *p) { return toggle[4]; }
void eng_TacWeaponsCtrl_Init(void *p) { UI *u = as_twc(p); if (u) wc_init(u); }

/* each frame: the torpedo count, the tractor and the cloak toggles follow the ship */
void tg_tac_ctrl_tick(void)
{
    void *sys; int n;
    if (!twc || twc->o.dead) return;
    sys = ship() ? eng_ShipClass_GetTorpedoSystem(ship()) : NULL;
    if (sys && count_para) { n = ammo_count(sys); if (n != last_count) { char buf[32]; snprintf(buf, sizeof buf, "%d", n); eng_TGParagraph_SetString(count_para, buf, 1); last_count = n; { UI *pane = count_para->parent; if (pane) ui_set_pos(count_para, pane->w - 0.003f - count_para->w, count_para->y, 0); } } }
    eng_TacWeaponsCtrl_RefreshTractorToggle(twc); eng_TacWeaponsCtrl_RefreshCloakToggle(twc);
}
