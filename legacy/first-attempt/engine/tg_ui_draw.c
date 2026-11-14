/* UI draw list: walks the visible UI tree from the root pane and emits textured quads, back to front. Node kinds:
   TGIcon (its icon, stretched to the node), TGFrame (a nine-slice frame: icons n..n+8 are the corners, edges and centre of the group),
   TGParagraph and TGTextButton (their text, from the default font's glyphs). Other nodes draw nothing; their children still draw.
   Glyphs and atlas names come from the icon and font groups (tg_glyph, tg_icon_glyph in tg_services.c).
   Approximations (marked): paragraphs are left and top aligned, text buttons are centred; word wrap breaks at any character (not at
   word ends); no clipping to the parent pane; a colour of all zeros draws white; no alpha sorting (the platform blends in tree order). */
#include <math.h>
#include <string.h>
#include "tg_ui.h"
#include "tg_ui_draw.h"

extern int tg_glyph(void *p, int num, const char **tex, float *l, float *t, float *w, float *h);
extern int tg_glyph_flags(void *p, int num, int *rot, int *mirror);
extern int tg_icon_glyph(const char *group, int num, const char **tex, float *l, float *t, float *w, float *h);
extern void *tg_default_font(void);
extern void tg_font_metrics(void *p, float *space, float *hspace, float *vspace, float *height);
extern const char *eng_TGString_GetCString(void *);
extern long eng_GraphicsModeInfo_GetWidth(void *);
extern long eng_GraphicsModeInfo_GetHeight(void *);
extern UI *tg_ui_root(void);
extern int tg_icon_flags(const char *group, int num, int *rot, int *mirror);
extern int tg_button_chosen(const void *node);
extern int tg_menu_opened(const void *node);
extern int tg_para_editable(const void *p);
extern int tg_para_caret(const void *p);
extern const void *tg_ui_focus_node(void);
extern void tg_ui_mouse_state(float *x, float *y, int *show);
extern int tg_ui_text_focus(const void *p);
extern int tg_is_menu(const void *node);
extern int tg_ui_pressed(const void *p);
extern long eng_GraphicsModeInfo_GetCurrentResolution(void *);
static int tg_res_index(void) { return (int)eng_GraphicsModeInfo_GetCurrentResolution(NULL); }
extern int tg_menu_untitled(const void *node);
extern float tg_menu_title_h(const void *node);
extern int tg_numeric_bar(const void *node, float *frac, const float **fill, const float **empty);
extern int tg_is_stylized_window(const void *node);
extern float tg_menu_height(const void *node);
extern int tg_rounded_colour(const void *node, int i, float *out);

typedef struct { TgUiQuad *out; int max, n; float mw, mh; int rot, mir, grow; float angle; float clip[4]; TgUiHit *hits; int nhits, maxhits; } Draw;

static void quad(Draw *d, float x, float y, float w, float h, const char *tex, float sx, float sy, float sw, float sh, const float *col)
{
    TgUiQuad *q;
    if (d->clip[2] > d->clip[0] && (x >= d->clip[2] || x + w <= d->clip[0] || y >= d->clip[3] || y + h <= d->clip[1])) { d->rot = d->mir = d->grow = 0; d->angle = 0; return; }   /* outside the clip rectangle */
    if (d->n >= d->max || w <= 0 || h <= 0) { d->rot = d->mir = d->grow = 0; d->angle = 0; return; }
    q = &d->out[d->n++];
    q->x = x; q->y = y; q->w = w; q->h = h; q->tex = tex; q->sx = sx; q->sy = sy; q->sw = sw; q->sh = sh;
    memcpy(q->col, col, sizeof q->col);
    q->rot = d->rot; q->mirror = d->mir; q->grow = d->grow; q->angle = d->angle; memcpy(q->clip, d->clip, sizeof q->clip); d->rot = d->mir = d->grow = 0; d->angle = 0;
}

/* the node's colour; all zeros (never set) is white */
static void colour_of(const UI *u, float *out)
{
    if (u->color[0] == 0 && u->color[1] == 0 && u->color[2] == 0 && u->color[3] == 0) { out[0] = out[1] = out[2] = out[3] = 1.0f; return; }
    memcpy(out, u->color, sizeof(float) * 4);
}

/* an icon stretched over the node rectangle (w and h in mode pixels); an unsized node takes the artwork size */
static void draw_icon(Draw *d, const UI *u, float x, float y, float w, float h)
{
    const char *tex; float l, t, iw, ih, col[4];
    if (!tg_icon_glyph(u->icon_group, u->icon_num, &tex, &l, &t, &iw, &ih)) return;
    if (w <= 0) w = iw;
    if (h <= 0) h = ih;
    colour_of(u, col);
    /* a big solid black block (the keyboard tab's "black region", 0.29 x 0.2 of the screen) only blanks the black inside of the menu; it
       would also cut into the frame beside it, so it is not drawn (approximation: the engine's draw order is not checked for this case) */
    if (u->icon_num == 200 && col[0] == 0 && col[1] == 0 && col[2] == 0 && w * h > 0.02f * d->mw * d->mh) {
        const UI *p; int in_window = 0;
        for (p = u->parent; p; p = p->parent) if (tg_is_stylized_window(p)) { in_window = 1; break; }     /* the solid glass of a window is wanted */
        if (!in_window) return;
    }
    tg_icon_flags(u->icon_group, u->icon_num, &d->rot, &d->mir);
    d->grow = 1;
    { extern float tg_blip_angle(const void *); d->angle = tg_blip_angle(u); }
    quad(d, x, y, w, h, tex, l, t, iw, ih, col);
}

/* nine slices of a group from icon num: 0 top left, 1 top, 2 top right, 3 left, 4 centre, 5 right, 6 bottom left, 7 bottom, 8 bottom
   right. The corners keep their artwork size, the edges and the centre stretch to fill the rectangle. */
static void draw_nine(Draw *d, const char *group, int num, float x, float y, float w, float h, const float *col, int skip_centre)
{
    const char *tex[9]; float sl[9][4], left, right, top, bottom, mw, mh; int k, fl[9][2];
    if (!group) return;
    for (k = 0; k < 9; k++) { if (!tg_icon_glyph(group, num + k, &tex[k], &sl[k][0], &sl[k][1], &sl[k][2], &sl[k][3])) return; tg_icon_flags(group, num + k, &fl[k][0], &fl[k][1]); }
    left = sl[0][2]; right = sl[2][2]; top = sl[0][3]; bottom = sl[6][3];
    mw = w - left - right; mh = h - top - bottom;
    if (mw < 0) mw = 0;
    if (mh < 0) mh = 0;
    d->grow = 1; d->rot = fl[0][0]; d->mir = fl[0][1];
    quad(d, x, y, left, top, tex[0], sl[0][0], sl[0][1], sl[0][2], sl[0][3], col);
    d->grow = 1; d->rot = fl[1][0]; d->mir = fl[1][1];
    quad(d, x + left, y, mw, top, tex[1], sl[1][0], sl[1][1], sl[1][2], sl[1][3], col);
    d->grow = 1; d->rot = fl[2][0]; d->mir = fl[2][1];
    quad(d, x + left + mw, y, right, top, tex[2], sl[2][0], sl[2][1], sl[2][2], sl[2][3], col);
    d->grow = 1; d->rot = fl[3][0]; d->mir = fl[3][1];
    quad(d, x, y + top, left, mh, tex[3], sl[3][0], sl[3][1], sl[3][2], sl[3][3], col);
    if (!skip_centre) { d->grow = 1; d->rot = fl[4][0]; d->mir = fl[4][1]; quad(d, x + left, y + top, mw, mh, tex[4], sl[4][0], sl[4][1], sl[4][2], sl[4][3], col); }
    d->grow = 1; d->rot = fl[5][0]; d->mir = fl[5][1];
    quad(d, x + left + mw, y + top, right, mh, tex[5], sl[5][0], sl[5][1], sl[5][2], sl[5][3], col);
    d->grow = 1; d->rot = fl[6][0]; d->mir = fl[6][1];
    quad(d, x, y + top + mh, left, bottom, tex[6], sl[6][0], sl[6][1], sl[6][2], sl[6][3], col);
    d->grow = 1; d->rot = fl[7][0]; d->mir = fl[7][1];
    quad(d, x + left, y + top + mh, mw, bottom, tex[7], sl[7][0], sl[7][1], sl[7][2], sl[7][3], col);
    d->grow = 1; d->rot = fl[8][0]; d->mir = fl[8][1];
    quad(d, x + left + mw, y + top + mh, right, bottom, tex[8], sl[8][0], sl[8][1], sl[8][2], sl[8][3], col);
}
static void draw_frame(Draw *d, const UI *u, float x, float y, float w, float h)
{
    float col[4];
    if (!u->icon_group) return;
    colour_of(u, col);
    draw_nine(d, u->icon_group, u->icon_num, x, y, w, h, col, 0);
}
static void draw_text_in(Draw *d, const UI *u, float x, float y, float w, float h, int centred, const float *col_in, float scale);
/* a rounded button (STRoundedButton): the body in the colour the script set for its state (normal, highlighted, selected), the
   FrameButton outline from Button.tga without its centre tile (the centre is black in the file), and the label in the text colour.
   With no colours set, the stock values are used (LoadInterface.py: orange, pale orange, brown; black text). */
static int break_lines(const char *s, void *fg, float wrap, float space, float hspace, int *start, int *end, float *width);
static float button_text_scale(const UI *u, float w, float h);
/* the label scale that keeps it inside the pill: at most 1, and smaller when the text is wider or taller than the inside */
static float button_text_scale(const UI *u, float w, float h)
{
    void *fg = u->font ? u->font : tg_default_font();
    const char *s = u->text ? eng_TGString_GetCString(u->text) : NULL;
    float space, hs, vs, height, sx = 1.0f, sy = 1.0f; int start[64], end[64], n; float width[64];
    if (!s || !*s || !fg) return 1.0f;
    tg_font_metrics(fg, &space, &hs, &vs, &height);
    if (height <= 0) return 1.0f;
    n = break_lines(s, fg, 0, space, hs, start, end, width);
    float padx = h < 30 ? 6 : 16, pady = h < 30 ? 1 : 10;                  /* small buttons (the tactical weapons control) keep their padding small */
    if (n > 0 && width[0] > w - padx) sx = (w - padx) / width[0];
    if (height > h - pady) sy = (h - pady) / height;
    return fminf(1.0f, fminf(sx, sy)) < 0.3f ? 0.3f : fminf(1.0f, fminf(sx, sy));
}
/* a button with no size from its script (STButton is sized by the menu layout, which is not all there yet): the label plus padding */
static const char *class_of(const UI *u);
int tg_text_extent(void *fg, const char *s, float wrap_px, float *w, float *h);
void tg_ui_size_of(const void *node, float *w, float *h);
static void button_size_fallback(const UI *u, float *w, float *h)
{
    const char *s = u->text ? eng_TGString_GetCString(u->text) : NULL;
    void *fg = u->font ? u->font : tg_default_font();
    float space, hs, vs, height, lw = 0; int start[64], end[64], n; float width[64];     /* 64: the line cap of break_lines */
    if (*w > 0 && *h > 0) return;
    if (!strcmp(class_of(u), "STButton") || !strcmp(class_of(u), "STToggle")) {         /* a plain STButton fills its parent pane's width, and is as high as the stock start button (0.0416667) */
        float mw = (float)eng_GraphicsModeInfo_GetWidth(NULL), mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
        if (*h <= 0) *h = 0.0416667f * mh;
        if (*w <= 0 && !(u->flags & 0x40000000u) && mw > 0) {                              /* STBSF_SIZE_TO_TEXT: no */
            const UI *p = u->parent;
            while (p && p->w <= 0) p = p->parent;                                          /* a menu's sub pane has no width of its own */
            if (p) *w = p->w * mw;
        }
        if (*w > 0 && *h > 0) return;
        if (*w <= 0 && (u->flags & 0x40000000u)) {                                  /* sized to its text: the label plus the end caps */
            float tw, th;
            if (s && tg_text_extent((void *)u->font, s, 0, &tw, &th)) { *w = tw + *h * 0.9f; return; }
        }
    }
    if (!s || !*s || !fg) return;
    tg_font_metrics(fg, &space, &hs, &vs, &height);
    n = break_lines(s, fg, 0, space, hs, start, end, width);
    if (n > 0) lw = width[0];
    if (*w <= 0) *w = lw + 16;
    if (*h <= 0) *h = height + 8;
}
/* STButton::Layout of a button with STBSF_SIZE_TO_TEXT and no size: as high as the stock button, as wide as the label plus the end caps
   (the script reads GetWidth after Layout: the tactical order buttons) */
void tg_button_autosize(UI *u)
{
    float mw = (float)eng_GraphicsModeInfo_GetWidth(NULL), mh = (float)eng_GraphicsModeInfo_GetHeight(NULL), tw, th;
    const char *s = u->text ? eng_TGString_GetCString(u->text) : NULL;
    if (u->w > 0 || mw <= 0 || mh <= 0 || !s) return;
    if (!tg_text_extent((void *)u->font, s, 0, &tw, &th)) return;
    u->h = 0.0416667f; u->w = (tw + 0.0416667f * mh * 0.9f) / mw;
}
/* one icon of the current LCARS group stretched over a rectangle (artwork: grown to meet its neighbours, with its rotation and mirror) */
static void lcars_piece(Draw *d, int num, float x, float y, float w, float h, const float *col)
{
    extern void *eng_GraphicsModeInfo_GetCurrentMode(void); extern const char *eng_GraphicsModeInfo_GetLcarsString(void *);
    const char *grp = eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode()), *tex; float l, t, iw, ih;
    if (!tg_icon_glyph(grp, num, &tex, &l, &t, &iw, &ih)) return;
    tg_icon_flags(grp, num, &d->rot, &d->mir);
    d->grow = 1;
    quad(d, x, y, w, h, tex, l, t, iw, ih, col);
}
/* an STRoundedButton is made by UIHelpers.CreateRoundedButton (STRoundedButton::Resize 0052e320 calls it): a block between two
   columns; each column has an upper curve and a lower curve, 0.4 of the height each, with a 0.2 filler between (curves 141, 143,
   145, 147 of the LCARS group, block and filler icon 200), all in the state colour */
static void draw_pill_rounded(Draw *d, float x, float y, float w, float h, const float *col)
{
    float ch = h * 0.4f, cw = ch, rw = w - 2 * cw;
    if (rw < 0) { cw = w * 0.5f; rw = 0; }
    lcars_piece(d, 200, x + cw, y, rw, h, col);
    lcars_piece(d, 141, x, y, cw, ch, col);
    lcars_piece(d, 200, x, y + ch, cw, h - 2 * ch, col);
    lcars_piece(d, 145, x, y + h - ch, cw, ch, col);
    lcars_piece(d, 143, x + cw + rw, y, cw, ch, col);
    lcars_piece(d, 200, x + cw + rw, y + ch, cw, h - 2 * ch, col);
    lcars_piece(d, 147, x + cw + rw, y + h - ch, cw, ch, col);
}
/* an STButton (STButton::Initialize 00518fd0): a left end cap (icon 3002, 26 x 28), a solid body (3001) and the mirrored right end cap
   (3003), tinted with the state colour; the caps are scaled to the button height (approximation: the engine's cap size is not decoded) */
static void draw_pill_caps(Draw *d, float x, float y, float w, float h, const float *col)
{
    float cw = 26.0f * h / 28.0f;
    if (cw > w * 0.5f) cw = w * 0.5f;
    lcars_piece(d, 3001, x + cw, y, w - 2 * cw, h, col);
    lcars_piece(d, 3002, x, y, cw, h, col);
    lcars_piece(d, 3003, x + w - cw, y, cw, h, col);
}
/* a button: the pill in the colour the script set for its state (LoadInterface.py values when unset: orange, pale orange, brown) and
   the centred label in the text colour (black unless set) */
static void draw_button(Draw *d, const UI *u, float x, float y, float w, float h)
{
    static const float normal[4] = {244 / 255.0f, 177 / 255.0f, 0, 1}, high[4] = {251 / 255.0f, 224 / 255.0f, 153 / 255.0f, 1},
                       sel[4] = {151 / 255.0f, 57 / 255.0f, 28 / 255.0f, 1}, grey[4] = {0.25f, 0.25f, 0.25f, 1}, black[4] = {0, 0, 0, 1}, white[4] = {1, 1, 1, 1};
    float body[4], text[4];
    /* STButton::SetColorBasedOnFlags 00519f30 and SetColor 00519e00: disabled first, then selected, then highlighted, else normal; the text has its own
       colour for each state (default: black, white when highlighted or selected, black when disabled) */
    int off = !(u->flags & UF_ENABLED), state = off ? 3 : ((u->flags & UF_SELECTED) || tg_button_chosen(u) || tg_ui_pressed(u)) ? 2 : (u->flags & UF_HIGHLIGHTED) ? 1 : 0;
    const float *fill = state == 3 ? grey : state == 2 ? sel : state == 1 ? high : normal, *tc = state == 1 || state == 2 ? white : black;
    static const int body_i[4] = {0, 1, 2, 4}, text_i[4] = {3, 5, 6, 7};
    if (w <= 0 || h <= 0) button_size_fallback(u, &w, &h);
    if (w <= 0 || h <= 0) return;
    if (tg_rounded_colour(u, body_i[state], body)) fill = body;
    if (tg_rounded_colour(u, text_i[state], text)) tc = text;
    { extern int tg_button_fill(const void *, float *); float ff;
      if (tg_button_fill(u, &ff) && state != 3) {                                 /* a gauge body (the repair buttons): dark behind, the button colour for the condition */
          draw_pill_caps(d, x, y, w, h, grey);
          if (ff > 0) draw_pill_caps(d, x, y, fmaxf(w * ff, h), h, fill);
      } else if (!strcmp(class_of(u), "STRoundedButton")) draw_pill_rounded(d, x, y, w, h, fill); else draw_pill_caps(d, x, y, w, h, fill); }
    if (u->text) draw_text_in(d, u, x, y, w, h, 1, tc, button_text_scale(u, w, h));
}
#define MAX_LINES 64
/* the glyph advance of one character: its width plus the horizontal spacing; a space uses the font's space width */
static float advance_of(void *fg, unsigned char c, float space, float hspace)
{
    const char *tex; float l, t, w, h; int rot, mir;
    if (c == ' ' || !tg_glyph(fg, c, &tex, &l, &t, &w, &h)) return space;
    tg_glyph_flags(fg, c, &rot, &mir);
    return ((rot & 1) ? h : w) + hspace;                         /* a glyph turned a quarter is as wide as its artwork is high */
}

/* breaks the text into lines (at a newline, or before a character that would pass the wrap width); returns the number of lines */
static int break_lines(const char *s, void *fg, float wrap, float space, float hspace, int *start, int *end, float *width)
{
    int i = 0, n = 0, begin = 0; float pen = 0;
    while (s[i] && n < MAX_LINES) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') { start[n] = begin; end[n] = i; width[n] = pen; n++; i++; begin = i; pen = 0; continue; }
        if (wrap > 0 && pen > 0 && pen + advance_of(fg, c, space, hspace) > wrap) {
            start[n] = begin; end[n] = i; width[n] = pen; n++; begin = i; pen = 0; continue;
        }
        pen += advance_of(fg, c, space, hspace);
        i++;
    }
    if (n < MAX_LINES && (begin < i || n == 0)) { start[n] = begin; end[n] = i; width[n] = pen; n++; }
    return n;
}

/* text of a paragraph (top left) or a text button (centred in the node). Glyphs are placed at their own size. */
static void draw_text_in(Draw *d, const UI *u, float x, float y, float w, float h, int centred, const float *col_in, float scale)
{
    const char *s = u->text ? eng_TGString_GetCString(u->text) : NULL;
    void *fg = u->font ? u->font : tg_default_font();
    float space, hs, vs, height, wrap = u->wrap > 0 ? u->wrap * d->mw : 0, col[4], total, top, left;
    int start[MAX_LINES], end[MAX_LINES], lines, i, k;
    float width[MAX_LINES];
    if (!s || !*s || !fg) return;
    tg_font_metrics(fg, &space, &hs, &vs, &height);
    if (height <= 0) return;
    lines = break_lines(s, fg, wrap, space, hs, start, end, width);
    if (col_in) memcpy(col, col_in, sizeof col); else colour_of(u, col);
    space *= scale; hs *= scale; vs *= scale; height *= scale;
    total = lines * (height + vs) - vs;
    top = centred ? y + (h - total) * 0.5f : y;
    for (i = 0; i < lines; i++) {
        float pen = 0;
        left = centred ? x + (w - width[i] * scale) * 0.5f : x;
        for (k = start[i]; k < end[i]; k++) {
            unsigned char c = (unsigned char)s[k];
            const char *tex; float l, t, gw, gh, dw, dh; int rot, mir;
            if (c == ' ' || !tg_glyph(fg, c, &tex, &l, &t, &gw, &gh)) { pen += space; continue; }
            tg_glyph_flags(fg, c, &rot, &mir);
            dw = (rot & 1) ? gh : gw; dh = (rot & 1) ? gw : gh;
            d->rot = rot; d->mir = mir;
            quad(d, left + pen, top + i * (height + vs), dw * scale, dh * scale, tex, l, t, gw, gh, col);
            pen += dw * scale + hs;
        }
    }
}

/* the size in mode pixels of a text in a font (NULL: the default), wrapped at wrap_px when it is above 0; 0 when there is no font */
int tg_text_extent(void *fg, const char *s, float wrap_px, float *w, float *h)
{
    float space, hs, vs, height, width[MAX_LINES], widest = 0; int start[MAX_LINES], end[MAX_LINES], n, i;
    if (!fg) fg = tg_default_font();
    if (!fg || !s) return 0;
    tg_font_metrics(fg, &space, &hs, &vs, &height);
    if (height <= 0) return 0;
    n = break_lines(s, fg, wrap_px, space, hs, start, end, width);
    for (i = 0; i < n; i++) if (width[i] > widest) widest = width[i];
    *w = widest; *h = n * (height + vs) - vs;
    return 1;
}
/* the node's text with its top left at x, y in the colour col */
static void draw_text_at(Draw *d, const UI *u, float x, float y, const float *col)
{
    draw_text_in(d, u, x, y, 0, 0, 0, col, 1.0f);
}
static void draw_text(Draw *d, const UI *u, float x, float y, float w, float h, int centred) { draw_text_in(d, u, x, y, w, h, centred, NULL, 1.0f); }
static const char *class_of(const UI *u) { return u->o.cls ? u->o.cls->name : ""; }
/* the size a script reads for a node, normalized like its position: its own size, and a button with none gets the label size plus
   the padding of button_size_fallback (the layout reads a button's height to stack the rows, so it cannot stay at zero) */
void tg_ui_size_of(const void *node, float *w, float *h)
{
    const UI *u = node;
    float mw = (float)eng_GraphicsModeInfo_GetWidth(NULL), mh = (float)eng_GraphicsModeInfo_GetHeight(NULL), pw, ph;
    if (!u) { *w = *h = 0; return; }
    *w = u->w; *h = u->h;
    if (tg_is_menu(u)) {
        *h = tg_menu_height(u);
        if (*w <= 0 && u->parent) *w = u->parent->w;
        return;
    }
    if (u->w > 0 && u->h > 0) return;
    if (strcmp(class_of(u), "STButton") && strcmp(class_of(u), "STToggle") && strcmp(class_of(u), "STRoundedButton")) return;
    if (mw <= 0 || mh <= 0) return;
    pw = u->w * mw; ph = u->h * mh;
    button_size_fallback(u, &pw, &ph);                       /* fills an unset side, in mode pixels */
    *w = pw / mw; *h = ph / mh;
}

/* an STMenu: its title bar as a pill with the name at the left (colours approximate: STMenu1 values of LoadInterface.py) */
static void draw_menu(Draw *d, const UI *u, float x, float y)
{
    static const float normal[4] = {216 / 255.0f, 94 / 255.0f, 86 / 255.0f, 1}, sel[4] = {127 / 255.0f, 60 / 255.0f, 43 / 255.0f, 1},
                       high[4] = {254 / 255.0f, 120 / 255.0f, 86 / 255.0f, 1}, black[4] = {0, 0, 0, 1};
    float w, h, tw, th;
    tg_ui_size_of(u, &w, &h);
    { extern float tg_menu_title_h(const void *); w *= d->mw; h = tg_menu_title_h(u) * d->mh; }
    if (w <= 0) return;
    float mb[4], mt[4]; extern int tg_menu_colours(const void *, float *, float *); int ok = tg_menu_colours(u, mb, mt);
    draw_pill_caps(d, x, y, w, h, ok ? mb : tg_menu_opened(u) ? sel : (u->flags & UF_HIGHLIGHTED) ? high : normal);
    if (ok && (u->flags & 0x40000000u)) {                                    /* the arrow of an openable menu (icons 0xbcc closed, 0xbcd open), tinted g_kSTMenuArrowColor or the target's group */
        extern const float *tg_subsystem_menu_colour(const void *); extern void *tg_global(const char *);
        const char *grp = eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode()), *tex; float l, t, iw, ih; const float *ac = tg_subsystem_menu_colour(u);
        if (!ac) ac = tg_global("g_kSTMenuArrowColor");
        if (ac && tg_icon_glyph(grp, tg_menu_opened(u) ? 0xbcd : 0xbcc, &tex, &l, &t, &iw, &ih)) {
            tg_icon_flags(grp, tg_menu_opened(u) ? 0xbcd : 0xbcc, &d->rot, &d->mir); d->grow = 0;
            quad(d, x + h * (10.0f / 23.0f) - iw * 0.5f, y + (h - ih) * 0.5f, iw, ih, tex, l, t, iw, ih, ac);
        }
    }
    if (u->text && tg_text_extent((void *)u->font, eng_TGString_GetCString(u->text), 0, &tw, &th)) {
        draw_text_at(d, u, x + 0.5f * h + 4.0f, y + (h - th) * 0.5f, ok ? mt : black);
    }
}
/* the caret of the paragraph that has the keyboard (blinks twice a second) */
static void draw_caret(Draw *d, const UI *u, float x, float y)
{
    extern double tg_real_time;
    static const float col[4] = {1, 1, 1, 1};
    char buf[256]; const char *t = u->text ? eng_TGString_GetCString(u->text) : ""; int c = tg_para_caret(u); float tw = 0, th = 0, lh = 0;
    if (((int)(tg_real_time * 2.0)) & 1) return;
    if (c < 0 || c > 255) c = 255;
    strncpy(buf, t, (size_t)c); buf[c] = 0;
    if (c > 0) tg_text_extent((void *)u->font, buf, 0, &tw, &th);
    tg_text_extent((void *)u->font, "M", 0, &lh, &th);
    quad(d, x + tw + 1, y, 1.5f, th > 0 ? th : 14, NULL, 0, 0, 1, 1, col);
}
static void draw_node(Draw *d, const UI *u, float x, float y, float w, float h)
{
    const char *c = class_of(u);
    if (!strcmp(c, "TGFrame")) draw_frame(d, u, x, y, w, h);
    else if (!strcmp(c, "TGIcon") || tg_is_type((TGObject *)&u->o, 0x202)) draw_icon(d, u, x, y, w, h);      /* also the icon subclasses: RadarBlip, DamageIcon */
    else if (!strcmp(c, "TGParagraph")) { draw_text(d, u, x, y, w, h, 0); if (tg_ui_text_focus(u)) draw_caret(d, u, x, y); }
    else if (!strcmp(c, "TGTextButton")) draw_text(d, u, x, y, w, h, 1);
    else if (!strcmp(c, "STRoundedButton") || !strcmp(c, "STButton") || !strcmp(c, "STToggle")) draw_button(d, u, x, y, w, h);
    else if (tg_is_menu(u) && !tg_menu_untitled(u)) draw_menu(d, u, x, y);
    else if (!strcmp(c, "STNumericBar")) {            /* the empty pill, and over it the filled part */
        float frac; const float *fill, *empty;
        if (tg_numeric_bar(u, &frac, &fill, &empty) && w > 0 && h > 0) {
            draw_pill_caps(d, x, y, w, h, empty);
            if (frac > 0) draw_pill_caps(d, x, y, fmaxf(w * frac, h), h, fill);
        }
    }
}

/* a node and its children; the parent's position is ox, oy in normalized screen units (children are placed relative to it) */
static void add_hit(Draw *d, const UI *u, float x0, float y0, float w, float h, int kind)
{
    TgUiHit *k;
    if (!d->hits || d->nhits >= d->maxhits || w <= 0 || h <= 0) return;
    k = &d->hits[d->nhits++];
    k->node = u; k->x0 = x0; k->y0 = y0; k->x1 = x0 + w; k->y1 = y0 + h; k->kind = kind; memcpy(k->clip, d->clip, sizeof k->clip);
}
/* the interactive kind of a node (0: not interactive) */
static int hit_kind(const UI *u)
{
    const char *c = class_of(u);
    if (!strcmp(c, "STButton") || !strcmp(c, "STRoundedButton") || !strcmp(c, "STToggle") || !strcmp(c, "TGButton")) return 1;
    if (tg_is_menu(u) && !tg_menu_untitled(u)) return 2;
    if (!strcmp(c, "STNumericBar")) return 3;
    if (!strcmp(c, "TGParagraph") && tg_para_editable(u)) return 4;
    return 0;
}
static void walk(Draw *d, const UI *u, float ox, float oy, int depth)
{
    float ax = ox + u->x, ay = oy + u->y;
    int i;
    if (u->parent && !strcmp(class_of(u->parent), "STNumericBar") && !strcmp(class_of(u), "TGParagraph") && u->parent->h > u->h) ay += (u->parent->h - u->h) * 0.5f - u->y;     /* the label sits in the middle of the bar */
    if (depth > 64 || !(u->flags & UF_COMPVISIBLE)) return;
    draw_node(d, u, ax * d->mw, ay * d->mh, u->w * d->mw, u->h * d->mh);
    {
        int kind = hit_kind(u);
        if (kind == 4 && u->parent && u->parent->w > 0 && u->parent->h > 0) add_hit(d, u, ox * d->mw, oy * d->mh, u->parent->w * d->mw, u->parent->h * d->mh, 4);
        else if (kind) { float w, h; tg_ui_size_of(u, &w, &h); if (kind == 2) { extern float tg_menu_title_h(const void *); h = tg_menu_title_h(u); } add_hit(d, u, ax * d->mw, ay * d->mh, w * d->mw, h * d->mh, kind); }
    }
    if (u->parent && tg_is_stylized_window(u->parent) && u->parent->nkids && u->parent->kids[0] == u) {      /* the scroll window of a stylized window cuts its contents to its own rectangle */
        float save[4], x0 = ax * d->mw, y0 = ay * d->mh, x1 = x0 + u->w * d->mw, y1 = y0 + u->h * d->mh;
        memcpy(save, d->clip, sizeof save);
        add_hit(d, u->parent, x0, y0, x1 - x0, y1 - y0, 5);
        if (d->clip[2] > d->clip[0]) { x0 = fmaxf(x0, d->clip[0]); y0 = fmaxf(y0, d->clip[1]); x1 = fminf(x1, d->clip[2]); y1 = fminf(y1, d->clip[3]); }
        d->clip[0] = x0; d->clip[1] = y0; d->clip[2] = x1 > x0 ? x1 : x0 + 0.01f; d->clip[3] = y1 > y0 ? y1 : y0 + 0.01f;
        for (i = u->nkids - 1; i >= 0; i--) walk(d, u->kids[i], ax, ay, depth + 1);
        memcpy(d->clip, save, sizeof save);
        return;
    }
    if (tg_is_menu(u)) {              /* the sub pane sits under the title bar and shows only when the menu is opened */
        if (tg_menu_opened(u) && u->nkids) walk(d, u->kids[0], ax, ay + (tg_menu_untitled(u) ? 0.0f : tg_menu_title_h(u)), depth + 1);
        return;
    }
    for (i = u->nkids - 1; i >= 0; i--) walk(d, u->kids[i], ax, ay, depth + 1);      /* index 0 is the front-most child */
}

/* Positions and sizes are cut to whole pixels one by one (ui_snap), so two pieces that should end on the same line can end one pixel
   apart. For artwork pieces that overlap or touch, an edge one pixel inside its neighbour's is moved out to it (all four sides). */
static void flush_edges(TgUiQuad *q, int n)
{
    int i, j, cnt = 0, idx[1024];
    for (i = 0; i < n && cnt < 1024; i++) if (q[i].grow) idx[cnt++] = i;
    for (i = 0; i < cnt; i++) for (j = 0; j < cnt; j++) {
        TgUiQuad *a = &q[idx[i]], *b = &q[idx[j]];
        float ar = a->x + a->w, ab = a->y + a->h, br = b->x + b->w, bb = b->y + b->h;
        if (i == j) continue;
        if (a->x > br + 0.5f || b->x > ar + 0.5f || a->y > bb + 0.5f || b->y > ab + 0.5f) continue;     /* not touching */
        if (br - ar > 0.5f && br - ar < 1.5f) a->w += br - ar;
        if (a->x - b->x > 0.5f && a->x - b->x < 1.5f) { a->w += a->x - b->x; a->x = b->x; }
        if (bb - ab > 0.5f && bb - ab < 1.5f) a->h += bb - ab;
        if (a->y - b->y > 0.5f && a->y - b->y < 1.5f) { a->h += a->y - b->y; a->y = b->y; }
    }
}

#define SCRATCH_QUADS 8192
int tg_ui_collect(TgUiHit *hits, int max)
{
    static TgUiQuad scratch[SCRATCH_QUADS];
    Draw d; UI *root = tg_ui_root();
    memset(&d, 0, sizeof d);
    d.out = scratch; d.max = SCRATCH_QUADS; d.hits = hits; d.maxhits = max;
    d.mw = (float)eng_GraphicsModeInfo_GetWidth(NULL); d.mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    if (!root || !hits || max <= 0) return 0;
    walk(&d, root, 0, 0, 0);
    return d.nhits;
}
/* the focus mark, and the mouse cursor on top of everything */
static void overlay(Draw *d)
{
    static const float ring[4] = {1.0f, 0.93f, 0.35f, 1.0f}, white[4] = {1, 1, 1, 1};
    const void *fn = tg_ui_focus_node(); float mx, my; int show, i;
    if (fn) for (i = d->nhits - 1; i >= 0; i--) if (d->hits[i].node == fn) {
        const TgUiHit *k = &d->hits[i]; float x0 = k->x0 - 2, y0 = k->y0 - 2, x1 = k->x1 + 2, y1 = k->y1 + 2;
        if (k->kind == 5) break;
        d->clip[2] = d->clip[0];
        if (k->kind == 1 && (!strcmp(class_of(k->node), "STButton") || !strcmp(class_of(k->node), "STToggle"))) {      /* a focused button shows its marker strip (child 0xbb9 = icon 3001, STButton::SetColorBasedOnFlags) */
            extern void *tg_global(const char *); const char *grp = eng_GraphicsModeInfo_GetLcarsString(eng_GraphicsModeInfo_GetCurrentMode()), *tex; float l, t, iw, ih; const float *mc = tg_global("g_kSTButtonMarkerDefault");
            float cap = 26.0f * (k->y1 - k->y0) / 28.0f;
            if (mc && tg_icon_glyph(grp, 3001, &tex, &l, &t, &iw, &ih)) { tg_icon_flags(grp, 3001, &d->rot, &d->mir); d->grow = 0; quad(d, k->x0 + cap + 0.003f * d->mw, k->y0, cap * 0.25f, k->y1 - k->y0, tex, l, t, iw, ih, mc); }
            break;
        }
        quad(d, x0, y0, x1 - x0, 2, NULL, 0, 0, 1, 1, ring); quad(d, x0, y1 - 2, x1 - x0, 2, NULL, 0, 0, 1, 1, ring);
        quad(d, x0, y0 + 2, 2, y1 - y0 - 4, NULL, 0, 0, 1, 1, ring); quad(d, x1 - 2, y0 + 2, 2, y1 - y0 - 4, NULL, 0, 0, 1, 1, ring);
        break;
    }
    tg_ui_mouse_state(&mx, &my, &show);
    if (show) {      /* TrekCursor.tga holds four arrow heads, a size for each resolution; the tip is at the top */
        static const float src[4][4] = {{35, 0, 14, 20}, {1, 0, 23, 33}, {34, 24, 25, 38}, {34, 24, 25, 38}};
        int idx = tg_res_index();
        const float *r = src[idx <= 1 ? 0 : idx == 2 ? 1 : 2];
        d->clip[2] = d->clip[0];
        quad(d, mx - r[2] * 0.5f, my, r[2], r[3], "Data/Icons/TrekCursor.tga", r[0], r[1], r[2], r[3], white);
    }
}
int tg_ui_quads(TgUiQuad *out, int max, int *mode_w, int *mode_h)
{
    Draw d;
    UI *root = tg_ui_root();
    memset(&d, 0, sizeof d);
    d.out = out; d.max = max; d.n = 0;
    d.mw = (float)eng_GraphicsModeInfo_GetWidth(NULL); d.mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    if (mode_w) *mode_w = (int)d.mw;
    if (mode_h) *mode_h = (int)d.mh;
    if (!root || !out || max <= 0) return 0;
    { static TgUiHit hits[2048]; d.hits = hits; d.maxhits = 2048; walk(&d, root, 0, 0, 0); flush_edges(out, d.n); overlay(&d); }
    return d.n;
}
