/* Music: TGMusic 00713760.. of stbc.exe. DynamicMusic.py drives it (LoadMusic, StartMusic, StopMusic, PlayFanfare, ET_MUSIC_DONE).
   Rules from the exe:
   - LoadMusic (00713ad0) makes a TGSound, streamed when options.cfg [Sound] StreamMusic is set (the default), in group "Music", not an
     SFX sound, priority 1.0, and keeps the float it was given (the fade length and bar length in seconds) in a table by name.
   - StartMusic (00713d60) with other music playing fades the old music out and the new one in. The fade starts on the next multiple of the
     old music's table float (StartFade 00714050: the next bar) and lasts that long (2 seconds when the table has no such number).
     With no music playing the new music starts at once at full volume. A start already waiting is dropped when another StartMusic comes.
   - A fade is a TGMusicFadeEvent (0x20003) on a timer of the real time clock; TGMusic::HandleFadeEvent 00714940 starts the music of an
     event (end event ET_MUSIC_DONE 0x20004) or, when it is playing already, only sets the fade; TGMusic::Update 00714690 moves the fade
     volumes each frame, stops a sound that faded to 0, and stops any other music sound once all fades are done.
   - PlayFanfare (00714260) fades the music out and plays the fanfare after it; when the fanfare is done (MusicDone 00715020) the music
     comes back at the position it was at.
   Real time here is tg_real_time, the clock the exe's timers use for music (DAT_009a09d0 + 0x90). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

#define ET_MUSIC_FADE 0x20003
#define ET_MUSIC_DONE 0x20004

extern double tg_real_time;
extern void *eng_TGSoundManager_GetSound(void *, char *);
extern long eng_TGSoundManager_StopSound(void *, char *);
extern void eng_TGSoundManager_DeleteAllSoundsInGroup(void *, char *);
extern long eng_TGSoundManager_DeleteSound(void *, char *);
extern void *tg_sound_named(const char *);
extern long tg_sound_play_notify(void *, void *, int);
extern void tg_sound_stop_slot(long);
extern void *tg_sound_playing(long);
extern void *tg_sound_playing_named(const char *);
extern void tg_sound_set_loop(void *, int);
extern void tg_sound_set_priority(void *, float);
extern void tg_sound_set_volume(void *, float);
extern void tg_sound_set_seek(void *, float);
extern void tg_sound_set_group_flag(void *, int, int, int);
extern const char *tg_sound_name(void *);
extern int tg_sound_is_loaded(void *);
extern int tg_sound_loop_of(void *);
extern float tg_sound_volume_of(void *);
extern long tg_sound_slot_of(void *);
extern double eng_TGSound_GetPlayPositionSeconds(void *);
extern void *eng_TGSound_Create(char *, char *, int);
extern void eng_TGSound_SetGroup(void *, char *);
extern long eng_TGSound_Stop(void *);
extern long eng_TGConfigMapping_HasValue(void *, char *, char *);
extern long eng_TGConfigMapping_GetIntValue(void *, char *, char *);
extern void *eng_global_get(const struct appc_global *g);

typedef struct Entry { char *name; float len; struct Entry *next; } Entry;            /* the table of LoadMusic: name -> the float */
typedef struct Fade { long slot; float total, rem, from, to; struct Fade *next; } Fade; /* a running fade (the 0x14 byte records at +0x2c) */
typedef struct Pend {                                                                 /* a TGMusicFadeEvent on its timer */
    double fire; char *name; float from, to, dur, seek; int loop, in_list, cancelled; struct Pend *next;
} Pend;

static Entry *entries;
static Pend *pend;
static Fade *fades;
static char *cur_name; static long cur_slot;                  /* +0x14, +0x18 */
static float volume = 1.0f; static double last_update = -1;   /* +0x1c, +0x20 */
static int enabled = 1;                                       /* +0x24 */
static int fanfare; static char *fan_name, *saved_name;       /* +0x68, +0x6c, +0x70 */
static int saved_loop = 1; static float saved_pos;            /* +0x74, +0x78 */
static void *music_obj;

static char *dupz(const char *s) { return s ? strdup(s) : NULL; }
static Entry *entry_of(const char *name) { Entry *e; for (e = entries; e; e = e->next) if (name && !strcmp(e->name, name)) return e; return NULL; }

static Pend *pend_add(double fire, const char *name, float from, float to, float dur, float seek, int loop, int in_list)
{
    Pend *p = calloc(1, sizeof *p), **pp;
    p->fire = fire; p->name = strdup(name); p->from = from; p->to = to; p->dur = dur; p->seek = seek; p->loop = loop; p->in_list = in_list;
    for (pp = &pend; *pp; pp = &(*pp)->next) ;
    *pp = p; return p;
}

static void clear_fanfare(void)                                                       /* stopAllMusic 00715150 */
{
    fanfare = 0; free(fan_name); fan_name = NULL; free(saved_name); saved_name = NULL; saved_pos = 0; saved_loop = 1;
}

/* the start time and the length of the fade after the playing music (StartFade 00714050 and the same lines of PlayFanfare) */
static void bar_fade(void *playing, double *when, float *len)
{
    Entry *e;
    *when = tg_real_time; *len = 0;
    if (!playing) return;
    *len = 2.0f;
    e = entry_of(tg_sound_name(playing));
    if (e && e->len >= 0) {
        double pos = eng_TGSound_GetPlayPositionSeconds(playing);
        int n = (int)(pos / e->len) + 1;
        *when += n * (double)e->len - pos;
        *len = e->len;
    }
}
static void start_fade_out(void *playing, double when, float len)
{
    pend_add(when, tg_sound_name(playing), -1.0f, 0.0f, len, 0.0f, tg_sound_loop_of(playing), 0);
    free(cur_name); cur_name = NULL; cur_slot = 0;
}

/* ---- Python API ---- */
long eng_TGMusic_LoadMusic(void *m, char *file, char *name, float len)                 /* 00713ad0 */
{
    void *s; int flags = 0; Entry *e;
    if (!file || !name) return 0;
    if (eng_TGConfigMapping_HasValue(NULL, "Sound", "StreamMusic") ? eng_TGConfigMapping_GetIntValue(NULL, "Sound", "StreamMusic") != 0 : 1) flags = 2;
    s = eng_TGSound_Create(file, name, flags);
    if (!s || !tg_sound_is_loaded(s)) return 0;
    eng_TGSound_SetGroup(s, "Music");
    tg_sound_set_group_flag(s, 0, 0, 0);
    tg_sound_set_priority(s, 1.0f);
    e = entry_of(name);
    if (e) e->len = len; else { e = calloc(1, sizeof *e); e->name = strdup(name); e->len = len; e->next = entries; entries = e; }
    return 1;
}
long eng_TGMusic_UnloadMusic(void *m, char *name)                                      /* 00713c50 */
{
    Entry **pp;
    if (!name) {
        eng_TGSoundManager_DeleteAllSoundsInGroup(NULL, "Music");
        while (entries) { Entry *e = entries; entries = e->next; free(e->name); free(e); }
        return 1;
    }
    for (pp = &entries; *pp; pp = &(*pp)->next) if (!strcmp((*pp)->name, name)) { Entry *e = *pp; *pp = e->next; free(e->name); free(e); eng_TGSoundManager_DeleteSound(NULL, name); break; }
    return 1;
}
long eng_TGMusic_StartMusic(void *m, char *name, int loop)                             /* 00713d60 */
{
    void *snd = eng_TGSoundManager_GetSound(NULL, name), *playing; double when; float len, from = 1.0f; Pend *p;
    if (!name) return 0;
    if (fanfare) {                                                                      /* after the fanfare */
        free(saved_name); saved_name = strdup(name); saved_loop = (char)loop; saved_pos = 0;
        return snd != NULL;
    }
    playing = tg_sound_playing(cur_slot);
    if (cur_name && !strcmp(cur_name, name) && playing) { tg_sound_set_loop(playing, (char)loop); return 1; }
    when = tg_real_time; len = 0;
    if (cur_name && playing) { bar_fade(playing, &when, &len); start_fade_out(playing, when, len); from = 0.0f; }
    for (p = pend; p; p = p->next) if (p->in_list) p->cancelled = 1;                    /* a start that waits for its time is dropped */
    if (snd) pend_add(when, name, from, 1.0f, len, 0.0f, (char)loop, 1);
    return snd != NULL;
}
void eng_TGMusic_StopMusic(void *m)                                                    /* 00714230 */
{
    if (fanfare) clear_fanfare();
    if (cur_name) { tg_sound_stop_slot(cur_slot); cur_slot = 0; }
}
void eng_TGMusic_PlayFanfare(void *m, char *name)                                      /* 00714260 */
{
    void *snd, *playing; double when; float len = 0;
    if (!enabled || fanfare || !name || !(snd = eng_TGSoundManager_GetSound(NULL, name))) return;
    playing = tg_sound_playing(cur_slot);
    when = tg_real_time;
    if (playing && cur_name) { bar_fade(playing, &when, &len); pend_add(when, tg_sound_name(playing), -1.0f, 0.0f, len, 0.0f, tg_sound_loop_of(playing), 0); }
    pend_add(when, name, 0.0f, 1.0f, len, 0.0f, 0, 1);
    fanfare = 1; free(fan_name); fan_name = strdup(name);
    free(saved_name); saved_name = NULL;
    if (playing && cur_name) saved_name = strdup(cur_name);
    saved_loop = 0; saved_pos = 0;
    if (playing && cur_name) { saved_pos = (float)eng_TGSound_GetPlayPositionSeconds(playing); saved_loop = (char)tg_sound_loop_of(playing); }
}
void eng_TGMusic_SetEnabled(void *m, int v)                                            /* 00714630 */
{
    if (enabled == (char)v) return;
    enabled = (char)v;
    if (!v) eng_TGMusic_StopMusic(m);
    else if (cur_name) eng_TGMusic_StartMusic(m, cur_name, 1);
}
long eng_TGMusic_IsEnabled(void *m) { return enabled; }
void eng_TGMusic_SetVolume(void *m, float v)                                           /* 00714660 */
{
    void *p;
    volume = v;
    if ((p = tg_sound_playing(cur_slot))) tg_sound_set_volume(p, volume);
}
double eng_TGMusic_GetVolume(void *m) { return volume; }
void eng_delete_TGMusic(void *m) { }

/* ---- the fade events ---- */
static void post_done_event(const char *name, void **ev)
{
    TGEvent *e = tg_ev_new("TGStringEvent", 0);
    e->type = ET_MUSIC_DONE; e->dest = music_obj; e->str = strdup(name);
    *ev = e;
}
static void handle_fade(Pend *p)                                                       /* TGMusic::HandleFadeEvent 00714940 */
{
    void *playing = tg_sound_playing_named(p->name); long slot = 0; float base = 0;
    if (!playing) {
        void *snd = eng_TGSoundManager_GetSound(NULL, p->name);
        if (snd && enabled) {
            void *ev = NULL;
            tg_sound_set_loop(snd, p->loop);
            tg_sound_set_volume(snd, p->from * volume);
            tg_sound_set_seek(snd, p->seek);
            post_done_event(tg_sound_name(snd), &ev);
            slot = tg_sound_play_notify(snd, ev, 1);
        }
        free(cur_name); cur_name = strdup(p->name); cur_slot = slot;
    } else {
        slot = tg_sound_slot_of(playing); base = tg_sound_volume_of(playing);
        tg_sound_set_loop(playing, p->loop);
    }
    if (slot) {
        Fade *f = calloc(1, sizeof *f), **pp;
        f->slot = slot; f->total = f->rem = p->dur; f->to = p->to; f->from = p->from == -1.0f ? base : p->from;
        for (pp = &fades; *pp; pp = &(*pp)->next) ;
        *pp = f;
    }
}
static void music_done(TGEvent *e)                                                     /* TGMusic::MusicDone 00715020 */
{
    if (fanfare && e->str && fan_name && !strcmp(e->str, fan_name)) {
        if (saved_name) {                                                               /* the music comes back where it was, at full volume at once */
            Pend back; memset(&back, 0, sizeof back);
            back.name = saved_name; back.from = 1.0f; back.to = 1.0f; back.dur = 0.0f; back.seek = saved_pos; back.loop = saved_loop;
            handle_fade(&back);
        }
        clear_fanfare();
    }
}

/* once a frame: the timers of the fade events, then TGMusic::Update 00714690 */
void tg_music_update(void)
{
    Pend **pp; Fade **fp; double dt;
    if (!music_obj) {
        const struct appc_global *g;
        for (g = appc_globals; g->name; g++) if (!strcmp(g->name, "g_kMusicManager")) music_obj = eng_global_get(g);
        if (music_obj) tg_register_native(music_obj, ET_MUSIC_DONE, music_done);
    }
    for (pp = &pend; *pp; ) {                                                           /* a timer fires when its time is reached */
        Pend *p = *pp;
        if (p->fire <= tg_real_time) { *pp = p->next; if (!p->cancelled) handle_fade(p); free(p->name); free(p); }
        else pp = &p->next;
    }
    dt = last_update < 0 ? 0 : tg_real_time - last_update; last_update = tg_real_time;
    for (fp = &fades; *fp; ) {
        Fade *f = *fp; void *s;
        f->rem -= (float)dt; if (f->rem < 0) f->rem = 0;
        if ((s = tg_sound_playing(f->slot))) {
            float v = f->to;
            if (f->total > 0) { float k = f->rem / f->total; v = k * f->from + (1.0f - k) * f->to; }
            tg_sound_set_volume(s, v * volume);
            if (f->rem <= 0 && f->to <= 0) eng_TGSound_Stop(s);
        }
        if (f->rem <= 0) { *fp = f->next; free(f); } else fp = &f->next;
    }
    if (!fades && cur_name && !fanfare) {                                               /* nothing fades: other music sounds are stopped */
        Entry *e;
        for (e = entries; e; e = e->next) if (strcmp(e->name, cur_name)) eng_TGSoundManager_StopSound(NULL, e->name);
    }
}
