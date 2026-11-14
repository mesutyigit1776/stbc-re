/* Tactical interface displays (subclasses of STStylizedWindow): WeaponsDisplay 0054aea0.., RadarDisplay 00542310.., ShipDisplay 00546a50...
   The C++ constructors create the stylized window, then call Create in the matching Python module (Tactical.Interface.*) to build the content.
   Not done yet (marked): ship events, PeriodicUpdate/Update of the weapon charge colors, the health gauge callback of ShipDisplay.
   Other methods of these classes are still provisional stubs (re/gen_ui_stubs.py). */
#include "tg_ui.h"

extern void *eng_new_TGString(char *); extern void eng_delete_TGString(void *);

void prop_set_i(void *, const char *, long); long prop_get_i(void *, const char *);

/* callPythonModuleFunction(module, func, "(O)" or "(Off)", wrapper of w, ...): the result is ignored */
static void call_module(const char *module, const char *func, UI *w, int with_size, float a, float b)
{
    PyObject *m, *f, *self, *args, *r;
    m = PyImport_ImportModule((char *)module); if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)func); Py_DECREF(m); if (!f) { PyErr_Print(); PyErr_Clear(); return; }
    self = appc_pyobj(w, w->o.cls->name);                                   /* vtbl +0x20: the Python wrapper of this object */
    args = with_size ? Py_BuildValue("(Off)", self, a, b) : Py_BuildValue("(O)", self);
    Py_DECREF(self);
    r = args ? PyEval_CallObject(f, args) : NULL; Py_XDECREF(args); Py_DECREF(f);
    if (!r) { PyErr_Print(); PyErr_Clear(); } Py_XDECREF(r);
}
static const float *border_color(void) { return tg_global("g_kInterfaceBorderColor"); }       /* &DAT_00988588 */
static UI *as_cls(void *p, unsigned ct) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
static UI *first_child(UI *u) { return u && u->nkids ? u->kids[0] : NULL; }                     /* 0072ed60 */
static void set_not_batch(UI *p)                                                               /* TGPane::SetNotBatchChildPolys 0072ef00, with layout */
{ int i; if (!p) return; p->flags &= ~UF_BATCHCHILD; for (i = 0; i < p->nkids; i++) p->kids[i]->flags &= ~UF_USEPARENTBATCH; ui_layout(p); }

/* ---- RadarDisplay (type 0x8103) ---- */
#define CT_RADAR 0x8103
static UI *rd_new(float fw, float fh)                                                          /* 00542310 */
{
    UI *w = stw_new("RadarDisplay", "StylizedWindow", "Radar", NULL, 0, 0, NULL, 0, 0, 0, border_color());
    ui_set_size(w, fw, fh, 0);                                                                 /* 0073ec10 (w, h, 0) */
    call_module("Tactical.Interface.RadarDisplay", "Create", w, 0, 0, 0);
    return w;
}
void *eng_RadarDisplay_Create(float fw, float fh) { return rd_new(fw, fh); }
void *eng_new_RadarDisplay(float fw, float fh) { return rd_new(fw, fh); }
void *eng_RadarDisplay_Cast(PyObject *o) { return as_cls(ui_arg_ptr(o), CT_RADAR); }
void eng_RadarDisplay_ResizeUI(void *p) { UI *w = as_cls(p, CT_RADAR); if (w) call_module("Tactical.Interface.RadarDisplay", "ResizeUI", w, 0, 0, 0); }
void eng_RadarDisplay_RepositionUI(void *p) { UI *w = as_cls(p, CT_RADAR); if (w) call_module("Tactical.Interface.RadarDisplay", "RepositionUI", w, 0, 0, 0); }

/* ---- RadarScope (type 0x8104): a TGPane of the given size (00543670).
   Not done yet (marked): the handlers for ET_OBJECT_GROUP_CHANGED, ET_ENTERED_SET, ET_TORPEDO_ENTERED_SET, ET_EXITED_SET, ET_TORPEDO_EXITED_SET,
   ET_SET_PLAYER (RadarScope.GroupChangeHandler / SetObjectUpdate / SetPlayerHandler) and the blip lists. ---- */
static UI *rs_new(float fw, float fh)
{
    UI *u = pane_new("RadarScope", 0, 0); ui_set_size(u, fw, fh, 1);
    call_module("Tactical.Interface.RadarScope", "Create", u, 0, 0, 0);       /* RebuildUI 00543ac0: kill children, then the Python Create builds them */
    return u;
}
void *eng_RadarScope_Create(float fw, float fh) { return rs_new(fw, fh); }
void *eng_new_RadarScope(float fw, float fh) { return rs_new(fw, fh); }
void *eng_RadarScope_Cast(PyObject *o) { return as_cls(ui_arg_ptr(o), 0x8104); }

/* RadarScope::CreateShipIcon 00543b40: icon of the player's species from "ShipIcons" (ShipClass +0x2e0 = ship property, +0x20 species); 0x65 without a player */
void *eng_RadarScope_CreateShipIcon(void *p)
{
    extern void *eng_Game_GetPlayer(void *); extern void *eng_Game_GetCurrentGame(void); extern void *eng_ShipClass_GetShipProperty(void *);
    extern long eng_ShipProperty_GetSpecies(void *); extern void *eng_TGIcon_Create(char *, int, PyObject *);
    void *pl = eng_Game_GetPlayer(eng_Game_GetCurrentGame()), *sp = pl ? eng_ShipClass_GetShipProperty(pl) : NULL;
    return eng_TGIcon_Create("ShipIcons", pl ? (sp ? (int)eng_ShipProperty_GetSpecies(sp) : 0) : 0x65, NULL);
}

/* ---- RadarBlip (TGIcon with the object id at +0x50) and RadarScope::Update 00544200 ----
   Children of the scope (Tactical.Interface.RadarScope.Create): 0 ship icon, 1 radar ring, 2 bracket pane, 3 target bracket, 4 blip pane,
   5 phaser line pane, 6 background. Update walks the player's set: ships the sensors see (icon 0x19a, 0x1a4 when the ship script name
   contains "Asteroid"), torpedoes (0x1a4) and asteroid objects get a blip in the blip pane (blips are reused in order; extra ones are deleted).
   Position: the object in the player's frame (x right, y forward); radial scale r = d x 0.75 / 229 up to 229 units, beyond that
   0.75 + 0.25 x (1 - (range + 1) / d) with range = sensor range (GetEffectiveRange 00543bb0); radius = ship icon half width x 1.15 + r x
   (scope width - ship icon width x 1.15) / 2, direction (x, -y) on screen, around the scope center. The target bracket follows the target.
   Approximations (marked): blip colors (affiliation, alpha by height 1 - 0.4 x |z| / d), pixel snapping of the center and the bracket
   pane (FindBlipForObject brackets) are not done. TacticalControlWindow::Update 0050a200 calls it through the radar display each frame. */
typedef struct { unsigned id; float angle; } BlipX;
/* the turn of a ship blip: the arrow points where the object faces, seen from the player's heading (RadarBlip::Update 00543310) */
float tg_blip_angle(const void *node) { const UI *u = node; return u && u->ext && tg_is_type((TGObject *)&u->o, 0x8105) && u->icon_num != 0x1a4 ? ((const BlipX *)u->ext)->angle : 0.0f; }
typedef struct { UI *bracket; } ScopeX;
static UI *blip_new(const char *group, int n) { extern void *eng_TGIcon_Create(char *, int, PyObject *); UI *b = ui_new("RadarBlip"); UI *t = eng_TGIcon_Create((char *)group, n, NULL);
    b->icon_group = t->icon_group ? strdup(t->icon_group) : NULL; b->icon_num = n; b->w = t->w; b->h = t->h; b->flags |= UF_NOFOCUS; t->o.dead = 1; b->ext = calloc(1, sizeof(BlipX)); return b; }
static UI *as_blip(void *p) { return as_cls(p, 0x8105); }
void *eng_RadarBlip_Create(char *group, int n) { return blip_new(group, n); }
void eng_RadarBlip_SetShipID(void *p, long id) { UI *b = as_blip(p); if (b) ((BlipX *)b->ext)->id = (unsigned)id; }
long eng_RadarBlip_GetShipID(void *p) { UI *b = as_blip(p); return b ? ((BlipX *)b->ext)->id : 0; }
static ScopeX *scope_x(UI *s) { if (!s->ext) s->ext = calloc(1, sizeof(ScopeX)); return s->ext; }
void eng_RadarScope_SetTargetBracket(void *p, void *b) { UI *s = as_cls(p, 0x8104); if (s) scope_x(s)->bracket = as_blip(b); }
/* the colour of a blip: a torpedo of the player is the incoming-torpedo colour; otherwise the group of the object (friendly, enemy, neutral) or unknown */
const float *tg_affiliation_color(void *o);
static const float *radar_color(void *o) { return tg_affiliation_color(o); }
const float *tg_affiliation_color(void *o)
{
    extern PyObject *eng_Mission_GetFriendlyGroup(void *), *eng_Mission_GetEnemyGroup(void *), *eng_Mission_GetNeutralGroup(void *);
    extern void *eng_Game_GetCurrentGame(void); extern void *eng_Game_GetCurrentEpisode(void *); extern void *eng_Episode_GetCurrentMission(void *);
    extern long eng_ObjectGroup_IsNameInGroup(void *, char *); extern const char *eng_BaseObjectClass_GetName(void *); extern long eng_Torpedo_GetParentID(void *);
    extern void *eng_Game_GetCurrentPlayer(void);
    void *g = eng_Game_GetCurrentGame(), *ep = g ? eng_Game_GetCurrentEpisode(g) : NULL, *mis = ep ? eng_Episode_GetCurrentMission(ep) : NULL, *pl = eng_Game_GetCurrentPlayer();
    const char *n = eng_BaseObjectClass_GetName(o); PyObject *grp[3]; int i, hit = -1; TGObject *par = NULL;
    if (tg_is_type(o, 0x8009)) {                                                   /* a torpedo: the player's are yellow, the others take the group of the ship that fired them */
        par = tg_find((unsigned)eng_Torpedo_GetParentID(o));
        if (par == pl) return tg_global("g_kSTRadarIncomingTorpColor");
        if (par) n = eng_BaseObjectClass_GetName(par);
    }
    if (mis && n) {
        grp[0] = eng_Mission_GetFriendlyGroup(mis); grp[1] = eng_Mission_GetEnemyGroup(mis); grp[2] = eng_Mission_GetNeutralGroup(mis);
        for (i = 0; i < 3; i++) { void *gp = ui_arg_ptr(grp[i]); if (hit < 0 && gp && eng_ObjectGroup_IsNameInGroup(gp, (char *)n)) hit = i; Py_XDECREF(grp[i]); }
    }
    return tg_global(hit == 0 ? "g_kRadarFriendlyColor" : hit == 1 ? "g_kRadarEnemyColor" : hit == 2 ? "g_kRadarNeutralColor" : "g_kRadarUnknownColor");
}
void eng_RadarScope_Update(void *p)
{
    extern void *eng_Game_GetCurrentPlayer(void); extern void *eng_ShipClass_GetSensorSubsystem(void *), *eng_ShipClass_GetTarget(void *);
    extern long eng_SensorSubsystem_IsObjectVisible(void *, void *); extern double eng_SensorSubsystem_GetSensorRange(void *);
    extern void *eng_BaseObjectClass_GetWorldLocation(void *), *eng_BaseObjectClass_GetWorldRotation(void *);
    extern const char *eng_ShipClass_GetScript(void *); extern void eng_TGPane_DeleteChild(void *, void *, int);
    extern const char *eng_GraphicsModeInfo_GetLcarsString(void *); extern void *eng_GraphicsModeInfo_GetCurrentMode(void);
    int tg_space_set_objects(void *, void ***);
    UI *s = as_cls(p, 0x8104), *icon, *pane, *bracket; void *pl, *set, *sens, *tgt, **objs; float *pp, *rot, range, halfw, halfh, iw, ih; int n, i, used = 0;
    const char *grp;
    if (!s || s->nkids < 5 || !(pl = eng_Game_GetCurrentPlayer()) || !(set = eng_BaseObjectClass_GetContainingSet(pl))) return;
    icon = s->kids[0]; pane = s->kids[4]; bracket = scope_x(s)->bracket;
    if (bracket) bracket->flags &= ~UF_VISIBLE;                                              /* vslot 0xb4(0): hidden unless a target is drawn */
    if (!(sens = eng_ShipClass_GetSensorSubsystem(pl))) return;
    grp = eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode());
    range = (float)eng_SensorSubsystem_GetSensorRange(sens);
    tgt = eng_ShipClass_GetTarget(pl);
    iw = icon->w * 1.15f; ih = icon->h * 1.15f; halfw = (s->w - iw) * 0.5f; halfh = (s->h - ih) * 0.5f;
    pp = eng_BaseObjectClass_GetWorldLocation(pl); rot = eng_BaseObjectClass_GetWorldRotation(pl);
    if (!pp || !rot) { free(pp); free(rot); return; }
    for (n = tg_space_set_objects(set, &objs), i = 0; i < n; i++) {
        void *o = objs[i]; int num; const char *sc; float *op, d[3], l[3], dist, len, dx, dy, r, x, y; UI *b; int k;
        if (o == pl || !tg_is_ours(o) || ((TGObject *)o)->dead) continue;
        if (tg_is_type(o, 0x8008)) {
            if (!eng_SensorSubsystem_IsObjectVisible(sens, o)) continue;
            sc = eng_ShipClass_GetScript(o); num = sc && strstr(sc, "Asteroid") ? 0x1a4 : 0x19a;
        } else if (tg_is_type(o, 0x8009)) num = 0x1a4;
        else continue;                                                                       /* asteroid DamageableObjects: none outside ships yet */
        b = used < pane->nkids ? as_blip(pane->kids[used]) : NULL;
        if (!b) { b = blip_new(grp, num); pane_add(pane, pane->nkids, b, 0, 0, 0); }
        b->icon_num = num; ((BlipX *)b->ext)->id = ((TGObject *)o)->id; used++;
        if (!(op = eng_BaseObjectClass_GetWorldLocation(o))) continue;
        for (k = 0; k < 3; k++) d[k] = op[k] - pp[k];
        free(op);
        for (k = 0; k < 3; k++) l[k] = rot[0 * 3 + k] * d[0] + rot[1 * 3 + k] * d[1] + rot[2 * 3 + k] * d[2];   /* transposed rotation: into the player's frame */
        dist = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        len = sqrtf(l[0] * l[0] + l[1] * l[1]);
        if (len <= 1e-6f) dx = dy = 0; else { dx = l[0] / len; dy = -l[1] / len; }
        r = dist > 229.0f ? (1.0f - (range + 1.0f) / dist) * 0.25f + 0.75f : dist * 0.0032751092f;
        x = (iw * 0.5f + r * halfw) * dx + s->w * 0.5f; y = (ih * 0.5f + r * halfh) * dy + s->h * 0.5f;
        ui_set_pos(b, x - b->w * 0.5f, y - b->h * 0.5f, 0); b->flags |= UF_VISIBLE;
        { /* colour (FUN_00543c20, FUN_00436250): the group of the object, scaled by how far it is above or below the plane (1 - 0.4 x |z| / distance, Update 00544200) */
          extern void *eng_BaseObjectClass_GetWorldForwardTG(void *); float *fw = eng_BaseObjectClass_GetWorldForwardTG(o), lf[3], fa = fabsf(l[2]) / (dist > 1e-6f ? dist : 1.0f);
          const float *c = radar_color(o); int q;
          if (fa > 1.0f) fa = 1.0f; fa = 1.0f - fa * 0.4f;
          if (c) for (q = 0; q < 4; q++) b->color[q] = c[q] * fa;
          if (fw) { for (q = 0; q < 3; q++) lf[q] = rot[0 * 3 + q] * fw[0] + rot[1 * 3 + q] * fw[1] + rot[2 * 3 + q] * fw[2]; ((BlipX *)b->ext)->angle = atan2f(lf[0], lf[1]); free(fw); } }
        if (bracket && o == tgt) { ui_set_pos(bracket, x - bracket->w * 0.5f, y - bracket->h * 0.5f, 0); ((BlipX *)bracket->ext)->id = ((TGObject *)o)->id; bracket->flags |= UF_VISIBLE; }
    }
    free(pp); free(rot);
    while (pane->nkids > used) eng_TGPane_DeleteChild(pane, pane->kids[pane->nkids - 1], 0);
}
/* the radar of the tactical window, each frame (TacticalControlWindow::Update 0050a200 -> FUN_005425e0: the radar display's first child) */
void tg_tactical_tick(void)
{
    { extern void tg_tac_displays_tick(void); tg_tac_displays_tick(); }
    extern void *eng_TacticalControlWindow_GetTacticalControlWindow(void); extern PyObject *eng_TacticalControlWindow_GetRadarDisplay(void *);
    void *t = eng_TacticalControlWindow_GetTacticalControlWindow(); PyObject *r; UI *d;
    if (!t || !(r = eng_TacticalControlWindow_GetRadarDisplay(t))) return;
    d = fwd(ui_arg_ptr(r)); Py_DECREF(r);                                                  /* the display is a window: its children are in the sub pane */
    if (d && d->nkids) eng_RadarScope_Update(d->kids[0]);
}
