/* Audio back end: miniaudio in place of Miles. The engine layer (tg_sound.c) keeps all the Bridge Commander sound
   rules; this layer only opens a file, plays it and moves it in space.
   One AudioVoice is one playing sample or stream (the exe's NiSource + Miles sample handle).
   Positions are listener relative: the listener sits at the origin, looks along -Z, with +Y up and +X to the right (the miniaudio
   defaults). tg_sound.c turns world positions into that frame. */
#ifndef BC_AUDIO_H
#define BC_AUDIO_H

typedef struct AudioVoice AudioVoice;

/* mode 0: no device, time is moved by audio_pump (headless runs); 1: open the default output device */
int  audio_init(int mode);
int  audio_is_open(void);
int  audio_has_device(void);
void audio_shutdown(void);
void audio_pump(double real_time);                         /* null mode: render the time since the last call */

/* a pinned file keeps its decoded data cached (call audio_pin when an original sound is loaded, audio_unpin when it goes) */
void audio_pin(const char *path);
void audio_unpin(const char *path);

/* flags: 1 = 3D (positioned), 2 = stream from disk. Returns NULL when the file cannot be opened or decoded. */
AudioVoice *audio_open(const char *path, int flags);
void   audio_close(AudioVoice *v);
double audio_length(AudioVoice *v);                        /* seconds, -1 when not known */
int    audio_start(AudioVoice *v);                         /* play from the current position */
void   audio_stop(AudioVoice *v);                          /* stop, the position stays */
void   audio_rewind(AudioVoice *v);
int    audio_is_playing(AudioVoice *v);                    /* started, not stopped, not at the end */
int    audio_at_end(AudioVoice *v);
void   audio_set_volume(AudioVoice *v, float vol);         /* 0 to 1 */
void   audio_set_pitch(AudioVoice *v, float ratio);        /* 1 = normal */
void   audio_set_loop(AudioVoice *v, int loop);
void   audio_seek(AudioVoice *v, double seconds);
double audio_tell(AudioVoice *v);
void   audio_set_position(AudioVoice *v, float x, float y, float z);
void   audio_set_distances(AudioVoice *v, float min, float max);
void   audio_set_cone(AudioVoice *v, float inner_deg, float outer_deg, float outer_gain, float dx, float dy, float dz);

#endif
