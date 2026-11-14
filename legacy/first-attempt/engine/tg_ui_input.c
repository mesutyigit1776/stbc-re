/* UI input: the mouse and the keyboard in the menus and dialogs.
   Mouse: the interactive nodes of the visible tree come from the draw walk (tg_ui_collect, mode pixels, back to front). A move highlights the
   node under the cursor. A left press on a button, toggle, menu title or numeric bar takes the mouse; the release over the same node clicks it
   (STButton::ButtonClicked, STRoundedButton::HandleClick: tg_button_press). Button events first go down the parents of the node as
   TGMouseEvents, so the instance handlers of the scripts see them before the native action (TGPane::MouseHandler 0072e1e0 hands the event to
   the children in the same order); a handler that sets the event handled stops the native action.
   Keyboard: a TGKeyboardEvent goes down the focus chain of the context window (options window, modal dialog or a dialog pane); the instance
   handlers of the scripts (arrow keys of the main menu buttons: HandleKeyboardTopButtonArea) see it first, as in TGPane::KeyboardHandler
   0072e350 and TGRootPane::KeyboardHandler 007281d0. What no script handled is done here: arrow keys move the focus to the nearest control in
   that direction, Enter and Space press the focused control (TGButtonBase::KeyboardHandler 0073f9a0 does Enter), Tab and Shift+Tab walk the
   tab order (FocusManager::Tab 0044d650), the arrow keys change a numeric bar, Right and Left open and close a menu, Page Up and Page Down,
   Home and End scroll, Escape closes an open menu or leaves a text entry. The original has no arrow keys beyond the stock handlers; this is the
   port's addition (docs/ui-keyboard-navigation.md).
   Approximations: mouse moves are not sent to script handlers; a click on a menu title always opens or closes it (the open and close flags are
   not checked); the tab order contains only the objects the scripts add with AddObjectToTabOrder, then all controls. */
#include <Python.h>
#include <math.h>
#include <stdio.h>
#include "tg_ui.h"
#include "tg_ui_draw.h"

extern long tg_const(const char *name);
extern const char *eng_TGString_GetCString(void *);
extern void *eng_TopWindow_GetTopWindow(void);
extern long eng_GraphicsModeInfo_GetWidth(void *);
extern long eng_GraphicsModeInfo_GetHeight(void *);
extern long eng_STStylizedWindow_Scroll(void *p, float d);
extern void tg_stw_scroll_tick(double dt);
extern void tg_button_press(UI *b);
extern int tg_is_toggle(const void *node);
extern void tg_toggle_step(UI *b, int dir);
extern void tg_menu_toggle(UI *m, int want_open);
extern void tg_numeric_set_fraction(UI *b, float f);
extern void tg_numeric_step(UI *b, int dir);
extern int tg_is_numeric_bar(const void *node);
extern int tg_is_menu(const void *node);
extern int tg_menu_opened(const void *node);
extern int tg_para_insert(UI *u, const char *text);
extern int tg_para_delete(UI *u, int backwards);
extern void tg_para_move(UI *u, int how);
extern int tg_para_editable(const void *p);
extern int tg_is_stylized_window(const void *node);
extern void eng_ModalDialogWindow_Cancel(void *self);

#define MAX_HITS 2048
static TgUiHit hits[MAX_HITS]; static int nhits;
static float mouse_x, mouse_y; static int mouse_seen;                    /* mode pixels */
static UI *hover, *pressed, *kfocus, *textfocus; static int pressed_kind, pressed_key;
static int ET_MOUSE_T, ET_KEYBOARD_T;

/* ---- the events that go to the scripts ---- */
static TGEvent *mk_event(const char *cls, int type) { TGEvent *e = tg_ev_new(cls, 0); e->type = (unsigned)type; return e; }
static void done(TGEvent *e) { e->o.dead = 1; }
static int handled(const TGEvent *e) { return (e->flags & 0x100) != 0; }

/* ---- which window gets the input ---- */
static int is_main_window(const UI *u) { return tg_is_type((TGObject *)&u->o, 0x810f); }
/* The mouse also works in the bridge and tactical views (their windows hold the interface panes); the keys stay with the game there.
   A click that hits no control goes to the game. */
static int mouse_ctx;
static UI *context(void)
{
    UI *tw = eng_TopWindow_GetTopWindow(); int i;
    if (!tw) return NULL;
    for (i = 0; i < tw->nkids; i++) {                           /* index 0 is the front-most child */
        UI *c = tw->kids[i];
        if (!(c->flags & UF_COMPVISIBLE)) continue;
        if (is_main_window(c)) { if (c->mwt == 4 || c->mwt == (int)tg_const("MWT_MODAL_DIALOG") || 0) return c; continue; }
        return c;                                               /* a dialog pane that a script put on the top window */
    }
    if (mouse_ctx) for (i = 0; i < tw->nkids; i++) { UI *c = tw->kids[i]; if ((c->flags & UF_COMPVISIBLE) && is_main_window(c) && (c->mwt == 0 || c->mwt == 1)) return c; }
    return NULL;
}
static int under(const UI *n, const UI *root) { for (; n; n = n->parent) if (n == root) return 1; return 0; }
/* the controls of the window that has the input (a modal dialog hides everything behind it) */
static void refresh(void)
{
    UI *ctx = context(); int i, k = 0;
    nhits = tg_ui_collect(hits, MAX_HITS);
    if (!ctx) return;
    for (i = 0; i < nhits; i++) if (under(hits[i].node, ctx)) hits[k++] = hits[i];
    nhits = k;
}
static int in_hits(const void *n) { int i; for (i = 0; i < nhits; i++) if (hits[i].node == n) return 1; return 0; }
static const TgUiHit *hit_of(const void *n) { int i; for (i = nhits - 1; i >= 0; i--) if (hits[i].node == n) return &hits[i]; return NULL; }
static int in_clip(const TgUiHit *k, float x, float y) { return !(k->clip[2] > k->clip[0]) || (x >= k->clip[0] && x < k->clip[2] && y >= k->clip[1] && y < k->clip[3]); }
static const TgUiHit *top_hit(float x, float y)
{
    int i;
    for (i = nhits - 1; i >= 0; i--) {
        const TgUiHit *k = &hits[i];
        if (x >= k->x0 && x < k->x1 && y >= k->y0 && y < k->y1 && in_clip(k, x, y)) return k;
    }
    return NULL;
}
static int enabled(const UI *u) { return (u->flags & UF_ENABLED) != 0; }

/* ---- hover and focus marks ---- */
static void set_highlight(UI *u, int on) { if (!u) return; if (on) u->flags |= UF_HIGHLIGHTED; else u->flags &= ~UF_HIGHLIGHTED; }
static void focus_chain(UI *n) { for (; n && n->parent; n = n->parent) pane_set_focus(n->parent, n); }      /* the object is on the focus path of every window above it */
static void focus_set(UI *n)
{
    if (kfocus == n) return;
    if (kfocus && kfocus != hover) set_highlight(kfocus, 0);
    kfocus = n;
    if (n) { set_highlight(n, 1); focus_chain(n); }
    if (textfocus && textfocus != n) textfocus = NULL;
}
void tg_ui_focus_set(UI *n) { refresh(); focus_set(n); }
const void *tg_ui_focus_node(void) { return kfocus && in_hits(kfocus) ? kfocus : NULL; }
int tg_ui_text_focus(const void *p) { return p && p == textfocus; }
int tg_ui_wants_text(void) { return textfocus != NULL; }
void tg_ui_mouse_state(float *x, float *y, int *show) { *x = mouse_x; *y = mouse_y; mouse_ctx = 1; *show = mouse_seen && context() != NULL; mouse_ctx = 0; }
int tg_ui_pressed(const void *p) { return p && p == pressed; }
int tg_ui_cursor_visible(void) { int r; mouse_ctx = 1; r = context() != NULL; mouse_ctx = 0; return r; }

/* ---- mouse ---- */
static void norm(float x, float y, float *nx, float *ny) { *nx = x / (float)eng_GraphicsModeInfo_GetWidth(NULL); *ny = y / (float)eng_GraphicsModeInfo_GetHeight(NULL); }
/* the button event goes down the parents of the node (top first); returns 1 when a script handled it */
static int send_mouse(UI *node, int flags, float x, float y)
{
    UI *path[80]; int n = 0, i, r = 0; TGEvent *e; float nx, ny;
    for (; node && n < 80; node = node->parent) path[n++] = node;
    norm(x, y, &nx, &ny);
    e = mk_event("TGMouseEvent", ET_MOUSE_T); e->flags = (unsigned short)flags; e->mx = nx; e->my = ny;
    for (i = n - 1; i >= 0 && !handled(e); i--) { e->dest = &path[i]->o; tg_event_process(path[i], e); }
    r = handled(e); done(e);
    return r;
}
static void numeric_from_x(UI *b, float x)
{
    const TgUiHit *k = hit_of(b); if (!k || k->x1 <= k->x0) return;
    tg_numeric_set_fraction(b, (x - k->x0) / (k->x1 - k->x0));
}
static void mouse_move_impl(float x, float y);
void tg_ui_mouse_move(float x, float y) { mouse_ctx = 1; mouse_move_impl(x, y); mouse_ctx = 0; }
static void mouse_move_impl(float x, float y)
{
    const TgUiHit *k; UI *n = NULL;
    mouse_x = x; mouse_y = y; mouse_seen = 1;
    refresh();
    if (pressed && pressed_kind == 3) { numeric_from_x(pressed, x); return; }
    k = top_hit(x, y);
    if (k && k->kind >= 1 && k->kind <= 3 && enabled((UI *)k->node)) n = (UI *)k->node;
    if (n != hover) {
        if (hover && hover != kfocus) set_highlight(hover, 0);
        hover = n;
        if (n) set_highlight(n, 1);
    }
}
/* returns 1 when the UI took the press or release (the game bindings must not see it) */
static int mouse_button_impl(int button, int down, float x, float y);
int tg_ui_mouse_button(int button, int down, float x, float y) { int r; mouse_ctx = 1; r = mouse_button_impl(button, down, x, y); mouse_ctx = 0; return r; }
static int mouse_button_impl(int button, int down, float x, float y)
{
    const TgUiHit *k; UI *n; int left = button == 1, flags;
    mouse_x = x; mouse_y = y; mouse_seen = 1;
    if (!context()) return 0;
    refresh();
    flags = (down ? 0x200 : 0x400) | (button == 1 ? 0x800 : button == 3 ? 0x1000 : 0x1800);
    k = top_hit(x, y);
    if (!left) return k != NULL && send_mouse((UI *)k->node, flags, x, y);
    if (down) {
        if (!k) { if (textfocus) textfocus = NULL; return 0; }
        n = (UI *)k->node;
        if (send_mouse(n, flags, x, y)) return 1;                           /* a script took it */
        if (k->kind == 4) { textfocus = n; focus_set(n); tg_para_move(n, 2); return 1; }
        if (textfocus) textfocus = NULL;
        if (k->kind == 5) return 1;
        if (!enabled(n)) return 1;
        pressed = n; pressed_kind = k->kind; focus_set(n);
        if (k->kind == 3) numeric_from_x(n, x);
        if (k->kind == 1 && n->down_ev) ui_send_event(n->down_ev, n);        /* TGButtonBase mouse-down event (the scroll arrows) */
        return 1;
    }
    /* release */
    if (pressed) {
        UI *p = pressed; int kind = pressed_kind; int inside = k && k->node == p;
        pressed = NULL;
        send_mouse(p, flags, x, y);
        if (kind == 1) { if (p->up_ev) ui_send_event(p->up_ev, p); if (inside) tg_button_press(p); }
        else if (kind == 2 && inside) tg_menu_toggle(p, !tg_menu_opened(p));
        return 1;
    }
    return k != NULL;
}
/* the wheel scrolls the stylized window under the cursor; returns 1 when it did */
static UI *scroll_window_of(const void *node)
{
    const UI *u = node;
    for (; u; u = u->parent) if (u->parent && tg_is_stylized_window(u->parent) && u->parent->nkids && u->parent->kids[0] == u) return u->parent;
    return NULL;
}
static int wheel_impl(float dy);
int tg_ui_wheel(float dy) { int r; mouse_ctx = 1; r = wheel_impl(dy); mouse_ctx = 0; return r; }
static int wheel_impl(float dy)
{
    const TgUiHit *k; UI *w; int i;
    if (!context() || dy == 0) return 0;
    refresh();
    k = top_hit(mouse_x, mouse_y);
    if (!k) return 0;
    w = scroll_window_of(k->node);
    if (!w) for (i = nhits - 1; i >= 0; i--) if (hits[i].kind == 5 && mouse_x >= hits[i].x0 && mouse_x < hits[i].x1 && mouse_y >= hits[i].y0 && mouse_y < hits[i].y1) { w = (UI *)hits[i].node; break; }
    if (!w) return 0;
    eng_STStylizedWindow_Scroll(w, -dy * 0.06f);                            /* a wheel step is about two rows */
    refresh();
    return 1;
}

/* ---- keyboard ---- */
static int WC_LEFT_, WC_RIGHT_, WC_UP_, WC_DOWN_, WC_RETURN_, WC_ENTER_, WC_SPACE_, WC_TAB_, WC_BACK_, WC_DEL_, WC_ESC_, WC_PGUP_, WC_PGDN_, WC_HOME_, WC_END_;
static void init_codes(void)
{
    if (ET_KEYBOARD_T) return;
    ET_KEYBOARD_T = (int)tg_const("ET_KEYBOARD"); ET_MOUSE_T = (int)tg_const("ET_MOUSE");
    WC_LEFT_ = (int)tg_const("WC_LEFT"); WC_RIGHT_ = (int)tg_const("WC_RIGHT"); WC_UP_ = (int)tg_const("WC_UP"); WC_DOWN_ = (int)tg_const("WC_DOWN");
    WC_RETURN_ = (int)tg_const("WC_RETURN"); WC_ENTER_ = (int)tg_const("WC_NUMPADENTER"); WC_SPACE_ = (int)tg_const("WC_SPACE"); WC_TAB_ = (int)tg_const("WC_TAB");
    WC_BACK_ = (int)tg_const("WC_BACKSPACE"); WC_DEL_ = (int)tg_const("WC_DELETE"); WC_ESC_ = (int)tg_const("WC_ESCAPE");
    WC_PGUP_ = (int)tg_const("WC_PAGEUP"); WC_PGDN_ = (int)tg_const("WC_PAGEDOWN"); WC_HOME_ = (int)tg_const("WC_HOME"); WC_END_ = (int)tg_const("WC_END");
}
/* the event goes down the focus chain; the handlers of each node run first. Returns 1 when a handler took it. */
static int send_key(UI *ctx, int wc, int state)
{
    UI *n; TGEvent *e; int r;
    e = mk_event("TGKeyboardEvent", ET_KEYBOARD_T); e->ukey = wc; e->kstate = state;
    for (n = ctx; n && !handled(e); n = (n->focus && (n->focus->flags & UF_COMPVISIBLE)) ? n->focus : NULL) { e->dest = &n->o; tg_event_process(n, e); }
    r = handled(e); done(e);
    return r;
}
/* the focus leaf of the context that is a control, for after a script moved the focus */
static void sync_focus(UI *ctx)
{
    UI *n = ctx;
    while (n && n->focus && (n->focus->flags & UF_COMPVISIBLE)) n = n->focus;
    if (n && in_hits(n)) { const TgUiHit *k = hit_of(n); if (k && k->kind >= 1 && k->kind <= 4 && n != kfocus) focus_set(n); }
}
static int is_control(const TgUiHit *k) { return k->kind >= 1 && k->kind <= 4 && enabled((UI *)k->node) && in_clip(k, (k->x0 + k->x1) * 0.5f, (k->y0 + k->y1) * 0.5f); }
/* the nearest control in a direction: dir 0 left, 1 right, 2 up, 3 down. Distance along the direction plus twice the one across it. */
static UI *nearest(const TgUiHit *from, int dir)
{
    float cx = (from->x0 + from->x1) * 0.5f, cy = (from->y0 + from->y1) * 0.5f, best = 1e30f; UI *pick = NULL; int i;
    for (i = 0; i < nhits; i++) {
        const TgUiHit *k = &hits[i]; float dx, dy, along, across, cost;
        if (k == from || !is_control(k)) continue;
        dx = (k->x0 + k->x1) * 0.5f - cx; dy = (k->y0 + k->y1) * 0.5f - cy;
        if (dir == 0) { along = -dx; across = fabsf(dy); } else if (dir == 1) { along = dx; across = fabsf(dy); }
        else if (dir == 2) { along = -dy; across = fabsf(dx); } else { along = dy; across = fabsf(dx); }
        if (along < 2.0f || across > along * 0.75f + 8.0f) continue;                /* within about 37 degrees of the direction */
        cost = along + 3.0f * across;
        if (cost < best) { best = cost; pick = (UI *)k->node; }
    }
    return pick;
}
static UI *first_control(void)                                             /* top left control */
{
    float best = 1e30f; UI *pick = NULL; int i;
    for (i = 0; i < nhits; i++) {
        const TgUiHit *k = &hits[i]; float c;
        if (!is_control(k)) continue;
        c = k->y0 * 4.0f + k->x0;
        if (c < best) { best = c; pick = (UI *)k->node; }
    }
    return pick;
}
/* keep the focused control inside the view of its scrolling window */
static void ensure_visible(UI *n)
{
    const TgUiHit *k = hit_of(n); UI *w = k ? scroll_window_of(n) : NULL; int i; float mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    if (!k || !w) return;
    for (i = nhits - 1; i >= 0; i--) if (hits[i].kind == 5 && hits[i].node == w) {
        float over = k->y1 - hits[i].y1, under = hits[i].y0 - k->y0;
        if (over > 0) eng_STStylizedWindow_Scroll(w, over / mh);
        else if (under > 0) eng_STStylizedWindow_Scroll(w, -under / mh);
        refresh();
        return;
    }
}
static void move_focus_to(UI *n) { if (!n) return; focus_set(n); ensure_visible(n); }
/* the tab order of FocusManager: the objects the scripts registered, completely visible ones only */
extern int tg_focus_tab_list(UI **out, int max);
static UI *tab_target(int backwards)
{
    UI *list[64]; int n = tg_focus_tab_list(list, 64), i, cur = -1; int k;
    for (i = 0; i < n; i++) { UI *p; for (p = kfocus; p; p = p->parent) if (p == list[i]) { cur = i; break; } if (cur >= 0) break; }
    for (k = 1; k <= n; k++) {
        int j = cur < 0 ? (backwards ? n - k : k - 1) : ((cur + (backwards ? -k : k)) % n + n) % n;
        if (list[j]->flags & UF_COMPVISIBLE) return list[j];
    }
    return NULL;
}
/* the first control inside a pane (for Tab into a registered pane) */
static UI *control_inside(UI *pane)
{
    float best = 1e30f; UI *pick = NULL; int i;
    for (i = 0; i < nhits; i++) {
        const TgUiHit *k = &hits[i]; UI *p; float c;
        if (!is_control(k)) continue;
        for (p = (UI *)k->node; p && p != pane; p = p->parent) ;
        if (!p) continue;
        c = k->y0 * 4.0f + k->x0;
        if (c < best) { best = c; pick = (UI *)k->node; }
    }
    return pick;
}
static void press_key(UI *n, int down)
{
    if (down) { if (!textfocus) pressed = n; pressed_kind = 1; return; }
    if (pressed == n) {
        const TgUiHit *k = hit_of(n); pressed = NULL;
        if (!k) return;
        if (k->kind == 1) { if (n->up_ev) ui_send_event(n->up_ev, n); tg_button_press(n); }
        else if (k->kind == 2) tg_menu_toggle(n, !tg_menu_opened(n));
    }
}
/* any node of the focus chain with a keyboard handler of the scripts? (then the arrow keys are theirs: they act on the key release) */
static int chain_scripted(UI *ctx)
{
    UI *n;
    for (n = ctx; n; n = (n->focus && (n->focus->flags & UF_COMPVISIBLE)) ? n->focus : NULL) if (tg_has_instance_handler(n, ET_KEYBOARD_T)) return 1;
    return 0;
}
/* state: 1 key down, 2 key up, 3 key repeat. Returns 1 when the key was used by the UI.
   The scripts see presses and releases. What the port adds happens on the release (like the stock handlers, which act on KS_KEYUP), except for
   a held key (repeat), which moves the focus when no script owns the keys, and typing, which acts on the press. */
int tg_ui_key(int wc, int state, int shift)
{
    UI *ctx = context(); int down = state == 1 || state == 3, act = state == 2, i, repeat = state == 3;
    init_codes();
    if (!ctx && !textfocus) return 0;
    if (!ctx) return 0;
    refresh();
    if (kfocus && !in_hits(kfocus)) kfocus = NULL;
    if (textfocus && !in_hits(textfocus)) textfocus = NULL;
    if (!repeat && send_key(ctx, wc, state == 3 ? 1 : state)) { sync_focus(ctx); refresh(); return 1; }
    sync_focus(ctx);
    /* text entry takes all keys */
    if (textfocus) {
        if (down) {
            if (wc == WC_BACK_) tg_para_delete(textfocus, 1);
            else if (wc == WC_DEL_) tg_para_delete(textfocus, 0);
            else if (wc == WC_LEFT_) tg_para_move(textfocus, -1);
            else if (wc == WC_RIGHT_) tg_para_move(textfocus, 1);
            else if (wc == WC_HOME_) tg_para_move(textfocus, -2);
            else if (wc == WC_END_) tg_para_move(textfocus, 2);
            else if (wc == WC_RETURN_ || wc == WC_ENTER_ || wc == WC_ESC_ || wc == WC_TAB_) textfocus = NULL;
        }
        return 1;
    }
    if (repeat && chain_scripted(ctx)) return 1;
    act = act || repeat;
    if (wc == WC_TAB_) {
        if (!act) return 1;
        { UI *t = tab_target(shift), *c = t ? control_inside(t) : NULL;
          if (!c) { /* no registered list: go through all controls in reading order */
              const TgUiHit *cur = kfocus ? hit_of(kfocus) : NULL; UI *pick = NULL, *firstc = NULL; float cc = cur ? cur->y0 * 4.0f + cur->x0 : -1e30f, best = shift ? -1e30f : 1e30f, fb = shift ? -1e30f : 1e30f;
              for (i = 0; i < nhits; i++) { const TgUiHit *k = &hits[i]; float v; if (!is_control(k)) continue; v = k->y0 * 4.0f + k->x0;
                  if (shift ? v > fb : v < fb) { fb = v; firstc = (UI *)k->node; }
                  if (shift ? (v < cc && v > best) : (v > cc && v < best)) { best = v; pick = (UI *)k->node; } }
              c = pick ? pick : firstc;
          }
          move_focus_to(c); }
        return 1;
    }
    if (wc == WC_ESC_) {                                                   /* an open menu around the focus closes */
        UI *p;
        if (!act) return 1;
        if (ctx->mwt == (int)tg_const("MWT_MODAL_DIALOG") && ctx->ext) { eng_ModalDialogWindow_Cancel(ctx); return 1; }
        for (p = kfocus; p; p = p->parent) if (tg_is_menu(p) && tg_menu_opened(p)) { tg_menu_toggle(p, 0); refresh(); move_focus_to(p); return 1; }
        return 0;
    }
    if (wc == WC_UP_ || wc == WC_DOWN_ || wc == WC_LEFT_ || wc == WC_RIGHT_) {
        const TgUiHit *cur = kfocus ? hit_of(kfocus) : NULL; UI *pick;
        if (!act) return cur != NULL || nhits > 0;
        if (!cur) { move_focus_to(first_control()); return kfocus != NULL; }
        if (tg_is_numeric_bar(kfocus) && (wc == WC_LEFT_ || wc == WC_RIGHT_)) { tg_numeric_step(kfocus, wc == WC_RIGHT_ ? 1 : -1); return 1; }
        if (tg_is_menu(kfocus) && wc == WC_RIGHT_ && !tg_menu_opened(kfocus)) { tg_menu_toggle(kfocus, 1); refresh(); return 1; }
        if (tg_is_menu(kfocus) && wc == WC_LEFT_ && tg_menu_opened(kfocus)) { tg_menu_toggle(kfocus, 0); refresh(); return 1; }
        pick = nearest(cur, wc == WC_LEFT_ ? 0 : wc == WC_RIGHT_ ? 1 : wc == WC_UP_ ? 2 : 3);
        if (pick) move_focus_to(pick); else if (wc == WC_UP_ || wc == WC_DOWN_) {          /* past the end of a column: wrap to the other end */
            float best = wc == WC_DOWN_ ? 1e30f : -1e30f; UI *w = NULL; float cx = (cur->x0 + cur->x1) * 0.5f;
            for (i = 0; i < nhits; i++) { const TgUiHit *k = &hits[i]; float cx2 = (k->x0 + k->x1) * 0.5f; if (!is_control(k) || fabsf(cx2 - cx) > (cur->x1 - cur->x0) * 0.6f + 8) continue;
                if (wc == WC_DOWN_ ? k->y0 < best : k->y0 > best) { best = k->y0; w = (UI *)k->node; } }
            if (w && w != kfocus) move_focus_to(w);
        }
        return 1;
    }
    if (wc == WC_RETURN_ || wc == WC_ENTER_ || wc == WC_SPACE_) {
        const TgUiHit *cur = kfocus ? hit_of(kfocus) : NULL;
        if (!cur) return 0;
        if (cur->kind == 4) { if (state == 1) { textfocus = kfocus; tg_para_move(kfocus, 2); } return 1; }
        if (cur->kind == 3) return 1;
        if (repeat) return 1;
        press_key(kfocus, state == 1);
        return 1;
    }
    if (wc == WC_PGUP_ || wc == WC_PGDN_ || wc == WC_HOME_ || wc == WC_END_) {
        UI *w = kfocus ? scroll_window_of(kfocus) : NULL; int j;
        if (!w) for (j = nhits - 1; j >= 0; j--) if (hits[j].kind == 5 && mouse_x >= hits[j].x0 && mouse_x < hits[j].x1 && mouse_y >= hits[j].y0 && mouse_y < hits[j].y1) { w = (UI *)hits[j].node; break; }
        if (!w) for (j = nhits - 1; j >= 0; j--) if (hits[j].kind == 5) { w = (UI *)hits[j].node; break; }
        if (!w) return 0;
        if (act) {
            float mh = (float)eng_GraphicsModeInfo_GetHeight(NULL), page = 0.3f;
            for (j = 0; j < nhits; j++) if (hits[j].kind == 5 && hits[j].node == w) page = (hits[j].y1 - hits[j].y0) / mh * 0.9f;
            if (wc == WC_PGUP_) eng_STStylizedWindow_Scroll(w, -page); else if (wc == WC_PGDN_) eng_STStylizedWindow_Scroll(w, page);
            else eng_STStylizedWindow_Scroll(w, wc == WC_HOME_ ? -10.0f : 10.0f);
            refresh();
        }
        return 1;
    }
    return 0;
}
/* typed characters (SDL text input) */
int tg_ui_text(const char *utf8)
{
    if (!textfocus || !context()) return 0;
    tg_para_insert(textfocus, utf8);
    { extern void tg_ui_sound_once(const char *); tg_ui_sound_once("UITextEntryClick"); }   /* InterfaceModule::TGParagraphSoundHandler 00556490: a typing click unless one plays */
    return 1;
}
/* the script's cursor calls (TGInputManager): positions are fractions of the screen except GetMouseX and GetMouseY (mode pixels);
   MoveMouseCursorTo(x, y, seconds) slides the cursor there (MissionLib.MoveMouseCursorToUIObject, the tutorial) */
static struct { int on, paused; float x0, y0, x1, y1, t, dur; } slide;
long eng_TGInputManager_GetMouseX(void *m) { return (long)mouse_x; }
long eng_TGInputManager_GetMouseY(void *m) { return (long)mouse_y; }
void *eng_TGInputManager_GetMousePos(void *m)
{
    static float p[2]; p[0] = mouse_x / (float)eng_GraphicsModeInfo_GetWidth(NULL); p[1] = mouse_y / (float)eng_GraphicsModeInfo_GetHeight(NULL); return p;
}
void eng_TGInputManager_SetMousePosition(void *m, void *pt)
{
    const float *p = pt; if (!p) return;
    slide.on = 0; tg_ui_mouse_move(p[0] * (float)eng_GraphicsModeInfo_GetWidth(NULL), p[1] * (float)eng_GraphicsModeInfo_GetHeight(NULL));
}
void eng_TGInputManager_MoveMouseCursorTo(void *m, float x, float y, float secs, int flags)
{
    float w = (float)eng_GraphicsModeInfo_GetWidth(NULL), h = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    slide.x0 = mouse_x; slide.y0 = mouse_y; slide.x1 = x * w; slide.y1 = y * h; slide.t = 0; slide.dur = secs; slide.paused = 0; slide.on = 1;
    if (secs <= 0) { slide.on = 0; tg_ui_mouse_move(slide.x1, slide.y1); }
}
void eng_TGInputManager_PauseMouseCursorMoveTo(void *m) { slide.paused = 1; }
void eng_TGInputManager_ResumeMouseCursorMoveTo(void *m) { slide.paused = 0; }
void tg_ui_input_tick(double dt)
{
    tg_stw_scroll_tick(dt);
    if (slide.on && !slide.paused) {
        float f; slide.t += (float)dt; f = slide.dur > 0 ? slide.t / slide.dur : 1; if (f >= 1) { f = 1; slide.on = 0; }
        tg_ui_mouse_move(slide.x0 + (slide.x1 - slide.x0) * f, slide.y0 + (slide.y1 - slide.y0) * f);
    }
}

/* ---- Python access for tests and hosts ---- */
static PyObject *py_move(PyObject *self, PyObject *a) { float x, y; if (!PyArg_ParseTuple(a, "ff:ui_mouse_move", &x, &y)) return NULL; tg_ui_mouse_move(x, y); Py_INCREF(Py_None); return Py_None; }
static PyObject *py_button(PyObject *self, PyObject *a) { int b, d; float x, y; if (!PyArg_ParseTuple(a, "iiff:ui_mouse_button", &b, &d, &x, &y)) return NULL; return PyInt_FromLong(tg_ui_mouse_button(b, d, x, y)); }
static PyObject *py_wheel(PyObject *self, PyObject *a) { float d; if (!PyArg_ParseTuple(a, "f:ui_wheel", &d)) return NULL; return PyInt_FromLong(tg_ui_wheel(d)); }
static PyObject *py_key(PyObject *self, PyObject *a) { int wc, st, sh = 0; if (!PyArg_ParseTuple(a, "ii|i:ui_key", &wc, &st, &sh)) return NULL; return PyInt_FromLong(tg_ui_key(wc, st, sh)); }
static PyObject *py_text(PyObject *self, PyObject *a) { char *s; if (!PyArg_ParseTuple(a, "s:ui_text", &s)) return NULL; return PyInt_FromLong(tg_ui_text(s)); }
/* ui_controls() -> list of (kind, x0, y0, x1, y1, text, flags, visible): the controls that can be pointed at, for tests (visible: the middle is not cut off) */
static PyObject *py_controls(PyObject *self, PyObject *a)
{
    PyObject *l = PyList_New(0); int i;
    if (!PyArg_ParseTuple(a, ":ui_controls")) return NULL;
    refresh();
    for (i = 0; i < nhits; i++) {
        const UI *u = hits[i].node; const char *t = u->text ? eng_TGString_GetCString(u->text) : "";
        PyObject *tup = Py_BuildValue("(iffffsii)", hits[i].kind, hits[i].x0, hits[i].y0, hits[i].x1, hits[i].y1, t, (int)u->flags, in_clip(&hits[i], (hits[i].x0 + hits[i].x1) * 0.5f, (hits[i].y0 + hits[i].y1) * 0.5f));
        PyList_Append(l, tup); Py_DECREF(tup);
    }
    return l;
}
/* ui_focus() -> the text of the focused control or None */
static PyObject *py_focus(PyObject *self, PyObject *a)
{
    const UI *u;
    if (!PyArg_ParseTuple(a, ":ui_focus")) return NULL;
    refresh(); u = tg_ui_focus_node();
    if (!u) { Py_INCREF(Py_None); return Py_None; }
    return PyString_FromString(u->text ? eng_TGString_GetCString(u->text) : "");
}
/* ui_dump(maxdepth) -> prints the UI tree (class, flags, position as fractions of the screen, text), for debugging the tactical windows */
static void dump_node(const UI *u, int depth, int maxd, float ox, float oy)
{
    int i; const char *t = u->text ? eng_TGString_GetCString(u->text) : "";
    extern const char *eng_TGFontGroup_GetFontName(void *); extern double eng_TGFontGroup_GetFontSize(void *);
    if (u->font && t[0] && !strcmp(u->o.cls->name, "TGParagraph")) printf("%*s[font %s %g]\n", depth * 2 + 2, "", eng_TGFontGroup_GetFontName(u->font) ? eng_TGFontGroup_GetFontName(u->font) : "?", eng_TGFontGroup_GetFontSize(u->font));
    printf("%*s%s%s x=%.3f y=%.3f w=%.3f h=%.3f%s%s%s%s%s\n", depth * 2, "", u->o.cls ? u->o.cls->name : "?", (u->flags & UF_VISIBLE) ? "" : " [hidden]", ox + u->x, oy + u->y, u->w, u->h,
           t[0] ? " '" : "", t, t[0] ? "'" : "", u->icon_group ? " icon=" : "", u->icon_group ? u->icon_group : "");
    if (depth >= maxd) return;
    for (i = 0; i < u->nkids; i++) dump_node(u->kids[i], depth + 1, maxd, ox + u->x, oy + u->y);
}
static PyObject *py_dump(PyObject *self, PyObject *a)
{
    int d = 6; extern UI *tg_ui_root(void);
    if (!PyArg_ParseTuple(a, "|i:ui_dump", &d)) return NULL;
    dump_node(tg_ui_root(), 0, d, 0, 0); fflush(stdout); Py_INCREF(Py_None); return Py_None;
}
void tg_ui_input_register_host(PyObject *dict)
{
    static PyMethodDef m[] = {{"ui_mouse_move", py_move, 1}, {"ui_mouse_button", py_button, 1}, {"ui_wheel", py_wheel, 1}, {"ui_key", py_key, 1},
                              {"ui_text", py_text, 1}, {"ui_controls", py_controls, 1}, {"ui_focus", py_focus, 1}, {"ui_dump", py_dump, 1}, {NULL, NULL}};
    PyMethodDef *d; for (d = m; d->ml_name; d++) { PyObject *f = PyCFunction_New(d, NULL); PyDict_SetItemString(dict, (char *)d->ml_name, f); Py_DECREF(f); }
}

/* FocusManager::Tab from a script: the focus goes to the next registered object */
int tg_ui_tab(int backwards)
{
    UI *ctx = context(); int wc;
    if (!ctx) return 0;
    init_codes();
    refresh();
    wc = WC_TAB_;
    return tg_ui_key(wc, 1, backwards) | tg_ui_key(wc, 2, backwards);
}
