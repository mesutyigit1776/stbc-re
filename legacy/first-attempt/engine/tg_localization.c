/* Localization: .tgl files (TGLocalizationDatabase 006d11d0, TGLocalizationManager 006d03d0).
   File format (verified on all shipped files, report G topic 16): uint32 magic 0x1701, version 1, flags 0, count N; N records
   {keyOffset, valueWideIndex, valueNarrowOffset}; then three tables, each preceded by a uint32 length: keys (8-bit), values (UTF-16, length in
   characters) and narrow values (8-bit, the voice-over file name). The original sorts the keys by a 32-bit hash and searches by the hash only;
   this version compares the key strings, which gives the same result unless two keys share a hash. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tg_core.h"

typedef struct LDB { TGObject o; char *path; int n; char **keys; void **vals; char **narrow; int refs; } LDB;
extern void *tg_str_new16(const unsigned short *w, int len);
static LDB *ldb(void *p) { return tg_is_ours(p) && ((TGObject *)p)->cls && !strcmp(((TGObject *)p)->cls->name, "TGLocalizationDatabase") ? p : NULL; }
static unsigned u32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

static LDB *db_load(const char *path)
{
    extern const char *tg_asset_resolve(const char *);            /* data roots (mods) and any case */
    const char *real = tg_asset_resolve(path);
    FILE *f = fopen(real ? real : path, "rb"); unsigned char *d; long size; LDB *db; unsigned n, i, l1, l2, l3, off; const unsigned char *A, *B, *C, *D;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(size); if (fread(d, 1, size, f) != (size_t)size) { fclose(f); free(d); return NULL; } fclose(f);
    if (size < 16 || u32(d) != 0x1701 || u32(d + 4) != 1 || u32(d + 8) != 0 || u32(d + 12) == 0) { free(d); return NULL; }
    n = u32(d + 12); A = d + 16; off = 16 + 12 * n;
    if (off + 4 > size) { free(d); return NULL; }
    l1 = u32(d + off); B = d + off + 4; off += 4 + l1;
    if (off + 4 > size) { free(d); return NULL; }
    l2 = u32(d + off); C = d + off + 4; off += 4 + l2 * 2;
    if (off + 4 > size) { free(d); return NULL; }
    l3 = u32(d + off); D = d + off + 4;
    db = tg_new("TGLocalizationDatabase", sizeof(LDB), 0);
    db->path = strdup(path); db->n = (int)n; db->refs = 1;
    db->keys = calloc(n, sizeof *db->keys); db->vals = calloc(n, sizeof *db->vals); db->narrow = calloc(n, sizeof *db->narrow);
    for (i = 0; i < n; i++) {
        unsigned ko = u32(A + 12 * i), wi = u32(A + 12 * i + 4), no = u32(A + 12 * i + 8), e;
        if (ko < l1) db->keys[i] = strdup((const char *)B + ko); else db->keys[i] = strdup("");
        if (wi < l2) { const unsigned short *w = (const unsigned short *)(C + 2 * wi); unsigned short *tmp; int len = 0; e = wi;
            while (e < l2 && (C[2 * e] || C[2 * e + 1])) { e++; len++; }
            tmp = malloc((len + 1) * 2); for (e = 0; (int)e < len; e++) tmp[e] = (unsigned short)(C[2 * (wi + e)] | (C[2 * (wi + e) + 1] << 8));
            db->vals[i] = tg_str_new16(tmp, len); free(tmp); (void)w; }
        else db->vals[i] = tg_str_new16((const unsigned short *)"\0", 0);
        db->narrow[i] = no < l3 ? strdup((const char *)D + no) : strdup("");
    }
    free(d); return db;
}
static int db_index(LDB *db, const char *key) { int i; if (!db || !key) return -1; for (i = 0; i < db->n; i++) if (!strcmp(db->keys[i], key)) return i; return -1; }

/* ---- manager: a registry keyed by the file name ---- */
static LDB *reg[256]; static int nreg;
static LDB *reg_find(const char *path) { int i; for (i = 0; i < nreg; i++) if (reg[i] && !reg[i]->o.dead && !strcmp(reg[i]->path, path)) return reg[i]; return NULL; }
void *eng_TGLocalizationManager_Load(void *m, char *path, int flag)
{
    LDB *db; if (!path) return NULL;
    db = reg_find(path); if (db) { db->refs++; return db; }
    db = db_load(path); if (!db) return NULL;
    if (nreg < 256) reg[nreg++] = db;
    return db;
}
void *eng_TGLocalizationManager_GetIfRegistered(void *m, char *path, int flag) { return path ? reg_find(path) : NULL; }
void eng_TGLocalizationManager_Unload(void *m, void *dbp) { LDB *db = ldb(dbp); if (db && --db->refs <= 0) db->o.dead = 1; }
void eng_TGLocalizationManager_DeleteAll(void *m) { int i; for (i = 0; i < nreg; i++) if (reg[i]) reg[i]->o.dead = 1; nreg = 0; }
void eng_TGLocalizationManager_Purge(void *m) { }
void eng_TGLocalizationManager_RegisterDatabase(void *m, void *dbp) { LDB *db = ldb(dbp); if (db && nreg < 256 && !reg_find(db->path)) reg[nreg++] = db; }
/* ---- database ---- */
void *eng_TGLocalizationDatabase_Create(char *path, int flag) { return path ? db_load(path) : NULL; }
void *eng_TGLocalizationDatabase_GetString(void *p, char *key) { LDB *db = ldb(p); int i = db_index(db, key); return i >= 0 ? db->vals[i] : NULL; }
long eng_TGLocalizationDatabase_HasString(void *p, char *key) { return db_index(ldb(p), key) >= 0; }
const char *eng_TGLocalizationDatabase_GetFilename(void *p, char *key) { LDB *db = ldb(p); int i = db_index(db, key); return i >= 0 ? db->narrow[i] : NULL; }
void eng_TGLocalizationDatabase_WriteSample(char *path) { }

/* ScriptObject::SetDatabase 0040a250: loads the file through the manager and keeps it at +0x34; the object unloads it when it ends */
void *tg_localization_load(const char *path) { return eng_TGLocalizationManager_Load(NULL, (char *)path, 1); }
