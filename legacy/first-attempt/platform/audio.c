/* Audio back end on miniaudio (see audio.h). WAV and MP3 are the only formats the game uses.
   Spatial model: Miles sample distances (AIL_set_3D_sample_distances) are a minimum (full volume inside it) and a maximum (no more
   attenuation beyond it); miniaudio's inverse model with min and max distance is the same curve. Doppler is off: the exe passes no
   velocity to Miles that the decompile shows being set. */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MA_NO_NODE_GRAPH_STATS
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

struct AudioVoice { ma_sound s; int flags; int started; };
static ma_engine eng; static int up, has_dev; static double last_time = -1; static float *scratch; static ma_uint32 scratch_frames;

/* BC_AUDIO_DUMP=file.wav: without a device the mix is kept and written as a 16 bit stereo WAV at exit (to check a run without listening) */
static short *dump; static size_t dump_frames, dump_cap; static char *dump_path; static unsigned dump_rate = 48000;
static void dump_write(void)
{
    FILE *f; unsigned rate, bytes; unsigned char h[44]; size_t i;
    if (!dump_path || !dump_frames) return;
    if (getenv("BC_SND_TRACE")) fprintf(stderr, "[audio] dump %zu frames, %.2f s\n", dump_frames, (double)dump_frames / dump_rate);
    f = fopen(dump_path, "wb"); if (!f) return;
    rate = dump_rate; bytes = (unsigned)(dump_frames * 4);
    memcpy(h, "RIFF", 4); { unsigned v = 36 + bytes; memcpy(h + 4, &v, 4); } memcpy(h + 8, "WAVEfmt ", 8);
    { unsigned v = 16; memcpy(h + 16, &v, 4); } { unsigned short v = 1; memcpy(h + 20, &v, 2); v = 2; memcpy(h + 22, &v, 2); }
    memcpy(h + 24, &rate, 4); { unsigned v = rate * 4; memcpy(h + 28, &v, 4); } { unsigned short v = 4; memcpy(h + 32, &v, 2); v = 16; memcpy(h + 34, &v, 2); }
    memcpy(h + 36, "data", 4); memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f); fwrite(dump, 4, dump_frames, f); fclose(f); (void)i;
}

static void audio_shutdown_atexit(void);
int audio_init(int mode)
{
    ma_engine_config cfg;
    if (up) return 1;
    cfg = ma_engine_config_init();
    cfg.channels = 2; cfg.sampleRate = 48000;
    cfg.noDevice = mode ? MA_FALSE : MA_TRUE;
    cfg.listenerCount = 1;
    if (ma_engine_init(&cfg, &eng) != MA_SUCCESS) {
        if (!mode) return 0;
        cfg.noDevice = MA_TRUE;                              /* no output device: run silent but keep the timing */
        if (ma_engine_init(&cfg, &eng) != MA_SUCCESS) return 0;
        mode = 0;
    }
    has_dev = mode != 0; up = 1; last_time = -1; dump_rate = ma_engine_get_sample_rate(&eng);
    { static int registered; if (!registered) { registered = 1; atexit(audio_shutdown_atexit); } }   /* the output device is closed before the process ends */
    if (getenv("BC_SND_TRACE")) fprintf(stderr, "[audio] %s, %u Hz\n", has_dev ? "output device" : "no device (silent clock)", ma_engine_get_sample_rate(&eng));
    if (!has_dev && getenv("BC_AUDIO_DUMP") && !dump_path) { dump_path = strdup(getenv("BC_AUDIO_DUMP")); atexit(dump_write); }
    ma_engine_listener_set_position(&eng, 0, 0, 0, 0);
    ma_engine_listener_set_direction(&eng, 0, 0, 0, -1);
    ma_engine_listener_set_world_up(&eng, 0, 0, 1, 0);
    return 1;
}
int audio_is_open(void) { return up; }
int audio_has_device(void) { return has_dev; }
static void audio_shutdown_atexit(void) { audio_shutdown(); }
void audio_shutdown(void) { if (up) { ma_engine_uninit(&eng); up = 0; has_dev = 0; } free(scratch); scratch = NULL; scratch_frames = 0; }

void audio_pump(double t)
{
    double dt; ma_uint32 want;
    if (!up || has_dev) return;
    if (last_time < 0) { last_time = t; return; }
    dt = t - last_time; last_time = t;
    if (dt <= 0) return;
    if (dt > 2.0) dt = 2.0;
    want = (ma_uint32)(dt * ma_engine_get_sample_rate(&eng));
    if (!scratch) { scratch_frames = 4096; scratch = malloc(scratch_frames * 2 * sizeof *scratch); }
    while (want) {
        ma_uint32 n = want < scratch_frames ? want : scratch_frames; ma_uint64 got = 0;
        { ma_result rr = ma_engine_read_pcm_frames(&eng, scratch, n, &got); if (rr != MA_SUCCESS) break; if (!got) { got = n; memset(scratch, 0, (size_t)n * 2 * sizeof *scratch); } }   /* nothing playing: the engine returns no frames, the time still passes */
        if (dump_path) {
            size_t k;
            if (dump_frames + got > dump_cap) { dump_cap = (dump_frames + got) * 2 + 65536; dump = realloc(dump, dump_cap * 4); }
            for (k = 0; k < got * 2; k++) { float v = scratch[k]; v = v > 1 ? 1 : v < -1 ? -1 : v; dump[dump_frames * 2 + k] = (short)(v * 32767.0f); }
            dump_frames += got;
        }
        want -= (ma_uint32)got;
    }
}

/* an MP3 (the voice lines, 30 to 100 seconds) is always streamed: decoding it whole on the game thread would stall the frame */
static int is_mp3(const char *path) { size_t n = strlen(path); return n > 4 && (!strcmp(path + n - 4, ".mp3") || !strcmp(path + n - 4, ".MP3")); }

/* pinned files: the decoded data stays cached while an original sound exists (the exe keeps the file entry, findOrLoadSoundFileEntry) */
static struct pin { char *path; int refs; } *pins; static int npins;
void audio_pin(const char *path)
{
    int i;
    if (!up || !path || is_mp3(path)) return;
    for (i = 0; i < npins; i++) if (!strcmp(pins[i].path, path)) { pins[i].refs++; return; }
    if (ma_resource_manager_register_file(ma_engine_get_resource_manager(&eng), path, MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE) != MA_SUCCESS) return;
    pins = realloc(pins, (npins + 1) * sizeof *pins); pins[npins].path = strdup(path); pins[npins].refs = 1; npins++;
}
void audio_unpin(const char *path)
{
    int i;
    if (!up || !path || is_mp3(path)) return;
    for (i = 0; i < npins; i++) if (!strcmp(pins[i].path, path)) {
        if (--pins[i].refs > 0) return;
        ma_resource_manager_unregister_file(ma_engine_get_resource_manager(&eng), path);
        free(pins[i].path); pins[i] = pins[--npins]; return;
    }
}

AudioVoice *audio_open(const char *path, int flags)
{
    AudioVoice *v; ma_uint32 f = 0;
    if (!up || !path) return NULL;
    v = calloc(1, sizeof *v);
    if (is_mp3(path)) flags |= 2;
    f |= (flags & 2) ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!(flags & 1)) f |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&eng, path, f, NULL, NULL, &v->s) != MA_SUCCESS) { free(v); return NULL; }
    v->flags = flags;
    if (flags & 1) {
        ma_sound_set_attenuation_model(&v->s, ma_attenuation_model_inverse);
        ma_sound_set_rolloff(&v->s, 1.0f);
        ma_sound_set_doppler_factor(&v->s, 0.0f);
        ma_sound_set_min_distance(&v->s, 50.0f); ma_sound_set_max_distance(&v->s, 700.0f);
        ma_sound_set_positioning(&v->s, ma_positioning_absolute);
    }
    return v;
}
void audio_close(AudioVoice *v) { if (!v) return; ma_sound_uninit(&v->s); free(v); }
double audio_length(AudioVoice *v)
{
    float len = 0;
    if (!v || ma_sound_get_length_in_seconds(&v->s, &len) != MA_SUCCESS) return -1;
    return len;
}
int audio_start(AudioVoice *v) { if (!v) return 0; v->started = 1; return ma_sound_start(&v->s) == MA_SUCCESS; }
void audio_stop(AudioVoice *v) { if (v) ma_sound_stop(&v->s); }
void audio_rewind(AudioVoice *v) { if (v) ma_sound_seek_to_pcm_frame(&v->s, 0); }
int audio_is_playing(AudioVoice *v) { return v && ma_sound_is_playing(&v->s) && !ma_sound_at_end(&v->s); }
int audio_at_end(AudioVoice *v) { return v && ma_sound_at_end(&v->s); }
void audio_set_volume(AudioVoice *v, float vol) { if (v) ma_sound_set_volume(&v->s, vol < 0 ? 0 : vol); }
void audio_set_pitch(AudioVoice *v, float r) { if (v && r > 0) ma_sound_set_pitch(&v->s, r); }
void audio_set_loop(AudioVoice *v, int loop) { if (v) ma_sound_set_looping(&v->s, loop ? MA_TRUE : MA_FALSE); }
void audio_seek(AudioVoice *v, double sec)
{
    if (!v) return;
    if (sec < 0) sec = 0;
    ma_sound_seek_to_pcm_frame(&v->s, (ma_uint64)(sec * ma_engine_get_sample_rate(&eng)));
}
double audio_tell(AudioVoice *v)
{
    float c = 0;
    if (!v || ma_sound_get_cursor_in_seconds(&v->s, &c) != MA_SUCCESS) return 0;
    return c;
}
void audio_set_position(AudioVoice *v, float x, float y, float z) { if (v && (v->flags & 1)) ma_sound_set_position(&v->s, x, y, z); }
void audio_set_distances(AudioVoice *v, float mn, float mx)
{
    if (!v || !(v->flags & 1)) return;
    if (mn <= 0.0f) mn = 1.0f;                                 /* a minimum of 0 (the engine hum): Miles and DirectSound3D keep their default of 1 */
    ma_sound_set_min_distance(&v->s, mn); ma_sound_set_max_distance(&v->s, mx > mn ? mx : mn);
}
void audio_set_cone(AudioVoice *v, float in_deg, float out_deg, float out_gain, float dx, float dy, float dz)
{
    const float rad = 3.14159265f / 180.0f;
    if (!v || !(v->flags & 1)) return;
    ma_sound_set_cone(&v->s, in_deg * rad, out_deg * rad, out_gain);
    ma_sound_set_direction(&v->s, dx, dy, dz);
}
