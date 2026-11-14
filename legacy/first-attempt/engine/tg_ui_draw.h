/* UI draw list: the visible UI tree as textured quads, drawn by the platform (port/platform/render_ui.c) */
#ifndef TG_UI_DRAW_H
#define TG_UI_DRAW_H

/* x, y, w, h: the destination in mode pixels (the graphics mode's size, top left origin). sx, sy, sw, sh: the source in texture
   pixels. tex: the atlas file name as the script gave it (resolved by the platform); NULL draws a plain colour. col: RGBA, multiplies
   the texel. The name points into the icon or font group and stays valid for the frame. */
typedef struct TgUiQuad {
    float x, y, w, h;
    const char *tex;
    float sx, sy, sw, sh;
    float col[4];
    float clip[4];       /* when clip[2] > clip[0]: the node was drawn inside this rectangle (mode pixels x0, y0, x1, y1); the platform cuts to it */
    int grow;            /* artwork piece (not text): the platform rounds its edges outward so abutting pieces leave no gap */
    int rot, mirror;     /* the source turned clockwise by rot quarter turns, then mirrored (1 horizontal, 2 vertical) */
    float angle;         /* the drawn quad turned clockwise about its centre by this angle in radians (the radar blips point where their ship faces) */
} TgUiQuad;

/* fills out (at most max quads), back to front; returns the count and the graphics mode size they are in */
int tg_ui_quads(TgUiQuad *out, int max, int *mode_w, int *mode_h);

/* what can be pointed at (tg_ui_input.c): the interactive nodes of the visible tree in mode pixels, back to front (the last one is on top).
   kind: 1 button (STButton, STRoundedButton, STToggle, TGButton), 2 menu title bar, 3 numeric bar, 4 editable paragraph (the rectangle is
   its entry pane's), 5 scroll area of a stylized window (the node is the window). clip: the rectangle that cuts it, clip[2] <= clip[0]: none. */
typedef struct TgUiHit {
    const void *node;
    float x0, y0, x1, y1, clip[4];
    int kind;
} TgUiHit;
/* walks the visible tree into a scratch draw list and returns its interactive nodes (at most max) */
int tg_ui_collect(TgUiHit *hits, int max);
#endif
