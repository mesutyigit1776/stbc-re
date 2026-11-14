/* Bridge crew: CharacterClass (00666f10..) and CharacterAction (type 0x8018), state only. There is no model, animation or lip sync output yet.
   A character is an ObjectClass (tg_space.c) with its data in Obj.ext. Flags at +0x80 (CS_*): SetFlags/ClearFlags 0066c0e0/0066c160 treat
   0x10 (CS_HIDDEN) and 0x100 (CS_VISIBLE) as show/hide of the model (vtbl +0x50) and do not store them; setting CS_UI_DISABLED (8) puts the
   menu down. MenuUp/MenuDown 0066cdf0/0066cea0 send ET_CHARACTER_MENU (a TGBoolEvent, true = up) to the character.
   SpeakLine/SayLine 0066bfa0/0066c020 build a speak action (CharacterAction type 10/12); the completion event goes on that action.
   Approximations (marked): movement, turning, glancing and animations finish at once (no animation lengths yet); a speak action lasts as long
   as its sound (file header duration); the speak sequence has no turn and no "turn back" parts; random animations and blinking do not run;
   GetObject ignores the set argument. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

void *tg_space_new_obj(const char *cls, const char *name); void **tg_space_obj_ext(void *o);
void *tg_hooked_action_new(const char *cls, void (*on_play)(void *, void *), void *user); void tg_action_set_duration(void *p, float d);
void *tg_action_user(void *p);
extern void *eng_TGLocalizationManager_Load(void *, char *, int); extern const char *eng_TGLocalizationDatabase_GetFilename(void *, char *);
extern void *eng_TGSoundManager_GetSound(void *, char *); extern void *eng_TGSound_Create(char *, char *, int);
extern long eng_TGSound_Play(void *, int); extern double eng_TGSound_GetDurationSeconds(void *);
extern void eng_TGAction_AddCompletedEvent(void *, PyObject *); extern void eng_TGAction_Play(void *);
extern void *eng_TGParagraph_CreateW(void *, float, PyObject *, char *, float, int);
extern const char *eng_BaseObjectClass_GetName(void *);
extern long eng_TGEventManager_AddEvent(void *, PyObject *);
int tg_add_event(TGEvent *e);
extern double tg_game_time;

#define CT_CHAR 0x8016
#define CS_STANDING 1
#define CS_GLANCING 2
#define CS_TURNED 4
#define CS_UI_DISABLED 8
#define CS_HIDDEN 0x10
#define CS_INITIATIVE 0x20
#define CS_VISIBLE 0x100
#define NSTATUS 8

typedef struct { char *a, *b; } Pair;
typedef struct { char *name; float zoom; char *look; } Zoom;
typedef struct Chr {
    char *body, *head, *char_name, *yes_sir, *location, *turn_to, *blink_anim;
    unsigned flags; int size, gender, blink_stages, audio_mode, rand_enabled, menu_enabled, active, extra, animated_speaking, menu_up, hidden;
    float rand_chance, blink_chance, look_adj[3]; double last_talk;
    void *db, *menu, *anim_done_event; int anim_done_flag;
    void *status_text[NSTATUS], *status_para[NSTATUS];
    Pair *anims; int nanims; Pair *rand_anims; int nrand; Pair *faces; int nfaces; int nphonemes, phoneme_group;
    Zoom *zooms; int nzooms;
    void *cur_line; void **queue[3]; int nqueue[3];                              /* CharacterSpeakingQueue: the line being said (+0x5c) and the lists by priority (+0x14, +0x2c, +0x44) */
} Chr;

static int allow_extras = 1, tooltips = 1, collision_alert = 1, skip_all_lines; static float walk_on_chance; static float line_volume[8] = {1, 1, 1, 1, 1, 1, 1, 1};
static void *tooltip_owner; static int speaking_count;
static char *dupz(const char *s) { return s ? strdup(s) : NULL; }
static void setz(char **p, const char *s) { free(*p); *p = dupz(s); }
static Chr *ch(void *p) { void **e; if (!p || !tg_is_ours(p) || !tg_is_type(p, CT_CHAR)) return NULL; e = tg_space_obj_ext(p); return e ? *e : NULL; }
static void post_pyevent(PyObject *e) { if (e && e != Py_None) eng_TGEventManager_AddEvent(NULL, e); }

/* ---- creation and lookup ---- */
void *eng_CharacterClass_Create(char *body, char *head, int flag)                /* 00667d10: CharacterClass() + SetupModel(body, head, flag) */
{
    void *o = tg_space_new_obj("CharacterClass", NULL); Chr *c = calloc(1, sizeof *c);
    *tg_space_obj_ext(o) = c;
    c->body = dupz(body); c->head = dupz(head); c->size = 1; c->blink_chance = 0.1f; c->rand_chance = 0.5f;      /* +0x98 = 0.5, +0xb8 = 0.1 */
    c->menu_enabled = 1; c->active = 1; c->audio_mode = 2; c->flags = CS_UI_DISABLED & 0;
    return o;
}
void *eng_CharacterClass_CreateNull(void) { return NULL; }
void *eng_CharacterClass_Cast(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return ch(p) ? p : NULL; }

/* ---- class-wide settings ---- */
void eng_CharacterClass_SetAllowExtras(int v) { allow_extras = v != 0; }
void eng_CharacterClass_SetWalkOnChance(float v) { walk_on_chance = v; }
double eng_CharacterClass_GetWalkOnChance(void) { return walk_on_chance; }
long eng_CharacterClass_IsSomeoneSpeaking(void) { return speaking_count > 0; }
void eng_CharacterClass_SetToolTipsEnabled(int v) { tooltips = v != 0; }
long eng_CharacterClass_AreToolTipsEnabled(void) { return tooltips; }
void eng_CharacterClass_SetCollisionAlertEnabled(int v) { collision_alert = v != 0; }
long eng_CharacterClass_IsCollisionAlertEnabled(void) { return collision_alert; }
void eng_CharacterClass_SetVolumeForLineType(int t, float v) { if (t >= 0 && t < 8) line_volume[t] = v; }
double eng_CharacterClass_GetVolumeForLineType(int t) { return t >= 0 && t < 8 ? line_volume[t] : 1.0; }
void eng_CharacterClass_ShowPhonemeDebugging(int v) { }
void eng_CharacterClass_SetCurrentToolTipOwner(void *p) { tooltip_owner = p; }
void *eng_CharacterClass_GetCurrentToolTipOwner(void) { return tooltip_owner; }
void eng_CharacterAction_SetSkipAllLines(int v) { skip_all_lines = v != 0; }

/* ---- model and appearance ---- */
void eng_CharacterClass_ReplaceBodyAndHead(void *p, char *b, char *h) { Chr *c = ch(p); if (c) { setz(&c->body, b); setz(&c->head, h); } }
void eng_CharacterClass_MorphBody(void *p, float t, char *b) { Chr *c = ch(p); if (c) setz(&c->body, b); }
void eng_CharacterClass_AddFacialImage(void *p, char *a, char *b) { Chr *c = ch(p); if (!c) return; c->faces = realloc(c->faces, (c->nfaces + 1) * sizeof *c->faces); c->faces[c->nfaces].a = dupz(a); c->faces[c->nfaces++].b = dupz(b); }
long eng_CharacterClass_AddPhoneme(void *p, PyObject *o, int a, int b) { Chr *c = ch(p); if (!c) return 0; c->nphonemes++; return 1; }
void eng_CharacterClass_UsePhonemeGroup(void *p, int g) { Chr *c = ch(p); if (c) c->phoneme_group = g; }
void eng_CharacterClass_SetAnimatedSpeaking(void *p, int v) { Chr *c = ch(p); if (c) c->animated_speaking = v != 0; }
long eng_CharacterClass_UsesAnimatedSpeaking(void *p) { Chr *c = ch(p); return c && c->animated_speaking; }
double eng_CharacterClass_GetHeadHeight(void *p) { return 0.0; }                 /* needs the model */
void eng_CharacterClass_SetSize(void *p, int v) { Chr *c = ch(p); if (c) c->size = v; }
long eng_CharacterClass_GetSize(void *p) { Chr *c = ch(p); return c ? c->size : 0; }
void eng_CharacterClass_SetGender(void *p, int v) { Chr *c = ch(p); if (c) c->gender = v; }
long eng_CharacterClass_GetGender(void *p) { Chr *c = ch(p); return c ? c->gender : 0; }
void eng_CharacterClass_SetBlinkStages(void *p, int v) { Chr *c = ch(p); if (c) c->blink_stages = v; }
void eng_CharacterClass_SetBlinkChance(void *p, float v) { Chr *c = ch(p); if (c) c->blink_chance = v; }
double eng_CharacterClass_GetBlinkChance(void *p) { Chr *c = ch(p); return c ? c->blink_chance : 0; }
void eng_CharacterClass_SetBlinkAnimation(void *p, char *n) { Chr *c = ch(p); if (c) setz(&c->blink_anim, n); }
void eng_CharacterClass_Blink(void *p) { }
void eng_CharacterClass_SetLookAtAdj(void *p, float x, float y, float z) { Chr *c = ch(p); if (c) { c->look_adj[0] = x; c->look_adj[1] = y; c->look_adj[2] = z; } }
void eng_CharacterClass_SetYesSir(void *p, char *s) { Chr *c = ch(p); if (c) setz(&c->yes_sir, s); }
const char *eng_CharacterClass_GetYesSir(void *p) { Chr *c = ch(p); return c ? c->yes_sir : NULL; }
void eng_CharacterClass_SetName(void *p, char *s) { extern void eng_BaseObjectClass_SetName(void *, char *); eng_BaseObjectClass_SetName(p, s); }
void eng_CharacterClass_SetCharacterName(void *p, char *s) { Chr *c = ch(p); if (c) setz(&c->char_name, s); }
const char *eng_CharacterClass_GetCharacterName(void *p) { Chr *c = ch(p); return c ? c->char_name : NULL; }

/* ---- state flags ---- */
long eng_CharacterClass_MenuDown(void *p);
long eng_CharacterClass_IsStateSet(void *p, int f) { Chr *c = ch(p); if (!c) return 0; if (f == CS_HIDDEN) return c->hidden; if (f == CS_VISIBLE) return !c->hidden; return (c->flags & (unsigned)f) != 0; }
void eng_CharacterClass_SetFlags(void *p, int f)
{
    Chr *c = ch(p); if (!c || !f) return;
    if (f == CS_HIDDEN) { c->hidden = 1; return; }
    if (f == CS_VISIBLE) { c->hidden = 0; return; }
    c->flags |= (unsigned)f;
    if ((c->flags & CS_UI_DISABLED) && c->menu_up) eng_CharacterClass_MenuDown(p);
}
void eng_CharacterClass_ClearFlags(void *p, int f)
{
    Chr *c = ch(p); if (!c || !f) return;
    if (f == CS_HIDDEN) { c->hidden = 0; return; }
    if (f == CS_VISIBLE) { c->hidden = 1; return; }
    c->flags &= ~(unsigned)f;
}
void eng_CharacterClass_SetStanding(void *p, int v) { if (v) eng_CharacterClass_SetFlags(p, CS_STANDING); else eng_CharacterClass_ClearFlags(p, CS_STANDING); }
long eng_CharacterClass_IsStanding(void *p) { return eng_CharacterClass_IsStateSet(p, CS_STANDING); }
long eng_CharacterClass_IsTurned(void *p) { return eng_CharacterClass_IsStateSet(p, CS_TURNED); }
long eng_CharacterClass_IsUIDisabled(void *p) { return eng_CharacterClass_IsStateSet(p, CS_UI_DISABLED); }
long eng_CharacterClass_IsGlancing(void *p) { return eng_CharacterClass_IsStateSet(p, CS_GLANCING); }
void eng_CharacterClass_SetInitiative(void *p, int v) { if (v) eng_CharacterClass_SetFlags(p, CS_INITIATIVE); else eng_CharacterClass_ClearFlags(p, CS_INITIATIVE); }
long eng_CharacterClass_IsInitiativeOn(void *p) { return eng_CharacterClass_IsStateSet(p, CS_INITIATIVE); }
void eng_CharacterClass_SetActive(void *p, int v) { Chr *c = ch(p); if (c) c->active = v != 0; }
long eng_CharacterClass_IsActive(void *p) { Chr *c = ch(p); return c && c->active; }
void eng_CharacterClass_SetAsExtra(void *p, int v) { Chr *c = ch(p); if (c) c->extra = v != 0; }
long eng_CharacterClass_IsAnExtra(void *p) { Chr *c = ch(p); return c && c->extra; }
void eng_CharacterClass_SetAudioMode(void *p, int v) { Chr *c = ch(p); if (c) c->audio_mode = v; }
long eng_CharacterClass_GetAudioMode(void *p) { Chr *c = ch(p); return c ? c->audio_mode : 0; }
/* CharacterSpeakingQueue::IsSpeaking 00672510: the line being said still exists as an action; IsReadyToSpeak 00672330: nothing is being said and a line waits */
extern int tg_action_exists(void *p);
long eng_CharacterClass_IsSpeaking(void *p) { Chr *c = ch(p); return c && c->cur_line && tg_action_exists(c->cur_line); }
static int queue_waiting(Chr *c) { return c->nqueue[0] || c->nqueue[1] || c->nqueue[2]; }
long eng_CharacterClass_IsReadyToSpeak(void *p) { Chr *c = ch(p); return c && !c->cur_line && queue_waiting(c); }
double eng_CharacterClass_GetLastTalkTime(void *p) { Chr *c = ch(p); return c && c->last_talk > 0 ? c->last_talk : 0; }

/* ---- animations (names only; no animation runs yet) ---- */
static void pair_add(Pair **v, int *n, const char *a, const char *b) { *v = realloc(*v, (*n + 1) * sizeof **v); (*v)[*n].a = dupz(a); (*v)[(*n)++].b = dupz(b); }
void eng_CharacterClass_AddAnimation(void *p, char *name, char *fn) { Chr *c = ch(p); if (c) pair_add(&c->anims, &c->nanims, name, fn); }
void eng_CharacterClass_ClearAnimations(void *p) { Chr *c = ch(p); if (c) { c->nanims = 0; c->nrand = 0; } }
void eng_CharacterClass_ClearExtraAnimations(void *p) { }
void eng_CharacterClass_AddRandomAnimation(void *p, char *fn, int type, int weight, int flag) { Chr *c = ch(p); if (c) pair_add(&c->rand_anims, &c->nrand, fn, NULL); }
void eng_CharacterClass_SetRandomAnimationEnabled(void *p, int v) { Chr *c = ch(p); if (c) c->rand_enabled = v != 0; }
long eng_CharacterClass_IsRandomAnimationEnabled(void *p) { Chr *c = ch(p); return c && c->rand_enabled; }
void eng_CharacterClass_SetRandomAnimationChance(void *p, float v) { Chr *c = ch(p); if (c) c->rand_chance = v; }
double eng_CharacterClass_GetRandomAnimationChance(void *p) { Chr *c = ch(p); return c ? c->rand_chance : 0; }
void eng_CharacterClass_ClearAnimationsOfType(void *p, int t) { }
void eng_CharacterClass_SetAnimationDoneEvent(void *p, void *e, int f) { Chr *c = ch(p); if (c) { c->anim_done_event = e; c->anim_done_flag = f; } }
void eng_CharacterClass_SetCurrentAnimation(void *p, void *a, int t, int f, char *n) { }
long eng_CharacterClass_IsAnimating(void *p) { return 0; }
long eng_CharacterClass_IsGoingToAnimate(void *p) { return 0; }
long eng_CharacterClass_IsAnimatingInterruptable(void *p) { return 0; }
long eng_CharacterClass_IsAnimatingNonInterruptable(void *p) { return 0; }

/* ---- location and movement: the state changes at once, the event is sent at once ---- */
long eng_CharacterClass_SetLocation(void *p, char *loc) { Chr *c = ch(p); if (!c) return 0; setz(&c->location, loc); c->flags &= ~(unsigned)(CS_TURNED | CS_GLANCING); return 1; }
void eng_CharacterClass_SetLocationName(void *p, char *loc) { Chr *c = ch(p); if (c) setz(&c->location, loc); }
const char *eng_CharacterClass_GetLocation(void *p) { Chr *c = ch(p); return c ? c->location : NULL; }
const char *eng_CharacterClass_GetTurnTowardsLocation(void *p) { Chr *c = ch(p); return c ? c->turn_to : NULL; }
long eng_CharacterClass_MoveTo(void *p, char *loc, PyObject *e) { long r = eng_CharacterClass_SetLocation(p, loc); post_pyevent(e); return r; }
long eng_CharacterClass_GlanceAt(void *p, char *loc, PyObject *e) { Chr *c = ch(p); if (!c) return 0; c->flags |= CS_GLANCING; setz(&c->turn_to, loc); post_pyevent(e); return 1; }
long eng_CharacterClass_GlanceAway(void *p, PyObject *e) { Chr *c = ch(p); if (!c) return 0; c->flags &= ~(unsigned)CS_GLANCING; post_pyevent(e); return 1; }
long eng_CharacterClass_TurnTowards(void *p, char *loc, PyObject *e) { Chr *c = ch(p); if (!c) return 0; c->flags |= CS_TURNED; setz(&c->turn_to, loc); post_pyevent(e); return 1; }
long eng_CharacterClass_TurnBack(void *p, PyObject *e) { Chr *c = ch(p); if (!c) return 0; c->flags &= ~(unsigned)CS_TURNED; setz(&c->turn_to, NULL); post_pyevent(e); return 1; }
long eng_CharacterClass_PlayAnimation(void *p, char *n, int f, PyObject *e) { if (!ch(p)) return 0; post_pyevent(e); return 1; }
long eng_CharacterClass_PlayAnimationFile(void *p, char *n, int f, PyObject *e) { if (!ch(p)) return 0; post_pyevent(e); return 1; }
long eng_CharacterClass_Breathe(void *p, PyObject *e) { if (!ch(p)) return 0; post_pyevent(e); return 1; }
void eng_CharacterClass_LookAtMe(void *p) { }
void eng_CharacterClass_AddPositionZoom(void *p, char *n, float z, char *look)
{ Chr *c = ch(p); if (!c) return; c->zooms = realloc(c->zooms, (c->nzooms + 1) * sizeof *c->zooms); c->zooms[c->nzooms].name = dupz(n); c->zooms[c->nzooms].zoom = z; c->zooms[c->nzooms++].look = dupz(look); }
static Zoom *zoom_find(Chr *c, const char *n) { int i; if (c && n) for (i = 0; i < c->nzooms; i++) if (!strcmp(c->zooms[i].name, n)) return &c->zooms[i]; return NULL; }
double eng_CharacterClass_GetPositionZoom(void *p, char *n) { Zoom *z = zoom_find(ch(p), n); return z ? z->zoom : 1.0; }
const char *eng_CharacterClass_GetPositionLookAtName(void *p, char *n) { Zoom *z = zoom_find(ch(p), n); return z ? z->look : NULL; }

/* ---- database, menu, status ---- */
void *eng_CharacterClass_SetDatabase(void *p, char *path) { Chr *c = ch(p); if (!c) return NULL; c->db = path ? eng_TGLocalizationManager_Load(NULL, path, 1) : NULL; return c->db; }   /* 0066dc10 */
void *eng_CharacterClass_GetDatabase(void *p) { Chr *c = ch(p); return c ? c->db : NULL; }
void eng_CharacterClass_SetMenu(void *p, void *m) { Chr *c = ch(p); if (c) c->menu = m; }
void *eng_CharacterClass_GetMenu(void *p) { Chr *c = ch(p); return c ? c->menu : NULL; }
void eng_CharacterClass_SetMenuEnabled(void *p, int v) { Chr *c = ch(p); if (!c) return; c->menu_enabled = v != 0; if (!v && c->menu_up) eng_CharacterClass_MenuDown(p); }
long eng_CharacterClass_IsMenuEnabled(void *p) { Chr *c = ch(p); return c && c->menu_enabled; }
extern void *eng_STTopLevelMenu_GetOpenMenu(void);
long eng_CharacterClass_IsMenuUp(void *p) { Chr *c = ch(p); return c && c->menu && eng_STTopLevelMenu_GetOpenMenu() == c->menu; }
static int menu_is(void *ext, void *m) { return ext && ((Chr *)ext)->menu == m; }
void *eng_CharacterClass_GetCharacterFromMenu(void *m)                          /* 0066a720: the character whose menu this is */
{ void *tg_space_find_by_ext(unsigned ct, int (*match)(void *ext, void *arg), void *arg); return tg_space_find_by_ext(CT_CHAR, menu_is, m); }
static void menu_event(void *p, int up)                                         /* ET_CHARACTER_MENU, a TGBoolEvent to the character */
{
    TGEvent *e = tg_ev_new("TGBoolEvent", 0); e->type = 8388684u; e->dest = p; e->v.i = up != 0; tg_add_event(e);     /* ET_CHARACTER_MENU */
}
extern void eng_STTopLevelMenu_SetOwner(void *, void *); extern void eng_STTopLevelMenu_SetVisible(void *, int); extern void eng_STTopLevelMenu_SetNotVisible(void *, int);
static void menu_show(void *p, Chr *c, int up)                                  /* 0066d450: show or hide the top-level menu */
{
    if (up) { eng_STTopLevelMenu_SetOwner(c->menu, p); eng_STTopLevelMenu_SetVisible(c->menu, 1); }
    else eng_STTopLevelMenu_SetNotVisible(c->menu, 1);
}
long eng_CharacterClass_MenuUp(void *p)
{ Chr *c = ch(p); if (!c || !c->menu || (c->flags & CS_UI_DISABLED) || !c->menu_enabled) return 0; c->menu_up = 1; menu_show(p, c, 1); menu_event(p, 1); return 1; }
long eng_CharacterClass_MenuDown(void *p) { Chr *c = ch(p); if (!c || !c->menu) return 0; c->menu_up = 0; menu_show(p, c, 0); menu_event(p, 0); return 1; }
PyObject *eng_CharacterClass_SetStatus(PyObject *args)
{
    PyObject *self, *so; int slot = 0; void *p = NULL, *s = NULL; Chr *c;
    if (!PyArg_ParseTuple(args, "OO|i:CharacterClass_SetStatus", &self, &so, &slot)) return NULL;
    if (!appc_to_ptr(self, &p, "CharacterClass") || !appc_to_ptr(so, &s, "TGString")) return NULL;
    c = ch(p);
    if (c && slot >= 0 && slot < NSTATUS) { extern void eng_delete_TGString(void *); if (c->status_text[slot]) eng_delete_TGString(c->status_text[slot]); c->status_text[slot] = s ? tg_str_copy(s) : NULL; c->status_para[slot] = NULL; }
    Py_INCREF(Py_None); return Py_None;
}
void eng_CharacterClass_ClearStatus(void *p, int slot) { Chr *c = ch(p); if (c && slot >= 0 && slot < NSTATUS) { c->status_text[slot] = NULL; c->status_para[slot] = NULL; } }
void *eng_CharacterClass_GetStatus(void *p, int slot)
{
    Chr *c = ch(p); if (!c || slot < 0 || slot >= NSTATUS || !c->status_text[slot]) return NULL;
    if (!c->status_para[slot]) c->status_para[slot] = eng_TGParagraph_CreateW(c->status_text[slot], 0, NULL, NULL, 0, 0);
    return c->status_para[slot];
}
void *eng_CharacterClass_GetStatusBox(void *p) { return NULL; }
void eng_CharacterClass_ToolTip(void *p) { }

/* ---- speaking: CharacterAction ---- */
typedef struct { void *chr; int type; char *detail, *detail2; int flag; void *db; int a, b; void *sound, *voice; } CAct;
#define AT_SET_LOCATION 0
#define AT_SET_LOCATION_NAME 1
#define AT_MOVE 2
#define AT_TURN 3
#define AT_TURN_NOW 4
#define AT_TURN_BACK 5
#define AT_TURN_BACK_NOW 6
#define AT_SPEAK_LINE 10
#define AT_SPEAK_LINE_NO_FLAP_LIPS 11
#define AT_SAY_LINE 12
#define AT_SAY_LINE_AFTER_TURN 13
#define AT_MENU_UP 20
#define AT_MENU_DOWN 21
#define AT_SET_AUDIO_MODE 22
#define AT_ENABLE_RANDOM_ANIMATIONS 23
#define AT_DISABLE_RANDOM_ANIMATIONS 24
#define AT_GLANCE_AT 25
#define AT_GLANCE_AWAY 26
#define AT_BECOME_ACTIVE 27
#define AT_BECOME_INACTIVE 28
#define AT_ENABLE_MENU 29
#define AT_DISABLE_MENU 30
#define AT_ENABLE_INITIATIVE 31
#define AT_DISABLE_INITIATIVE 32
static void *line_sound(CAct *a)                                                 /* the line's sound: the database gives the file name */
{
    const char *f; void *s;
    if (!a->detail) return NULL;
    if ((s = eng_TGSoundManager_GetSound(NULL, a->detail))) return s;
    f = a->db ? eng_TGLocalizationDatabase_GetFilename(a->db, a->detail) : NULL;
    {   extern long eng_TGConfigMapping_HasValue(void *, char *, char *), eng_TGConfigMapping_GetIntValue(void *, char *, char *);
        int stream = eng_TGConfigMapping_HasValue(NULL, "Sound", "StreamVoices") && eng_TGConfigMapping_GetIntValue(NULL, "Sound", "StreamVoices") != 0;   /* options.cfg StreamVoices (default 0) */
        return f ? eng_TGSound_Create((char *)f, a->detail, stream ? 2 : 0) : NULL; }
}
/* ---- the speaking queue: CharacterClass::AddSoundToQueue 0066cb90, CharacterSpeakingQueue::AddSoundToQueue 00672530, PopNextSound 00672270, PlayNextSound 00672360,
   SoundDoneHandler 00672720, CharacterClass::Update 00669680. A spoken line (SpeakLine, type 10) is queued by its priority (CSP_MISSION_CRITICAL 0, CSP_NORMAL 1,
   CSP_SPONTANEOUS 2) and the character says one line at a time. A spontaneous line is dropped when the character is busy; every new line clears the spontaneous
   lines that still wait. A dropped line is completed (the exe skips it; its character action would stay open): approximation. ---- */
extern void tg_action_finish(void *), tg_action_play_now(void *), tg_action_skip_now(void *), tg_action_add_event(void *, void *); extern void *tg_action_completion_event(void *);
extern double tg_real_time; extern void *eng_Game_GetCurrentPlayer(void); extern void tg_space_each(unsigned ct, void (*fn)(void *));
static void queue_next(void *chr);
static void on_sound_done(TGEvent *e)                                            /* SoundDoneHandler: the line is over, the next one may start */
{
    Chr *c = e->dest ? ch(e->dest) : NULL;
    if (!c) return;
    c->cur_line = NULL; if (speaking_count > 0) speaking_count--;
    queue_next(e->dest);
}
static void *queue_pop(Chr *c)                                                   /* the oldest line of the first list that has one; lines whose action is gone are skipped */
{
    int k;
    for (k = 0; k < 3; k++) while (c->nqueue[k]) {
        void *a = c->queue[k][0];
        memmove(c->queue[k], c->queue[k] + 1, (size_t)(--c->nqueue[k]) * sizeof(void *));
        if (tg_action_exists(a)) return a;
    }
    return NULL;
}
static void queue_next(void *chr)                                                /* PlayNextSound */
{
    Chr *c = ch(chr); void *a;
    if (!c || c->cur_line || !(a = queue_pop(c))) return;
    {   TGEvent *e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const("ET_CHARACTER_SOUND_DONE"); e->dest = chr; tg_action_add_event(a, e); }
    c->cur_line = a; speaking_count++;
    tg_action_play_now(a);
}
static void queue_add(void *chr, void *action, int priority)                     /* CharacterClass::AddSoundToQueue */
{
    static int registered; Chr *c = ch(chr); int k;
    if (!registered) { tg_register_native_broadcast((int)tg_const("ET_CHARACTER_SOUND_DONE"), on_sound_done); registered = 1; }
    if (!c || !action) return;
    if (priority == 2 && (eng_CharacterClass_IsReadyToSpeak(chr) || eng_CharacterClass_IsSpeaking(chr))) { tg_action_finish(action); return; }
    while (c->nqueue[2]) { void *old = c->queue[2][0]; memmove(c->queue[2], c->queue[2] + 1, (size_t)(--c->nqueue[2]) * sizeof(void *)); if (tg_action_exists(old)) tg_action_finish(old); }
    k = priority == 0 ? 0 : priority == 1 ? 1 : 2;
    c->queue[k] = realloc(c->queue[k], (size_t)(c->nqueue[k] + 1) * sizeof(void *)); c->queue[k][c->nqueue[k]++] = action;
}
static void character_update(void *chr)                                          /* CharacterClass::Update: when a line waits and nothing is said, the next one starts */
{
    Chr *c = ch(chr);
    if (c && !c->cur_line && queue_waiting(c)) { queue_next(chr); c->last_talk = tg_real_time; }
}
void tg_character_tick(void)
{
    void *pl = eng_Game_GetCurrentPlayer();
    if (pl) tg_space_each(CT_CHAR, character_update);                           /* the exe's Update returns early without a live player */
}
static void cact_play(void *act, void *user);
static void cact_skip(void *act, void *user)                                      /* a skipped line stops its sound (and leaves the queue) */
{ CAct *a = user; if (a->voice) tg_action_skip_now(a->voice); }
static void cact_play(void *act, void *user)
{
    CAct *a = user; Chr *c = ch(a->chr); void *p = a->chr;
    if (!c) return;
    switch (a->type) {
    case AT_SET_LOCATION: case AT_MOVE: eng_CharacterClass_SetLocation(p, a->detail); break;
    case AT_SET_LOCATION_NAME: eng_CharacterClass_SetLocationName(p, a->detail); break;
    case AT_TURN: case AT_TURN_NOW: c->flags |= CS_TURNED; setz(&c->turn_to, a->detail); break;
    case AT_TURN_BACK: case AT_TURN_BACK_NOW: c->flags &= ~(unsigned)CS_TURNED; break;
    case AT_SPEAK_LINE: case AT_SPEAK_LINE_NO_FLAP_LIPS: case AT_SAY_LINE: case AT_SAY_LINE_AFTER_TURN:
        /* CharacterAction::Execute 0066fe50: the line's sound action (flags 1 restart + 8 go on + 16 voice, volume of the line type, priority 1.0, skippable) completes
           this action when it ends; a spoken line (type 10) of a character goes through the speaking queue, the others play at once */
        if (skip_all_lines || !(a->sound = line_sound(a))) { tg_action_finish(act); break; }
        {   extern void *eng_TGSoundAction_Create(char *, int, char *, float); extern void eng_TGSoundAction_SetPriority(void *, float);
            void *sa = eng_TGSoundAction_Create(a->detail, 0x19, NULL, (a->b >= 0 && a->b < 8) ? line_volume[a->b] : 1.0f);
            eng_TGSoundAction_SetPriority(sa, 1.0f); eng_TGAction_SetSkippable(sa, 1);
            a->voice = sa;
            tg_action_add_event(sa, tg_action_completion_event(act));
            if (a->type == AT_SPEAK_LINE) queue_add(a->chr, sa, a->a); else tg_action_play_now(sa);
        }
        c->last_talk = tg_real_time; break;
    case AT_MENU_UP: eng_CharacterClass_MenuUp(p); break;
    case AT_MENU_DOWN: eng_CharacterClass_MenuDown(p); break;
    case AT_SET_AUDIO_MODE: c->audio_mode = a->a; break;
    case AT_ENABLE_RANDOM_ANIMATIONS: c->rand_enabled = 1; break;
    case AT_DISABLE_RANDOM_ANIMATIONS: c->rand_enabled = 0; break;
    case AT_GLANCE_AT: c->flags |= CS_GLANCING; setz(&c->turn_to, a->detail); break;
    case AT_GLANCE_AWAY: c->flags &= ~(unsigned)CS_GLANCING; break;
    case AT_BECOME_ACTIVE: c->active = 1; break;
    case AT_BECOME_INACTIVE: c->active = 0; break;
    case AT_ENABLE_MENU: eng_CharacterClass_SetMenuEnabled(p, 1); break;
    case AT_DISABLE_MENU: eng_CharacterClass_SetMenuEnabled(p, 0); break;
    case AT_ENABLE_INITIATIVE: eng_CharacterClass_SetInitiative(p, 1); break;
    case AT_DISABLE_INITIATIVE: eng_CharacterClass_SetInitiative(p, 0); break;
    default: break;                                                              /* animations, breathe, look at me, status: no visible effect yet */
    }
}
void *eng_CharacterAction_Create(void *chr, int type, char *detail, char *detail2, int flag, void *db, int a, int b)
{
    CAct *x = calloc(1, sizeof *x);
    x->chr = chr; x->type = type; x->detail = dupz(detail); x->detail2 = dupz(detail2); x->flag = flag; x->db = db; x->a = a; x->b = b;
    if (type >= AT_SPEAK_LINE && type <= AT_SAY_LINE_AFTER_TURN) { extern void *tg_open_action_new(const char *, void (*)(void *, void *), void (*)(void *, void *), void *); return tg_open_action_new("CharacterAction", cact_play, cact_skip, x); }
    return tg_hooked_action_new("CharacterAction", cact_play, x);
}
void *eng_CharacterAction_CreateByName(char *chr, char *set, int type, char *detail, char *detail2, int flag, void *db, int a, int b)
{
    extern void *eng_CharacterClass_GetObject(void *, char *); void *x = eng_CharacterAction_Create(eng_CharacterClass_GetObject(NULL, chr), type, detail, detail2, flag, db, a, b);
    return x;
}
void *eng_CharacterAction_Cast(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p && tg_is_ours(p) && tg_is_type(p, 0x8018) ? p : NULL; }
void eng_CharacterAction_ConvertFolderLWVs(char *f) { }
long eng_CharacterAction_GetActionType(void *p) { CAct *x = tg_action_user(p); return x ? x->type : 0; }
const char *eng_CharacterAction_GetDetail(void *p) { CAct *x = tg_action_user(p); return x ? x->detail : NULL; }
void eng_CharacterAction_SetPriority(void *p, int v) { }
void eng_CharacterAction_SetSubPriority(void *p, int v) { }
void eng_CharacterAction_UseNameAndSetInsteadOfObject(void *p, char *name, char *set)
{ extern void *eng_CharacterClass_GetObject(void *, char *); CAct *x = tg_action_user(p); if (x) x->chr = eng_CharacterClass_GetObject(NULL, name); }
static long speak(void *p, char *line, char *turn_to, int type, void *db, int prio, int ltype, PyObject *ev)  /* BuildSpeakSequence 0066ca00 (speak part only) */
{
    void *a; if (!ch(p)) return 0;
    a = eng_CharacterAction_Create(p, type, line, turn_to, 0, db, prio, ltype);
    if (ev && ev != Py_None) eng_TGAction_AddCompletedEvent(a, ev);
    eng_TGAction_Play(a); return 1;
}
long eng_CharacterClass_SpeakLine(void *p, void *db, char *line, int a, int b, PyObject *ev) { return speak(p, line, NULL, AT_SPEAK_LINE, db, a, b, ev); }
long eng_CharacterClass_SayLine(void *p, void *db, char *line, char *turn, int a, int b, int c, int d, PyObject *ev) { return speak(p, line, turn, AT_SAY_LINE, db, b, c, ev); }   /* a: the turn back flag; SayLine plays at once, it does not use the queue */
PyObject *eng_CharacterClass_AddSoundToQueue(PyObject *args) { Py_INCREF(Py_None); return Py_None; }
