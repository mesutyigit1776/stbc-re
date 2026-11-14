/* Sound: TGSound 0070b030.., TGSoundManager 0070ee80.., TGSoundRegion 0070dd90.. on miniaudio (port/platform/audio.c).
   The exe plays through Miles (3D samples and streams in Miles handles). The rules of the game are in the exe, not in Miles, and are kept:
   - A sound made with LoadSound is an "original" kept by name in the manager. Play makes a "playing copy" (TGSound_copy 0070b0c0) that has a
     slot id; the original is never played itself. The copy ends when its audio ends; Stop sends the end event (TGSound::Stop 0070bbf0).
   - The manager has max-sounds slots (allocSoundSlot 00711230): a free slot, else a finished sound's slot, else the playing sound of the
     lowest priority when the new sound has a higher priority. A slot id is generation + 1 + index (FUN_00711540).
   - Volume (TGSound::SetVolume 0070c0b0): master * volume, then SFX or voice switch and volume, the interface volume, and the SFX
     adjustment (the SFX are ducked while a voice plays: TGSoundManager::Update 0070f7e0).
   - 3D: the sound's node is a child of the node it is attached to; each update the position is taken relative to the listener of the
     sound's region (TGSound::Update 0070c2a0); the listener is the active camera of the set of that name (SetClass::Update 0040ffb0).
   - Region filter (TGSoundRegion::SetFilter 0070e260, FUN_0070e790/0070e7c0): filter 1 mutes the sounds of the region (the sets that are
     not rendered), 0 and 2 leave them audible.
   Approximations (marked): miniaudio's inverse distance model stands for Miles' (same min / max rule); no EAX or A3D (the manager reports
   neither); Doppler is off; the sound's save and load (WriteToStream, ReadFromStream) are not ported; reserve lists of the sound card
   (provider handles) are not needed. The max sounds at once come from options.cfg like the exe (SoundConfig.py, key ProviderReserveList). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"
#include "audio.h"

static const char *tg_class_name(void *p) { const struct cls *c = ((TGObject *)p)->cls; return c ? c->name : ""; }

extern double tg_real_time;
void prop_set_i(void *, const char *, long); long prop_get_i(void *, const char *);
extern const char *eng_TGLocalizationDatabase_GetFilename(void *, char *);
extern const char *eng_ScriptObject_GetScript(void *);
extern const char *tg_asset_resolve(const char *);
extern int tg_effects_node_pos(void *pt, float out[3]);
extern int tg_space_world_pos(void *o, float out[3]);
extern void tg_space_pose(void *o, float p[3], float f[3], float u[3], float r[3], float *scale);
extern int tg_space_listener(const char *set_name, float pos[3], float f[3], float u[3], float r[3]);
extern void *eng_global_get(const struct appc_global *g);
int tg_add_event(TGEvent *e);

#define INVALID_SLOT 0u
#define SLOT_BASE 1u                                  /* DAT_00896aa0 */
#define ET_SOUND_DELETE 0x20005                       /* the string event of a single shot sound that stopped (TGSound::Stop) */

typedef struct Region Region;
typedef struct Fade { float t, v; struct Fade *next; } Fade;
typedef struct Snd {
    TGObject o;
    char *group, *file, *name;                        /* +0, +4, +8 */
    unsigned char sfx, voice, iface;                  /* +0xc, +0xd, +0xe */
    unsigned char loaded, failed, copy, loop, streamed, is3d;   /* +0xf, +0x10, +0x11, +0x12, +0x13, +0x14 */
    unsigned char muted, flag51, paused, single_shot; /* +0x50 (the region filter), +0x51, +0x52, +0x53 */
    unsigned char have_seek; float seek;              /* +0x54, +0x58: a play position set before the sound plays */
    float orient[6];                                  /* forward and up (+0x28, +0x34) */
    float cone[3]; float pitch; float minmax[2];      /* +0x40.., +0x4c, +0x5c, +0x60 */
    float volume, priority;                           /* +0x64, +0x68 */
    float local[3];                                   /* the sound node's local translation */
    void *parent;                                     /* the node it is attached to (+0x20's parent) */
    Fade *fade;                                       /* +0x74 */
    unsigned slot;                                    /* +0x8c */
    TGEvent *end_event;                               /* +0x90 */
    Region *region;                                   /* +0x94 */
    AudioVoice *voice_h;                              /* the playing audio (+0x18) */
    int pinned, dist_set, cone_set;
    double dur;                                       /* seconds, -1 = not read yet */
    struct Snd *src, *next, *free_next;               /* copy: its original; name table link; free list link */
    double grave_time;
} Snd;

struct Region { TGObject o; char *name; int filter; Snd **snds; int n, cap; struct Region *next; };

/* ---------------------------------------------------------------- manager state ---------------------------------------------------------------- */
static Snd *named;                                    /* the name table (originals) */
static int mgr_up = 1;                                /* +0x1d: the manager is initialised */
static float vol_master = 1.0f, vol_sfx = 1.0f, vol_voice = 1.0f, vol_iface = 1.0f, vol_movie = 1.0f;   /* +0x20.. */
static int on_sfx = 1, on_voice = 1;                  /* +0x30, +0x31 */
static float sfx_adj = 1.0f, sfx_adj_min = 0.8f;      /* +0x34, +0x38 */
static int max_slots; static Snd **slots; static unsigned char *slot_used; static unsigned slot_gen;   /* +0x4c, +0x54, +0x58, +0x5c */
static int paused_all;                                /* +0x70 */
static Snd *free_list;                                /* stopped copies waiting to be destroyed (+0x90) */
static float units_per_meter = 1.0f;
static double last_idle = -1; static float frame_dt;  /* DAT_0099c6c4 */
static int audio_ready;

static Snd *as_snd(void *p) { return p && tg_is_ours(p) && ((TGObject *)p)->cls && !strcmp(tg_class_name(p), "TGSound") && !((TGObject *)p)->dead ? p : NULL; }
static char *dupz(const char *s) { return s ? strdup(s) : NULL; }
static int name_eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

/* ---------------------------------------------------------------- audio device ---------------------------------------------------------------- */
/* BC_AUDIO=0 turns the device off (headless runs and tests keep the timing without sound); BC_AUDIO=1 turns it on. The window host turns it on. */
static int want_device = -1;
static void purge_free(int force); static void snd_stop_impl(Snd *s); static void ensure_audio(void);
void tg_sound_use_device(int on)                                              /* the window host turns the output device on */
{
    int i; Snd *s;
    want_device = on;
    if (audio_ready && on && !audio_has_device()) {                           /* the silent clock was started before: switch over */
        for (i = 0; i < max_slots; i++) if (slots[i]) snd_stop_impl(slots[i]);
        purge_free(1);
        for (s = named; s; s = s->next) s->pinned = 0;
        audio_shutdown(); audio_ready = 0; ensure_audio();
    }
}
static void ensure_audio(void)
{
    const char *e;
    if (audio_ready) return;
    audio_ready = 1;
    if (want_device < 0) { e = getenv("BC_AUDIO"); want_device = e ? atoi(e) : 0; }
    audio_init(want_device);
}

/* ---------------------------------------------------------------- files ---------------------------------------------------------------- */
static char *real_path(const char *file)
{
    char p[1024]; int i; const char *real;
    if (!file) return NULL;
    snprintf(p, sizeof p, "%s", file); for (i = 0; p[i]; i++) if (p[i] == '\\') p[i] = '/';
    real = tg_asset_resolve(p);
    return real ? strdup(real) : NULL;
}

/* ---------------------------------------------------------------- slots ---------------------------------------------------------------- */
static void set_max_slots(int n);
static int slot_index(unsigned id) { return (id != INVALID_SLOT && max_slots) ? (int)((id - SLOT_BASE) % (unsigned)max_slots) : -1; }   /* getSoundSlotIndex 00711510 */
static void snd_stop_impl(Snd *s);
static void snd_destroy(Snd *s);
static int snd_status(Snd *s);

static unsigned alloc_slot(Snd *orig)                                         /* TGSoundManager::allocSoundSlot 00711230 */
{
    int idx = -1, best = -1, i; float best_prio = 1.0f; int reused = 1;
    for (i = 0; i < max_slots; i++) {
        Snd *s = slots[i];
        if (!s) { reused = slot_used[i]; idx = i; if (!reused) break; }
        else {
            if (s == orig) { reused = slot_used[i]; idx = i; break; }
            if (!s->paused && snd_status(s) != 0) { reused = slot_used[i]; idx = i; if (!reused) break; }
            if (s->priority < best_prio) { best_prio = s->priority; best = i; }
        }
    }
    if (idx == -1) {
        if (orig->priority <= best_prio || best == -1) return INVALID_SLOT;
        reused = slot_used[best]; idx = best;
    }
    if (reused) { slot_gen += (unsigned)max_slots; memset(slot_used, 0, (size_t)max_slots); }
    if (slots[idx]) snd_stop_impl(slots[idx]);
    slots[idx] = NULL;
    slot_used[idx] = 1;
    return slot_gen + SLOT_BASE + (unsigned)idx;                                /* FUN_00711540 */
}
static void set_max_slots(int n)                                              /* TGSoundManager::SetMaxSoundsAtOnce 0070f920 */
{
    int i;
    if (n < 1) n = 1;
    for (i = 0; i < max_slots; i++) if (slots[i]) snd_stop_impl(slots[i]);
    slot_gen += (unsigned)max_slots;
    free(slots); free(slot_used);
    max_slots = n; slots = calloc((size_t)n, sizeof *slots); slot_used = calloc((size_t)n, 1);
    if (slot_gen % (unsigned)n) slot_gen += (unsigned)n - slot_gen % (unsigned)n;
}
static Snd *playing_by_slot(unsigned id)                                      /* TGSoundManager::GetPlayingSound 0070fc10 */
{
    int i;
    if (!mgr_up || (i = slot_index(id)) < 0) return NULL;
    return slots[i] && slots[i]->slot == id ? slots[i] : NULL;
}

/* ---------------------------------------------------------------- volume ---------------------------------------------------------------- */
static float effective_volume(Snd *s, float v)                                /* TGSound::SetVolume 0070c0b0 */
{
    v = vol_master * v;
    if (!s->voice) {
        if (s->sfx) v = on_sfx ? v * vol_sfx : 0.0f;
    } else v = on_voice ? v * vol_voice : 0.0f;
    if (s->iface) v = on_sfx ? v * vol_iface : 0.0f;
    if (!s->voice) v *= sfx_adj;
    return v;
}
static long snd_set_volume(Snd *s, float v)
{
    s->volume = v;
    if (s->voice_h) { audio_set_volume(s->voice_h, effective_volume(s, v)); return 1; }
    return 0;
}

/* ---------------------------------------------------------------- the sound object ---------------------------------------------------------------- */
static Snd *snd_alloc(void) { return tg_new_plain("TGSound", sizeof(Snd)); }
static void snd_defaults(Snd *s)
{
    s->cone[0] = s->cone[1] = 360.0f; s->cone[2] = 1.0f; s->pitch = 1.0f;
    s->minmax[0] = 50.0f; s->minmax[1] = 700.0f; s->volume = 1.0f; s->priority = 0.5f; s->dur = -1; s->orient[2] = 0; s->orient[1] = 1.0f; s->orient[5] = 1.0f;
}
static void reg_add_table(Snd *s)                                             /* FUN_00710910: the name table (a name is replaced) */
{
    Snd **pp;
    if (!s->name) return;
    for (pp = &named; *pp; pp = &(*pp)->next) if (!strcmp((*pp)->name, s->name)) { *pp = (*pp)->next; break; }
    s->next = named; named = s;
}
static void unload(Snd *s);
static long snd_load(Snd *s)                                                  /* TGSound::Load 0070b730 */
{
    if (!mgr_up) return 0;
    if (s->muted && s->copy) return 0;
    if (s->loaded) return s->loaded;
    if (s->failed) return 0;
    s->loaded = 1;
    if (!s->copy) {
        if (!s->streamed) {                                                   /* findOrLoadSoundFileEntry: the file must be there */
            char *p = real_path(s->file);
            if (!p) s->loaded = 0;
            free(p);
        }
    } else {
        char *p = real_path(s->file);
        int fl = (s->is3d ? 1 : 0) | (s->streamed ? 2 : 0);
        ensure_audio();
        if (p && !s->streamed && s->src && !s->src->pinned && audio_is_open()) { audio_pin(p); s->src->pinned = 1; }   /* the decoded data stays while the original lives */
        s->voice_h = p && audio_is_open() ? audio_open(p, fl) : NULL;
        if (!s->voice_h) s->loaded = 0;
        free(p);
        if (s->voice_h) {
            audio_set_pitch(s->voice_h, s->pitch > 0 ? s->pitch : 1.0f);
            audio_set_volume(s->voice_h, effective_volume(s, s->volume));
            if (s->is3d) { audio_set_distances(s->voice_h, s->minmax[0], s->minmax[1]); s->dist_set = 1; }
        }
    }
    s->failed = !s->loaded;
    return s->loaded;
}
static void unload(Snd *s)                                                    /* TGSound::Unload 0070b930 */
{
    if (!s->loaded) return;
    if (s->voice_h) { audio_stop(s->voice_h); audio_close(s->voice_h); s->voice_h = NULL; }
    if (!s->copy && s->pinned) { char *p = real_path(s->file); if (p) { audio_unpin(p); free(p); } s->pinned = 0; }
    s->loaded = 0;
}
static Snd *snd_new(const char *file, const char *name, unsigned flags, int copy)   /* TGSound::TGSound + Init 0070b360 */
{
    Snd *s = snd_alloc();
    snd_defaults(s);
    s->file = dupz(file); s->name = dupz(name); s->is3d = flags & 1; s->streamed = (flags >> 1) & 1; s->copy = copy;
    s->sfx = 1;                                                               /* Init: +0xc = 1 (an SFX sound until a script says otherwise) */
    s->slot = INVALID_SLOT;
    prop_set_i(s, "SingleShot", 0);
    if (!(flags & 4)) snd_load(s);                                            /* LS_DELAY_LOADING (4): load on the first play */
    if (!copy) reg_add_table(s);
    return s;
}

/* ---------------------------------------------------------------- regions ---------------------------------------------------------------- */
static Region *regions;
static Region *as_region(void *p) { return p && tg_is_ours(p) && !strcmp(tg_class_name(p), "TGSoundRegion") ? p : NULL; }
static void filter_apply(Snd *s, int f)                                       /* FUN_0070e790 */
{
    if (f == 1 && !s->muted) { snd_stop_impl(s); if (s->loaded && s->copy) unload(s); s->muted = 1; }
}
static void filter_remove(Snd *s, int f) { if (f == 1) s->muted = 0; }        /* FUN_0070e7c0 */
void *eng_TGSoundRegion_GetRegion(char *name)                                 /* 0070e0a0 */
{ Region *r; if (!name) return NULL; for (r = regions; r; r = r->next) if (!strcmp(r->name, name)) return r; return NULL; }
void *eng_TGSoundRegion_Create(char *name)                                    /* 0070dd90: a region of that name is replaced */
{
    Region *r = tg_new("TGSoundRegion", sizeof(Region), 0), **pp;
    r->name = strdup(name ? name : ""); r->filter = 2;
    for (pp = &regions; *pp; pp = &(*pp)->next) if (!strcmp((*pp)->name, r->name)) { (*pp)->o.dead = 1; *pp = (*pp)->next; break; }
    r->next = regions; regions = r; return r;
}
static void region_remove(Snd *s)                                             /* TGSoundRegion::RemoveSound 0070e180 */
{
    Region *r = s->region; int i;
    if (!r) return;
    for (i = 0; i < r->n; i++) if (r->snds[i] == s) { memmove(r->snds + i, r->snds + i + 1, (size_t)(r->n - i - 1) * sizeof *r->snds); r->n--; break; }
    filter_remove(s, r->filter);
    s->region = NULL;
}
long tg_region_add(void *rp, void *sp)                                        /* 0070e0f0: a sound is in one region at a time */
{
    Region *r = as_region(rp); Snd *s = as_snd(sp);
    if (!r || !s) return 0;
    if (s->region == r) return 1;
    if (s->region) region_remove(s);
    s->region = r;
    if (r->n == r->cap) { r->cap = r->cap ? r->cap * 2 : 8; r->snds = realloc(r->snds, (size_t)r->cap * sizeof *r->snds); }
    r->snds[r->n++] = s;
    filter_apply(s, r->filter);
    return 1;
}
long eng_TGSoundRegion_AddSound(void *p, PyObject *o)
{
    void *sp = NULL;
    if (!o || o == Py_None || !appc_to_ptr(o, &sp, "TGSound")) { PyErr_Clear(); return 0; }
    return tg_region_add(p, sp);
}
long eng_TGSoundRegion_RemoveSound(void *p, PyObject *o)
{
    Region *r = as_region(p); void *sp = NULL; Snd *s;
    if (!r || !o || o == Py_None || !appc_to_ptr(o, &sp, "TGSound")) { PyErr_Clear(); return 0; }
    s = as_snd(sp);
    if (!s || s->region != r) return 0;
    region_remove(s); return 1;
}
long eng_TGSoundRegion_SetFilter(void *p, int f)                              /* 0070e260 */
{
    Region *r = as_region(p); int i, old;
    if (!r) return 0;
    old = r->filter;
    for (i = 0; i < r->n; i++) filter_remove(r->snds[i], old);
    r->filter = f;
    for (i = 0; i < r->n; i++) filter_apply(r->snds[i], f);
    return 1;
}
int tg_region_filter(const char *name) { Region *r = eng_TGSoundRegion_GetRegion((char *)name); return r ? r->filter : -1; }

/* ---------------------------------------------------------------- playing ---------------------------------------------------------------- */
static void snd_set_end_event(Snd *s, TGEvent *e)                             /* TGSound::SetEndEvent 0070c690: the old event is deleted */
{
    if (s->end_event == e) return;
    if (s->end_event) { if (s->end_event->refcount) s->end_event->refcount--; s->end_event = NULL; }
    if (e) { e->refcount++; s->end_event = e; }
}
static int snd_status(Snd *s)                                                 /* TGSound::GetStatus 0070bd00: 0 playing, 1 stopped or finished, 2 not loaded */
{
    if (!s->loaded) return 2;
    if (!s->copy) return 1;
    if (!s->voice_h || s->paused) return 1;
    return audio_is_playing(s->voice_h) ? 0 : 1;
}
static void post_delete_event(Snd *s)                                         /* a single shot sound asks the manager to delete it */
{
    TGEvent *e = tg_ev_new("TGStringEvent", 0);
    e->type = ET_SOUND_DELETE; e->str = strdup(s->name ? s->name : "");
    { const struct appc_global *g; for (g = appc_globals; g->name; g++) if (!strcmp(g->name, "g_kSoundManager")) e->dest = eng_global_get(g); }
    tg_add_event(e);
}
static void release_slot(Snd *s)                                              /* TGSoundManager::ReleaseSlot 007113c0 */
{
    int i = slot_index(s->slot);
    if (i >= 0 && slots[i] == s) {
        if (s->copy) { s->free_next = free_list; free_list = s; }
        slots[i] = NULL; s->slot = INVALID_SLOT;
    }
}
static void snd_stop_impl(Snd *s)                                             /* TGSound::Stop 0070bbf0 */
{
    if (snd_status(s) == 0 && s->voice_h) audio_stop(s->voice_h);
    if (s->copy) unload(s);
    if (s->end_event) { TGEvent *e = s->end_event; s->end_event = NULL; tg_add_event(e); if (e->refcount) e->refcount--; }
    release_slot(s);
    if (s->single_shot) post_delete_event(s);
}
static void snd_destroy(Snd *s)                                               /* TGSound::~TGSound 0070b590 */
{
    if (s->slot != INVALID_SLOT) { Snd *p = playing_by_slot(s->slot); if (p) snd_stop_impl(p); }
    unload(s);
    if (s->region) region_remove(s);
    snd_set_end_event(s, NULL);
    while (s->fade) { Fade *f = s->fade; s->fade = f->next; free(f); }
    s->parent = NULL;
    s->o.dead = 1;
    if (!s->copy) {                                                           /* TGSoundManager::removeSound 007109b0 */
        Snd **pp;
        for (pp = &named; *pp; pp = &(*pp)->next) if (*pp == s) { *pp = s->next; break; }
    }
}

/* ---- 3D: the listener of the sound's region ---- */
static int node_world(void *n, float out[3])
{
    if (!n) return 0;
    if (tg_effects_node_pos(n, out)) return 1;
    return tg_space_world_pos(n, out);
}
static void snd_update_3d(Snd *s)                                             /* TGSound::Update 0070c2a0, second half */
{
    float w[3] = {0, 0, 0}, l[3] = {0, 0, 0}, f[3] = {0, 1, 0}, u[3] = {0, 0, 1}, r[3] = {1, 0, 0}, d[3]; float fd, ud, rd;
    if (!s->voice_h || !s->is3d) return;
    if (s->parent && node_world(s->parent, w)) {
        w[0] += s->local[0]; w[1] += s->local[1]; w[2] += s->local[2];
    } else { w[0] = s->local[0]; w[1] = s->local[1]; w[2] = s->local[2]; }
    if (s->region) { if (!tg_space_listener(s->region->name, l, f, u, r)) { l[0] = l[1] = l[2] = 0; f[0] = 0; f[1] = 1; f[2] = 0; u[0] = u[1] = 0; u[2] = 1; r[0] = 1; r[1] = r[2] = 0; } }
    d[0] = w[0] - l[0]; d[1] = w[1] - l[1]; d[2] = w[2] - l[2];
    fd = d[0] * f[0] + d[1] * f[1] + d[2] * f[2]; ud = d[0] * u[0] + d[1] * u[1] + d[2] * u[2]; rd = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
    /* the exe's listener frame is (forward, up, right) and Miles gets (x, y, -z) of it; miniaudio's listener looks along -Z with +X to the right */
    audio_set_position(s->voice_h, rd, ud, -fd);
    if (s->cone_set) {
        float cf = s->orient[0] * f[0] + s->orient[1] * f[1] + s->orient[2] * f[2], cu = s->orient[0] * u[0] + s->orient[1] * u[1] + s->orient[2] * u[2],
              cr = s->orient[0] * r[0] + s->orient[1] * r[1] + s->orient[2] * r[2];
        audio_set_cone(s->voice_h, s->cone[0], s->cone[1], s->cone[2], cr, cu, -cf);
    }
}
static void snd_update(Snd *s)                                                /* TGSound::Update 0070c2a0 */
{
    if (s->fade) {                                                            /* fade points: relative to the play position */
        float pos = s->voice_h ? (float)audio_tell(s->voice_h) : (s->have_seek ? s->seek : 0.0f);
        float v = s->volume, prev_t = 0, prev_v, next_t, next_v; int passed = 0; Fade *p;
        prev_v = v; next_t = prev_t; next_v = prev_v;
        for (p = s->fade; p; p = p->next) {
            if (pos < p->t) { next_t = p->t; next_v = p->v; break; }
            prev_t = p->t; prev_v = p->v; passed++; next_t = prev_t; next_v = prev_v;
        }
        while (passed > 1) { Fade *h = s->fade; s->fade = h->next; free(h); passed--; }
        v = next_v;
        if (next_t != prev_t && pos <= next_t) { float k = (next_t - pos) / (next_t - prev_t); v = k * prev_v + (1.0f - k) * next_v; }
        if (s->fade && !s->fade->next && s->fade->v == v) { Fade *h = s->fade; s->fade = NULL; free(h); }
        snd_set_volume(s, v);
    }
    snd_update_3d(s);
}
static int snd_start(Snd *s, int rewind)                                      /* FUN_0070bb80 */
{
    if (rewind && s->voice_h) audio_rewind(s->voice_h);                       /* Rewind */
    if (s->voice_h) audio_set_loop(s->voice_h, s->loop);                      /* streams loop through the manager, samples through the loop flag: both loop here */
    snd_update(s);
    if (!paused_all || (!s->sfx && !s->voice)) {
        if (!s->voice_h || !audio_start(s->voice_h)) return 0;
    } else s->paused = 1;                                                     /* the pause wins: it starts at the unpause */
    return 1;
}
static Snd *snd_copy(Snd *o)                                                  /* TGSound::TGSound_copy 0070b0c0 */
{
    Snd *c = snd_alloc(); Fade *p, **pp;
    snd_defaults(c);
    c->file = dupz(o->file); c->name = dupz(o->name); c->is3d = o->is3d; c->streamed = o->streamed; c->copy = 1; c->src = o; c->slot = INVALID_SLOT;
    c->muted = o->muted; c->flag51 = o->flag51;
    c->group = dupz(o->group); c->sfx = o->sfx; c->voice = o->voice; c->iface = o->iface;
    c->parent = o->parent;
    memcpy(c->cone, o->cone, sizeof c->cone); c->cone_set = o->cone_set; memcpy(c->minmax, o->minmax, sizeof c->minmax);
    c->loop = o->loop; c->volume = o->volume; c->priority = o->priority; c->pitch = o->pitch;
    c->have_seek = o->have_seek; c->seek = o->seek;
    c->single_shot = o->single_shot;
    memcpy(c->orient, o->orient, sizeof c->orient); memcpy(c->local, o->local, sizeof c->local);
    for (pp = &c->fade, p = o->fade; p; p = p->next) { Fade *n = malloc(sizeof *n); *n = *p; n->next = NULL; *pp = n; pp = &n->next; }
    snd_load(c);                                                              /* Init with the copy flag loads it */
    if (c->loaded && c->have_seek && c->voice_h) audio_seek(c->voice_h, c->seek);
    if (o->region) tg_region_add(o->region, c);
    return c;
}
static int snd_trace(void) { static int on = -1; if (on < 0) on = getenv("BC_SND_TRACE") != NULL; return on; }   /* BC_SND_TRACE=1 logs the plays */
long eng_TGSound_PlayAndNotify(void *p, void *ev, int rewind)                 /* 0070ba60: returns the slot id, 0 when it cannot play */
{
    Snd *o = as_snd(p), *c = NULL; TGEvent *e = ev; unsigned slot = INVALID_SLOT; int ok;
    if (snd_trace() && o) fprintf(stderr, "[snd %8.2f] play %s (%s)%s%s\n", tg_real_time, o->name ? o->name : "?", o->file ? o->file : "?", o->parent ? " attached" : "", o->region ? " region" : "");
    if (!o) { if (e) tg_add_event(e); return 0; }
    ok = mgr_up && !o->copy && (o->loaded || snd_load(o));                    /* CanPlay 0070ba20 */
    if (ok) { slot = alloc_slot(o); if (slot == INVALID_SLOT) ok = 0; }
    if (ok) {
        c = snd_copy(o);
        if (c->loaded) {
            snd_set_end_event(c, e);
            { int i = slot_index(slot); if (i >= 0) { slots[i] = c; c->slot = slot; } }
            if (snd_start(c, rewind)) return (long)slot;
            snd_stop_impl(c);
            return INVALID_SLOT;
        }
    }
    if (snd_trace() && o) fprintf(stderr, "[snd %8.2f] CANNOT PLAY %s: %s\n", tg_real_time, o->name ? o->name : "?", !o->loaded ? "not loaded (file missing?)" : slot == INVALID_SLOT ? "no free slot / priority too low" : "copy did not open (muted region or decode error)");
    if (e) tg_add_event(e);                                                   /* the sound could not play: the end event comes at once */
    if (c) snd_destroy(c);
    return INVALID_SLOT;
}
long eng_TGSound_Play(void *p, int rewind) { return eng_TGSound_PlayAndNotify(p, NULL, rewind); }   /* 0070ba10 */

/* ---------------------------------------------------------------- python API: TGSound ---------------------------------------------------------------- */
static void snd_set_flags(Snd *s, unsigned f) { s->is3d = f & 1; s->streamed = (f >> 1) & 1; }
void *eng_TGSound_Create(char *file, char *name, int flags) { Snd *s; (void)snd_set_flags; if (!file) return NULL; s = snd_new(file, name, (unsigned)flags, 0); return s; }
void *eng_ScriptObject_LoadSound(void *o, char *file, char *name, int flags)            /* 0040a1b0: group = the script object's name */
{ Snd *s; if (!file) return NULL; s = snd_new(file, name, (unsigned)flags, 0); free(s->group); s->group = dupz(eng_ScriptObject_GetScript(o)); return s; }
void *eng_ScriptObject_LoadDatabaseSound(void *o, void *db, char *name, int flags)
{ const char *f = db ? eng_TGLocalizationDatabase_GetFilename(db, name) : NULL; return f ? eng_ScriptObject_LoadSound(o, (char *)f, name, flags) : NULL; }
void *eng_Game_LoadSoundInGroup(void *g, char *file, char *name, char *group, int flags)  /* 00406cf0 */
{ Snd *s; if (!g || !file) return NULL; s = snd_new(file, name, (unsigned)flags, 0); free(s->group); s->group = dupz(group); return s; }
void *eng_Game_LoadDatabaseSoundInGroup(void *g, void *db, char *name, char *group, int flags)
{ const char *f = db ? eng_TGLocalizationDatabase_GetFilename(db, name) : NULL; return f ? eng_Game_LoadSoundInGroup(g, (char *)f, name, group, flags) : NULL; }
void eng_TGSound_SetGroup(void *p, char *g) { Snd *s = as_snd(p); if (s) { free(s->group); s->group = dupz(g); } }
const char *eng_TGSound_GetGroup(void *p) { Snd *s = as_snd(p); return s ? s->group : NULL; }
long eng_TGSound_Load(void *p) { Snd *s = as_snd(p); return s ? snd_load(s) : 0; }
void eng_TGSound_Unload(void *p) { Snd *s = as_snd(p); if (s) unload(s); }
long eng_TGSound_IsLoaded(void *p) { Snd *s = as_snd(p); return s && s->loaded; }
const char *eng_TGSound_GetSoundName(void *p) { Snd *s = as_snd(p); return s ? s->name : NULL; }
const char *eng_TGSound_GetFileName(void *p) { Snd *s = as_snd(p); return s ? s->file : NULL; }
long eng_TGSound_Is3D(void *p) { Snd *s = as_snd(p); return s && s->is3d; }
long eng_TGSound_IsStreamed(void *p) { Snd *s = as_snd(p); return s && s->streamed; }
void eng_TGSound_SetSingleShot(void *p, int v) { Snd *s = as_snd(p); if (s) s->single_shot = v != 0; }
long eng_TGSound_IsSingleShot(void *p) { Snd *s = as_snd(p); return s && s->single_shot; }
void eng_TGSound_SetSFX(void *p, int v) { Snd *s = as_snd(p); if (s) s->sfx = v != 0; }
long eng_TGSound_IsSFX(void *p) { Snd *s = as_snd(p); return s && s->sfx; }
void eng_TGSound_SetVoice(void *p, int v) { Snd *s = as_snd(p); if (s) s->voice = v != 0; }
long eng_TGSound_IsVoice(void *p) { Snd *s = as_snd(p); return s && s->voice; }
void eng_TGSound_SetInterface(void *p, int v) { Snd *s = as_snd(p); if (s) s->iface = v != 0; }
long eng_TGSound_IsInterface(void *p) { Snd *s = as_snd(p); return s && s->iface; }
void eng_TGSound_SetLooping(void *p, int v) { Snd *s = as_snd(p); if (s) s->loop = v != 0; }
long eng_TGSound_GetLooping(void *p) { Snd *s = as_snd(p); return s && s->loop; }
long eng_TGSound_SetVolume(void *p, float v) { Snd *s = as_snd(p); return s ? snd_set_volume(s, v) : 0; }
double eng_TGSound_GetVolume(void *p) { Snd *s = as_snd(p); return s ? s->volume : 0.0; }
long eng_TGSound_SetPriority(void *p, float v) { Snd *s = as_snd(p); if (!s) return 0; s->priority = v; return 1; }
double eng_TGSound_GetPriority(void *p) { Snd *s = as_snd(p); return s ? s->priority : 0.0; }
long eng_TGSound_SetPitch(void *p, float v) { Snd *s = as_snd(p); if (!s) return 0; s->pitch = v; if (s->voice_h) audio_set_pitch(s->voice_h, v); return 1; }
double eng_TGSound_GetPitch(void *p) { Snd *s = as_snd(p); return s ? s->pitch : 0.0; }
long eng_TGSound_SetPlayPositionSeconds(void *p, float t)
{ Snd *s = as_snd(p); if (!s) return 0; if (s->voice_h) audio_seek(s->voice_h, t); else { s->have_seek = 1; s->seek = t; } return 1; }
double eng_TGSound_GetPlayPositionSeconds(void *p)
{ Snd *s = as_snd(p); if (!s) return 0.0; if (s->voice_h) return audio_tell(s->voice_h); return s->have_seek ? s->seek : 0.0; }
long eng_TGSound_SetPlayPosition(void *p, PyObject *o) { return 1; }          /* a position in samples: nothing in the scripts uses it */
PyObject *eng_TGSound_GetPlayPosition(void *p) { return PyInt_FromLong(0); }
double eng_TGSound_GetDurationSeconds(void *p)
{
    Snd *s = as_snd(p);
    if (!s) return 0.0;
    if (s->voice_h) { double d = audio_length(s->voice_h); return d < 0 ? 0 : d; }
    if (s->src) s = s->src;
    if (s->dur < 0) {                                                         /* an original has no audio: the length is read from the file when asked */
        char *path = real_path(s->file); AudioVoice *v = NULL;
        ensure_audio();
        if (path && audio_is_open()) v = audio_open(path, 2);
        s->dur = v ? audio_length(v) : 0; if (s->dur < 0) s->dur = 0;
        audio_close(v); free(path);
    }
    return s->dur;
}
long eng_TGSound_Stop(void *p)
{
    Snd *s = as_snd(p); if (!s) return 0;
    snd_stop_impl(s);                                                         /* Stop 0070bbf0 works on the object it is called on (a playing copy); StopSound(name) stops the copies of a name */
    return 1;
}
long eng_TGSound_Rewind(void *p) { Snd *s = as_snd(p); if (s && s->voice_h) audio_rewind(s->voice_h); return 1; }
void eng_TGSound_Pause(void *p) { Snd *s = as_snd(p); if (s && s->copy && s->voice_h) { s->paused = 1; audio_stop(s->voice_h); } }
void eng_TGSound_Unpause(void *p) { Snd *s = as_snd(p); if (s && s->copy && s->voice_h) { s->paused = 0; audio_start(s->voice_h); } }
long eng_TGSound_IsPaused(void *p) { Snd *s = as_snd(p); return s && s->paused; }
long eng_TGSound_GetStatus(void *p) { Snd *s = as_snd(p); return s ? snd_status(s) : 2; }
long eng_TGSound_AttachToNode(void *p, void *n) { Snd *s = as_snd(p); if (!s) return 0; s->parent = n; return 1; }
long eng_TGSound_DetachFromNode(void *p) { Snd *s = as_snd(p); if (!s || !s->parent) return 0; s->parent = NULL; return 1; }
void *eng_TGSound_GetParentNode(void *p) { Snd *s = as_snd(p); return s ? s->parent : NULL; }
long eng_TGSound_SetOrientation(void *p, void *f, void *u) { Snd *s = as_snd(p); if (!s) return 0; if (f) memcpy(s->orient, f, 12); if (u) memcpy(s->orient + 3, u, 12); return 1; }
long eng_TGSound_GetOrientation(void *p, void *f, void *u) { Snd *s = as_snd(p); if (!s) return 0; if (f) memcpy(f, s->orient, 12); if (u) memcpy(u, s->orient + 3, 12); return 1; }
void eng_TGSound_SetPosition(void *p, void *pt) { Snd *s = as_snd(p); if (s && pt) memcpy(s->local, pt, 12); }
void *eng_TGSound_GetPosition(void *p) { Snd *s = as_snd(p); return s ? tg_mem_dup(s->local, 12) : NULL; }
long eng_TGSound_SetConeData(void *p, float a, float b, float v)
{ Snd *s = as_snd(p); if (!s) return 0; s->cone[0] = a; s->cone[1] = b; s->cone[2] = v; s->cone_set = !(a >= 360.0f && b >= 360.0f && v == 1.0f); return 1; }
long eng_TGSound_GetConeData(void *p, void *a, void *b, void *v) { Snd *s = as_snd(p); if (!s) return 0; if (a) *(float *)a = s->cone[0]; if (b) *(float *)b = s->cone[1]; if (v) *(float *)v = s->cone[2]; return 1; }
long eng_TGSound_SetMinMaxDistance(void *p, float a, float b)
{ Snd *s = as_snd(p); if (!s) return 0; s->minmax[0] = a; s->minmax[1] = b; if (s->voice_h) audio_set_distances(s->voice_h, a, b); return 1; }
long eng_TGSound_GetMinMaxDistance(void *p, void *a, void *b) { Snd *s = as_snd(p); if (!s) return 0; if (a) *(float *)a = s->minmax[0]; if (b) *(float *)b = s->minmax[1]; return 1; }
long eng_TGSound_SetEndEvent(void *p, void *ev) { Snd *s = as_snd(p); if (!s) return 0; snd_set_end_event(s, ev && tg_is_ours(ev) && tg_is_type(ev, 0x101) ? ev : NULL); return 1; }
void *eng_TGSound_GetEndEvent(void *p) { Snd *s = as_snd(p); return s ? s->end_event : NULL; }
void eng_TGSound_Update(void *p) { Snd *s = as_snd(p); if (s) snd_update(s); }
PyObject *eng_TGSound_GetNiSourceObj(void *p) { Py_INCREF(Py_None); return Py_None; }
void eng_TGSound_AddFadePoint(void *p, float t, float v)                      /* 0070c4f0: sorted by time, volume 0 to 1 */
{
    Snd *s = as_snd(p); Fade *n, **pp;
    if (!s) return;
    if (t < 0) t = 0;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    n = malloc(sizeof *n); n->t = t; n->v = v;
    for (pp = &s->fade; *pp && (*pp)->t <= t; pp = &(*pp)->next) ;
    n->next = *pp; *pp = n;
}
void eng_TGSound_ClearFadePoints(void *p) { Snd *s = as_snd(p); if (s) while (s->fade) { Fade *f = s->fade; s->fade = f->next; free(f); } }

/* ---------------------------------------------------------------- TGSoundManager ---------------------------------------------------------------- */
void *eng_TGSoundManager_GetSound(void *m, char *name)                        /* 0070fbb0 */
{ Snd *s; if (!name || !mgr_up) return NULL; for (s = named; s; s = s->next) if (!strcmp(s->name, name)) return s; return NULL; }
void *eng_TGSoundManager_GetPlayingSound(void *m, long slot) { return playing_by_slot((unsigned)slot); }
long eng_TGSoundManager_PlaySound(void *m, char *name) { void *s = eng_TGSoundManager_GetSound(m, name); return s ? eng_TGSound_Play(s, 1) : 0; }   /* 0070fcd0 */
long eng_TGSoundManager_StopSound(void *m, char *name)                        /* 0070fd00: every playing copy of that name */
{
    int i;
    if (!mgr_up) return 1;
    for (i = 0; i < max_slots; i++) if (slots[i] && name_eq(slots[i]->name, name)) snd_stop_impl(slots[i]);
    return 1;
}
long eng_TGSoundManager_StopPlayingSound(void *m, long slot)                  /* 0070fd80 */
{ Snd *s; if (mgr_up && (s = playing_by_slot((unsigned)slot))) snd_stop_impl(s); return 1; }
long eng_TGSoundManager_StopAllSounds(void *m) { int i; if (!mgr_up) return 1; for (i = 0; i < max_slots; i++) if (slots[i]) snd_stop_impl(slots[i]); return 1; }
long eng_TGSoundManager_StopAllSoundsInGroup(void *m, char *g)
{
    int i;
    if (!mgr_up) return 1;
    for (i = 0; i < max_slots; i++) {
        Snd *s = slots[i];
        if (s && s->group && (!g || !strcmp(s->group, g))) snd_stop_impl(s);
    }
    return 1;
}
static void purge_free(int force)                                             /* purgeFreeSounds 007114a0 */
{
    Snd **pp = &free_list;
    while (*pp) {
        Snd *s = *pp;
        (void)force; *pp = s->free_next; snd_destroy(s);
    }
}
long eng_TGSoundManager_DeleteSound(void *m, char *name)                      /* 0070ff40 */
{
    Snd *s; Snd **pp;
    if (!mgr_up || !name) return 0;
    for (pp = &free_list; *pp; ) { if (name_eq((*pp)->name, name)) { Snd *d = *pp; *pp = d->free_next; snd_destroy(d); } else pp = &(*pp)->free_next; }
    s = eng_TGSoundManager_GetSound(m, name);
    if (!s) return 0;
    eng_TGSoundManager_StopSound(m, name);
    snd_destroy(s);
    return 1;
}
void eng_TGSoundManager_DeleteAllSounds(void *m)                              /* 007100a0 */
{
    if (!mgr_up) return;
    eng_TGSoundManager_StopAllSounds(m); purge_free(1);
    while (named) snd_destroy(named);
}
void eng_TGSoundManager_DeleteAllSoundsInGroup(void *m, char *g)              /* 00710140 */
{
    Snd *s, *n;
    if (!mgr_up) return;
    eng_TGSoundManager_StopAllSoundsInGroup(m, g); purge_free(1);
    for (s = named; s; s = n) { n = s->next; if (s->group && g && !strcmp(s->group, g)) snd_destroy(s); }
}
long eng_TGSoundManager_GetNumSoundsAllocated(void *m) { long n = 0; Snd *s; for (s = named; s; s = s->next) n++; return n; }
long eng_TGSoundManager_GetRoughMemoryUsage(void *m) { return eng_TGSoundManager_GetNumSoundsAllocated(m) * 0x198; }   /* Init adds 0x198 per sound */
long eng_TGSoundManager_Reload(void *m, int a) { return 1; }
void eng_TGSoundManager_SetMaxSoundsAtOnce(void *m, int n) { set_max_slots(n); }
long eng_TGSoundManager_GetMaxSoundsAtOnce(void *m) { return max_slots; }
long eng_TGSoundManager_SetUnitsPerMeter(void *m, float v) { if (v < 0) return 0; units_per_meter = v; return 1; }
double eng_TGSoundManager_GetUnitsPerMeter(void *m) { return units_per_meter; }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static void update_volumes(void)                                              /* UpdateSoundVolumes 0070faa0 */
{ int i; for (i = 0; i < max_slots; i++) if (slots[i]) snd_set_volume(slots[i], slots[i]->volume); }
void eng_TGSoundManager_SetMasterVolume(void *m, float v) { if (!mgr_up) return; vol_master = clamp01(v); update_volumes(); }
double eng_TGSoundManager_GetMasterVolume(void *m) { return vol_master; }
void eng_TGSoundManager_SetSFXVolume(void *m, float v) { if (!mgr_up) return; vol_sfx = clamp01(v); update_volumes(); }
double eng_TGSoundManager_GetSFXVolume(void *m) { return vol_sfx; }
void eng_TGSoundManager_SetVoiceVolume(void *m, float v) { if (!mgr_up) return; vol_voice = clamp01(v); update_volumes(); }
double eng_TGSoundManager_GetVoiceVolume(void *m) { return vol_voice; }
void eng_TGSoundManager_SetInterfaceVolume(void *m, float v) { if (!mgr_up) return; vol_iface = clamp01(v); update_volumes(); }
double eng_TGSoundManager_GetInterfaceVolume(void *m) { return vol_iface; }
void eng_TGSoundManager_SetSFXEnabled(void *m, int v) { if (v != on_sfx) { on_sfx = v != 0; update_volumes(); } }
long eng_TGSoundManager_IsSFXEnabled(void *m) { return on_sfx; }
void eng_TGSoundManager_SetVoiceEnabled(void *m, int v) { if (v != on_voice) { on_voice = v != 0; update_volumes(); } }
long eng_TGSoundManager_IsVoiceEnabled(void *m) { return on_voice; }
void eng_TGSoundManager_SetEnabled(void *m, int v) { mgr_up = v != 0; }       /* py 005db1d0 writes +0x1d, the flag every manager call checks first */
long eng_TGSoundManager_IsEnabled(void *m) { return mgr_up; }               /* py 005db250: +0x1c (started) and +0x1d */
void eng_TGMovieManager_SetMovieVolume(void *m, float v) { vol_movie = v; }
double tg_sound_movie_volume(void) { return vol_movie; }
void eng_TGSoundManager_SetSFXAdjustmentMinimum(void *m, float v) { sfx_adj_min = v < 0 ? 0 : v > 1 ? 1 : v; }   /* 0070fe30 */
long eng_TGSoundManager_IsA3DSupported(void *m) { return 0; }
long eng_TGSoundManager_IsEAXSupported(void *m) { return 0; }
void eng_delete_TGSoundManager(void *m) { }
void tg_sound_pause_all(int on)                                               /* PauseAllSounds 0070fdb0 / UnpauseAllSounds 0070fdf0 (UtopiaApp::SetPaused 00438e60) */
{
    int i;
    for (i = 0; i < max_slots; i++) {
        Snd *s = slots[i];
        if (!s) continue;
        if (on) { if ((s->sfx || s->voice) && s->voice_h) { s->paused = 1; audio_stop(s->voice_h); } }
        else if (s->paused && (s->sfx || s->voice) && s->voice_h) { s->paused = 0; audio_start(s->voice_h); }
    }
    paused_all = on != 0;
}

/* InterfaceModule::PlayClickSound (py 00620820) only sets a flag; InterfaceModule::Update 00555b10 plays "UIBeep" from the start once a frame */
static int click_pending;
void eng_InterfaceModule_PlayClickSound(void *m) { click_pending = 1; }

/* Manager::Update 0070f7e0, once a frame */
void tg_music_update(void);
void tg_sound_idle(void)
{
    int i, any_voice = 0; double dt;
    if (last_idle < 0) last_idle = tg_real_time;
    dt = tg_real_time - last_idle; last_idle = tg_real_time; frame_dt = (float)(dt > 0 ? (dt > 0.25 ? 0.25 : dt) : 0);
    if (!audio_ready) ensure_audio();
    audio_pump(tg_real_time);
    if (!mgr_up) return;
    for (i = 0; i < max_slots; i++) {
        Snd *s = slots[i];
        if (!s) continue;
        if (!s->streamed || !s->loop) {
            if (!s->paused && snd_status(s) != 0) snd_stop_impl(s);
            else { snd_update(s); if (s->voice) any_voice = 1; }
        } else {
            if (s->voice_h) audio_set_loop(s->voice_h, s->loop);
            if (!s->paused && snd_status(s) != 0) snd_start(s, 1);
        }
    }
    {
        int changed = 0;
        if (!any_voice) { if (sfx_adj < 1.0f) { sfx_adj += (1.0f - sfx_adj_min) * 5.0f * frame_dt; if (sfx_adj > 1.0f) sfx_adj = 1.0f; changed = 1; } }
        else if (sfx_adj > sfx_adj_min) { sfx_adj -= (1.0f - sfx_adj_min) * 5.0f * frame_dt; if (sfx_adj < sfx_adj_min) sfx_adj = sfx_adj_min; changed = 1; }
        if (changed) update_volumes();
    }
    purge_free(0);
    if (click_pending) { click_pending = 0; eng_TGSoundManager_StopSound(NULL, "UIBeep"); eng_TGSoundManager_PlaySound(NULL, "UIBeep"); }
    tg_music_update();
}

/* ---------------------------------------------------------------- start-up and the C API of the engine ---------------------------------------------------------------- */
/* LoadSoundConfig 00710340: options.cfg [Sound] ProviderReserveList names an entry of SoundConfig.AllProviderReserveLists whose first number is the
   max sounds at once (LowQualitySoftware 8, HighQualitySoftware 16). The exe's first run writes LowQualitySoftware into the cfg; the port has no hardware voice
   limit, so when the key is missing it keeps the manager's constructor value, 32. A cfg that names one of the lists gets its number. Approximation. */
static void on_sound_delete(TGEvent *e) { if (e->str) eng_TGSoundManager_DeleteSound(NULL, e->str); }
extern long eng_TGConfigMapping_HasValue(void *, char *, char *);
extern const char *eng_TGConfigMapping_GetStringValue(void *, char *, char *);
void tg_sound_init(void)
{
    static int done; int n = 32;                                              /* the manager's constructor value (DAT_00896aa4); the first run of the exe writes the cfg key and gets 8 */
    if (done) return;
    done = 1;
    if (eng_TGConfigMapping_HasValue(NULL, "Sound", "ProviderReserveList")) {
        const char *l = eng_TGConfigMapping_GetStringValue(NULL, "Sound", "ProviderReserveList");
        if (l && !strcmp(l, "LowQualitySoftware")) n = 8;
        else if (l && !strcmp(l, "HighQualitySoftware")) n = 16;
    }
    set_max_slots(n);
    {   const struct appc_global *g;                                                  /* HandleDeleteSoundEvent 00711460 */
        for (g = appc_globals; g->name; g++) if (!strcmp(g->name, "g_kSoundManager")) { void *m = eng_global_get(g); if (m) tg_register_native(m, ET_SOUND_DELETE, on_sound_delete); }
    }
}
void *tg_sound_named(const char *name) { return eng_TGSoundManager_GetSound(NULL, (char *)name); }
long tg_sound_play(void *s) { return eng_TGSound_Play(s, 1); }
void tg_sound_stop_slot(long slot) { eng_TGSoundManager_StopPlayingSound(NULL, slot); }
void *tg_sound_playing(long slot) { return playing_by_slot((unsigned)slot); }
int tg_sound_is_playing_name(const char *name)                                 /* FUN_0070fc50: a copy of that name is playing */
{ int i; for (i = 0; i < max_slots; i++) if (slots[i] && name_eq(slots[i]->name, name)) return 1; return 0; }
void *tg_sound_playing_named(const char *name)
{ int i; for (i = 0; i < max_slots; i++) if (slots[i] && name_eq(slots[i]->name, name)) return slots[i]; return NULL; }
void tg_sound_set_loop(void *s, int v) { eng_TGSound_SetLooping(s, v); }
void tg_sound_set_priority(void *s, float v) { eng_TGSound_SetPriority(s, v); }
void tg_sound_set_volume(void *s, float v) { eng_TGSound_SetVolume(s, v); }
void tg_sound_set_voice(void *s, int v) { eng_TGSound_SetVoice(s, v); }
void tg_sound_attach(void *s, void *node) { eng_TGSound_AttachToNode(s, node); }
void tg_sound_set_min_max(void *s, float a, float b) { eng_TGSound_SetMinMaxDistance(s, a, b); }
long tg_sound_play_notify(void *s, void *ev, int rewind) { return eng_TGSound_PlayAndNotify(s, ev, rewind); }
void tg_sound_set_seek(void *s, float t) { eng_TGSound_SetPlayPositionSeconds(s, t); }
const char *tg_sound_name(void *s) { Snd *x = as_snd(s); return x ? x->name : NULL; }
int tg_sound_is_loaded(void *s) { Snd *x = as_snd(s); return x && x->loaded; }
int tg_sound_loop_of(void *s) { Snd *x = as_snd(s); return x ? x->loop : 0; }
float tg_sound_volume_of(void *s) { Snd *x = as_snd(s); return x ? x->volume : 0; }
long tg_sound_slot_of(void *s) { Snd *x = as_snd(s); return x ? (long)x->slot : 0; }
void tg_sound_set_group_flag(void *s, int sfx, int voice, int iface) { Snd *x = as_snd(s); if (x) { x->sfx = sfx; x->voice = voice; x->iface = iface; } }

/* ---- interface sounds: the exe's widgets look the sound up by name and play it (STButton::ButtonClicked 0051a4e0 UIButtonClicked, STMenu::Open 00526c10 UIMenuOpened,
   STMenu::Close 00526d50 UIMenuClosed, STNumericBar::SetValue 0052a8d0 UINumericBarClick, STStylizedWindow::StartPeriodicScroll 00531e60 UIScrolling) ---- */
void tg_ui_sound(const char *name) { void *s = eng_TGSoundManager_GetSound(NULL, (char *)name); if (s) eng_TGSound_Play(s, 1); }
void tg_ui_sound_once(const char *name)                                       /* only when no copy of it is playing (the numeric bar's tick) */
{ if (!tg_sound_is_playing_name(name)) tg_ui_sound(name); }
void tg_ui_sound_loop(const char *name, int on)                               /* the scroll sound: loops while the window scrolls */
{
    if (!on) { eng_TGSoundManager_StopSound(NULL, (char *)name); return; }
    if (!tg_sound_is_playing_name(name)) { void *s = eng_TGSoundManager_GetSound(NULL, (char *)name); if (s) { eng_TGSound_SetLooping(s, 1); eng_TGSound_Play(s, 1); } }
}

/* WarpFlash::Initialize 004d4950 plays "Warp Flash" from the flash's node (at the ship) in the set's sound region. The flash picture itself is not drawn yet
   (the warp visuals come with the effects); the objects these calls return stay placeholders. */
extern void *appc_placeholder(void);
extern void *eng_BaseObjectClass_GetContainingSet(void *); extern const char *eng_SetClass_GetName(void *);
static void flash_sound(void *set, const float pos[3])
{
    const char *n = set ? eng_SetClass_GetName(set) : NULL; void *r = n ? eng_TGSoundRegion_GetRegion((char *)n) : NULL, *snd = r ? eng_TGSoundManager_GetSound(NULL, "Warp Flash") : NULL; Snd *x;
    if (!snd || !(x = as_snd(snd))) return;
    x->parent = NULL; memcpy(x->local, pos, 12);                             /* the copy takes the position of the flash */
    tg_region_add(r, snd); eng_TGSound_Play(snd, 1);
    x->local[0] = x->local[1] = x->local[2] = 0;
}
void *eng_WarpFlash_Create(void *ship, float scale)
{ float p[3]; if (ship && tg_space_world_pos(ship, p)) flash_sound(eng_BaseObjectClass_GetContainingSet(ship), p); return appc_placeholder(); }
void *eng_WarpFlash_CreateWithoutShip(void *set, void *pos, void *rot, float a, float b)
{ if (set && pos) flash_sound(set, pos); return appc_placeholder(); }
