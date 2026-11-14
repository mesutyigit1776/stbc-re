/* ST widgets: STSubPane, STMenu, STTopLevelMenu, STButton, STRoundedButton, STToggle, STFillGauge, STNumericBar, TacticalControlWindow.
   Behavior from stbc.exe: STSubPane 00532f20.., STMenu 005255b0.., STButton 00518be0.. (flags below are the bits at +0x28).
   STMenu flags: 0x1000000 use end cap space, 0x2000000 chosen, 0x4000000 opened, 0x8000000 clicked once, 0x10000000 auto choose,
   0x20000000 closeable, 0x40000000 openable, 0x80000000 two clicks. STButton: 0x4000000 use end cap space, 0x8000000 choosable,
   0x10000000 auto choose, 0x20000000 chosen. STSubPane: 0x80000000 radio group.
   Approximations (marked): layout metrics (the title bar and arrow icons are not built), colors and sounds are ignored. */
#include "tg_ui.h"

extern void *eng_new_TGString(char *); extern void eng_delete_TGString(void *);
extern const char *eng_TGString_GetCString(void *);
extern long eng_TGString_Compare(void *, void *, int);

#define MF_ENDCAP 0x1000000u
#define MF_CHOSEN 0x2000000u
#define MF_OPENED 0x4000000u
#define MF_CLICKED_ONCE 0x8000000u
#define MF_AUTOCHOOSE 0x10000000u
#define MF_CLOSEABLE 0x20000000u
#define MF_OPENABLE 0x40000000u
#define MF_TWOCLICKS 0x80000000u
#define BF_ENDCAPSPACE 0x4000000u
#define BF_CHOOSABLE 0x8000000u
#define BF_AUTOCHOOSE 0x10000000u
#define BF_CHOSEN 0x20000000u
#define SP_RADIO 0x80000000u
#define CT_STMENU 0x80e3
#define CT_STBUTTON 0x80ec
#define CT_STSUBPANE 0x80ea
#define CT_STTOGGLE 0x80ef
#define CT_TOPMENU 0x80e4

typedef struct { float spacing; int expand; } SubX;
typedef struct { void *act_ev, *update_ev; UI *owner; } MenuX;

static UI *as_cls(void *p, unsigned ct) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
static PyObject *none(void);
static PyObject *obj_of(UI *u) { return u ? appc_pyobj(u, u->o.cls->name) : (Py_INCREF(Py_None), Py_None); }
static PyObject *none(void) { Py_INCREF(Py_None); return Py_None; }
static int name_is(UI *u, void *s) { return u && u->text && s && eng_TGString_Compare(s, u->text, 1) == 0; }     /* case-insensitive (flag 1) */

/* ---------------------------------------------- STSubPane: a vertical stack ---------------------------------------------- */
static UI *sub_new(float w, float h, int expand)
{
    UI *u = pane_new("STSubPane", w, h); SubX *x = calloc(1, sizeof *x); x->spacing = 0.003f; x->expand = expand != 0; u->ext = x; return u;
}
extern void tg_ui_size_of(const void *node, float *w, float *h);
static float kid_h(const UI *c) { float w, h; tg_ui_size_of(c, &w, &h); return h; }
/* an STMenu is a title bar (as high as a stock button) and, when opened, its sub pane under it (approximation: the bar height) */
#define MENU_TITLE_H 0.0416667f
int tg_menu_untitled(const void *node);
extern int tg_text_extent(void *fg, const char *s, float wrap_px, float *w, float *h);
extern const char *eng_TGFontGroup_GetFontName(void *); extern double eng_TGFontGroup_GetFontSize(void *);
/* The title bar of an STMenu is as high as its text plus two gaps of 0.003 (STMenu::Layout 00526120). The main menu keeps its stock bar height (its
   scripts size the bars; approximation), the small flight fonts of the tactical windows (Crillee 5 to 9) use the text. */
float tg_menu_title_h(const void *node)
{
    const UI *m = node; float w, h;
    if (m && m->font && eng_TGFontGroup_GetFontSize(m->font) <= 9.0 && tg_text_extent(m->font, "Ag", 0, &w, &h)) return 0.006f + h / ui_scr_h();
    return MENU_TITLE_H;
}
int tg_is_menu(const void *node) { const UI *m = node; return m && tg_is_ours((void *)m) && tg_is_type((TGObject *)&m->o, CT_STMENU); }     /* STMenu and the menus derived from it */
int tg_menu_opened(const void *node) { const UI *m = node; return m && tg_is_type((TGObject *)&m->o, CT_STMENU) && ((m->flags & MF_OPENED) != 0 || tg_menu_untitled(m)); }
float tg_menu_height(const void *node)
{
    const UI *m = node, *sub = m && m->nkids ? m->kids[0] : NULL;
    return (tg_menu_untitled(m) ? 0.0f : tg_menu_title_h(m)) + (tg_menu_opened(m) && sub ? sub->h : 0.0f);
}     /* a button with no script height has its draw-time height */
static void sub_layout_children(UI *u)                                  /* 005335f0: visible children from the head, stacked downwards with the spacing */
{
    SubX *x = u->ext; float y = 0; int i;
    for (i = 0; i < u->nkids; i++) { UI *c = u->kids[i]; if (c->flags & UF_VISIBLE) { ui_set_pos(c, 0, y, 0); y = kid_h(c) + c->y + x->spacing; } }
}
void *eng_STSubPane_Create(float w, float h, int expand) { return sub_new(w, h, expand); }
void *eng_STSubPane_Cast(PyObject *o) { return as_cls(ui_arg_ptr(o), CT_STSUBPANE); }
static UI *containing_window(UI *u) { for (u = u->parent; u; u = u->parent) if (tg_is_type(&u->o, 0x80f7)) return u; return NULL; }   /* STSubPane::GetContainingWindow 00533c40 */
extern void eng_STStylizedWindow_InteriorChangedSize(void *, int);
extern void *eng_STStylizedWindow_GetInteriorPane(void *);
extern void eng_STStylizedWindow_SetFixedSize(void *, float, float, int);
extern void eng_STStylizedWindow_SetTitleBarThickness(void *, float, int);
static int tg_menu_busy;
void eng_STSubPane_ResizeToContents(void *p, int layout)                /* 00533d00 (simplified: the pane takes the height of its stack) */
{
    UI *u = as_cls(p, CT_STSUBPANE); float h = 0; int i; UI *win, *par;
    if (!u) return;
    sub_layout_children(u);
    for (i = 0; i < u->nkids; i++) if (u->kids[i]->flags & UF_VISIBLE) { float b = u->kids[i]->y + kid_h(u->kids[i]); if (b > h) h = b; }
    win = containing_window(u);
    if (win && ((SubX *)u->ext)->expand) {                              /* an expanding stack takes the width of the window's inside */
        extern double eng_STStylizedWindow_GetMaximumInteriorWidth(void *);
        float w = (float)eng_STStylizedWindow_GetMaximumInteriorWidth(win);
        if (w > 0 && w != u->w) {
            ui_set_size(u, w, h, 0);
            if (u->parent && tg_is_type(&u->parent->o, CT_STMENU) && u->parent->w != w) ui_set_size(u->parent, w, u->parent->h, 0);
        }
    }
    ui_set_size(u, u->w, h, 0);
    par = u->parent;                                                  /* the menu around the pane grows with it, and the stack that holds the menu */
    if (par && par != win && tg_is_type(&par->o, CT_STMENU) && !tg_menu_busy) {
        float mh = tg_menu_height(par);
        if (mh != par->h) {
            tg_menu_busy = 1; ui_set_size(par, par->w, mh, 0); tg_menu_busy = 0;
            if (par->parent && par->parent != win && tg_is_type(&par->parent->o, CT_STSUBPANE)) eng_STSubPane_ResizeToContents(par->parent, 0);
        }
    }
    if (win) eng_STStylizedWindow_InteriorChangedSize(win, 0);
    if (layout) ui_layout(win ? win : u);
}
void eng_STSubPane_InsertChild(void *p, int idx, void *c, float x, float y, int layout)
{ UI *u = as_cls(p, CT_STSUBPANE), *cc = as_ui(c); if (u && cc) { pane_add(u, idx < 0 ? idx + u->nkids : idx, cc, x, y, 0); eng_STSubPane_ResizeToContents(u, layout); } }
void eng_STSubPane_RemoveChild(void *p, void *c, int layout)
{ extern void eng_TGPane_RemoveChild(void *, void *, int); UI *u = as_cls(p, CT_STSUBPANE); if (u) { eng_TGPane_RemoveChild(u, c, 0); eng_STSubPane_ResizeToContents(u, layout); } }
void eng_STSubPane_DeleteChild(void *p, void *c, int layout)
{ extern void eng_TGPane_DeleteChild(void *, void *, int); UI *u = as_cls(p, CT_STSUBPANE); if (u) { eng_TGPane_DeleteChild(u, c, 0); eng_STSubPane_ResizeToContents(u, layout); } }
double eng_STSubPane_GetVertSpacing(void *p) { UI *u = as_cls(p, CT_STSUBPANE); return u ? ((SubX *)u->ext)->spacing : 0.0; }
void eng_STSubPane_SetRadioGroup(void *p, int v) { UI *u = as_cls(p, CT_STSUBPANE); if (u) { if (v) u->flags |= SP_RADIO; else u->flags &= ~SP_RADIO; } }
long eng_STSubPane_IsRadioGroup(void *p) { UI *u = as_cls(p, CT_STSUBPANE); return u && (u->flags & SP_RADIO) != 0; }
void eng_STSubPane_SetExpandToFillParent(void *p, int v) { UI *u = as_cls(p, CT_STSUBPANE); if (u) ((SubX *)u->ext)->expand = v != 0; }
double eng_STSubPane_GetTotalHeightOfChildren(void *p)                  /* 00533640 */
{
    UI *u = as_cls(p, CT_STSUBPANE); double h = 0.006; int i; if (!u) return 0;
    for (i = 0; i < u->nkids; i++) if (u->kids[i]->flags & UF_VISIBLE) h += kid_h(u->kids[i]) + ((SubX *)u->ext)->spacing;
    return h;
}
void *eng_STSubPane_GetContainingWindow(void *p) { UI *u = as_cls(p, CT_STSUBPANE); return u ? containing_window(u) : NULL; }
/* item lookups by name (005337c0, 00533890, 00533b30): menus and buttons are matched on their name, case-insensitively */
static void *sub_find(UI *u, void *name, int want_menu)
{
    int i;
    for (i = 0; i < u->nkids; i++) {
        UI *c = u->kids[i];
        if (want_menu && tg_is_type(&c->o, CT_STMENU) && name_is(c, name)) return c;
        if (!want_menu && tg_is_type(&c->o, CT_STBUTTON) && name_is(c, name)) return c;
        if (!want_menu && tg_is_type(&c->o, CT_STTOGGLE)) { void *cap = c->text; if (cap && name && eng_TGString_Compare(name, cap, 1) == 0) return c; }
    }
    return NULL;
}
void *eng_STSubPane_GetSubmenuW(void *p, void *name) { UI *u = as_cls(p, CT_STSUBPANE); return u ? sub_find(u, name, 1) : NULL; }
PyObject *eng_STSubPane_GetButtonW(void *p, void *name) { UI *u = as_cls(p, CT_STSUBPANE); UI *c = u ? sub_find(u, name, 0) : NULL; return c ? appc_from_ptr(c, c->o.cls->name) : none(); }   /* App.py wraps the pointer */
void eng_STSubPane_RemoveItemW(void *p, void *name)
{
    extern void eng_TGPane_DeleteChild(void *, void *, int); UI *u = as_cls(p, CT_STSUBPANE), *c; if (!u) return;
    c = sub_find(u, name, 1); if (!c) c = sub_find(u, name, 0);
    if (c) eng_STSubPane_DeleteChild(u, c, 1);
}
void *eng_STSubPane_GetSubmenu(void *p, char *n) { void *s = eng_new_TGString(n); void *r = eng_STSubPane_GetSubmenuW(p, s); return r; }
PyObject *eng_STSubPane_GetButton(void *p, char *n) { void *s = eng_new_TGString(n); PyObject *r = eng_STSubPane_GetButtonW(p, s); eng_delete_TGString(s); return r; }
void eng_STSubPane_RemoveItem(void *p, char *n) { void *s = eng_new_TGString(n); eng_STSubPane_RemoveItemW(p, s); }

/* ---------------------------------------------- STMenu ---------------------------------------------- */
/* A menu that gets a width gives it to its sub pane, and a sub pane that expands to fill its parent gives it to the menus and buttons in it
   (STSubPane::SetExpandToFillParent). Approximation: done when the menu is resized, not on every layout. */
void tg_menu_resized(UI *m)
{
    UI *sub; SubX *x; int i;
    if (!m->ext || m->nkids < 1 || !tg_is_type(&m->o, CT_STMENU)) return;
    sub = m->kids[0]; if (!sub || !tg_is_type(&sub->o, CT_STSUBPANE) || m->w <= 0) return;
    if (sub->w != m->w) sub->w = m->w;
    x = sub->ext;
    if (x && x->expand)
        for (i = 0; i < sub->nkids; i++) { UI *c = sub->kids[i]; if ((tg_is_type(&c->o, CT_STMENU) || tg_is_type(&c->o, CT_STBUTTON)) && c->w != m->w) { c->w = m->w; tg_menu_resized(c); } }
    { extern void eng_STSubPane_ResizeToContents(void *, int); static int busy; if (!busy) { busy = 1; eng_STSubPane_ResizeToContents(sub, 0); busy = 0; } }   /* the stack under the new width */
}
static UI *menu_new(const char *cls, void *name, unsigned flags)
{
    UI *sub = sub_new(0, 0, 1), *m = window_new(cls, 0, 0, 0, 0, sub); MenuX *x = calloc(1, sizeof *x);
    m->ext = x; m->text = name ? tg_str_copy(name) : eng_new_TGString("");
    m->flags = (m->flags & 0x0fffffffu) | (flags & 0xf0000000u) | 0x1000 | UF_ENABLED;
    return m;
}
static UI *as_menu(void *p) { return as_cls(p, CT_STMENU); }
void *eng_STMenu_CreateW(void *name, int flags, PyObject *ev) { UI *m = menu_new("STMenu", name, (unsigned)flags); MenuX *x = m->ext; x->act_ev = ui_arg_ptr(ev); return m; }
void *eng_STMenu_Create(char *name, int flags, PyObject *ev) { void *s = eng_new_TGString(name ? name : ""); return eng_STMenu_CreateW(s, flags, ev); }
void *eng_STMenu_CreateNull(void) { return NULL; }
void *eng_STMenu_Cast(PyObject *o) { return as_menu(ui_arg_ptr(o)); }
static UI *menu_sub(UI *m) { return m->nkids ? m->kids[0] : NULL; }
/* The colours of a menu's title bar (STMenu::Update 00525c90, FUN_005273f0, table at 008e28ec..): the scheme comes from the depth of the menu
   (the number of menus around it, 0 to 3: g_kSTMenu1.. to g_kSTMenu4..). Disabled: the disabled colour. Enabled: the normal base colour; when the
   cursor is on it the highlighted base (the opened and highlighted base when it is open); selected (the focus): the selected colour. Text:
   black, white when highlighted (schemes 1 and 2). Returns 1 and fills body and text. */
int tg_menu_colours(const void *node, float *body, float *text)
{
    const UI *m = node; int depth = 0, sch, st; const UI *p; char nm[48]; extern void *tg_global(const char *); const float *c;
    static const char *part[5] = {"NormalBase", "HighlightedBase", "Disabled", "OpenedHighlightedBase", "Selected"};
    unsigned fl;
    if (!m || !tg_is_type((TGObject *)&m->o, CT_STMENU)) return 0;
    for (p = m->parent; p; p = p->parent) { if (p->flags & UF_SKIPPARENT) continue; if (tg_is_type((TGObject *)&p->o, CT_STMENU)) depth++; }
    if ((m->flags & MF_CHOSEN) && --depth < 0) depth = 0;
    if (depth > 3) depth = 3;
    sch = depth + 1; fl = m->flags;
    st = !(fl & UF_ENABLED) ? 2 : (fl & UF_SELECTED) ? 4 : (fl & UF_HIGHLIGHTED) ? ((fl & MF_OPENED) ? 3 : 1) : 0;
    snprintf(nm, sizeof nm, "g_kSTMenu%d%s", sch, part[st]);
    c = tg_global(nm); if (!c) return 0;
    memcpy(body, c, 16);
    { int white = (st == 1 || st == 3 || st == 4) && sch <= 2; if (st == 4) white = 1; text[0] = text[1] = text[2] = white ? 1.0f : 0.0f; text[3] = 1.0f; }
    return 1;
}
/* STMenu::GetDesiredSize 00525e50: the title bar's width (text plus the caps and the arrow), and when the menu is open the widest entry of the
   sub pane (STSubPane::GetDesiredSize 00533670); the height is the title and the stacked entries. Approximation: widths come from the labels
   (an entry: its label plus 0.9 of its height for the end caps, a menu entry: its own desired size). */
int tg_text_extent(void *fg, const char *s, float wrap_px, float *w, float *h);
static void menu_desired(UI *m, float *ow, float *oh)
{
    float tw = 0, th = 0, w, h = tg_menu_title_h(m), sw = 0, sh = 0, sy = 0; int i;
    const char *t = m->text ? eng_TGString_GetCString(m->text) : "";
    if (t && *t && tg_text_extent(m->font, t, 0, &tw, &th)) w = tw / ui_scr_w() + h * 0.9f + 0.012f; else w = h * 2;
    if (tg_menu_opened(m) && menu_sub(m)) {
        UI *sub = menu_sub(m);
        for (i = 0; i < sub->nkids; i++) {
            UI *c = sub->kids[i]; float cw = c->w, ch = c->h;
            if (!(c->flags & UF_VISIBLE)) continue;
            if (as_menu(c)) menu_desired(c, &cw, &ch);
            else if (cw <= 0) {
                const char *ct = c->text ? eng_TGString_GetCString(c->text) : "";
                if (ct && *ct && tg_text_extent(c->font, ct, 0, &tw, &th)) cw = tw / ui_scr_w() + 0.0417f * (ui_scr_h() / ui_scr_w()) * 0.9f * ui_scr_w() / ui_scr_w();
                if (ch <= 0) ch = 0.0417f;
            }
            if (cw > sw) sw = cw; sy += ch;
        }
        sh = sy; if (sw > w) w = sw; h += sh + 0.003f;
    }
    *ow = w; *oh = h;
}
void eng_STMenu_GetDesiredSize(void *p, void *out) { UI *m = as_menu(p); float *o = out; if (m && o) menu_desired(m, &o[0], &o[1]); }
double eng_STMenu_GetActualHeight(void *p) { UI *m = as_menu(p); return m ? m->h : 0.0; }
double eng_STMenu_GetTitleBarHeight(void *p) { UI *m = as_menu(p); return m ? m->h - (menu_sub(m) ? menu_sub(m)->h : 0) : 0.0; }
double eng_STMenu_GetDesiredHeight(void *p) { UI *m = as_menu(p); return m ? m->h : 0.0; }
void *eng_STMenu_GetText(void *p) { UI *m = as_menu(p); return m ? m->text : NULL; }
void *eng_STMenu_GetSubmenu(void *p, char *n) { UI *m = as_menu(p); return m && menu_sub(m) ? eng_STSubPane_GetSubmenu(menu_sub(m), n) : NULL; }
void *eng_STMenu_GetSubmenuW(void *p, void *n) { UI *m = as_menu(p); return m && menu_sub(m) ? eng_STSubPane_GetSubmenuW(menu_sub(m), n) : NULL; }
PyObject *eng_STMenu_GetButton(void *p, char *n) { UI *m = as_menu(p); return m && menu_sub(m) ? eng_STSubPane_GetButton(menu_sub(m), n) : none(); }
PyObject *eng_STMenu_GetButtonW(void *p, void *n) { UI *m = as_menu(p); return m && menu_sub(m) ? eng_STSubPane_GetButtonW(menu_sub(m), n) : none(); }
void eng_STMenu_RemoveItem(void *p, char *n) { UI *m = as_menu(p); if (m && menu_sub(m)) eng_STSubPane_RemoveItem(menu_sub(m), n); }
void eng_STMenu_RemoveItemW(void *p, void *n) { UI *m = as_menu(p); if (m && menu_sub(m)) eng_STSubPane_RemoveItemW(menu_sub(m), n); }
void eng_STMenu_UseFullTitleBar(void *p, int l) { }
void eng_STMenu_DisableFullTitleBar(void *p, int l) { }
#define MFLAG(Name, Set, Is, bit)                                                                                    \
    long eng_STMenu_##Is(void *p) { UI *m = as_menu(p); return m && (m->flags & (bit)) != 0; }
long eng_STMenu_IsAutoChoose(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_AUTOCHOOSE); }
void eng_STMenu_SetAutoChoose(void *p, int v) { UI *m = as_menu(p); if (m) { if (v) m->flags |= MF_AUTOCHOOSE; else m->flags &= ~MF_AUTOCHOOSE; } }
long eng_STMenu_IsChosen(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_CHOSEN); }
void eng_STMenu_SetChosen(void *p, int on, int layout)                   /* 00526f80: choosing a menu un-chooses its radio-group siblings */
{
    UI *m = as_menu(p); extern void eng_STButton_SetChosen(void *, int, int); int i;
    if (!m) return;
    if (on) {
        m->flags |= MF_CHOSEN;
        if (m->parent && tg_is_type(&m->parent->o, CT_STSUBPANE) && (m->parent->flags & SP_RADIO))
            for (i = 0; i < m->parent->nkids; i++) { UI *c = m->parent->kids[i]; if (tg_is_type(&c->o, CT_STBUTTON)) eng_STButton_SetChosen(c, 0, 0); if (c != m && tg_is_type(&c->o, CT_STMENU)) eng_STMenu_SetChosen(c, 0, 0); }
    } else m->flags &= ~MF_CHOSEN;
    if (layout) ui_layout(m);
}
void eng_STMenu_SetUseEndCapSpace(void *p, int v, int l) { UI *m = as_menu(p); if (m) { if (v) m->flags |= MF_ENDCAP; else m->flags &= ~MF_ENDCAP; } }
long eng_STMenu_IsUseEndCapSpace(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_ENDCAP); }
void *eng_STMenu_GetClientArea(void *p) { UI *m = as_menu(p); float *r = calloc(4, sizeof *r); if (m) { r[2] = m->w; r[3] = m->h; } return r; }
void eng_STMenu_SetTwoClicks(void *p) { UI *m = as_menu(p); if (m) m->flags |= MF_TWOCLICKS; }
void eng_STMenu_SetSingleClick(void *p) { UI *m = as_menu(p); if (m) m->flags &= ~MF_TWOCLICKS; }
long eng_STMenu_IsTwoClicks(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_TWOCLICKS); }
void eng_STMenu_SetOpenable(void *p) { UI *m = as_menu(p); if (m) m->flags |= MF_OPENABLE; }
void eng_STMenu_SetNotOpenable(void *p) { UI *m = as_menu(p); if (m) m->flags &= ~MF_OPENABLE; }
long eng_STMenu_IsOpenable(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_OPENABLE); }
void eng_STMenu_SetCloseable(void *p) { UI *m = as_menu(p); if (m) m->flags |= MF_CLOSEABLE; }
void eng_STMenu_SetNotCloseable(void *p) { UI *m = as_menu(p); if (m) m->flags &= ~MF_CLOSEABLE; }
long eng_STMenu_IsCloseable(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_CLOSEABLE); }
void eng_STMenu_SetClickedOnce(void *p) { UI *m = as_menu(p); if (m) m->flags |= MF_CLICKED_ONCE; }
void eng_STMenu_ClearClickedOnce(void *p) { UI *m = as_menu(p); if (m) m->flags &= ~MF_CLICKED_ONCE; }
long eng_STMenu_IsClickedOnce(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_CLICKED_ONCE); }
void eng_STMenu_SetOpened(void *p) { UI *m = as_menu(p); if (m) m->flags |= MF_OPENED; }
void eng_STMenu_ClearOpened(void *p) { UI *m = as_menu(p); if (m) m->flags &= ~MF_OPENED; }
long eng_STMenu_IsOpened(void *p) { UI *m = as_menu(p); return m && (m->flags & MF_OPENED); }
void eng_STMenu_SetUseUIHeight(void *p, int v) { }
void eng_STMenu_SetActivationEvent(void *p, PyObject *ev) { UI *m = as_menu(p); if (m) ((MenuX *)m->ext)->act_ev = ui_arg_ptr(ev); }
void eng_STMenu_ClearActivationEvent(void *p) { UI *m = as_menu(p); if (m) ((MenuX *)m->ext)->act_ev = NULL; }
void eng_STMenu_SendActivationEvent(void *p) { UI *m = as_menu(p); if (m) ui_send_event(((MenuX *)m->ext)->act_ev, m); }
void eng_STMenu_SetName(void *p, void *name, int layout) { UI *m = as_menu(p); if (m && name) tg_str_assign(m->text, name); }
void eng_STMenu_GetName(void *p, void *out) { UI *m = as_menu(p); if (m && out) tg_str_assign(out, m->text); }
void *eng_STMenu_GetContainingWindow(void *p) { UI *m = as_menu(p); return m ? containing_window(m) : NULL; }
void eng_STMenu_ResetColorScheme(void *p) { }
void eng_STMenu_ForceUpdate(void *p, int layout) { UI *m = as_menu(p); if (m && menu_sub(m)) eng_STSubPane_ResizeToContents(menu_sub(m), layout); }
void eng_STMenu_Open(void *p, int layout)                                 /* 00526c10: only an openable menu that is not open yet */
{
    UI *m = as_menu(p); if (!m || (m->flags & MF_OPENED) || !(m->flags & MF_OPENABLE)) return;
    { extern void tg_ui_sound(const char *); tg_ui_sound("UIMenuOpened"); }
    m->flags |= MF_OPENED; if (menu_sub(m)) { eng_STSubPane_ResizeToContents(menu_sub(m), 0); ui_show(menu_sub(m), 0); }
    if (m->parent && tg_is_type(&m->parent->o, CT_STSUBPANE)) eng_STSubPane_ResizeToContents(m->parent, 0);          /* the rows below move down */
    if (layout) ui_layout(m->parent ? m->parent : m);
}
void eng_STMenu_Close(void *p, int layout)                                /* 00526d50 */
{
    UI *m = as_menu(p); if (!m || !(m->flags & MF_OPENED) || !(m->flags & MF_CLOSEABLE)) return;
    { extern void tg_ui_sound(const char *); tg_ui_sound("UIMenuClosed"); }
    m->flags &= ~MF_OPENED; if (menu_sub(m)) eng_STSubPane_ResizeToContents(menu_sub(m), 0);
    if (m->parent && tg_is_type(&m->parent->o, CT_STSUBPANE)) eng_STSubPane_ResizeToContents(m->parent, 0);          /* the rows below move up */
    if (layout) ui_layout(m->parent ? m->parent : m);
}
void eng_STMenu_ResizeToContents(void *p, int layout) { UI *m = as_menu(p); if (m && menu_sub(m)) { eng_STSubPane_ResizeToContents(menu_sub(m), layout); ui_set_size(m, menu_sub(m)->w, menu_sub(m)->h, 0); } }

/* ---------------------------------------------- STTopLevelMenu ---------------------------------------------- */
static UI *top_menu(void *p) { return as_cls(p, CT_TOPMENU); }
static UI *tcw;
void *eng_TacticalControlWindow_GetMenuParentPane(void *, void *);
void *eng_STTopLevelMenu_CreateW(void *name, int flags, PyObject *ev) { UI *m = menu_new("STTopLevelMenu", name, (unsigned)flags); ((MenuX *)m->ext)->act_ev = ui_arg_ptr(ev); return m; }
void *eng_STTopLevelMenu_Create(char *name, int flags, PyObject *ev) { return eng_STTopLevelMenu_CreateW(eng_new_TGString(name ? name : ""), flags, ev); }
void *eng_STTopLevelMenu_CreateNull(void) { return NULL; }
void *eng_STTopLevelMenu_Cast(PyObject *o) { return top_menu(ui_arg_ptr(o)); }
void eng_STTopLevelMenu_CloseMenu(void *p, int l) { UI *m = top_menu(p); if (m) { extern void eng_STMenu_Close(void *, int); m->flags |= MF_CLOSEABLE; eng_STMenu_Close(m, l); } }
void eng_STTopLevelMenu_SetResizeSubpane(void *p, int v, int l) { }
long eng_STTopLevelMenu_IsResizeSubpane(void *p) { return 1; }
void eng_STTopLevelMenu_GotFocus(void *p, int x) { }
void eng_STTopLevelMenu_LostFocus(void *p, int x) { }
/* Show and Hide are virtual in the exe (vtable +0xb0 and +0xb4): a script that calls SetVisible or SetNotVisible through a base class wrapper reaches
   STTopLevelMenu::Show / Hide. Called by the generic flag code (tg_ui.c). */
void eng_STTopLevelMenu_SetVisible(void *p, int x); void eng_STTopLevelMenu_SetNotVisible(void *p, int x);
int tg_vis_virtual(UI *u, int on, int layout)
{
    if (!top_menu(u)) return 0;
    if (on) eng_STTopLevelMenu_SetVisible(u, layout); else eng_STTopLevelMenu_SetNotVisible(u, layout);
    return 1;
}
/* The open top level menu (DAT_0098a34c): the one the player opened last, GetOpenMenu returns it. Show 0053d010 and Hide 0053d1c0 also show and hide the
   child of the tactical control window that holds the menu (GetMenuParentPane) and refresh the window (Refresh picks the layout for the open menu).
   Not done (marked): the call at vtable +0x148 of the menu in Show (its meaning is unknown, it is the title bar update of the menu). */
static UI *open_top; void eng_STTopLevelMenu_SetNotVisible(void *p, int x);
static void refresh_tcw(void) { tg_py_call_module("Tactical.Interface.TacticalControlWindow", "Refresh", 0, NULL); }
static void drop_tooltips(void) { tg_py_call_module("BridgeHandlers", "DropCharacterToolTips", 0, NULL); }
void eng_STTopLevelMenu_SetVisible(void *p, int x)                        /* 0053d010 */
{
    UI *m = top_menu(p), *cp, *pane, *t; extern void *eng_TGUIObject_GetConceptualParent(void *); extern void eng_STSubPane_ResizeToContents(void *, int);
    if (!m) return;
    if (open_top && open_top != m) eng_STTopLevelMenu_SetNotVisible(open_top, 1);
    if (!(m->flags & UF_ENABLED)) { drop_tooltips(); refresh_tcw(); return; }
    open_top = m; m->flags |= MF_OPENED;          /* approximation: the exe sets the opened bit elsewhere (not found) */
    ui_show(m, x);
    cp = eng_TGUIObject_GetConceptualParent(m); if (cp) ui_show(cp, x);
    if (m->parent) ui_show(m->parent, x);
    { UI *c = m; while (c->parent && c->parent->o.id) { if (c->parent->focus != c) pane_set_focus(c->parent, c); c = c->parent; } }    /* InterfaceModule::ForceFocusOnObject 005567e0 */
    t = tcw;
    if (t) {
        pane = eng_TacticalControlWindow_GetMenuParentPane(t, m->text);
        if (pane) ui_show(pane, 1);
        if (menu_sub(m)) eng_STSubPane_ResizeToContents(menu_sub(m), 1);
        if (pane) pane_set_focus(t, pane);
        ui_show(t, 1);
    }
    drop_tooltips(); refresh_tcw();
}
void eng_STTopLevelMenu_SetNotVisible(void *p, int x)                     /* Hide 0053d1c0 */
{
    UI *m = top_menu(p), *cp, *pane, *sub; extern void *eng_TGUIObject_GetConceptualParent(void *); extern void *eng_TopWindow_GetTopWindow(void); extern long eng_TopWindow_IsTacticalVisible(void *);
    if (!m) return;
    sub = m->focus; if (sub && as_menu(sub)) { extern void eng_STMenu_Close(void *, int); sub->flags |= MF_CLOSEABLE; eng_STMenu_Close(sub, 1); }
    cp = eng_TGUIObject_GetConceptualParent(m);
    ui_hide(m, x); m->flags &= ~MF_OPENED;
    if (cp) ui_hide(cp, x);
    if (tcw) {
        pane = eng_TacticalControlWindow_GetMenuParentPane(tcw, m->text);
        if (pane) ui_hide(pane, 1);
        if (tcw->parent) pane_set_focus(tcw->parent, tcw);
        if (tcw->parent) refresh_tcw();
        if (eng_TopWindow_IsTacticalVisible(eng_TopWindow_GetTopWindow()) && tcw->nkids) { ui_show(tcw->kids[0], 0); pane_set_focus(tcw, tcw->kids[0]); }
        if (open_top == m) open_top = NULL;
    }
}
void eng_STTopLevelMenu_ResizeToContents(void *p, int l) { eng_STMenu_ResizeToContents(p, l); }
void *eng_STTopLevelMenu_GetOwner(void *p) { UI *m = top_menu(p); return m ? ((MenuX *)m->ext)->owner : NULL; }
void eng_STTopLevelMenu_SetOwner(void *p, void *o) { UI *m = top_menu(p); if (m) ((MenuX *)m->ext)->owner = as_ui(o); }
void *eng_STTopLevelMenu_GetOpenMenu(void) { return open_top; }

/* ---------------------------------------------- STButton ---------------------------------------------- */
typedef struct { void *act_ev; int justification, use_end_caps; float color[4]; float rb_cols[12][4]; int rb_set[12];   /* 0 normal, 1 highlighted, 2 selected, 3 text, 4 disabled, 5 highlighted text, 6 selected text, 7 disabled text, 8..10 marker normal, highlighted, selected */
               void *caption, *state_name[4]; void *state_ev[4]; int nstates, state; int fill_on; float fill_frac; int auto_col, scheme; } BtnX;      /* the last line: STToggle */   /* rb_cols: rounded button normal, highlighted, selected, text */
static UI *as_btn(void *p) { return as_cls(p, CT_STBUTTON); }
static UI *btn_new(const char *cls, void *name, void *ev, unsigned flags)
{
    UI *b = ui_new(cls); BtnX *x = calloc(1, sizeof *x); x->auto_col = 1; b->ext = x; b->text = name ? tg_str_copy(name) : eng_new_TGString(""); x->act_ev = ev;
    b->act_ev = ev; b->flags = (b->flags & 0x0fffffffu) | (flags & 0xf0000000u); b->flags |= UF_ENABLED;
    return b;
}
/* a button whose body is a gauge (StRepairButton): the left part, fraction f of the width, in the button colour, the rest dark */
void tg_button_gauge(UI *b, float f) { BtnX *x = b ? b->ext : NULL; if (x) { x->fill_on = 1; x->fill_frac = f < 0 ? 0 : f > 1 ? 1 : f; } }
int tg_button_fill(const void *node, float *f) { const UI *b = node; const BtnX *x = b && b->ext && tg_is_type((TGObject *)&b->o, CT_STBUTTON) ? b->ext : NULL; if (!x || !x->fill_on) return 0; *f = x->fill_frac; return 1; }
UI *tg_button_new(void *name, void *ev) { UI *b = btn_new("STButton", name, ev, 0x80000000u); return b; }
void *eng_STButton_CreateW(void *name, PyObject *ev, int flags, float w, float h, PyObject *parent, int a)
{ UI *b = btn_new("STButton", name, ui_arg_ptr(ev), (unsigned)flags); b->w = w; b->h = h; return b; }
void *eng_STButton_Create(char *name, PyObject *ev, int flags, float w, float h, PyObject *parent, int a)
{ UI *b = btn_new("STButton", eng_new_TGString(name ? name : ""), ui_arg_ptr(ev), (unsigned)flags); b->w = w; b->h = h; return b; }
void *eng_STButton_Cast(PyObject *o) { return as_btn(ui_arg_ptr(o)); }
void *eng_STButton_GetText(void *p) { UI *b = as_btn(p); return b ? b->text : NULL; }
void eng_STButton_SetName(void *p, void *n, int l) { UI *b = as_btn(p); if (b && n) tg_str_assign(b->text, n); }
void eng_STButton_GetName(void *p, void *out) { UI *b = as_btn(p); if (b && out) tg_str_assign(out, b->text); }
/* the colours the drawing uses (tg_rounded_colour): normal, highlighted, selected and text; the others are not drawn yet */
static void bt_colour(void *p, int i, PyObject *c) { UI *b = as_btn(p); float *q = ui_arg_ptr(c); if (b && b->ext && q) { memcpy(((BtnX *)b->ext)->rb_cols[i], q, 16); ((BtnX *)b->ext)->rb_set[i] = 1; } }
void eng_STButton_SetNormalColor(void *p, PyObject *c) { bt_colour(p, 0, c); }
void eng_STButton_SetHighlightedColor(void *p, PyObject *c) { bt_colour(p, 1, c); }
void eng_STButton_SetSelectedColor(void *p, PyObject *c) { bt_colour(p, 2, c); }
void eng_STButton_SetTextColor(void *p, PyObject *c) { bt_colour(p, 3, c); }
void eng_STButton_SetDisabledColor(void *p, PyObject *c) { bt_colour(p, 4, c); }
void eng_STButton_SetHighlightedTextColor(void *p, PyObject *c) { bt_colour(p, 5, c); }
void eng_STButton_SetSelectedTextColor(void *p, PyObject *c) { bt_colour(p, 6, c); }
void eng_STButton_SetDisabledTextColor(void *p, PyObject *c) { bt_colour(p, 7, c); }
void eng_STButton_SetMarkerNormalColor(void *p, PyObject *c) { bt_colour(p, 8, c); }
void eng_STButton_SetMarkerHighlightedColor(void *p, PyObject *c) { bt_colour(p, 9, c); }
void eng_STButton_SetMarkerSelectedColor(void *p, PyObject *c) { bt_colour(p, 10, c); }
/* STButton colour schemes (FUN_00519650, FUN_005196b0, table g_kSTButtonColors at 009889f0: per scheme normal, selected, highlighted, disabled). The scheme is
   the number of sub panes the button sits in (a menu's stack, through the menu to the next one), one less for a chosen button that is not choosable;
   at most 3. A button that has the flag set takes the colours of its scheme when it is laid out in a new one (scripts' own colours are lost then). */
static const float btn_scheme[4][4][4] = {
    {{0.8471f, 0.3686f, 0.3373f, 1}, {0.498f, 0.2353f, 0.1686f, 1}, {0.9961f, 0.4706f, 0.3373f, 1}, {0.25f, 0.25f, 0.25f, 1}},
    {{0.5765f, 0.4039f, 1, 1}, {0.3392f, 0.2588f, 0.5f, 1}, {0.6784f, 0.5176f, 1, 1}, {0.25f, 0.25f, 0.25f, 1}},
    {{0.8118f, 0.3765f, 0.6235f, 1}, {0.4059f, 0.1882f, 0.3118f, 1}, {0.9647f, 0.5765f, 0.8f, 1}, {0.25f, 0.25f, 0.25f, 1}},
    {{0.5647f, 0.4039f, 0.5647f, 1}, {0.2824f, 0.202f, 0.2824f, 1}, {0.6863f, 0.5647f, 0.6863f, 1}, {0.25f, 0.25f, 0.25f, 1}}};
static int btn_scheme_of(const UI *b)
{
    const UI *p = b->parent; int n = 0;
    while (p) {
        if (tg_is_type((TGObject *)&p->o, CT_STSUBPANE)) n++;
        if (!p->parent) break;
        p = p->parent->parent;                                        /* the menu around the pane, then its parent */
        if (!p || !tg_is_type((TGObject *)&p->o, CT_STSUBPANE)) break;
    }
    if ((b->flags & 0x20000000u) && !(b->flags & 0x8000000u) && --n < 0) return 0;
    return n > 3 ? 3 : n;
}
void tg_button_relayout(UI *b)                                         /* from the layout: take the scheme's colours when it changed */
{
    BtnX *x = b->ext; int sc;
    if (!x || !x->auto_col) return;
    sc = btn_scheme_of(b);
    if (sc != x->scheme) { x->scheme = sc; x->rb_set[0] = x->rb_set[1] = x->rb_set[2] = x->rb_set[4] = 0; }
}
/* 1 when a button is chosen (a radio or toggle choice, drawn in its selected colour) */
int tg_button_chosen(const void *node) { const UI *b = node; return b && (b->flags & BF_CHOSEN) && tg_is_type((TGObject *)&b->o, CT_STBUTTON); }
#define BTN_COLOR(Name) void eng_STButton_Set##Name(void *p, PyObject *c) { }
void eng_STButton_SetColor(void *p, PyObject *c) { }
void eng_STButton_SetColorBasedOnFlags(void *p) { }
void eng_STButton_SetJustification(void *p, int j) { UI *b = as_btn(p); if (b) ((BtnX *)b->ext)->justification = j; }
void *eng_STButton_GetContainingWindow(void *p) { UI *b = as_btn(p); return b ? containing_window(b) : NULL; }
void eng_STButton_SetAutoChoose(void *p, int v) { UI *b = as_btn(p); if (b) { if (v) b->flags |= BF_AUTOCHOOSE; else b->flags &= ~BF_AUTOCHOOSE; } }
long eng_STButton_IsAutoChoose(void *p) { UI *b = as_btn(p); return b && (b->flags & BF_AUTOCHOOSE); }
void eng_STButton_SetChoosable(void *p, int v, int l) { UI *b = as_btn(p); if (b) { if (v) b->flags |= BF_CHOOSABLE; else b->flags &= ~BF_CHOOSABLE; } }
long eng_STButton_IsChoosable(void *p) { UI *b = as_btn(p); return b && (b->flags & BF_CHOOSABLE); }
void eng_STButton_SetChosen(void *p, int on, int layout)                   /* 0051a2c0: choosing un-chooses the siblings of a radio group */
{
    UI *b = as_btn(p); int i; if (!b) return;
    if (on) {
        b->flags |= BF_CHOSEN;
        if (b->parent && tg_is_type(&b->parent->o, CT_STSUBPANE) && (b->parent->flags & SP_RADIO))
            for (i = 0; i < b->parent->nkids; i++) { UI *c = b->parent->kids[i]; if (c != b && tg_is_type(&c->o, CT_STBUTTON)) eng_STButton_SetChosen(c, 0, 0); if (tg_is_type(&c->o, CT_STMENU)) eng_STMenu_SetChosen(c, 0, 0); }
    } else b->flags &= ~BF_CHOSEN;
}
long eng_STButton_IsChosen(void *p) { UI *b = as_btn(p); return b && (b->flags & BF_CHOSEN); }
long eng_STButton_IsUsingEndCaps(void *p) { UI *b = as_btn(p); return b && ((BtnX *)b->ext)->use_end_caps; }
void eng_STButton_SetUseEndCaps(void *p, int v, int l) { UI *b = as_btn(p); if (b) ((BtnX *)b->ext)->use_end_caps = v != 0; }
void eng_STButton_SetUseEndCapSpace(void *p, int v, int l) { UI *b = as_btn(p); if (b) { if (v) b->flags |= BF_ENDCAPSPACE; else b->flags &= ~BF_ENDCAPSPACE; } }
long eng_STButton_IsUseEndCapSpace(void *p) { UI *b = as_btn(p); return b && (b->flags & BF_ENDCAPSPACE); }
void eng_STButton_SetUseUIHeight(void *p, int v) { UI *b = as_btn(p); if (b) ((BtnX *)b->ext)->auto_col = v != 0; }       /* the flag at +0x10c: recolour with the menu scheme when laid out */

/* ---------------------------------------------- STNumericBar ---------------------------------------------- */
/* A bar with a label (the text paragraph, a child) and a filled part for the value between the range ends: the volume sliders of the
   options menu. Drawn by tg_ui_draw.c (tg_numeric_bar). The percentage text and the marker are made but not drawn yet. */
typedef struct { float lo, hi, val, key; int use_marker, use_alt, use_buttons, show_pct; float normal[4], empty[4]; UI *text, *pct; void *upd_ev; } NumX;
extern void *eng_TGParagraph_Create(char *text, float wrap, PyObject *color, char *font, float f, int flags);
static UI *as_numbar(void *p) { return as_cls(p, 0x80f1); }
void *eng_STNumericBar_Create(float w, float h)
{
    UI *u = ui_new("STNumericBar"); NumX *x = calloc(1, sizeof *x); static const float black[4] = {0, 0, 0, 1};
    u->flags |= UF_ENABLED; u->ext = x; x->lo = 0; x->hi = 1; x->key = 0.1f;
    x->normal[0] = x->normal[3] = 1; x->empty[3] = 1;
    ui_set_size(u, w > 0 ? w : 0.3f, h > 0 ? h : 0.0416667f, 0);
    x->text = eng_TGParagraph_Create("", 1.0f, NULL, NULL, 0, 0); memcpy(x->text->color, black, 16);
    x->pct = eng_TGParagraph_Create("", 1.0f, NULL, NULL, 0, 0); memcpy(x->pct->color, black, 16);
    pane_add(u, 0, x->text, 0.012f, 0, 0); pane_add(u, 1, x->pct, 0.0f, 0, 0); ui_hide(x->pct, 0);
    return u;
}
void tg_numeric_colours(UI *bar, const float *normal, const float *empty) { UI *b = as_numbar(bar); NumX *x = b ? b->ext : NULL; if (!x) return; if (normal) memcpy(x->normal, normal, 16); if (empty) memcpy(x->empty, empty, 16); }
void *eng_STNumericBar_Cast(PyObject *o) { return as_numbar(ui_arg_ptr(o)); }
#define NB(p) UI *b = as_numbar(p); NumX *x = b ? b->ext : NULL; if (!x) return
void eng_STNumericBar_SetRange(void *p, float lo, float hi) { NB(p); x->lo = lo; x->hi = hi; }
extern void eng_TGParagraph_SetString(void *, char *, int);
static void numbar_pct(UI *b)                              /* the percentage at the right end of the bar (the power sliders) */
{
    NumX *x = b->ext; char t[16]; if (!x || !x->show_pct) return;
    snprintf(t, sizeof t, "%d%%", (int)(x->val * 100.0f + 0.5f));
    eng_TGParagraph_SetString(x->pct, t, 0); ui_set_pos(x->pct, b->w - x->pct->w - 0.006f, x->text->y, 0); ui_show(x->pct, 0);
}
void tg_numeric_show_pct(UI *bar) { UI *b = as_numbar(bar); NumX *x = b ? b->ext : NULL; if (x) { x->show_pct = 1; numbar_pct(b); } }
void eng_STNumericBar_SetValue(void *p, float v)
{
    NB(p);
    if (x->hi != x->lo && (int)x->val != (int)v) { extern void tg_ui_sound_once(const char *); tg_ui_sound_once("UINumericBarClick"); }   /* 0052a8d0: a new whole value ticks */
    x->val = v; numbar_pct(b);
}
double eng_STNumericBar_GetValue(void *p) { UI *b = as_numbar(p); return b ? ((NumX *)b->ext)->val : 0.0; }
double eng_STNumericBar_GetKeyInterval(void *p) { UI *b = as_numbar(p); return b ? ((NumX *)b->ext)->key : 0.0; }
void eng_STNumericBar_SetKeyInterval(void *p, float v) { NB(p); x->key = v; }
void eng_STNumericBar_SetUseMarker(void *p, int v) { NB(p); x->use_marker = v; }
void eng_STNumericBar_SetUseAlternateColor(void *p, int v) { NB(p); x->use_alt = v; }
void eng_STNumericBar_SetUseButtons(void *p, int v) { NB(p); x->use_buttons = v; }
void eng_STNumericBar_SetNormalColor(void *p, PyObject *c) { NB(p); float *q = ui_arg_ptr(c); if (q) memcpy(x->normal, q, 16); }
void eng_STNumericBar_SetEmptyColor(void *p, PyObject *c) { NB(p); float *q = ui_arg_ptr(c); if (q) memcpy(x->empty, q, 16); }
void eng_STNumericBar_SetUpdateEvent(void *p, void *e) { NB(p); x->upd_ev = e; }
void *eng_STNumericBar_GetText(void *p) { UI *b = as_numbar(p); return b ? ((NumX *)b->ext)->text : NULL; }
void *eng_STNumericBar_GetPercentageText(void *p) { UI *b = as_numbar(p); return b ? ((NumX *)b->ext)->pct : NULL; }
/* for the draw list: the filled fraction and the two colours; 0 when the node is not a numeric bar */
int tg_numeric_bar(const void *node, float *frac, const float **fill, const float **empty)
{
    const UI *b = node; const NumX *x; if (!b || !b->ext || !tg_is_type((TGObject *)&b->o, 0x80f1)) return 0;
    x = b->ext; *frac = x->hi > x->lo ? (x->val - x->lo) / (x->hi - x->lo) : 0; if (*frac < 0) *frac = 0; if (*frac > 1) *frac = 1;
    *fill = x->normal; *empty = x->empty; return 1;
}

/* ---------------------------------------------- STToggle ---------------------------------------------- */
void tg_toggle_activate(UI *b);
/* A button that cycles through up to four named states; it shows its caption and the name of the state ("Texture Detail: High"). Each
   state has an event (sent when the state is chosen). Approximation: the label is the caption, a space, and the state name. */
static void *ev_ptr(PyObject *o) { return o && o != Py_None ? ui_arg_ptr(o) : NULL; }
static UI *as_toggle(void *p) { return as_cls(p, CT_STTOGGLE); }
static void tog_refresh(UI *b)
{
    BtnX *x = b->ext; char buf[256]; const char *c = x->caption ? eng_TGString_GetCString(x->caption) : "", *st = x->nstates ? eng_TGString_GetCString(x->state_name[x->state]) : "";
    snprintf(buf, sizeof buf, "%s%s%s", c, *c && *st ? " " : "", st); eng_TGString_SetString(b->text, buf);
}
static void *tog_make(void *name, int def, void **names, void **evs, int n)
{
    UI *b = btn_new("STToggle", name, NULL, 0); BtnX *x = b->ext; int i;
    x->caption = name ? tg_str_copy(name) : eng_new_TGString("");
    for (i = 0; i < n && i < 4; i++) if (names[i]) { x->state_name[x->nstates] = tg_str_copy(names[i]); x->state_ev[x->nstates] = evs[i]; x->nstates++; }
    x->state = def >= 0 && def < x->nstates ? def : 0;
    tog_refresh(b); return b;
}
void *eng_STToggle_CreateW(void *name, int def, void *n1, PyObject *e1, void *n2, PyObject *e2, void *n3, PyObject *e3, void *n4, PyObject *e4)
{ void *n[4] = {n1, n2, n3, n4}; void *e[4]; e[0] = ev_ptr(e1); e[1] = ev_ptr(e2); e[2] = ev_ptr(e3); e[3] = ev_ptr(e4); return tog_make(name, def, n, e, 4); }
void *eng_STToggle_CreateUnsizedW(void *name, int def, void *n1, PyObject *e1, void *n2, PyObject *e2, void *n3, PyObject *e3, void *n4, PyObject *e4)
{ return eng_STToggle_CreateW(name, def, n1, e1, n2, e2, n3, e3, n4, e4); }
void *eng_STToggle_Create(PyObject *name, int def, PyObject *n1, PyObject *e1, PyObject *n2, PyObject *e2, PyObject *n3, PyObject *e3, PyObject *n4, PyObject *e4)
{
    void *n[4], *e[4]; PyObject *src[4] = {n1, n2, n3, n4}; int i; char *cs;
    e[0] = ev_ptr(e1); e[1] = ev_ptr(e2); e[2] = ev_ptr(e3); e[3] = ev_ptr(e4);
    void *nm = appc_to_cstr(name, &cs) ? eng_new_TGString(cs) : eng_new_TGString("");
    for (i = 0; i < 4; i++) n[i] = src[i] && src[i] != Py_None && appc_to_cstr(src[i], &cs) ? eng_new_TGString(cs) : NULL;
    return tog_make(nm, def, n, e, 4);
}
/* a press on a toggle: the next state, its label, and the event of that state */
void tg_toggle_activate(UI *b)
{
    BtnX *x = b->ext; if (!x || x->nstates < 1) return;
    x->state = (x->state + 1) % x->nstates; tog_refresh(b);
    if (x->state_ev[x->state]) ui_send_event(x->state_ev[x->state], b);
}
void *eng_STToggle_Cast(PyObject *o) { return as_toggle(ui_arg_ptr(o)); }
long eng_STToggle_GetState(void *p) { UI *b = as_toggle(p); return b ? ((BtnX *)b->ext)->state : 0; }
void eng_STToggle_SetState(void *p, int st, int layout) { UI *b = as_toggle(p); BtnX *x = b ? b->ext : NULL; if (x && st >= 0 && st < x->nstates) { x->state = st; tog_refresh(b); } }
void eng_STToggle_SetPrimaryCaptionW(void *p, void *t) { UI *b = as_toggle(p); if (b && t) { ((BtnX *)b->ext)->caption = tg_str_copy(t); tog_refresh(b); } }
void eng_STToggle_SetPrimaryCaption(void *p, char *t) { eng_STToggle_SetPrimaryCaptionW(p, eng_new_TGString(t ? t : "")); }
void *eng_STToggle_GetPrimaryCaption(void *p) { UI *b = as_toggle(p); return b ? ((BtnX *)b->ext)->caption : NULL; }
void eng_STToggle_SetStateValueW(void *p, int i, void *name, PyObject *ev)
{ UI *b = as_toggle(p); BtnX *x = b ? b->ext : NULL; if (x && i >= 0 && i < 4) { x->state_name[i] = tg_str_copy(name); x->state_ev[i] = ev_ptr(ev); if (i >= x->nstates) x->nstates = i + 1; tog_refresh(b); } }
/* for the tactical weapons control (tg_tac_ctrl.c): toggles and button colours from C */
UI *tg_toggle_new(void *caption, int def, int n, void **names, void **evs) { return tog_make(caption, def, names, evs, n); }
void tg_toggle_set_value(UI *b, int i, void *name, void *ev)
{ BtnX *x = b ? b->ext : NULL; if (x && i >= 0 && i < 4) { x->state_name[i] = name ? tg_str_copy(name) : eng_new_TGString(""); x->state_ev[i] = ev; if (i >= x->nstates) x->nstates = i + 1; tog_refresh(b); } }
void tg_toggle_set_caption(UI *b, void *caption) { BtnX *x = b ? b->ext : NULL; if (x && caption) { x->caption = tg_str_copy(caption); tog_refresh(b); } }
void tg_button_colours(UI *b, const float *normal, const float *highlighted, const float *selected, const float *text)
{
    BtnX *x = b ? b->ext : NULL; int i; const float *c[4] = {normal, highlighted, selected, text};
    if (x) for (i = 0; i < 4; i++) if (c[i]) { memcpy(x->rb_cols[i], c[i], 16); x->rb_set[i] = 1; }
}
void eng_STToggle_SetStateValue(void *p, int i, char *name, PyObject *ev) { eng_STToggle_SetStateValueW(p, i, eng_new_TGString(name ? name : ""), ev); }

/* ---------------------------------------------- GraphicsMenu ---------------------------------------------- */
/* The graphics options of the Configure tab: a holder object whose parts the script adds to its own window (the holder itself stays
   hidden): the display device menu, the resolution menu (a radio group of buttons), the window size toggle and the colour depth
   toggle. Approximation: the port has one device (the SDL3 Metal renderer); the lists are fixed. Strings come from Options.tgl. */
typedef struct { UI *devices, *resolution, *size, *depth; } GfxX;
extern void *eng_TGLocalizationManager_Load(void *m, char *path, int flag);
extern void *eng_TGLocalizationDatabase_GetString(void *p, char *key);
extern long eng_UtopiaModule_GetNextEventType(void);
static void *gfx_str(void *db, const char *key, const char *fallback)
{
    void *t = db ? eng_TGLocalizationDatabase_GetString(db, (char *)key) : NULL;
    return t ? tg_str_copy(t) : eng_new_TGString((char *)fallback);
}
/* what a click on one of the display buttons does: tells the Python module ui_display (port/appc/shim) which choice was made */
static void *NULL_EVS[4];
static int et_gfx_resolution, et_gfx_layout, et_gfx_size, et_gfx_fullscreen;
static void gfx_call(const char *fn, int value)
{
    PyObject *m = PyImport_ImportModule("ui_display"), *r;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    r = PyObject_CallMethod(m, (char *)fn, "(i)", value);
    if (!r) { PyErr_Print(); PyErr_Clear(); } else Py_DECREF(r);
    Py_DECREF(m);
}
static void gfx_on_resolution(TGEvent *e) { gfx_call("apply_resolution_index", (int)e->v.i); }
static void gfx_on_layout(TGEvent *e) { gfx_call("apply_layout", (int)e->v.i); }
static void gfx_on_size(TGEvent *e) { gfx_call("apply_size", (int)e->v.i); }
static void gfx_on_fullscreen(TGEvent *e) { gfx_call("apply_fullscreen", (int)e->v.i); }
static void *gfx_event(UI *holder, int type, int value)
{
    TGEvent *e = tg_ev_new("TGEvent", 0); e->type = (unsigned)type; e->dest = &holder->o; e->v.i = value; e->refcount++; return e;
}
static long gfx_py_int(const char *fn, long dflt)
{
    PyObject *m = PyImport_ImportModule("ui_display"), *r; long v = dflt;
    if (!m) { PyErr_Clear(); return dflt; }
    r = PyObject_CallMethod(m, (char *)fn, NULL);
    if (r) { if (PyInt_Check(r)) v = PyInt_AsLong(r); Py_DECREF(r); } else { PyErr_Print(); PyErr_Clear(); }
    Py_DECREF(m); return v;
}
void *eng_GraphicsMenu_CreateW(void *name)
{
    UI *m = menu_new("STMenu", name, 0x60000000u); GfxX *g = calloc(1, sizeof *g);
    void *db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Options.tgl", 1);
    int i, n, cur;
    void *states[4], *evs[4] = {NULL, NULL, NULL, NULL};
    UI *sub; PyObject *list = NULL, *mod;
    if (!et_gfx_resolution) {
        et_gfx_resolution = (int)eng_UtopiaModule_GetNextEventType(); et_gfx_layout = (int)eng_UtopiaModule_GetNextEventType();
        et_gfx_size = (int)eng_UtopiaModule_GetNextEventType(); et_gfx_fullscreen = (int)eng_UtopiaModule_GetNextEventType();
    }
    tg_register_native(m, et_gfx_resolution, gfx_on_resolution); tg_register_native(m, et_gfx_layout, gfx_on_layout);
    tg_register_native(m, et_gfx_size, gfx_on_size); tg_register_native(m, et_gfx_fullscreen, gfx_on_fullscreen);
    m->ext = realloc(m->ext, sizeof(MenuX)); ((MenuX *)m->ext)->owner = (UI *)g;           /* the parts are kept in the owner slot's block */
    g->devices = menu_new("STMenu", gfx_str(db, "Display Device", "Display Device"), 0x60000000u);
    sub = menu_sub(g->devices); sub->flags |= SP_RADIO;
    { UI *b = btn_new("STButton", eng_new_TGString("Apple Metal (SDL3)"), NULL, 0); b->flags |= BF_CHOOSABLE | BF_CHOSEN; pane_add(sub, sub->nkids, b, 0, 0, 0); }
    /* the window sizes of this display (ui_display.resolutions), the current one chosen */
    g->resolution = menu_new("STMenu", gfx_str(db, "Resolution", "Resolution"), 0x60000000u);
    sub = menu_sub(g->resolution); sub->flags |= SP_RADIO;
    mod = PyImport_ImportModule("ui_display");
    if (mod) { list = PyObject_CallMethod(mod, "resolutions", NULL); if (!list) { PyErr_Print(); PyErr_Clear(); } Py_DECREF(mod); } else PyErr_Clear();
    cur = (int)gfx_py_int("current_resolution_index", -1);
    n = list && PyList_Check(list) ? (int)PyList_Size(list) : 0;
    for (i = 0; i < n; i++) {
        PyObject *t = PyList_GetItem(list, i); char label[48]; UI *b;
        snprintf(label, sizeof label, "%d x %d", (int)PyInt_AsLong(PyTuple_GetItem(t, 0)), (int)PyInt_AsLong(PyTuple_GetItem(t, 1)));
        b = btn_new("STButton", eng_new_TGString(label), gfx_event(m, et_gfx_resolution, i), 0); b->flags |= BF_CHOOSABLE;
        if (i == cur) b->flags |= BF_CHOSEN;
        pane_add(sub, sub->nkids, b, 0, 0, 0);
    }
    Py_XDECREF(list);
    /* interface options are toggles in the same menu: layout (auto or 4:3), size (small, normal, large) and full screen */
    states[0] = eng_new_TGString("AUTO"); states[1] = eng_new_TGString("4:3");
    evs[0] = gfx_event(m, et_gfx_layout, 0); evs[1] = gfx_event(m, et_gfx_layout, 1);
    { UI *t = tog_make(eng_new_TGString("Interface layout:"), (int)gfx_py_int("get_layout", 0), states, evs, 2); pane_add(sub, sub->nkids, t, 0, 0, 0); }
    states[0] = eng_new_TGString("SMALL"); states[1] = eng_new_TGString("NORMAL"); states[2] = eng_new_TGString("LARGE");
    evs[0] = gfx_event(m, et_gfx_size, -1); evs[1] = gfx_event(m, et_gfx_size, 0); evs[2] = gfx_event(m, et_gfx_size, 1);
    { UI *t = tog_make(eng_new_TGString("Interface size:"), (int)gfx_py_int("get_size", 0) + 1, states, evs, 3); pane_add(sub, sub->nkids, t, 0, 0, 0); }
    states[0] = eng_new_TGString("WINDOW"); states[1] = eng_new_TGString("FULL SCREEN");
    evs[0] = gfx_event(m, et_gfx_fullscreen, 0); evs[1] = gfx_event(m, et_gfx_fullscreen, 1);
    { UI *t = tog_make(eng_new_TGString("Display mode:"), (int)gfx_py_int("get_fullscreen", 0), states, evs, 2); pane_add(sub, sub->nkids, t, 0, 0, 0); }
    states[0] = gfx_str(db, "[FULL-SCREEN] WINDOWED", "WINDOWED"); states[1] = gfx_str(db, "FULL-SCREEN [WINDOWED]", "FULL-SCREEN");
    g->size = tog_make(eng_new_TGString(""), (int)gfx_py_int("get_fullscreen", 0), states, NULL_EVS, 2);
    states[0] = eng_new_TGString("16 bit"); states[1] = eng_new_TGString("32 bit"); evs[0] = evs[1] = NULL;
    g->depth = tog_make(gfx_str(db, "Color depth:", "Color depth:"), 1, states, evs, 2);
    return m;
}
static GfxX *as_gfx(void *p) { UI *m = as_cls(p, CT_STMENU); return m && m->ext ? (GfxX *)((MenuX *)m->ext)->owner : NULL; }
void *eng_GraphicsMenu_Cast(PyObject *o) { return as_gfx(ui_arg_ptr(o)) ? ui_arg_ptr(o) : NULL; }
void *eng_GraphicsMenu_GetDevicesMenu(void *p) { GfxX *g = as_gfx(p); return g ? g->devices : NULL; }
void *eng_GraphicsMenu_GetResolutionMenu(void *p) { GfxX *g = as_gfx(p); return g ? g->resolution : NULL; }
PyObject *eng_GraphicsMenu_GetSizeToggle(void *p) { GfxX *g = as_gfx(p); return g ? appc_from_ptr(g->size, "STToggle") : none(); }
PyObject *eng_GraphicsMenu_GetColorDepthToggle(void *p) { GfxX *g = as_gfx(p); return g ? appc_from_ptr(g->depth, "STToggle") : none(); }
void eng_GraphicsMenu_ResetToggles(void *p) { }

/* ---------------------------------------------- STFileDialog (STLoadDialog, STSaveDialog) ---------------------------------------------- */
/* STFileDialog::Layout 0051f740: a pane (the middle content size) holding the top area, the list window ("Files": a stylized window with
   a fixed size and an STFileMenu inside) and the button column. Sizes and places come from the current LCARS module (FILE_* and
   MAIN_MENU_*). The Load button (STLoadDialog::CreateButtons 00524040) and the Delete button are rounded buttons in the main menu button
   colours. Approximations: the button column is at the tab column of the Configure tab; the list shows the *.BCS files of the folder. */
typedef struct { UI *menu; char dir[256], filter[64]; } FileX;
/* a file menu shows only its files; a top level menu inside a stylized window (the crew menus, the target list) has no title bar of its own: the window's title serves, and its list is always shown */
int tg_menu_untitled(const void *node)
{
    const UI *m = node;
    if (!m || !m->ext || !tg_is_type((TGObject *)&m->o, CT_STMENU)) return 0;
    if (((FileX *)((const MenuX *)m->ext)->owner) && ((FileX *)((const MenuX *)m->ext)->owner)->menu == m) return 1;
    return tg_is_type((TGObject *)&m->o, CT_TOPMENU) && containing_window((UI *)m) != NULL;
}
static FileX *file_x(UI *m) { return m && m->ext ? (FileX *)((MenuX *)m->ext)->owner : NULL; }
static float lcars_f(const char *name, float dflt)
{
    extern const char *eng_GraphicsModeInfo_GetLcarsModule(void *); extern void *eng_GraphicsModeInfo_GetCurrentMode(void);
    PyObject *mod = PyImport_ImportModule((char *)eng_GraphicsModeInfo_GetLcarsModule(eng_GraphicsModeInfo_GetCurrentMode())), *a; double v = dflt;
    if (!mod) { PyErr_Clear(); return dflt; }
    a = PyObject_GetAttrString(mod, (char *)name);
    if (a) { v = PyFloat_AsDouble(a); Py_DECREF(a); } else PyErr_Clear();
    Py_DECREF(mod); return (float)v;
}
void *eng_STFileMenu_CreateW(void *name, char *dir, char *filter)
{
    UI *m = menu_new("STMenu", name, 0x60000000u); FileX *f = calloc(1, sizeof *f);
    m->ext = realloc(m->ext, sizeof(MenuX)); ((MenuX *)m->ext)->owner = (UI *)f; f->menu = m;
    snprintf(f->dir, sizeof f->dir, "%s", dir ? dir : ""); snprintf(f->filter, sizeof f->filter, "%s", filter ? filter : "*");
    return m;
}
void *eng_STFileMenu_Create(char *name, char *dir, char *filter) { return eng_STFileMenu_CreateW(eng_new_TGString(name ? name : ""), dir, filter); }
void *eng_STFileMenu_Cast(PyObject *o) { UI *m = as_cls(ui_arg_ptr(o), CT_STMENU); return file_x(m) ? m : NULL; }
void eng_STFileMenu_SetDir(void *p, char *d) { FileX *f = file_x(as_menu(p)); if (f) snprintf(f->dir, sizeof f->dir, "%s", d ? d : ""); }
void eng_STFileMenu_SetFilter(void *p, char *d) { FileX *f = file_x(as_menu(p)); if (f) snprintf(f->filter, sizeof f->filter, "%s", d ? d : "*"); }
const char *eng_STFileMenu_GetDir(void *p) { FileX *f = file_x(as_menu(p)); return f ? f->dir : ""; }
const char *eng_STFileMenu_GetFilter(void *p) { FileX *f = file_x(as_menu(p)); return f ? f->filter : "*"; }
#include <dirent.h>
#include <strings.h>
void eng_STFileMenu_Refresh(void *p)                                     /* one button per file that matches the filter ("*.ext" only), in name order */
{
    UI *m = as_menu(p), *sub; FileX *f = file_x(m); DIR *d; struct dirent *e; char path[512], *names[256]; int n = 0, i, j; const char *ext;
    if (!f || !(sub = menu_sub(m))) return;
    while (sub->nkids) { extern void eng_TGPane_DeleteChild(void *, void *, int); eng_TGPane_DeleteChild(sub, sub->kids[0], 0); }
    ext = strrchr(f->filter, '.');
    { const char *rel = f->dir; char fixed[256]; int k;
      for (k = 0; rel[k] && k < 255; k++) fixed[k] = rel[k] == '\\' ? '/' : rel[k]; fixed[k] = 0;
      snprintf(path, sizeof path, "%s", fixed); }
    d = opendir(path);
    if (d) {
        while ((e = readdir(d)) && n < 256) if (e->d_name[0] != '.' && (!ext || (strlen(e->d_name) > strlen(ext) && !strcasecmp(e->d_name + strlen(e->d_name) - strlen(ext), ext)))) names[n++] = strdup(e->d_name);
        closedir(d);
    }
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++) if (strcasecmp(names[i], names[j]) > 0) { char *t = names[i]; names[i] = names[j]; names[j] = t; }
    for (i = 0; i < n; i++) { UI *b = btn_new("STButton", eng_new_TGString(names[i]), NULL, 0); pane_add(sub, sub->nkids, b, 0, 0, 0); free(names[i]); }
    eng_STSubPane_ResizeToContents(sub, 0);
}
typedef struct { UI *content, *list, *file_menu; } DlgX;
static void *file_dialog_make(const char *cls, float w, float h, const char *load_key, const char *load_fallback)
{
    UI *root = pane_new(cls, w, h), *content, *top, *win, *btn1, *btn2; DlgX *x = calloc(1, sizeof *x);
    void *db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Options.tgl", 1);
    float cw = lcars_f("MAIN_MENU_MIDDLE_CONTENT_WIDTH", 0.733f), ch = lcars_f("MAIN_MENU_MIDDLE_CONTENT_HEIGHT", 0.7375f);
    float bw = lcars_f("MAIN_MENU_TOP_BUTTON_WIDTH", 0.144f), bh = lcars_f("MAIN_MENU_TOP_BUTTON_HEIGHT", 0.0417f);
    float lw = lcars_f("FILE_LIST_AREA_WIDTH", 0.359f), lh = lcars_f("FILE_LIST_AREA_HEIGHT", 0.7f);
    static const float c_n[4] = {244 / 255.0f, 177 / 255.0f, 0, 1}, c_h[4] = {251 / 255.0f, 224 / 255.0f, 153 / 255.0f, 1}, c_s[4] = {151 / 255.0f, 57 / 255.0f, 28 / 255.0f, 1};
    const float *gn = NULL, *gh = NULL, *gs = NULL; const struct appc_global *g; int k;
    for (g = appc_globals; g->name; g++) {
        const float **dst = !strcmp(g->name, "g_kMainMenuButtonColor") ? &gn : !strcmp(g->name, "g_kMainMenuButtonHighlightedColor") ? &gh : !strcmp(g->name, "g_kMainMenuButtonSelectedColor") ? &gs : NULL;
        if (dst) { const float *c = eng_global_get(g); if (c && (c[0] || c[1] || c[2])) *dst = c; }
    }
    content = pane_new("TGPane", cw, ch); top = pane_new("TGPane", lcars_f("FILE_TOP_AREA_WIDTH", 0.3f), lcars_f("FILE_TOP_AREA_HEIGHT", 0.44f));
    pane_add(root, 0, content, lcars_f("MAIN_MENU_MIDDLE_CONTENT_X", 0.1625f), lcars_f("MAIN_MENU_MIDDLE_CONTENT_Y", 0.045f), 0);
    pane_add(content, 0, top, lcars_f("FILE_TOP_AREA_X", 0.145f) - lcars_f("MAIN_MENU_MIDDLE_CONTENT_X", 0.1625f) + 0.0175f, 0, 0);
    {   FileX *f; extern void *eng_STFileMenu_CreateW(void *, char *, char *);
        x->file_menu = eng_STFileMenu_CreateW(gfx_str(db, "Files", "Files"), "saves\\", "*.BCS"); f = file_x(x->file_menu); (void)f; }
    win = stw_new("STStylizedWindow", "StylizedWindow", "NoMinimize", gfx_str(db, "Files", "Files"), 0, 0, NULL, 0, 0, 0, NULL);
    eng_STStylizedWindow_SetFixedSize(win, lw, lh * 0.95f, 1); eng_STStylizedWindow_SetTitleBarThickness(win, 0.0291f, 1);
    pane_add(eng_STStylizedWindow_GetInteriorPane(win), 0, x->file_menu, 0, 0, 0);
    pane_add(content, content->nkids, win, lcars_f("FILE_LIST_AREA_X", 0.336f) - lcars_f("MAIN_MENU_MIDDLE_CONTENT_X", 0.1625f) , lcars_f("FILE_LIST_AREA_Y", 0.00767f), 0);
    btn1 = btn_new("STRoundedButton", gfx_str(db, load_key, load_fallback), NULL, 0); btn1->w = bw; btn1->h = bh;
    btn2 = btn_new("STRoundedButton", gfx_str(db, "Delete", "Delete"), NULL, 0); btn2->w = bw; btn2->h = bh;
    { BtnX *b1 = btn1->ext, *b2 = btn2->ext;
      for (k = 0; k < 3; k++) { const float *c = k == 0 ? (gn ? gn : c_n) : k == 1 ? (gh ? gh : c_h) : (gs ? gs : c_s); memcpy(b1->rb_cols[k], c, 16); b1->rb_set[k] = 1; memcpy(b2->rb_cols[k], c, 16); b2->rb_set[k] = 1; } }
    pane_add(top, 0, btn1, 0, 0, 0); pane_add(top, 1, btn2, 0, bh + 0.0104f, 0);
    x->content = content; x->list = win; root->ext = x;
    return root;
}
void *eng_STFileDialog_Create(float w, float h) { return file_dialog_make("STFileDialog", w, h, "Load Game", "Load Game"); }
void *eng_STLoadDialog_Create(float w, float h) { return file_dialog_make("STLoadDialog", w, h, "Load Game", "Load Game"); }
void *eng_STSaveDialog_Create(float w, float h) { return file_dialog_make("STSaveDialog", w, h, "Save Game", "Save Game"); }
void *eng_STFileDialog_GetFileMenu(void *p) { UI *r = p; return r && r->ext && r->o.cls && !strncmp(r->o.cls->name, "ST", 2) ? ((DlgX *)r->ext)->file_menu : NULL; }
void eng_STFileDialog_RefreshFileList(void *p) { UI *r = p; if (r && r->ext) { eng_STFileMenu_Refresh(((DlgX *)r->ext)->file_menu); eng_STStylizedWindow_InteriorChangedSize(((DlgX *)r->ext)->list, 0); } }
/* STRoundedButton / STToggle share the button base */
void *eng_STRoundedButton_Create(char *name, PyObject *ev, float w, float h, int a) { UI *b = btn_new("STRoundedButton", eng_new_TGString(name ? name : ""), ui_arg_ptr(ev), 0); b->w = w; b->h = h; return b; }
void *eng_STRoundedButton_CreateW(void *name, PyObject *ev, float w, float h, int a) { UI *b = btn_new("STRoundedButton", name, ui_arg_ptr(ev), 0); b->w = w; b->h = h; return b; }
void *eng_STRoundedButton_Cast(PyObject *o) { void *p = ui_arg_ptr(o); return as_cls(p, 0x80ed); }
void eng_STRoundedButton_SetName(void *p, void *n, int l) { UI *b = as_cls(p, 0x80ed); if (b && n) tg_str_assign(b->text, n); }
void eng_STRoundedButton_GetName(void *p, void *out) { UI *b = as_cls(p, 0x80ed); if (b && out) tg_str_assign(out, b->text); }
/* the four colours the drawing uses (tg_ui_draw.c reads them through tg_rounded_colour); the others are not drawn yet */
static void rb_colour(void *p, int i, PyObject *c) { UI *b = as_cls(p, 0x80ed); float *q = ui_arg_ptr(c); if (b && q) { memcpy(((BtnX *)b->ext)->rb_cols[i], q, 16); ((BtnX *)b->ext)->rb_set[i] = 1; } }
void eng_STRoundedButton_SetNormalColor(void *p, PyObject *c) { rb_colour(p, 0, c); }
void eng_STRoundedButton_SetHighlightedColor(void *p, PyObject *c) { rb_colour(p, 1, c); }
void eng_STRoundedButton_SetSelectedColor(void *p, PyObject *c) { rb_colour(p, 2, c); }
void eng_STRoundedButton_SetTextColor(void *p, PyObject *c) { rb_colour(p, 3, c); }
#define RB_COLOR(Name) void eng_STRoundedButton_Set##Name(void *p, PyObject *c) { }
void eng_STRoundedButton_SetDisabledColor(void *p, PyObject *c) { rb_colour(p, 4, c); }
void eng_STRoundedButton_SetHighlightedTextColor(void *p, PyObject *c) { rb_colour(p, 5, c); }
void eng_STRoundedButton_SetSelectedTextColor(void *p, PyObject *c) { rb_colour(p, 6, c); }
void eng_STRoundedButton_SetDisabledTextColor(void *p, PyObject *c) { rb_colour(p, 7, c); }
RB_COLOR(Color)
/* the stored colour of a rounded button: 0 normal, 1 highlighted, 2 selected, 3 text. Returns 0 when the script never set it. */
int tg_rounded_colour(const void *node, int i, float *out)
{
    const UI *b = node; if (!b || !b->ext || !(tg_is_type((TGObject *)&b->o, 0x80ed) || tg_is_type((TGObject *)&b->o, CT_STBUTTON)) || i < 0 || i > 10) return 0;
    if (!((const BtnX *)b->ext)->rb_set[i]) {
        if (tg_is_type((TGObject *)&b->o, CT_STBUTTON) && (i <= 2 || i == 4) && strcmp(b->o.cls->name, "STToggle")) {      /* the scheme's colours: normal, highlighted, selected, disabled */
            static const int slot[5] = {0, 2, 1, 0, 3}; memcpy(out, btn_scheme[((const BtnX *)b->ext)->scheme][slot[i]], 16); return 1;
        }
        return 0;
    }
    memcpy(out, ((const BtnX *)b->ext)->rb_cols[i], 16); return 1;
}
void eng_STRoundedButton_SetColorBasedOnFlags(void *p) { }

/* ---------------------------------------------- TacticalControlWindow (00509750) ---------------------------------------------- */
typedef struct { void *disp[8]; unsigned ids[128]; int n; int pick_fire, ignore_next; void *tac_menu; } TcwX;
static UI *tcw;
static UI *as_tcw(void *p) { return as_cls(p, 0x8112); }
/* TacticalControlWindow ctor 00509750: the window's Python handlers for a display mode change (ET_UI_SWITCH_ICON_GROUPS, ET_UI_RESIZE, ET_UI_REPOSITION) and a minimize */
extern long eng_TGEventManager_AddBroadcastPythonFuncHandler(void *, int, void *, char *, void *);
void *eng_TacticalControlWindow_Create(void)
{
    UI *u = pane_new("TacticalControlWindow", 1.0f, 1.0f); u->ext = calloc(1, sizeof(TcwX)); tcw = u;
    eng_TGEventManager_AddBroadcastPythonFuncHandler(NULL, (int)tg_const("ET_UI_SWITCH_ICON_GROUPS"), u, "Tactical.Interface.TacticalControlWindow.HandleSwitchIconGroups", ANY_TARGET);
    eng_TGEventManager_AddBroadcastPythonFuncHandler(NULL, (int)tg_const("ET_UI_RESIZE"), u, "Tactical.Interface.TacticalControlWindow.HandleResizeUI", ANY_TARGET);
    eng_TGEventManager_AddBroadcastPythonFuncHandler(NULL, (int)tg_const("ET_UI_REPOSITION"), u, "Tactical.Interface.TacticalControlWindow.HandleRepositionUI", ANY_TARGET);
    return u;
}
void *eng_new_TacticalControlWindow(void) { return eng_TacticalControlWindow_Create(); }
void *eng_TacticalControlWindow_GetTacticalControlWindow(void) { return tcw; }
/* 00622b20/00623100...: the original stores the plain C++ pointer (+0xd0.. for ShipDisplay) and returns it as a typed SWIG pointer, not the Python object */
#define TCW_PYOBJ(Name, idx)                                                                                           \
    void eng_TacticalControlWindow_Set##Name(void *p, PyObject *o) { UI *u = as_tcw(p); if (u) ((TcwX *)u->ext)->disp[idx] = o && o != Py_None ? ui_arg_ptr(o) : NULL; } \
    PyObject *eng_TacticalControlWindow_Get##Name(void *p) { UI *u = as_tcw(p); UI *d = u ? ((TcwX *)u->ext)->disp[idx] : NULL; return d ? appc_from_ptr(d, d->o.cls->name) : none(); }
TCW_PYOBJ(ShipDisplay, 0) TCW_PYOBJ(EnemyShipDisplay, 1) TCW_PYOBJ(WeaponsDisplay, 2) TCW_PYOBJ(WeaponsControl, 3) TCW_PYOBJ(TargetMenu, 4) TCW_PYOBJ(RadarDisplay, 5) TCW_PYOBJ(RadarToggle, 6)
/* Game::SetUIShipID 00406770: the player's ship goes to the ship display and the weapons display, the enemy display follows the player's target */
void tg_tcw_set_ui_ship(unsigned id)
{
    extern void eng_ShipDisplay_SetShipID(void *, long); extern void eng_WeaponsDisplay_SetShipID(void *, long); extern void eng_ShipDisplay_AddTargetChangeEvent(void *);
    TcwX *x; UI *d;
    if (!tcw) return;
    x = tcw->ext;
    if ((d = x->disp[0])) eng_ShipDisplay_SetShipID(d, (long)id);
    if ((d = x->disp[2])) eng_WeaponsDisplay_SetShipID(d, (long)id);
    if ((d = x->disp[1])) eng_ShipDisplay_AddTargetChangeEvent(d);
    refresh_tcw();
}
void eng_TacticalControlWindow_SetTacticalMenu(void *p, void *m) { UI *u = as_tcw(p); if (u) ((TcwX *)u->ext)->tac_menu = m; }
void *eng_TacticalControlWindow_GetTacticalMenu(void *p) { UI *u = as_tcw(p); return u ? ((TcwX *)u->ext)->tac_menu : NULL; }
void eng_TacticalControlWindow_SetMousePickFire(void *p, int v) { UI *u = as_tcw(p); if (u) ((TcwX *)u->ext)->pick_fire = v != 0; }
long eng_TacticalControlWindow_GetMousePickFire(void *p) { UI *u = as_tcw(p); return u && ((TcwX *)u->ext)->pick_fire; }
long eng_TacticalControlWindow_IgnoreNextMouseKeyEvent(void *p) { UI *u = as_tcw(p); return u && ((TcwX *)u->ext)->ignore_next; }
void eng_TacticalControlWindow_SetIgnoreNextMouseKeyEvent(void *p, int v) { UI *u = as_tcw(p); if (u) ((TcwX *)u->ext)->ignore_next = v != 0; }
void eng_TacticalControlWindow_AddMenuToList(void *p, void *m)           /* 00509bd0: the list holds object ids, no duplicates */
{
    UI *u = as_tcw(p), *mm = as_ui(m); TcwX *x; int i; if (!u || !mm) return; x = u->ext;
    for (i = 0; i < x->n; i++) if (x->ids[i] == mm->o.id) return;
    if (x->n < 128) x->ids[x->n++] = mm->o.id;
}
void *eng_TacticalControlWindow_FindMenu(void *p, void *name)             /* 0050a2f0 */
{
    UI *u = as_tcw(p); TcwX *x; int i; if (!u) return NULL; x = u->ext;
    for (i = 0; i < x->n; i++) { UI *m = (UI *)tg_find(x->ids[i]); if (m && tg_is_type((TGObject *)&m->o, CT_TOPMENU) && name_is(m, name)) return m; }
    return NULL;
}
void *eng_TacticalControlWindow_GetMenuParentPane(void *p, void *name)    /* 0050a420: the child of the window that contains the menu (through conceptual parents) */
{
    extern void *eng_TGUIObject_GetConceptualParent(void *); UI *m = eng_TacticalControlWindow_FindMenu(p, name), *c, *par; if (!m) return NULL;
    c = m; par = eng_TGUIObject_GetConceptualParent(c);
    while (par && par != p) { c = par; par = eng_TGUIObject_GetConceptualParent(c); }
    return c;
}
void *eng_TacticalControlWindow_GetOpenMenu(void *p)                      /* 0050a3c0: a visible, opened top level menu */
{
    UI *u = as_tcw(p); TcwX *x; int i; if (!u) return NULL; x = u->ext;
    for (i = 0; i < x->n; i++) { UI *m = (UI *)tg_find(x->ids[i]); if (m && tg_is_type((TGObject *)&m->o, CT_TOPMENU) && (m->flags & MF_OPENED) && (m->flags & UF_VISIBLE) && (void *)m != x->disp[4]) return m; }      /* not the target list (+0xe0) */
    return NULL;
}

/* ---------------------------------------------- menus created by name (subclasses of STMenu) ---------------------------------------------- */
void *eng_STCharacterMenu_CreateW(void *name, PyObject *ev, PyObject *a) { return menu_new("STCharacterMenu", name, 0x60000000u); }
void *eng_STCharacterMenu_Create(char *name, PyObject *a, PyObject *b) { return menu_new("STCharacterMenu", eng_new_TGString(name ? name : ""), 0x60000000u); }
void *eng_new_STComponentMenu(void *a, void *b, PyObject *c) { return menu_new("STComponentMenu", b, 0x60000000u); }
void *eng_new_STComponentMenuItem(void *a, PyObject *b) { return btn_new("STComponentMenuItem", a, NULL, 0); }

/* ---------------------------------------------- STTargetMenu (00537be0) ---------------------------------------------- */
/* A top level menu (openable, closeable) whose sub pane is a radio group of STSubsystemMenu entries, one per ship the player can target
   in its set. Entries (+0x6c: object id + keep flag), persistent target (+0x70 object, +0x74 subsystem). Handlers (broadcast, as in the
   exe): ET_ENTERED_SET / ET_EXITED_SET / ET_OBJECT_EXPLODING / ET_CLOAK_COMPLETED / ET_DECLOAK_COMPLETED refresh the list (RefreshTargetList
   00538c90), ET_SENSORS_SHIP_IDENTIFIED adds or names the entry (005384c0), ET_TARGET_WAS_CHANGED selects the player's target (00538690);
   an entry sends ET_TARGET_LIST_CLICKED (int = object id) to the menu, whose handler (TargetButtonClicked 00538590) changes the player's target.
   Approximations (marked): the STSubsystemMenu entries have no subsystem buttons or hull gauge yet (BuildSubsystemList 00535590), no colors,
   the PythonMethodProcess and ET_OBJECT_GROUP_CHANGED / ET_TARGETED_SUBSYSTEM_CHANGED (affiliation colors, subsystem focus) are not done. */
#define CT_TARGETMENU 0x80f2
#define CT_SUBSYSMENU 0x80f3
typedef struct { MenuX m; struct { unsigned id; int keep; } *e; int n; unsigned persist, persist_sub; } TgtX;
typedef struct { MenuX m; unsigned ship; } SubsX;
static UI *target_menu;
static UI *as_tm(void *p) { return as_cls(p, CT_TARGETMENU); }
static UI *as_subs(void *p) { return as_cls(p, CT_SUBSYSMENU); }
static void *game_player(void) { extern void *eng_Game_GetCurrentPlayer(void); return eng_Game_GetCurrentPlayer(); }
static void *live_obj(unsigned id) { TGObject *o = id ? tg_find(id) : NULL; return o && !o->dead ? o : NULL; }
/* STSubsystemMenu (ctor 00535170): +0x5c ship id; names: ShowRealName 00535950 (the ship's display name), ShowUnknownName 00535800
   ("Unknown Ship" / "Unknown Station" / "Unknown Object" by the ship property genus, from Bridge Menus.tgl) */
static void subs_on_list_fwd(TGEvent *e);
static UI *subs_new(void *ship, void *ev)
{
    UI *m = menu_new("STSubsystemMenu", NULL, 0x60000000u); SubsX *x = calloc(1, sizeof *x);
    free(m->ext); m->ext = x; x->m.act_ev = ev; x->ship = ship && tg_is_ours(ship) ? ((TGObject *)ship)->id : 0;
    tg_register_native(m, (int)tg_const("ET_SUBSYSTEM_LIST_CLICKED"), subs_on_list_fwd);
    return m;
}
void *eng_new_STSubsystemMenu(void *ship, PyObject *ev) { return subs_new(ship, ui_arg_ptr(ev)); }
void *eng_STSubsystemMenu_Cast(PyObject *o) { return as_subs(ui_arg_ptr(o)); }
void *eng_STSubsystemMenu_GetShip(void *p) { UI *m = as_subs(p); return m ? live_obj(((SubsX *)m->ext)->ship) : NULL; }
void eng_STSubsystemMenu_ShowRealName(void *p)
{
    extern void *eng_BaseObjectClass_GetDisplayName(void *); UI *m = as_subs(p); void *sh = m ? eng_STSubsystemMenu_GetShip(m) : NULL, *n;
    if (!sh || !(n = eng_BaseObjectClass_GetDisplayName(sh))) return;
    tg_str_assign(m->text, n); eng_delete_TGString(n);
}
void eng_STSubsystemMenu_ShowUnknownName(void *p)
{
    extern void *eng_ShipClass_GetShipProperty(void *); extern long eng_ShipProperty_GetGenus(void *);
    extern void *eng_TGLocalizationManager_Load(void *, char *, int); extern void eng_TGLocalizationManager_Unload(void *, void *); extern void *eng_TGLocalizationDatabase_GetString(void *, char *);
    UI *m = as_subs(p); void *sh = m ? eng_STSubsystemMenu_GetShip(m) : NULL, *prop, *db, *s; long genus;
    if (!sh || !(prop = eng_ShipClass_GetShipProperty(sh))) return;
    genus = eng_ShipProperty_GetGenus(prop);
    db = eng_TGLocalizationManager_Load(NULL, "data/TGL/Bridge Menus.tgl", 1);
    s = db ? eng_TGLocalizationDatabase_GetString(db, genus == 1 ? "Unknown Ship" : genus == 2 ? "Unknown Station" : "Unknown Object") : NULL;
    if (s) tg_str_assign(m->text, s);                                                      /* the database keeps its string */
    if (db) eng_TGLocalizationManager_Unload(NULL, db);
}
/* the colour of a target list entry's arrow: the group of its ship (FUN_00436250: friendly, enemy, neutral, unknown); NULL for other menus */
const float *tg_affiliation_color(void *o);
const float *tg_subsystem_menu_colour(const void *node)
{
    const UI *m = node; void *sh;
    if (!m || !tg_is_type((TGObject *)&m->o, CT_SUBSYSMENU)) return NULL;
    sh = live_obj(((const SubsX *)m->ext)->ship);
    return sh ? tg_affiliation_color(sh) : NULL;
}
/* The subsystem list of a target entry (STSubsystemMenu::BuildSubsystemList 00535590, STComponentMenu ctor 005364b0 and BuildItemList 005367c0).
   Each targetable subsystem of the ship (or one with a targetable child) that is not the hull and is not destroyed becomes a component menu in the
   entry; a component menu with two or more children opens onto one button per targetable child, one with fewer is a plain row. A click on a row
   (ET_SUBSYSTEM_LIST_CLICKED to the entry) or a child button (ET_COMPONENT_LIST_CLICKED to its menu) targets the ship and that subsystem. */
extern long eng_ShipSubsystem_IsTargetable(void *), eng_ShipSubsystem_HasTargetableChildSubsystem(void *), eng_ShipSubsystem_GetNumChildSubsystems(void *);
extern void *eng_ShipSubsystem_GetChildSubsystem(void *, int); extern double eng_ShipSubsystem_GetCombinedConditionPercentage(void *); extern void *eng_ShipSubsystem_GetDisplayName(void *);
extern void *eng_ShipClass_GetHull(void *); extern void tg_ship_change_target(void *, void *, void *); extern int tg_ship_top_subsystems(void *, void ***);
typedef struct { MenuX m; unsigned ship; } CompX;
static UI *as_comp(void *p) { return as_cls(p, 0x80f4); }
static int sub_listable(void *sub) { return sub && (eng_ShipSubsystem_IsTargetable(sub) || eng_ShipSubsystem_HasTargetableChildSubsystem(sub)) && eng_ShipSubsystem_GetCombinedConditionPercentage(sub) > 0; }
static void *ev_sub_obj(TGEvent *e) { void *p = e->v.p; return p && tg_is_ours(p) && !((TGObject *)p)->dead ? p : NULL; }
static void target_ship_sub(unsigned ship_id, void *sub)                                       /* the player's target becomes the ship and the subsystem */
{
    void *pl = game_player(), *sh = live_obj(ship_id);
    if (!pl || !sh) return;
    tg_ship_change_target(pl, sh, sub && eng_ShipSubsystem_IsTargetable(sub) ? sub : NULL);
}
static void comp_on_item(TGEvent *e)                                                           /* STComponentMenu::TargetButtonClicked 005369c0 */
{
    UI *cm = as_comp(e->dest), *it = (UI *)e->source; void *sub = ev_sub_obj(e); int i;
    if (!cm || !sub) return;
    for (i = 0; i < menu_sub(cm)->nkids; i++) { UI *c = menu_sub(cm)->kids[i]; if (c != it && as_btn(c)) eng_STButton_SetChosen(c, 0, 0); }
    if (it && as_btn(it)) eng_STButton_SetChosen(it, 1, 0);
    target_ship_sub(((CompX *)cm->ext)->ship, sub);
}
static void subs_on_list(TGEvent *e)                                                           /* the entry's handler for a component row */
{
    UI *en = as_subs(e->dest), *cm = (UI *)e->source, *sp; void *sub = ev_sub_obj(e); int i;
    if (!en) return;
    sp = menu_sub(en);
    for (i = 0; sp && i < sp->nkids; i++) { UI *c = sp->kids[i]; if (as_comp(c) && c != cm) eng_STMenu_SetChosen(c, 0, 0); }
    if (cm && as_comp(cm)) eng_STMenu_SetChosen(cm, 1, 0);
    target_ship_sub(((SubsX *)en->ext)->ship, sub);
}
static void subs_on_list_fwd(TGEvent *e) { subs_on_list(e); }
static UI *comp_new(void *sub, void *ship, UI *entry)
{
    void *nm = eng_ShipSubsystem_GetDisplayName(sub); UI *m = menu_new("STComponentMenu", nm, 0x60000000u); CompX *x = calloc(1, sizeof *x); TGEvent *ev; int i, n, kids = 0;
    if (nm) eng_delete_TGString(nm);
    free(m->ext); m->ext = x; x->ship = ((TGObject *)ship)->id;
    ev = tg_ev_new("TGObjPtrEvent", 0); ev->type = (unsigned)tg_const("ET_SUBSYSTEM_LIST_CLICKED"); ev->dest = &entry->o; ev->ref_id = ((TGObject *)sub)->id; ev->v.p = sub; x->m.act_ev = ev;
    tg_register_native(m, (int)tg_const("ET_COMPONENT_LIST_CLICKED"), comp_on_item);
    n = (int)eng_ShipSubsystem_GetNumChildSubsystems(sub);
    if (n >= 2) {
        for (i = 0; i < n; i++) {
            void *c = eng_ShipSubsystem_GetChildSubsystem(sub, i), *cn; UI *b; TGEvent *ce;
            if (!sub_listable(c)) continue;
            cn = eng_ShipSubsystem_GetDisplayName(c);
            ce = tg_ev_new("TGObjPtrEvent", 0); ce->type = (unsigned)tg_const("ET_COMPONENT_LIST_CLICKED"); ce->dest = &m->o; ce->ref_id = ((TGObject *)c)->id; ce->v.p = c;
            b = btn_new("STButton", cn, ce, 0x8000000u); if (cn) eng_delete_TGString(cn);
            ce->source = &b->o; pane_add(menu_sub(m), menu_sub(m)->nkids, b, 0, 0, 0); kids++;
        }
    }
    if (!kids) m->flags &= ~(MF_OPENABLE | MF_CLOSEABLE);          /* a plain row: a click only targets */
    else eng_STMenu_Close(m, 0);
    return m;
}
void tg_subs_build_list(UI *en)                                                                 /* BuildSubsystemList */
{
    void *ship = live_obj(((SubsX *)en->ext)->ship), *hull, **top; int n, i, any = 0; UI *sp = menu_sub(en);
    if (!ship || !sp) return;
    while (sp->nkids) eng_TGPane_DeleteChild(sp, sp->kids[0], 0);
    hull = eng_ShipClass_GetHull(ship); n = tg_ship_top_subsystems(ship, &top);
    for (i = 0; i < n; i++) if (sub_listable(top[i]) && top[i] != hull) { UI *c = comp_new(top[i], ship, en); pane_add(sp, sp->nkids, c, 0, 0, 0); any = 1; }
    free(top);
    { int was = (en->flags & MF_OPENED) != 0;
      if (!any) en->flags &= ~MF_OPENABLE; else { en->flags |= MF_OPENABLE | MF_CLOSEABLE; if (was) eng_STMenu_Open(en, 0); else eng_STMenu_Close(en, 0); } }
}
static int tm_find(TgtX *x, void *o) { int i; for (i = 0; o && i < x->n; i++) if (x->e[i].id == ((TGObject *)o)->id) return i; return -1; }   /* FindObjectIndex 00538c50 */
static UI *tm_entry(UI *m, void *o)                                                          /* GetObjectEntry 00538b00 */
{
    UI *sp = menu_sub(m); int i; if (!sp || !o) return NULL;
    for (i = 0; i < sp->nkids; i++) { UI *c = as_subs(sp->kids[i]); if (c && ((SubsX *)c->ext)->ship == ((TGObject *)o)->id) return c; }
    return NULL;
}
static void tm_event(const char *et, void *src, void *dst) { TGEvent *e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const(et); e->source = src; e->dest = dst; tg_add_event(e); }
static void tm_add(UI *m, void *o)                                                           /* AddObjectEntry 00538790 */
{
    extern long eng_ShipClass_CanTargetObject(void *, void *), eng_DamageableObject_IsDead(void *), eng_SensorSubsystem_IsObjectKnown(void *, void *);
    extern void *eng_ShipClass_GetSensorSubsystem(void *);
    TgtX *x = m->ext; void *pl = game_player(), *sens; UI *e; TGEvent *ev;
    if (!pl || !eng_ShipClass_CanTargetObject(pl, o) || eng_DamageableObject_IsDead(o) || tm_entry(m, o) || !tg_is_type(o, 0x8008)) return;
    ev = tg_ev_new("TGIntEvent", 0); ev->type = (unsigned)tg_const("ET_TARGET_LIST_CLICKED"); ev->dest = &m->o; ev->v.i = ((TGObject *)o)->id;
    e = subs_new(o, ev);
    sens = eng_ShipClass_GetSensorSubsystem(pl);
    if (sens && eng_SensorSubsystem_IsObjectKnown(sens, o)) eng_STSubsystemMenu_ShowRealName(e); else eng_STSubsystemMenu_ShowUnknownName(e);
    pane_add(menu_sub(m), menu_sub(m)->nkids, e, 0, 0, 0); tg_subs_build_list(e);
    x->e = realloc(x->e, (x->n + 1) * sizeof *x->e); x->e[x->n].id = ((TGObject *)o)->id; x->e[x->n].keep = 0; x->n++;
    tm_event("ET_TARGET_LIST_OBJECT_ADDED", e, m);
}
static void tm_remove_entry(UI *m, int i)                                                    /* RemoveObjectEntry 00538a50 */
{
    extern void eng_TGPane_DeleteChild(void *, void *, int); TgtX *x = m->ext; void *o = live_obj(x->e[i].id); UI *c = o ? tm_entry(m, o) : NULL;
    if (!c) { UI *sp = menu_sub(m); int k; for (k = 0; sp && k < sp->nkids; k++) { UI *d = as_subs(sp->kids[k]); if (d && ((SubsX *)d->ext)->ship == x->e[i].id) c = d; } }
    if (c) eng_TGPane_DeleteChild(menu_sub(m), c, 0);
    memmove(x->e + i, x->e + i + 1, (x->n - i - 1) * sizeof *x->e); x->n--;
}
static void tm_select(UI *m, void *o)                                                        /* SelectObject 005386c0 */
{
    UI *sp = menu_sub(m), *e = o ? tm_entry(m, o) : NULL; int i;
    for (i = 0; sp && i < sp->nkids; i++) if (as_subs(sp->kids[i])) eng_STMenu_SetChosen(sp->kids[i], 0, 0);
    if (e) { eng_STMenu_SetChosen(e, 1, 0); pane_set_focus(sp, e); }
}
static void tm_restore_persistent(UI *m)                                                     /* RestorePersistentTarget 00538fc0 */
{
    extern void *eng_ShipClass_GetTarget(void *); extern long eng_DamageableObject_IsDead(void *); extern void tg_ship_change_target(void *, void *, void *);
    TgtX *x = m->ext; void *pl = game_player(), *o;
    if (!pl || eng_ShipClass_GetTarget(pl) || !x->persist) return;
    if ((o = live_obj(x->persist)) && !eng_DamageableObject_IsDead(o)) { tm_event("ET_RESTORE_PERSISTENT_TARGET", m, pl); tg_ship_change_target(pl, o, live_obj(x->persist_sub)); }
    x->persist = x->persist_sub = 0;
}
static void tm_refresh(UI *m)                                                                /* RefreshTargetList 00538c90 */
{
    extern PyObject *eng_SetClass_GetTargetableObjects(void *, void *, int); extern long eng_ShipClass_CanTargetObject(void *, void *), eng_DamageableObject_IsDead(void *);
    extern void *eng_ShipClass_GetSensorSubsystem(void *); extern long eng_SensorSubsystem_IsObjectKnown(void *, void *);
    TgtX *x = m->ext; void *pl = game_player(), *set, *sens; PyObject *t; int i, n;
    if (!pl || !(sens = eng_ShipClass_GetSensorSubsystem(pl)) || !(set = eng_BaseObjectClass_GetContainingSet(pl))) return;
    for (i = 0; i < x->n; i++) x->e[i].keep = 0;
    t = eng_SetClass_GetTargetableObjects(set, pl, 0); n = t ? PyTuple_Size(t) : 0;
    for (i = 0; i < n; i++) {
        void *o = ui_arg_ptr(PyTuple_GetItem(t, i)); int k = tm_find(x, o);
        if (!o) continue;
        if (k < 0) { tm_add(m, o); if ((k = tm_find(x, o)) >= 0) x->e[k].keep = 1; }
        else x->e[k].keep = eng_ShipClass_CanTargetObject(pl, o) && !eng_DamageableObject_IsDead(o);
    }
    Py_XDECREF(t);
    for (i = x->n - 1; i >= 0; i--) {
        void *o = live_obj(x->e[i].id); UI *e;
        if (!o || !x->e[i].keep) { if (o) tm_event("ET_TARGET_LIST_OBJECT_REMOVED", m, o); tm_remove_entry(m, i); continue; }   /* RemoveObject 005389d0 */
        if ((e = tm_entry(m, o))) { if (eng_SensorSubsystem_IsObjectKnown(sens, o)) eng_STSubsystemMenu_ShowRealName(e); else eng_STSubsystemMenu_ShowUnknownName(e); }
    }
    tm_restore_persistent(m);
}
static void tm_clear(UI *m)                                                                  /* ClearTargetList 00538b40 */
{ TgtX *x = m->ext; while (x->n) tm_remove_entry(m, x->n - 1); }
static void tm_on_subsystem_change(TGEvent *e)                                               /* HandleSubsystemDestroyed 00535e70, HandleSubsystemRebuilt: the list of the ship changes */
{
    UI *sp; int i; if (!target_menu || target_menu->o.dead) return;
    sp = menu_sub(target_menu);
    for (i = 0; sp && i < sp->nkids; i++) { UI *en = as_subs(sp->kids[i]); if (en) tg_subs_build_list(en); }
}
static void tm_on_name_change(TGEvent *e)                                                    /* STSubsystemMenu::HandleNameChange 00536410 */
{
    extern void *eng_ShipClass_GetSensorSubsystem(void *); extern long eng_SensorSubsystem_IsObjectKnown(void *, void *);
    void *pl = game_player(), *sens, *sh = e->source; UI *en;
    if (!target_menu || target_menu->o.dead || !pl || !sh || !tg_is_ours(sh) || !tg_is_type(sh, 0x8008) || !(sens = eng_ShipClass_GetSensorSubsystem(pl))) return;
    if ((en = tm_entry(target_menu, sh)) && eng_SensorSubsystem_IsObjectKnown(sens, sh)) eng_STSubsystemMenu_ShowRealName(en);
}
static void tm_on_refresh(TGEvent *e) { if (target_menu && !target_menu->o.dead) tm_refresh(target_menu); }
static void tm_on_entered(TGEvent *e)                                                        /* ObjectEnteredSet 00538280 */
{
    void *pl = game_player(), *o = e->dest;
    if (!target_menu || target_menu->o.dead || !pl || !o || !tg_is_ours(o) || !tg_is_type(o, 0x8003) || eng_BaseObjectClass_GetContainingSet(pl) != eng_BaseObjectClass_GetContainingSet(o)) return;
    if (o == pl) tm_clear(target_menu);
    tm_refresh(target_menu);
}
static void tm_on_identified(TGEvent *e)                                                     /* ObjectIdentified 005384c0 */
{
    void *o = live_obj(e->ref_id); UI *en; TgtX *x; int k;
    if (!target_menu || target_menu->o.dead || !o) return;
    x = target_menu->ext;
    if ((k = tm_find(x, o)) < 0) { tm_add(target_menu, o); k = tm_find(x, o); }
    if (k >= 0) x->e[k].keep = 1;
    if ((en = tm_entry(target_menu, o))) eng_STSubsystemMenu_ShowRealName(en);
}
static void tm_on_target_changed(TGEvent *e)                                                 /* TargetChanged 00538690 -> FUN_00538600 */
{
    extern void *eng_ShipClass_GetTarget(void *); void *pl = game_player(), *t;
    if (!target_menu || target_menu->o.dead || !pl || e->dest != pl) return;
    t = eng_ShipClass_GetTarget(pl); tm_select(target_menu, t);
    if (t) ((TgtX *)target_menu->ext)->persist = ((TGObject *)t)->id;
}
static void tm_on_clicked(TGEvent *e)                                                        /* TargetButtonClicked 00538590 */
{
    extern long eng_ShipClass_CanTargetObject(void *, void *); extern void *eng_ShipClass_GetTarget(void *); extern void tg_ship_change_target(void *, void *, void *);
    UI *m = as_tm(e->dest); void *pl = game_player(), *o = live_obj((unsigned)e->v.i);
    if (!m || !pl || !o || eng_BaseObjectClass_GetContainingSet(o) != eng_BaseObjectClass_GetContainingSet(pl)) return;
    if (eng_ShipClass_CanTargetObject(pl, o) && eng_ShipClass_GetTarget(pl) != o) { tg_ship_change_target(pl, o, NULL); tm_select(m, o); ((TgtX *)m->ext)->persist = ((TGObject *)o)->id; }
}
void tg_subs_build_list(UI *en);
void *eng_STTargetMenu_CreateW(void *name)
{
    static int registered; UI *m = menu_new("STTargetMenu", name, 0x60000000u), *s = menu_sub(m); TgtX *x = calloc(1, sizeof *x);
    free(m->ext); m->ext = x;
    if (s) s->flags |= SP_RADIO;
    target_menu = m;
    tg_register_native(m, (int)tg_const("ET_TARGET_LIST_CLICKED"), tm_on_clicked);
    if (!registered) {                                                                        /* broadcast handlers (the exe binds them per menu) */
        registered = 1;
        tg_register_native_broadcast((int)tg_const("ET_ENTERED_SET"), tm_on_entered);
        tg_register_native_broadcast((int)tg_const("ET_EXITED_SET"), tm_on_refresh);
        tg_register_native_broadcast((int)tg_const("ET_OBJECT_EXPLODING"), tm_on_refresh);
        tg_register_native_broadcast((int)tg_const("ET_CLOAK_COMPLETED"), tm_on_refresh);
        tg_register_native_broadcast((int)tg_const("ET_DECLOAK_COMPLETED"), tm_on_refresh);
        tg_register_native_broadcast((int)tg_const("ET_SENSORS_SHIP_IDENTIFIED"), tm_on_identified);
        tg_register_native_broadcast((int)tg_const("ET_TARGET_WAS_CHANGED"), tm_on_target_changed);
        tg_register_native_broadcast((int)tg_const("ET_SUBSYSTEM_COMPLETELY_DESTROYED"), tm_on_subsystem_change);
        tg_register_native_broadcast((int)tg_const("ET_SUBSYSTEM_REBUILT"), tm_on_subsystem_change);
        tg_register_native_broadcast((int)tg_const("ET_NAME_CHANGE"), tm_on_name_change);
    }
    tm_clear(m); tm_refresh(m); tm_select(m, NULL);
    return m;
}
void *eng_STTargetMenu_Cast(PyObject *o) { return as_tm(ui_arg_ptr(o)); }
void *eng_STTargetMenu_GetTargetMenu(void) { return tcw ? ((TcwX *)tcw->ext)->disp[4] : NULL; }    /* 00538260: TacticalControlWindow +0xe0 */
void eng_STTargetMenu_ClearTargetList(void *p) { UI *m = as_tm(p); if (m) tm_clear(m); }
void eng_STTargetMenu_ClearPersistentTarget(void *p) { UI *m = as_tm(p); if (m) ((TgtX *)m->ext)->persist = ((TgtX *)m->ext)->persist_sub = 0; }   /* 00538b30 */
void eng_STTargetMenu_ResetAffiliationColors(void *p) { }
void eng_STTargetMenu_RebuildShipMenus(void *p) { }
void *eng_STTargetMenu_GetObjectEntry(void *p, void *o) { UI *m = as_tm(p); return m ? tm_entry(m, o) : NULL; }

/* Provisional constructor for UI classes that are not written yet (re/gen_ui_stubs.py): build the object like its nearest written base class,
   so that the base class methods work (STMenu family: menu with sub pane; STButton family: button; STStylizedWindow: stylized window). */
void *ui_st_make(const char *cls, void *name, float w, float h)
{
    extern void *tg_sub_make(const char *); extern void *tg_space_new_obj(const char *, const char *);
    if (tg_class_is(cls, 0x801b)) return tg_sub_make(cls);
    if (tg_class_is(cls, 0x8002)) return tg_space_new_obj(cls, NULL);         /* any BaseObjectClass */
    int menu = tg_class_is(cls, CT_STMENU), btn = tg_class_is(cls, CT_STBUTTON), stw = tg_class_is(cls, 0x80f7), sub = tg_class_is(cls, CT_STSUBPANE);
    if (menu) return menu_new(cls, name, 0x60000000u);
    if (btn) return btn_new(cls, name, NULL, 0);
    if (stw) return stw_new(cls, "StylizedWindow", "NormalStyle", name, 0, 0, NULL, 0, 0, 0, NULL);
    if (sub) { UI *s = pane_new(cls, w, h); SubX *x = calloc(1, sizeof *x); x->spacing = 0.003f; s->ext = x; return s; }
    return pane_new(cls, w, h);
}

/* ---------------------------------------------- SortedRegionMenu (00516b80 ..) ---------------------------------------------- */
/* A Set Course menu item for a region (system or set): region module (+0x5c), episode (+0x60), mission (+0x64), placement (+0x68),
   large loading screen flag (+0x6c). The first one made is the root (DAT_00987f64); SetWarpButton names the warp button (DAT_00987f60).
   Approximations (marked): sorting, the location icons and UpdateItems (00517a00) are visual and not done. */
#define CT_SRM 0x80fc
#define CT_WARPBTN 0x80fd
typedef struct { MenuX m; char *region, *episode, *mission, *placement; int large; } SrmX;
static UI *srm_root, *warp_button; static int srm_paused;
static char *sdup(const char *s) { return s ? strdup(s) : NULL; }
static void sset(char **f, const char *v) { free(*f); *f = sdup(v); }
static UI *as_srm(void *p) { return as_cls(p, CT_SRM); }
static UI *srm_new(void *name, const char *region, const char *placement)                                   /* CreateW 00516b80 */
{
    UI *m = menu_new("SortedRegionMenu", name, 0x60000000u); SrmX *x = calloc(1, sizeof *x);
    free(m->ext); m->ext = x;
    x->region = sdup(region); x->placement = sdup(placement);
    m->flags &= ~MF_OPENABLE;
    if (!srm_root) srm_root = m;                                                                              /* the ET_SET_PLAYER handler of the root only updates icons */
    return m;
}
void *eng_SortedRegionMenu_CreateW(void *name, char *region, char *placement) { return srm_new(name, region, placement); }
void *eng_SortedRegionMenu_Create(char *name, char *region, char *placement) { return srm_new(eng_new_TGString(name ? name : ""), region, placement); }
void *eng_SortedRegionMenu_Cast(PyObject *o) { return as_srm(ui_arg_ptr(o)); }
void *eng_SortedRegionMenu_CreateNull(void) { return NULL; }
void *eng_SortedRegionMenu_GetRoot(void) { return srm_root; }
void eng_SortedRegionMenu_SetWarpButton(void *b) { warp_button = as_cls(b, CT_WARPBTN); }
void *eng_SortedRegionMenu_GetWarpButton(void) { return warp_button; }
#define SRM_STR(F, f) void eng_SortedRegionMenu_Set##F(void *p, char *v) { UI *m = as_srm(p); if (m) sset(&((SrmX *)m->ext)->f, v); } \
    const char *eng_SortedRegionMenu_Get##F(void *p) { UI *m = as_srm(p); return m ? ((SrmX *)m->ext)->f : NULL; }
SRM_STR(RegionName, region) SRM_STR(EpisodeName, episode) SRM_STR(MissionName, mission) SRM_STR(PlacementName, placement)
void eng_SortedRegionMenu_SetUseLargeLoadingScreen(void *p, int v) { UI *m = as_srm(p); if (m) ((SrmX *)m->ext)->large = v != 0; }
void eng_SortedRegionMenu_ClearInfo(void *p) { UI *m = as_srm(p); SrmX *x; if (!m) return; x = m->ext; sset(&x->episode, NULL); sset(&x->mission, NULL); }   /* 00517ff0 */
void eng_SortedRegionMenu_ClearSetCourseMenu(void) { }
void *eng_SortedRegionMenu_InitializeAllSets(void *p) { return NULL; }
void eng_SortedRegionMenu_SetPauseSorting(int v) { srm_paused = v != 0; }
long eng_SortedRegionMenu_IsSortingPaused(void) { return srm_paused; }
static void warp_set_destination(UI *b, const char *dest, const char *mission, const char *placement, const char *episode, unsigned btn_id);
void eng_SortedRegionMenu_SetWarpToHere(void *p)                                                              /* 005176a0 */
{
    UI *m = as_srm(p); SrmX *x; if (!m || !warp_button) return; x = m->ext;
    warp_set_destination(warp_button, x->region, x->mission, x->placement, x->episode, m->o.id);
    { extern void tg_warpbtn_set_large(UI *, int); tg_warpbtn_set_large(warp_button, x->large); }
}
/* the menu was clicked (vtbl slot, 00517510): a region item that is not the current location (or a leaf at it) becomes the warp
   destination; a leaf item then tells Helm's menu (ET_SET_COURSE, int 0, source = this menu) and is chosen */
static void srm_click(UI *m)
{
    SrmX *x = m->ext; UI *b = warp_button; const char *loc = NULL;
    extern void *eng_SetManager_GetSet(void *, char *); extern void *eng_CharacterClass_GetObject(void *, char *); extern void *eng_CharacterClass_GetMenu(void *);
    int has_kids = menu_sub(m) && menu_sub(m)->nkids > 0, openable = (m->flags & MF_OPENABLE) != 0;
    if (!x->region || (openable && has_kids)) goto done;
    if (b) { extern const char *tg_warpbtn_location(UI *); loc = tg_warpbtn_location(b); }
    if (loc && !strcmp(x->region, loc)) goto done;
    eng_SortedRegionMenu_SetWarpToHere(m);
    if (!openable || !has_kids) {
        void *bridge = eng_SetManager_GetSet(NULL, "bridge"), *helm = bridge ? eng_CharacterClass_GetObject(bridge, "Helm") : NULL, *menu = helm ? eng_CharacterClass_GetMenu(helm) : NULL;
        if (menu) { TGEvent *e = tg_ev_new("TGIntEvent", 0); e->type = (unsigned)tg_const("ET_SET_COURSE"); e->v.i = 0; e->source = &m->o; e->dest = menu; tg_add_event(e); }
    }
    eng_STMenu_SetChosen(m, 1, 0);
done:
    if (((MenuX *)m->ext)->update_ev) ui_send_event(((MenuX *)m->ext)->update_ev, m);              /* STMenu::SendUpdateEvent */
}

/* ---------------------------------------------- STWarpButton (0053e600 ..) ---------------------------------------------- */
/* Fields: mission (+0x114), episode (+0x118), location (+0x11c), destination region module (+0x120), placement (+0x124), large loading
   screen (+0x128), destination menu id (+0x12c), warp sequence (+0x130), course menu (+0x134), ship (+0x138), warp time (+0x13c, 4 s),
   five action sequences that HandleWarpEvent puts into the warp sequence (+0x140 before, +0x144 before-during, +0x148 during,
   +0x14c after-during, +0x150 after). The button sends ET_WARP_BUTTON_PRESSED to itself; the native handlers are instance handlers, so
   Python handlers added later run first and reach them through CallNextHandler (the exe binds them by class name). */
typedef struct { BtnX b; char *mission, *episode, *location, *dest, *placement; int large; unsigned dest_btn, seq, ship; void *course_menu; float warp_time; void *bda[5]; } WarpX;
static UI *as_wb(void *p) { return as_cls(p, CT_WARPBTN); }
void tg_warpbtn_set_large(UI *b, int v) { if (as_wb(b)) ((WarpX *)b->ext)->large = v; }
const char *tg_warpbtn_location(UI *b) { return as_wb(b) ? ((WarpX *)b->ext)->location : NULL; }
static int seq_playing(unsigned id) { extern long eng_TGAction_IsPlaying(void *); TGObject *o = id ? tg_find(id) : NULL; return o && !o->dead && eng_TGAction_IsPlaying(o); }
static void warp_set_destination(UI *b, const char *dest, const char *mission, const char *placement, const char *episode, unsigned btn_id)   /* 0053e990 */
{
    WarpX *x = b->ext; extern void eng_TGUIObject_SetEnabled(void *, int), eng_TGUIObject_SetDisabled(void *, int);
    if (dest && x->location && !strcmp(x->location, dest)) { sset(&x->dest, NULL); eng_TGUIObject_SetDisabled(b, 0); return; }
    sset(&x->dest, dest); sset(&x->mission, mission); sset(&x->episode, episode); sset(&x->placement, placement);
    if (x->course_menu && x->dest) { if (seq_playing(x->seq)) eng_TGUIObject_SetDisabled(b, 0); else eng_TGUIObject_SetEnabled(b, 0); x->dest_btn = btn_id; return; }
    eng_TGUIObject_SetDisabled(b, 0);
}
static PyObject *py_none(void) { Py_INCREF(Py_None); return Py_None; }
static PyObject *call_mod_args(const char *mod, const char *fn, PyObject *args)
{
    PyObject *m = PyImport_ImportModule((char *)mod), *f, *r = NULL;
    if (m) { f = PyObject_GetAttrString(m, (char *)fn); Py_DECREF(m); if (f) { r = PyEval_CallObject(f, args); Py_DECREF(f); } }
    Py_XDECREF(args); if (!r || PyErr_Occurred()) { PyErr_Print(); PyErr_Clear(); }
    return r;
}
static void *bda_seq(WarpX *x, int i) { extern void *eng_TGSequence_Create(void); if (!x->bda[i]) x->bda[i] = eng_TGSequence_Create(); return x->bda[i]; }
static void bda_add(void *p, int i, void *a, float d) { extern void eng_TGSequence_AppendAction(void *, void *, float); UI *b = as_wb(p); if (b && a) eng_TGSequence_AppendAction(bda_seq(b->ext, i), a, d); }
static void *script_act(const char *mod, const char *fn) { extern PyObject *eng_TGScriptAction_Create(PyObject *); PyObject *r = eng_TGScriptAction_Create(Py_BuildValue("(ss)", mod, fn)); void *p = ui_arg_ptr(r); Py_XDECREF(r); return p; }
/* HandleWarpEvent 0053f000 */
static void warp_pressed(TGEvent *e)
{
    UI *b = e->dest ? as_wb(e->dest) : NULL; WarpX *x; void *pl, *seq, *load = NULL; PyObject *r, *ai;
    extern void *eng_Game_GetCurrentPlayer(void); extern void *tg_warp_sequence_new(void *, const char *, float, const char *);
    extern void tg_warp_sequence_set_event_dest(void *, void *), tg_warp_sequence_set_episode(void *, const char *, const char *);
    extern void eng_WarpSequence_AddActionBeforeWarp(void *, void *, float), eng_WarpSequence_AddActionDuringWarp(void *, void *, float), eng_WarpSequence_AddActionAfterWarp(void *, void *, float);
    extern void eng_TGSequence_AppendAction(void *, void *, float);
    if (!b) return; x = b->ext;
    if (x->dest_btn) { UI *m = as_srm(tg_find(x->dest_btn)); if (m) eng_SortedRegionMenu_SetWarpToHere(m); }
    if (!x->dest || !(pl = eng_Game_GetCurrentPlayer())) return;
    r = call_mod_args(x->dest, "GetSet", PyTuple_New(0));                                     /* callScriptGetSet; Initialize() makes the set */
    if (!r || r == Py_None || !ui_arg_ptr(r)) { Py_XDECREF(r); r = call_mod_args(x->dest, "Initialize", PyTuple_New(0)); }
    Py_XDECREF(r);
    seq = tg_warp_sequence_new(pl, x->dest, x->warp_time, x->placement);
    if (!seq) return;
    x->seq = ((TGObject *)seq)->id;
    tg_warp_sequence_set_event_dest(seq, b);                                                   /* vslot 0x60: ET_EXITED_WARP comes back to the button */
    if (x->mission || x->episode) {                                                            /* another mission or episode loads during the warp */
        extern void *eng_LoadMissionAction_Create(char *), *eng_LoadEpisodeAction_Create(char *);
        if (x->large) bda_add(b, 2, script_act("MissionLib", "ShowLargeLoadingScreen"), 0.0f);
        bda_add(b, 2, script_act("MissionLib", "ShowLoadingText"), 0.0f);                       /* the exe skips it when the name is the current one */
        load = x->episode ? eng_LoadEpisodeAction_Create(x->episode) : eng_LoadMissionAction_Create(x->mission);   /* not ported yet: NULL */
        if (load) bda_add(b, 2, load, 0.1f);
        tg_warp_sequence_set_episode(seq, x->episode, x->episode ? NULL : x->mission);
        if (x->large) bda_add(b, 2, script_act("MissionLib", "HideLargeLoadingScreen"), 0.0f);
    }
    x->large = 0;
    if (x->bda[0]) eng_WarpSequence_AddActionBeforeWarp(seq, x->bda[0], 0.0f);
    if (x->bda[1]) { if (x->bda[2]) eng_TGSequence_AppendAction(x->bda[1], x->bda[2], 0.0f); if (x->bda[3]) eng_TGSequence_AppendAction(x->bda[1], x->bda[3], 0.0f); eng_WarpSequence_AddActionDuringWarp(seq, x->bda[1], 0.0f); }
    else { if (x->bda[3]) eng_TGSequence_AppendAction(bda_seq(x, 2), x->bda[3], 0.0f); if (x->bda[2]) eng_WarpSequence_AddActionDuringWarp(seq, x->bda[2], 0.0f); }
    if (x->bda[4]) eng_WarpSequence_AddActionAfterWarp(seq, x->bda[4], 0.0f);
    memset(x->bda, 0, sizeof x->bda);
    ai = call_mod_args("AI.Player.PlayerWarp", "CreateAI", Py_BuildValue("(NN)", tg_obj_pyobj(pl), tg_obj_pyobj(seq)));
    if (ai && ai != Py_None) { r = call_mod_args("MissionLib", "SetPlayerAI", Py_BuildValue("(sO)", "Helm", ai)); Py_XDECREF(r); }
    Py_XDECREF(ai);
}
/* ExitedWarpHandler 0053f5a0: the location becomes the region module of the player's (or the button ship's) set */
static void warp_exited(TGEvent *e)
{
    UI *b = e->dest ? as_wb(e->dest) : NULL; WarpX *x; void *s, *o; extern void *eng_Game_GetCurrentPlayer(void); extern const char *eng_SetClass_GetRegionModule(void *);
    extern void eng_TGUIObject_SetEnabled(void *, int), eng_TGUIObject_SetDisabled(void *, int);
    if (!b) return; x = b->ext;
    sset(&x->location, NULL);
    if (!(o = eng_Game_GetCurrentPlayer())) return;
    if (x->ship) { TGObject *sh = tg_find(x->ship); if (sh && !sh->dead) o = sh; }
    if ((s = eng_BaseObjectClass_GetContainingSet(o))) sset(&x->location, eng_SetClass_GetRegionModule(s));
    if (x->dest && x->location && !strcmp(x->dest, x->location)) { sset(&x->dest, NULL); eng_TGUIObject_SetDisabled(b, 1); }
    if (x->dest) eng_TGUIObject_SetEnabled(b, 1);
    sset(&x->episode, NULL); sset(&x->mission, NULL); x->seq = 0;
}
static UI *wb_new(void *name)                                                                 /* Create 0053e600 */
{
    TGEvent *e = tg_ev_new("TGEvent", 0); UI *b; WarpX *x = calloc(1, sizeof *x);
    e->type = (unsigned)tg_const("ET_WARP_BUTTON_PRESSED");
    b = btn_new("STWarpButton", name, e, 0x80000000u); free(b->ext); b->ext = x; x->b.act_ev = e;
    e->source = &b->o; e->dest = &b->o;
    x->warp_time = 4.0f;
    b->flags &= ~UF_ENABLED;
    tg_register_native(b, (int)e->type, warp_pressed);
    tg_register_native(b, (int)tg_const("ET_EXITED_WARP"), warp_exited);
    return b;
}
void *eng_STWarpButton_CreateW(void *name) { return wb_new(name); }
void *eng_STWarpButton_Create(char *name) { return wb_new(eng_new_TGString(name ? name : "")); }
void *eng_STWarpButton_Cast(PyObject *o) { return as_wb(ui_arg_ptr(o)); }
void eng_STWarpButton_SetCourseMenu(void *p, void *m) { UI *b = as_wb(p); if (b) ((WarpX *)b->ext)->course_menu = m; }
void eng_STWarpButton_SetWarpTime(void *p, float t) { UI *b = as_wb(p); if (b) ((WarpX *)b->ext)->warp_time = t; }
double eng_STWarpButton_GetWarpTime(void *p) { UI *b = as_wb(p); return b ? ((WarpX *)b->ext)->warp_time : 0.0; }
void eng_STWarpButton_SetShip(void *p, void *s) { UI *b = as_wb(p); if (b) ((WarpX *)b->ext)->ship = s && tg_is_ours(s) ? ((TGObject *)s)->id : 0; }
void eng_STWarpButton_AddActionBeforeWarp(void *p, void *a, float d) { bda_add(p, 0, a, d); }
void eng_STWarpButton_AddActionBeforeDuringWarp(void *p, void *a, float d) { bda_add(p, 1, a, d); }
void eng_STWarpButton_AddActionDuringWarp(void *p, void *a, float d) { bda_add(p, 2, a, d); }
void eng_STWarpButton_AddActionAfterDuringWarp(void *p, void *a, float d) { bda_add(p, 3, a, d); }
void eng_STWarpButton_AddActionAfterWarp(void *p, void *a, float d) { bda_add(p, 4, a, d); }
void eng_STWarpButton_ClearBDASequences(void *p) { UI *b = as_wb(p); int i; if (!b) return; for (i = 0; i < 5; i++) { if (((WarpX *)b->ext)->bda[i]) ((TGObject *)((WarpX *)b->ext)->bda[i])->dead = 1; ((WarpX *)b->ext)->bda[i] = NULL; } }
static const char *opt_str(PyObject *o) { return o && PyString_Check(o) ? PyString_AsString(o) : NULL; }
void eng_STWarpButton_SetDestination(void *p, PyObject *dest, PyObject *mission, PyObject *placement, PyObject *episode, long btn)
{ UI *b = as_wb(p); if (b) warp_set_destination(b, opt_str(dest), opt_str(mission), opt_str(placement), opt_str(episode), (unsigned)btn); }
const char *eng_STWarpButton_GetDestination(void *p) { UI *b = as_wb(p); return b ? ((WarpX *)b->ext)->dest : NULL; }
long eng_STWarpButton_GetDestinationButtonID(void *p) { UI *b = as_wb(p); return b ? ((WarpX *)b->ext)->dest_btn : 0; }
void eng_STWarpButton_SetUseLargeLoadingScreen(void *p, int v) { UI *b = as_wb(p); if (b) ((WarpX *)b->ext)->large = v != 0; }
void eng_STWarpButton_SetLocation(void *p, char *s) { UI *b = as_wb(p); if (b) sset(&((WarpX *)b->ext)->location, s); }
void eng_STWarpButton_SetPlacementName(void *p, char *s) { UI *b = as_wb(p); if (b) sset(&((WarpX *)b->ext)->placement, s); }

/* host: a mouse click on a UI item (there is no input layer yet). SortedRegionMenu items run their click (00517510); enabled buttons send
   their activation event (TGButtonBase); menus send their activation event. */
static PyObject *host_click(PyObject *self, PyObject *args)
{
    PyObject *o; UI *u;
    if (!PyArg_ParseTuple(args, "O", &o)) return NULL;
    u = as_ui(ui_arg_ptr(o));
    if (u && as_srm(u)) srm_click(u);
    else if (u && tg_is_type(&u->o, CT_STMENU)) ui_send_event(((MenuX *)u->ext)->act_ev, u);
    else if (u && (u->flags & UF_ENABLED)) ui_send_event(u->act_ev, u);
    return PyInt_FromLong(u != NULL);
}
void tg_ui_st_register_host(PyObject *dict)
{
    static PyMethodDef m[] = {{"engine_click", host_click, 1}, {NULL, NULL}};
    PyMethodDef *d; for (d = m; d->ml_name; d++) { PyObject *f = PyCFunction_New(d, NULL); PyDict_SetItemString(dict, (char *)d->ml_name, f); Py_DECREF(f); }
}


/* ---------------------------------------------- what a click, a key or a drag does to the widgets (tg_ui_input.c) ---------------------------------------------- */
/* STButton::ButtonClicked 0051a4e0 and STRoundedButton::HandleClick 0052e7a0: an STButton that chooses itself on a click flips its chosen
   state; then ET_ST_BUTTON_CLICKED goes up the parents until one handles it. The activation event of the button is sent first. */
void tg_button_press(UI *b)
{
    extern void eng_TGButtonBase_SendActivationEvent(void *p);
    TGEvent *e; UI *p; int isb;
    isb = tg_is_type((TGObject *)&b->o, CT_STBUTTON) && !tg_is_type((TGObject *)&b->o, CT_STTOGGLE);
    if (!tg_is_type((TGObject *)&b->o, CT_STTOGGLE)) { extern void tg_ui_sound(const char *); tg_ui_sound("UIButtonClicked"); }      /* the click sound comes first */
    if (tg_is_type((TGObject *)&b->o, CT_STTOGGLE)) { eng_TGButtonBase_SendActivationEvent(b); return; }          /* cycles and sends the state's event */
    if (isb && (b->flags & BF_AUTOCHOOSE)) eng_STButton_SetChosen(b, !(b->flags & BF_CHOSEN), 1);
    eng_TGButtonBase_SendActivationEvent(b);
    e = tg_ev_new("TGBoolEvent", 0); e->type = (unsigned)tg_const("ET_ST_BUTTON_CLICKED"); e->source = &b->o;
    for (p = b->parent; p && !(e->flags & 0x100); p = p->parent) { e->dest = &p->o; tg_event_process(p, e); }
    e->o.dead = 1;
}
int tg_is_toggle(const void *node) { const UI *b = node; return b && tg_is_type((TGObject *)&b->o, CT_STTOGGLE); }
/* a toggle to the next state (dir 1) or the previous one (dir -1); the event of the new state is sent */
void tg_toggle_step(UI *b, int dir)
{
    BtnX *x = b->ext; if (!x || x->nstates < 1) return;
    x->state = (x->state + dir + x->nstates) % x->nstates; tog_refresh(b);
    if (x->state_ev[x->state]) ui_send_event(x->state_ev[x->state], b);
}
/* open or close a menu with a click or a key; the menu's own activation event is sent when it opens */
void tg_menu_toggle(UI *m, int want_open)
{
    MenuX *x = m->ext; int was = (m->flags & MF_OPENED) != 0;
    if (!(m->flags & MF_OPENABLE) && (!strcmp(m->o.cls->name, "STComponentMenu") || !strcmp(m->o.cls->name, "STSubsystemMenu"))) {      /* a row that does not open: the click only sends its event */
        if (want_open && x && x->act_ev) ui_send_event(x->act_ev, m);
        return;
    }
    if (want_open == was) return;
    m->flags |= MF_OPENABLE | MF_CLOSEABLE;                  /* a click always works (the flags of the script are not checked: approximation) */
    if (want_open) eng_STMenu_Open(m, 1); else eng_STMenu_Close(m, 1);
    if (x && x->act_ev && want_open) ui_send_event(x->act_ev, m);
}
/* numeric bar: set from a fraction of its width (0 to 1), or step by the key interval; the update event gets the new value */
static void numbar_changed(UI *b)
{
    NumX *x = b->ext; TGEvent *e; extern void *eng_TGEvent_Duplicate(void *);
    if (!x->upd_ev) return;
    numbar_pct(b); e = eng_TGEvent_Duplicate(x->upd_ev); e->v.f = x->val; e->source = &b->o; tg_add_event(e);
}
void tg_numeric_set_fraction(UI *b, float f)
{
    NumX *x = b->ext; float v; if (!x) return;
    if (f < 0) f = 0; if (f > 1) f = 1;
    v = x->lo + f * (x->hi - x->lo);
    if (x->key > 0) v = x->lo + floorf((v - x->lo) / x->key + 0.5f) * x->key;     /* the keys move in steps; the mouse snaps to the same steps */
    if (v > x->hi) v = x->hi; if (v < x->lo) v = x->lo;
    if (v != x->val) { x->val = v; numbar_changed(b); }
}
void tg_numeric_step(UI *b, int dir)
{
    NumX *x = b->ext; float v; if (!x) return;
    v = x->val + dir * (x->key > 0 ? x->key : (x->hi - x->lo) * 0.1f);
    if (v > x->hi) v = x->hi; if (v < x->lo) v = x->lo;
    if (v != x->val) { x->val = v; numbar_changed(b); }
}
int tg_is_numeric_bar(const void *node) { const UI *b = node; return b && tg_is_type((TGObject *)&b->o, 0x80f1); }


/* ---------------------------------------------- ModalDialogWindow ---------------------------------------------- */
/* ModalDialogWindow::Run 00502550: a fixed size stylized window (style SolidGlass, 0.5 x 0.35 at 0.25, 0.375) with the text, and an OK and an
   optional Cancel button; a click sends ET_OKAY or ET_CANCEL to the window, which sends the stored event and hides the dialog. While the dialog
   shows, the input goes to it (tg_ui_input.c takes the first visible modal window as the context). Approximation: the button places are the
   original's by their decompiled formulae, the paragraph wraps at the window width. */
typedef struct { void *ok_ev, *cancel_ev; UI *ok, *cancel; } ModalX;
extern void eng_TGPane_MoveToFront(void *p, void *c, int l);
extern void *eng_TopWindow_GetTopWindow(void);
extern void tg_ui_focus_set(UI *n);
static ModalX *modal_x(UI *d) { if (!d->ext) d->ext = calloc(1, sizeof(ModalX)); return d->ext; }
static void modal_send(UI *d, void *ev)
{
    extern void *eng_TGEvent_Duplicate(void *);
    if (ev) { TGEvent *e = eng_TGEvent_Duplicate(ev); if (e) tg_add_event(e); }
}
static void modal_hide(UI *d) { ui_hide(d, 1); }
static void modal_okay(TGEvent *e) { UI *d = (UI *)e->dest; ModalX *x = modal_x(d); modal_send(d, x->ok_ev); modal_hide(d); }
static void modal_cancel(TGEvent *e) { UI *d = (UI *)e->dest; ModalX *x = modal_x(d); modal_send(d, x->cancel_ev); modal_hide(d); }
void eng_ModalDialogWindow_Cancel(void *self) { UI *d = self; ModalX *x; if (!d || !d->ext) return; x = d->ext; modal_send(d, x->cancel_ev); modal_hide(d); }
void *eng_ModalDialogWindow_Cast(PyObject *o) { void *p = ui_arg_ptr(o); return p && tg_is_ours(p) && !strcmp(((UI *)p)->o.cls->name, "ModalDialogWindow") ? p : NULL; }
void eng_ModalDialogWindow_SetAsKeyboardConfig(void *self, int v) { }
void eng_delete_ModalDialogWindow(void *self) { }
static UI *modal_button(UI *d, void *label, int type, void *ev, ModalX *x)
{
    UI *b; TGEvent *e = tg_ev_new("TGEvent", 0);
    e->type = (unsigned)type; e->dest = &d->o;
    b = btn_new("STButton", label, e, 0x40000000u);                                                  /* STBSF_SIZE_TO_TEXT */
    (void)ev; (void)x; return b;
}
void eng_ModalDialogWindow_Run(void *self, void *title, void *text, void *ok_label, PyObject *ok_ev, void *cancel_label, PyObject *cancel_ev)
{
    UI *d = self, *win, *inter, *para, *ok = NULL, *cancel = NULL; ModalX *x; float iw, ih, ww = 0.5f, wh = 0.35f; char *old_font = NULL; float old_size = 0; extern void eng_TGPane_KillChildren(void *); extern double eng_STStylizedWindow_GetBorderWidth(void *), eng_STStylizedWindow_GetBorderHeight(void *); extern void eng_STStylizedWindow_SetUseScrolling(void *, int);
    extern void *eng_TGParagraph_CreateW(void *text, float wrap, PyObject *color, char *font, float f, int flags);
    if (!d || !d->o.cls || strcmp(d->o.cls->name, "ModalDialogWindow")) return;
    x = modal_x(d);
    eng_TGPane_KillChildren(d);
    {   /* the dialog is made with the menu font of the mode, whatever the default font is now (the quick battle setup leaves its small flight font) */
        static const float small[5] = {6, 6, 9, 12, 15}; extern long eng_GraphicsModeInfo_GetCurrentResolution(void *);
        extern void tg_font_default_get(char **, float *); extern void tg_font_default_set(const char *, float);
        tg_font_default_get(&old_font, &old_size); tg_font_default_set("Crillee", small[eng_GraphicsModeInfo_GetCurrentResolution(NULL)]);
    }
    x->ok_ev = ok_ev ? ui_arg_ptr(ok_ev) : NULL; x->cancel_ev = cancel_ev ? ui_arg_ptr(cancel_ev) : NULL;
    tg_register_native(d, (int)tg_const("ET_OKAY"), modal_okay); tg_register_native(d, (int)tg_const("ET_CANCEL"), modal_cancel);
    win = stw_new("STStylizedWindow", "StylizedWindow", "SolidGlass", title, 0, 0, NULL, 1, ww, wh, NULL);
    eng_STStylizedWindow_SetUseScrolling(win, 0);
    pane_add(d, 0, win, 0.25f, 0.375f, 0);
    inter = eng_STStylizedWindow_GetInteriorPane(win);
    iw = ww - (float)eng_STStylizedWindow_GetBorderWidth(win); ih = wh - (float)eng_STStylizedWindow_GetBorderHeight(win);
    if (text) {
        para = eng_TGParagraph_CreateW(text, iw - 0.02f, NULL, NULL, 0, 0xa0000000u);
        pane_add(inter, inter->nkids, para, (iw - para->w) * 0.5f, ih * 0.5f - para->h, 0);
    }
    if (ok_label) {
        ok = modal_button(d, ok_label, (int)tg_const("ET_OKAY"), x->ok_ev, x);
        pane_add(inter, inter->nkids, ok, cancel_label ? iw * 0.25f - 0.08f : iw * 0.5f - 0.08f, ih - 0.0417f, 0);
        pane_set_focus(inter, ok);
    }
    if (cancel_label) {
        cancel = modal_button(d, cancel_label, (int)tg_const("ET_CANCEL"), x->cancel_ev, x);
        pane_add(inter, inter->nkids, cancel, iw * 0.75f - 0.08f, ih - 0.0417f, 0);
    }
    x->ok = ok; x->cancel = cancel;
    ui_show(d, 1); eng_TGPane_MoveToFront(eng_TopWindow_GetTopWindow(), d, 0); pane_set_focus(eng_TopWindow_GetTopWindow(), d);
    eng_STStylizedWindow_InteriorChangedSize(win, 1);
    tg_ui_focus_set(ok ? ok : cancel);
    { extern void tg_font_default_set(const char *, float); tg_font_default_set(old_font, old_size); free(old_font); }
}
