/* Minimal engine behavior needed so that the stock and mod scripts can be imported (run their module-level code).
   Objects are opaque placeholder blocks: every method called on them reaches a weak Appc stub (returns 0/None) until the real class exists.
   The one piece of real logic is the event type allocator (UtopiaModule::GetNextEventType 00443440). */
#include <stdlib.h>
#include <string.h>
#include "appc_runtime.h"

#define PLACEHOLDER_SIZE 512
static void *new_placeholder(void) { return calloc(1, PLACEHOLDER_SIZE); }

/* named objects are created once per (kind, name) so that two lookups of the same group return the same object */
struct named { const char *kind; char *name; void *obj; };
static struct named table[4096]; static int ntable;
static void *named_object(const char *kind, const char *name)
{
    int i;
    for (i = 0; i < ntable; i++) if (!strcmp(table[i].kind, kind) && !strcmp(table[i].name, name ? name : "")) return table[i].obj;
    if (ntable >= 4096) return new_placeholder();
    table[ntable].kind = kind; table[ntable].name = strdup(name ? name : ""); table[ntable].obj = new_placeholder();
    return table[ntable++].obj;
}

void *eng_new_CPyDebug(char *name) { return new_placeholder(); }

/* UtopiaModule::GetNextEventType 00443440 (counter DAT_0097fb38 starts at 0) */
static unsigned counter;
long eng_UtopiaModule_GetNextEventType(void)
{
    int v = (int)(counter + 0x80110d);
    counter = (counter + 1) & 0x80003fffu;
    if ((int)counter < 0) counter = ((counter - 1) | 0xffffc000u) + 1;
    return v;
}

/* Model axes, from the startup stores at 0x45e462 (globals 0x980df0.. read by TGPoint3::GetModelForward 00434cd0 and its siblings):
   forward (0,1,0) @980df0, up (0,0,1) @980e30, right (1,0,0) @980e50, left (-1,0,0) @980e70, down (0,0,-1) @980ee8, backward (0,-1,0) @980f20 */
static void *axis(float x, float y, float z) { float *p = malloc(12); if (p) { p[0] = x; p[1] = y; p[2] = z; } return p; }
void *eng_TGPoint3_GetModelForward(void)  { return axis(0.0f,  1.0f,  0.0f); }
void *eng_TGPoint3_GetModelBackward(void) { return axis(0.0f, -1.0f,  0.0f); }
void *eng_TGPoint3_GetModelUp(void)       { return axis(0.0f,  0.0f,  1.0f); }
void *eng_TGPoint3_GetModelDown(void)     { return axis(0.0f,  0.0f, -1.0f); }
void *eng_TGPoint3_GetModelRight(void)    { return axis(1.0f,  0.0f,  0.0f); }
void *eng_TGPoint3_GetModelLeft(void)     { return axis(-1.0f, 0.0f,  0.0f); }

/* py_VarManagerClass_MakeEpisodeEventType: PyInt_FromLong(0x80c001 + arg), no engine call */

/* TGSystemWrapperClass::GetRandomNumber(n): the original generator is not decoded yet; this is a placeholder with the same range assumption [0, n) */
long eng_TGSystemWrapperClass_GetRandomNumber(void *m, int n) { return n > 0 ? random() % n : 0; }


/* Sounds: until the audio layer exists a loaded sound is a named object that accepts every call (weak stubs). Loading the same name twice
   returns the same object (the original keeps one sound per name in the sound manager). */
