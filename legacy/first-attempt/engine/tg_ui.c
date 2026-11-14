/* UI toolkit core: TGRect, TGUIModule, TGUIObject, TGPane, root pane, TopWindow and main windows, basic widgets.
   Behavior from stbc.exe: TGUIObject 0072fc20.., flag setters 00730870..00730a50, TGPane 0072dcc0..0072f0e0, TopWindow 0050c430.., Game::Create 00405c10.
   Flags (+0x28): 1 enabled, 2 selected, 4 highlighted, 8 visible (default), 0x10 exclusive, 0x20 skip-parent, 0x40 always handle events,
   0x80 use parent batch, 0x100 no focus, 0x200 batch child polys, 0x400 completely visible (cache).
   Child list: index 0 is the front-most child (drawn last). Sizes and offsets are snapped to whole pixels (truncation) like the original.
   Approximations (marked): no layout engine beyond what the scripts ask for, no drawing, no mouse handling yet, font metrics. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tg_ui.h"

extern void *eng_new_TGString(char *);
extern const char *eng_TGString_GetCString(void *);
extern long eng_GraphicsModeInfo_GetWidth(void *);
extern long eng_GraphicsModeInfo_GetHeight(void *);
extern void tg_global_override(const char *, void *);
static char *dup(const char *s) { return s ? strdup(s) : NULL; }
void *ui_arg_ptr(PyObject *o) { void *p = NULL; if (!o || o == Py_None) return NULL; if (!appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p; }

UI *as_ui(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x201) ? p : NULL; }
UI *as_pane(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x204) ? p : NULL; }
float ui_snap(float v, float scale) { int i = (int)(v * scale); return i == 0 ? 0.0f : (float)i / scale; }   /* __ftol then divide (007300e0) */
float ui_scr_w(void) { return (float)eng_GraphicsModeInfo_GetWidth(NULL); }
float ui_scr_h(void) { return (float)eng_GraphicsModeInfo_GetHeight(NULL); }

/* ================================================= TGRect ================================================= */
typedef struct { float l, t, w, h; } Rect;
void *eng_new_TGRect(float l, float t, float w, float h) { Rect *r = malloc(sizeof *r); r->l = l; r->t = t; r->w = w; r->h = h; return r; }
void eng_TGRect_Set(void *p, float l, float t, float w, float h) { Rect *r = p; r->l = l; r->t = t; r->w = w; r->h = h; }
double eng_TGRect_GetLeft(void *p) { return ((Rect *)p)->l; }
double eng_TGRect_GetTop(void *p) { return ((Rect *)p)->t; }
double eng_TGRect_GetWidth(void *p) { return ((Rect *)p)->w; }
double eng_TGRect_GetHeight(void *p) { return ((Rect *)p)->h; }
double eng_TGRect_GetRight(void *p) { return ((Rect *)p)->l + ((Rect *)p)->w; }
double eng_TGRect_GetBottom(void *p) { return ((Rect *)p)->t + ((Rect *)p)->h; }
void eng_TGRect_GetPosition(void *p, void *out) { ((float *)out)[0] = ((Rect *)p)->l; ((float *)out)[1] = ((Rect *)p)->t; }
void eng_TGRect_SetLeft(void *p, float v) { ((Rect *)p)->l = v; }
void eng_TGRect_SetTop(void *p, float v) { ((Rect *)p)->t = v; }
void eng_TGRect_SetWidth(void *p, float v) { ((Rect *)p)->w = v; }
void eng_TGRect_SetHeight(void *p, float v) { ((Rect *)p)->h = v; }
void eng_TGRect_SetRight(void *p, float v) { ((Rect *)p)->w = v - ((Rect *)p)->l; }
void eng_TGRect_SetBottom(void *p, float v) { ((Rect *)p)->h = v - ((Rect *)p)->t; }
void eng_TGRect_SetPosition(void *p, void *q) { ((Rect *)p)->l = ((float *)q)[0]; ((Rect *)p)->t = ((float *)q)[1]; }
long eng_TGRect_IsEmpty(void *p) { return ((Rect *)p)->w == 0 || ((Rect *)p)->h == 0; }
long eng_TGRect_IsClear(void *p) { Rect *r = p; return r->l == 0 && r->t == 0 && r->w == 0 && r->h == 0; }
static int rect_within(const Rect *r, float x, float y) { return r->w != 0 && r->h != 0 && x >= r->l && x <= r->l + r->w && y >= r->t && y <= r->t + r->h; }   /* 00739e90 */
long eng_TGRect_IsWithin(void *p, float x, float y) { return rect_within(p, x, y); }
static int rect_intersects(const Rect *a, const Rect *b)                 /* 00739e30 */
{ return !(a->w + a->l <= b->l || a->h + a->t <= b->t || b->w + b->l <= a->l || b->h + b->t <= a->t); }
long eng_TGRect_DoesIntersect(void *p, void *q) { return rect_intersects(p, q); }
static void rect_clip(Rect *a, const Rect *b)                            /* 00739fc0 */
{
    float ar, br, ab, bb;
    if (!rect_intersects(a, b)) { a->l = a->t = a->w = a->h = 0; return; }
    ar = a->l + a->w; br = b->l + b->w; ab = a->t + a->h; bb = b->t + b->h;
    if (a->l < b->l) { a->w -= b->l - a->l; a->l = b->l; }
    if (br < ar) a->w -= ar - br;
    if (a->t < b->t) { a->h -= b->t - a->t; a->t = b->t; }
    if (bb < ab) a->h -= ab - bb;
}
void eng_TGRect_ClipTo(void *p, void *q) { rect_clip(p, q); }
void eng_TGRect_Clear(void *p) { memset(p, 0, 16); }

/* TGUIModule pixel alignment: values are snapped to the pixel grid (the exact rounding of PixelAlignRect is not decoded: nearest pixel) */
double eng_TGUIModule_PixelAlignValue(float v, int horizontal) { float s = horizontal ? ui_scr_w() : ui_scr_h(); return floorf(v * s + 0.5f) / s; }
void *eng_TGUIModule_PixelAlignPoint(void *p) { float *q = malloc(8), *a = p; q[0] = (float)eng_TGUIModule_PixelAlignValue(a[0], 1); q[1] = (float)eng_TGUIModule_PixelAlignValue(a[1], 0); return q; }
void *eng_TGUIModule_PixelAlignRect(void *p)
{
    Rect *r = p, *q = malloc(sizeof *q); float s_w = ui_scr_w(), s_h = ui_scr_h();
    q->l = floorf(r->l * s_w + 0.5f) / s_w; q->t = floorf(r->t * s_h + 0.5f) / s_h;
    q->w = floorf(r->w * s_w + 0.5f) / s_w; q->h = floorf(r->h * s_h + 0.5f) / s_h; return q;
}

/* ================================================= TGUIObject ================================================= */
extern void *tg_default_font(void);
/* a node keeps the default font group of the moment it was made (the menu scripts change the default font between widgets) */
UI *ui_new(const char *cls) { UI *u = tg_new(cls, sizeof(UI), 0); u->font = tg_default_font(); u->flags = UF_VISIBLE; u->color[0] = u->color[1] = u->color[2] = u->color[3] = 1.0f; return u; }
void ui_layout(UI *u);                                             /* vtbl +0xe8 */
static int ui_comp_visible(UI *u)                                         /* 00730910: cached bit */
{ return (u->flags & UF_COMPVISIBLE) != 0; }
static int ui_comp_enabled(UI *u) { for (; u && (void *)u != (void *)-1; u = u->parent) if (!(u->flags & UF_ENABLED)) return 0; return 1; }   /* 007308f0 */
static void propagate_vis(UI *p)                                         /* TGPane::propagateVisibility 0072df90 */
{
    int i, cv = ui_comp_visible(p);
    for (i = 0; i < p->nkids; i++) {
        UI *c = p->kids[i];
        if ((c->flags & UF_VISIBLE) && cv) c->flags |= UF_COMPVISIBLE; else c->flags &= ~UF_COMPVISIBLE;
        if (as_pane(c)) propagate_vis(c);
    }
}
static void refresh_vis(UI *u)                                           /* 00730a50 */
{
    if (u->parent && (u->flags & UF_VISIBLE) && ui_comp_visible(u->parent)) u->flags |= UF_COMPVISIBLE; else u->flags &= ~UF_COMPVISIBLE;
    if (as_pane(u)) propagate_vis(u);
}
static int is_focus(UI *u) { return u->parent && u->parent->focus == u; }
void ui_show(UI *u, int layout)                                    /* 00730920 + TGPane::Show */
{
    int was = ui_comp_visible(u);
    if (!(u->flags & UF_VISIBLE)) {
        u->flags |= UF_VISIBLE;
        if (u->parent && ui_comp_visible(u->parent)) u->flags |= UF_COMPVISIBLE;
        if (!u->parent) u->flags |= UF_COMPVISIBLE;
    }
    if (!was && ui_comp_visible(u) && as_pane(u)) propagate_vis(u);
    if (layout) ui_layout(u);
}
void ui_hide(UI *u, int layout)                                    /* 00730970 + TGPane::Hide */
{
    int was = ui_comp_visible(u);
    if (u->flags & UF_VISIBLE) {
        u->flags &= ~(UF_VISIBLE | UF_COMPVISIBLE);
        if (u->parent && u->parent->focus == u) pane_set_focus(u->parent, NULL);
    }
    if (was && as_pane(u)) propagate_vis(u);
    if (layout) ui_layout(u);
}
extern void tg_button_autosize(UI *u);
extern void tg_button_relayout(UI *u);
void ui_layout(UI *u) { int i; if (u->ext && u->o.cls && (!strcmp(u->o.cls->name, "STButton") || !strcmp(u->o.cls->name, "STToggle"))) { tg_button_relayout(u); if ((u->flags & 0x40000000u) && u->w <= 0 && !strcmp(u->o.cls->name, "STButton")) tg_button_autosize(u); } if (!ui_comp_visible(u) && u->parent) return; for (i = u->nkids - 1; i >= 0; i--) if (u->kids[i]->flags & UF_VISIBLE) ui_layout(u->kids[i]); }   /* 0072e060 */

void *eng_TGUIObject_GetParent(void *p) { UI *u = as_ui(p); return u ? u->parent : NULL; }
void eng_TGUIObject_SetParent(void *p, void *par, int layout) { UI *u = as_ui(p); if (u) { u->parent = as_ui(par); refresh_vis(u); if (layout) ui_layout(u); } }
void *eng_TGUIObject_GetConceptualParent(void *p)                         /* 0072fe40 */
{ UI *u = as_ui(p), *c; if (!u) return NULL; for (c = u->parent; c && (c->flags & UF_SKIPPARENT) && c->parent; c = c->parent) ; return c; }
void eng_TGUIObject_GetBounds(void *p, void *out) { UI *u = as_ui(p); Rect *r = out; if (u && r) { r->l = u->x; r->t = u->y; r->w = u->w; r->h = u->h; } }
void ui_set_pos(UI *u, float x, float y, int layout) { u->x = ui_snap(x, ui_scr_w()); u->y = ui_snap(y, ui_scr_h()); if (layout) ui_layout(u->parent ? u->parent : u); }
extern void tg_menu_resized(UI *m);
void ui_set_size(UI *u, float w, float h, int layout) { u->w = ui_snap(w, ui_scr_w()); u->h = ui_snap(h, ui_scr_h()); tg_menu_resized(u); if (layout) ui_layout(u); }
void eng_TGUIObject_SetBounds(void *p, void *r, int layout) { UI *u = as_ui(p); Rect *b = r; if (u && b) { ui_set_pos(u, b->l, b->t, 0); ui_set_size(u, b->w, b->h, 0); if (layout) ui_layout(u); } }
double eng_TGUIObject_GetLeft(void *p) { UI *u = as_ui(p); return u ? u->x : 0; }
double eng_TGUIObject_GetTop(void *p) { UI *u = as_ui(p); return u ? u->y : 0; }
double eng_TGUIObject_GetRight(void *p) { UI *u = as_ui(p); return u ? u->x + u->w : 0; }
double eng_TGUIObject_GetBottom(void *p) { UI *u = as_ui(p); return u ? u->y + u->h : 0; }
void tg_ui_size_of(const void *node, float *w, float *h);       /* tg_ui_draw.c: a button with no size reads its label size */
double eng_TGUIObject_GetWidth(void *p) { UI *u = as_ui(p); float w, h; tg_ui_size_of(u, &w, &h); return w; }
double eng_TGUIObject_GetHeight(void *p) { UI *u = as_ui(p); float w, h; tg_ui_size_of(u, &w, &h); return h; }
long eng_TGUIObject_IsWithin(void *p, void *pt) { UI *u = as_ui(p); float *q = pt; return u && q && !(q[0] < 0 || u->w < q[0] || q[1] < 0 || u->h < q[1]); }
void eng_TGUIObject_GetPosition(void *p, void *out) { UI *u = as_ui(p); if (u && out) { ((float *)out)[0] = u->x; ((float *)out)[1] = u->y; } }
void eng_TGUIObject_SetPosition(void *p, float x, float y, int layout) { UI *u = as_ui(p); if (u) ui_set_pos(u, x, y, layout); }
long eng_TGUIObject_GetFlags(void *p) { UI *u = as_ui(p); return u ? (long)u->flags : 0; }
void eng_TGUIObject_GetScreenOffset(void *p, void *out)                   /* 0072ff80 */
{ UI *u = as_ui(p), *c; float *o = out; if (!u || !o) return; o[0] = u->x; o[1] = u->y; for (c = u->parent; c; c = c->parent) { o[0] += c->x; o[1] += c->y; } }
void eng_TGUIObject_GetClipRect(void *p, void *out)                       /* 0072ffc0 */
{
    UI *u = as_ui(p); Rect *r = out; if (!u || !r) return;
    r->l = u->x; r->t = u->y; r->w = u->w; r->h = u->h;
    if (r->w != 0 && r->h != 0 && u->parent && ui_comp_visible(u)) { Rect pr; eng_TGUIObject_GetClipRect(u->parent, &pr); pr.l = pr.t = 0; /* parent clip relative to parent origin (0073a0c0 shifts by its offset) */ pr.w = u->parent->w; pr.h = u->parent->h; rect_clip(r, &pr); }
    else if (!(r->w != 0 && r->h != 0 && u->parent && ui_comp_visible(u))) { if (!(u->parent == NULL && r->w != 0)) memset(r, 0, 16); }
}
void *eng_TGUIObject_GetCommonAncestor(void *p, void *q)                   /* 00730c00 */
{
    UI *a = as_ui(p), *b = as_ui(q), *c, *d; if (!a || !b) return NULL;
    for (c = b; c; c = c->parent) { if (c == a) return a; }
    for (c = a->parent; c; c = c->parent) for (d = b; d; d = d->parent) if (c == d) return c;
    return NULL;
}
void eng_TGUIObject_Move(void *p, float dx, float dy, int layout) { UI *u = as_ui(p); if (u) { u->x += ui_snap(dx, ui_scr_w()); u->y += ui_snap(dy, ui_scr_h()); if (layout) ui_layout(u->parent ? u->parent : u); } }   /* 007300e0 */
void eng_TGUIObject_Resize(void *p, float w, float h, int layout) { UI *u = as_ui(p); if (u) ui_set_size(u, w, h, layout); }
void eng_TGUIObject_AlignTo(void *p, void *other, int oc, int mc, int layout)   /* 007305d0: my corner mc goes to the corner oc of other (0 UL, 1 UR, 2 BL, 3 BR); no other: inside the parent */
{
    UI *u = as_ui(p), *o = as_ui(other); float ox, oy, mx, my, ua[2], oa[2];
    if (!u) return;
    if (!o) {
        if (u->parent) { float x = 0, y = 0; if (oc == 1 || oc == 3) x = u->parent->w - u->w; if (oc == 2 || oc == 3) y = u->parent->h - u->h; ui_set_pos(u, x, y, layout); }
        return;
    }
    eng_TGUIObject_GetScreenOffset(u, ua); eng_TGUIObject_GetScreenOffset(o, oa);
    ox = oa[0]; oy = oa[1]; if (oc == 1 || oc == 3) ox += o->w; if (oc == 2 || oc == 3) oy += o->h;
    mx = ua[0]; my = ua[1]; if (mc == 1 || mc == 3) mx += u->w; if (mc == 2 || mc == 3) my += u->h;
    eng_TGUIObject_Move(u, ox - mx, oy - my, layout);
}
void *eng_TGUIObject_GetFocusLeaf(void *p) { UI *u = as_ui(p); while (u && u->focus) u = u->focus; return u; }
void eng_TGUIObject_GotFocus(void *p, int x) { }
void eng_TGUIObject_GotTrueFocus(void *p, int x) { }
void eng_TGUIObject_GotTrueFocusPath(void *p, int x) { }
void eng_TGUIObject_LostFocus(void *p, int x) { }
void eng_TGUIObject_LostTrueFocus(void *p, int x) { }
void eng_TGUIObject_LostTrueFocusPath(void *p, int x) { }
static UI *true_focus;
long eng_TGUIObject_HasFocus(void *p) { UI *u = as_ui(p); return u && is_focus(u); }
long eng_TGUIObject_HasTrueFocus(void *p) { return p == (void *)true_focus && p; }
long eng_TGUIObject_HasAncestor(void *p, void *a) { UI *u = as_ui(p), *c; if (!u || !a) return 0; for (c = u->parent; c; c = c->parent) if (c == a) return 1; return 0; }
long eng_TGUIObject_IsInTrueFocusPath(void *p) { UI *c; if (!true_focus || !p) return 0; for (c = true_focus; c; c = c->parent) if (c == p) return 1; return 0; }
long eng_TGUIObject_FocusHeight(void *p) { UI *u = as_ui(p); long n = 0; while (u && u->focus) { n++; u = u->focus; } return n; }
extern int tg_is_menu(const void *); extern void eng_STMenu_GetDesiredSize(void *, void *);
void eng_TGUIObject_GetDesiredSize(void *p, void *out) { UI *u = as_ui(p); float *o = out; if (u && o) { if (tg_is_menu(u)) eng_STMenu_GetDesiredSize(u, out); else { o[0] = u->w; o[1] = u->h; } } }
void eng_TGUIObject_MouseEntered(void *p, void *e) { }
void eng_TGUIObject_MouseExited(void *p, void *e) { }
long eng_TGUIObject_IsCompletelyEnabled(void *p) { UI *u = as_ui(p); return u ? ui_comp_enabled(u) : 0; }
long eng_TGUIObject_IsCompletelyVisible(void *p) { UI *u = as_ui(p); return u ? ui_comp_visible(u) : 0; }
void eng_TGUIObject_Print(void *p, int a, int b) { }
void eng_TGUIObject_Layout(void *p) { UI *u = as_ui(p); if (u) ui_layout(u); }

/* flag families: IsX, SetX(bool "layout"), SetNotX, ToggleX, SetXFlag(on, layout) */
static void set_flag(UI *u, unsigned bit, int on, int arg)
{
    if (bit == UF_VISIBLE) { extern int tg_vis_virtual(UI *, int, int); if (tg_vis_virtual(u, on, arg)) return; if (on) ui_show(u, arg); else ui_hide(u, arg); return; }
    if (on) u->flags |= bit; else u->flags &= ~bit;
}
#define FLAGFN(Name, bit)                                                                                           \
    long eng_TGUIObject_Is##Name(void *p) { UI *u = as_ui(p); return u && (u->flags & (bit)) != 0; }                 \
    void eng_TGUIObject_Set##Name(void *p, int a) { UI *u = as_ui(p); if (u) set_flag(u, bit, 1, a); }               \
    void eng_TGUIObject_SetNot##Name(void *p, int a) { UI *u = as_ui(p); if (u) set_flag(u, bit, 0, a); }            \
    void eng_TGUIObject_Toggle##Name(void *p, int a) { UI *u = as_ui(p); if (u) set_flag(u, bit, !(u->flags & (bit)), a); } \
    void eng_TGUIObject_Set##Name##Flag(void *p, int a, int on) { UI *u = as_ui(p); if (u) set_flag(u, bit, on != 0, a); }
FLAGFN(Selected, UF_SELECTED)
FLAGFN(Highlighted, UF_HIGHLIGHTED)
FLAGFN(Visible, UF_VISIBLE)
FLAGFN(Exclusive, UF_EXCLUSIVE)
FLAGFN(UseParentBatch, UF_USEPARENTBATCH)
FLAGFN(BatchChildPolys, UF_BATCHCHILD)
long eng_TGUIObject_IsEnabled(void *p) { UI *u = as_ui(p); return u && (u->flags & UF_ENABLED) != 0; }
void eng_TGUIObject_SetEnabled(void *p, int a) { UI *u = as_ui(p); if (u) u->flags |= UF_ENABLED; }
void eng_TGUIObject_SetDisabled(void *p, int a) { UI *u = as_ui(p); if (u) u->flags &= ~UF_ENABLED; }
void eng_TGUIObject_ToggleEnabled(void *p, int a) { UI *u = as_ui(p); if (u) u->flags ^= UF_ENABLED; }
void eng_TGUIObject_SetEnabledFlag(void *p, int a, int on) { UI *u = as_ui(p); if (u) { if (on) u->flags |= UF_ENABLED; else u->flags &= ~UF_ENABLED; } }
/* flags without the extra argument */
#define FLAGFN1(Name, Not, bit)                                                                                      \
    long eng_TGUIObject_Is##Name(void *p) { UI *u = as_ui(p); return u && (u->flags & (bit)) != 0; }                 \
    void eng_TGUIObject_Set##Name(void *p) { UI *u = as_ui(p); if (u) u->flags |= (bit); }                           \
    void eng_TGUIObject_##Not(void *p) { UI *u = as_ui(p); if (u) u->flags &= ~(bit); }                              \
    void eng_TGUIObject_Toggle##Name(void *p) { UI *u = as_ui(p); if (u) u->flags ^= (bit); }                        \
    void eng_TGUIObject_Set##Name##Flag(void *p, int on) { UI *u = as_ui(p); if (u) { if (on) u->flags |= (bit); else u->flags &= ~(bit); } }
FLAGFN1(SkipParent, SetNoSkipParent, UF_SKIPPARENT)
FLAGFN1(AlwaysHandleEvents, SetNotAlwaysHandleEvents, UF_ALWAYSEVENTS)
FLAGFN1(NoFocus, SetNotNoFocus, UF_NOFOCUS)

/* ================================================= TGPane ================================================= */
void *pane_new(const char *cls, float w, float h) { UI *u = ui_new(cls); u->w = ui_snap(w, ui_scr_w()); u->h = ui_snap(h, ui_scr_h()); return u; }
void *eng_new_TGPane(float w, float h) { return pane_new("TGPane", w, h); }
void *eng_TGPane_Create(float w, float h) { return pane_new("TGPane", w, h); }
void *eng_TGPane_Cast(PyObject *o) { void *p = ui_arg_ptr(o); return as_pane(p); }
UI *fwd(void *p)                                                   /* TGWindow::AddChild & co (0073e940..0073ead0) forward to the sub pane (vtbl +0x148 = first child) */
{
    UI *u = as_pane(p);
    while (u && u->is_window && u->nkids) u = u->kids[0];
    return u;
}
static int pane_find(UI *p, UI *c) { int i; for (i = 0; i < p->nkids; i++) if (p->kids[i] == c) return i; return -1; }
static void pane_insert_at(UI *p, int idx, UI *c)
{
    if (p->nkids == p->ckids) { p->ckids = p->ckids ? p->ckids * 2 : 8; p->kids = realloc(p->kids, p->ckids * sizeof *p->kids); }
    if (idx < 0 || idx > p->nkids) idx = p->nkids;
    memmove(p->kids + idx + 1, p->kids + idx, (p->nkids - idx) * sizeof *p->kids); p->kids[idx] = c; p->nkids++;
}
void pane_add(UI *p, int idx, UI *c, float x, float y, int layout)      /* TGPane::AddChild 0072e400 */
{
    c->parent = p; pane_insert_at(p, idx, c);
    if (p->nkids == 1 || !p->focus) pane_set_focus(p, c);
    ui_set_pos(c, x, y, 0); refresh_vis(c);
    if (p->o.cls && !strcmp(p->o.cls->name, "STSubPane")) { extern void eng_STSubPane_ResizeToContents(void *, int); eng_STSubPane_ResizeToContents(p, layout); }     /* the stack is laid out as children come */
    else if (layout) ui_layout(p);
}
void eng_TGPane_AddChild(void *p, void *c, float x, float y, int layout) { UI *pp = fwd(p), *cc = as_ui(c); if (pp && cc) pane_add(pp, pp->nkids, cc, x, y, layout); }
void eng_TGPane_PrependChild(void *p, void *c, float x, float y, int layout) { UI *pp = fwd(p), *cc = as_ui(c); if (pp && cc) pane_add(pp, 0, cc, x, y, layout); }
void eng_TGPane_InsertChild(void *p, int i, void *c, float x, float y, int layout) { UI *pp = fwd(p), *cc = as_ui(c); if (pp && cc) pane_add(pp, i < 0 ? i + pp->nkids : i, cc, x, y, layout); }
static void pane_remove(UI *p, UI *c, int layout, int destroy)               /* 0072e5b0 / 0072e6c0 */
{
    int i = pane_find(p, c);
    if (i >= 0) { memmove(p->kids + i, p->kids + i + 1, (p->nkids - i - 1) * sizeof *p->kids); p->nkids--; c->parent = NULL; c->flags &= ~UF_COMPVISIBLE; }
    if (p->focus == c) { UI *n = NULL; int k; for (k = 0; k < p->nkids; k++) if (!(p->kids[k]->flags & UF_NOFOCUS)) { n = p->kids[k]; break; } p->focus = NULL; pane_set_focus(p, n); }
    if (destroy) c->o.dead = 1;
    if (layout) ui_layout(p);
}
void eng_TGPane_RemoveChild(void *p, void *c, int layout) { UI *pp = fwd(p), *cc = as_ui(c); if (pp && cc) pane_remove(pp, cc, layout, 0); }
void eng_TGPane_DeleteChild(void *p, void *c, int layout) { UI *pp = fwd(p), *cc = as_ui(c); if (pp && cc && cc->parent == pp) pane_remove(pp, cc, layout, 1); }
long eng_TGPane_GetNumChildren(void *p) { UI *pp = fwd(p); return pp ? pp->nkids : 0; }
long eng_TGPane_GetTrueNumChildren(void *p) { return eng_TGPane_GetNumChildren(p); }
long eng_TGPane_FindPos(void *p, void *c) { UI *pp = fwd(p); return pp ? pane_find(pp, as_ui(c)) : -1; }
void *eng_TGPane_GetFirstChild(void *p) { UI *pp = fwd(p); return pp && pp->nkids ? pp->kids[0] : NULL; }
void *eng_TGPane_GetLastChild(void *p) { UI *pp = fwd(p); return pp && pp->nkids ? pp->kids[pp->nkids - 1] : NULL; }
void *eng_TGPane_GetNthChild(void *p, int n) { UI *pp = fwd(p); if (!pp || pp->nkids == 0 || n >= pp->nkids || -n >= pp->nkids) return NULL; return pp->kids[n < 0 ? n + pp->nkids : n]; }   /* 0072ed80 */
void *eng_TGPane_GetTrueNthChild(void *p, int n) { return eng_TGPane_GetNthChild(p, n); }
void *eng_TGPane_GetNextChild(void *p, void *c) { UI *pp = fwd(p); int i = pp ? pane_find(pp, as_ui(c)) : -1; return i >= 0 && i + 1 < pp->nkids ? pp->kids[i + 1] : NULL; }
void *eng_TGPane_GetPrevChild(void *p, void *c) { UI *pp = fwd(p); int i = pp ? pane_find(pp, as_ui(c)) : -1; return i > 0 ? pp->kids[i - 1] : NULL; }
void eng_TGPane_KillChildren(void *p) { UI *pp = fwd(p); if (!pp) return; pane_set_focus(pp, NULL); while (pp->nkids) { UI *c = pp->kids[0]; pp->kids[0] = pp->kids[--pp->nkids]; memmove(pp->kids, pp->kids + 1, 0); c->parent = NULL; c->o.dead = 1; } }
static void pane_move(UI *p, UI *c, int to) { int i = pane_find(p, c); if (i < 0) return; if (to < 0) to = 0; if (to >= p->nkids) to = p->nkids - 1; memmove(p->kids + i, p->kids + i + 1, (p->nkids - i - 1) * sizeof *p->kids); p->nkids--; pane_insert_at(p, to, c); }
void eng_TGPane_MoveToFront(void *p, void *c, int l) { UI *pp = fwd(p); if (pp) pane_move(pp, as_ui(c), 0); }
void eng_TGPane_MoveToBack(void *p, void *c, int l) { UI *pp = fwd(p); if (pp) pane_move(pp, as_ui(c), pp->nkids - 1); }
void eng_TGPane_MoveTowardsFront(void *p, void *c, int l) { UI *pp = fwd(p); int i = pp ? pane_find(pp, as_ui(c)) : -1; if (i > 0) pane_move(pp, as_ui(c), i - 1); }     /* 0072e920 */
void eng_TGPane_MoveTowardsBack(void *p, void *c, int l) { UI *pp = fwd(p); int i = pp ? pane_find(pp, as_ui(c)) : -1; if (i >= 0 && i < pp->nkids - 1) pane_move(pp, as_ui(c), i + 1); }   /* 0072eac0 */
void pane_set_focus(UI *p, UI *c)                                     /* 0072eb40 */
{
    if (c) { if (c->parent != p || p->focus == c || (c->flags & UF_NOFOCUS)) return; }
    p->focus = c;
}
void eng_TGPane_SetFocus(void *p, void *c, int x) { UI *pp = as_pane(p); if (pp) pane_set_focus(pp, as_ui(c)); }
void *eng_TGPane_GetFocus(void *p) { UI *pp = as_pane(p); return pp ? pp->focus : NULL; }
void eng_TGPane_MoveFocus(void *p, int step, int wrap, int x)                 /* 0072eba0 */
{
    UI *pp = as_pane(p); int i, n; if (!pp || !step || !pp->nkids) return;
    i = pp->focus ? pane_find(pp, pp->focus) : (step < 0 ? pp->nkids : -1);
    for (n = 0; n < pp->nkids && step; n++) {
        i += step > 0 ? 1 : -1;
        if (i >= pp->nkids) { if (!wrap) break; i = 0; } else if (i < 0) { if (!wrap) break; i = pp->nkids - 1; }
        if (!(pp->kids[i]->flags & UF_NOFOCUS)) { pane_set_focus(pp, pp->kids[i]); return; }
    }
}
void *eng_TGPane_GetFocusLeaf(void *p) { return eng_TGUIObject_GetFocusLeaf(p); }
long eng_TGPane_FocusHeight(void *p) { return eng_TGUIObject_FocusHeight(p); }

/* ================================================= root pane, TopWindow, main windows ================================================= */
static UI *root, *top;
UI *tg_ui_root(void) { return root; }                       /* the root pane (draw list walks it, tg_ui_draw.c) */
static void *new_main(const char *cls, int mwt, float w, float h) { UI *u = ui_new(cls); u->mwt = mwt; u->w = w; u->h = h; return u; }
void *eng_TopWindow_GetTopWindow(void) { return top; }
void *eng_new_TopWindow(void) { return top; }
long eng_MainWindow_IsWindowActive(void *p) { UI *u = as_ui(p); return u && (u->flags & UF_VISIBLE) && ui_comp_visible(u); }     /* approximation: a visible window is active */
void *eng_TopWindow_FindMainWindow(void *p, int t)                           /* 0050e1b0: first MainWindow child of that type */
{ UI *tw = as_pane(p ? p : top); int i; if (!tw) return NULL; for (i = 0; i < tw->nkids; i++) if (tg_is_type(&tw->kids[i]->o, 0x810f) && tw->kids[i]->mwt == t) return tw->kids[i]; return NULL; }
void tg_ui_init(void)                                                          /* engine_init: root pane + TopWindow with the windows its constructor creates (0050c430) */
{
    root = ui_new("TGRootPane"); root->w = 1.0f; root->h = 1.0f; tg_global_override("g_kRootWindow", root);
    top = ui_new("TopWindow"); top->w = 1.0f; top->h = 1.0f; top->flags |= UF_COMPVISIBLE; root->flags |= UF_COMPVISIBLE;
    pane_add(root, 0, top, 0, 0, 0);
    pane_add(top, top->nkids, new_main("OptionsWindow", 4, 0.9f, 0.9f), 0.2f, 0.1f, 0);
    pane_add(top, top->nkids, new_main("ConsoleWindow", 2, 1.0f, 1.0f), 0, 0, 0);
    pane_add(top, top->nkids, new_main("MultiplayerWindow", 8, 1.0f, 1.0f), 0, 0, 0);
    pane_add(top, top->nkids, new_main("CdCheckWindow", 9, 1.0f, 1.0f), 0.2f, 0.4f, 0);
    pane_add(top, top->nkids, new_main("ModalDialogWindow", 10, 1.0f, 1.0f), 0, 0, 0);
    pane_add(top, top->nkids, new_main("SubtitleWindow", 5, 1.0f, 0.2f), 0, 0.8f, 0);   /* assumed: no script creates it, the exe has no direct creator caller (only Python/stream); position is set by SetPositionForMode */
    {   /* menus and dialogs start hidden: the main menu is the options window, and it covered the tactical view */
        int i; for (i = 0; i < top->nkids; i++) { int t = top->kids[i]->mwt; if (t == 2 || t == 4 || t == 8 || t == 9 || t == 10) ui_hide(top->kids[i], 0); }
    }
    { void tg_ui_register_top_handlers(void); tg_ui_register_top_handlers(); }
}
/* Game::Create 00405c10 creates the bridge, tactical, map and cinematic windows (and the mission log) and adds them to the TopWindow; the tactical window gets the focus */
void tg_ui_game_windows(void)
{
    UI *b, *t, *m, *c;
    if (!top || eng_TopWindow_FindMainWindow(top, 1)) return;
    b = new_main("BridgeWindow", 0, 1.0f, 1.0f); t = new_main("TacticalWindow", 1, 1.0f, 1.0f);
    m = new_main("MapWindow", 6, 1.0f, 1.0f); c = new_main("CinematicWindow", 7, 1.0f, 1.0f);
    pane_add(top, top->nkids, t, 0, 0, 1); pane_add(top, top->nkids, m, 0, 0, 1); pane_add(top, top->nkids, c, 0, 0, 1);
    pane_add(top, top->nkids, b, 0, 0, 0);
    ui_hide(m, 0); ui_hide(c, 0);
    pane_set_focus(top, t); ui_hide(t, 0);                       /* the exe hides the tactical window and shows the bridge by default */
}
void *eng_new_BridgeWindow(void) { return new_main("BridgeWindow", 0, 1.0f, 1.0f); }
void *eng_new_ConsoleWindow(void) { return new_main("ConsoleWindow", 2, 1.0f, 1.0f); }
void *eng_new_OptionsWindow(void) { return new_main("OptionsWindow", 4, 0.9f, 0.9f); }
void *eng_SubtitleWindow_Create(void) { return new_main("SubtitleWindow", 5, 1.0f, 0.2f); }
void *eng_new_TacticalWindow(void) { return new_main("TacticalWindow", 1, 1.0f, 1.0f); }
#define CASTFN(Cls, ct) void *eng_##Cls##_Cast(PyObject *o) { void *p = ui_arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
CASTFN(BridgeWindow, 0x8110) CASTFN(TacticalWindow, 0x8111) CASTFN(MapWindow, 0x8116) CASTFN(CinematicWindow, 0x8117) CASTFN(OptionsWindow, 0x8114) CASTFN(SubtitleWindow, 0x8115)
CASTFN(TGWindow, 0x1000d) CASTFN(TGFrameWindow, 0x20d) CASTFN(TGFrame, 0x203) CASTFN(TGIcon, 0x202) CASTFN(TGButtonBase, 0x206) CASTFN(TGButton, 0x207) CASTFN(TGTextButton, 0x20b)
CASTFN(TacticalControlWindow, 0x8112)
static int vis_flag(void *p) { UI *u = as_ui(p); return u && (u->flags & UF_VISIBLE); }
long eng_TopWindow_IsBridgeVisible(void *w) { UI *b = eng_TopWindow_FindMainWindow(w, 0); return b && b == top->focus; }
long eng_TopWindow_IsTacticalVisible(void *w) { UI *b = eng_TopWindow_FindMainWindow(w, 1); return b && b == top->focus; }
/* the Python focus hooks of the two main windows: TacticalWindow::Activate 0050b430 / Deactivate 0050b600 call TacticalInterfaceHandlers.GotFocus / LostFocus
   (window, layout), BridgeWindow::GotFocus 004fb920 calls BridgeHandlers.GotFocus (bridge LostFocus is a no-op). GotFocus refreshes the tactical control
   window (TacticalControlWindow.Refresh), which shows the tactical displays. */
static void win_hook(const char *mod, const char *func, UI *w)
{
    PyObject *m = PyImport_ImportModule((char *)mod), *f, *a, *r;
    if (!m) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)func); Py_DECREF(m);
    if (!f) { PyErr_Clear(); return; }
    a = Py_BuildValue("(Ni)", tg_obj_pyobj(w), 0); r = a ? PyObject_CallObject(f, a) : NULL; Py_DECREF(f); Py_XDECREF(a);
    if (!r) { PyErr_Print(); PyErr_Clear(); } else Py_DECREF(r);
}
static void top_show(int mwt)
{
    UI *w = eng_TopWindow_FindMainWindow(top, mwt), *o, *prev = top->focus; int i;
    if (!w) return;
    if (prev && prev != w && prev->mwt == 1 && (mwt == 0 || mwt == 1)) win_hook("TacticalInterfaceHandlers", "LostFocus", prev);
    for (i = 0; i < top->nkids; i++) { o = top->kids[i]; if ((mwt == 0 || mwt == 1) && (o->mwt == 0 || o->mwt == 1) && o != w) ui_hide(o, 0); }
    ui_show(w, 1); pane_set_focus(top, w);
    if (mwt == 1) win_hook("TacticalInterfaceHandlers", "GotFocus", w);
    else if (mwt == 0) win_hook("BridgeHandlers", "GotFocus", w);
}
void eng_TopWindow_ForceBridgeVisible(void *w) { top_show(0); }
void eng_TopWindow_ForceTacticalVisible(void *w)
{
    /* the tactical view draws the player's set: TacticalWindow focus (GotFocus 004fb920) calls SetManager::MakeRenderedSet with it, which
       also puts the player camera into its space modes (Camera.PlayerCameraAsSpace) */
    extern void *eng_Game_GetCurrentPlayer(void); extern void *eng_BaseObjectClass_GetContainingSet(void *); extern const char *eng_SetClass_GetName(void *);
    extern void eng_SetManager_MakeRenderedSet(void *, char *);
    void *pl = eng_Game_GetCurrentPlayer();
    void *set = pl ? eng_BaseObjectClass_GetContainingSet(pl) : NULL;
    const char *nm = set ? eng_SetClass_GetName(set) : NULL;
    if (nm) { char *c = strdup(nm); eng_SetManager_MakeRenderedSet(NULL, c); free(c); }
    top_show(1);
}
void eng_TopWindow_ToggleBridgeAndTactical(void *w) { if (eng_TopWindow_IsBridgeVisible(w)) top_show(1); else top_show(0); }
/* TopWindow::ToggleBridgeAndTacticalHandler 0050d370 (instance handler of the top window): when there is a living player, close the crew
   menus (BridgeHandlers.DropMenusTurnBack) and toggle. Not checked (marked): multiplayer, the game states 2/3 (+0x8c) and the top window
   flag +0x54 (meaning unknown). */
static void on_toggle_bridge_tactical(TGEvent *e)
{
    extern void *eng_Game_GetCurrentPlayer(void); void *pl = eng_Game_GetCurrentPlayer();
    if (!pl) return;
    tg_py_call_module("BridgeHandlers", "DropMenusTurnBack", 0, NULL);
    eng_TopWindow_ToggleBridgeAndTactical(top);
    e->dest = NULL;
}
void tg_ui_register_top_handlers(void) { tg_register_native(top, (int)tg_const("ET_INPUT_TOGGLE_BRIDGE_AND_TACTICAL"), on_toggle_bridge_tactical); }
static int edit_mode, kb_allowed = 1, mouse_allowed = 1, cutscene, fading; static void *last_rendered;
void eng_TopWindow_SetEditMode(void *w, int v) { edit_mode = v != 0; }
long eng_TopWindow_IsEditModeEnabled(void *w) { return edit_mode; }
void eng_TopWindow_ToggleEditMode(void *w) { edit_mode = !edit_mode; }
void eng_TopWindow_ToggleOptionsMenu(void *w) { UI *o = eng_TopWindow_FindMainWindow(w, 4); if (o) { if (o->flags & UF_VISIBLE) ui_hide(o, 1); else { ui_show(o, 1); pane_set_focus(top, o); } } }
void eng_TopWindow_ToggleConsole(void *w) { UI *o = eng_TopWindow_FindMainWindow(w, 2); if (o) { if (o->flags & UF_VISIBLE) ui_hide(o, 1); else ui_show(o, 1); } }
void eng_TopWindow_ToggleMapWindow(void *w) { UI *o = eng_TopWindow_FindMainWindow(w, 6); if (o) { if (o->flags & UF_VISIBLE) ui_hide(o, 1); else ui_show(o, 1); } }
void eng_TopWindow_ToggleCinematicWindow(void *w) { UI *o = eng_TopWindow_FindMainWindow(w, 7); if (o) { if (o->flags & UF_VISIBLE) ui_hide(o, 1); else ui_show(o, 1); } }
void eng_TopWindow_ToggleWireframe(void *w) { }
void eng_TopWindow_SetLastRenderedSet(void *w, void *s) { last_rendered = s; }
void *eng_TopWindow_GetLastRenderedSet(void *w) { return last_rendered; }
void eng_TopWindow_ShowBadConnectionText(void *w, int v) { }
void eng_TopWindow_Initialize(void *w) { tg_py_call_module("TopWindow", "Initialize", 'O', NULL); }
void eng_TopWindow_Update(void *w, float dt) { }
void eng_TopWindow_AllowKeyboardInput(void *w, int v) { kb_allowed = v != 0; }
long eng_TopWindow_IsKeyboardInputAllowed(void *w) { return kb_allowed; }
void eng_TopWindow_AllowMouseInput(void *w, int v) { mouse_allowed = v != 0; }
long eng_TopWindow_IsMouseInputAllowed(void *w) { return mouse_allowed; }
long eng_TopWindow_StartCutscene(void *w, float a, float b, int c) { cutscene = 1; return 1; }
long eng_TopWindow_EndCutscene(void *w, float a) { cutscene = 0; return 1; }
void eng_TopWindow_AbortCutscene(void *w) { cutscene = 0; }
long eng_TopWindow_IsCutsceneMode(void *w) { return cutscene; }
long eng_TopWindow_FadeOut(void *w, float t) { fading = 1; return 1; }
long eng_TopWindow_FadeIn(void *w, float t) { fading = 1; return 1; }
void eng_TopWindow_AbortFade(void *w) { fading = 0; }
long eng_TopWindow_IsFading(void *w) { return fading; }
void eng_TopWindow_DisableOptionsMenu(void *w, int v) { }
/* TGRootPane */
static char *cursor_name;
void eng_TGRootPane_SetMouseCursor(void *p, char *n, int a, int b, float f) { free(cursor_name); cursor_name = dup(n); }
void *eng_TGRootPane_GetMouseCursor(void *p) { return NULL; }
void eng_TGRootPane_SetCursorVisible(void *p, int a, int b) { }
void eng_TGRootPane_PushCursor(void *p, char *n, int a, int b, float f) { }
void eng_TGRootPane_PopCursor(void *p) { }
static UI *mouse_owner;
long eng_TGRootPane_GrabMouse(void *p, void *o) { if (mouse_owner && mouse_owner != o) return 0; mouse_owner = as_ui(o); return 1; }
long eng_TGRootPane_ReleaseMouse(void *p, void *o) { if (mouse_owner == o) { mouse_owner = NULL; return 1; } return 0; }
void *eng_TGRootPane_GetMouseGrabOwner(void *p) { return mouse_owner; }
void eng_TGRootPane_UpdateTrueFocus(void *p, int x) { UI *u = root; while (u && u->focus) u = u->focus; true_focus = u; }
void *eng_TGRootPane_GetTrueFocus(void *p) { return true_focus; }
void eng_TGRootPane_ShowFPS(void *p) { }
void eng_TGRootPane_HideFPS(void *p) { }
void eng_TGRootPane_Layout(void *p) { if (root) ui_layout(root); }

/* ================================================= basic widgets ================================================= */
void tg_ui_set_color(void *p, const float *c) { UI *u = as_ui(p); if (u && c) memcpy(u->color, c, 16); }
void ui_set_color(UI *u, PyObject *c) { float *q = ui_arg_ptr(c); if (u && q) memcpy(u->color, q, 16); }
/* TGWindow 0073e6a0: a pane whose only initial child is the sub pane (skip-parent flag); SetIndent offsets it. */
UI *window_new(const char *cls, float w, float h, float ix, float iy, UI *sub)
{
    UI *u = pane_new(cls, w, h);
    u->is_window = 1;
    if (!sub) sub = pane_new("TGPane", w, h);
    sub->flags |= UF_SKIPPARENT;
    pane_add(u, u->nkids, sub, 0, 0, 1);
    ui_set_pos(sub, u->x + ix, u->y + iy, 1);
    return u;
}
void *eng_TGWindow_Create(float x, float y, float w, float h, void *sub) { return window_new("TGWindow", w, h, x, y, as_ui(sub)); }
void *eng_new_TGWindow(float x, float y, float w, float h, void *sub) { return eng_TGWindow_Create(x, y, w, h, sub); }
void eng_TGWindow_SetIndent(void *p, float a, float b) { UI *u = as_pane(p); if (u && u->nkids) ui_set_pos(u->kids[0], u->x + a, u->y + b, 1); }
void *eng_TGWindow_GetSubPane(void *p) { UI *u = as_pane(p); return u && u->nkids ? u->kids[0] : NULL; }
void *eng_TGFrameWindow_Create(float w, float h, char *group, int num, float scale, int stretch, void *sub)
{
    UI *u = window_new("TGFrameWindow", w, h, 0, 0, as_ui(sub)), *f = ui_new("TGFrame");
    f->icon_group = dup(group); f->icon_num = num; f->w = w; f->h = h; pane_add(u, u->nkids, f, 0, 0, 1); return u;
}
void *eng_new_TGFrameWindow(float w, float h, char *group, int num, float scale, int stretch, void *sub) { return eng_TGFrameWindow_Create(w, h, group, num, scale, stretch, sub); }
void *eng_TGFrameWindow_CreateFromTheme(float w, float h, void *theme) { return eng_TGFrameWindow_Create(w, h, NULL, 0, 1.0f, 0, NULL); }
PyObject *eng_TGFrameWindow_GetFramePane(void *p) { UI *u = as_pane(p); return appc_from_ptr(u && u->nkids ? u->kids[u->nkids - 1] : NULL, "TGFrame"); }
void *eng_TGFrame_Create(char *group, int n) { UI *u = ui_new("TGFrame"); u->icon_group = dup(group); u->icon_num = n; return u; }
void *eng_new_TGFrame(char *group, int n, void *x, float y, int z) { return eng_TGFrame_Create(group, n); }
extern int tg_icon_size(const char *group, int num, float *w, float *h);
static void icon_size_to_art(UI *u, int layout) { float w, h; if (tg_icon_size(u->icon_group, u->icon_num, &w, &h)) ui_set_size(u, w, h, layout); }
void *eng_TGIcon_Create(char *group, int n, PyObject *color)                 /* 0073d2f0: sized to its artwork, never takes focus */
{ UI *u = ui_new("TGIcon"); u->icon_group = dup(group); u->icon_num = n; if (color) ui_set_color(u, color); icon_size_to_art(u, 0); u->flags |= UF_NOFOCUS; return u; }
void eng_TGIcon_SizeToArtwork(void *p, int layout) { UI *u = as_ui(p); if (u) icon_size_to_art(u, layout); }
void eng_TGIcon_SetClamp(void *p, int v) { }
long eng_TGIcon_GetClamp(void *p) { return 0; }
PyObject *eng_TGIcon_GetIconPoly(void *p) { Py_INCREF(Py_None); return Py_None; }
void *eng_new_TGIcon(char *group, int n, PyObject *color) { return eng_TGIcon_Create(group, n, color); }
void eng_TGIcon_SetIconNum(void *p, int n, int l) { UI *u = as_ui(p); if (u) u->icon_num = n; }
long eng_TGIcon_GetIconNum(void *p) { UI *u = as_ui(p); return u ? u->icon_num : 0; }
void eng_TGIcon_SetColor(void *p, PyObject *c) { ui_set_color(as_ui(p), c); }
void eng_TGIcon_GetColor(void *p, PyObject *c) { UI *u = as_ui(p); float *q = ui_arg_ptr(c); if (u && q) memcpy(q, u->color, 16); }
void eng_TGIcon_SetIconGroupName(void *p, char *g, int l) { UI *u = as_ui(p); if (u) { free(u->icon_group); u->icon_group = dup(g); } }
const char *eng_TGIcon_GetIconGroupName(void *p) { UI *u = as_ui(p); return u ? u->icon_group : NULL; }
void *eng_TGTextButton_Create(char *text, char *ev, int a, float b, int c, float d, float e) { UI *u = ui_new("TGTextButton"); u->text = eng_new_TGString(text ? text : ""); return u; }
void *eng_new_TGTextButton(char *text, char *ev, int a, float b, int c, float d, float e) { return eng_TGTextButton_Create(text, ev, a, b, c, d, e); }
void *eng_TGButtonBase_Create(float w, float h) { return pane_new("TGButtonBase", w, h); }
void *eng_new_TGButtonBase(float w, float h) { return pane_new("TGButtonBase", w, h); }
void eng_TGButton_SetIcons(void *p, char *g, int n)                        /* 00743a00: one icon child (the button takes its size) */
{
    UI *u = as_ui(p), *ic; if (!u) return;
    while (u->nkids) { UI *c = u->kids[0]; memmove(u->kids, u->kids + 1, (u->nkids - 1) * sizeof *u->kids); u->nkids--; c->parent = NULL; c->o.dead = 1; }
    free(u->icon_group); u->icon_group = dup(g); u->icon_num = n;
    ic = (UI *)eng_TGIcon_Create(g, n, NULL); pane_add(u, u->nkids, ic, 0, 0, 0); ui_set_size(u, ic->w, ic->h, 0);
}
void *eng_TGButton_Create(char *g, int n, PyObject *c)                      /* 007438e0: enabled, not selected */
{ UI *u = ui_new("TGButton"); u->flags |= UF_ENABLED; u->flags &= ~UF_SELECTED; if (c) ui_set_color(u, c); if (g) eng_TGButton_SetIcons(u, g, n); return u; }
void eng_TGButton_SetIconColor(void *p, PyObject *c) { UI *u = as_ui(p); if (u && u->nkids) ui_set_color(u->kids[0], c); }
long eng_TGButton_GetFirstIconNum(void *p) { UI *u = as_ui(p); return u ? u->icon_num : 0; }
void *eng_new_TGButton(char *g, int n, PyObject *c) { return eng_TGButton_Create(g, n, c); }
/* activation events (TGButtonBase): the stored event is duplicated and queued when the button is pressed */
void eng_TGButtonBase_SetActivationEvent(void *p, void *e) { UI *u = as_ui(p); if (u) u->act_ev = e; }
void eng_TGButtonBase_ClearActivationEvent(void *p) { UI *u = as_ui(p); if (u) u->act_ev = NULL; }
void *eng_TGButtonBase_GetActivationEvent(void *p) { UI *u = as_ui(p); return u ? u->act_ev : NULL; }
void ui_send_event(TGEvent *e, UI *src)
{
    extern void *eng_TGEvent_Duplicate(void *);
    TGEvent *d; if (!e) return; d = eng_TGEvent_Duplicate(e); if (!d->source) d->source = &src->o; tg_add_event(d);
}
extern void tg_toggle_activate(UI *b);
void eng_TGButtonBase_SendActivationEvent(void *p) { UI *u = as_ui(p); if (!u) return; if (u->o.cls && !strcmp(u->o.cls->name, "STToggle")) tg_toggle_activate(u); else ui_send_event(u->act_ev, u); }
void eng_TGButtonBase_SetMouseDownEvent(void *p, void *e) { UI *u = as_ui(p); if (u) u->down_ev = e; }
void eng_TGButtonBase_SendMouseDownEvent(void *p) { UI *u = as_ui(p); if (u) ui_send_event(u->down_ev, u); }
void eng_TGButtonBase_SetMouseUpEvent(void *p, void *e) { UI *u = as_ui(p); if (u) u->up_ev = e; }
void eng_TGButtonBase_SendMouseUpEvent(void *p) { UI *u = as_ui(p); if (u) ui_send_event(u->up_ev, u); }
void eng_TGButtonBase_ClearMouseDown(void *p, int x) { }

/* ================================================= STStylizedWindow ================================================= */
/* A TGWindow whose sub pane is a TGWindow (scroll window, its sub pane is the interior pane) and whose last child is the exterior pane.
   The look is a Python style class (module "StylizedWindow"): C++ calls style.Setup / Resize / GetBorderWidth / GetNameParagraph ... with
   (window, exterior, scroll). STStylizedWindow::CreateW 00530d80, CallStyleMethod 00531c60, InteriorChangedSize 005312c0, SetMaximumSize 00531a10. */
typedef struct STW {
    PyObject *style; char *module, *style_name;
    int has_max, fixed, minimizable, minimized, use_scrolling, focus_glass;
    float maxw, maxh, border_w, border_h, title_thick, scroll_view_h, scroll_speed, last_w, last_h, fixw, fixh;
    float color[4];
} STW;
static UI *as_stw(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x80f7) && ((UI *)p)->st ? p : NULL; }
int tg_is_stylized_window(const void *p) { return as_stw((void *)p) != NULL; }
static UI *stw_exterior(UI *w) { return w->nkids ? w->kids[w->nkids - 1] : NULL; }
static UI *stw_scroll(UI *w) { return w->nkids ? w->kids[0] : NULL; }
static void stw_on_scroll(TGEvent *e);
static UI *stw_interior(UI *w) { UI *s = stw_scroll(w); return s && s->nkids ? s->kids[0] : NULL; }
static PyObject *ui_obj(UI *u) { return u ? appc_pyobj(u, u->o.cls->name) : (Py_INCREF(Py_None), Py_None); }
/* CallStyleMethod: style.method(window, exterior, scroll); a new reference to the result or NULL */
static PyObject *style_call(UI *w, const char *method)
{
    PyObject *f, *r, *a, *b, *c, *args;
    if (!w->st || !w->st->style) return NULL;
    f = PyObject_GetAttrString(w->st->style, (char *)method); if (!f) { PyErr_Clear(); return NULL; }
    a = ui_obj(w); b = ui_obj(stw_exterior(w)); c = ui_obj(stw_scroll(w));
    if (!a || !b || !c) { PyErr_Print(); PyErr_Clear(); Py_XDECREF(a); Py_XDECREF(b); Py_XDECREF(c); Py_DECREF(f); return NULL; }
    args = PyTuple_New(3);
    PyTuple_SetItem(args, 0, a); PyTuple_SetItem(args, 1, b); PyTuple_SetItem(args, 2, c);
    r = PyEval_CallObject(f, args); Py_DECREF(f); Py_DECREF(args);
    if (!r) { PyErr_Print(); PyErr_Clear(); }
    return r;
}
static void style_void(UI *w, const char *m) { PyObject *r = style_call(w, m); Py_XDECREF(r); }
static void stw_update_border(UI *w)                                      /* 00531b50: style.GetBorderWidth returns a NiPoint2 */
{
    PyObject *r = style_call(w, "GetBorderWidth"); void *pt;
    if (r && r != Py_None && appc_to_ptr(r, &pt, "void")) { w->st->border_w = ((float *)pt)[0]; w->st->border_h = ((float *)pt)[1]; }
    PyErr_Clear(); Py_XDECREF(r);
}
static void stw_interior_changed(UI *w, int force)
{
    UI *inter = stw_interior(w), *scroll = stw_scroll(w); STW *s = w->st; float W = 0, H = 0; int i;
    if (!inter || !scroll) return;
    if (s->fixed && s->has_max)        /* a stack pane inside a fixed window may not be wider than the window's inside (approximation) */
        for (i = 0; i < inter->nkids; i++) { UI *c = inter->kids[i]; if (!strcmp(c->o.cls->name, "STSubPane") && c->w > s->maxw - s->border_w) ui_set_size(c, s->maxw - s->border_w, c->h, 0); }
    for (i = 0; i < inter->nkids; i++) { UI *c = inter->kids[i]; if (c->flags & UF_VISIBLE) { if (W < c->w + c->x) W = c->w + c->x; if (H < c->h + c->y) H = c->h + c->y; } }
    if (s->fixed) {
        float lim = s->has_max ? s->maxw : 1.0f; if (W < lim - s->border_w) W = lim - s->border_w;
        lim = s->has_max ? s->maxh : 1.0f; if (H < lim - s->border_h) H = lim - s->border_h;
    }
    if (s->minimized) H = 0;
    if (s->last_w != W || s->last_h != H || force) {
        s->last_w = W; s->last_h = H;
        ui_set_size(inter, W, H, 0); ui_set_size(scroll, W, H, 0);
        style_void(w, "Resize");
        if (s->scroll_view_h < 0) { if (inter->x != 0 || inter->y != 0) ui_set_pos(inter, 0, 0, 0); } else H = s->scroll_view_h;
        if (scroll->w != W || scroll->h != H) ui_set_size(scroll, W, H, 0);
    }
    style_void(w, "UpdateScrollArrows");
}
static void stw_set_max(UI *w, float mw, float mh)                         /* 00531a10 */
{
    STW *s = w->st; s->last_w = s->last_h = -1;
    if (mw < 0 || mh < 0) { s->has_max = 0; return; }
    s->maxw = ui_snap(mw, ui_scr_w()); s->maxh = ui_snap(mh, ui_scr_h()); s->has_max = 1;
    if (s->maxw < w->w) ui_set_size(w, s->maxw, w->h, 0);
    if (s->maxh < w->h) { ui_set_size(w, w->w, s->maxh, 0); stw_interior_changed(w, 0); return; }
    if (s->maxw < w->w) stw_interior_changed(w, 0);
}
/* the default window colour is the script global g_kInterfaceBorderColor (the default argument of py_STStylizedWindow_Create is 00988588) */
static const float *default_window_colour(void)
{
    const struct appc_global *g;
    for (g = appc_globals; g->name; g++) if (!strcmp(g->name, "g_kInterfaceBorderColor")) { const float *c = eng_global_get(g); return c && (c[0] || c[1] || c[2] || c[3]) ? c : NULL; }
    return NULL;
}
/* STStylizedWindow::CreateW 00530d80; also used by the C++ constructors of the subclasses (WeaponsDisplay, RadarDisplay, ShipDisplay...) with their class name */
UI *stw_new(const char *cls, const char *cm, const char *cs, void *title, float ix, float iy, UI *sub, int fixed, float fw, float fh, const float *color)
{
    UI *w, *ext; STW *s;
    w = window_new(cls, 0, 0, ix, iy, window_new("TGWindow", 0, 0, 0, 0, NULL));
    if (sub) {                                                    /* the sub pane is the interior: it replaces the scroll's default pane */
        UI *scr = stw_scroll(w);
        if (scr && scr->nkids) { scr->kids[0] = sub; sub->parent = scr; pane_set_focus(scr, sub); }
        else if (scr) pane_add(scr, 0, sub, 0, 0, 0);
    }
    ext = pane_new("TGPane", 0, 0); ext->flags |= UF_USEPARENTBATCH & 0; pane_add(w, w->nkids, ext, 0, 0, 0);
    s = calloc(1, sizeof *s); w->st = s; s->module = dup(cm); s->style_name = dup(cs);
    s->minimizable = 1; s->use_scrolling = 1; s->scroll_view_h = -1; s->scroll_speed = 0.5f; s->last_w = s->last_h = -1;
    s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;
    if (!color) color = default_window_colour();
    if (color) memcpy(s->color, color, 16);
    if (title) w->text = tg_str_copy(title);
    if (!w->text) w->text = eng_new_TGString("");
    { PyObject *st = NULL; if (tg_py_call_module(cm, cs, 'O', &st) == 0) s->style = st; }     /* module.StyleName() instantiates the style class */
    style_void(w, "Setup");
    tg_register_native(w, (int)tg_const("ET_ST_SCROLL"), stw_on_scroll);
    s->fixed = fixed != 0;
    if (s->fixed) { s->last_w = s->last_h = -1; if (fw == 0 && fh == 0) s->fixed = 0; else { s->fixed = 1; stw_set_max(w, fw, fh); } stw_interior_changed(w, 1); }
    { UI *i = stw_interior(w); if (i && w->nkids) pane_set_focus(stw_scroll(w), i); }
    stw_update_border(w);
    return w;
}
static PyObject *stw_create(PyObject *args)
{
    PyObject *mod, *sty, *title = NULL, *subpane = NULL, *color = NULL; float ix = 0, iy = 0, fw = 0, fh = 0; int fixed = 0;
    char *cm = NULL, *cs = NULL; void *t = NULL; float *c = NULL;
    if (!PyArg_ParseTuple(args, "OO|OffOiffO:STStylizedWindow_Create", &mod, &sty, &title, &ix, &iy, &subpane, &fixed, &fw, &fh, &color)) return NULL;
    if (!appc_to_cstr(mod, &cm) || !appc_to_cstr(sty, &cs)) return NULL;
    if (title && title != Py_None) t = ui_arg_ptr(title);
    if (color && color != Py_None) c = ui_arg_ptr(color);
    return appc_from_ptr(stw_new("STStylizedWindow", cm, cs, t, ix, iy, as_ui(ui_arg_ptr(subpane)), fixed, fw, fh, c), "STStylizedWindow");
}
PyObject *eng_STStylizedWindow_Create(PyObject *args) { return stw_create(args); }
PyObject *eng_STStylizedWindow_CreateW(PyObject *args) { return stw_create(args); }
void *eng_STStylizedWindow_Cast(PyObject *o) { void *p = ui_arg_ptr(o); return p && tg_is_ours(p) && tg_is_type(p, 0x80f7) ? p : NULL; }
PyObject *eng_STStylizedWindow_GetStyleInstance(void *p) { UI *w = as_stw(p); PyObject *r = w && w->st->style ? w->st->style : Py_None; Py_INCREF(r); return r; }
void eng_STStylizedWindow_SetName(void *p, void *t) { UI *w = as_stw(p); if (w && t) { w->text = tg_str_copy(t); } }
void *eng_STStylizedWindow_GetName(void *p) { UI *w = as_stw(p); return w ? tg_str_copy(w->text) : NULL; }   /* App.py takes ownership */
void *eng_STStylizedWindow_GetNameParagraph(void *p)
{
    UI *w = as_stw(p); PyObject *r = w ? style_call(w, "GetNameParagraph") : NULL; void *q = NULL;
    if (r && r != Py_None && !appc_to_ptr(r, &q, "void")) { PyErr_Clear(); q = NULL; }
    Py_XDECREF(r); return q;
}
void *eng_STStylizedWindow_GetExteriorPane(void *p) { UI *w = as_stw(p); return w ? stw_exterior(w) : NULL; }
void *eng_STStylizedWindow_GetInteriorPane(void *p) { UI *w = as_stw(p); return w ? stw_interior(w) : NULL; }
void eng_STStylizedWindow_InteriorChangedSize(void *p, int force) { UI *w = as_stw(p); if (w) stw_interior_changed(w, force); }
void eng_STStylizedWindow_SetMinimized(void *p, int layout) { UI *w = as_stw(p); if (w && !w->st->minimized && w->st->minimizable) { w->st->minimized = 1; ui_hide(stw_scroll(w), 0); stw_interior_changed(w, 0); if (layout) ui_layout(w); } }
void eng_STStylizedWindow_SetNotMinimized(void *p, int layout) { UI *w = as_stw(p); if (w && w->st->minimized) { w->st->minimized = 0; ui_show(stw_scroll(w), 0); stw_interior_changed(w, 0); if (layout) ui_layout(w); } }
long eng_STStylizedWindow_IsMinimized(void *p) { UI *w = as_stw(p); return w && w->st->minimized; }
void eng_STStylizedWindow_SetMinimizable(void *p, int v) { UI *w = as_stw(p); if (w) w->st->minimizable = v != 0; }
long eng_STStylizedWindow_IsMinimizable(void *p) { UI *w = as_stw(p); return w && w->st->minimizable; }
void eng_STStylizedWindow_SetFixedSize(void *p, float fw, float fh, int layout)       /* 00531880 */
{
    UI *w = as_stw(p); if (!w) return; w->st->last_w = w->st->last_h = -1;
    if (fw == 0 && fh == 0) w->st->fixed = 0; else { w->st->fixed = 1; stw_set_max(w, fw, fh); }
    stw_interior_changed(w, 1);          /* approximation: always (the engine does it only with layout), so the interior fits the new size */
}
long eng_STStylizedWindow_IsFixedSize(void *p) { UI *w = as_stw(p); return w && w->st->fixed; }
long eng_STStylizedWindow_IsScrolling(void *p) { return 0; }
long eng_STStylizedWindow_IsUseScrolling(void *p) { UI *w = as_stw(p); return w && w->st->use_scrolling; }
void eng_STStylizedWindow_SetUseScrolling(void *p, int v) { UI *w = as_stw(p); if (w) w->st->use_scrolling = v != 0; }
void eng_STStylizedWindow_SetUseFocusGlass(void *p, int v) { UI *w = as_stw(p); if (w) w->st->focus_glass = v != 0; }
long eng_STStylizedWindow_IsUseFocusGlass(void *p) { UI *w = as_stw(p); return w && w->st->focus_glass; }
void eng_STStylizedWindow_SetTitleBarThickness(void *p, float v, int layout)          /* 00531810 */
{ UI *w = as_stw(p); if (!w) return; w->st->title_thick = ui_snap(v, ui_scr_h()); stw_update_border(w); if (layout) stw_interior_changed(w, 1); }
double eng_STStylizedWindow_GetTitleBarThickness(void *p) { UI *w = as_stw(p); return w ? w->st->title_thick : 0.0; }
void eng_STStylizedWindow_SetMaximumSize(void *p, float mw, float mh) { UI *w = as_stw(p); if (w) stw_set_max(w, mw, mh); }
/* without a maximum size the maximum is the whole screen, 1.0 (py_STStylizedWindow_GetMaximumWidth 00625620) */
double eng_STStylizedWindow_GetMaximumWidth(void *p) { UI *w = as_stw(p); return w ? (w->st->has_max ? w->st->maxw : 1.0) : 0.0; }
double eng_STStylizedWindow_GetMaximumHeight(void *p) { UI *w = as_stw(p); return w ? (w->st->has_max ? w->st->maxh : 1.0) : 0.0; }
double eng_STStylizedWindow_GetBorderWidth(void *p) { UI *w = as_stw(p); return w ? w->st->border_w : 0.0; }
double eng_STStylizedWindow_GetBorderHeight(void *p) { UI *w = as_stw(p); return w ? w->st->border_h : 0.0; }
double eng_STStylizedWindow_GetMaximumInteriorWidth(void *p) { UI *w = as_stw(p); return w ? (w->st->has_max ? w->st->maxw : 1.0) - w->st->border_w : 0.0; }
double eng_STStylizedWindow_GetMaximumInteriorHeight(void *p) { UI *w = as_stw(p); return w ? (w->st->has_max ? w->st->maxh : 1.0) - w->st->border_h : 0.0; }
void eng_STStylizedWindow_SetScrollViewHeight(void *p, float v) { UI *w = as_stw(p); if (w) w->st->scroll_view_h = v; }
double eng_STStylizedWindow_GetScrollViewHeight(void *p) { UI *w = as_stw(p); return w ? w->st->scroll_view_h : 0.0; }
void eng_STStylizedWindow_SetScrollSpeed(void *p, float v) { UI *w = as_stw(p); if (w) w->st->scroll_speed = v; }
void eng_STStylizedWindow_ScrollToTop(void *p) { UI *w = as_stw(p); UI *i = w ? stw_interior(w) : NULL; if (i) ui_set_pos(i, i->x, 0, 0); }
void eng_STStylizedWindow_ScrollToBottom(void *p) { UI *w = as_stw(p); UI *i = w ? stw_interior(w) : NULL, *s = w ? stw_scroll(w) : NULL; if (i && s && i->h > s->h) ui_set_pos(i, i->x, s->h - i->h, 0); }
/* scroll the interior by d (screen heights, positive moves the content up); returns 1 when it moved. The scroll window is the view. */
long eng_STStylizedWindow_Scroll(void *p, float d)
{
    UI *w = as_stw(p), *inter = w ? stw_interior(w) : NULL, *view = w ? stw_scroll(w) : NULL; float lo, ny;
    if (!inter || !view || !w->st->use_scrolling) return 0;
    lo = view->h - inter->h; if (lo > 0) lo = 0;
    ny = inter->y - d; if (ny < lo) ny = lo; if (ny > 0) ny = 0;
    if (ny == inter->y) return 0;
    inter->y = ny; style_void(w, "UpdateScrollArrows"); return 1;
}
/* the scroll arrows of the window styles send ET_ST_SCROLL (int -1 up, 1 down, 0 stop); the window keeps scrolling while held (tg_ui_input_tick) */
static UI *scroll_win; static int scroll_dir;
static void stw_on_scroll(TGEvent *e)
{
    extern void tg_ui_sound_loop(const char *, int);
    scroll_win = (UI *)(e->dest); scroll_dir = (int)e->v.i;
    if (!scroll_dir) tg_ui_sound_loop("UIScrolling", 0);                    /* HandleScrollEvent 00531d60: stop */
}
void tg_stw_scroll_tick(double dt)
{
    extern void tg_ui_sound_loop(const char *, int);
    if (scroll_win && scroll_dir && as_stw(scroll_win)) {                    /* StartPeriodicScroll 00531e60: the scroll sound loops while the window moves */
        long moved = eng_STStylizedWindow_Scroll(scroll_win, (float)(scroll_dir * scroll_win->st->scroll_speed * dt));
        tg_ui_sound_loop("UIScrolling", moved != 0);
    }
}
void eng_STStylizedWindow_GetColor(void *p, PyObject *c) { UI *w = as_stw(p); float *q = ui_arg_ptr(c); if (w && q) memcpy(q, w->st->color, 16); }
void eng_STStylizedWindow_EnsureFocusIsVisible(void *p) { }

/* ---- provisional objects for the generated UI stubs (re/gen_ui_stubs.py) ---- */
void *ui_stub_new(const char *cls, float w, float h) { UI *u = pane_new(cls, w, h); return u; }
void *ui_stub_cast(void *p, unsigned ct) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; }
void ui_stub_log(const char *n) { if (getenv("APPC_TRACE")) fprintf(stderr, "[Appc] stubbed: %s\n", n); }

/* ---- TGParagraph (text) ---- */
/* The size comes from the font: here an estimate (glyph width 0.55 * size, line height 1.25 * size) until the font data of the icon groups is used. */
extern int tg_text_extent(void *fg, const char *s, float wrap_px, float *w, float *h);
static void para_measure(UI *u)
{
    float w, h;
    if (!tg_text_extent(u->font, u->text ? eng_TGString_GetCString(u->text) : "", u->wrap > 0 ? u->wrap * ui_scr_w() : 0, &w, &h)) { w = 0; h = 15.0f; }
    ui_set_size(u, w / ui_scr_w(), h / ui_scr_h(), 0);
}
extern void *eng_TGFontManager_GetFontGroup(void *m, char *name, float size);
static void para_font(UI *u, char *font, float size) { if (font && *font) { void *g = eng_TGFontManager_GetFontGroup(NULL, font, size); if (g) u->font = g; } }
static UI *as_para(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x209) ? p : NULL; }
static void para_set(UI *u, const char *t) { if (!u->text) u->text = eng_new_TGString(""); eng_TGString_SetString(u->text, (char *)t); para_measure(u); }

void *eng_TGParagraph_Create(char *text, float wrap, PyObject *color, char *font, float f, int flags)
{ UI *u = ui_new("TGParagraph"); u->wrap = wrap; if (color) ui_set_color(u, color); para_font(u, font, f); para_set(u, text ? text : ""); return u; }
void *eng_TGParagraph_CreateW(void *text, float wrap, PyObject *color, char *font, float f, int flags)
{ UI *u = ui_new("TGParagraph"); u->wrap = wrap; if (color) ui_set_color(u, color); para_font(u, font, f); u->text = text ? tg_str_copy(text) : eng_new_TGString(""); para_measure(u); return u; }
void *eng_new_TGParagraph(char *text, float wrap, PyObject *color, char *font, float f, int flags) { return eng_TGParagraph_Create(text, wrap, color, font, f, flags); }
void *eng_TGParagraph_Cast(PyObject *o) { void *p = ui_arg_ptr(o); return as_para(p); }
void *eng_TGParagraph_GetFontGroup(void *p) { extern void *eng_TGFontManager_GetDefaultFont(void *); return eng_TGFontManager_GetDefaultFont(NULL); }
void eng_TGParagraph_SetFontGroup(void *p, void *g, int l) { UI *u = as_para(p); if (u) para_measure(u); }
void eng_TGParagraph_SetColor(void *p, PyObject *c) { ui_set_color(as_para(p), c); }
/* the editing state of a paragraph (kept in ext): read-only unless a script clears it (the text entries of the menus do), the caret, the
   limit of characters and the characters that are not accepted */
typedef struct { int editable, caret, maxc; char *ignore; } ParaX;
static ParaX *para_x(UI *u) { if (!u->ext) { ParaX *x = calloc(1, sizeof *x); x->maxc = 0x7fffffff; u->ext = x; } return u->ext; }
void eng_TGParagraph_SetReadOnly(void *p, int v) { UI *u = as_para(p); if (u) para_x(u)->editable = !v; }
long eng_TGParagraph_IsReadOnly(void *p) { UI *u = as_para(p); return !(u && u->ext && ((ParaX *)u->ext)->editable); }
int tg_para_editable(const void *p) { const UI *u = p; return u && u->ext && tg_is_type((TGObject *)&u->o, 0x209) && ((ParaX *)u->ext)->editable; }
int tg_para_caret(const void *p) { const UI *u = p; int n; if (!tg_para_editable(p)) return -1; n = u->text ? (int)strlen(eng_TGString_GetCString(u->text)) : 0; return ((ParaX *)u->ext)->caret > n ? n : ((ParaX *)u->ext)->caret; }
void eng_TGParagraph_SetCursorBlinkInterval(float v) { }
void eng_TGParagraph_GetString(void *p, void *out) { UI *u = as_para(p); if (u && out) tg_str_assign(out, u->text); }
double eng_TGParagraph_GetWrapWidth(void *p) { UI *u = as_para(p); return u ? u->wrap : 0.0; }
void eng_TGParagraph_SetWrapWidth(void *p, float w, int l) { UI *u = as_para(p); if (u) { u->wrap = w; para_measure(u); } }
void eng_TGParagraph_SetMaxChars(void *p, int n) { UI *u = as_para(p); if (u) para_x(u)->maxc = n; }
long eng_TGParagraph_GetMaxChars(void *p) { UI *u = as_para(p); return u ? para_x(u)->maxc : 0x7fffffff; }
void eng_TGParagraph_InsertChild(void *p, int i, void *c, float x, float y, int l) { UI *u = as_para(p), *cc = as_ui(c); if (u && cc) pane_add(u, i, cc, x, y, 0); }
void eng_TGParagraph_RecalcBounds(void *p, int l) { UI *u = as_para(p); if (u) para_measure(u); }
static void para_edit(void *p, int pos, const char *ins, int replace)
{
    UI *u = as_para(p); char *t, *r; int n; if (!u) return;
    t = strdup(u->text ? eng_TGString_GetCString(u->text) : ""); n = (int)strlen(t);
    if (replace) { free(t); t = strdup(""); n = 0; pos = 0; }
    if (pos < 0 || pos > n) pos = n;
    r = malloc(n + strlen(ins) + 1); memcpy(r, t, pos); strcpy(r + pos, ins); strcpy(r + pos + strlen(ins), t + pos);
    para_set(u, r); free(t); free(r);
}
void eng_TGParagraph_InsertChar(void *p, int pos, int c) { char b[2]; b[0] = (char)c; b[1] = 0; para_edit(p, pos, b, 0); }
void eng_TGParagraph_AppendChar(void *p, int c, int l) { char b[2]; b[0] = (char)c; b[1] = 0; para_edit(p, -1, b, 0); }
void eng_TGParagraph_InsertString(void *p, char *s, int pos) { para_edit(p, pos, s ? s : "", 0); }
void eng_TGParagraph_AppendString(void *p, char *s, int l) { para_edit(p, -1, s ? s : "", 0); }
void eng_TGParagraph_InsertStringW(void *p, void *s, int pos) { para_edit(p, pos, s ? eng_TGString_GetCString(s) : "", 0); }
void eng_TGParagraph_AppendStringW(void *p, void *s, int l) { para_edit(p, -1, s ? eng_TGString_GetCString(s) : "", 0); }
void eng_TGParagraph_SetString(void *p, char *s, int l) { para_edit(p, 0, s ? s : "", 1); }
void eng_TGParagraph_SetStringW(void *p, void *s, int l) { para_edit(p, 0, s ? eng_TGString_GetCString(s) : "", 1); }
void eng_TGParagraph_SetIgnoreString(void *p, char *s) { UI *u = as_para(p); if (u) { free(para_x(u)->ignore); para_x(u)->ignore = strdup(s ? s : ""); } }
void eng_TGParagraph_SetIgnoreStringW(void *p, void *s) { eng_TGParagraph_SetIgnoreString(p, s ? (char *)eng_TGString_GetCString(s) : ""); }
void eng_TGParagraph_SetCursorPosition(void *p, int n) { UI *u = as_para(p); if (u) para_x(u)->caret = n < 0 ? 0 : n; }
void eng_TGParagraph_MoveCursorToStart(void *p) { UI *u = as_para(p); if (u) para_x(u)->caret = 0; }
void eng_TGParagraph_MoveCursorToEnd(void *p) { UI *u = as_para(p); if (u) para_x(u)->caret = 0x7fffffff; }
/* typing into an editable paragraph (tg_ui_input.c): text at the caret (characters of the ignore string are dropped, the length is limited),
   Backspace, Delete, and the caret moves (-1 left, 1 right, -2 start, 2 end) */
int tg_para_insert(UI *u, const char *text)
{
    ParaX *x; char *t, *r; int n, pos, k, add = 0; char buf[64];
    if (!tg_para_editable(u)) return 0;
    x = u->ext; t = strdup(u->text ? eng_TGString_GetCString(u->text) : ""); n = (int)strlen(t); pos = x->caret > n ? n : x->caret;
    for (k = 0; text[k] && add < 63; k++) {
        if ((unsigned char)text[k] < 32 || (x->ignore && strchr(x->ignore, text[k]))) continue;
        if (n + add + 1 > x->maxc) break;
        buf[add++] = text[k];
    }
    buf[add] = 0;
    r = malloc(n + add + 1); memcpy(r, t, pos); memcpy(r + pos, buf, add); strcpy(r + pos + add, t + pos);
    x->caret = pos + add; para_set(u, r); free(t); free(r);
    return add;
}
int tg_para_delete(UI *u, int backwards)
{
    ParaX *x; char *t; int n, pos;
    if (!tg_para_editable(u)) return 0;
    x = u->ext; t = strdup(u->text ? eng_TGString_GetCString(u->text) : ""); n = (int)strlen(t); pos = x->caret > n ? n : x->caret;
    if (backwards && pos > 0) { memmove(t + pos - 1, t + pos, n - pos + 1); x->caret = pos - 1; para_set(u, t); }
    else if (!backwards && pos < n) { memmove(t + pos, t + pos + 1, n - pos); para_set(u, t); }
    free(t); return 1;
}
void tg_para_move(UI *u, int how)
{
    ParaX *x; int n;
    if (!tg_para_editable(u)) return;
    x = u->ext; n = u->text ? (int)strlen(eng_TGString_GetCString(u->text)) : 0;
    if (x->caret > n) x->caret = n;
    if (how == -1 && x->caret > 0) x->caret--; else if (how == 1 && x->caret < n) x->caret++; else if (how == -2) x->caret = 0; else if (how == 2) x->caret = n;
}
const char *eng_TGParagraph_GetCString(void *p) { UI *u = as_para(p); return u && u->text ? eng_TGString_GetCString(u->text) : ""; }
/* TGTextButton / TGFrame */
void eng_TGTextButton_SetText(void *p, char *t) { UI *u = as_ui(p); if (u) { if (!u->text) u->text = eng_new_TGString(""); eng_TGString_SetString(u->text, t); } }
void eng_TGTextButton_SetTextW(void *p, void *t) { UI *u = as_ui(p); if (u && t) u->text = tg_str_copy(t); }
void *eng_TGTextButton_CreateW(void *text, char *ev, int a, float b, int c, float d, float e) { UI *u = ui_new("TGTextButton"); u->text = text ? tg_str_copy(text) : eng_new_TGString(""); return u; }
void eng_TGTextButton_SetIcons(void *p, char *g, int n) { }
void eng_TGTextButton_SetTextWrapWidth(void *p, float w) { }
void eng_TGTextButton_AlignTextHorizontal(void *p, int a, int l) { }
void eng_TGTextButton_AlignTextVertical(void *p, int a, int l) { }
void eng_TGTextButton_GetInnerRect(void *p, void *out) { UI *u = as_ui(p); Rect *r = out; if (u && r) { r->l = r->t = 0; r->w = u->w; r->h = u->h; } }
void eng_TGFrame_SetIconGroupName(void *p, char *g, int l) { UI *u = as_ui(p); if (u) { free(u->icon_group); u->icon_group = dup(g); } }
void eng_TGFrame_SetScale(void *p, float s, int l) { }
void eng_TGFrame_SetStretchType(void *p, int t, int l) { }
void eng_TGFrame_SetFirstIconNum(void *p, int n, int l) { UI *u = as_ui(p); if (u) u->icon_num = n; }
void *eng_TGFrame_GetInnerRect(void *p) { UI *u = as_ui(p); Rect *r = calloc(1, sizeof *r); if (u) { r->w = u->w; r->h = u->h; } return r; }
PyObject *eng_TGFrame_GetNiColor(void *p) { UI *u = as_ui(p); float *c = malloc(16); if (u) memcpy(c, u->color, 16); return appc_from_ptr(c, "NiColorA"); }   /* App.py wraps the pointer (NiColorAPtr) */
void eng_TGFrame_SetNiColor(void *p, float r, float g, float b, float a) { UI *u = as_ui(p); if (u) { u->color[0] = r; u->color[1] = g; u->color[2] = b; u->color[3] = a; } }
void eng_TGFrame_SetEdgeStretch(void *p, int s, int l) { }
long eng_TGFrame_GetEdgeStretch(void *p) { return 0; }


/* ================================================= FocusManager ================================================= */
/* The tab order: the objects that scripts register (the main menu registers its top button pane, the tab panes and the entry panes). Tab goes to
   the next one that is completely visible (FocusManager::Tab 0044d650); the walk itself is in tg_ui_input.c. */
static UI *tab_list[128]; static int ntab;
void eng_FocusManager_AddObjectToTabOrder(void *m, void *o) { UI *u = as_ui(o); int i; if (!u) return; for (i = 0; i < ntab; i++) if (tab_list[i] == u) return; if (ntab < 128) tab_list[ntab++] = u; }
static void tab_drop(int i) { memmove(tab_list + i, tab_list + i + 1, (ntab - i - 1) * sizeof *tab_list); ntab--; }
void eng_FocusManager_RemoveObjectFromTabOrder(void *m, void *o) { UI *u = as_ui(o); int i; for (i = 0; i < ntab; i++) if (tab_list[i] == u) { tab_drop(i); return; } }
void eng_FocusManager_RemoveAllObjects(void *m) { ntab = 0; }
void eng_FocusManager_RemoveAllObjectsUnder(void *m, void *o)           /* everything inside a pane that is being thrown away */
{
    UI *u = as_ui(o); int i;
    for (i = ntab - 1; i >= 0; i--) { UI *p; for (p = tab_list[i]; p; p = p->parent) if (p == u) { tab_drop(i); break; } }
}
int tg_focus_tab_list(UI **out, int max) { int i; for (i = 0; i < ntab && i < max; i++) out[i] = tab_list[i]; return i < max ? i : max; }
extern int tg_ui_tab(int backwards);
void eng_FocusManager_Tab(void *m) { tg_ui_tab(0); }
void *eng_new_FocusManager(void) { return calloc(1, 64); }
void eng_delete_FocusManager(void *p) { }
