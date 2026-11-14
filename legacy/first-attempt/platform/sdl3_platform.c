/* Platform layer: one window and one SDL3 GPU device, a frame clear and the event pump, for the Python host.
   Python side (Appc module, added by engine_register_host):
     platform_open(title, w, h) -> 1 or 0      window + GPU device (Metal on macOS; SDL_GPU_SHADERFORMAT_MSL)
     platform_pump() -> 1 while the window is open, 0 after a quit or close request
     platform_clear(r, g, b)                    clears the window through a GPU render pass and presents it
     platform_ticks() -> float seconds           SDL's monotonic clock
     platform_render(r, g, b) -> (objects, triangles)   draws the rendered set (render.c)
     Keyboard and mouse input go to the key bindings (tg_key_event), see the input section below.
     platform_keys() -> list of key names pressed since the last call (SDL key names, e.g. 'Space', 'Return', 'C'); Escape closes
     platform_fx() -> (beams, projectiles, explosions) drawn in the last frame
     platform_window_size() -> (w, h) of the drawable in pixels
     platform_display_size() -> (w, h) of the usable desktop area in points
     platform_display_modes() -> list of (w, h) the display offers for full screen, largest first
     platform_set_window(w, h, fullscreen)      sets the window size (points) and the full screen state
     platform_resized() -> 1 once after the drawable size changed
     platform_close()
   The renderer (meshes, textures, effects) is in render.c; this layer only owns the window, the device and the frame. */
#include <Python.h>
#include <stdio.h>
#include <SDL3/SDL.h>

static SDL_Window *win;
static SDL_GPUDevice *dev;
static int quit_requested, resized_flag;
static char keys[16][32]; static int nkeys;                      /* key presses since the last platform_keys() */

static PyObject *p_open(PyObject *self, PyObject *args)
{
    char *title; int w, h;
    if (!PyArg_ParseTuple(args, "sii:platform_open", &title, &w, &h)) return NULL;
    if (win) return PyInt_FromLong(1);
    if (!SDL_Init(SDL_INIT_VIDEO)) { PyErr_SetString(PyExc_RuntimeError, SDL_GetError()); return NULL; }
    win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) { PyErr_SetString(PyExc_RuntimeError, SDL_GetError()); return NULL; }
    dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, 0, NULL);
    if (!dev) { PyErr_SetString(PyExc_RuntimeError, SDL_GetError()); return NULL; }
    if (!SDL_ClaimWindowForGPUDevice(dev, win)) { PyErr_SetString(PyExc_RuntimeError, SDL_GetError()); return NULL; }
    quit_requested = 0;
    { extern void tg_sound_use_device(int); const char *e = getenv("BC_AUDIO"); tg_sound_use_device(e ? atoi(e) : 1); }   /* a window plays the sound (BC_AUDIO=0 keeps the silent clock) */
    return PyInt_FromLong(1);
}

/* Keyboard and mouse input. The WC_ codes are the App.WC_* constants the key bindings use (DefaultKeyboardBinding.py).
   SDL gives letters and digits their ASCII codes, which are the WC codes of the lower-case letters and digits. */
long tg_key_event(int key, int state, void *win);           /* tg_services.c: posts the bound events */
void *eng_TacticalControlWindow_GetTacticalControlWindow(void);
static long wc_named(const char *name) { extern long tg_const(const char *); return tg_const(name); }
static long wc_for_key(SDL_Keycode k, SDL_Keymod mod)
{
    static const struct { SDL_Keycode k; const char *wc; } named[] = {
        {SDLK_ESCAPE, "WC_ESCAPE"}, {SDLK_RETURN, "WC_RETURN"}, {SDLK_KP_ENTER, "WC_NUMPADENTER"}, {SDLK_SPACE, "WC_SPACE"},
        {SDLK_TAB, "WC_TAB"}, {SDLK_BACKSPACE, "WC_BACKSPACE"}, {SDLK_DELETE, "WC_DELETE"}, {SDLK_INSERT, "WC_INSERT"},
        {SDLK_HOME, "WC_HOME"}, {SDLK_END, "WC_END"}, {SDLK_PAGEUP, "WC_PAGEUP"}, {SDLK_PAGEDOWN, "WC_PAGEDOWN"},
        {SDLK_LEFT, "WC_LEFT"}, {SDLK_RIGHT, "WC_RIGHT"}, {SDLK_UP, "WC_UP"}, {SDLK_DOWN, "WC_DOWN"},
        {SDLK_LSHIFT, "WC_SHIFT"}, {SDLK_RSHIFT, "WC_SHIFT"}, {SDLK_LCTRL, "WC_CTRL"}, {SDLK_RCTRL, "WC_CTRL"},
        {SDLK_LALT, "WC_ALT"}, {SDLK_RALT, "WC_ALT"}, {SDLK_PRINTSCREEN, "WC_PRINTSCREEN"}, {SDLK_SCROLLLOCK, "WC_SCROLL"},
        {SDLK_PAUSE, "WC_PAUSE"}, {SDLK_KP_PLUS, "WC_ADD"}, {SDLK_KP_MINUS, "WC_SUBTRACT"}, {SDLK_KP_MULTIPLY, "WC_MULTIPLY"},
        {SDLK_KP_DIVIDE, "WC_DIVIDE"}, {SDLK_KP_PERIOD, "WC_DECIMAL"}, {SDLK_KP_0, "WC_NUMPAD0"}, {SDLK_KP_1, "WC_NUMPAD1"},
        {SDLK_KP_2, "WC_NUMPAD2"}, {SDLK_KP_3, "WC_NUMPAD3"}, {SDLK_KP_4, "WC_NUMPAD4"}, {SDLK_KP_5, "WC_NUMPAD5"},
        {SDLK_KP_6, "WC_NUMPAD6"}, {SDLK_KP_7, "WC_NUMPAD7"}, {SDLK_KP_8, "WC_NUMPAD8"}, {SDLK_KP_9, "WC_NUMPAD9"},
        {SDLK_F1, "WC_F1"}, {SDLK_F2, "WC_F2"}, {SDLK_F3, "WC_F3"}, {SDLK_F4, "WC_F4"}, {SDLK_F5, "WC_F5"}, {SDLK_F6, "WC_F6"},
        {SDLK_F7, "WC_F7"}, {SDLK_F8, "WC_F8"}, {SDLK_F9, "WC_F9"}, {SDLK_F10, "WC_F10"}, {SDLK_F11, "WC_F11"}, {SDLK_F12, "WC_F12"},
        {SDLK_GRAVE, "WC_BACKQUOTE"}, {SDLK_EQUALS, "WC_EQUALS"}, {SDLK_MINUS, "WC_MINUS"}, {SDLK_LEFTBRACKET, "WC_OPEN_BRACKET"},
        {SDLK_RIGHTBRACKET, "WC_CLOSE_BRACKET"}, {SDLK_BACKSLASH, "WC_BACKSLASH"}, {SDLK_COMMA, "WC_COMMA"},
        {SDLK_PERIOD, "WC_PERIOD"}, {SDLK_SLASH, "WC_SLASH"}, {SDLK_SEMICOLON, "WC_SEMICOLON"}, {SDLK_APOSTROPHE, "WC_QUOTE"},
    };
    char nm[24]; long c; int i; int shift = (mod & SDL_KMOD_SHIFT) != 0, ctrl = (mod & SDL_KMOD_CTRL) != 0, alt = (mod & SDL_KMOD_ALT) != 0;
    if ((k >= 'a' && k <= 'z') || (k >= '0' && k <= '9')) {
        int u = k >= 'a' && k <= 'z' ? k - 'a' + 'A' : k;               /* the upper-case name, as WC_CTRL_Q, WC_ALT_1, WC_CAPS_K */
        if (ctrl) { snprintf(nm, sizeof nm, "WC_CTRL_%c", u); if ((c = wc_named(nm))) return c; }
        if (alt) { snprintf(nm, sizeof nm, "WC_ALT_%c", u); if ((c = wc_named(nm))) return c; }
        if (shift && k >= 'a' && k <= 'z') { snprintf(nm, sizeof nm, "WC_CAPS_%c", u); if ((c = wc_named(nm))) return c; }
        snprintf(nm, sizeof nm, "WC_%c", (int)k);
        return wc_named(nm);
    }
    for (i = 0; i < (int)(sizeof named / sizeof named[0]); i++) if (named[i].k == k) return wc_named(named[i].wc);
    return 0;
}
static long wc_for_button(Uint8 b)                       /* SDL: 1 left, 2 middle, 3 right */
{ return b == SDL_BUTTON_LEFT ? wc_named("WC_LBUTTON") : b == SDL_BUTTON_MIDDLE ? wc_named("WC_MBUTTON") : b == SDL_BUTTON_RIGHT ? wc_named("WC_RBUTTON") : 0; }
static void post_input(long code, int state)
{
    if (code) tg_key_event((int)code, state, eng_TacticalControlWindow_GetTacticalControlWindow());
}

/* a window point to a point of the graphics mode (the UI is drawn scaled and centred in the window: render_ui.c) */
extern long eng_GraphicsModeInfo_GetWidth(void *);
extern long eng_GraphicsModeInfo_GetHeight(void *);
static void to_mode(float wx, float wy, float *mx, float *my)
{
    int pw = 1, ph = 1, lw = 1, lh = 1; float sx, sy, mw, mh, sc, ox, oy;
    SDL_GetWindowSizeInPixels(win, &pw, &ph); SDL_GetWindowSize(win, &lw, &lh);
    sx = lw > 0 ? (float)pw / (float)lw : 1.0f; sy = lh > 0 ? (float)ph / (float)lh : 1.0f;
    mw = (float)eng_GraphicsModeInfo_GetWidth(NULL); mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    sc = SDL_min((float)pw / mw, (float)ph / mh); ox = ((float)pw - mw * sc) * 0.5f; oy = ((float)ph - mh * sc) * 0.5f;
    *mx = (wx * sx - ox) / sc; *my = (wy * sy - oy) / sc;
}
void tg_ui_mouse_move(float x, float y);
int tg_ui_mouse_button(int button, int down, float x, float y);
int tg_ui_wheel(float dy);
int tg_ui_key(int wc, int state, int shift);
int tg_ui_text(const char *utf8);
int tg_ui_wants_text(void);
int tg_ui_cursor_visible(void);
static int text_on, cursor_hidden;

static PyObject *p_pump(PyObject *self, PyObject *args)
{
    SDL_Event e; float fx, fy;
    if (!PyArg_ParseTuple(args, ":platform_pump")) return NULL;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit_requested = 1;
        if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) resized_flag = 1;
        if (e.type == SDL_EVENT_MOUSE_MOTION) { to_mode(e.motion.x, e.motion.y, &fx, &fy); tg_ui_mouse_move(fx, fy); }
        if (e.type == SDL_EVENT_TEXT_INPUT) tg_ui_text(e.text.text);
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {                 /* the UI first (with key repeat for the arrows), then the game bindings */
            long code = wc_for_key(e.key.key, e.key.mod); int used = 0;
            if (code)
                used = tg_ui_key((int)code, e.type == SDL_EVENT_KEY_UP ? 2 : e.key.repeat ? 3 : 1, (e.key.mod & SDL_KMOD_SHIFT) != 0);
            if (!used && !e.key.repeat) {
                post_input(code, e.type == SDL_EVENT_KEY_DOWN ? 1 : 2);
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    if (e.key.key == SDLK_ESCAPE) quit_requested = 1;
                    else if (nkeys < 16) { SDL_strlcpy(keys[nkeys], SDL_GetKeyName(e.key.key), sizeof keys[0]); nkeys++; }
                }
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            to_mode(e.button.x, e.button.y, &fx, &fy);
            if (!tg_ui_mouse_button(e.button.button, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, fx, fy))
                post_input(wc_for_button(e.button.button), e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 2);
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y != 0 && !tg_ui_wheel(e.wheel.y))     /* a wheel step is a key press (bound to speed up or down) */
            post_input(wc_named(e.wheel.y > 0 ? "WC_SCROLL_WHEEL_UP" : "WC_SCROLL_WHEEL_DOWN"), 1);
    }
    if (win) {
        int want = tg_ui_wants_text(), hide = tg_ui_cursor_visible();
        if (want != text_on) { if (want) SDL_StartTextInput(win); else SDL_StopTextInput(win); text_on = want; }
        if (hide != cursor_hidden) { if (hide) SDL_HideCursor(); else SDL_ShowCursor(); cursor_hidden = hide; }
    }
    return PyInt_FromLong(win && !quit_requested);
}

static PyObject *p_clear(PyObject *self, PyObject *args)
{
    double r, g, b; SDL_GPUCommandBuffer *cmd; SDL_GPUTexture *tex = NULL; Uint32 tw = 0, th = 0; SDL_GPURenderPass *pass;
    SDL_GPUColorTargetInfo ct;
    if (!PyArg_ParseTuple(args, "ddd:platform_clear", &r, &g, &b)) return NULL;
    if (!dev || !win) { PyErr_SetString(PyExc_RuntimeError, "platform_open first"); return NULL; }
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) { PyErr_SetString(PyExc_RuntimeError, SDL_GetError()); return NULL; }
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &tex, &tw, &th) || !tex) {
        SDL_SubmitGPUCommandBuffer(cmd);                       /* a minimized window has no swapchain: skip the frame */
        return PyInt_FromLong(0);
    }
    SDL_zero(ct);
    ct.texture = tex;
    ct.clear_color.r = (float)r; ct.clear_color.g = (float)g; ct.clear_color.b = (float)b; ct.clear_color.a = 1.0f;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    return PyInt_FromLong(1);
}

static PyObject *p_ticks(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":platform_ticks")) return NULL;
    return PyFloat_FromDouble((double)SDL_GetTicksNS() / 1e9);
}

static PyObject *p_close(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":platform_close")) return NULL;
    if (dev && win) SDL_ReleaseWindowFromGPUDevice(dev, win);
    if (dev) SDL_DestroyGPUDevice(dev);
    if (win) SDL_DestroyWindow(win);
    dev = NULL; win = NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

/* render.c: the rendered set, one frame. Returns (objects drawn, triangles drawn). */
int pl_render_scene(double r, double g, double b, int *objects, int *tris);
int pl_snapshot(const char *path, int *tris);
SDL_GPUDevice *pl_gpu(void) { return dev; }
SDL_Window *pl_window(void) { return win; }

static PyObject *p_render(PyObject *self, PyObject *args)
{
    double r, g, b; int objs = 0, tris = 0;
    if (!PyArg_ParseTuple(args, "ddd:platform_render", &r, &g, &b)) return NULL;
    if (!pl_render_scene(r, g, b, &objs, &tris)) return Py_BuildValue("(ii)", 0, 0);
    return Py_BuildValue("(ii)", objs, tris);
}

static PyObject *p_snapshot(PyObject *self, PyObject *args)
{
    char *path; int tris = 0, objs;
    if (!PyArg_ParseTuple(args, "s:platform_snapshot", &path)) return NULL;
    objs = pl_snapshot(path, &tris);
    if (objs < 0) { PyErr_SetString(PyExc_RuntimeError, "snapshot failed"); return NULL; }
    return Py_BuildValue("(ii)", objs, tris);
}

static PyObject *p_keys(PyObject *self, PyObject *args)
{
    PyObject *l; int i;
    if (!PyArg_ParseTuple(args, ":platform_keys")) return NULL;
    l = PyList_New(nkeys);
    for (i = 0; i < nkeys; i++) PyList_SetItem(l, i, PyString_FromString(keys[i]));
    nkeys = 0;
    return l;
}

void pl_fx_counts(int *beams, int *shots, int *booms);
static PyObject *p_fx(PyObject *self, PyObject *args)
{
    int b = 0, s = 0, e = 0;
    if (!PyArg_ParseTuple(args, ":platform_fx")) return NULL;
    pl_fx_counts(&b, &s, &e);
    return Py_BuildValue("(iii)", b, s, e);
}

static PyObject *p_window_size(PyObject *self, PyObject *args)
{
    int w = 0, h = 0;
    if (!PyArg_ParseTuple(args, ":platform_window_size")) return NULL;
    if (win) SDL_GetWindowSizeInPixels(win, &w, &h);
    return Py_BuildValue("(ii)", w, h);
}
static PyObject *p_display_size(PyObject *self, PyObject *args)
{
    SDL_Rect r; SDL_DisplayID id; int w = 1280, h = 720;
    if (!PyArg_ParseTuple(args, ":platform_display_size")) return NULL;
    id = win ? SDL_GetDisplayForWindow(win) : SDL_GetPrimaryDisplay();
    if (!id) id = SDL_GetPrimaryDisplay();
    if (id && SDL_GetDisplayUsableBounds(id, &r) && r.w > 0 && r.h > 0) { w = r.w; h = r.h; }
    return Py_BuildValue("(ii)", w, h);
}
static PyObject *p_display_modes(PyObject *self, PyObject *args)
{
    SDL_DisplayID id; int n = 0, i; SDL_DisplayMode **m; PyObject *l = PyList_New(0);
    if (!PyArg_ParseTuple(args, ":platform_display_modes")) return NULL;
    id = win ? SDL_GetDisplayForWindow(win) : SDL_GetPrimaryDisplay();
    m = id ? SDL_GetFullscreenDisplayModes(id, &n) : NULL;
    for (i = 0; m && i < n; i++) {
        PyObject *t = Py_BuildValue("(ii)", m[i]->w, m[i]->h); int dup = 0, k;
        for (k = 0; k < PyList_Size(l); k++) if (PyObject_Compare(PyList_GetItem(l, k), t) == 0) dup = 1;
        if (!dup) PyList_Append(l, t);
        Py_DECREF(t);
    }
    SDL_free(m);
    return l;
}
static PyObject *p_set_window(PyObject *self, PyObject *args)
{
    int w, h, fs = 0;
    if (!PyArg_ParseTuple(args, "ii|i:platform_set_window", &w, &h, &fs)) return NULL;
    if (win) { SDL_SetWindowFullscreen(win, fs != 0); if (!fs) SDL_SetWindowSize(win, w, h); }
    return PyInt_FromLong(win != NULL);
}
static PyObject *p_resized(PyObject *self, PyObject *args)
{
    int r = resized_flag;
    if (!PyArg_ParseTuple(args, ":platform_resized")) return NULL;
    resized_flag = 0;
    return PyInt_FromLong(r);
}

/* Test hooks: events pushed into SDL's queue, so a test goes through the same conversion as a real mouse or keyboard.
   platform_inject_mouse(kind, button, x, y): kind 0 move, 1 press, 2 release, 3 wheel (y is the step); x and y are window points.
   platform_inject_key(name, down, shift): the SDL key name ("Down", "Return", "Tab" ...). platform_inject_text(text).
   platform_window_point(mx, my) -> the window point that is the point (mx, my) of the graphics mode. */
static PyObject *p_inject_mouse(PyObject *self, PyObject *args)
{
    int kind, button; float x, y; SDL_Event e;
    if (!PyArg_ParseTuple(args, "iiff:platform_inject_mouse", &kind, &button, &x, &y)) return NULL;
    SDL_zero(e);
    if (kind == 0) { e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = x; e.motion.y = y; }
    else if (kind == 3) { e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = y; }
    else { e.type = kind == 1 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP; e.button.button = (Uint8)button; e.button.x = x; e.button.y = y; e.button.down = kind == 1; }
    SDL_PushEvent(&e);
    return PyInt_FromLong(1);
}
static PyObject *p_inject_key(PyObject *self, PyObject *args)
{
    char *name; int down, shift = 0; SDL_Event e; SDL_Keycode k;
    if (!PyArg_ParseTuple(args, "si|i:platform_inject_key", &name, &down, &shift)) return NULL;
    k = SDL_GetKeyFromName(name);
    if (k == SDLK_UNKNOWN) { PyErr_SetString(PyExc_ValueError, "unknown key name"); return NULL; }
    SDL_zero(e); e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.key = k; e.key.down = down != 0; e.key.mod = shift ? SDL_KMOD_LSHIFT : 0;
    SDL_PushEvent(&e);
    return PyInt_FromLong(1);
}
static PyObject *p_inject_text(PyObject *self, PyObject *args)
{
    char *text; SDL_Event e;
    if (!PyArg_ParseTuple(args, "s:platform_inject_text", &text)) return NULL;
    SDL_zero(e); e.type = SDL_EVENT_TEXT_INPUT; e.text.text = SDL_strdup(text);
    SDL_PushEvent(&e);
    return PyInt_FromLong(1);
}
static PyObject *p_window_point(PyObject *self, PyObject *args)
{
    float mx, my, sx, sy, mw, mh, sc, ox, oy; int pw = 1, ph = 1, lw = 1, lh = 1;
    if (!PyArg_ParseTuple(args, "ff:platform_window_point", &mx, &my)) return NULL;
    if (!win) return Py_BuildValue("(ff)", 0.0, 0.0);
    SDL_GetWindowSizeInPixels(win, &pw, &ph); SDL_GetWindowSize(win, &lw, &lh);
    sx = lw > 0 ? (float)pw / (float)lw : 1.0f; sy = lh > 0 ? (float)ph / (float)lh : 1.0f;
    mw = (float)eng_GraphicsModeInfo_GetWidth(NULL); mh = (float)eng_GraphicsModeInfo_GetHeight(NULL);
    sc = SDL_min((float)pw / mw, (float)ph / mh); ox = ((float)pw - mw * sc) * 0.5f; oy = ((float)ph - mh * sc) * 0.5f;
    return Py_BuildValue("(ff)", (mx * sc + ox) / sx, (my * sc + oy) / sy);
}

static PyMethodDef platform_methods[] = {
    {"platform_inject_mouse", p_inject_mouse, 1},
    {"platform_inject_key", p_inject_key, 1},
    {"platform_inject_text", p_inject_text, 1},
    {"platform_window_point", p_window_point, 1},
    {"platform_window_size", p_window_size, 1},
    {"platform_display_size", p_display_size, 1},
    {"platform_display_modes", p_display_modes, 1},
    {"platform_set_window", p_set_window, 1},
    {"platform_resized", p_resized, 1},
    {"platform_keys", p_keys, 1},
    {"platform_fx", p_fx, 1},
    {"platform_snapshot", p_snapshot, 1},
    {"platform_render", p_render, 1},
    {"platform_open", p_open, 1},
    {"platform_pump", p_pump, 1},
    {"platform_clear", p_clear, 1},
    {"platform_ticks", p_ticks, 1},
    {"platform_close", p_close, 1},
    {NULL, NULL}
};

/* called from engine_register_host (tg_core.c) with the Appc module's dictionary */
void platform_register_host(PyObject *dict)
{
    PyMethodDef *m;
    for (m = platform_methods; m->ml_name; m++) {
        PyObject *f = PyCFunction_New(m, NULL);
        if (f) { PyDict_SetItemString(dict, m->ml_name, f); Py_DECREF(f); }
    }
}
