/* UI toolkit internals shared by tg_ui.c and tg_ui_st.c */
#ifndef TG_UI_H
#define TG_UI_H
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#define UF_ENABLED 1
#define UF_SELECTED 2
#define UF_HIGHLIGHTED 4
#define UF_VISIBLE 8
#define UF_EXCLUSIVE 0x10
#define UF_SKIPPARENT 0x20
#define UF_ALWAYSEVENTS 0x40
#define UF_USEPARENTBATCH 0x80
#define UF_NOFOCUS 0x100
#define UF_BATCHCHILD 0x200
#define UF_COMPVISIBLE 0x400

typedef struct UI {
    TGObject o;
    struct UI *parent;
    float x, y, w, h;
    unsigned flags;
    struct UI **kids; int nkids, ckids;          /* TGPane */
    struct UI *focus;
    void *text;                                   /* TGString* (paragraph / button / menu name) */
    TGEvent *act_ev, *down_ev, *up_ev;            /* TGButtonBase */
    int mwt;                                      /* MainWindow type */
    char *icon_group; int icon_num; float color[4];
    float wrap;
    void *font;                                   /* TGParagraph: its font group (NULL: the default font) */
    int is_window;                                /* TGWindow family: child operations are forwarded to the first child (the sub pane) */
    struct STW *st;
    void *ext;                                    /* class specific extension (tg_ui_st.c) */                               /* STStylizedWindow state */
} UI;
UI *as_ui(void *p); UI *as_pane(void *p); UI *fwd(void *p);
UI *ui_new(const char *cls); void *pane_new(const char *cls, float w, float h);
void pane_add(UI *p, int idx, UI *c, float x, float y, int layout);
void pane_set_focus(UI *p, UI *c);
void ui_set_pos(UI *u, float x, float y, int layout); void ui_set_size(UI *u, float w, float h, int layout);
void ui_layout(UI *u); void ui_show(UI *u, int layout); void ui_hide(UI *u, int layout);
UI *window_new(const char *cls, float w, float h, float ix, float iy, UI *sub);
UI *stw_new(const char *cls, const char *module, const char *style, void *title, float ix, float iy, UI *sub, int fixed, float fw, float fh, const float *color);
float ui_snap(float v, float scale); float ui_scr_w(void); float ui_scr_h(void);
void ui_send_event(TGEvent *e, UI *src);
void *ui_arg_ptr(PyObject *o);
void *eng_BaseObjectClass_GetContainingSet(void *); void eng_TGString_SetString(void *, char *);
void ui_set_color(UI *u, PyObject *c);
#endif
