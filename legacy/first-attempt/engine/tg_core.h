/* Internal interface of the TG core (objects, events, handlers, timers) shared by the action code. */
#ifndef TG_CORE_H
#define TG_CORE_H
#include "appc_runtime.h"

struct cls { const char *name; unsigned ct; const char *base; unsigned chain[12]; };
typedef struct TGObject { const struct cls *cls; unsigned id; unsigned dead; } TGObject;

#define EVF_LOGGED 1
#define EVF_PRIVATE 2
#define EVF_NOTSAVED 4
#define ANY_TARGET ((void *)0xFFFFFFFFUL)
struct handler;
typedef struct TGEvent {
    TGObject o; TGObject *source, *dest; unsigned type; float timestamp; unsigned short flags, refcount;
    struct handler *cur; struct TGEvent *next;
    union { long i; double d; float f; char *s; void *p; } v; char *str;
    unsigned ref_id; float fval;                         /* ActionManager events: +0x28 action id, +0x2c delay */
    float mx, my; int ukey, kdata, kstate;               /* TGMouseEvent: position; TGKeyboardEvent: key, key data, state */
} TGEvent;
typedef void (*native_handler)(TGEvent *);
struct handler { int type; void *target; char *name; int is_method; void *filter; int deleted; void *owner; struct handler *next; native_handler native; };

typedef struct TGTimer { TGObject o; float next, delay, duration, start; TGEvent *event; struct TGTimer *link; } TGTimer;
typedef struct { TGTimer *head, *tail; int count; } TimerList;
typedef struct { TimerList *list; float current; } TimerMgr;

void *tg_new(const char *clsname, size_t size, long want_id);
void *tg_new_plain(const char *clsname, size_t size);    /* not a TGObject: no id */
TGObject *tg_find(unsigned id);
int tg_is_ours(void *p);
int tg_is_type(TGObject *o, unsigned ct);
int tg_class_is(const char *clsname, unsigned ct);
long tg_const(const char *name);                          /* Appc integer constant by name (0 if unknown) */
void *tg_ev_new(const char *cn, long id);
int tg_add_event(TGEvent *e);                 /* queue an event (reference counted) */
void tg_register_native(void *owner, int type, native_handler fn);
int tg_has_instance_handler(void *obj, int type);   /* 1 when obj has an instance handler for the event type */
long tg_event_process(void *obj, TGEvent *e);   /* run the instance handlers of obj for e (CallNextHandler goes on to the next one) */
TimerMgr *tg_timer_mgr(int real_time);
int tg_timer_add(TimerMgr *g, TGTimer *t);
extern double tg_game_time, tg_real_time;
void tg_actions_init(void);
void tg_mission_init(void);                    /* called by engine_init */
void tg_mission_idle(void);                    /* called by engine_tick after the events */
void *tg_global(const char *name);             /* storage of an engine global */                    /* called by engine_init */
void *tg_str_copy(void *s);
void *tg_mem_dup(const void *p, size_t n);          /* malloc copy of n bytes (NULL stays NULL); for getters whose App.py wrapper takes ownership */
void tg_str_assign(void *dst, void *src);             /* copy the text of src into the existing TGString dst */                    /* TGString copy */
int tg_py_call_module(const char *module, const char *func, char rfmt, void *out);
PyObject *tg_obj_pyobj(void *p);
void tg_register_native_broadcast(int type, native_handler fn);
#endif
