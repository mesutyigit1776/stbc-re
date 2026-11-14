/* Ships: PhysicsObjectClass / DamageableObject / ShipClass state and the ship subsystems (ShipSubsystem family).
   Ship loading is data driven, as in the original: the Python ship script (ships/<name>.py, from the stock game or a mod: Foundation
   plugins in Custom/Ships, Bedrock in Orion Delta) creates the ship, the hardpoint script (ships/Hardpoints/<file>.py) fills its
   TGModelPropertySet, and SetupProperties builds the subsystems from those properties. Nothing here is specific to one ship.
   From stbc.exe:
   - DamageableObject::SetupProperties 005935d0: AddSubsystemFromProperty (vtbl +0x160) for every property of the set, then
     ProcessSubsystems (+0x164).
   - ShipClass::AddSubsystemFromProperty 005b3fb0: ShipProperty (0x812e) -> ApplyShipProperty; BlinkingLightProperty (0x8145) -> blinking
     light; subsystem properties by type: 0x812f WeaponSystemProperty by weapon type (+0x4c: 1 phaser, 2 torpedo, 3 pulse, 4 tractor),
     0x8132 PhaserBank, 0x8133 TorpedoSystem, 0x8134 TorpedoTube, 0x8135 PulseWeapon, 0x8136 TractorBeamProjector, 0x8137 ShieldClass,
     0x8138 HullClass, 0x8139 SensorSubsystem, 0x813a CloakingSubsystem, 0x813b WarpEngineSubsystem, 0x813c ImpulseEngineSubsystem,
     0x813d EngineProperty -> ShipSubsystem, 0x813e PowerSubsystem, 0x813f RepairSubsystem. A property with the primary flag (+0x26)
     becomes the ship's subsystem of that kind (ShipClass +0x2b0 power .. +0x2dc cloak).
   - ShipClass::AddSubsystem 005b5030 (ProcessSubsystems): powered subsystems get the power subsystem as power source; weapons become
     children of the primary system of their kind (0x802c phaser, 0x802d pulse, 0x802e tractor, 0x802f torpedo); engines (EngineProperty
     +0x48: 0 impulse, 1 warp) become children of the impulse / warp system. Children leave the top level list.
   - A subsystem keeps its property at +0x18; getters that read the property are generated (re/gen_ship_deleg.py).
   Approximations (marked): no physics integration, no damage model, no firing, no AI execution, no model (radius 1, see ObjectClass). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "props.h"
#include "tg_models.h"
#include "tg_fx.h"
void tg_space_pose(void *o, float *p, float *f, float *u, float *r, float *scale); void tg_space_each(unsigned ct, void (*fn)(void *));

void *tg_space_new_obj(const char *cls, const char *name); void **tg_space_obj_ext(void *o);
int tg_propset_count(void *p); void *tg_propset_prop(void *p, int i);
extern void *eng_BridgeObjectClass_GetPropertySet(void *); extern void *eng_BaseObjectClass_GetContainingSet(void *);
extern void *eng_SetClass_GetObject(void *, char *); extern const char *eng_BaseObjectClass_GetName(void *);
extern double eng_SubsystemProperty_GetMaxCondition(void *); extern long eng_ShipProperty_GetAffiliation(void *);
extern double eng_ShipProperty_GetMass(void *); extern double eng_ShipProperty_GetRotationalInertia(void *);
extern long eng_ShipProperty_GetSpecies(void *); extern long eng_ShipProperty_IsStationary(void *);
extern void *eng_TGModelProperty_GetName(void *); extern const char *eng_TGString_GetCString(void *); extern void eng_delete_TGString(void *);

/* ---- subsystems ---- */
extern void *tg_sound_named(const char *); extern long tg_sound_play(void *); extern void tg_sound_stop_slot(long);
extern void tg_sound_set_loop(void *, int); extern void tg_sound_set_priority(void *, float); extern void tg_sound_attach(void *, void *);
extern long tg_region_add(void *, void *); extern void *eng_TGSoundRegion_GetRegion(char *); extern const char *eng_SetClass_GetName(void *);
extern void *eng_Game_GetCurrentPlayer(void); extern const char *eng_EnergyWeaponProperty_GetFireSound(void *);
struct Ammo;
typedef struct Sub {
    TGObject o;
    void *prop, *ship; struct Sub *parent, **kids; int nkids;
    float condition; int invincible;
    float power_pct_wanted, power_received; int on; struct Sub *power_src;
    float avail, main_batt, backup_batt;                                     /* PowerSubsystem +0xa0 available, +0xac main, +0xb4 backup battery */
    float main_cond, back_cond, dispensed, last_upd;                         /* PowerSubsystem +0xa4/+0xa8 conduit left, +0xbc dispensed, +0xc0 last whole-second update */
    float pw_wanted, pw_pct, pw_norm_pct, last_dt; int pw_mode, dis_flag;   /* PoweredSubsystem +0x8c, +0x94, +0x98; ShipSubsystem +0x3c dt, +0x45; mode +0xa0 */
    float sh_timer, sh_accum;                                                /* ShieldClass +0x154, +0x158 */
    struct Sub **rq; int nrq;                                                /* RepairSubsystem repair list (+0xac head, +0xa8 count) */
    float sens_range; int probes;                                            /* SensorSubsystem last range (+0xa8), probes left */
    int firing, group_mode, chain_mode; long snd[2];                           /* weapons; snd: the slots of the fire sounds (PhaserBank +0xa4 / +0xa8, TractorBeamProjector +0xa4) */
    int chains_loaded, nchains, chain_group, chain_last; unsigned chain_mask[8]; char *chain_name[8];   /* WeaponSystem firing chains (+0xd8..): group bits and names, the group fired last, the tube fired last */
    int ready, tractor_mode; float reload[8], last_fire;                     /* TorpedoTube +0xa0 ready, +0xac slot timers, +0xa4 last fire; TractorBeamSystem mode +0xf4; WeaponSystem +0xf0 last fire */
    float charge, beam_time; void *fire_target, *beam_target; float fire_off[3]; int sys_firing, power_level;   /* charge +0xa0; system target and power level (+0xf0) */
    float shields[6];                                                        /* ShieldClass: current strength per facing */
    int cloaked, cloak_want, cloak_state; float cloak_timer;              /* CloakingSubsystem +0xac cloaked, +0xad trying, +0xb0 state, +0xb4 timer */
    struct Ammo *ammo[8]; int cur_ammo, ammo_left[8];                     /* TorpedoSystem */
    int warp_state, warp_ticks, warp_inv; float warp_timer, warp_exit[3];  /* WarpEngineSubsystem +0xb4 state, +0xb8 stretch frames, +0xa8 saved invincible, +0xb0 timer, +0xc0 exit point */
    unsigned warp_place, warp_seq; void *warp_ai;                           /* +0xbc exit placement (id here), +0xac warp sequence id, +0xcc AI held back while warping */
    char *name;
} Sub;
static Sub *sub(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x801b) ? p : NULL; }
void *tg_sub_prop(void *s) { Sub *x = sub(s); return x ? x->prop : NULL; }
static Sub *sub_new(const char *cls)
{ Sub *s = tg_new(cls, sizeof(Sub), 0); s->power_pct_wanted = 1.0f; s->pw_pct = 1.0f; s->pw_norm_pct = 1.0f; s->on = 1; s->power_level = 1;   /* PoweredSubsystem ctor 00562240 */
  s->last_upd = (float)tg_game_time - 1.0f; return s; }                                                                                     /* PowerSubsystem ctor 00563530 */
void *tg_sub_make(const char *cls) { return sub_new(cls); }
static void sub_set_prop(Sub *s, void *prop)                                 /* vtbl +0x60: the property gives the name and the condition */
{
    void *n;
    s->prop = prop; s->condition = prop ? (float)eng_SubsystemProperty_GetMaxCondition(prop) : 0.0f;
    free(s->name); s->name = NULL;
    if (prop && (n = eng_TGModelProperty_GetName(prop))) { s->name = strdup(eng_TGString_GetCString(n)); eng_delete_TGString(n); }
    if (tg_is_type(&s->o, 0x8022) && prop) { extern double eng_PowerProperty_GetMainBatteryLimit(void *), eng_PowerProperty_GetBackupBatteryLimit(void *);
        s->main_batt = (float)eng_PowerProperty_GetMainBatteryLimit(prop); s->backup_batt = (float)eng_PowerProperty_GetBackupBatteryLimit(prop); }
    if (tg_is_type(&s->o, 0x802b) && prop) { extern double eng_EnergyWeaponProperty_GetMaxCharge(void *); s->charge = (float)eng_EnergyWeaponProperty_GetMaxCharge(prop); }   /* EnergyWeapon::SetProperty 0056fbd0: full charge */
    if (tg_is_type(&s->o, 0x802f) && prop) { extern long eng_TorpedoTubeProperty_GetMaxReady(void *); int i; s->ready = (int)eng_TorpedoTubeProperty_GetMaxReady(prop); for (i = 0; i < 8; i++) s->reload[i] = -1.0f; s->last_fire = -1e9f; }   /* tubes start loaded */
    if (tg_is_type(&s->o, 0x8028) && prop) { extern double eng_ShieldProperty_GetMaxShields(void *, int); int i; for (i = 0; i < 6; i++) s->shields[i] = (float)eng_ShieldProperty_GetMaxShields(prop, i); }
}
static void sub_add_child(Sub *p, Sub *c) { p->kids = realloc(p->kids, (p->nkids + 1) * sizeof *p->kids); p->kids[p->nkids++] = c; c->parent = p; }

/* ---- physics / damageable / ship data (Obj.ext) ---- */
enum { S_POWER, S_TORPEDO, S_PHASER, S_PULSE, S_SHIELD, S_HULL, S_SENSOR, S_IMPULSE, S_WARP, S_TRACTOR, S_REPAIR, S_CLOAK, S_N };
typedef struct Phys {
    float mass, inertia, vel[3], angvel[3], accel[3], angaccel[3]; int is_static, use_physics, net_type, do_net, ang_dir_type;
    void *ai; float lifetime; int dying, dead; float splash, splash_radius, dmg_rad_mod, dmg_str_mod, specular; int collisions;
    /* ship */
    char *script, *death_script; int targetable, invincible, hurtable, destroy_broken, docked, net_player, alert, affiliation, coll_disabled;
    char *module; int parent_id, target_id, player_id; float dmg, dmg_radius, guidance, max_ang, life, age, launch_speed, tgt_off[3];   /* Torpedo / WeaponPayload */
    int fx_kind; float fx_shell[4], fx_core[4], fx_core_size, fx_glow_rate, fx_glow_min, fx_glow_max, fx_len;   /* projectile look (CreateTorpedoModel / CreateDisruptorModel) */
    const char *fx_core_tex, *fx_glow_tex;                                 /* their texture names (interned, see fx_intern) */
    const char *fx_flare_tex; float fx_flare[4], fx_spin, fx_flare_w, fx_flare_len, fx_flare_life, *fx_ray_start, *fx_ray_pt; int fx_nflares, fx_ray_init; unsigned fx_rng;   /* flare rays (see tg_fx_rays) */
    int fx_sides; float fx_width, fx_in_r, fx_in_l;                       /* disruptor bolt: columns, radius, inner radius and length factors */
    int warping, warp_target; float warp_dist;                                /* ShipClass +0x210 in-system warp, +0x214 target id, +0x218 distance */
    int imp_set, imp_dir_set, ang_set; float imp_dir[3], ang_target[3], disabled_decel;   /* ShipClass +0x1e4.. impulse request, +0x1e5/+0x1e8 angular target */
    void *ship_prop; Sub *typed[S_N]; Sub **subs; int nsubs; float impulse, speed; int reverse; void *target, *target_sub; float target_off[3];
} Phys;
extern const char *tg_space_model_of(void *o);
static Phys *phys(void *o)
{
    void **e; Phys *p;
    if (!o || !tg_is_ours(o) || !tg_is_type(o, 0x8006) || !(e = tg_space_obj_ext(o))) return NULL;
    if (!*e) { p = calloc(1, sizeof *p); p->mass = 1.0f; p->inertia = 1.0f; p->use_physics = 1; p->targetable = 1; p->hurtable = 1; p->collisions = 1; p->lifetime = 3.4028235e38f; *e = p; }   /* lifetime FLT_MAX = not dying (DamageableObject +0x14c) */
    return *e;
}
static Phys *ship(void *o) { return o && tg_is_ours(o) && tg_is_type(o, 0x8008) ? phys(o) : NULL; }
static void *pt(const float *v) { return tg_mem_dup(v, 12); }
static char *dupz(const char *s) { return s ? strdup(s) : NULL; }

/* PhysicsObjectClass */
void *eng_PhysicsObjectClass_Cast(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return phys(p) ? p : NULL; }
void *eng_PhysicsObjectClass_ConstCast(PyObject *o) { return eng_PhysicsObjectClass_Cast(o); }
void *eng_PhysicsObjectClass_Create(void *x) { void *o = tg_space_new_obj("PhysicsObjectClass", NULL); phys(o); return o; }
void eng_PhysicsObjectClass_SetStatic(void *o, int v) { Phys *p = phys(o); if (p) p->is_static = v != 0; }
long eng_PhysicsObjectClass_IsStatic(void *o) { Phys *p = phys(o); return p && p->is_static; }
void eng_PhysicsObjectClass_SetupModel(void *o, char *m) { }
void eng_PhysicsObjectClass_ApplyForce(void *o, PyObject *f) { }
double eng_PhysicsObjectClass_GetMass(void *o) { Phys *p = phys(o); return p ? p->mass : 0; }
double eng_PhysicsObjectClass_GetRotationalInertia(void *o) { Phys *p = phys(o); return p ? p->inertia : 0; }
void eng_PhysicsObjectClass_SetMass(void *o, float v) { Phys *p = phys(o); if (p) p->mass = v; }
void eng_PhysicsObjectClass_SetRotationalInertia(void *o, float v) { Phys *p = phys(o); if (p) p->inertia = v; }
void eng_PhysicsObjectClass_SetVelocity(void *o, void *v) { Phys *p = phys(o); if (p && v) memcpy(p->vel, v, 12); }
void eng_PhysicsObjectClass_SetAngularVelocity(void *o, void *v, int t) { Phys *p = phys(o); if (p && v) memcpy(p->angvel, v, 12); }
void eng_PhysicsObjectClass_SetAcceleration(void *o, void *v) { Phys *p = phys(o); if (p && v) memcpy(p->accel, v, 12); }
void eng_PhysicsObjectClass_SetAngularAcceleration(void *o, void *v) { Phys *p = phys(o); if (p && v) memcpy(p->angaccel, v, 12); }
void eng_PhysicsObjectClass_SetAngularAccelerationLinear(void *o, void *a, void *b, float c) { }
double eng_PhysicsObjectClass_TurnTowardOrientation(void *o, void *f, void *u) { return 0.0; }
#define VGET(Name, f) void *eng_PhysicsObjectClass_##Name(void *o) { static const float z[3]; Phys *p = phys(o); return pt(p ? p->f : z); }
VGET(GetVelocity, vel) VGET(GetVelocityTG, vel) VGET(GetAcceleration, accel) VGET(GetAccelerationTG, accel)
int tg_ship_velocity(void *o, float v[3]) { Phys *p = phys(o); if (!p) return 0; memcpy(v, p->vel, 12); return 1; }   /* world velocity (effects: InheritsVelocity) */
VGET(GetAngularVelocity, angvel) VGET(GetAngularVelocityTG, angvel) VGET(GetAngularAcceleration, angaccel) VGET(GetAngularAccelerationTG, angaccel)
void eng_PhysicsObjectClass_SetAngularDirectionType(void *o, int v) { Phys *p = phys(o); if (p) p->ang_dir_type = v; }
long eng_PhysicsObjectClass_GetAngularDirectionType(void *o) { Phys *p = phys(o); return p ? p->ang_dir_type : 0; }
void *eng_PhysicsObjectClass_GetPredictedPosition(void *o, void *a, void *b, void *c, float t) { extern void *eng_BaseObjectClass_GetWorldLocation(void *); return eng_BaseObjectClass_GetWorldLocation(o); }
void *eng_PhysicsObjectClass_GetPredictedRotationTG(void *o, float t) { extern void *eng_BaseObjectClass_GetWorldRotation(void *); return eng_BaseObjectClass_GetWorldRotation(o); }
void eng_PhysicsObjectClass_SetNetType(void *o, int v) { Phys *p = phys(o); if (p) p->net_type = v; }
long eng_PhysicsObjectClass_GetNetType(void *o) { Phys *p = phys(o); return p ? p->net_type : 0; }
void eng_PhysicsObjectClass_SetDoNetUpdate(void *o, int v) { Phys *p = phys(o); if (p) p->do_net = v != 0; }
long eng_PhysicsObjectClass_IsDoingNetUpdate(void *o) { Phys *p = phys(o); return p && p->do_net; }
void eng_PhysicsObjectClass_SetUsePhysics(void *o, int v) { Phys *p = phys(o); if (p) p->use_physics = v != 0; }
long eng_PhysicsObjectClass_IsUsingPhysics(void *o) { Phys *p = phys(o); return p && p->use_physics; }
int tg_warp_hold_ai(void *o, void *ai);
long eng_PhysicsObjectClass_SetAI(void *o, void *ai, int a, int b) { Phys *p = phys(o); if (!p) return 0; if (tg_warp_hold_ai(o, ai)) return 1; p->ai = ai; return 1; }   /* AddAI 005a15a0 (one root AI; runs in tg_ai_tick); ShipClass::AddAI 005ae5a0 holds it back while warping */
void eng_PhysicsObjectClass_ClearAI(void *o, int a, void *ai) { Phys *p = phys(o); if (p && (!ai || p->ai == ai)) p->ai = NULL; }
void *eng_PhysicsObjectClass_GetAI(void *o) { Phys *p = phys(o); return p ? p->ai : NULL; }
long eng_PhysicsObjectClass_HasBuildingAIs(void *o) { return 0; }
void eng_delete_PhysicsObjectClass(void *o) { }

/* DamageableObject */
static int dmg_geom = 1, vol_dmg_geom = 1, breakable = 1;
void *eng_DamageableObject_Cast(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p && tg_is_ours(p) && tg_is_type(p, 0x8007) ? p : NULL; }
void *eng_DamageableObject_Create(char *model) { void *o = tg_space_new_obj("DamageableObject", NULL); phys(o); return o; }   /* model not loaded */
double eng_DamageableObject_GetClonedModelRadius(void *o) { return 1.0; }
long eng_DamageableObject_HasClonedModel(void *o) { return 0; }
long eng_DamageableObject_GetClonedModelCount(void *o) { return 0; }
void eng_DamageableObject_RemoveVisibleDamage(void *o) { }
void eng_DamageableObject_SetVisibleDamageRadiusModifier(void *o, float v) { Phys *p = phys(o); if (p) p->dmg_rad_mod = v; }
void eng_DamageableObject_SetVisibleDamageStrengthModifier(void *o, float v) { Phys *p = phys(o); if (p) p->dmg_str_mod = v; }
void eng_DamageableObject_SetSpecularKs(void *o, float v) { Phys *p = phys(o); if (p) p->specular = v; }
/* the specular coefficient of an object's geometry (SetSpecularKs, DamageableObject +0x1c0; the geometry field +0x100 the renderer reads); 0 when none was set */
float tg_ship_specular_ks(void *o) { Phys *p = phys(o); return p ? p->specular : 0.0f; }
void eng_DamageableObject_DisableGlowAlphaMaps(void *o) { }
long eng_DamageableObject_CanCollide(void *o, void *q) { Phys *p = phys(o); return p && p->collisions; }
void eng_DamageableObject_EnableCollisionsWith(void *o, void *q, int v) { }
void eng_DamageableObject_SetCollisionsOn(void *o, int v) { Phys *p = phys(o); if (p) p->collisions = v != 0; }
/* hull damage volumes (DamageableObject::AddDamageVolume 00594020; see tg_damage.c). The point is given in the object's own frame, in world units, as a script
   passes it (AddObjectDamageVolume 005941b0: x, y, z, radius, damage) or in world space (AddDamage: the world point and radius / damage). Mesh space is the ship's
   axes divided by its scale. */
double eng_BaseObjectClass_GetScale(void *); static void world_to_model_scaled(void *o, const float *wp, float *mp);
static void damage_volume_mesh(void *o, const float *mp_world_units, float radius_world, float strength)
{
    float s = (float)eng_BaseObjectClass_GetScale(o), c[3]; if (s <= 0) s = 1.0f;
    c[0] = mp_world_units[0] / s; c[1] = mp_world_units[1] / s; c[2] = mp_world_units[2] / s;
    tg_damage_add_volume(o, c, radius_world / s, strength);
}
void eng_DamageableObject_AddObjectDamageVolume(void *o, float x, float y, float z, float r, float dmg) { float p[3] = {x, y, z}; if (phys(o)) damage_volume_mesh(o, p, r, dmg); }
void eng_DamageableObject_AddDamage(void *o, void *pt, float r, float dmg)    /* a world point (Effects.DeathExplosionDamage passes a random point on the model) */
{
    extern int tg_effects_node_pos(void *, float *); float wp[3], mp[3]; if (!phys(o) || !pt || !tg_effects_node_pos(pt, wp)) return;
    world_to_model_scaled(o, wp, mp); if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[damage] AddDamage world %.2f %.2f %.2f -> object %.2f %.2f %.2f radius %.3f scale %.4f\n", wp[0], wp[1], wp[2], mp[0], mp[1], mp[2], r, eng_BaseObjectClass_GetScale(o)); damage_volume_mesh(o, mp, r, dmg);
}
void eng_DamageableObject_DamageRefresh(void *o, int a, int b) { }
void eng_DamageableObject_SetLifeTime(void *o, float v) { Phys *p = phys(o); if (p) p->lifetime = v; }
double eng_DamageableObject_GetLifeTime(void *o) { Phys *p = phys(o); return p ? p->lifetime : 0; }
long eng_DamageableObject_IsDying(void *o) { Phys *p = phys(o); return p && (p->dying || p->lifetime < 3.4028235e38f); }
long eng_DamageableObject_IsDead(void *o) { Phys *p = phys(o); return p && p->dead; }
void eng_DamageableObject_SetDead(void *o) { Phys *p = phys(o); if (p) p->dead = 1; }
void eng_DamageableObject_SetSplashDamage(void *o, float d, float r) { Phys *p = phys(o); if (p) { p->splash = d; p->splash_radius = r; } }
double eng_DamageableObject_GetSplashDamage(void *o) { Phys *p = phys(o); return p ? p->splash : 0; }
double eng_DamageableObject_GetSplashDamageRadius(void *o) { Phys *p = phys(o); return p ? p->splash_radius : 0; }
void eng_DamageableObject_SetDamageGeometryEnabled(int v) { dmg_geom = v != 0; }
long eng_DamageableObject_IsDamageGeometryEnabled(void) { return dmg_geom; }
void eng_DamageableObject_SetVolumeDamageGeometryEnabled(int v) { vol_dmg_geom = v != 0; }
long eng_DamageableObject_IsVolumeDamageGeometryEnabled(void) { return vol_dmg_geom; }
int tg_dmg_flag(int which) { return which == 0 ? dmg_geom : which == 1 ? vol_dmg_geom : breakable; }
int tg_ship_motion(void *o, float *vel, float *life) { Phys *p = phys(o); if (!p) return 0; memcpy(vel, p->vel, 12); if (life) *life = p->lifetime < 1.0e9f ? p->lifetime : -1.0f; return 1; }
void eng_DamageableObject_SetBreakableComponentsEnabled(int v) { breakable = v != 0; }
long eng_DamageableObject_IsBreakableComponentsEnabled(void) { return breakable; }
void eng_delete_DamageableObject(void *o) { }

/* SetupProperties: subsystems from the property set */
static Sub *new_for_prop(Phys *s, void *prop)                                /* AddSubsystemFromProperty 005b3fb0 */
{
    PropObj *p = pobj(prop); const struct cls *c = p ? p->o.cls : NULL; unsigned ct = c ? c->ct : 0; Sub *x = NULL; int kind = -1;
    switch (ct) {
    case 0x812f: case 0x8133: {
        int t = ct == 0x8133 ? 2 : P32S(p, 0x4c);
        if (t == 1) { x = sub_new("PhaserSystem"); kind = S_PHASER; }
        else if (t == 2) { x = sub_new("TorpedoSystem"); kind = S_TORPEDO; }
        else if (t == 3) { x = sub_new("PulseWeaponSystem"); kind = S_PULSE; }
        else if (t == 4) { x = sub_new("TractorBeamSystem"); kind = S_TRACTOR; }
        break; }
    case 0x8132: x = sub_new("PhaserBank"); break;
    case 0x8134: x = sub_new("TorpedoTube"); break;
    case 0x8135: x = sub_new("PulseWeapon"); break;
    case 0x8136: x = sub_new("TractorBeamProjector"); break;
    case 0x8137: x = sub_new("ShieldClass"); kind = S_SHIELD; break;
    case 0x8138: x = sub_new("HullClass"); kind = S_HULL; break;
    case 0x8139: x = sub_new("SensorSubsystem"); kind = S_SENSOR; break;
    case 0x813a: x = sub_new("CloakingSubsystem"); x->on = 0; x->pw_mode = 2; kind = S_CLOAK; break;   /* ctor 0055e2b0: off, power from the reserve */
    case 0x813b: x = sub_new("WarpEngineSubsystem"); kind = S_WARP; break;
    case 0x813c: x = sub_new("ImpulseEngineSubsystem"); kind = S_IMPULSE; break;
    case 0x813d: x = sub_new("ShipSubsystem"); break;
    case 0x813e: x = sub_new("PowerSubsystem"); kind = S_POWER; break;
    case 0x813f: x = sub_new("RepairSubsystem"); kind = S_REPAIR; break;
    default: return NULL;
    }
    sub_set_prop(x, prop);
    if (kind >= 0 && P8(p, 0x26) == 1) s->typed[kind] = x;
    return x;
}
static void apply_ship_property(Phys *s, void *prop)                          /* ApplyShipProperty 005b5240 */
{
    s->ship_prop = prop; s->mass = (float)eng_ShipProperty_GetMass(prop); s->inertia = (float)eng_ShipProperty_GetRotationalInertia(prop);
    s->affiliation = (int)eng_ShipProperty_GetAffiliation(prop); s->is_static = eng_ShipProperty_IsStationary(prop) != 0;
}
static void process(Phys *s)                                                  /* ProcessSubsystems 005b3e20 / AddSubsystem 005b5030 */
{
    int i, j;
    for (i = 0; i < s->nsubs; ) {
        Sub *x = s->subs[i], *par = NULL; unsigned ct = x->o.cls->ct; PropObj *p = pobj(x->prop);
        if (tg_is_type(&x->o, 0x801c) && s->typed[S_POWER]) x->power_src = s->typed[S_POWER];
        if (ct == 0x802c) par = s->typed[S_PHASER]; else if (ct == 0x802d) par = s->typed[S_PULSE];
        else if (ct == 0x802e) par = s->typed[S_TRACTOR]; else if (ct == 0x802f) par = s->typed[S_TORPEDO];
        else if (p && p->o.cls && p->o.cls->ct == 0x813d) par = P32S(p, 0x48) == 0 ? s->typed[S_IMPULSE] : P32S(p, 0x48) == 1 ? s->typed[S_WARP] : NULL;
        if (par) { sub_add_child(par, x); for (j = i; j < s->nsubs - 1; j++) s->subs[j] = s->subs[j + 1]; s->nsubs--; continue; }
        i++;
    }
}
static void *prop_clone(void *prop)                                          /* a copy of a template property (TGModelPropertyInstance copy, assumed) */
{
    PropObj *p = pobj(prop), *q; if (!p) return prop;
    q = tg_new(p->o.cls->name, sizeof(PropObj), 0); { TGObject keep = q->o; *q = *p; q->o = keep; }
    if (p->name) q->name = tg_str_copy(p->name);
    return q;
}
void eng_DamageableObject_SetupProperties(void *o)                            /* 005935d0 */
{
    Phys *s = phys(o); void *set = eng_BridgeObjectClass_GetPropertySet(o); int i, n = tg_propset_count(set);
    if (!s) return;
    for (i = 0; i < n; i++) {
        void *prop = tg_propset_prop(set, i); PropObj *p = pobj(prop); Sub *x;
        if (!p) continue;
        if (p->o.cls && p->o.cls->ct == 0x812e) { apply_ship_property(s, prop); continue; }
        prop = prop_clone(prop);                                             /* each ship owns a copy (scripts change it, e.g. AdjustShipForDifficulty) */
        if ((x = new_for_prop(s, prop))) { x->ship = o; s->subs = realloc(s->subs, (s->nsubs + 1) * sizeof *s->subs); s->subs[s->nsubs++] = x; }
    }
    process(s);
}

/* ShipClass */
void *eng_ShipClass_Create(char *name)                                        /* 005ab930 */
{ void tg_space_set_model(void *, const char *); void *o = tg_space_new_obj("ShipClass", name); phys(o); tg_space_set_model(o, name); return o; }   /* the name is the LOD model name (ships/<x>.py) */
void *eng_ShipClass_Cast(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return ship(p) ? p : NULL; }
void eng_ShipClass_SetScript(void *o, char *s) { Phys *p = ship(o); if (p) { free(p->script); p->script = dupz(s); } }
const char *eng_ShipClass_GetScript(void *o) { Phys *p = ship(o); return p ? p->script : NULL; }
void eng_ShipClass_SetDeathScript(void *o, char *s) { Phys *p = ship(o); if (p) { free(p->death_script); p->death_script = dupz(s); } }
const char *eng_ShipClass_GetDeathScript(void *o) { Phys *p = ship(o); return p ? p->death_script : NULL; }
#define SFLAG(Set, Is, f) void eng_ShipClass_##Set(void *o, int v) { Phys *p = ship(o); if (p) p->f = v != 0; } long eng_ShipClass_##Is(void *o) { Phys *p = ship(o); return p && p->f; }
SFLAG(SetTargetable, IsTargetable, targetable) SFLAG(SetInvincible, IsInvincible, invincible) SFLAG(SetHurtable, IsHurtable, hurtable)
SFLAG(SetDestroyBrokenSystems, IsDestroyBrokenSystems, destroy_broken) SFLAG(SetDocked, IsDocked, docked)
void eng_ShipClass_DisableCollisionDamage(void *o, int v) { Phys *p = ship(o); if (p) p->coll_disabled = v != 0; }
long eng_ShipClass_IsCollisionDamageDisabled(void *o) { Phys *p = ship(o); return p && p->coll_disabled; }
#define TGET(Name, k) void *eng_ShipClass_##Name(void *o) { Phys *p = ship(o); return p ? p->typed[k] : NULL; }
TGET(GetHull, S_HULL) TGET(GetShields, S_SHIELD) TGET(GetPowerSubsystem, S_POWER) TGET(GetSensorSubsystem, S_SENSOR)
TGET(GetImpulseEngineSubsystem, S_IMPULSE) TGET(GetWarpEngineSubsystem, S_WARP) TGET(GetTorpedoSystem, S_TORPEDO) TGET(GetPhaserSystem, S_PHASER)
TGET(GetPulseWeaponSystem, S_PULSE) TGET(GetTractorBeamSystem, S_TRACTOR) TGET(GetRepairSubsystem, S_REPAIR) TGET(GetCloakingSubsystem, S_CLOAK)
void *eng_ShipClass_GetShipProperty(void *o) { Phys *p = ship(o); return p ? p->ship_prop : NULL; }
/* ShipClass::GetWeaponSystemGroup 005b0ad0: primary = phasers, or the pulse weapons when the ship has no phasers (+0x2b8, +0x2bc);
   secondary = torpedoes (+0x2b4); tertiary = pulse weapons (+0x2bc); tractor = tractor beam (+0x2d4). Matches DefaultKeyboardBinding
   (F, X, G) and the tactical weapon controls. */
void *eng_ShipClass_GetWeaponSystemGroup(void *o, int g)
{
    Phys *p = ship(o);
    if (!p) return NULL;
    switch (g) {
    case 1: return p->typed[S_PHASER] ? p->typed[S_PHASER] : p->typed[S_PULSE];
    case 2: return p->typed[S_TORPEDO];
    case 3: return p->typed[S_PULSE];
    case 4: return p->typed[S_TRACTOR];
    default: return NULL;
    }
}
long eng_ShipClass_GetWeaponGroupForSystem(void *o, void *s) { return 0; }
void eng_ShipClass_CompleteStop(void *o) { Phys *p = ship(o); if (p) { p->impulse = p->speed = 0; memset(p->vel, 0, 12); memset(p->angvel, 0, 12); } }
void eng_ShipClass_AddSubsystem(void *o, void *x) { Phys *p = ship(o); Sub *s = sub(x); if (p && s) { s->ship = o; p->subs = realloc(p->subs, (p->nsubs + 1) * sizeof *p->subs); p->subs[p->nsubs++] = s; } }
static void all_subs(Phys *p, Sub ***out, int *n)
{
    int i, j, k = 0, cap = 0; Sub **v = NULL;
    for (i = 0; i < p->nsubs; i++) { Sub *s = p->subs[i]; for (j = -1; j < s->nkids; j++) { if (k == cap) { cap = cap ? cap * 2 : 32; v = realloc(v, cap * sizeof *v); } v[k++] = j < 0 ? s : s->kids[j]; } }
    *out = v; *n = k;
}
/* the top level subsystems of a ship (ShipClass +0x284 list), for the damage display; the caller frees the array */
int tg_ship_top_subsystems(void *o, void ***out)
{
    Phys *p = ship(o); int i; void **v;
    *out = NULL; if (!p || !p->nsubs) return 0;
    v = malloc(p->nsubs * sizeof *v); for (i = 0; i < p->nsubs; i++) v[i] = p->subs[i];
    *out = v; return p->nsubs;
}
void *eng_ShipClass_GetSubsystemByProperty(void *o, PyObject *po)
{
    Phys *p = ship(o); void *pr = NULL; Sub **v; int n, i; void *r = NULL;
    if (!p || !po || !appc_to_ptr(po, &pr, "void")) { PyErr_Clear(); return NULL; }
    all_subs(p, &v, &n); for (i = 0; i < n; i++) if (v[i]->prop == pr) { r = v[i]; break; } free(v); return r;
}
/* iteration: the iterator is a Python int (index into the flattened list) carried in a one-element list; matching by type id */
typedef struct { int ct, i; } Match;
PyObject *eng_ShipClass_StartGetSubsystemMatch(void *o, int ct) { Match *m = malloc(sizeof *m); m->ct = ct; m->i = 0; return appc_from_ptr(m, "void"); }
void *eng_ShipClass_GetNextSubsystemMatch(void *o, PyObject *it)
{
    Phys *p = ship(o); Match *m = NULL; Sub **v; int n; void *r = NULL;
    if (!p || !appc_to_ptr(it, (void **)&m, "void") || !m) { PyErr_Clear(); return NULL; }
    all_subs(p, &v, &n); for (; m->i < n; m->i++) if (!m->ct || tg_is_type(&v[m->i]->o, (unsigned)m->ct)) { r = v[m->i++]; break; } free(v); return r;
}
void eng_ShipClass_EndGetSubsystemMatch(void *o, PyObject *it) { void *m = NULL; if (appc_to_ptr(it, &m, "void")) free(m); else PyErr_Clear(); }
PyObject *eng_ShipClass_GetSubsystems(void *o)
{
    Phys *p = ship(o); PyObject *l; int i;                                    /* 005b3930: a tuple of shadow instances (vtbl +0x20) */
    l = PyTuple_New(p ? p->nsubs : 0);
    if (p) for (i = 0; i < p->nsubs; i++) PyTuple_SetItem(l, i, appc_pyobj(p->subs[i], p->subs[i]->o.cls->name));
    return l;
}
void eng_ShipClass_SetImpulse(void *o, float v, void *dir, int a)        /* 005ac470: clamp to [-1, 1]; needs an impulse engine */
{
    Phys *p = ship(o); if (!p || !p->typed[S_IMPULSE]) return;
    p->impulse = v < -1 ? -1 : v > 1 ? 1 : v; p->reverse = p->impulse < 0; p->imp_set = 1; p->imp_dir_set = a != 0;
    if (dir) memcpy(p->imp_dir, dir, 12);
}
static float imp_max_speed(Phys *p) { extern double eng_ImpulseEngineProperty_GetMaxSpeed(void *); Sub *e = p->typed[S_IMPULSE]; return e && e->prop ? (float)eng_ImpulseEngineProperty_GetMaxSpeed(e->prop) : 0; }
double eng_ShipClass_GetImpulse(void *o) { Phys *p = ship(o); return p ? p->impulse : 0; }
long eng_ShipClass_IsReverse(void *o) { Phys *p = ship(o); return p && p->reverse; }
void eng_ShipClass_SetSpeed(void *o, float v, void *dir, int a) { Phys *p = ship(o); float m; if (!p || !p->typed[S_IMPULSE]) return; m = imp_max_speed(p); p->speed = v; eng_ShipClass_SetImpulse(o, m > 0 ? v / m : 0, dir, a); }   /* 005ac590 */
void eng_ShipClass_SetAcceleration(void *o, void *v) { eng_PhysicsObjectClass_SetAcceleration(o, v); }
void eng_ShipClass_SetDisabledEngineDeceleration(void *o, float v) { Phys *p = ship(o); if (p) p->disabled_decel = v; }
/* ShipClass::SetTargetAngularVelocityFraction (TurnShip in TacticalInterfaceHandlers.py): the wanted angular velocity is the unit
   axis x fraction x the impulse engine's max angular velocity. The axis is in model space when ang_dir_type is DIRECTION_MODEL_SPACE
   (0). Model axes: forward (0,1,0), up (0,0,1), right = forward x up (the stock frame). Approximation: the fraction is the length of
   the vector, capped at 1 (the script passes a unit axis or zero). */
void eng_ShipClass_SetTargetAngularVelocityFraction(void *o, void *v)
{
    extern double eng_ImpulseEngineSubsystem_GetMaxAngularVelocity(void *);
    extern void *eng_BaseObjectClass_GetWorldForwardTG(void *), *eng_BaseObjectClass_GetWorldUpTG(void *);
    Phys *p = ship(o); Sub *e; float a[3], w[3], fwd[3], up[3], right[3], len, frac, *f, *u; int i;
    if (!p || !v) return;
    memcpy(a, v, 12);
    len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    frac = len > 1.0f ? 1.0f : len;
    e = p->typed[S_IMPULSE];
    if (!e || len < 1e-9f) { memset(p->ang_target, 0, 12); p->ang_set = 1; return; }
    if (p->ang_dir_type == 0) {                                          /* model space: rotate the axis into world space */
        f = eng_BaseObjectClass_GetWorldForwardTG(o); u = eng_BaseObjectClass_GetWorldUpTG(o);
        memcpy(fwd, f, 12); memcpy(up, u, 12); free(f); free(u);
        right[0] = fwd[1] * up[2] - fwd[2] * up[1]; right[1] = fwd[2] * up[0] - fwd[0] * up[2]; right[2] = fwd[0] * up[1] - fwd[1] * up[0];
        for (i = 0; i < 3; i++) w[i] = (a[0] * right[i] + a[1] * fwd[i] + a[2] * up[i]) / len;
    } else for (i = 0; i < 3; i++) w[i] = a[i] / len;
    for (i = 0; i < 3; i++) p->ang_target[i] = w[i] * frac * (float)eng_ImpulseEngineSubsystem_GetMaxAngularVelocity(e);
    p->ang_set = 1;
}
void eng_ShipClass_SetTargetAngularVelocityDirect(void *o, void *v) { Phys *p = ship(o); if (p && v) { memcpy(p->ang_target, v, 12); p->ang_set = 1; } }
void *eng_ShipClass_GetTargetAngularVelocityTG(void *o) { static const float z[3]; Phys *p = ship(o); return pt(p ? p->ang_target : z); }
long eng_ShipClass_IsCloaked(void *o) { Phys *p = ship(o); return p && p->typed[S_CLOAK] && p->typed[S_CLOAK]->cloaked; }
void eng_ShipClass_SetNetPlayerID(void *o, int v) { Phys *p = ship(o); if (p) p->net_player = v; }
long eng_ShipClass_GetNetPlayerID(void *o) { Phys *p = ship(o); return p ? p->net_player : 0; }
long eng_ShipClass_IsPlayerShip(void *o) { extern void *eng_Game_GetPlayer(void *); extern void *eng_Game_GetCurrentGame(void); return o && eng_Game_GetPlayer(eng_Game_GetCurrentGame()) == o; }
void eng_ShipClass_GetNextTarget(void *o) { }
void *eng_ShipClass_GetTarget(void *o) { Phys *p = ship(o); return p ? p->target : NULL; }
/* ChangeTarget 005ae210: a new target stops the weapons and sends ET_TARGET_WAS_CHANGED (TGObjPtrEvent: destination the ship, source the
   targeted subsystem, object = the old target); then the targeted subsystem is set */
void eng_ShipClass_StopFiringWeapons(void *o); long eng_SensorSubsystem_IsObjectVisible(void *, void *);
void tg_ship_change_target(void *o, void *t, void *tsub)
{
    Phys *p = ship(o); void *old; TGEvent *e;
    if (!p) return;
    old = p->target;
    if (!old || old != t) {
        p->target = t; eng_ShipClass_StopFiringWeapons(o);
        e = tg_ev_new("TGObjPtrEvent", 0); e->type = (unsigned)tg_const("ET_TARGET_WAS_CHANGED"); e->dest = o; e->source = tsub;
        e->v.p = old; e->ref_id = old && tg_is_ours(old) ? ((TGObject *)old)->id : 0; tg_add_event(e);
    }
    p->target_sub = tsub;
}
void eng_ShipClass_SetTarget(void *o, char *name) { if (ship(o) && eng_BaseObjectClass_GetContainingSet(o)) tg_ship_change_target(o, name ? eng_SetClass_GetObject(eng_BaseObjectClass_GetContainingSet(o), name) : NULL, NULL); }   /* 005ae1e0 */
/* IsTargetable (vslot 0xc8): ships keep a flag (+0x2e8); other objects are not targetable here */
int tg_obj_targetable(void *o) { Phys *p = ship(o); return p ? p->targetable : 0; }
/* ShipClass::IsObjectVisible 005ae0f0 (vslot 0xcc, the CanTargetObject of the target list): not itself, targetable, and seen by the sensors */
long eng_ShipClass_CanTargetObject(void *o, void *t)
{ Phys *p = ship(o); return p && t && t != o && tg_obj_targetable(t) && p->typed[S_SENSOR] && eng_SensorSubsystem_IsObjectVisible(p->typed[S_SENSOR], t); }
/* SetClass::GetTargetableObjects 0040f980: targetable objects of the set; with the "alive" flag no dead or dying objects; with a ship only those
   it can target. Python gets a tuple of the objects. */
PyObject *eng_SetClass_GetTargetableObjects(void *sp, void *by, int alive)
{
    int tg_space_set_objects(void *, void ***); void **objs; int n = tg_space_set_objects(sp, &objs), i, k = 0; PyObject *t; void **hit = malloc((n ? n : 1) * sizeof *hit);
    for (i = 0; i < n; i++) {
        void *o = objs[i]; Phys *q;
        if (!tg_obj_targetable(o)) continue;
        if (alive && (q = phys(o)) && tg_is_type(o, 0x8007) && (q->dead || q->lifetime < 3.4028235e38f)) continue;
        if (by && !eng_ShipClass_CanTargetObject(by, o)) continue;
        hit[k++] = o;
    }
    t = PyTuple_New(k); for (i = 0; i < k; i++) PyTuple_SetItem(t, i, tg_obj_pyobj(hit[i]));
    free(hit); return t;
}
void eng_ShipClass_SetTargetSubsystem(void *o, void *s) { Phys *p = ship(o); if (p) p->target_sub = s; }
void *eng_ShipClass_GetTargetSubsystem(void *o) { Phys *p = ship(o); return p ? p->target_sub : NULL; }
void *eng_ShipClass_GetTargetOffsetTG(void *o) { static const float z[3]; Phys *p = ship(o); return pt(p ? p->target_off : z); }
void eng_ShipClass_UseTargetOffsetTG(void *o, int u, void *v) { Phys *p = ship(o); if (p && v) memcpy(p->target_off, v, 12); }
long eng_ShipClass_GetAffiliation(void *o) { Phys *p = ship(o); return p ? p->affiliation : 0; }
void eng_ShipClass_SetAffiliation(void *o, int v) { Phys *p = ship(o); if (p) p->affiliation = v; }
long eng_ShipClass_GetAlertLevel(void *o) { Phys *p = ship(o); return p ? p->alert : 0; }
/* ShipClass::SetAlertLevel 005b0110 (+0x238): green turns the shields and weapons (phasers, torpedoes, pulse) off and the tractor, impulse,
   warp and sensors on; yellow turns the shields on and the weapons off; red turns all of them on. A system that is turned on goes to
   100% power wanted. Levels other than 0..2 change nothing. */
void eng_PoweredSubsystem_TurnOn(void *), eng_PoweredSubsystem_TurnOff(void *), eng_PoweredSubsystem_SetPowerPercentageWanted(void *, float);
static void sub_turn_on(Sub *x) { if (x && !x->on) { eng_PoweredSubsystem_TurnOn(x); eng_PoweredSubsystem_SetPowerPercentageWanted(x, 1.0f); } }
static void sub_turn_off(Sub *x) { if (x) eng_PoweredSubsystem_TurnOff(x); }
void eng_ShipClass_SetAlertLevel(void *o, int v)
{
    Phys *p = ship(o); Sub **t; int i;
    static const int order[8] = {S_SHIELD, S_PHASER, S_TORPEDO, S_PULSE, S_TRACTOR, S_IMPULSE, S_WARP, S_SENSOR};
    if (!p || v < 0 || v > 2) return;
    t = p->typed; p->alert = v;
    for (i = 0; i < 8; i++) {
        int k = order[i], on = v == 2 || k == S_TRACTOR || k == S_IMPULSE || k == S_WARP || k == S_SENSOR || (v == 1 && k == S_SHIELD);
        if (on) sub_turn_on(t[k]);
    }
    for (i = 0; i < 8; i++) {
        int k = order[i], on = v == 2 || k == S_TRACTOR || k == S_IMPULSE || k == S_WARP || k == S_SENSOR || (v == 1 && k == S_SHIELD);
        if (!on) sub_turn_off(t[k]);
    }
}
void eng_ShipClass_IncrementAIDoneIgnore(void *o) { }
void eng_delete_ShipClass(void *o) { }

/* ---- ShipSubsystem family (state) ---- */
#define SCASTV(Cls, ct) void *eng_##Cls##_Cast(void *p) { return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; } \
    void *eng_##Cls##_GetProperty(void *s) { return tg_sub_prop(s); }
#define SCAST(Cls, ct) void *eng_##Cls##_Cast(PyObject *o) { void *p = NULL; if (!o || o == Py_None || !appc_to_ptr(o, &p, "void")) { PyErr_Clear(); return NULL; } return p && tg_is_ours(p) && tg_is_type(p, ct) ? p : NULL; } \
    void *eng_##Cls##_GetProperty(void *s) { return tg_sub_prop(s); }
SCAST(ShipSubsystem, 0x801b) SCAST(ShieldClass, 0x8028) SCAST(HullClass, 0x8027) SCASTV(SensorSubsystem, 0x8023) SCASTV(CloakingSubsystem, 0x8024)
SCAST(WarpEngineSubsystem, 0x8025) SCAST(ImpulseEngineSubsystem, 0x8026) SCAST(RepairSubsystem, 0x8029) SCASTV(TorpedoSystem, 0x801e) SCASTV(PhaserSystem, 0x801f)
SCASTV(PulseWeaponSystem, 0x8020) SCAST(TractorBeamSystem, 0x8021) SCAST(EnergyWeapon, 0x802b) SCASTV(PhaserBank, 0x802c) SCASTV(PulseWeapon, 0x802d)
SCAST(TractorBeamProjector, 0x802e) SCASTV(TorpedoTube, 0x802f) SCAST(PoweredSubsystem, 0x801c) SCAST(PowerSubsystem, 0x8022) SCAST(Weapon, 0x802a) SCAST(WeaponSystem, 0x801d)
const char *eng_ShipSubsystem_GetName(void *s) { Sub *x = sub(s); return x ? x->name : NULL; }
void *eng_ShipSubsystem_GetDisplayName(void *s) { extern void *eng_new_TGString(char *); Sub *x = sub(s); return x ? eng_new_TGString(x->name ? x->name : "") : NULL; }   /* App.py owns it */
double eng_ShipSubsystem_GetCondition(void *s) { Sub *x = sub(s); return x ? x->condition : 0; }
static float maxc(Sub *x) { return x->prop ? (float)eng_SubsystemProperty_GetMaxCondition(x->prop) : 1.0f; }
double eng_ShipSubsystem_GetConditionPercentage(void *s) { Sub *x = sub(s); float m; if (!x) return 0; m = maxc(x); return m > 0 ? x->condition / m : 0; }
double eng_ShipSubsystem_GetCombinedConditionPercentage(void *s)
{ Sub *x = sub(s); int i; double t; if (!x) return 0; t = eng_ShipSubsystem_GetConditionPercentage(s); if (!x->nkids) return t; for (i = 0; i < x->nkids; i++) t += eng_ShipSubsystem_GetConditionPercentage(x->kids[i]); return t / (x->nkids + 1); }
double eng_ShipSubsystem_GetDamage(void *s) { Sub *x = sub(s); return x ? maxc(x) - x->condition : 0; }
double eng_ShipSubsystem_GetRepairPointsNeeded(void *s) { return eng_ShipSubsystem_GetDamage(s); }
void *eng_ShipSubsystem_GetWorldLocation(void *s) { extern void *eng_SubsystemProperty_GetPosition(void *); Sub *x = sub(s); return x && x->prop ? eng_SubsystemProperty_GetPosition(x->prop) : NULL; }
void *eng_ShipSubsystem_GetParentShip(void *s) { Sub *x = sub(s); return x ? x->ship : NULL; }
/* ---- condition watchers (FloatRangeWatcher): range checks on a subsystem's condition percentage. When the value crosses a
   threshold in the chosen direction, a copy of the registered event goes out with the value in v.f. The AI status watchers
   (ConditionSystemBelow, ConditionPowerBelow) use this. Approximation: the value is checked once per frame (tg_ship_tick),
   not on every change of the condition. */
#define FRW_ABOVE 1
#define FRW_BELOW 2
#define FRW_BOTH  3
typedef struct RangeCheck { int id, mode, above; float threshold; unsigned type; TGObject *src, *dst; struct RangeCheck *next; } RangeCheck;
/* kind 0: the subsystem's condition; 1 and 2: the main and backup battery of a PowerSubsystem (level / limit); 3-8: one shield facing;
   9: all shields together (ShieldClass::GetShieldWatcher(6)); 10: the combined condition of a subsystem and its children */
typedef struct FRW { TGObject o; int kind; Sub *sub; RangeCheck *checks; int next_id; struct FRW *all_next; } FRW;
static FRW *all_frw;
static FRW *frw_of(void *w) { return w && tg_is_ours(w) && tg_is_type(w, 0x10020) ? w : NULL; }
static void *frw_get(int kind, Sub *x)
{
    FRW *f;
    if (!x) return NULL;
    for (f = all_frw; f; f = f->all_next) if (f->sub == x && f->kind == kind) return f;
    f = tg_new_plain("FloatRangeWatcher", sizeof *f); f->kind = kind; f->sub = x; f->next_id = 1; f->all_next = all_frw; all_frw = f;
    return f;
}
void *eng_ShipSubsystem_GetConditionWatcher(void *s) { return frw_get(0, sub(s)); }
void *eng_PowerSubsystem_GetMainBatteryWatcher(void *s) { return frw_get(1, sub(s)); }
void *eng_PowerSubsystem_GetBackupBatteryWatcher(void *s) { return frw_get(2, sub(s)); }
void *eng_ShieldClass_GetShieldWatcher(void *s, int side) { return side >= 0 && side < 6 ? frw_get(3 + side, sub(s)) : side == 6 ? frw_get(9, sub(s)) : NULL; }   /* kind 3 + facing */
double eng_FloatRangeWatcher_GetWatchedVariable(void *w)
{
    FRW *f = frw_of(w); extern double eng_PowerProperty_GetMainBatteryLimit(void *), eng_PowerProperty_GetBackupBatteryLimit(void *);
    extern double eng_ShieldClass_GetSingleShieldPercentage(void *, int), eng_ShieldClass_GetShieldPercentage(void *);
    double lim; Sub *x;
    if (!f || !(x = f->sub)) return 0.0;
    if (f->kind == 0) return eng_ShipSubsystem_GetConditionPercentage(x);
    if (f->kind == 9) return eng_ShieldClass_GetShieldPercentage(x);
    if (f->kind == 10) return eng_ShipSubsystem_GetCombinedConditionPercentage(x);
    if (f->kind >= 3) return eng_ShieldClass_GetSingleShieldPercentage(x, f->kind - 3);
    lim = f->kind == 1 ? eng_PowerProperty_GetMainBatteryLimit(x->prop) : eng_PowerProperty_GetBackupBatteryLimit(x->prop);
    return lim > 0 ? (f->kind == 1 ? x->main_batt : x->backup_batt) / lim : 0.0;
}
long eng_FloatRangeWatcher_AddRangeCheck(void *w, float threshold, int mode, void *event)
{
    FRW *f = frw_of(w); RangeCheck *c; TGEvent *e = event && tg_is_ours(event) && tg_is_type(event, 0x101) ? event : NULL;
    if (!f || !e) return 0;
    c = calloc(1, sizeof *c);
    c->id = f->next_id++; c->threshold = threshold; c->mode = mode; c->type = e->type; c->src = e->source; c->dst = e->dest;
    c->above = eng_FloatRangeWatcher_GetWatchedVariable(f) > threshold;
    c->next = f->checks; f->checks = c;
    return c->id;
}
void eng_FloatRangeWatcher_RemoveRangeCheck(void *w, int id)
{
    FRW *f = frw_of(w); RangeCheck **pc;
    if (!f) return;
    for (pc = &f->checks; *pc; pc = &(*pc)->next) if ((*pc)->id == id) { RangeCheck *c = *pc; *pc = c->next; free(c); return; }
}
static void frw_tick(void)
{
    FRW *f; RangeCheck *c;
    for (f = all_frw; f; f = f->all_next) for (c = f->checks; c; c = c->next) {
        float v = (float)eng_FloatRangeWatcher_GetWatchedVariable(f); int now = v > c->threshold;
        if (now != c->above) {
            TGEvent *e;
            c->above = now;
            if (c->mode == FRW_BOTH || (c->mode == FRW_ABOVE && now) || (c->mode == FRW_BELOW && !now)) {
                e = tg_ev_new("TGFloatEvent", 0);
                e->type = c->type; e->source = c->src; e->dest = c->dst; e->v.f = v; tg_add_event(e);
            }
        }
    }
}
void *eng_ShipSubsystem_GetCombinedPercentageWatcher(void *s) { return frw_get(10, sub(s)); }
double eng_ShipSubsystem_GetLastUpdateLength(void *s) { return 0; }
long eng_ShipSubsystem_IsDisabled(void *s) { extern double eng_SubsystemProperty_GetDisabledPercentage(void *); Sub *x = sub(s); return x && x->prop && eng_ShipSubsystem_GetConditionPercentage(s) < eng_SubsystemProperty_GetDisabledPercentage(x->prop); }
void *eng_ShipSubsystem_GetParentSubsystem(void *s) { Sub *x = sub(s); return x ? x->parent : NULL; }
long eng_ShipSubsystem_IsHittableFromLocation(void *s, void *l) { return 1; }
PyObject *eng_ShipSubsystem_GetChildSubsystems(void *s)
{ Sub *x = sub(s); PyObject *l = PyList_New(0); int i; if (x) for (i = 0; i < x->nkids; i++) { PyObject *o = appc_pyobj(x->kids[i], x->kids[i]->o.cls->name); PyList_Append(l, o); Py_DECREF(o); } return l; }
void *eng_ShipSubsystem_GetChildSubsystem(void *s, int i) { Sub *x = sub(s); return x && i >= 0 && i < x->nkids ? x->kids[i] : NULL; }
long eng_ShipSubsystem_GetChildSubsystemIndex(void *s, void *c) { Sub *x = sub(s); int i; if (x) for (i = 0; i < x->nkids; i++) if (x->kids[i] == c) return i; return -1; }
long eng_ShipSubsystem_GetNumChildSubsystems(void *s) { Sub *x = sub(s); return x ? x->nkids : 0; }
long eng_ShipSubsystem_GetLastChildSubsystemIndex(void *s) { Sub *x = sub(s); return x ? x->nkids - 1 : -1; }
long eng_ShipSubsystem_HasTargetableChildSubsystem(void *s) { extern long eng_SubsystemProperty_IsTargetable(void *); Sub *x = sub(s); int i; if (x) for (i = 0; i < x->nkids; i++) if (x->kids[i]->prop && eng_SubsystemProperty_IsTargetable(x->kids[i]->prop)) return 1; return 0; }
void *eng_ShipSubsystem_GetNextTargetableChildSubsystem(void *s, void *c)
{ extern long eng_SubsystemProperty_IsTargetable(void *); Sub *x = sub(s); int i = (int)eng_ShipSubsystem_GetChildSubsystemIndex(s, c) + 1; if (x) for (; i < x->nkids; i++) if (x->kids[i]->prop && eng_SubsystemProperty_IsTargetable(x->kids[i]->prop)) return x->kids[i]; return NULL; }
void eng_ShipSubsystem_AddChildSubsystem(void *s, void *c) { Sub *x = sub(s), *y = sub(c); if (x && y) sub_add_child(x, y); }
/* ShipSubsystem::SetCondition 0056c470: at most the max condition; below it, ET_SUBSYSTEM_DAMAGED (TGObjPtrEvent, subsystem ID at +0x28)
   goes to the ship unless the ship is dying (lifetime below FLT_MAX). The exe does not clamp at 0 here (DamageSystem passes >= 0). */
static void sub_set_condition(Sub *x, float v)
{
    float m = maxc(x); Phys *p = x->ship ? phys(x->ship) : NULL;
    x->condition = v > m ? m : v;
    if (x->condition < m && (!x->ship || (p && p->lifetime >= 3.4028234e38f))) {
        TGEvent *e = tg_ev_new("TGObjPtrEvent", 0);
        e->type = (unsigned)tg_const("ET_SUBSYSTEM_DAMAGED"); e->dest = x->ship; e->ref_id = x->o.id; e->v.p = x; tg_add_event(e);
    }
}
void eng_ShipSubsystem_SetCondition(void *s, float v) { Sub *x = sub(s); if (x) sub_set_condition(x, v); }
void eng_ShipSubsystem_SetConditionPercentage(void *s, float v) { Sub *x = sub(s); if (x) eng_ShipSubsystem_SetCondition(s, v * maxc(x)); }
long eng_ShipSubsystem_IsInvincible(void *s) { Sub *x = sub(s); return x && x->invincible; }
void eng_ShipSubsystem_SetInvincible(void *s, int v) { Sub *x = sub(s); if (x) x->invincible = v != 0; }
void eng_ShipSubsystem_SetParentShip(void *s, void *o) { Sub *x = sub(s); if (x) x->ship = o; }
void eng_ShipSubsystem_SetParentSubsystem(void *s, void *p) { Sub *x = sub(s); if (x) x->parent = sub(p); }
void eng_ShipSubsystem_SetProperty(void *s, void *p) { Sub *x = sub(s); if (x) sub_set_prop(x, p); }
void eng_ShipSubsystem_Repair(void *s, float v)                                 /* 0056bd90: points / repair complexity */
{ extern double eng_SubsystemProperty_GetRepairComplexity(void *); Sub *x = sub(s); float c; if (!x) return; c = x->prop ? (float)eng_SubsystemProperty_GetRepairComplexity(x->prop) : 1.0f; sub_set_condition(x, x->condition + (c != 0 ? v / c : v)); }
void eng_ShipSubsystem_Update(void *s, float dt) { }
void *eng_ShipSubsystem_GetDamagePoint(void *s) { return eng_ShipSubsystem_GetWorldLocation(s); }
void eng_ShipSubsystem_UpdateDamagePoint(void *s) { }
void *eng_ShipSubsystem_Create(void) { return sub_new("ShipSubsystem"); }
void *eng_new_ShipSubsystem(long id) { return sub_new("ShipSubsystem"); }
void eng_delete_ShipSubsystem(void *s) { }

/* powered subsystems */
static float normal_power(Sub *x) { extern double eng_PoweredSubsystemProperty_GetNormalPowerPerSecond(void *); return x->prop ? (float)eng_PoweredSubsystemProperty_GetNormalPowerPerSecond(x->prop) : 0; }
double eng_PoweredSubsystem_GetPowerPercentageWanted(void *s) { Sub *x = sub(s); return x ? x->power_pct_wanted : 0; }
void eng_PoweredSubsystem_SetPowerPercentageWanted(void *s, float v)          /* 00562430: the frame's wanted power scales with it */
{ Sub *x = sub(s); float o; if (!x) return; o = x->power_pct_wanted; x->power_pct_wanted = v; if (o != 0.0f) x->pw_wanted = x->pw_wanted * v / o; }
double eng_PoweredSubsystem_GetPowerWanted(void *s) { Sub *x = sub(s); return x ? x->pw_wanted : 0; }                                 /* +0x8c, this frame */
static int any_kid_firing(Sub *x) { int i; for (i = 0; i < x->nkids; i++) if (x->kids[i]->firing) return 1; return 0; }
static float normal_wanted(Sub *x)                                              /* vslot 0x78 005623d0; tractor systems 00582260: only while firing (+0xa9) */
{ if (tg_is_type(&x->o, 0x8021) && !any_kid_firing(x)) return 0.0f; return !eng_ShipSubsystem_IsDisabled(x) && x->on && x->prop ? normal_power(x) : 0.0f; }
double eng_PoweredSubsystem_GetNormalPowerWanted(void *s) { Sub *x = sub(s); return x ? normal_wanted(x) : 0; }
void eng_PoweredSubsystem_SetPowerWanted(void *s, float v) { Sub *x = sub(s); if (x) x->pw_wanted = v; }                                /* 0060e760 writes +0x8c */
void eng_PoweredSubsystem_SetPowerSource(void *s, void *p) { Sub *x = sub(s); if (x) x->power_src = sub(p); }
double eng_PoweredSubsystem_GetPowerReceived(void *s) { Sub *x = sub(s); return x ? x->power_received : 0; }                          /* +0x88 */
double eng_PoweredSubsystem_GetPowerPercentage(void *s) { Sub *x = sub(s); return x ? x->pw_pct : 0; }                                /* +0x94 received / wanted */
double eng_PoweredSubsystem_GetNormalPowerPercentage(void *s) { Sub *x = sub(s); return x ? x->pw_norm_pct : 0; }                     /* +0x98 received / normal */
static void state_event(Sub *x, int on)                                        /* TurnOn 00562560 / TurnOff 00562630: ET_SUBSYSTEM_STATE_CHANGED to the ship */
{ TGEvent *e = tg_ev_new("TGBoolEvent", 0); e->type = (unsigned)tg_const("ET_SUBSYSTEM_STATE_CHANGED"); e->source = &x->o; e->dest = x->ship; e->v.i = on; tg_add_event(e); }
static int cloak_blocks_shields(Phys *p);
void eng_PoweredSubsystem_TurnOn(void *s)
{
    Sub *x = sub(s);
    if (!x || x->on) return;
    if (tg_is_type(&x->o, 0x8028) && x->ship && cloak_blocks_shields(ship(x->ship))) return;   /* shields: turnOnIfNotCloaking 0056ab10 */
    x->on = 1; state_event(x, 1);
}
void eng_PoweredSubsystem_TurnOff(void *s) { Sub *x = sub(s); if (x && x->on) { x->on = 0; state_event(x, 0); } }
void eng_PoweredSubsystem_Turn(void *s, int v) { if (v) eng_PoweredSubsystem_TurnOn(s); else eng_PoweredSubsystem_TurnOff(s); }
long eng_PoweredSubsystem_IsOn(void *s) { Sub *x = sub(s); return x && x->on; }
void *eng_new_PoweredSubsystem(long id) { return sub_new("PoweredSubsystem"); }
void eng_delete_PoweredSubsystem(void *s) { }
/* power subsystem (batteries; no power flow yet) */
double eng_PowerSubsystem_GetAvailablePower(void *s) { Sub *x = sub(s); return x ? x->avail : 0; }
double eng_PowerSubsystem_GetMainBatteryPower(void *s) { Sub *x = sub(s); return x ? x->main_batt : 0; }
double eng_PowerSubsystem_GetBackupBatteryPower(void *s) { Sub *x = sub(s); return x ? x->backup_batt : 0; }
double eng_PowerSubsystem_GetMaxMainConduitCapacity(void *s) { extern double eng_PowerProperty_GetMainConduitCapacity(void *); Sub *x = sub(s); return x && x->prop ? eng_PowerProperty_GetMainConduitCapacity(x->prop) : 0; }
void eng_PowerSubsystem_SetAvailablePower(void *s, float v) { Sub *x = sub(s); if (x) x->avail = v; }
void eng_PowerSubsystem_SetMainBatteryPower(void *s, float v) { Sub *x = sub(s); if (x) x->main_batt = v; }
void eng_PowerSubsystem_SetBackupBatteryPower(void *s, float v) { Sub *x = sub(s); if (x) x->backup_batt = v; }
/* PowerSubsystem::AddPower 005638d0: main battery first, then the backup battery, then what is left back into the main battery
   (which is full by then, so it is lost); +0xa0 counts what was stored */
static float main_lim(Sub *x) { extern double eng_PowerProperty_GetMainBatteryLimit(void *); return x->prop ? (float)eng_PowerProperty_GetMainBatteryLimit(x->prop) : 0; }
static float back_lim(Sub *x) { extern double eng_PowerProperty_GetBackupBatteryLimit(void *); return x->prop ? (float)eng_PowerProperty_GetBackupBatteryLimit(x->prop) : 0; }
static void power_add(Sub *x, float v)
{
    float room = main_lim(x) - x->main_batt, rest = 0, rest2 = 0;
    if (room <= v) { x->main_batt = main_lim(x); x->avail += room; rest = v - room; } else { x->main_batt += v; x->avail += v; }
    room = back_lim(x) - x->backup_batt;
    if (room <= rest) { x->backup_batt = back_lim(x); x->avail += room; rest2 = rest - room; } else { x->backup_batt += rest; x->avail += rest; }
    room = main_lim(x) - x->main_batt;
    if (room > 0.0f) { if (rest2 < room) { x->main_batt += rest2; x->avail += rest2; } else { x->main_batt = main_lim(x); x->avail += room; } }
}
/* DeductPower 00563a70 (single player): from the main conduit and battery; the rest from the backup conduit and battery. When the
   backup conduit cannot give all of the rest, it is emptied, the battery keeps its power and only the main part is returned (as in the exe). */
static float power_deduct(Sub *x, float v)
{
    float m = x->main_cond, r;
    if (v <= m) { x->main_cond = m - v; x->main_batt -= v; x->dispensed += v; return v; }
    x->main_cond = 0; r = v - m; x->main_batt -= m;
    if (x->back_cond < r) { x->back_cond = 0; x->dispensed += m; return m; }
    x->back_cond -= r; x->backup_batt -= r; x->dispensed += v; return v;
}
static float power_steal(Sub *x, float v)                                      /* StealPower 00563bb0: batteries directly, no conduit limit */
{
    float m = x->main_batt, t;
    if (v <= m) { x->main_batt = m - v; return v; }
    x->main_batt = 0; t = m + x->backup_batt;
    if (t < v) { x->backup_batt = 0; return t; }
    x->backup_batt -= v - m; return v;
}
static float power_steal_reserve(Sub *x, float v)                              /* StealPowerFromReserve 00563cb0 */
{ float b = x->backup_batt; if (v <= b) { x->backup_batt = b - v; return v; } x->backup_batt = 0; return b; }
void eng_PowerSubsystem_AddPower(void *s, float v) { Sub *x = sub(s); if (x) power_add(x, v); }
double eng_PowerSubsystem_DeductPower(void *s, float v) { Sub *x = sub(s); return x ? power_deduct(x, v) : 0; }
double eng_PowerSubsystem_StealPower(void *s, float v) { Sub *x = sub(s); return x ? power_steal(x, v) : 0; }
double eng_PowerSubsystem_StealPowerFromReserve(void *s, float v) { Sub *x = sub(s); return x ? power_steal_reserve(x, v) : 0; }
double eng_PowerSubsystem_GetPowerWanted(void *s, float dt)                    /* 00563ed0: sum of the sources' frame power wanted x dt */
{ Sub *x = sub(s); Phys *p; double t = 0; int i; if (!x || !(p = ship(x->ship))) return 0; for (i = 0; i < p->nsubs; i++) if (p->subs[i]->power_src == x) t += p->subs[i]->pw_wanted * dt; return t; }
double eng_PowerSubsystem_GetPowerDispensed(void *s) { Sub *x = sub(s); return x ? x->dispensed : 0; }
/* ManagePowerAI (native replacement of the AI.Preprocessors.ManagePower script, which is "Unused" in Python): every 3 s the power
   subsystem's main battery level picks a power budget (00487180/00487220), and DistributePowerToSubsystems 00487330 shares it out by
   weight (sensors 0.25, warp engines 0.05, others 1; cloaks are skipped) as power percentages, capped at 125%. */
static float cond_pct(Sub *x);
int tg_ship_manage_power(void *o, int conserve)
{
    extern double eng_PowerProperty_GetMainBatteryLimit(void *), eng_PowerProperty_GetMainConduitCapacity(void *),
        eng_PowerProperty_GetBackupConduitCapacity(void *), eng_PowerProperty_GetPowerOutput(void *);
    Phys *p = ship(o); Sub *ps, *v[64]; float w[64], wsum = 0, lim, thr, r, t, mainc, backc, out, lo = 0, hi = 0, budget; int i, n = 0, again;
    if (!p || !(ps = p->typed[S_POWER]) || !ps->prop) return 0;
    lim = (float)eng_PowerProperty_GetMainBatteryLimit(ps->prop); thr = lim * (conserve ? 1.0625f : 0.9f);
    if (ps->main_batt < thr) r = (1.0f - (thr - ps->main_batt) / thr) * 0.5f;
    else { t = lim - thr != 0 ? (lim - ps->main_batt) / (lim - thr) : 0; r = t * 0.5f + (1.0f - t); }
    mainc = (float)eng_PowerProperty_GetMainConduitCapacity(ps->prop) * cond_pct(ps); if (ps->main_batt < mainc) mainc = ps->main_batt;
    backc = (float)eng_PowerProperty_GetBackupConduitCapacity(ps->prop); if (ps->backup_batt < backc) backc = ps->backup_batt;
    out = (float)eng_PowerProperty_GetPowerOutput(ps->prop) * cond_pct(ps);
    if (r >= 0.5f) { hi = 2 * (1.0f - r); budget = (1.0f - hi) * (backc + mainc) + out * hi; }
    else { lo = 2 * (0.5f - r); budget = out * lo * 0.1f + out * (1.0f - lo); }
    for (i = 0; i < p->nsubs && n < 64; i++) {
        Sub *x = p->subs[i];
        if (x->power_src != ps || tg_is_type(&x->o, 0x8024)) continue;
        w[n] = tg_is_type(&x->o, 0x8023) ? 0.25f : tg_is_type(&x->o, 0x8025) ? 0.05f : 1.0f; wsum += w[n]; v[n++] = x;
    }
    do {                                                                       /* a subsystem over 125% gets 125%; the rest is shared again */
        again = 0;
        for (i = 0; i < n; i++) {
            float np = normal_power(v[i]), pct = np != 0 && wsum > 0 ? w[i] / wsum * budget / np : 0;
            if (pct <= 1.25f) { v[i]->power_pct_wanted = pct; continue; }
            budget -= 1.25f * np; wsum -= w[i]; v[i]->power_pct_wanted = 1.25f;
            v[i] = v[n - 1]; w[i] = w[n - 1]; n--; i--; again = 1;
        }
    } while (again && n);
    return 1;
}
void *eng_PowerSubsystem_Create(void) { return sub_new("PowerSubsystem"); }
void *eng_new_PowerSubsystem(void) { return sub_new("PowerSubsystem"); }
void eng_delete_PowerSubsystem(void *s) { }
/* weapons: no firing yet */
void eng_Weapon_StopFiring(void *s) { Sub *x = sub(s); if (x) x->firing = 0; }
long eng_Weapon_Fire(void *s, float a, int b) { return 0; }
long eng_Weapon_FireDumb(void *s, float a, int b) { return 0; }
long eng_Weapon_CanFire(void *s) { Sub *x = sub(s); return x && x->condition > 0; }
long eng_Weapon_IsFiring(void *s) { Sub *x = sub(s); return x && x->firing; }
void eng_Weapon_SetFiring(void *s, int v) { Sub *x = sub(s); if (x) x->firing = v != 0; }
long eng_Weapon_IsMemberOfGroup(void *s, int g) { return 0; }
double eng_Weapon_GetOverallConditionPercentage(void *s) { return eng_ShipSubsystem_GetConditionPercentage(s); }
long eng_Weapon_IsDumbFire(void *s) { return 0; }
void *eng_Weapon_CalculateRoughDirection(void *s) { static const float z[3]; return pt(z); }
double eng_Weapon_CalculateWeaponAppeal(void *s) { return 0; }
long eng_Weapon_GetTargetID(void *s) { return 0; }
void *eng_new_Weapon(long id) { return sub_new("Weapon"); }
void eng_delete_Weapon(void *s) { }
long eng_WeaponSystem_ShouldBeAimed(void *s) { return 0; }
long eng_WeaponSystem_IsTryingToFire(void *s) { return 0; }
long eng_WeaponSystem_IsFiring(void *s) { Sub *x = sub(s); int i; if (x) for (i = 0; i < x->nkids; i++) if (x->kids[i]->firing) return 1; return 0; }
void *eng_WeaponSystem_GetWeapon(void *s, int i) { return eng_ShipSubsystem_GetChildSubsystem(s, i); }
long eng_WeaponSystem_CanFire(void *s) { Sub *x = sub(s); return x && x->on && x->condition > 0; }
void eng_WeaponSystem_ClearTargetList(void *s) { }
long eng_WeaponSystem_IsInTargetList(void *s, void *t) { return 0; }
void eng_WeaponSystem_SetForceUpdate(void *s, int v) { }
void eng_WeaponSystem_RemoveFromTargetList(void *s, void *t) { }
void *eng_WeaponSystem_UpdateWeapons(void *s, float dt, PyObject *o) { return NULL; }
void eng_WeaponSystem_UpdateTargetList(void *s) { }
PyObject *eng_WeaponSystem_GetTargetList(void *s) { return PyList_New(0); }
long eng_WeaponSystem_GetGroupFireMode(void *s) { Sub *x = sub(s); return x ? x->group_mode : 0; }
void eng_WeaponSystem_SetGroupFireMode(void *s, int v) { Sub *x = sub(s); if (x) x->group_mode = v; }
static void chains_load(Sub *x);
long eng_WeaponSystem_GetFiringChainMode(void *s) { Sub *x = sub(s); return x ? x->chain_mode : 0; }
void eng_WeaponSystem_SetFiringChainMode(void *s, int v) { Sub *x = sub(s); if (x) { chains_load(x); if (v >= 0 && v < x->nchains) x->chain_mode = v; } }   /* 00584fa0: only a mode the chains have */
/* WeaponSystem::LoadFiringChains 00585020: the property's firing chain string is "groups;name;groups;name..." ("0;Single;123;Dual;53;Quad": the digits 1 to 9 of
   the groups text are the weapon groups of the chain, 0 means no group). Loaded the first time it is needed. */
static void chains_load(Sub *x)
{
    extern void *eng_WeaponSystemProperty_GetFiringChainString(void *); extern const char *eng_TGString_GetCString(void *);
    void *ts; char *str, *tok, *save; int expect_name = 0; unsigned mask = 0;
    if (x->chains_loaded) return;
    x->chains_loaded = 1; x->nchains = 0;
    if (!x->prop || !(ts = eng_WeaponSystemProperty_GetFiringChainString(x->prop))) return;
    str = strdup(eng_TGString_GetCString(ts)); if (!str) return;
    for (tok = strtok_r(str, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        if (!expect_name) { const char *c; mask = 0; for (c = tok; *c; c++) if (*c > '0' && *c <= '9') mask |= 1u << (*c - '1'); expect_name = 1; }
        else if (x->nchains < 8) { x->chain_mask[x->nchains] = mask; x->chain_name[x->nchains] = strdup(tok); x->nchains++; expect_name = 0; }
    }
    free(str);
}
long eng_WeaponSystem_GetNumFiringChains(void *s) { Sub *x = sub(s); if (!x) return 0; chains_load(x); return x->nchains; }
void *eng_WeaponSystem_GetFiringChain(void *s, int i) { return NULL; }
const char *eng_WeaponSystem_GetFiringChainName(void *s, int i) { Sub *x = sub(s); if (!x) return NULL; chains_load(x); return i >= 0 && i < x->nchains ? x->chain_name[i] : NULL; }
void *eng_WeaponSystem_GetLastWeaponFired(void *s) { return NULL; }
long eng_WeaponSystem_GetLastGroupFired(void *s) { return 0; }
void *eng_WeaponSystem_GetNextWeapon(void *s) { return NULL; }
void eng_WeaponSystem_Fire(void *s) { }
void *eng_WeaponSystem_Create(void) { return sub_new("WeaponSystem"); }
void *eng_new_WeaponSystem(long id) { return sub_new("WeaponSystem"); }
void eng_delete_WeaponSystem(void *s) { }
/* shields: current strength per facing */
double eng_ShieldClass_GetCurShields(void *s, int i) { Sub *x = sub(s); return x && i >= 0 && i < 6 ? x->shields[i] : 0; }
void eng_ShieldClass_SetCurShields(void *s, int i, float v) { Sub *x = sub(s); if (x && i >= 0 && i < 6) x->shields[i] = v; }
/* cloak */
double eng_ShieldClass_GetSingleShieldPercentage(void *s, int i)
{ extern double eng_ShieldProperty_GetMaxShields(void *, int); Sub *x = sub(s); double m; if (!x || i < 0 || i >= 6 || !x->prop) return 0; m = eng_ShieldProperty_GetMaxShields(x->prop, i); return m > 0 ? x->shields[i] / m : 0; }

/* torpedo ammo types: one per torpedo script of the TorpedoSystemProperty; name, speed, sound and power cost come from the script's
   GetName / GetLaunchSpeed / GetLaunchSound / GetPowerCost (the Tactical.Projectiles modules) */
typedef struct Ammo { TGObject o; char *script, *name, *sound; float speed, cost; int max; } Ammo;
extern long eng_TorpedoSystemProperty_GetMaxTorpedoes(void *, int); extern const char *eng_TorpedoSystemProperty_GetTorpedoScript(void *, int);
extern long eng_TorpedoSystem_GetNumAmmoTypes(void *);
static Ammo *as_ammo(void *p) { return p && tg_is_ours(p) && tg_is_type(p, 0x1000f) ? p : NULL; }
void *eng_TorpedoSystem_GetAmmoType(void *s, int i)
{
    Sub *x = sub(s); Ammo *a; const char *sc;
    if (!x || i < 0 || i >= 8 || i >= eng_TorpedoSystem_GetNumAmmoTypes(s)) return NULL;
    if (x->ammo[i]) return x->ammo[i];
    a = tg_new_plain("TorpedoAmmoType", sizeof(Ammo)); sc = eng_TorpedoSystemProperty_GetTorpedoScript(x->prop, i);
    a->script = dupz(sc); a->max = (int)eng_TorpedoSystemProperty_GetMaxTorpedoes(x->prop, i);
    if (sc) { tg_py_call_module(sc, "GetName", 'z', &a->name); tg_py_call_module(sc, "GetLaunchSpeed", 'f', &a->speed);
              tg_py_call_module(sc, "GetLaunchSound", 'z', &a->sound); tg_py_call_module(sc, "GetPowerCost", 'f', &a->cost); }
    x->ammo[i] = a; x->ammo_left[i] = a->max; return a;
}
void *eng_TorpedoSystem_GetCurrentAmmoType(void *s) { Sub *x = sub(s); return x ? eng_TorpedoSystem_GetAmmoType(s, x->cur_ammo) : NULL; }
long eng_TorpedoSystem_GetCurrentAmmoTypeNumber(void *s) { Sub *x = sub(s); return x ? x->cur_ammo : 0; }
long eng_TorpedoSystem_GetAmmoTypeNumber(void *s) { return eng_TorpedoSystem_GetCurrentAmmoTypeNumber(s); }
void eng_TorpedoSystem_SetAmmoType(void *s, int i, int f) { Sub *x = sub(s); if (x && i >= 0 && i < 8) x->cur_ammo = i; }
long eng_TorpedoSystem_GetNumAvailableTorpsToType(void *s, int i) { Sub *x = sub(s); if (!x || !eng_TorpedoSystem_GetAmmoType(s, i)) return 0; return x->ammo_left[i]; }
long eng_TorpedoSystem_LoadAmmoType(void *s, int i, int n) { Sub *x = sub(s); if (!x || !eng_TorpedoSystem_GetAmmoType(s, i)) return 0; x->ammo_left[i] += n; if (x->ammo_left[i] > x->ammo[i]->max) x->ammo_left[i] = x->ammo[i]->max; return 1; }
void eng_TorpedoSystem_FillAmmoType(void *s, int i) { Sub *x = sub(s); if (x && eng_TorpedoSystem_GetAmmoType(s, i)) x->ammo_left[i] = x->ammo[i]->max; }
void eng_TorpedoSystem_RemoveAmmoType(void *s, int i) { Sub *x = sub(s); if (x && i >= 0 && i < 8) x->ammo_left[i] = 0; }
const char *eng_TorpedoAmmoType_GetAmmoName(void *p) { Ammo *a = as_ammo(p); return a ? a->name : NULL; }
const char *eng_TorpedoAmmoType_GetTorpedoScript(void *p) { Ammo *a = as_ammo(p); return a ? a->script : NULL; }
void eng_TorpedoAmmoType_SetTorpedoScript(void *p, char *s) { Ammo *a = as_ammo(p); if (a) { free(a->script); a->script = dupz(s); } }
double eng_TorpedoAmmoType_GetLaunchSpeed(void *p) { Ammo *a = as_ammo(p); return a ? a->speed : 0; }
const char *eng_TorpedoAmmoType_GetLaunchSound(void *p) { Ammo *a = as_ammo(p); return a ? a->sound : NULL; }
double eng_TorpedoAmmoType_GetPowerCost(void *p) { Ammo *a = as_ammo(p); return a ? a->cost : 0; }
long eng_TorpedoAmmoType_GetMaxTorpedoes(void *p) { Ammo *a = as_ammo(p); return a ? a->max : 0; }
void eng_TorpedoAmmoType_SetMaxTorpedoes(void *p, int n) { Ammo *a = as_ammo(p); if (a) a->max = n; }

/* ---- per-frame motion (ShipClass movement FUN_005adbd0, PhysicsObjectClass::Update 005a05c0, integrateMotion 005a09d0) ----
   Ships: target velocity = direction x impulse x max speed (direction = the ship's forward axis unless SetImpulse got one); the acceleration
   is (target - velocity) limited to the engine's max acceleration; the angular acceleration is (target angular velocity - angular velocity)
   limited to the max angular acceleration. A disabled or switched-off impulse engine decelerates by at most the disabled-engine deceleration.
   Integration: dt limited to [0.001, 1]; position += v dt + a dt^2 / 2; v += a dt; the same for the rotation (world angular velocity).
   Approximations (marked): the max speed is the property value (no power or damage scaling yet); TurnToward* (predictive controller
   005ad4d0) is not written; static and non-physics objects do not move. */
extern void *eng_BaseObjectClass_GetWorldForwardTG(void *), *eng_BaseObjectClass_GetWorldUpTG(void *), *eng_BaseObjectClass_GetTranslate(void *);
extern void eng_BaseObjectClass_SetTranslate(void *, void *); extern void eng_BaseObjectClass_AlignToVectors(void *, void *, void *);
static float vlen(const float *v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static void clampv(float *v, float m) { float l = vlen(v); if (m >= 0 && l > m && l > 0) { v[0] *= m / l; v[1] *= m / l; v[2] *= m / l; } }
static void rot_vec(float *v, const float *axis, float ang)                  /* Rodrigues: rotate v about a unit axis */
{
    float c = cosf(ang), s = sinf(ang), d = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2], x[3]; int i;
    x[0] = axis[1] * v[2] - axis[2] * v[1]; x[1] = axis[2] * v[0] - axis[0] * v[2]; x[2] = axis[0] * v[1] - axis[1] * v[0];
    for (i = 0; i < 3; i++) v[i] = v[i] * c + x[i] * s + axis[i] * d * (1 - c);
}
static float tick_dt;
double eng_ImpulseEngineSubsystem_GetMaxSpeed(void *), eng_ImpulseEngineSubsystem_GetMaxAccel(void *), eng_ImpulseEngineSubsystem_GetMaxAngularAccel(void *);
long eng_ShipClass_InSystemWarp(void *o, void *tgt, float dist); void eng_ShipClass_StopInSystemWarp(void *o);
static void ship_controls(void *o, Phys *p)
{
    extern double eng_ImpulseEngineProperty_GetMaxAccel(void *), eng_ImpulseEngineProperty_GetMaxAngularAccel(void *);
    Sub *e = p->typed[S_IMPULSE]; int i;
    if (!e || !e->prop) return;
    if (!eng_ShipSubsystem_IsDisabled(e) && e->on) {
        if (p->imp_set) {
            float dir[3], tv[3], ms = (float)eng_ImpulseEngineSubsystem_GetMaxSpeed(e), *f;
            if (p->imp_dir_set) memcpy(dir, p->imp_dir, 12); else { f = eng_BaseObjectClass_GetWorldForwardTG(o); memcpy(dir, f, 12); free(f); }
            for (i = 0; i < 3; i++) { tv[i] = dir[i] * p->impulse * ms; p->accel[i] = tv[i] - p->vel[i]; }
            clampv(p->accel, (float)eng_ImpulseEngineSubsystem_GetMaxAccel(e));
        }
        if (p->ang_set) {
            for (i = 0; i < 3; i++) p->angaccel[i] = p->ang_target[i] - p->angvel[i];
            clampv(p->angaccel, (float)eng_ImpulseEngineSubsystem_GetMaxAngularAccel(e));
        }
    } else {
        float l = vlen(p->vel), m = l < p->disabled_decel ? l : p->disabled_decel;
        for (i = 0; i < 3; i++) p->accel[i] = l > 1e-6f ? -p->vel[i] / l * m : 0;
    }
}
static void move_one(void *o)
{
    Phys *p = phys(o); float dt = tick_dt, *pos, w; int i;
    void tg_lifetime_tick(void *, float); if (tg_is_type(o, 0x8007)) { tg_lifetime_tick(o, dt); p = phys(o); if (!p || p->dead) return; }
    if (!p || p->is_static || !p->use_physics || p->dead) return;
    if (p->ai && !p->dying) { int tg_ai_tick(void *, void *, double); void *r = p->ai; if (tg_ai_tick(o, r, dt) && (p = phys(o)) && p->ai == r) p->ai = NULL; p = phys(o); if (!p || p->dead) return; }   /* AIList::UpdateAI 004722d0 */
    if (tg_is_type(o, 0x8008)) { void tg_systems_tick(void *, float), tg_combat_tick(void *, float); tg_systems_tick(o, dt); tg_combat_tick(o, dt); }
    if (tg_is_type(o, 0x8009)) { int tg_torpedo_tick(void *, float); if (!tg_torpedo_tick(o, dt)) return; }
    if (tg_is_type(o, 0x8008)) { if (p->warping) { void *w = tg_find((unsigned)p->warp_target); if (w) eng_ShipClass_InSystemWarp(o, w, p->warp_dist); } if (!p->warping) ship_controls(o, p); }   /* ShipClass::Update 005adae0 */
    pos = eng_BaseObjectClass_GetTranslate(o);
    if (!pos) return;
    for (i = 0; i < 3; i++) { pos[i] += p->vel[i] * dt + 0.5f * p->accel[i] * dt * dt; p->vel[i] += p->accel[i] * dt; p->angvel[i] += p->angaccel[i] * dt; }
    eng_BaseObjectClass_SetTranslate(o, pos); free(pos);
    if ((w = vlen(p->angvel)) > 1e-7f) {
        float ax[3] = {p->angvel[0] / w, p->angvel[1] / w, p->angvel[2] / w}, *f = eng_BaseObjectClass_GetWorldForwardTG(o), *u = eng_BaseObjectClass_GetWorldUpTG(o);
        rot_vec(f, ax, w * dt); rot_vec(u, ax, w * dt); eng_BaseObjectClass_AlignToVectors(o, f, u); free(f); free(u);
    }
}
static void engine_sounds_update(void);
void tg_ship_tick(double dt)                                                  /* called once per frame by tg_mission_idle */
{
    void tg_space_each(unsigned ct, void (*fn)(void *));
    tick_dt = (float)(dt < 0.001 ? 0.001 : dt > 1.0 ? 1.0 : dt);
    tg_space_each(0x8006, move_one);
    { void tg_prox_tick(void); tg_prox_tick(); }                             /* proximity checks after the motion */
    { void tg_sensors_tick(void); tg_sensors_tick(); }
    frw_tick();                                                            /* condition range checks (AI status watchers) */
    engine_sounds_update();
}

/* ---- engine limits: ImpulseEngineSubsystem::GetMaxSpeed 00561230 (and the same for accel / angular velocity / angular accel):
   0 when disabled or off; the property value minus, for each child engine, (1 - its condition) x value / children (a whole share when
   the child is disabled); at most the property value; times the power percentage wanted (+0x90); Update 00561180 multiplies by the power
   percentage received (+0x94). Approximation (marked): the own-tractor-beam factor (+0xa8, FUN_005822d0) is left out; power received = wanted. */
static double engine_limit(void *s, double value)
{
    Sub *e = sub(s); double v; int i;
    if (!e || eng_ShipSubsystem_IsDisabled(e) || !e->on) return 0.0;
    v = value;
    for (i = 0; i < e->nkids; i++) v -= eng_ShipSubsystem_IsDisabled(e->kids[i]) ? value / e->nkids : (1.0 - eng_ShipSubsystem_GetConditionPercentage(e->kids[i])) * value / e->nkids;
    if (v > value) v = value;
    return v < 0 ? 0.0 : v * e->power_pct_wanted;
}
#define IMP(Name) double eng_ImpulseEngineSubsystem_Get##Name(void *s) { extern double eng_ImpulseEngineProperty_Get##Name(void *); Sub *e = sub(s); return e && e->prop ? engine_limit(s, eng_ImpulseEngineProperty_Get##Name(e->prop)) : 0.0; } \
    double eng_ImpulseEngineSubsystem_GetCur##Name(void *s) { Sub *e = sub(s); return e ? eng_ImpulseEngineSubsystem_Get##Name(s) * e->pw_pct : 0.0; }   /* Update 00561180: x +0x94 */
IMP(MaxSpeed) IMP(MaxAccel) IMP(MaxAngularVelocity) IMP(MaxAngularAccel)

/* ---- turning: ShipClass::TurnTowardDifference 005ad4d0. D = wanted rotation (axis x angle, world space), w = angular velocity.
   Find the shortest time T (bisection in [0, 30] from 5 until the bracket is below 1/16) for which both the start acceleration
   6D/T^2 - 4w/T and the end acceleration -6D/T^2 + 2w/T stay within the max angular acceleration; apply the start acceleration
   (ApplyAngularAcceleration 005ad090: limited to the maximum, stops a direct angular target). Returns T. ---- */
static void apply_ang(Phys *p, Sub *e, const float *a) { float v[3]; memcpy(v, a, 12); clampv(v, (float)eng_ImpulseEngineSubsystem_GetMaxAngularAccel(e)); memcpy(p->angaccel, v, 12); p->ang_set = 0; }
double eng_ShipClass_TurnTowardDifference(void *o, void *dp)
{
    Phys *p = ship(o); Sub *e; const float *D = dp; float lo = 0, hi = 30, T = 5, a0[3], a1[3], m2; int i, k;
    if (!p || !(e = p->typed[S_IMPULSE]) || !D) return 0.0;
    m2 = (float)eng_ImpulseEngineSubsystem_GetMaxAngularAccel(e); m2 *= m2;
    for (k = 0; k < 64 && hi - lo > 0.0625f; k++) {
        float n0, n1;
        for (i = 0; i < 3; i++) { a0[i] = 6 * D[i] / (T * T) - 4 * p->angvel[i] / T; a1[i] = -6 * D[i] / (T * T) + 2 * p->angvel[i] / T; }
        n0 = a0[0] * a0[0] + a0[1] * a0[1] + a0[2] * a0[2]; n1 = a1[0] * a1[0] + a1[1] * a1[1] + a1[2] * a1[2];
        if (n0 > m2 || n1 > m2) { lo = T; T = (T + hi) * 0.5f; }
        else if (n0 >= m2 && n1 >= m2) break;
        else { hi = T; T = (lo + T) * 0.5f; }
    }
    for (i = 0; i < 3; i++) a0[i] = 6 * D[i] / (hi * hi) - 4 * p->angvel[i] / hi;
    apply_ang(p, e, a0); return hi;
}
static void fwd_of(void *o, float *f) { float *x = eng_BaseObjectClass_GetWorldForwardTG(o); memcpy(f, x, 12); free(x); }
static void up_of(void *o, float *u) { float *x = eng_BaseObjectClass_GetWorldUpTG(o); memcpy(u, x, 12); free(x); }
static void rot_between(const float *a, const float *b, float *out)          /* rotation vector taking unit a to unit b */
{
    float c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}, s = vlen(c), d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2], ang = atan2f(s, d);
    if (s < 1e-6f) { memset(out, 0, 12); if (d < 0) { float u[3] = {0, 0, 1}; memcpy(out, u, 12); out[2] = 3.14159265f; } return; }
    out[0] = c[0] / s * ang; out[1] = c[1] / s * ang; out[2] = c[2] / s * ang;
}
double eng_ShipClass_TurnTowardDirection(void *o, void *dir)                  /* 005ad450 */
{ float f[3], d[3], D[3], l; if (!dir || !ship(o)) return 0; memcpy(d, dir, 12); l = vlen(d); if (l < 1e-6f) return 0; d[0] /= l; d[1] /= l; d[2] /= l; fwd_of(o, f); rot_between(f, d, D); return eng_ShipClass_TurnTowardDifference(o, D); }
double eng_ShipClass_TurnTowardLocation(void *o, void *loc)                   /* 005ad3a0 */
{ float *pos, d[3]; double r; if (!loc || !ship(o)) return 0; pos = eng_BaseObjectClass_GetTranslate(o); d[0] = ((float *)loc)[0] - pos[0]; d[1] = ((float *)loc)[1] - pos[1]; d[2] = ((float *)loc)[2] - pos[2]; free(pos); r = eng_ShipClass_TurnTowardDirection(o, d); return r; }
double eng_ShipClass_TurnTowardOrientation(void *o, void *fw, void *upv)      /* forward first, then roll the up axis about the new forward */
{
    float f[3], u[3], D[3], D2[3], nu[3], *F = fw, *U = upv, l; int i;
    if (!fw || !upv || !ship(o)) return 0;
    fwd_of(o, f); up_of(o, u); rot_between(f, F, D);
    l = vlen(D); memcpy(nu, u, 12); if (l > 1e-6f) { float ax[3] = {D[0] / l, D[1] / l, D[2] / l}; rot_vec(nu, ax, l); }
    rot_between(nu, U, D2); for (i = 0; i < 3; i++) D[i] += D2[i];
    return eng_ShipClass_TurnTowardDifference(o, D);
}
/* ShipClass::TurnDirectionsToDirections 005ad910, Python order (from0, to0, from1, to1): the wanted rotation takes the primary direction
   from0 to to0; when both secondary vectors are given (not zero) it also takes the secondary direction from1 to to1 (the frame built from
   the two "from" vectors goes to the frame built from the two "to" vectors). The scripts' AdjustHeading and the others call it with two
   vectors only. */
static int is_zero3(const float *v) { return !v || (v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f); }
static void frame_of(const float *a, const float *b, float m[3][3])         /* rows: a, a x b (unit), a x (a x b) -> orthonormal frame of the two vectors */
{
    float x[3], c[3], l; int i;
    for (i = 0; i < 3; i++) x[i] = a[i];
    l = vlen(x); if (l > 1e-9f) for (i = 0; i < 3; i++) x[i] /= l;
    c[0] = x[1] * b[2] - x[2] * b[1]; c[1] = x[2] * b[0] - x[0] * b[2]; c[2] = x[0] * b[1] - x[1] * b[0];
    l = vlen(c); if (l > 1e-9f) for (i = 0; i < 3; i++) c[i] /= l;
    for (i = 0; i < 3; i++) { m[0][i] = x[i]; m[1][i] = c[i]; }
    m[2][0] = x[1] * c[2] - x[2] * c[1]; m[2][1] = x[2] * c[0] - x[0] * c[2]; m[2][2] = x[0] * c[1] - x[1] * c[0];
}
double eng_ShipClass_TurnDirectionsToDirections(void *o, void *f0, void *f1, void *u0, void *u1)
{
    float D[3], A[3][3], B[3][3], R[3][3], tr, ang, ax[3], l; int i, j, k;
    if (!f0 || !f1) return 0;
    if (is_zero3(u0) || is_zero3(u1)) { rot_between(f0, f1, D); return eng_ShipClass_TurnTowardDifference(o, D); }
    frame_of(f0, u0, A); frame_of(f1, u1, B);
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) { R[i][j] = 0; for (k = 0; k < 3; k++) R[i][j] += B[k][i] * A[k][j]; }   /* R = B^T A: takes the first frame's vectors to the second's */
    tr = R[0][0] + R[1][1] + R[2][2]; tr = (tr - 1.0f) * 0.5f; tr = tr > 1 ? 1 : tr < -1 ? -1 : tr; ang = acosf(tr);
    ax[0] = R[2][1] - R[1][2]; ax[1] = R[0][2] - R[2][0]; ax[2] = R[1][0] - R[0][1]; l = vlen(ax);
    if (l < 1e-6f) { if (ang < 1e-3f) { memset(D, 0, 12); return eng_ShipClass_TurnTowardDifference(o, D); } ax[0] = A[1][0]; ax[1] = A[1][1]; ax[2] = A[1][2]; l = vlen(ax); }
    for (i = 0; i < 3; i++) D[i] = ax[i] / l * ang;
    return eng_ShipClass_TurnTowardDifference(o, D);
}

/* ---- in-system warp: ShipClass::InSystemWarp 005ac6e0 / StopInSystemWarp 005acdb0. Each frame while warping: aim at the point
   (target radius + distance) short of the target; when the ship faces it (within cos 15 degrees = 0.9659) fly at 75 units/s, otherwise
   turn at impulse 1. Starting sends ET_IN_SYSTEM_WARP (TGBoolEvent, true) to the ship and plays "InSystemWarp" for the player.
   Approximation (marked): no ProximityManager obstacle check (the exe shortens the path at objects in the way). ---- */
long eng_ShipClass_InSystemWarp(void *o, void *tgt, float dist)
{
    extern double eng_ObjectClass_GetRadius(void *); extern long eng_ShipClass_IsPlayerShip(void *);
    Phys *p = ship(o); Sub *e; float *a, *b, d[3], l, stop, dest[3], f[3], v[3]; int i;
    if (!p || !tgt) return 0;
    p->warp_target = (int)((TGObject *)tgt)->id; p->warp_dist = dist;
    if (!(e = p->typed[S_IMPULSE]) || eng_ImpulseEngineSubsystem_GetMaxAccel(e) <= 0) { eng_ShipClass_StopInSystemWarp(o); return 0; }
    a = eng_BaseObjectClass_GetTranslate(o); b = eng_BaseObjectClass_GetTranslate(tgt);
    for (i = 0; i < 3; i++) d[i] = b[i] - a[i];
    l = vlen(d); stop = (float)eng_ObjectClass_GetRadius(tgt) + dist;
    if (l <= 50.0f || l <= stop) { free(a); free(b); eng_ShipClass_StopInSystemWarp(o); return 0; }
    for (i = 0; i < 3; i++) dest[i] = a[i] + d[i] / l * (l - stop);
    free(a); free(b);
    if (!p->warping) {
        TGEvent *ev = tg_ev_new("TGBoolEvent", 0); ev->type = 8388847u; ev->source = o; ev->dest = o; ev->v.i = 1; tg_add_event(ev);   /* ET_IN_SYSTEM_WARP */
        if (eng_ShipClass_IsPlayerShip(o)) {                                        /* the player's warp hum loops until the warp stops */
            extern int tg_sound_is_playing_name(const char *); void *sn;
            if (!tg_sound_is_playing_name("InSystemWarp") && (sn = tg_sound_named("InSystemWarp"))) { tg_sound_set_loop(sn, 1); tg_sound_play(sn); } }
    }
    p->warping = 1;
    eng_ShipClass_TurnTowardLocation(o, dest);
    fwd_of(o, f);
    { float *pos = eng_BaseObjectClass_GetTranslate(o); for (i = 0; i < 3; i++) d[i] = dest[i] - pos[i]; free(pos); }
    l = vlen(d); if (l > 1e-6f) for (i = 0; i < 3; i++) d[i] /= l;
    if (d[0] * f[0] + d[1] * f[1] + d[2] * f[2] < 0.9659258f) {               /* not facing yet: normal flight at full impulse */
        float fwd[3] = {0, 1, 0}; eng_ShipClass_StopInSystemWarp(o); eng_ShipClass_SetImpulse(o, 1.0f, fwd, 0); return 1;   /* the caller (AI) asks again next frame */
    }
    for (i = 0; i < 3; i++) v[i] = d[i] * 75.0f;
    eng_PhysicsObjectClass_SetVelocity(o, v); memset(p->accel, 0, 12);
    return 1;
}
long eng_ShipClass_IsDoingInSystemWarp(void *o) { Phys *p = ship(o); return p && p->warping; }
void eng_ShipClass_StopInSystemWarp(void *o)                                    /* 005acdb0: keep the direction, slow to the impulse max speed */
{
    Phys *p = ship(o); float l, m; int i;
    if (!p) return;
    if (p->warping) {
        m = p->typed[S_IMPULSE] ? (float)eng_ImpulseEngineSubsystem_GetMaxSpeed(p->typed[S_IMPULSE]) : 0; l = vlen(p->vel);
        if (l > 1e-6f) { if (l < m) m = l; for (i = 0; i < 3; i++) p->vel[i] = p->vel[i] / l * m; }
        { float fwd[3] = {0, 1, 0}; eng_ShipClass_SetImpulse(o, 0.0f, fwd, 0); }
        { TGEvent *ev = tg_ev_new("TGBoolEvent", 0); ev->type = 8388847u; ev->source = o; ev->dest = o; ev->v.i = 0; tg_add_event(ev); }
        { extern long eng_ShipClass_IsPlayerShip(void *); extern void *tg_sound_playing_named(const char *); extern long eng_TGSound_Stop(void *);
          void *s = eng_ShipClass_IsPlayerShip(o) ? tg_sound_playing_named("InSystemWarp") : NULL; if (s) eng_TGSound_Stop(s); }
    }
    p->warping = 0; p->warp_target = 0; p->warp_dist = 0;
}

/* ================= combat: energy weapons, beams, shields, subsystem damage, explosion =================
   EnergyWeapon/PhaserBank::UpdateCharge 0056fd70/00572b80: not firing: charge += recharge rate x condition x dt x power; firing: charge -=
   discharge rate x level factor (0.35, 1, 1 for phaser levels 0/1/2, FUN_00572b00) x dt, the beam stops at 0.
   PhaserBank::CanFire 00571e60: not while cloaked; charge not below the minimum firing charge (not firing; 00571e60 compares min > charge) or above 0 (firing); weapon condition
   x system condition above the disabled percentage. CanHit 005714a0: target direction inside the arc (width = azimuth around the bank's up
   axis, height = elevation; angles in radians from the hardpoint file). No range limit (the AI decides).
   ComputeDamage 00572a50: max damage x weapon condition x system condition x min(1, max damage distance / distance) x level factor
   (0.25, 0.5, 0.5) x dt.
   ShipClass::ApplyShieldDamage 005aed90: the hit facing's shield takes (1 - bleed) x damage; bleed = 0.6 at <= 10% shield, falling linearly
   to 0 at 60%; what the shield cannot hold, plus the bleed, goes on. DamageSubsystems 005afd70: every targetable subsystem with condition > 0
   whose damage point is within (its radius + damage radius) of the hit point (model space) takes the full damage (DamageSystem 005af4a0);
   the hull only when nothing else is hit. A destroyed critical subsystem explodes the ship (Explode 005afea0: ET_OBJECT_EXPLODING).
   Approximations (marked): the shield facing is the dominant axis of the hit direction in model space (FindShieldForLine 0056a690 intersects
   the shield shape); the beam hits the target's bounding sphere (not the mesh); no impact force; WeaponHitEvent goes through the queue. */
void prop_set_p(void *, const char *, void *); void *prop_get_p(void *, const char *); void prop_set_d(void *, const char *, double); double prop_get_d(void *, const char *);
void prop_set_i(void *, const char *, long); long prop_get_i(void *, const char *); double eng_BaseObjectClass_GetScale(void *); void *eng_BaseObjectClass_GetWorldRightTG(void *);
static const float lvl_discharge[3] = {0.35f, 1.0f, 1.0f}, lvl_damage[3] = {0.25f, 0.5f, 0.5f};
extern double eng_EnergyWeaponProperty_GetRechargeRate(void *), eng_EnergyWeaponProperty_GetNormalDischargeRate(void *), eng_EnergyWeaponProperty_GetMaxCharge(void *);
extern double eng_EnergyWeaponProperty_GetMinFiringCharge(void *), eng_EnergyWeaponProperty_GetMaxDamage(void *), eng_EnergyWeaponProperty_GetMaxDamageDistance(void *);
extern double eng_WeaponProperty_GetDamageRadiusFactor(void *), eng_SubsystemProperty_GetRadius(void *);
extern long eng_SubsystemProperty_IsTargetable(void *), eng_SubsystemProperty_IsCritical(void *);
static int lvl(Sub *w) { int l = w->parent ? w->parent->power_level : 1; return l < 0 ? 0 : l > 2 ? 2 : l; }
static float cond_pct(Sub *x) { return (float)eng_ShipSubsystem_GetConditionPercentage(x); }
static void world_to_model(void *o, const float *wp, float *mp)            /* (p - position) in the ship's axes / scale */
{
    float *pos = eng_BaseObjectClass_GetTranslate(o), f[3], u[3], r[3], d[3], s = ((float)eng_BaseObjectClass_GetScale(o)); int i;
    float *rr = eng_BaseObjectClass_GetWorldRightTG(o); memcpy(r, rr, 12); free(rr); fwd_of(o, f); up_of(o, u);
    for (i = 0; i < 3; i++) d[i] = wp[i] - pos[i]; free(pos); if (s <= 0) s = 1;
    mp[0] = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / s; mp[1] = (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / s; mp[2] = (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / s;
}
static void world_to_model_scaled(void *o, const float *wp, float *mp)       /* like world_to_model, in world units (not divided by the scale) */
{
    float s = (float)eng_BaseObjectClass_GetScale(o); int i; world_to_model(o, wp, mp); if (s <= 0) s = 1.0f;
    for (i = 0; i < 3; i++) mp[i] *= s;
}
static void send_event(unsigned type, void *src, void *dst)
{ TGEvent *e = tg_ev_new("TGEvent", 0); e->type = type; e->source = src; e->dest = dst; tg_add_event(e); }
/* ShipClass::RunDeathScript 005ac250: calls <module>.<function>(ship). The script string of the ship (SetDeathScript) is "Module.Function"; without one
   it is Effects.ObjectExploding (the stock script of the death: explosions over the wreck's time, the final blast, the sound). It also sets the wreck's
   life time when none is set (4 to 6 s). Returns 1 when the call ran. */
static int run_death_script(void *o, Phys *p)
{
    char mod[256] = "Effects", fn[256] = "ObjectExploding"; PyObject *m, *f, *a, *r, *so; int ok = 0;
    if (p->death_script && *p->death_script) {
        const char *dot = strrchr(p->death_script, '.');
        if (dot) { size_t n = (size_t)(dot - p->death_script); if (n > 255) n = 255; memcpy(mod, p->death_script, n); mod[n] = 0; snprintf(fn, sizeof fn, "%s", dot + 1); }
        else snprintf(fn, sizeof fn, "%s", p->death_script);
    }
    if (!(m = PyImport_ImportModule(mod))) { PyErr_Print(); PyErr_Clear(); return 0; }
    f = PyObject_GetAttrString(m, fn); Py_DECREF(m);
    if (!f) { PyErr_Print(); PyErr_Clear(); return 0; }
    so = tg_obj_pyobj(o); a = Py_BuildValue("(O)", so);
    r = a ? PyEval_CallObject(f, a) : NULL;
    if (r) { ok = 1; Py_DECREF(r); } else { PyErr_Print(); PyErr_Clear(); }
    Py_XDECREF(a); Py_XDECREF(so); Py_DECREF(f);
    return ok;
}
void eng_ShipClass_RunDeathScript(void *o) { Phys *p = ship(o); if (p) run_death_script(o, p); }
static void explode(void *o, void *by)                                       /* Explode 005afea0 */
{
    Phys *p = ship(o); TGEvent *e; Sub **v; int n, i;
    if (!p || p->dying || p->dead) return;
    p->dying = 1; p->target = NULL;
    tg_shieldfx_clear(o);                                                    /* ShipClass +0x244 is set to 0: the shield patches go */
    all_subs(p, &v, &n); for (i = 0; i < n; i++) { v[i]->firing = 0; v[i]->sys_firing = 0; v[i]->beam_target = NULL; } free(v);
    run_death_script(o, p);
    /* Effects.ObjectExploding sets the wreck's time when none is set. A death script that does not (or that failed) would leave the wreck for ever; the
       port then uses the stock script's range so that a battle always ends (approximation, marked). */
    if (p->lifetime >= 3.4028235e38f) p->lifetime = 4.0f + (float)(rand() % 100) / 50.0f;
    e = tg_ev_new("ObjectExplodingEvent", 0); e->type = 8388686u; e->dest = o; e->source = by; tg_add_event(e);   /* ET_OBJECT_EXPLODING */
}
static double damage_system(void *o, Sub *s, float dmg, void *by)            /* DamageSystem 005af4a0: returns the overflow */
{
    Phys *p = ship(o); float m, c, over = 0;
    if (!p || !s || !p->hurtable || p->invincible || s->invincible) return 0;
    m = s->prop ? (float)eng_SubsystemProperty_GetMaxCondition(s->prop) : 1.0f;
    c = s->condition - dmg;
    if (c <= 0) { over = -c; c = 0; }      /* not done: the +0x2e9 critical-survival and +0x44 disable-only cases */
    sub_set_condition(s, c);
    if (c <= 0 && s->prop && eng_SubsystemProperty_IsCritical(s->prop)) explode(o, by);
    else if (c <= 0) send_event(8388719u, s, o);                               /* ET_SUBSYSTEM_DESTROYED (assumed sender/destination) */
    return over;
}
double eng_ShipClass_DamageSystem(void *o, void *s, float d, PyObject *x, int f) { return damage_system(o, sub(s), d, NULL); }
double eng_ShipClass_DestroySystem(void *o, void *s) { Sub *t = sub(s); return t ? damage_system(o, t, t->condition, NULL) : 0; }   /* 005af5f0 */
static void collect(Sub *s, const float *mp, float rad, Sub ***v, int *n)     /* collectSubsystemsInRadius 005aeb90 */
{
    int i; float *q; float r;
    if (s->prop && eng_SubsystemProperty_IsTargetable(s->prop) && s->condition > 0) {
        extern void *eng_SubsystemProperty_GetPosition(void *); q = eng_SubsystemProperty_GetPosition(s->prop); r = (float)eng_SubsystemProperty_GetRadius(s->prop) + rad;
        if (q) { float dx = mp[0] - q[0], dy = mp[1] - q[1], dz = mp[2] - q[2]; if (dx * dx + dy * dy + dz * dz < r * r) { *v = realloc(*v, (*n + 1) * sizeof **v); (*v)[(*n)++] = s; } free(q); }
    }
    for (i = 0; i < s->nkids; i++) collect(s->kids[i], mp, rad, v, n);
}
static float damage_subsystems(void *o, const float *mp, float rad, float dmg, void *by, int with_hull)   /* 005afd70 */
{
    Phys *p = ship(o); Sub **v = NULL; int n = 0, i; float over = 0;
    for (i = 0; i < p->nsubs; i++) if (p->subs[i] != p->typed[S_HULL]) collect(p->subs[i], mp, rad, &v, &n);
    if (p->typed[S_HULL] && (with_hull || n == 0)) { v = realloc(v, (n + 1) * sizeof *v); v[n++] = p->typed[S_HULL]; }
    for (i = 0; i < n; i++) over += (float)damage_system(o, v[i], dmg, by);
    free(v); return over;
}
static int shield_facing(const float *mp)                                    /* FRONT, REAR, TOP, BOTTOM, LEFT, RIGHT */
{
    float ax = fabsf(mp[0]), ay = fabsf(mp[1]), az = fabsf(mp[2]);
    if (ay >= ax && ay >= az) return mp[1] >= 0 ? 0 : 1;
    if (az >= ax) return mp[2] >= 0 ? 2 : 3;
    return mp[0] < 0 ? 4 : 5;
}
/* Effects.<fn>(ship, event): callPythonModuleFunction("Effects", fn, ship, event), as ShipClass::WeaponHitHandler 005af010 calls it */
static void effects_call(const char *fn, void *ship_obj, TGEvent *ev)
{
    PyObject *m, *f, *a, *r, *so, *eo;
    if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[fx] %.2f Effects.%s\n", tg_game_time, fn);
    if (!(m = PyImport_ImportModule("Effects"))) { PyErr_Print(); PyErr_Clear(); return; }
    f = PyObject_GetAttrString(m, (char *)fn); Py_DECREF(m);
    if (!f) { PyErr_Print(); PyErr_Clear(); return; }
    so = tg_obj_pyobj(ship_obj); eo = appc_pyobj(ev, ev->o.cls->name); a = so && eo ? Py_BuildValue("(OO)", so, eo) : NULL;
    r = a ? PyEval_CallObject(f, a) : NULL;
    if (!r) { PyErr_Print(); PyErr_Clear(); }
    Py_XDECREF(r); Py_XDECREF(a); Py_XDECREF(so); Py_XDECREF(eo); Py_DECREF(f);
}
/* ApplyShieldDamage 005aed90 and ShipClass::WeaponHitHandler 005af010. The event carries the weapon's damage before the shield took its share (event +0x54),
   the object-space hit point and normal, whether the hit reached the hull (+0x58: no shield facing took it) and, for a shield hit, the shield's fraction
   left after it (+0x50). The handler then shows the hit: a shield hit gets the shield effect (FUN_005af160), and a torpedo's also Effects.TorpedoShieldHit;
   a hull hit calls Effects.PhaserHullHit (phaser) or Effects.TorpedoHullHit (torpedo and other projectiles); a blast (type 2) shows nothing. */
static void weapon_hit(void *tgt, void *weapon, void *firer, const float *hitw0, float dmg, float rad, int kind)
{
    Phys *p = ship(tgt); Sub *sh; float mp[3], nrm[3], wn[3], *c, l, frac = 0.0f, incoming = dmg, hitv[3]; const float *hitw = hitw0; int f, hull = 1, i; TGEvent *e;
    int hull_snap = 0;
    if (!p || p->dying || p->dead || dmg <= 0) return;
    world_to_model(tgt, hitw, mp);
    if (p->hurtable && (sh = p->typed[S_SHIELD]) && sh->on && !eng_ShipSubsystem_IsDisabled(sh)) {   /* ApplyShieldDamage 005aed90 */
        float mx, pct, bleed, left;
        f = shield_facing(mp); mx = sh->prop ? (float)eng_ShieldProperty_GetMaxShields(sh->prop, f) : 0; pct = mx > 0 ? sh->shields[f] / mx : 0;
        if (pct > 0.1f) {                                              /* ShipClass::RayPick 005ae... picks the shield only when the facing holds more than 0.1 (DAT_0088bf28); otherwise it picks the hull: a hull hit, no shield absorbs */
            bleed = pct >= 0.6f ? 0.0f : (1.0f - 2.0f * (pct - 0.1f)) * 0.6f;
            left = sh->shields[f] - (1.0f - bleed) * dmg;
            if (left >= 0) dmg = bleed * dmg; else { dmg = -left + bleed * dmg; left = 0; }
            sh->shields[f] = left; hull = 0; frac = mx > 0 ? left / mx : 0.0f;
        }
    }
    l = sqrtf(mp[0] * mp[0] + mp[1] * mp[1] + mp[2] * mp[2]);          /* the normal of the port's hits: radial (the hits are on the bounding sphere) */
    if (l > 1e-6f) { nrm[0] = mp[0] / l; nrm[1] = mp[1] / l; nrm[2] = mp[2] / l; } else { nrm[0] = nrm[1] = 0.0f; nrm[2] = 1.0f; }
    if (hull && kind <= 1 && l > 1e-6f) {                              /* a hull hit lands on the hull: the first triangle the hit's path crosses (the exe's hit comes from the collision with the model) */
        const char *mn = tg_space_model_of(tgt); float b[3], t, hn[3], s = (float)eng_BaseObjectClass_GetScale(tgt);
        if (s <= 0) s = 1.0f;
        for (i = 0; i < 3; i++) b[i] = mp[i] - nrm[i] * 2.0f * l;                                   /* through the model's origin and out the far side */
        if (mn && tg_model_ray_first(mn, mp, b, &t, hn)) {
            float mh[3], r[3], fw[3], up[3], tr[3]; float *pos = eng_BaseObjectClass_GetTranslate(tgt), *rr = eng_BaseObjectClass_GetWorldRightTG(tgt);
            for (i = 0; i < 3; i++) mh[i] = mp[i] + t * (b[i] - mp[i]);
            memcpy(r, rr, 12); free(rr); fwd_of(tgt, fw); up_of(tgt, up); memcpy(tr, pos, 12); free(pos);
            for (i = 0; i < 3; i++) hitv[i] = tr[i] + s * (mh[0] * r[i] + mh[1] * fw[i] + mh[2] * up[i]);      /* back to world space */
            hitw = hitv; memcpy(mp, mh, 12); hull_snap = 1;
            for (i = 0; i < 3; i++) nrm[i] = hn[i];                                                  /* the triangle's own normal (model space) */
        }
    }
    c = eng_BaseObjectClass_GetTranslate(tgt);
    for (i = 0; i < 3; i++) wn[i] = hitw[i] - c[i];
    free(c); l = sqrtf(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
    if (l > 1e-6f) { wn[0] /= l; wn[1] /= l; wn[2] /= l; } else { wn[0] = wn[1] = 0.0f; wn[2] = 1.0f; }
    e = tg_ev_new("WeaponHitEvent", 0); e->type = 8388708u; e->source = firer; e->dest = tgt;   /* ET_WEAPON_HIT */
    prop_set_p(e, "Weapon", weapon); prop_set_d(e, "Damage", incoming); prop_set_d(e, "Radius", rad); prop_set_i(e, "WeaponType", kind); prop_set_i(e, "HullHit", hull);
    prop_set_p(e, "WorldHitPoint", tg_mem_dup(hitw, 12)); prop_set_p(e, "ObjectHitPoint", tg_mem_dup(mp, 12));
    prop_set_p(e, "ObjectHitNormal", tg_mem_dup(nrm, 12)); prop_set_p(e, "WorldHitNormal", tg_mem_dup(wn, 12)); prop_set_d(e, "ShieldFraction", frac);
    tg_add_event(e);
    if (kind <= 1) {                                                    /* the handler's effects (types 0 and 1 only) */
        e->refcount++;
        if (!hull) {
            extern void *eng_ShieldProperty_GetShieldGlowColor(void *); float col[4] = {0.5f, 0.75f, 1.0f, 1.0f}; void *gc;
            if (p->typed[S_SHIELD]->prop && (gc = eng_ShieldProperty_GetShieldGlowColor(p->typed[S_SHIELD]->prop))) { memcpy(col, gc, 16); free(gc); }
            tg_shieldfx_hit(tgt, nrm, frac, col);
            if (kind == 1 && weapon && tg_is_ours(weapon) && tg_is_type(weapon, 0x8009)) effects_call("TorpedoShieldHit", tgt, e);
        } else effects_call(kind == 0 ? "PhaserHullHit" : "TorpedoHullHit", tgt, e);
        if (e->refcount > 0) e->refcount--;
    }
    if (dmg > 0) damage_subsystems(tgt, mp, rad, dmg, firer, kind != 0 && hull);
    if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[damage] weapon hit kind %d hull %d dmg %.1f\n", kind, hull, incoming);
    if (hull && kind <= 1) {                                            /* FUN_005af420: a damage volume at the hit: radius twice the event's, strength half its damage */
        float s = (float)eng_BaseObjectClass_GetScale(tgt); if (s <= 0) s = 1.0f;
        if (getenv("APPC_FX_TRACE")) fprintf(stderr, "[damage] hull hit kind %d dmg %.1f radius %.3f snapped %d\n", kind, incoming, rad, hull_snap);
        if (hull_snap) tg_damage_add_volume(tgt, mp, 2.0f * rad / s, 0.5f * incoming);
    }
}
void *eng_WeaponHitEvent_GetFiringObject(void *e) { TGEvent *x = e; return x ? x->source : NULL; }
void *eng_WeaponHitEvent_GetTargetObject(void *e) { TGEvent *x = e; return x ? x->dest : NULL; }
double eng_WeaponHitEvent_GetDamage(void *e) { return prop_get_d(e, "Damage"); }
double eng_WeaponHitEvent_GetRadius(void *e) { return prop_get_d(e, "Radius"); }
long eng_WeaponHitEvent_GetWeaponType(void *e) { return prop_get_i(e, "WeaponType"); }
long eng_WeaponHitEvent_IsHullHit(void *e) { return prop_get_i(e, "HullHit"); }
void *eng_WeaponHitEvent_GetObjectHitNormal(void *e) { return tg_mem_dup(prop_get_p(e, "ObjectHitNormal"), 12); }
void *eng_WeaponHitEvent_GetWorldHitNormal(void *e) { return tg_mem_dup(prop_get_p(e, "WorldHitNormal"), 12); }
void *eng_WeaponHitEvent_GetWorldHitPoint(void *e) { return tg_mem_dup(prop_get_p(e, "WorldHitPoint"), 12); }
void *eng_WeaponHitEvent_GetObjectHitPoint(void *e) { return tg_mem_dup(prop_get_p(e, "ObjectHitPoint"), 12); }

/* charge, arcs, beams */
static void wdir(Sub *w, float *f, float *u, float *r)                        /* the bank's axes in model space */
{
    extern void *eng_PhaserProperty_GetOrientationForward(void *), *eng_PhaserProperty_GetOrientationUp(void *);
    extern void *eng_PulseWeaponProperty_GetOrientationForward(void *), *eng_PulseWeaponProperty_GetOrientationUp(void *);
    float *a, *b; int ph = tg_is_type(&w->o, 0x802c);
    a = ph ? eng_PhaserProperty_GetOrientationForward(w->prop) : eng_PulseWeaponProperty_GetOrientationForward(w->prop);
    b = ph ? eng_PhaserProperty_GetOrientationUp(w->prop) : eng_PulseWeaponProperty_GetOrientationUp(w->prop);
    memcpy(f, a, 12); memcpy(u, b, 12); free(a); free(b);
    r[0] = f[1] * u[2] - f[2] * u[1]; r[1] = f[2] * u[0] - f[0] * u[2]; r[2] = f[0] * u[1] - f[1] * u[0];
}
static int can_hit_model(Sub *w, float *mp);
static int can_hit(void *o, Sub *w, void *tgt)                               /* CanHit 005714a0 (arc only) */
{
    float *tp, mp[3];
    if (!tgt || !w->prop) return 0;
    tp = eng_BaseObjectClass_GetTranslate(tgt); world_to_model(o, tp, mp); free(tp);
    return can_hit_model(w, mp);
}
static int can_hit_model(Sub *w, float *mp)                                  /* mp: the target in the ship's model space */
{
    extern double eng_PhaserProperty_GetArcWidthAngleMin(void *), eng_PhaserProperty_GetArcWidthAngleMax(void *), eng_PhaserProperty_GetArcHeightAngleMin(void *), eng_PhaserProperty_GetArcHeightAngleMax(void *);
    extern double eng_PulseWeaponProperty_GetArcWidthAngleMin(void *), eng_PulseWeaponProperty_GetArcWidthAngleMax(void *), eng_PulseWeaponProperty_GetArcHeightAngleMin(void *), eng_PulseWeaponProperty_GetArcHeightAngleMax(void *);
    float f[3], u[3], r[3], l, az, el; int ph = tg_is_type(&w->o, 0x802c); double wmin, wmax, hmin, hmax;
    if (!w->prop) return 0;
    mp = (float[3]){mp[0], mp[1], mp[2]};
    { extern void *eng_SubsystemProperty_GetPosition(void *); float *q = eng_SubsystemProperty_GetPosition(w->prop); if (q) { mp[0] -= q[0]; mp[1] -= q[1]; mp[2] -= q[2]; free(q); } }
    l = vlen(mp); if (l < 1e-6f) return 1;
    wdir(w, f, u, r);
    az = atan2f((mp[0] * r[0] + mp[1] * r[1] + mp[2] * r[2]) / l, (mp[0] * f[0] + mp[1] * f[1] + mp[2] * f[2]) / l);
    el = asinf(fmaxf(-1.0f, fminf(1.0f, (mp[0] * u[0] + mp[1] * u[1] + mp[2] * u[2]) / l)));
    wmin = ph ? eng_PhaserProperty_GetArcWidthAngleMin(w->prop) : eng_PulseWeaponProperty_GetArcWidthAngleMin(w->prop);
    wmax = ph ? eng_PhaserProperty_GetArcWidthAngleMax(w->prop) : eng_PulseWeaponProperty_GetArcWidthAngleMax(w->prop);
    hmin = ph ? eng_PhaserProperty_GetArcHeightAngleMin(w->prop) : eng_PulseWeaponProperty_GetArcHeightAngleMin(w->prop);
    hmax = ph ? eng_PhaserProperty_GetArcHeightAngleMax(w->prop) : eng_PulseWeaponProperty_GetArcHeightAngleMax(w->prop);
    return az >= wmin && az <= wmax && el >= hmin && el <= hmax;
}
/* the arc test for a world point (PhaserBank::CanHit 00571410 / the weapons display arc indicators): 1 when the point is inside the bank's arc */
int tg_weapon_point_in_arc(void *o, void *w, const float *world)
{
    Sub *x = sub(w); float mp[3];
    if (!x || !o) return 0;
    world_to_model(o, (float *)world, mp);
    return can_hit_model(x, mp);
}
long eng_PhaserBank_CanHit(void *p, void *world) { Sub *x = sub(p); return x && x->ship && world && tg_weapon_point_in_arc(x->ship, x, (const float *)world); }   /* 00571410: a world point */
static int can_fire(void *o, Sub *w)                                         /* PhaserBank::CanFire 00571e60 */
{
    extern double eng_SubsystemProperty_GetDisabledPercentage(void *);
    if (eng_ShipClass_IsCloaked(o) || !w->prop) return 0;
    if (!w->firing ? w->charge < (float)eng_EnergyWeaponProperty_GetMinFiringCharge(w->prop) : w->charge <= 0) return 0;
    return cond_pct(w) * (w->parent ? cond_pct(w->parent) : 1.0f) > (float)eng_SubsystemProperty_GetDisabledPercentage(w->prop);
}
static void update_charge(Sub *w, float dt, float power)
{
    float mx = (float)eng_EnergyWeaponProperty_GetMaxCharge(w->prop);
    if (w->firing) { w->charge -= (float)eng_EnergyWeaponProperty_GetNormalDischargeRate(w->prop) * lvl_discharge[lvl(w)] * dt; if (w->charge <= 0) { w->charge = 0; w->firing = 0; w->beam_target = NULL; } }
    else { w->charge += (float)eng_EnergyWeaponProperty_GetRechargeRate(w->prop) * cond_pct(w) * dt * power; if (w->charge > mx) w->charge = mx; }
}
static void beam(void *o, Sub *w, float dt)                                  /* UpdateBeam 00572440 / ApplyBeamDamage 005722b0 / ComputeDamage 00572a50 */
{
    void *t = w->beam_target; float *a = eng_BaseObjectClass_GetTranslate(o), *b = eng_BaseObjectClass_GetTranslate(t), d[3], l, hit[3], dist, md, dmg; int i;
    extern double eng_ObjectClass_GetRadius(void *);
    for (i = 0; i < 3; i++) d[i] = b[i] - a[i];
    l = vlen(d); dist = l - (float)eng_ObjectClass_GetRadius(t); if (dist < 0) dist = 0;
    for (i = 0; i < 3; i++) hit[i] = l > 1e-6f ? a[i] + d[i] / l * dist : b[i];
    free(a); free(b);
    md = (float)eng_EnergyWeaponProperty_GetMaxDamageDistance(w->prop);
    dmg = (float)eng_EnergyWeaponProperty_GetMaxDamage(w->prop) * cond_pct(w) * (w->parent ? cond_pct(w->parent) : 1.0f) * (dist > md && dist > 0 ? md / dist : 1.0f) * lvl_damage[lvl(w)] * dt;
    weapon_hit(t, w, o, hit, dmg, (float)eng_WeaponProperty_GetDamageRadiusFactor(w->prop), 0);
}
static void phaser_sounds(void *o, Sub *w, int on), tractor_sounds(void *o, Sub *w, int on), shot_sound(void *o, const char *name, float other_prio), ship_stop_weapon_sounds(Phys *p), cloak_sound(Sub *c, const char *name);
static void update_pulse(void *o, Sub *w, void *t); static void update_tractor(void *o, Sub *w, void *t, float dt); static void torpedo_step(void *o, Sub *s, void *t); static void tube_reload_tick(Sub *s, Sub *w, float dt);
static void update_weapon_system(void *o, Sub *s, float dt)
{
    int i; float power = s->on ? s->pw_norm_pct : 0;                          /* received power relative to normal (+0x98) */
    for (i = 0; i < s->nkids; i++) {
        Sub *w = s->kids[i]; void *t = s->sys_firing ? s->fire_target : NULL;
        if (w->prop && tg_is_type(&w->o, 0x802f)) { tube_reload_tick(s, w, dt); continue; }
        if (!w->prop || !tg_is_type(&w->o, 0x802b)) continue;              /* energy weapons (phasers, pulse, tractors) */
        update_charge(w, dt, power);
        if (tg_is_type(&w->o, 0x802d)) { update_pulse(o, w, t); continue; }
        if (tg_is_type(&w->o, 0x802e)) { update_tractor(o, w, t, dt); tractor_sounds(o, w, w->firing); continue; }
        if (!tg_is_type(&w->o, 0x802c)) continue;                          /* beams: phasers */
        if (w->firing && (!t || t != w->beam_target || ship(t) == NULL || ship(t)->dying || !can_hit(o, w, t))) { w->firing = 0; w->beam_target = NULL; }
        if (!w->firing && t && ship(t) && !ship(t)->dying && can_fire(o, w) && can_hit(o, w, t)) { w->firing = 1; w->beam_target = t; }
        if (w->firing) beam(o, w, dt);
        phaser_sounds(o, w, w->firing);
    }
    if (tg_is_type(&s->o, 0x801e)) torpedo_step(o, s, s->sys_firing ? s->fire_target : NULL);          /* the torpedo system: one step of the firing chain */
}
void tg_combat_tick(void *o, float dt)
{
    Phys *p = ship(o); int k;
    if (!p) return;
    if (p->dead || p->dying) { ship_stop_weapon_sounds(p); if (p->dead) return; }
    for (k = 0; k < S_N; k++) if (p->typed[k] && tg_is_type(&p->typed[k]->o, 0x801d)) update_weapon_system(o, p->typed[k], dt);
}
void eng_WeaponSystem_StartFiring(void *s, void *t, void *off) { Sub *x = sub(s); if (!x) return; x->fire_target = t; x->sys_firing = t != NULL; if (off) memcpy(x->fire_off, off, 12); }   /* 00584390 */
void eng_WeaponSystem_StopFiring(void *s) { Sub *x = sub(s); int i; if (!x) return; x->sys_firing = 0; x->fire_target = NULL; for (i = 0; i < x->nkids; i++) { x->kids[i]->firing = 0; x->kids[i]->beam_target = NULL; } }
void eng_WeaponSystem_StopFiringAtTarget(void *s, void *t) { Sub *x = sub(s); if (x && x->fire_target == t) eng_WeaponSystem_StopFiring(s); }
void eng_ShipClass_StopFiringWeapons(void *o) { Phys *p = ship(o); int k; if (p) for (k = 0; k < S_N; k++) if (p->typed[k] && tg_is_type(&p->typed[k]->o, 0x801d)) eng_WeaponSystem_StopFiring(p->typed[k]); }
double eng_EnergyWeapon_GetChargeLevel(void *s) { Sub *x = sub(s); return x ? x->charge : 0; }
void eng_EnergyWeapon_SetChargeLevel(void *s, float v) { Sub *x = sub(s); if (x) x->charge = v; }
double eng_EnergyWeapon_GetChargePercentage(void *s) { Sub *x = sub(s); float m; if (!x || !x->prop || !x->parent || !x->parent->on || eng_ShipSubsystem_IsDisabled(x)) return 0; m = (float)eng_EnergyWeaponProperty_GetMaxCharge(x->prop); return m > 0 ? x->charge / m : 0; }
void eng_EnergyWeapon_UpdateCharge(void *s, float dt, float power) { Sub *x = sub(s); if (x && x->prop) update_charge(x, dt, power); }
void eng_PhaserSystem_SetPowerLevel(void *s, int v) { Sub *x = sub(s); if (x) x->power_level = v; }
long eng_PhaserSystem_GetPowerLevel(void *s) { Sub *x = sub(s); return x ? x->power_level : 0; }
double eng_ShieldClass_GetShieldPercentage(void *s)
{ Sub *x = sub(s); double c = 0, m = 0; int i; if (!x || !x->prop) return 0; for (i = 0; i < 6; i++) { c += x->shields[i]; m += eng_ShieldProperty_GetMaxShields(x->prop, i); } return m > 0 ? c / m : 0; }
long eng_ShieldClass_IsShieldBreached(void *s, int i) { Sub *x = sub(s); return x && i >= 0 && i < 6 && x->shields[i] <= 0; }
long eng_ShieldClass_IsAnyShieldBreached(void *s) { int i; for (i = 0; i < 6; i++) if (eng_ShieldClass_IsShieldBreached(s, i)) return 1; return 0; }
long eng_ShieldClass_GetOppositeShield(void *s, int i) { return i ^ 1; }          /* 0056aad0: FRONT<->REAR, TOP<->BOTTOM, LEFT<->RIGHT */

/* ================= projectiles: Torpedo (pulse weapon shots now) =================
   Torpedo::Create(script, position, ?) runs <script>.Create(torpedo) (Tactical/Projectiles/*.py: damage, damage radius, guidance lifetime,
   max angular acceleration, lifetime, model). PulseWeapon::Fire 005752a0 / CreateProjectile 00576080: the shot starts at the weapon's world
   position with velocity = launch speed x direction to the target point + the ship's velocity; the weapon's charge drops by the power cost
   (script GetPowerCost); the cooldown starts; ET_WEAPON_FIRED goes to the ship.
   Approximations (marked): collision is point against each ship's bounding sphere (not the mesh); guidance turns the velocity toward the
   target by at most max angular acceleration x dt per frame; the model is not created; GetDamageScale/GetPowerScaled of the weapon = 1. */
static int torp_seq;
void *eng_Torpedo_Create(char *script, void *pos, int flag)
{
    extern long eng_SetClass_AddObjectToSet(void *, void *, char *); char nm[32]; void *o = tg_space_new_obj("Torpedo", NULL); Phys *p = phys(o); PyObject *m, *f, *args, *r, *self;
    p->module = dupz(script); p->life = 10.0f; p->dmg_radius = 0.1f;
    if (pos) eng_BaseObjectClass_SetTranslate(o, pos);
    snprintf(nm, sizeof nm, "Torpedo%d", ++torp_seq); (void)nm;
    if (script && (m = PyImport_ImportModule(script))) {
        if ((f = PyObject_GetAttrString(m, "Create"))) { self = appc_pyobj(o, "Torpedo"); args = Py_BuildValue("(O)", self); Py_DECREF(self); r = PyEval_CallObject(f, args); Py_DECREF(args); Py_DECREF(f); if (!r) { PyErr_Print(); PyErr_Clear(); } Py_XDECREF(r); }
        else PyErr_Clear();
        { float v; if (tg_py_call_module(script, "GetLaunchSpeed", 'f', &v) == 0) p->launch_speed = v; }
        Py_DECREF(m);
    } else PyErr_Clear();
    return o;
}
static Phys *torp(void *o) { return o && tg_is_ours(o) && tg_is_type(o, 0x8009) ? phys(o) : NULL; }
void *eng_Torpedo_Cast(void *o) { return torp(o) ? o : NULL; }
double eng_Torpedo_GetMaxLifetime(void) { return 10.0; }
const char *eng_Torpedo_GetModuleName(void *o) { Phys *p = torp(o); return p ? p->module : NULL; }
/* Texture names of the projectile looks: the names the scripts pass, copied once and kept (a shot's name lives as long as the program). */
static const char *fx_intern(const char *s)
{
    static const char **names; static int n;
    int i; char *c;
    if (!s || !*s) return NULL;
    for (i = 0; i < n; i++) if (!strcmp(names[i], s)) return names[i];
    c = strdup(s); names = realloc(names, (n + 1) * sizeof *names); names[n++] = c;
    return c;
}
/* Projectile looks, kept for the renderer (tg_fx_shots, tg_fx_rays). The exe builds NetImmerse nodes from these in Torpedo::CreateTorpedoModel
   (004f4630) and Torpedo::CreateDisruptorModel (004dd250). The mapping is read from the argument pushes in the exe's assembly, not from names:
   CreateTorpedoModel(a1 core texture, a2 core colour, a3 core quad side and flare ray half width factor (half width 0.4 * a3), a4 flare spin rate
   (radians a second about the torpedo's y axis), a5 glow texture, a6 glow colour (also the point light colour), a7 glow pulse rate, a8 glow side
   at sin 0 (minimum), a9 glow side at sin 1 (maximum), a10 flare texture, a11 flare colour, a12 number of rays, a13 ray length, a14 ray life
   in seconds (the gap between lives is 0.5 to 1 times twice that).
   CreateDisruptorModel(shell colour, core colour, length, radius, [columns 12, core radius factor 0.5, core length factor 0.8]).
   The torpedo's point light (a6 colour, constant 0, linear a3, quadratic 0, dimmer 1) is a child of the torpedo node and is never attached as an
   effect, so it lights nothing (the phaser beam lights are attached to the ship with AttachEffect, 004e6e90; these are not). */
static void fx_color(float *d, void *c) { if (c) memcpy(d, c, 16); else { d[0] = d[1] = d[2] = d[3] = 1.0f; } }
void eng_Torpedo_CreateTorpedoModel(void *o, char *core_tex, void *core, float core_size, float spin, char *glow_tex, void *glow, float rate, float gmin, float gmax, char *flare_tex, void *flare, int nflares, float ray_len, float ray_life)
{
    Phys *p = phys(o); if (!p) return;
    p->fx_kind = 1; fx_color(p->fx_core, core); fx_color(p->fx_shell, glow); fx_color(p->fx_flare, flare);
    p->fx_core_tex = fx_intern(core_tex); p->fx_glow_tex = fx_intern(glow_tex); p->fx_flare_tex = fx_intern(flare_tex);
    p->fx_core_size = fabsf(core_size); p->fx_glow_rate = rate; p->fx_glow_min = gmin; p->fx_glow_max = gmax; p->fx_len = 0;
    p->fx_spin = spin; p->fx_flare_w = 0.4f * fabsf(core_size); p->fx_flare_len = ray_len; p->fx_flare_life = ray_life;
    free(p->fx_ray_start); free(p->fx_ray_pt); p->fx_ray_start = NULL; p->fx_ray_pt = NULL; p->fx_ray_init = 0;
    p->fx_nflares = nflares > 0 ? (nflares > 64 ? 64 : nflares) : 0;
    if (p->fx_nflares) { p->fx_ray_start = calloc(p->fx_nflares, sizeof(float)); p->fx_ray_pt = calloc(p->fx_nflares * 12, sizeof(float)); }
    p->fx_rng = (unsigned)(size_t)p * 2654435761u + 12345u;
}
void eng_Torpedo_CreateDisruptorModel(void *o, void *shell, void *core, float len, float width, int sides, float in_r, float in_l)
{
    Phys *p = phys(o); if (!p) return;
    p->fx_kind = 2; fx_color(p->fx_shell, shell); fx_color(p->fx_core, core); p->fx_core_tex = p->fx_glow_tex = NULL;
    p->fx_len = len; p->fx_width = width; p->fx_sides = sides > 0 ? (sides > 64 ? 64 : sides) : 12;      /* the wrapper passes 0 for an argument the script left out; the exe's defaults are 12, 0.5 and 0.8 */
    p->fx_in_r = in_r != 0.0f ? in_r : 0.5f; p->fx_in_l = in_l != 0.0f ? in_l : 0.8f; p->fx_nflares = 0;
}
double eng_Torpedo_GetLaunchSpeed(void *o) { Phys *p = torp(o); return p ? p->launch_speed : 0; }
const char *eng_Torpedo_GetLaunchSound(void *o) { return NULL; }
void eng_Torpedo_SetParent(void *o, long id) { Phys *p = torp(o); if (p) p->parent_id = (int)id; }
void eng_Torpedo_SetTarget(void *o, long id) { Phys *p = torp(o); if (p) p->target_id = (int)id; }
void eng_Torpedo_SetTargetOffset(void *o, void *v) { Phys *p = torp(o); if (p && v) memcpy(p->tgt_off, v, 12); }
void eng_Torpedo_SetMaxAngularAccel(void *o, float v) { Phys *p = torp(o); if (p) p->max_ang = v; }
void eng_Torpedo_SetGuidanceLifetime(void *o, float v) { Phys *p = torp(o); if (p) p->guidance = v; }
void eng_Torpedo_SetDamage(void *o, float v) { Phys *p = torp(o); if (p) p->dmg = v; }
void eng_Torpedo_SetLifetime(void *o, float v) { Phys *p = torp(o); if (p) p->life = v; }
long eng_Torpedo_GetTargetID(void *o) { Phys *p = torp(o); return p ? p->target_id : 0; }
long eng_Torpedo_GetParentID(void *o) { Phys *p = torp(o); return p ? p->parent_id : 0; }
void eng_Torpedo_SetPlayerID(void *o, int v) { Phys *p = torp(o); if (p) p->player_id = v; }
long eng_Torpedo_GetPlayerID(void *o) { Phys *p = torp(o); return p ? p->player_id : 0; }
double eng_Torpedo_GetMaxAngularAccel(void *o) { Phys *p = torp(o); return p ? p->max_ang : 0; }
double eng_Torpedo_GetDamage(void *o) { Phys *p = torp(o); return p ? p->dmg : 0; }
double eng_Torpedo_GetLifetime(void *o) { Phys *p = torp(o); return p ? p->life : 0; }
double eng_Torpedo_GetGuidanceLifeTime(void *o) { Phys *p = torp(o); return p ? p->guidance : 0; }
void eng_WeaponPayload_SetDamageRadiusFactor(void *o, float v) { Phys *p = torp(o); if (p) p->dmg_radius = v; }
double eng_WeaponPayload_GetDamageRadiusFactor(void *o) { Phys *p = torp(o); return p ? p->dmg_radius : 0; }
static void torp_remove(void *o)
{
    extern void *eng_SetClass_RemoveObjectFromSet(void *, char *); Phys *p = phys(o); void *s = eng_BaseObjectClass_GetContainingSet(o); const char *n = eng_BaseObjectClass_GetName(o);
    p->dead = 1; if (s && n) { char *c = strdup(n); eng_SetClass_RemoveObjectFromSet(s, c); free(c); }
    ((TGObject *)o)->dead = 1;
}
static void *torp_hit_obj; static float torp_pos[3]; static int torp_parent; void tg_space_each_ship_check(void);
static void torp_check(void *s)
{
    extern double eng_ObjectClass_GetRadius(void *); float *q, d2, r;
    if (torp_hit_obj || s == NULL || (int)((TGObject *)s)->id == torp_parent || !ship(s) || ship(s)->dead || ship(s)->dying) return;
    q = eng_BaseObjectClass_GetTranslate(s); r = (float)eng_ObjectClass_GetRadius(s);
    d2 = (q[0] - torp_pos[0]) * (q[0] - torp_pos[0]) + (q[1] - torp_pos[1]) * (q[1] - torp_pos[1]) + (q[2] - torp_pos[2]) * (q[2] - torp_pos[2]);
    if (d2 < r * r) torp_hit_obj = s;
    free(q);
}
int tg_torpedo_tick(void *o, float dt)                                     /* 0 = removed */
{
    Phys *p = torp(o); float *pos; void *tg;
    if (!p || p->dead) return 0;
    p->age += dt;
    if (p->age >= p->life) { torp_remove(o); return 0; }
    if (p->target_id && p->age < p->guidance && (tg = tg_find((unsigned)p->target_id)) && p->max_ang > 0) {   /* guidance */
        float *a = eng_BaseObjectClass_GetTranslate(o), *b = eng_BaseObjectClass_GetTranslate(tg), d[3], sp = vlen(p->vel), ax[3], ang, c; int i;
        for (i = 0; i < 3; i++) d[i] = b[i] + p->tgt_off[i] - a[i];
        free(a); free(b);
        if (sp > 1e-6f && vlen(d) > 1e-6f) {
            float v[3] = {p->vel[0] / sp, p->vel[1] / sp, p->vel[2] / sp}, l = vlen(d); for (i = 0; i < 3; i++) d[i] /= l;
            ax[0] = v[1] * d[2] - v[2] * d[1]; ax[1] = v[2] * d[0] - v[0] * d[2]; ax[2] = v[0] * d[1] - v[1] * d[0]; c = vlen(ax);
            ang = atan2f(c, v[0] * d[0] + v[1] * d[1] + v[2] * d[2]); if (ang > p->max_ang * dt) ang = p->max_ang * dt;
            if (c > 1e-6f) { for (i = 0; i < 3; i++) ax[i] /= c; rot_vec(p->vel, ax, ang); }
        }
    }
    pos = eng_BaseObjectClass_GetTranslate(o); memcpy(torp_pos, pos, 12); free(pos);
    torp_hit_obj = NULL; torp_parent = p->parent_id; tg_space_each_ship_check();
    if (torp_hit_obj) {
        void *firer = p->parent_id ? tg_find((unsigned)p->parent_id) : NULL;
        weapon_hit(torp_hit_obj, o, firer, torp_pos, p->dmg, p->dmg_radius, 1);
        torp_remove(o); return 0;
    }
    return 1;
}
void tg_space_each_ship_check(void) { void tg_space_each(unsigned ct, void (*fn)(void *)); tg_space_each(0x8008, torp_check); }

/* pulse weapons (PulseWeapon::CanFire 00575180, Fire 005752a0) */
static void fire_pulse(void *o, Sub *w, void *t)
{
    extern long eng_SetClass_AddObjectToSet(void *, void *, char *); extern const char *eng_PulseWeaponProperty_GetModuleName(void *); extern void *eng_SubsystemProperty_GetPosition(void *);
    float *wp = eng_SubsystemProperty_GetPosition(w->prop), *sp = eng_BaseObjectClass_GetTranslate(o), *tp = eng_BaseObjectClass_GetTranslate(t), r[3], f[3], u[3], start[3], d[3], l, cost = 0; char nm[32]; void *tor; Phys *tq; int i;
    float *rr = eng_BaseObjectClass_GetWorldRightTG(o); memcpy(r, rr, 12); free(rr); fwd_of(o, f); up_of(o, u);
    for (i = 0; i < 3; i++) start[i] = sp[i] + (wp ? wp[0] * r[i] + wp[1] * f[i] + wp[2] * u[i] : 0);
    for (i = 0; i < 3; i++) d[i] = tp[i] - start[i];
    free(wp); free(sp); free(tp); l = vlen(d); if (l < 1e-6f) return;
    tor = eng_Torpedo_Create((char *)eng_PulseWeaponProperty_GetModuleName(w->prop), start, 0); tq = phys(tor);
    for (i = 0; i < 3; i++) tq->vel[i] = d[i] / l * tq->launch_speed + ship(o)->vel[i];
    tq->parent_id = (int)((TGObject *)o)->id; tq->target_id = (int)((TGObject *)t)->id;
    snprintf(nm, sizeof nm, "Torpedo%d", torp_seq); eng_SetClass_AddObjectToSet(eng_BaseObjectClass_GetContainingSet(o), tor, nm);
    if (tq->module) tg_py_call_module(tq->module, "GetPowerCost", 'f', &cost);
    w->charge -= cost; if (w->charge < 0) w->charge = 0; w->beam_time = (float)tg_game_time;
    shot_sound(o, eng_EnergyWeaponProperty_GetFireSound(w->prop), 0.5f);          /* PulseWeapon::CreateProjectile 00576080 */
    send_event(8388732u, w, o);                                                   /* ET_WEAPON_FIRED */
}
static void update_pulse(void *o, Sub *w, void *t)
{
    extern double eng_PulseWeaponProperty_GetCooldownTime(void *);
    if (!t || !ship(t) || ship(t)->dying || eng_ShipClass_IsCloaked(o)) return;
    if (w->charge < (float)eng_EnergyWeaponProperty_GetMinFiringCharge(w->prop)) return;
    if (w->beam_time > 0 && tg_game_time - w->beam_time < eng_PulseWeaponProperty_GetCooldownTime(w->prop)) return;
    if (!can_hit(o, w, t)) return;
    fire_pulse(o, w, t);
}

/* ================= torpedo tubes (TorpedoTube::Update 0057c6a0, CanFire 0057d780, ReloadTorpedo 0057d8a0, Fire 0057c770) =================
   A tube holds up to MaxReady torpedoes; each fired slot reloads after ReloadDelay x system normal power (the slot timer runs only from a
   shot); a reload needs ammo of the current type that is not already loaded. Fire: not cloaked, a torpedo ready, at least 0.5 s since the
   system's last shot, ImmediateDelay since this tube's last shot, condition above the disabled percentage, target inside the tube's arc;
   the torpedo comes from the current ammo type's script (Torpedo::Create), starts at the tube along the tube direction at launch speed and
   homes on the target. Events: ET_TORPEDO_RELOAD (to the tube), ET_TORPEDO_FIRED, ET_WEAPON_FIRED (to the ship), ET_CANT_FIRE.
   Approximation (marked): group/skew fire modes are not done; the tube arc check uses the tube direction as forward with a 90-degree cone. */
static int ammo_loaded(Sub *s) { int i, n = 0; for (i = 0; i < s->nkids; i++) if (tg_is_type(&s->kids[i]->o, 0x802f)) n += s->kids[i]->ready; return n; }
static void tube_reload(Sub *s, Sub *w)
{
    extern long eng_TorpedoTubeProperty_GetMaxReady(void *); extern void *eng_TorpedoSystem_GetAmmoType(void *, int); int i, best = -1; float bt = -1;
    if (w->ready >= eng_TorpedoTubeProperty_GetMaxReady(w->prop)) return;
    if (!eng_TorpedoSystem_GetAmmoType(s, s->cur_ammo) || s->ammo_left[s->cur_ammo] - ammo_loaded(s) <= 0) return;
    w->ready++;
    for (i = 0; i < 8 && i < eng_TorpedoTubeProperty_GetMaxReady(w->prop); i++) if (w->reload[i] > bt) { bt = w->reload[i]; best = i; }
    if (best >= 0) w->reload[best] = -1.0f;
    { TGEvent *e = tg_ev_new("TGIntEvent", 0); e->type = (unsigned)tg_const("ET_TORPEDO_RELOAD"); e->dest = w; tg_add_event(e); }
}
void eng_TorpedoTube_ReloadTorpedo(void *p) { Sub *w = sub(p); if (w && w->parent) tube_reload(w->parent, w); }
static int tube_can_hit(void *o, Sub *w, void *t)                            /* direction to the target within 90 degrees of the tube */
{
    extern void *eng_TorpedoTubeProperty_GetDirection(void *); float *d = eng_TorpedoTubeProperty_GetDirection(w->prop), *tp, mp[3], l, c;
    tp = eng_BaseObjectClass_GetTranslate(t); world_to_model(o, tp, mp); free(tp);
    l = vlen(mp); c = l > 1e-6f && d ? (mp[0] * d[0] + mp[1] * d[1] + mp[2] * d[2]) / (l * vlen(d)) : 1.0f; free(d);
    return c >= 0.0f;
}
static void tube_reload_tick(Sub *s, Sub *w, float dt)                       /* the reload clock of a tube's empty slots */
{
    extern long eng_TorpedoTubeProperty_GetMaxReady(void *); extern double eng_TorpedoTubeProperty_GetReloadDelay(void *);
    int i, mx = (int)eng_TorpedoTubeProperty_GetMaxReady(w->prop);
    if (mx > 8) mx = 8;
    if (w->ready < mx) for (i = 0; i < mx; i++) if (w->reload[i] >= 0) { w->reload[i] += (s->on ? s->pw_norm_pct : 0) * dt; if (w->reload[i] > (float)eng_TorpedoTubeProperty_GetReloadDelay(w->prop)) tube_reload(s, w); }
}
/* 1 the tube may launch at the target now, 0 it may not (empty, too soon, damaged), -1 the target is outside its arc */
static int tube_launch_state(void *o, Sub *s, Sub *w, void *t, float now)
{
    extern double eng_TorpedoTubeProperty_GetImmediateDelay(void *), eng_SubsystemProperty_GetDisabledPercentage(void *);
    if (w->ready < 1 || now - w->last_fire < (float)eng_TorpedoTubeProperty_GetImmediateDelay(w->prop)) return 0;
    if (cond_pct(w) * cond_pct(s) <= (float)eng_SubsystemProperty_GetDisabledPercentage(w->prop)) return 0;
    return tube_can_hit(o, w, t) ? 1 : -1;
}
static void tube_launch(void *o, Sub *s, Sub *w, void *t, float now)
{
    extern long eng_TorpedoTubeProperty_GetMaxReady(void *);
    extern void *eng_TorpedoTubeProperty_GetDirection(void *), *eng_SubsystemProperty_GetPosition(void *);
    int i, mx = (int)eng_TorpedoTubeProperty_GetMaxReady(w->prop);
    if (mx > 8) mx = 8;
    {   /* CreateTorpedo 0057cd90 */
        extern void *eng_TorpedoSystem_GetAmmoType(void *, int); extern const char *eng_TorpedoAmmoType_GetTorpedoScript(void *); extern long eng_SetClass_AddObjectToSet(void *, void *, char *);
        void *am = eng_TorpedoSystem_GetAmmoType(s, s->cur_ammo); const char *sc = am ? eng_TorpedoAmmoType_GetTorpedoScript(am) : NULL;
        float *wp = eng_SubsystemProperty_GetPosition(w->prop), *dir = eng_TorpedoTubeProperty_GetDirection(w->prop), *sp = eng_BaseObjectClass_GetTranslate(o), r[3], f[3], u[3], start[3], wd[3], l; char nm[32]; void *tor; Phys *tq;
        float *rr = eng_BaseObjectClass_GetWorldRightTG(o); memcpy(r, rr, 12); free(rr); fwd_of(o, f); up_of(o, u);
        for (i = 0; i < 3; i++) { start[i] = sp[i] + (wp ? wp[0] * r[i] + wp[1] * f[i] + wp[2] * u[i] : 0); wd[i] = dir ? dir[0] * r[i] + dir[1] * f[i] + dir[2] * u[i] : f[i]; }
        free(wp); free(dir); free(sp); l = vlen(wd); if (l > 1e-6f) for (i = 0; i < 3; i++) wd[i] /= l;
        if (!sc) return;
        tor = eng_Torpedo_Create((char *)sc, start, 0); tq = phys(tor);
        for (i = 0; i < 3; i++) tq->vel[i] = wd[i] * tq->launch_speed + ship(o)->vel[i];
        tq->parent_id = (int)((TGObject *)o)->id; tq->target_id = (int)((TGObject *)t)->id; memcpy(tq->tgt_off, s->fire_off, 12);
        snprintf(nm, sizeof nm, "Torpedo%d", torp_seq); eng_SetClass_AddObjectToSet(eng_BaseObjectClass_GetContainingSet(o), tor, nm);
        shot_sound(o, am ? eng_TorpedoAmmoType_GetLaunchSound(am) : NULL, 0.6f);        /* TorpedoTube::CreateTorpedo 0057cd90: the ammo type's launch sound */
        w->ready--; s->ammo_left[s->cur_ammo]--; w->last_fire = now; s->last_fire = now;
        for (i = 0; i < mx; i++) if (w->reload[i] <= 0) { w->reload[i] = 0; break; }
        send_event((unsigned)tg_const("ET_TORPEDO_FIRED"), tor, o); send_event(8388732u, w, o);   /* ET_WEAPON_FIRED */
    }
}
/* The firing chains (WeaponSystem::UpdateWeapons 00584930): the chain of the current mode lists weapon groups; one step of the fire button fires the
   tubes of one group (the one after the group fired last that has a tube ready), group 0 (no group) means any one tube, taken in turn.
   Approximations (marked): the exe's per-weapon cooldown (0.33 s) is the system gap of half a second here; the tubes of a group launch together. */
static int chain_next_group(unsigned mask, int g)
{
    int k; if (!mask) return 0;
    for (k = g + 1; k <= 9; k++) if (mask & (1u << (k - 1))) return k;
    for (k = 1; k <= 9; k++) if (mask & (1u << (k - 1))) return k;
    return 0;
}
static void torpedo_step(void *o, Sub *s, void *t)
{
    extern long eng_WeaponProperty_GetGroups(void *);
    float now = (float)tg_game_time; unsigned mask; int mode, g, start, i, k, fired, cant = 0;
    if (!t || !ship(t) || ship(t)->dying || eng_ShipClass_IsCloaked(o) || s->nkids < 1) return;
    if (now - s->last_fire < 0.5f) return;
    chains_load(s);
    mode = s->chain_mode < s->nchains ? s->chain_mode : 0; mask = s->nchains ? s->chain_mask[mode] : 0;
    g = mask ? (s->chain_group > 0 && (mask & (1u << (s->chain_group - 1))) ? s->chain_group : chain_next_group(mask, 0)) : 0;
    start = g;
    do {
        fired = 0;
        for (k = 0; k < s->nkids; k++) {
            Sub *w; int st;
            i = (s->chain_last + 1 + k) % s->nkids; w = s->kids[i];
            if (!w->prop || !tg_is_type(&w->o, 0x802f)) continue;
            if (g && !(eng_WeaponProperty_GetGroups(w->prop) & (1L << (g - 1)))) continue;
            st = tube_launch_state(o, s, w, t, now);
            if (st < 0) cant = 1;
            if (st > 0) { tube_launch(o, s, w, t, now); fired++; s->chain_last = i; if (!g) break; }
        }
        if (fired) { s->chain_group = chain_next_group(mask, g); return; }
        g = chain_next_group(mask, g);
    } while (g != start);
    if (cant) send_event((unsigned)tg_const("ET_CANT_FIRE"), s, o);
}
long eng_TorpedoTube_GetNumReady(void *p) { Sub *w = sub(p); return w ? w->ready : 0; }
void eng_TorpedoTube_SetNumReady(void *p, int n) { Sub *w = sub(p); if (w) w->ready = n; }
void eng_TorpedoTube_IncNumReady(void *p) { Sub *w = sub(p); if (w) w->ready++; }
void eng_TorpedoTube_DecNumReady(void *p) { Sub *w = sub(p); if (w && w->ready > 0) w->ready--; }
double eng_TorpedoTube_GetLastFireTime(void *p) { Sub *w = sub(p); return w ? w->last_fire : 0; }
void eng_TorpedoTube_SetLastFireTime(void *p, float v) { Sub *w = sub(p); if (w) w->last_fire = v; }
void eng_TorpedoTube_UnloadTorpedo(void *p) { Sub *w = sub(p); if (w && w->ready > 0) w->ready--; }
long eng_TorpedoTube_CanHit(void *p, void *t) { Sub *w = sub(p); return w && w->prop && w->ship && t ? tube_can_hit(w->ship, w, t) : 0; }
long eng_TorpedoTube_IsInArc(void *p, void *t) { return eng_TorpedoTube_CanHit(p, t); }
long eng_TorpedoSystem_GetNumReady(void *p) { Sub *s = sub(p); return s ? ammo_loaded(s) : 0; }
long eng_TorpedoSystem_IsTubeReady(void *p, int i) { Sub *s = sub(p); return s && i >= 0 && i < s->nkids && s->kids[i]->ready > 0; }

/* ================= tractor beams (TractorBeamProjector::ApplyTractorBeam 0057ff60, GetDamageAtDistance 00580f50) =================
   Strength = max damage x weapon condition x system condition x min(1, max damage distance / distance) (x combined condition when the
   system's level is not 0) x dt. Modes (TractorBeamSystem +0xf4): HOLD 0 slows the target to a stop, TOW 1 matches the ship's velocity,
   PULL 2 moves it toward the ship, PUSH 3 away, DOCK 4/5 pull in. The beam drains charge like a phaser and stops at 0 or out of arc.
   Approximations (marked): the velocity change is limited by strength / target mass (the exe also uses the 3.0 range factor and dock
   alignment); no tractor damage to the target. */
static void update_tractor(void *o, Sub *w, void *t, float dt)
{
    Sub *s = w->parent; Phys *q; float *a, *b, d[3], l, st, dv[3], m, md; int i;
    if (w->firing && (!t || !(q = phys(t)) || q->dead || !can_hit(o, w, t))) { w->firing = 0; w->beam_target = NULL; }
    if (!w->firing && t && phys(t) && !phys(t)->dead && can_fire(o, w) && can_hit(o, w, t)) { w->firing = 1; w->beam_target = t; }
    if (!w->firing || !(q = phys(t))) return;
    a = eng_BaseObjectClass_GetTranslate(o); b = eng_BaseObjectClass_GetTranslate(t);
    for (i = 0; i < 3; i++) d[i] = b[i] - a[i];
    free(a); free(b); l = vlen(d); if (l < 1e-6f) return;
    md = (float)eng_EnergyWeaponProperty_GetMaxDamageDistance(w->prop);
    st = (float)eng_EnergyWeaponProperty_GetMaxDamage(w->prop) * cond_pct(w) * (s ? cond_pct(s) : 1.0f) * (l > md && l > 0 ? md / l : 1.0f) * dt;
    m = q->mass > 0 ? q->mass : 1.0f;
    switch (s ? s->tractor_mode : 0) {
    case 0: for (i = 0; i < 3; i++) dv[i] = -q->vel[i]; break;                                         /* hold */
    case 1: for (i = 0; i < 3; i++) dv[i] = ship(o)->vel[i] - q->vel[i]; break;                        /* tow */
    case 3: for (i = 0; i < 3; i++) dv[i] = d[i] / l * st; break;                                       /* push */
    default: for (i = 0; i < 3; i++) dv[i] = -d[i] / l * st; break;                                     /* pull, dock */
    }
    clampv(dv, st / m * 100.0f);
    for (i = 0; i < 3; i++) q->vel[i] += dv[i];
    {   /* pull/push speed limit relative to the ship: 3 x the beam strength per second (the exe limits by 3.0 x a range term) */
        extern double eng_ObjectClass_GetRadius(void *); float rv[3], lim = 3.0f * st / (dt > 0 ? dt : 1) / m * 100.0f;
        int mode = s ? s->tractor_mode : 0;
        for (i = 0; i < 3; i++) rv[i] = q->vel[i] - ship(o)->vel[i];
        if ((mode == 2 || mode >= 4) && l < (float)(eng_ObjectClass_GetRadius(o) + eng_ObjectClass_GetRadius(t)) * 1.5f) for (i = 0; i < 3; i++) rv[i] = 0;   /* close enough: hold */
        clampv(rv, lim); for (i = 0; i < 3; i++) q->vel[i] = ship(o)->vel[i] + rv[i];
    }
}
/* the tractor toggle of the weapons control (RefreshTractorToggle 00549d70): 1 a beam holds the target, 2 the system fires but no beam reaches (out of range or arc), 0 idle */
int tg_tractor_status(void *p)
{
    Sub *s = sub(p); int i;
    if (!s || !s->sys_firing) return 0;
    for (i = 0; i < s->nkids; i++) if (s->kids[i]->firing) return 1;
    return 2;
}
long eng_TractorBeamSystem_GetMode(void *p) { Sub *s = sub(p); return s ? s->tractor_mode : 0; }
void eng_TractorBeamSystem_SetMode(void *p, int v) { Sub *s = sub(p); if (s) s->tractor_mode = v; }

/* ================= destruction (DamageableObject::Update 00592c00) =================
   The lifetime (+0x14c) is FLT_MAX until a script sets it (the explosion effects do). It then counts down in game time; below 0 the object
   sends ET_OBJECT_DESTROYED (a TGStringEvent with its name), is marked dead and applies blast damage (splash damage within the splash
   radius, falling off with distance; ApplyBlastDamage 00593c10). Approximation (marked): the dead object leaves its set at once. */
static void *blast_src; static float blast_pos[3], blast_dmg, blast_rad;
static void blast_one(void *s)
{
    extern double eng_ObjectClass_GetRadius(void *); float *q, d, f; int i; float hit[3];
    if (s == blast_src || !ship(s) || ship(s)->dead) return;
    q = eng_BaseObjectClass_GetTranslate(s);
    d = sqrtf((q[0] - blast_pos[0]) * (q[0] - blast_pos[0]) + (q[1] - blast_pos[1]) * (q[1] - blast_pos[1]) + (q[2] - blast_pos[2]) * (q[2] - blast_pos[2])) - (float)eng_ObjectClass_GetRadius(s);
    if (d < blast_rad) { f = d <= 0 ? 1.0f : 1.0f - d / blast_rad; for (i = 0; i < 3; i++) hit[i] = q[i]; weapon_hit(s, blast_src, blast_src, hit, blast_dmg * f, 1.0f, 2); }
    free(q);
}
void tg_lifetime_tick(void *o, float dt)
{
    Phys *p = phys(o); void tg_space_each(unsigned ct, void (*fn)(void *));
    if (!p || p->dead || p->lifetime >= 3.4028235e38f) return;
    p->lifetime -= dt;
    if (p->lifetime >= 0) return;
    {   TGEvent *e = tg_ev_new("TGStringEvent", 0); const char *n = eng_BaseObjectClass_GetName(o);
        e->type = (unsigned)tg_const("ET_OBJECT_DESTROYED"); e->dest = o; e->str = n ? strdup(n) : NULL; tg_add_event(e); }
    p->lifetime = 3.4028235e38f; p->dead = 1;
    if (p->splash > 0 && p->splash_radius > 0) { float *pos = eng_BaseObjectClass_GetTranslate(o); memcpy(blast_pos, pos, 12); free(pos); blast_src = o; blast_dmg = p->splash; blast_rad = p->splash_radius; tg_space_each(0x8008, blast_one); }
    /* a torpedo leaves its set here. A destroyed ship stays in its set (dead) until the scripts delete it: the Quick Battle reads the
       player's set after the player is destroyed (RecreatePlayer) */
    if (tg_is_type(o, 0x8009)) torp_remove(o);
}

/* AIScriptAssist::TorpIsIncoming 00473960: the torpedo aims at the target (target id), is in the same set, (optionally) was fired by
   firing_id, moves toward the target and reaches it within danger_time (distance / speed). GetIncomingTorpIDsInSet: the ids of all such
   torpedoes in a set. */
static int torp_incoming(void *tgt, void *tor, float danger, long firing, int use_firing)
{
    Phys *q = torp(tor); float *a, *b, d[3], sp, dist, dot; int i, r = 0;
    if (!q || !tgt || q->dead) return 0;
    if (use_firing && q->parent_id != (int)firing) return 0;
    if (eng_BaseObjectClass_GetContainingSet(tor) != eng_BaseObjectClass_GetContainingSet(tgt) || q->target_id != (int)((TGObject *)tgt)->id) return 0;
    sp = vlen(q->vel); a = eng_BaseObjectClass_GetTranslate(tgt); b = eng_BaseObjectClass_GetTranslate(tor);
    for (i = 0; i < 3; i++) d[i] = b[i] - a[i];
    free(a); free(b);
    dot = d[0] * q->vel[0] + d[1] * q->vel[1] + d[2] * q->vel[2]; dist = vlen(d);
    if (dot <= 0 && dist > 0 && sp > 0 && dist / sp < danger) r = 1;
    return r;
}
long eng_AIScriptAssist_TorpIsIncoming(void *tgt, PyObject *to, float danger, long firing, int use)
{ void *p = NULL; if (!to || !appc_to_ptr(to, &p, "void")) { PyErr_Clear(); return 0; } return torp_incoming(tgt, p, danger, firing, use); }
static PyObject *inc_list; static void *inc_tgt; static float inc_danger; static long inc_firing; static int inc_use; static void *inc_set;
static void inc_one(void *o) { if (eng_BaseObjectClass_GetContainingSet(o) == inc_set && torp_incoming(inc_tgt, o, inc_danger, inc_firing, inc_use)) { PyObject *v = PyInt_FromLong((long)((TGObject *)o)->id); PyList_Append(inc_list, v); Py_DECREF(v); } }
PyObject *eng_AIScriptAssist_GetIncomingTorpIDsInSet(void *tgt, void *set, float danger, int firing, int use)
{
    void tg_space_each(unsigned ct, void (*fn)(void *));
    inc_list = PyList_New(0); inc_tgt = tgt; inc_set = set; inc_danger = danger; inc_firing = firing; inc_use = use;
    if (tgt && set) tg_space_each(0x8009, inc_one);
    return inc_list;
}

/* ================= ship systems: subsystem update and power flow =================
   ShipSubsystem::Update 0056bc60 (Ghidra name SetCondition): keeps the frame time (+0x3c) and sends ET_SUBSYSTEM_COMPLETELY_DISABLED /
   ET_SUBSYSTEM_PARTIALLY_OPERATIONAL (TGFloatEvent, combined condition) to the ship when the disabled state changes.
   PowerSubsystem::Update 00563780: once per whole second of game time the reactor adds output x seconds (unless disabled) and the conduits
   get capacity x seconds, limited by the battery levels (UpdateConduitCapacity 00563700); the main conduit capacity scales with condition.
   PoweredSubsystem::Update 00562470: wanted = percentage wanted x normal power (0 when off or disabled) x dt, taken with DeductPower
   (mode 0), StealPower (1) or StealPowerFromReserve (2); +0x94 = received / wanted, +0x98 = received / (normal power x dt).
   ShieldClass::Update 0056a230: every 0.5 s the power received in that time, / 6 x 0.85, boosts each facing (BoostShield 0056a420:
   charge per second x power / (normal power / 6), excess returned); shields that are off but not disabled take normal power / 6 per facing
   straight from the batteries (StealPower) and give back what they cannot use; disabled shields drop to 0.
   Order: the ship's top-level subsystems in list order (ShipClass::Update); children (engines, weapons) are updated by their parent. */
static void sub_update(Sub *x, float dt)
{
    int dis = (int)eng_ShipSubsystem_IsDisabled(x);
    x->last_dt = dt;
    if (dis != x->dis_flag) {
        TGEvent *e = tg_ev_new("TGFloatEvent", 0);
        x->dis_flag = dis;
        e->type = (unsigned)tg_const(dis ? "ET_SUBSYSTEM_COMPLETELY_DISABLED" : "ET_SUBSYSTEM_PARTIALLY_OPERATIONAL");
        e->source = &x->o; e->dest = x->ship; e->v.f = (float)eng_ShipSubsystem_GetCombinedConditionPercentage(x); tg_add_event(e);
    }
}
static void powered_update(Sub *x, float dt)
{
    Sub *ps = x->power_src; float n;
    sub_update(x, dt);
    x->pw_wanted = x->power_pct_wanted * normal_wanted(x) * dt;
    if (!ps) x->power_received = 0;                                            /* no power subsystem: nothing arrives (the exe needs one) */
    else x->power_received = x->pw_mode == 0 ? power_deduct(ps, x->pw_wanted) : x->pw_mode == 1 ? power_steal(ps, x->pw_wanted) : power_steal_reserve(ps, x->pw_wanted);
    x->pw_pct = x->pw_wanted <= 0.0f ? 0.0f : x->power_received / x->pw_wanted;
    n = normal_wanted(x) * x->last_dt;
    x->pw_norm_pct = n <= 0.0f ? 1.0f : x->power_received / n;
}
static void power_update(Sub *x, float dt)
{
    extern double eng_PowerProperty_GetMainConduitCapacity(void *), eng_PowerProperty_GetBackupConduitCapacity(void *), eng_PowerProperty_GetPowerOutput(void *);
    float now = (float)tg_game_time, secs, mc, bc;
    sub_update(x, dt);
    if (now < x->last_upd) x->last_upd = now;
    if (now - x->last_upd > 1.0f && x->prop) {
        x->dispensed = 0;
        secs = (float)(int)(now - x->last_upd);
        if (!eng_ShipSubsystem_IsDisabled(x)) power_add(x, (float)eng_PowerProperty_GetPowerOutput(x->prop) * cond_pct(x) * secs);   /* GetPowerOutput 005634b0 x condition */
        mc = (float)eng_PowerProperty_GetMainConduitCapacity(x->prop) * cond_pct(x) * secs; bc = (float)eng_PowerProperty_GetBackupConduitCapacity(x->prop) * secs;
        x->main_cond = mc < x->main_batt ? mc : x->main_batt;
        x->back_cond = (bc < x->backup_batt ? bc : x->backup_batt);
        x->avail = x->main_cond + x->back_cond;
        x->last_upd = now - fmodf(now, 1.0f);
    }
}
static float shield_boost(Sub *sh, int i, float pw)                           /* BoostShield 0056a420: returns the power it could not use */
{
    extern double eng_ShieldProperty_GetShieldChargePerSecond(void *, int);
    float k = normal_power(sh) / 6.0f, cps, mx;
    if (k <= 0.0f) return pw;
    cps = (float)eng_ShieldProperty_GetShieldChargePerSecond(sh->prop, i);
    sh->shields[i] += cps * pw / k;
    mx = (float)eng_ShieldProperty_GetMaxShields(sh->prop, i);
    if (sh->shields[i] > mx) { float over = cps / k > 0.0f ? (sh->shields[i] - mx) / (cps / k) : 0.0f; sh->shields[i] = mx; return over; }
    return 0.0f;
}
static void shield_update(Sub *sh, float dt)
{
    int i; float pw;
    sh->sh_timer += dt; sh->sh_accum += sh->power_received;
    if (sh->sh_timer >= 0.5f && sh->prop) {
        pw = sh->sh_accum / 6.0f; sh->sh_timer = 0;
        if (!eng_ShipSubsystem_IsDisabled(sh) && sh->on) for (i = 0; i < 6; i++) shield_boost(sh, i, pw * 0.85f);
        else for (i = 0; i < 6; i++) {
            if (eng_ShipSubsystem_IsDisabled(sh)) sh->shields[i] = 0;
            else if (sh->power_src) power_add(sh->power_src, shield_boost(sh, i, power_steal(sh->power_src, normal_power(sh) / 6.0f)));
        }
        sh->sh_accum = 0;
    }
    powered_update(sh, dt);
}
static void repair_update(Sub *r, float dt); static void sensor_update(Sub *s, float dt); static void cloak_update(Sub *c, float dt); static void warp_update(Sub *w, float dt);
static void child_update(Sub *x, float dt) { int i; for (i = 0; i < x->nkids; i++) { if (tg_is_type(&x->kids[i]->o, 0x801c)) powered_update(x->kids[i], dt); else sub_update(x->kids[i], dt); child_update(x->kids[i], dt); } }
void tg_systems_tick(void *o, float dt)
{
    Phys *p = ship(o); int i;
    if (!p || p->dead) return;
    for (i = 0; i < p->nsubs; i++) {
        Sub *x = p->subs[i];
        if (tg_is_type(&x->o, 0x8022)) power_update(x, dt);
        else if (tg_is_type(&x->o, 0x8028)) shield_update(x, dt);
        else if (tg_is_type(&x->o, 0x8029)) repair_update(x, dt);
        else if (tg_is_type(&x->o, 0x8023)) sensor_update(x, dt);
        else if (tg_is_type(&x->o, 0x8024)) cloak_update(x, dt);
        else if (tg_is_type(&x->o, 0x8025)) warp_update(x, dt);
        else if (tg_is_type(&x->o, 0x801c)) powered_update(x, dt);
        else sub_update(x, dt);
        child_update(x, dt);
    }
}
/* getters that scale the property value with the condition (005634b0, 005634f0) */
double eng_PowerSubsystem_GetPowerOutput(void *s) { extern double eng_PowerProperty_GetPowerOutput(void *); Sub *x = sub(s); return x && x->prop ? eng_PowerProperty_GetPowerOutput(x->prop) * cond_pct(x) : 0; }
double eng_PowerSubsystem_GetMainConduitCapacity(void *s) { extern double eng_PowerProperty_GetMainConduitCapacity(void *); Sub *x = sub(s); return x && x->prop ? eng_PowerProperty_GetMainConduitCapacity(x->prop) * cond_pct(x) : 0; }

/* ================= repair (RepairSubsystem 005652a0 .. 00565dd0) =================
   The repair list holds damaged subsystems in order. Update: when on, points = own condition x MaxRepairPoints x dt; the first
   NumRepairTeams subsystems that are not destroyed each get points / min(teams, list length) (ShipSubsystem::Repair divides by the
   repair complexity); a subsystem at full condition sends ET_REPAIR_COMPLETED, a destroyed one (condition 0) ET_REPAIR_CANNOT_BE_COMPLETED
   (TGObjPtrEvent, subsystem ID, source = repair subsystem, destination = ship). Handlers (SetShip 00565220, bound to the ship):
   ET_SUBSYSTEM_DAMAGED adds the subsystem at the end (AddSubsystem 00565520: only once, only when condition > 0);
   ET_REPAIR_COMPLETED / ET_REPAIR_CANNOT_BE_COMPLETED remove it; broadcast ET_REPAIR_INCREASE_PRIORITY moves it to the front, or to the
   end when a team works on it already (00565b50); ET_ADD_TO_REPAIR_LIST adds the event source. The engineering repair pane refresh is UI.
   Approximation (marked): the handlers run as one native broadcast handler per event type that finds the ship's repair subsystem. */
static int rq_find(Sub *r, Sub *x) { int i; for (i = 0; i < r->nrq; i++) if (r->rq[i] == x) return i; return -1; }
static void rq_remove(Sub *r, int i) { if (i < 0) return; memmove(r->rq + i, r->rq + i + 1, (r->nrq - i - 1) * sizeof *r->rq); r->nrq--; }
static int repair_add(Sub *r, Sub *x)
{
    if (!r || !x || rq_find(r, x) >= 0) return 0;
    if (x->condition > 0.0f) { r->rq = realloc(r->rq, (r->nrq + 1) * sizeof *r->rq); r->rq[r->nrq++] = x; }
    return 1;
}
static long repair_teams(Sub *r) { extern long eng_RepairSubsystemProperty_GetNumRepairTeams(void *); return r->prop ? eng_RepairSubsystemProperty_GetNumRepairTeams(r->prop) : 0; }
static void repair_event(Sub *r, Sub *x, const char *et)
{ TGEvent *e = tg_ev_new("TGObjPtrEvent", 0); e->type = (unsigned)tg_const(et); e->source = &r->o; e->dest = r->ship; e->ref_id = x->o.id; e->v.p = x; tg_add_event(e); }
static void repair_update(Sub *r, float dt)
{
    extern double eng_RepairSubsystemProperty_GetMaxRepairPoints(void *);
    float pts; long teams, used = 0; int i;
    powered_update(r, dt);
    if (!r->on || !r->prop) return;
    pts = cond_pct(r) * (float)eng_RepairSubsystemProperty_GetMaxRepairPoints(r->prop) * dt;
    teams = repair_teams(r);
    for (i = 0; i < r->nrq; i++) {
        Sub *x = r->rq[i];
        if (used >= teams) { if (x->condition <= 0.0f) repair_event(r, x, "ET_REPAIR_CANNOT_BE_COMPLETED"); continue; }
        if (x->condition > 0.0f) {
            eng_ShipSubsystem_Repair(x, pts / (float)(teams < r->nrq ? teams : r->nrq));
            if (x->condition / maxc(x) >= 1.0f) repair_event(r, x, "ET_REPAIR_COMPLETED");
            used++;
        } else repair_event(r, x, "ET_REPAIR_CANNOT_BE_COMPLETED");
    }
}
static Sub *ship_repair(void *o) { Phys *p = o && tg_is_ours(o) && !((TGObject *)o)->dead ? ship(o) : NULL; return p ? p->typed[S_REPAIR] : NULL; }
static Sub *ev_sub(TGEvent *e) { TGObject *o = e->ref_id ? tg_find(e->ref_id) : NULL; return o ? sub(o) : NULL; }
static void on_sub_damaged(TGEvent *e) { Sub *x = ev_sub(e); if (x) repair_add(ship_repair(e->dest), x); }                       /* HandleHitEvent 005658d0 */
static void on_repair_done(TGEvent *e) { Sub *r = ship_repair(e->dest), *x = ev_sub(e); if (r && x) rq_remove(r, rq_find(r, x)); }   /* 00565980 / 00565a80 */
static void on_increase_priority(TGEvent *e)                                   /* HandleIncreasePriorityEvent 00565b50 */
{
    Sub *x = ev_sub(e), *r; int i, busy;
    if (!x || !(r = ship_repair(x->ship)) || (i = rq_find(r, x)) < 0) return;
    busy = i < repair_teams(r);                                                /* IsBeingRepaired 00565890: within the first NumRepairTeams entries */
    rq_remove(r, i);
    if (busy) r->rq[r->nrq++] = x;
    else { memmove(r->rq + 1, r->rq, r->nrq * sizeof *r->rq); r->rq[0] = x; r->nrq++; }
}
static void on_add_to_repair_list(TGEvent *e) { Sub *x = e->source ? sub(e->source) : NULL; if (x) repair_add(ship_repair(x->ship), x); }   /* 00565b30 */
long eng_RepairSubsystem_AddSubsystem(void *r, void *x) { return repair_add(sub(r), sub(x)); }   /* 00565520 returns 0 or 1 */
void eng_RepairSubsystem_AddToRepairList(void *r, void *x, int f) { repair_add(sub(r), sub(x)); }
long eng_RepairSubsystem_IsBeingRepaired(void *r, void *x) { Sub *a = sub(r); int i = a ? rq_find(a, sub(x)) : -1; return i >= 0 && i < repair_teams(a); }
/* for the engineering repair pane: the repair queue of a ship (damaged subsystems in order), the number of repair teams, and the destroyed
   subsystems (condition 0, children included; approximation of the exe's list that ET_REPAIR_CANNOT_BE_COMPLETED fills) */
int tg_repair_queue(void *ship, void **out, int max, int *teams)
{
    Sub *r = ship_repair(ship); int i, n = 0;
    if (!r) { if (teams) *teams = 0; return 0; }
    if (teams) *teams = (int)repair_teams(r);
    for (i = 0; i < r->nrq && n < max; i++) out[n++] = r->rq[i];
    return n;
}
static void destroyed_walk(Sub *x, void **out, int max, int *n)
{
    int i; if (!x || *n >= max) return;
    if (x->condition <= 0.0f && x->prop && maxc(x) > 0) out[(*n)++] = x;
    for (i = 0; i < x->nkids; i++) destroyed_walk(x->kids[i], out, max, n);
}
int tg_destroyed_subsystems(void *ship, void **out, int max)
{
    void **top; int n = 0, k, i; k = tg_ship_top_subsystems(ship, &top);
    for (i = 0; i < k; i++) destroyed_walk(sub(top[i]), out, max, &n);
    free(top); return n;
}
void *eng_RepairSubsystem_Create(void) { return sub_new("RepairSubsystem"); }


/* ================= sensors (SensorSubsystem 005670b0 .. 00568f40) =================
   Range (GetSensorRange 00567190) = base range (property +0x4c) x normal power percentage (+0x98) x condition, 0 when off or disabled.
   "Near" = within half the range, "far" = within the range, same set only; a launched probe in range counts too (probe list +0xb0).
   The player's sensors keep a static state (the object at 0x0098c000): the identified list (+0x04), identify timers (+0x1c, 4 s,
   GetIdentificationTime 005671c0), far (+0x30) and near (+0x48) lists. For the player's own sensors IsObjectNear/IsObjectFar (flag 0) read
   those lists and IsObjectKnown reads the identified list; for other ships IsObjectKnown is always false.
   Every second (ET_SENSORS_PERIODIC_SCAN, HandlePeriodicScanEvent 005680c0 / PeriodicScan 00568520) every physics object except the player
   is checked (005685b0): an unknown near object starts its identify timer; entering / leaving the near and far lists sends
   ET_SENSORS_SHIP_NEAR_PROXIMITY / ET_SENSORS_SHIP_FAR_PROXIMITY (TGBoolEvent, source = object, destination = player). Without power received
   the scan is skipped every second time. The timer sends ET_SENSORS_SHIP_IDENTIFIED (TGObjPtrEvent to the object); its handler adds the
   object to the identified list (00568080). IsObjectVisible 005671d0: sensors on and working, not a cloaked ship, same set; visible when
   the normal power percentage is above 1.2, or the object is far (in range), or it is known.
   Update 005670b0 (player only): ET_SENSORS_RANGE_CHANGED when the range changes; drops the target when it is not visible.
   Approximations (marked): nebulas do not hide objects yet; the per-object timers are a list checked each frame (same game-time result
   as TGTimer); probes are counted but do not extend the range. */
static struct { unsigned *ident; int nident; struct { unsigned id; float at; } *tm; int ntm; unsigned *farl, *nearl; int nfar, nnear; float next_scan; int skip; } SN;
static int idl_has(unsigned *l, int n, unsigned id) { int i; for (i = 0; i < n; i++) if (l[i] == id) return 1; return 0; }
static void idl_add(unsigned **l, int *n, unsigned id) { if (idl_has(*l, *n, id)) return; *l = realloc(*l, (*n + 1) * sizeof **l); (*l)[(*n)++] = id; }
static void idl_del(unsigned *l, int *n, unsigned id) { int i; for (i = 0; i < *n; i++) if (l[i] == id) { memmove(l + i, l + i + 1, (*n - i - 1) * sizeof *l); (*n)--; return; } }
static void *player(void) { extern void *eng_Game_GetCurrentPlayer(void); return eng_Game_GetCurrentPlayer(); }
static int is_player_sensor(Sub *s) { void *pl = player(); return pl && s && s->ship == pl; }
static void sn_clear_lists(void) { SN.nfar = SN.nnear = 0; }                    /* 005689d0 */
static void sn_clear_timers(void) { SN.ntm = 0; }                                /* ClearAllTimers 00568e40 */
static float sensor_range(Sub *s)
{
    extern double eng_SensorProperty_GetBaseSensorRange(void *);
    if (!s || eng_ShipSubsystem_IsDisabled(s) || !s->on || !s->prop) return 0.0f;
    return (float)eng_SensorProperty_GetBaseSensorRange(s->prop) * s->pw_norm_pct * cond_pct(s);
}
static float obj_dist(void *a, void *b)
{ float *x = eng_BaseObjectClass_GetTranslate(a), *y = eng_BaseObjectClass_GetTranslate(b), d[3]; int i; if (!x || !y) { free(x); free(y); return 3.4e38f; } for (i = 0; i < 3; i++) d[i] = x[i] - y[i]; free(x); free(y); return vlen(d); }
static int same_set(void *a, void *b) { void *s = eng_BaseObjectClass_GetContainingSet(a); return s && s == eng_BaseObjectClass_GetContainingSet(b); }
static int sens_near(Sub *s, void *o, int compute)                               /* IsObjectNear 00567440 */
{
    if (!compute && is_player_sensor(s)) return idl_has(SN.nearl, SN.nnear, ((TGObject *)o)->id);
    if (!same_set(s->ship, o)) return 0;
    return obj_dist(s->ship, o) <= sensor_range(s) * 0.5f;
}
static int sens_far(Sub *s, void *o, int compute)                                /* IsObjectFar 00567640 */
{
    if (!compute && is_player_sensor(s)) return idl_has(SN.farl, SN.nfar, ((TGObject *)o)->id) || idl_has(SN.nearl, SN.nnear, ((TGObject *)o)->id);
    if (!same_set(s->ship, o)) return 0;
    return obj_dist(s->ship, o) <= sensor_range(s);
}
static int sens_known(Sub *s, void *o) { return is_player_sensor(s) && o && idl_has(SN.ident, SN.nident, ((TGObject *)o)->id); }   /* IsObjectKnown 00567830 */
static int sens_visible(Sub *s, void *o)                                         /* IsObjectVisible 005671d0 */
{
    Phys *t;
    if (!s || !o || eng_ShipSubsystem_IsDisabled(s) || !s->on) return 0;
    t = ship(o);
    if (t && t->typed[S_CLOAK] && t->typed[S_CLOAK]->cloaked) return 0;
    if (!same_set(s->ship, o)) return 0;
    if (s->pw_norm_pct > 1.2f) return 1;
    if (sens_far(s, o, 0)) return 1;
    return sens_known(s, o);
}
static void sn_start_timer(void *o)                                              /* StartIdentifyTimer 00568ad0 */
{ SN.tm = realloc(SN.tm, (SN.ntm + 1) * sizeof *SN.tm); SN.tm[SN.ntm].id = ((TGObject *)o)->id; SN.tm[SN.ntm].at = (float)tg_game_time + 4.0f; SN.ntm++; }
static int sn_has_timer(unsigned id) { int i; for (i = 0; i < SN.ntm; i++) if (SN.tm[i].id == id) return 1; return 0; }
static void sn_del_timer(unsigned id) { int i; for (i = 0; i < SN.ntm; i++) if (SN.tm[i].id == id) { memmove(SN.tm + i, SN.tm + i + 1, (SN.ntm - i - 1) * sizeof *SN.tm); SN.ntm--; return; } }
static void sn_identified_event(void *o)
{ TGEvent *e = tg_ev_new("TGObjPtrEvent", 0); e->type = (unsigned)tg_const("ET_SENSORS_SHIP_IDENTIFIED"); e->source = o; e->dest = o; e->ref_id = ((TGObject *)o)->id; e->v.p = o; tg_add_event(e); }
static void sn_prox_event(const char *et, void *o, void *pl, int on)
{ TGEvent *e = tg_ev_new("TGBoolEvent", 0); e->type = (unsigned)tg_const(et); e->source = o; e->dest = pl; e->v.i = on; tg_add_event(e); }
static void sn_scan_object(Sub *s, void *o, void *pl)                           /* 005685b0 */
{
    unsigned id = ((TGObject *)o)->id; int in_far = idl_has(SN.farl, SN.nfar, id), in_near = idl_has(SN.nearl, SN.nnear, id);
    int nr = sens_near(s, o, 1), fr = sens_far(s, o, 1), known = sens_known(s, o);
    if (nr) {
        if (!known && !sn_has_timer(id)) sn_start_timer(o);
        if (!in_near) {
            idl_add(&SN.nearl, &SN.nnear, id); sn_prox_event("ET_SENSORS_SHIP_NEAR_PROXIMITY", o, pl, 1);
            if (in_far) { idl_del(SN.farl, &SN.nfar, id); sn_prox_event("ET_SENSORS_SHIP_FAR_PROXIMITY", o, pl, 0); }
            return;
        }
    }
    if (fr && !in_far) { if (in_near) return; idl_add(&SN.farl, &SN.nfar, id); sn_prox_event("ET_SENSORS_SHIP_FAR_PROXIMITY", o, pl, 1); return; }
    if ((!in_near && !in_far) || fr) return;
    if (in_near) { idl_del(SN.nearl, &SN.nnear, id); sn_prox_event("ET_SENSORS_SHIP_NEAR_PROXIMITY", o, pl, 0); }
    if (in_far) { idl_del(SN.farl, &SN.nfar, id); sn_prox_event("ET_SENSORS_SHIP_FAR_PROXIMITY", o, pl, 0); }
}
static Sub *sn_sensor; static void *sn_player;
static void sn_scan_one(void *o) { if (o != sn_player && !((TGObject *)o)->dead) sn_scan_object(sn_sensor, o, sn_player); }
static void sn_periodic(void)                                                    /* PeriodicScan 00568520 */
{
    void tg_space_each(unsigned ct, void (*fn)(void *)); void *pl = player(); Phys *p = pl ? ship(pl) : NULL; Sub *s;
    if (!p || !(s = p->typed[S_SENSOR]) || !eng_BaseObjectClass_GetContainingSet(pl)) return;
    if (s->power_received > 0.0f) SN.skip = 0; else { int was = SN.skip; SN.skip = 1; if (!was) return; }
    sn_sensor = s; sn_player = pl; tg_space_each(0x8006, sn_scan_one);
}
static void sensor_update(Sub *s, float dt)                                      /* Update 005670b0 */
{
    float r;
    powered_update(s, dt);
    r = sensor_range(s);
    if (is_player_sensor(s)) {
        Phys *p = ship(s->ship);
        if (r != s->sens_range) { TGEvent *e = tg_ev_new("TGFloatEvent", 0); e->type = (unsigned)tg_const("ET_SENSORS_RANGE_CHANGED"); e->source = &s->o; e->dest = s->ship; e->v.f = r; tg_add_event(e); }
        if (p && p->target && !tg_is_type(p->target, 0x80de) && !sens_visible(s, p->target)) tg_ship_change_target(s->ship, NULL, NULL);   /* SetTargetByID(0) */
    }
    s->sens_range = r;
}
void tg_sensors_tick(void)                                                       /* once per frame: identify timers and the 1 s periodic scan */
{
    int i;
    for (i = 0; i < SN.ntm; ) {
        if ((float)tg_game_time >= SN.tm[i].at) { TGObject *o = tg_find(SN.tm[i].id); memmove(SN.tm + i, SN.tm + i + 1, (SN.ntm - i - 1) * sizeof *SN.tm); SN.ntm--; if (o && !o->dead) sn_identified_event(o); }
        else i++;
    }
    if ((float)tg_game_time >= SN.next_scan) { SN.next_scan = (float)tg_game_time + 1.0f; sn_periodic(); }
}
static void on_ship_identified(TGEvent *e) { if (e->ref_id) { sn_del_timer(e->ref_id); idl_add(&SN.ident, &SN.nident, e->ref_id); } }       /* 00568080 */
static void on_enter_set(TGEvent *e)                                             /* HandleEnterSet 00567f60 */
{
    void *o = e->dest, *pl = player(); Phys *p = pl ? ship(pl) : NULL;
    if (!o || !tg_is_ours(o) || !p || !p->typed[S_SENSOR]) return;
    if (o == pl) { sn_clear_lists(); return; }
    if (same_set(pl, o) && tg_is_type(o, 0x8006)) sn_scan_object(p->typed[S_SENSOR], o, pl);
}
static void on_exit_set(TGEvent *e)                                              /* HandleExitSet 00567ff0 */
{ void *o = e->dest; if (!o || !tg_is_ours(o)) return; sn_del_timer(((TGObject *)o)->id); if (o == player()) { sn_clear_timers(); sn_clear_lists(); } }
static void on_sensor_set_player(TGEvent *e) { sn_clear_timers(); sn_clear_lists(); }   /* HandleSetPlayer 00567cf0 */
static void on_ship_decloaked(TGEvent *e)                                        /* HandleShipDecloaked 00568040 */
{ void *pl = player(); Phys *p = pl ? ship(pl) : NULL; void *o = e->dest; if (p && p->typed[S_SENSOR] && o && tg_is_ours(o) && sens_near(p->typed[S_SENSOR], o, 0)) sn_start_timer(o); }

double eng_SensorSubsystem_GetSensorRange(void *s) { return sensor_range(sub(s)); }
double eng_SensorSubsystem_GetBaseSensorRange(void *s) { extern double eng_SensorProperty_GetBaseSensorRange(void *); Sub *x = sub(s); return x && x->prop ? eng_SensorProperty_GetBaseSensorRange(x->prop) : 0; }
double eng_SensorSubsystem_GetIdentificationTime(void *s) { return 4.0; }
long eng_SensorSubsystem_IsObjectVisible(void *s, void *o) { return sens_visible(sub(s), o); }
long eng_SensorSubsystem_IsObjectKnown(void *s, void *o) { return sens_known(sub(s), o); }
long eng_SensorSubsystem_IsObjectNear(void *s, void *o, int compute) { Sub *x = sub(s); return x && o ? sens_near(x, o, compute) : 0; }
long eng_SensorSubsystem_IsObjectFar(void *s, void *o, int compute) { Sub *x = sub(s); return x && o ? sens_far(x, o, compute) : 0; }
void eng_SensorSubsystem_IdentifyObject(void *s, void *o) { if (o && is_player_sensor(sub(s))) sn_start_timer(o); }                         /* 00567880 */
void eng_SensorSubsystem_ForceObjectIdentified(void *s, void *o)                 /* 005678b0 */
{ if (!o || !tg_is_ours(o) || !is_player_sensor(sub(s))) return; sn_identified_event(o); idl_add(&SN.ident, &SN.nident, ((TGObject *)o)->id); }
long eng_SensorSubsystem_GetNumProbes(void *s) { Sub *x = sub(s); return x ? x->probes : 0; }
void eng_SensorSubsystem_SetNumProbes(void *s, int n) { Sub *x = sub(s); if (x) x->probes = n; }
void *eng_SensorSubsystem_Create(void) { return sub_new("SensorSubsystem"); }


/* ================= cloak (CloakingSubsystem 0055e500 .. 0055fb00) =================
   States (+0xb0): 0 decloaked, 2 cloaking, 3 cloaked, 5 decloaking (1 and 4 are tested but never set). Cloak time 5 s (DAT_008e4e1c),
   shield delay 1 s (DAT_008e4e20). The subsystem starts switched off and takes its power from the backup battery (mode 2).
   StartCloaking 0055f360: TurnOn, trying = 1. StopCloaking 0055f380: decloak when cloaking or cloaked, trying = 0, TurnOff.
   Update 0055e500: cloaking counts the timer up to 5 s (then OnCloakStarted 0055f6d0: state 3, cloaked, ET_CLOAK_COMPLETED); decloaking
   counts down to 0 (OnDecloakComplete 0055f7f0: state 0, ET_DECLOAK_COMPLETED, the shields get ET_SHIELDS_SET_STATE on after 1 s and 100%
   power when they had 0). Then, when on: trying and not cloaking/cloaked -> SetCloaking(1); not trying and cloaking/cloaked -> SetCloaking(0);
   cloaked with less than 80% of the wanted power -> StopCloaking.
   SetCloaking(1) 0055f110 (not when disabled): the shields get ET_SHIELDS_SET_STATE off at once, ET_CLOAK_BEGINNING follows 1 s later (at
   once without shields); timer 0 unless it was decloaking; state 2. SetCloaking(0): ET_DECLOAK_BEGINNING, timer 5 s unless it was
   cloaking, cloaked = 0 (0055f770), state 5. Shields cannot be turned on while the ship cloaks, is cloaked or decloaks (0056ab10).
   Approximations (marked): the model fade (ApplyCloakLevel / applyCloakLevelToNode), hiding the node and the Cloak/Uncloak sounds are
   rendering and audio. */
static float CLOAK_TIME = 5.0f, SHIELD_DELAY = 1.0f;
static void ship_event(const char *et, Sub *src, void *dst) { TGEvent *e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const(et); e->source = &src->o; e->dest = dst; tg_add_event(e); }
static void shields_event(Sub *src, Sub *sh, int on, float delay)
{
    TGEvent *e = tg_ev_new("TGBoolEvent", 0); e->type = (unsigned)tg_const("ET_SHIELDS_SET_STATE"); e->source = &src->o; e->dest = &sh->o; e->v.i = on;
    if (delay <= 0.0f) tg_add_event(e);
    else { extern TimerMgr *tg_timer_mgr(int); extern int tg_timer_add(TimerMgr *, TGTimer *); TGTimer *t = tg_new("TGTimer", sizeof(TGTimer), 0);
        t->next = t->start = (float)tg_game_time + delay; t->event = e; e->refcount++; tg_timer_add(tg_timer_mgr(0), t); }
}
static void delayed_event(TGEvent *e, float delay)
{ extern TimerMgr *tg_timer_mgr(int); extern int tg_timer_add(TimerMgr *, TGTimer *); TGTimer *t = tg_new("TGTimer", sizeof(TGTimer), 0); t->next = t->start = (float)tg_game_time + delay; t->event = e; e->refcount++; tg_timer_add(tg_timer_mgr(0), t); }
static int set_cloaking(Sub *c, int on)                                         /* SetCloaking 0055f110 */
{
    Phys *p = ship(c->ship); Sub *sh = p ? p->typed[S_SHIELD] : NULL;
    if (!on) {
        ship_event("ET_DECLOAK_BEGINNING", c, c->ship);
        if (c->cloak_state != 2) c->cloak_timer = CLOAK_TIME;
        c->cloaked = 0; c->cloak_state = 5;                                    /* 0055f770 */
        cloak_sound(c, "Uncloak");
        return 1;
    }
    if (eng_ShipSubsystem_IsDisabled(c)) return 0;
    if (sh) shields_event(c, sh, 0, 0.0f);
    { TGEvent *e = tg_ev_new("TGEvent", 0); e->type = (unsigned)tg_const("ET_CLOAK_BEGINNING"); e->source = &c->o; e->dest = c->ship;
      if (sh) delayed_event(e, SHIELD_DELAY); else tg_add_event(e); }
    if (c->cloak_state != 5) c->cloak_timer = 0;
    c->cloak_state = 2;
    return 1;
}
static void cloak_turn_off(Sub *c);
static void cloak_start(Sub *c) { eng_PoweredSubsystem_TurnOn(c); c->cloak_want = 1; }                                       /* 0055f360 */
static void cloak_stop(Sub *c)                                                   /* 0055f380 */
{ if (c->cloak_state == 1 || c->cloak_state == 2 || c->cloaked) set_cloaking(c, 0); c->cloak_want = 0; cloak_turn_off(c); }
static void cloak_turn_off(Sub *c)                                               /* TurnOff 0055f930 */
{ if (!c->on) return; eng_PoweredSubsystem_TurnOff(c); if (c->cloak_state == 3 || c->cloak_want) { cloak_stop(c); set_cloaking(c, 0); } }
static void cloak_update(Sub *c, float dt)
{
    int st, can_start, can_stop;
    powered_update(c, dt);
    if (c->cloak_state == 2) {
        c->cloak_timer += dt;
        if (c->cloak_timer / CLOAK_TIME >= 1.0f) { c->cloak_state = 3; ship_event("ET_CLOAK_COMPLETED", c, c->ship); c->cloaked = 1; }   /* OnCloakStarted */
    } else if (c->cloak_state == 5) {
        c->cloak_timer -= dt;
        if (c->cloak_timer / CLOAK_TIME <= 0.0f) {                               /* OnDecloakComplete */
            Phys *p = ship(c->ship); Sub *sh = p ? p->typed[S_SHIELD] : NULL;
            c->cloak_state = 0; ship_event("ET_DECLOAK_COMPLETED", c, c->ship);
            if (sh) { shields_event(c, sh, 1, SHIELD_DELAY); if (sh->power_pct_wanted <= 0.0f) eng_PoweredSubsystem_SetPowerPercentageWanted(sh, 1.0f); }
        }
    }
    if (!c->on) { powered_update(c, dt); return; }                               /* as in the exe: a second update when off */
    st = c->cloak_state; can_start = !(st == 1 || st == 2 || st == 3); can_stop = !(st == 4 || st == 5 || st == 0);
    if (c->cloak_want && can_start) { set_cloaking(c, 1); return; }
    if (c->cloak_want || !can_stop) { if (st != 3 || c->pw_pct >= 0.8f) return; cloak_stop(c); }
    set_cloaking(c, 0);
}
static int cloak_blocks_shields(Phys *p) { Sub *c = p ? p->typed[S_CLOAK] : NULL; return c && (c->cloak_state == 1 || c->cloak_state == 2 || c->cloak_state == 4 || c->cloak_state == 5 || c->cloaked); }
static void on_shields_set_state(TGEvent *e)                                     /* ShieldClass::HandleSetShieldState 0056aae0 */
{
    Sub *sh = e->dest && tg_is_ours(e->dest) ? sub(e->dest) : NULL;
    if (!sh || !tg_is_type(&sh->o, 0x8028)) return;
    if (e->v.i) eng_PoweredSubsystem_TurnOn(sh);
    else eng_PoweredSubsystem_TurnOff(sh);
}
long eng_CloakingSubsystem_IsCloaked(void *s) { Sub *x = sub(s); return x && x->cloaked; }
long eng_CloakingSubsystem_IsCloaking(void *s) { Sub *x = sub(s); return x && (x->cloak_state == 1 || x->cloak_state == 2); }
long eng_CloakingSubsystem_IsDecloaking(void *s) { Sub *x = sub(s); return x && (x->cloak_state == 4 || x->cloak_state == 5); }
long eng_CloakingSubsystem_IsTryingToCloak(void *s) { Sub *x = sub(s); return x && x->cloak_want; }
void eng_CloakingSubsystem_StartCloaking(void *s) { Sub *x = sub(s); if (x) cloak_start(x); }
void eng_CloakingSubsystem_StopCloaking(void *s) { Sub *x = sub(s); if (x) cloak_stop(x); }
void eng_CloakingSubsystem_InstantCloak(void *s)                                 /* 0055f3e0 */
{
    Sub *c = sub(s); Phys *p; if (!c || c->cloaked) return;
    cloak_start(c); p = ship(c->ship);
    if (p && p->typed[S_SHIELD]) shields_event(c, p->typed[S_SHIELD], 0, 0.0f);
    ship_event("ET_CLOAK_COMPLETED", c, c->ship); c->cloak_state = 3; c->cloaked = 1;
}
void eng_CloakingSubsystem_InstantDecloak(void *s)                               /* 0055f560 */
{ Sub *c = sub(s); if (!c || !c->cloaked) return; cloak_stop(c); c->cloaked = 0; ship_event("ET_DECLOAK_COMPLETED", c, c->ship); }
/* The cloak as the exe draws it (CloakingSubsystem::Update 0055e500, ApplyCloakLevel 0055e640, applyCloakLevelToNode 0055ee10): every vertex of the ship
   gets a new random alpha every frame from the visibility v = 1 - timer / CLOAK_TIME (v is 1 when visible, 0 when cloaked). Cloaking (states 1 and 2):
   f = (v <= 0.4 ? r : v - 0.4 + r) * v with r in [0, 0.4]; decloaking (states 4 and 5): f = (1.4 v - 0.4 + r) * v; the alpha is f plus 0.3 (at most 1)
   for the player's own ship and f for any other ship, so another ship is invisible when cloaked. Returns 0 when the ship is drawn normally; else the
   visibility v, the mode (1 cloaking, 2 decloaking, 3 cloaked) and whether it is the player's ship, for the renderer's vertex shader. */
int tg_ship_cloak_view(void *o, float *v, int *mode, int *is_player)
{
    extern long eng_ShipClass_IsPlayerShip(void *); Phys *p = ship(o); Sub *c; float f;
    if (!p || !(c = p->typed[S_CLOAK])) return 0;
    f = CLOAK_TIME > 0 ? c->cloak_timer / CLOAK_TIME : 1.0f; f = f < 0 ? 0 : f > 1 ? 1 : f;
    *is_player = eng_ShipClass_IsPlayerShip(o) != 0;
    switch (c->cloak_state) {
    case 1: case 2: *v = 1.0f - f; *mode = 1; return 1;
    case 4: case 5: *v = 1.0f - f; *mode = 2; return 1;
    default: if (c->cloaked) { *v = 0.0f; *mode = 3; return 1; } return 0;
    }
}
double eng_CloakingSubsystem_GetCloakTime(void) { return CLOAK_TIME; }
double eng_CloakingSubsystem_GetShieldDelay(void) { return SHIELD_DELAY; }
void eng_CloakingSubsystem_SetCloakTime(float v) { CLOAK_TIME = v; }
void eng_CloakingSubsystem_SetShieldDelay(float v) { SHIELD_DELAY = v; }
void *eng_CloakingSubsystem_Create(void) { Sub *x = sub_new("CloakingSubsystem"); x->on = 0; x->pw_mode = 2; return x; }

/* ================= warp engines (WarpEngineSubsystem 0056de70 .. 0056ef10) =================
   States (TransitionToState 0056e7d0, Update 0056e030): 0 not warping, 1 warp initiated, 2 beginning, 3 ending, 4 warping, 5 dewarp initiated,
   6 dewarp beginning, 7 dewarp ending. The WarpSequence script actions (Actions.EffectScriptActions.SetWarpStage) set 1, 3, 4, 5, 7, 0;
   Update moves 1 -> 2, 5 -> 6 and 2 -> 3. Warp speed 700 (DAT_008e532c) and warp effect time 2 s (DAT_008e5328) are class values.
   1: the ship becomes invincible (old flag kept). 2: timer = effect time, stop the impulse; 3: velocity = forward x warp speed.
   4: velocity 0. 5: the ship goes to the exit location (700 units behind the exit placement, along its forward axis) with the placement's
   rotation. 6 and 7: the ship slides from the exit location to the placement while the timer runs down (velocity 0); without a placement it
   flies forward at warp speed. 0: velocity 0, the ship goes to the placement, the AI held back during the warp is set, invincible restored.
   Approximations (marked): the model stretch (NiMatrix scale per frame while +0xb8 counts) and the cloak-style fade need the renderer; the
   ship has no model node here, so state 2 goes to 3 at once (the exe does the same when the ship has no node). Multiplayer exit points: not ported. */
static float WARP_EFFECT_TIME = 2.0f, WARP_SPEED = 700.0f;
static const float model_fwd[3] = {0.0f, 1.0f, 0.0f};                          /* DAT_00980df0 (set by 0045e460) */
static Sub *warp_sub(void *s) { Sub *x = sub(s); return x && tg_is_type(&x->o, 0x8025) ? x : NULL; }
extern void *eng_BaseObjectClass_GetWorldLocation(void *), *eng_BaseObjectClass_GetWorldRotation(void *); extern void eng_BaseObjectClass_SetMatrixRotation(void *, void *);
static void vel_set(void *o, const float *v) { Phys *p = phys(o); if (p) { memcpy(p->vel, v, 12); memset(p->accel, 0, 12); } }
static void ship_forward(void *o, float *out) { float *f = eng_BaseObjectClass_GetWorldForwardTG(o); if (f) { memcpy(out, f, 12); free(f); } else memcpy(out, model_fwd, 12); }
static void *warp_placement(Sub *w) { TGObject *o = w->warp_place ? tg_find(w->warp_place) : NULL; return o && !o->dead ? o : NULL; }
static void warp_exit_location(Sub *w, float *out)                               /* GetWarpExitLocation 0056eae0 (single player) */
{
    void *pl = warp_placement(w); float *pos, d[3]; int i;
    if (!pl) { pos = eng_BaseObjectClass_GetWorldLocation(w->ship); if (pos) { memcpy(out, pos, 12); free(pos); } else memset(out, 0, 12); return; }
    ship_forward(pl, d);                                                           /* the placement's rotation x model forward */
    pos = eng_BaseObjectClass_GetWorldLocation(pl);
    for (i = 0; i < 3; i++) out[i] = (pos ? pos[i] : 0.0f) - d[i] * WARP_SPEED * WARP_EFFECT_TIME * 0.5f;
    free(pos);
}
static void warp_slide(Sub *w, void *o, float dt)                               /* states 6 and 7: from the exit location to the placement */
{
    void *pl = warp_placement(w); float ex[3], *pp, f, d[3], zero[3] = {0, 0, 0}; int i;
    if (!pl) { float fw[3]; ship_forward(o, fw); for (i = 0; i < 3; i++) fw[i] *= WARP_SPEED; vel_set(o, fw); return; }
    warp_exit_location(w, ex); pp = eng_BaseObjectClass_GetWorldLocation(pl);
    f = w->warp_timer / WARP_EFFECT_TIME; if (f < 0.0f) f = 0.0f;
    vel_set(o, zero);
    for (i = 0; i < 3; i++) d[i] = ex[i] + ((pp ? pp[i] : 0.0f) - ex[i]) * (1.0f - f);
    free(pp);
    eng_BaseObjectClass_SetTranslate(o, d);
    w->warp_timer -= dt;
}
void eng_WarpEngineSubsystem_TransitionToState(void *s, int st)
{
    Sub *w = warp_sub(s); void *o; Phys *p; float zero[3] = {0, 0, 0}, v[3]; int i;
    if (!w || w->warp_state == st || !(o = w->ship) || !(p = ship(o))) return;
    switch (st) {
    case 0: {
        void *pl = warp_placement(w), *ai;
        vel_set(o, zero);
        if (pl) { float *pp = eng_BaseObjectClass_GetWorldLocation(pl); if (pp) { eng_BaseObjectClass_SetTranslate(o, pp); free(pp); } }
        w->warp_place = 0; w->warp_timer = 0.0f;
        if ((ai = w->warp_ai)) { w->warp_state = 0; w->warp_ai = NULL; p->ai = ai; }
        p->invincible = w->warp_inv; w->warp_inv = 0;
        break; }
    case 1: w->warp_inv = p->invincible; p->invincible = 1; break;
    case 2: w->warp_timer = WARP_EFFECT_TIME; w->warp_ticks = 0; ship_forward(o, v); eng_ShipClass_SetImpulse(o, 0.0f, v, 1); break;
    case 4: vel_set(o, zero); break;
    case 5: {
        float ex[3]; void *pl = warp_placement(w), *r;
        warp_exit_location(w, ex); eng_BaseObjectClass_SetTranslate(o, ex);
        r = eng_BaseObjectClass_GetWorldRotation(pl ? pl : o); if (r) { eng_BaseObjectClass_SetMatrixRotation(o, r); free(r); }
        break; }
    case 6: w->warp_timer = WARP_EFFECT_TIME; break;
    case 7: vel_set(o, zero); break;
    }
    (void)i;
    w->warp_state = st;
}
static void warp_update(Sub *w, float dt)                                        /* Update 0056e030 */
{
    void *o = w->ship; float v[3]; int i;
    powered_update(w, dt);
    if (!o || !ship(o)) return;
    switch (w->warp_state) {
    case 1: eng_WarpEngineSubsystem_TransitionToState(w, 2); break;
    case 2: eng_WarpEngineSubsystem_TransitionToState(w, 3); break;               /* no model node: no stretch frames */
    case 3: ship_forward(o, v); for (i = 0; i < 3; i++) v[i] *= WARP_SPEED; vel_set(o, v); break;
    case 5: eng_WarpEngineSubsystem_TransitionToState(w, 6); break;
    case 6: case 7: warp_slide(w, o, dt); break;
    }
}
int tg_warp_hold_ai(void *o, void *ai)                                           /* ShipClass::AddAI 005ae5a0 -> startWarp 0056e700 */
{
    Phys *p = ship(o); Sub *w = p ? p->typed[S_WARP] : NULL;
    if (!w || w->warp_state == 0) return 0;
    w->warp_ai = ai; return 1;
}
int tg_warp_state(void *o) { Phys *p = ship(o); return p && p->typed[S_WARP] ? p->typed[S_WARP]->warp_state : 0; }
long eng_WarpEngineSubsystem_GetWarpState(void *s) { Sub *w = warp_sub(s); return w ? w->warp_state : 0; }
void eng_WarpEngineSubsystem_SetWarpState(void *s, int st) { Sub *w = warp_sub(s); if (w) w->warp_state = st; }
long eng_WarpEngineSubsystem_Warp(void *s) { return warp_sub(s) && eng_ShipSubsystem_IsDisabled(s); }          /* 0056ead0 returns IsDisabled */
void *eng_WarpEngineSubsystem_GetPlacement(void *s) { Sub *w = warp_sub(s); return w ? warp_placement(w) : NULL; }
void eng_WarpEngineSubsystem_SetPlacement(void *s, void *pl) { Sub *w = warp_sub(s); if (w) w->warp_place = pl && tg_is_ours(pl) ? ((TGObject *)pl)->id : 0; }
void eng_WarpEngineSubsystem_SetExitPoint(void *s, void *pt) { Sub *w = warp_sub(s); if (w && pt) memcpy(w->warp_exit, pt, 12); }
void *eng_WarpEngineSubsystem_GetWarpExitLocation(void *s) { Sub *w = warp_sub(s); float e[3]; if (!w) return NULL; warp_exit_location(w, e); return pt(e); }
void *eng_WarpEngineSubsystem_GetWarpExitRotation(void *s) { Sub *w = warp_sub(s); void *pl; if (!w) return NULL; pl = warp_placement(w); return eng_BaseObjectClass_GetWorldRotation(pl ? pl : w->ship); }
double eng_WarpEngineSubsystem_GetWarpEffectTime(void) { return WARP_EFFECT_TIME; }
void eng_WarpEngineSubsystem_SetWarpEffectTime(float v) { WARP_EFFECT_TIME = v; }
double eng_WarpEngineSubsystem_GetWarpSpeed(void) { return WARP_SPEED; }
void eng_WarpEngineSubsystem_SetWarpSpeed(float v) { WARP_SPEED = v; }
void *eng_WarpEngineSubsystem_GetWarpSequence(void *s) { extern void *eng_TGAction_Cast(void *); Sub *w = warp_sub(s); TGObject *a = w && w->warp_seq ? tg_find(w->warp_seq) : NULL; return a && !a->dead ? eng_TGAction_Cast(a) : NULL; }
static void on_set_warp_sequence(TGEvent *e)                                     /* SetWarpSequence 0056ed10: the source action, or none */
{
    extern void *eng_TGAction_Cast(void *); Sub *w = e->dest && tg_is_ours(e->dest) ? warp_sub(e->dest) : NULL; void *a;
    if (!w) return;
    a = e->source && tg_is_ours(e->source) ? eng_TGAction_Cast(e->source) : NULL;
    w->warp_seq = a ? ((TGObject *)a)->id : 0;
}

static void on_set_alert_level(TGEvent *e)                                       /* HandleChangeAlertLevel 005b0760: events whose destination is a ship */
{ if (e->dest && tg_is_ours(e->dest) && !((TGObject *)e->dest)->dead && ship(e->dest)) eng_ShipClass_SetAlertLevel(e->dest, (int)e->v.i); }

/* native handlers of the ship systems (registered once by engine_init) */
/* ================= sounds of ships =================
   PhaserBank::StartFiring 00571f40 / StopFiring 00571200: the fire sound named "<fire sound> Start" and the looping "<fire sound> Loop" both play from
   the ship's node while the bank fires (nothing plays unless both exist; priority 0.9 for the player, 0.6 for others, the start sound 0.01 below).
   TractorBeamProjector::StartFiringBeam 0057f580 / StopFiring 0057f080: the fire sound loops while the beam holds (0.9 player, 0.5 others).
   PulseWeapon::CreateProjectile 00576080 plays the fire sound once per shot (0.9 / 0.5); TorpedoTube::CreateTorpedo 0057cd90 the launch sound of the
   ammo type (0.9 / 0.6). All of them attach the sound to the ship's node and add it to the sound region of the ship's set.
   CloakingSubsystem startCloakEffects 0055f660 and FUN_0055f770 play "Cloak" and "Uncloak" from the ship (region needed). */
static void *snd_region_of(void *o)                                           /* TGSoundRegion::GetRegion(set name of the ship's set) */
{
    void *set = eng_BaseObjectClass_GetContainingSet(o); const char *n = set ? eng_SetClass_GetName(set) : NULL;
    return n ? eng_TGSoundRegion_GetRegion((char *)n) : NULL;
}
static float snd_prio(void *o, float other) { return o == eng_Game_GetCurrentPlayer() ? 0.9f : other; }
static void *snd_named2(const char *base, const char *suffix)
{
    char n[256]; if (!base) return NULL;
    snprintf(n, sizeof n, "%s%s", base, suffix); return tg_sound_named(n);
}
static void phaser_sounds(void *o, Sub *w, int on)
{
    if (!on) { int k; for (k = 0; k < 2; k++) if (w->snd[k]) { tg_sound_stop_slot(w->snd[k]); w->snd[k] = 0; } return; }
    if (w->snd[0] || w->snd[1] || !w->prop || !eng_BaseObjectClass_GetContainingSet(o)) return;
    {   const char *fs = eng_EnergyWeaponProperty_GetFireSound(w->prop);
        void *st = snd_named2(fs, " Start"), *lp = snd_named2(fs, " Loop"), *r; float p;
        if (!st || !lp) return;
        tg_sound_attach(st, o); tg_sound_attach(lp, o); tg_sound_set_loop(lp, 1);
        if ((r = snd_region_of(o))) { tg_region_add(r, st); tg_region_add(r, lp); }
        p = snd_prio(o, 0.6f); tg_sound_set_priority(st, p - 0.01f); tg_sound_set_priority(lp, p);
        w->snd[0] = tg_sound_play(st); w->snd[1] = tg_sound_play(lp);
    }
}
static void tractor_sounds(void *o, Sub *w, int on)
{
    if (!on) { if (w->snd[0]) { tg_sound_stop_slot(w->snd[0]); w->snd[0] = 0; } return; }
    if (w->snd[0] || !w->prop || !eng_BaseObjectClass_GetContainingSet(o)) return;
    {   void *s = tg_sound_named(eng_EnergyWeaponProperty_GetFireSound(w->prop)), *r;
        if (!s) return;
        tg_sound_attach(s, o); tg_sound_set_loop(s, 1);
        if ((r = snd_region_of(o))) { tg_region_add(r, s); tg_sound_set_priority(s, snd_prio(o, 0.5f)); w->snd[0] = tg_sound_play(s); }
    }
}
static void shot_sound(void *o, const char *name, float other_prio)            /* a launch or pulse shot sound: from the ship's node, once */
{
    void *s = name ? tg_sound_named(name) : NULL, *r;
    if (!s) return;
    tg_sound_attach(s, o);
    if ((r = snd_region_of(o))) tg_region_add(r, s);
    tg_sound_set_priority(s, snd_prio(o, other_prio)); tg_sound_play(s);
}
static void ship_stop_weapon_sounds(Phys *p)                                  /* a ship that died or left: the beams are over */
{
    Sub **v; int n, i;
    all_subs(p, &v, &n);
    for (i = 0; i < n; i++) if (v[i]->snd[0] || v[i]->snd[1]) { int k; for (k = 0; k < 2; k++) if (v[i]->snd[k]) { tg_sound_stop_slot(v[i]->snd[k]); v[i]->snd[k] = 0; } }
    free(v);
}
static void cloak_sound(Sub *c, const char *name)
{
    void *o = c->ship, *s, *r;
    if (!o || !eng_BaseObjectClass_GetContainingSet(o) || !(r = snd_region_of(o)) || !(s = tg_sound_named(name))) return;
    tg_sound_attach(s, o); tg_region_add(r, s); tg_sound_play(s);
}
static void on_cloak_beginning(TGEvent *e)                                    /* startCloakEffects 0055f660 runs when the cloak sequence reaches it */
{
    Sub *c = e->source && tg_is_ours(e->source) ? sub(e->source) : NULL;
    if (c && tg_is_type(&c->o, 0x8024)) cloak_sound(c, "Cloak");
}

/* SetClass::UpdateShipEngineSounds 00413cb0: in the rendered set the four ships nearest to the camera, within 35 units of it, hum with the engine sound of their
   impulse engine property (looping, minimum distance 0, maximum 35, from the ship's node); the hum of a ship that is not among them any more stops.
   Other sets keep no engine sounds. */
extern void *tg_render_set(void); extern void *tg_render_camera(void); extern int tg_space_set_objects(void *sp, void ***out); extern int tg_space_world_pos(void *o, float out[3]);
extern const char *eng_ImpulseEngineProperty_GetEngineSound(void *); extern void tg_sound_set_min_max(void *, float, float);
static struct { unsigned id; long slot; } esnd[4]; static int n_esnd; static void *esnd_set;
static void engine_sounds_update(void)
{
    void *set = tg_render_set(), *cam = tg_render_camera(), **objs; float cp[3]; int n, i, k, nn = 0; void *near[4]; float nd[4];
    if (set != esnd_set) { for (i = 0; i < n_esnd; i++) tg_sound_stop_slot(esnd[i].slot); n_esnd = 0; esnd_set = set; }
    if (!set) return;
    if (cam && tg_space_world_pos(cam, cp)) {
        n = tg_space_set_objects(set, &objs);
        for (i = 0; i < n; i++) {
            void *o = objs[i]; Phys *p = ship(o); float q[3], d2;
            if (!p || p->dead || !p->typed[S_IMPULSE] || !tg_space_world_pos(o, q)) continue;
            d2 = (q[0] - cp[0]) * (q[0] - cp[0]) + (q[1] - cp[1]) * (q[1] - cp[1]) + (q[2] - cp[2]) * (q[2] - cp[2]);
            if (d2 > 35.0f * 35.0f) continue;
            for (k = nn < 4 ? nn : 3; k > 0 && nd[k - 1] > d2; k--) { near[k] = near[k - 1]; nd[k] = nd[k - 1]; }
            if (k < 4) { near[k] = o; nd[k] = d2; if (nn < 4) nn++; }
        }
    }
    for (i = 0; i < n_esnd; ) {                                                  /* the ships that are not near any more */
        int found = 0; for (k = 0; k < nn; k++) if (((TGObject *)near[k])->id == esnd[i].id) found = 1;
        if (found) i++; else { tg_sound_stop_slot(esnd[i].slot); esnd[i] = esnd[--n_esnd]; }
    }
    for (k = 0; k < nn; k++) {                                                  /* the near ships that do not hum yet */
        void *o = near[k], *snd, *r; Phys *p = ship(o); const char *name; int have = 0;
        for (i = 0; i < n_esnd; i++) if (esnd[i].id == ((TGObject *)o)->id) have = 1;
        if (have || n_esnd >= 4 || !p->typed[S_IMPULSE]->prop || !(name = eng_ImpulseEngineProperty_GetEngineSound(p->typed[S_IMPULSE]->prop))) continue;
        if (!(r = snd_region_of(o)) || !(snd = tg_sound_named(name))) continue;
        tg_region_add(r, snd); tg_sound_attach(snd, o); tg_sound_set_loop(snd, 1); tg_sound_set_min_max(snd, 0.0f, 35.0f);
        esnd[n_esnd].id = ((TGObject *)o)->id; esnd[n_esnd].slot = tg_sound_play(snd); n_esnd++;
    }
}

void tg_ship_init(void)
{
    tg_register_native_broadcast((int)tg_const("ET_CLOAK_BEGINNING"), on_cloak_beginning);
    tg_register_native_broadcast((int)tg_const("ET_SUBSYSTEM_DAMAGED"), on_sub_damaged);
    tg_register_native_broadcast((int)tg_const("ET_REPAIR_COMPLETED"), on_repair_done);
    tg_register_native_broadcast((int)tg_const("ET_REPAIR_CANNOT_BE_COMPLETED"), on_repair_done);
    tg_register_native_broadcast((int)tg_const("ET_REPAIR_INCREASE_PRIORITY"), on_increase_priority);
    tg_register_native_broadcast((int)tg_const("ET_ADD_TO_REPAIR_LIST"), on_add_to_repair_list);
    tg_register_native_broadcast((int)tg_const("ET_SENSORS_SHIP_IDENTIFIED"), on_ship_identified);
    tg_register_native_broadcast((int)tg_const("ET_ENTERED_SET"), on_enter_set);
    tg_register_native_broadcast((int)tg_const("ET_EXITED_SET"), on_exit_set);
    tg_register_native_broadcast((int)tg_const("ET_SET_PLAYER"), on_sensor_set_player);
    tg_register_native_broadcast((int)tg_const("ET_DECLOAK_COMPLETED"), on_ship_decloaked);
    tg_register_native_broadcast((int)tg_const("ET_SHIELDS_SET_STATE"), on_shields_set_state);
    tg_register_native_broadcast((int)tg_const("ET_SET_ALERT_LEVEL"), on_set_alert_level);
    tg_register_native_broadcast((int)tg_const("ET_SET_WARP_SEQUENCE"), on_set_warp_sequence);
}

/* ================= effect data for the renderer (tg_fx.h) =================
   Beams: each firing phaser bank of a ship in the set, from the bank's hardpoint position (model position x scale, in the ship's axes) to
   the point where beam() applies the damage (the target's bounding sphere, toward the ship). Colours: outer shell (+0xd4) and inner core
   (+0x104) of the PhaserProperty; radius: MainRadius (+0x118) x ship scale. Approximation (marked): the original beam is a tapered tube
   (NumSides, TaperRadius, CoreScale, textures PhaserTextureStart/End) drawn from the bank's arc toward the target subsystem; here it is a
   straight glow line to the bounding sphere. */
static void *fx_set; static TgFxBeam *fx_bout; static TgFxShot *fx_sout; static TgFxRay *fx_rout; static int fx_n, fx_max;
static void fx_beam_one(void *o)
{
    extern void *eng_SubsystemProperty_GetPosition(void *); extern double eng_ObjectClass_GetRadius(void *);
    extern const char *eng_PhaserProperty_GetTextureName(void *);
    Phys *p = ship(o); Sub *s; float sp[3], f[3], u[3], r[3], sc, tp[3], tf[3], tu[3], tr[3], d[3], l, dist; int i, k;
    if (!p || p->dead || eng_BaseObjectClass_GetContainingSet(o) != fx_set || !(s = p->typed[S_PHASER])) return;
    tg_space_pose(o, sp, f, u, r, &sc); if (sc <= 0) sc = 1;
    for (k = 0; k < s->nkids && fx_n < fx_max; k++) {
        Sub *w = s->kids[k]; TgFxBeam *b = &fx_bout[fx_n]; PropObj *pp = pobj(w->prop); float *q;
        if (!w->firing || !w->beam_target || !pp || !ship(w->beam_target)) continue;
        q = eng_SubsystemProperty_GetPosition(w->prop);
        for (i = 0; i < 3; i++) b->a[i] = sp[i] + (q ? (q[0] * r[i] + q[1] * f[i] + q[2] * u[i]) * sc : 0);
        free(q);
        tg_space_pose(w->beam_target, tp, tf, tu, tr, NULL);
        for (i = 0; i < 3; i++) d[i] = tp[i] - b->a[i];
        l = vlen(d); dist = l - (float)eng_ObjectClass_GetRadius(w->beam_target); if (dist < 0) dist = 0;
        for (i = 0; i < 3; i++) b->b[i] = l > 1e-6f ? b->a[i] + d[i] / l * dist : tp[i];
        for (i = 0; i < 4; i++) { b->col[0][i] = PF(pp, 0xd4 + 4 * i); b->col[1][i] = PF(pp, 0xe4 + 4 * i); b->col[2][i] = PF(pp, 0xf4 + 4 * i); b->col[3][i] = PF(pp, 0x104 + 4 * i); }
        b->sides = (int)P32S(pp, 0x114); b->radius = PF(pp, 0x118) * sc; b->taper_r = PF(pp, 0x11c) * sc; b->core_scale = PF(pp, 0x120); b->taper_ratio = PF(pp, 0x124);
        b->taper_min = PF(pp, 0x128) * sc; b->taper_max = PF(pp, 0x12c) * sc; b->ltile = PF(pp, 0x130) / sc; b->ptile = PF(pp, 0x134); b->speed = PF(pp, 0x138); b->scale = sc;
        if (b->radius <= 0) b->radius = 0.1f * sc;
        {   static struct { Sub *w; double t0, seen; } tab[64]; int z, fz = 0;                    /* when the beam began (it grows for 0.35 s: CreateBeam 00572800) */
            for (z = 0; z < 64; z++) if (tab[z].w == w) break;
            if (z == 64) { for (z = 0; z < 64; z++) if (tab[z].seen < tg_game_time - 1.0) { fz = z; break; } z = fz; tab[z].w = NULL; }
            if (tab[z].w != w || tab[z].seen < tg_game_time - 0.25) tab[z].t0 = tg_game_time;
            tab[z].w = w; tab[z].seen = tg_game_time; b->age = (float)(tg_game_time - tab[z].t0); }
        b->key = w; b->owner = o; b->tex = eng_PhaserProperty_GetTextureName(w->prop);                   /* the beam's own texture (tube and core) */
        fx_n++;
    }
}
int tg_fx_beams(void *set, TgFxBeam *out, int max) { fx_set = set; fx_bout = out; fx_n = 0; fx_max = max; tg_space_each(0x8008, fx_beam_one); return fx_n; }
/* Projectiles: torpedoes and pulse shots of the set, with the look their script asked for; the glow pulses with the age. A projectile whose
   script made no model gets an orange glow. */
static void fx_shot_one(void *o)
{
    Phys *p = torp(o); TgFxShot *t; float sc, l, k; int i;
    if (!p || p->dead || fx_n >= fx_max || eng_BaseObjectClass_GetContainingSet(o) != fx_set) return;
    t = &fx_sout[fx_n];
    tg_space_pose(o, t->p, t->f, t->u, t->r, &sc);
    l = vlen(p->vel); for (i = 0; i < 3; i++) t->dir[i] = l > 1e-6f ? p->vel[i] / l : t->f[i];
    t->sides = 0; t->width = t->in_r = t->in_l = 0;
    if (!p->fx_kind) { t->kind = 1; for (i = 0; i < 4; i++) { t->shell[i] = i == 3 || i == 0 ? 1.0f : 0.4f; t->core[i] = 1.0f; } t->core_size = 0.2f; t->glow_size = 0.5f; t->len = 0; t->core_tex = t->glow_tex = NULL; fx_n++; return; }
    t->kind = p->fx_kind; memcpy(t->shell, p->fx_shell, 16); memcpy(t->core, p->fx_core, 16);
    t->core_tex = p->fx_core_tex; t->glow_tex = p->fx_glow_tex; t->len = p->fx_len;
    if (p->fx_kind == 2) { t->sides = p->fx_sides; t->width = p->fx_width; t->in_r = p->fx_in_r; t->in_l = p->fx_in_l; t->core_size = t->glow_size = 0; fx_n++; return; }
    /* the glow quad (FUN_004f5780): side = |(max - min) * sin(phase) + min|, the phase grows by rate a second and wraps at pi; the core quad has the fixed side a3 */
    k = sinf(fmodf(p->age * p->fx_glow_rate, 3.14159265f));
    t->core_size = p->fx_core_size; t->glow_size = fabsf((p->fx_glow_max - p->fx_glow_min) * k + p->fx_glow_min);
    fx_n++;
}
int tg_fx_shots(void *set, TgFxShot *out, int max) { fx_set = set; fx_sout = out; fx_n = 0; fx_max = max; tg_space_each(0x8009, fx_shot_one); return fx_n; }
/* Flare rays of the torpedoes (FUN_004f4630 builds them, the controller FUN_004f5780 / FUN_004f5600 runs them). A ray is a quad (0,-w,0) (0,-w,h) (0,w,h)
   (0,w,0) with w = 0.4 * core side and h = ray length, turned by a random Y rotation and then a random Z rotation (fixed for the torpedo's life). All of
   them spin about the torpedo's y axis at a4 radians a second. Each ray cycles: a life of ray_life seconds (alpha rises over the first 30 percent, stays
   1 up to 70 percent and falls over the last 30 percent), then waits a random 0.5 to 1 times twice the life and starts again. The first update starts the
   rays at random points in their life. */
static float rng01(Phys *p) { p->fx_rng ^= p->fx_rng << 13; p->fx_rng ^= p->fx_rng >> 17; p->fx_rng ^= p->fx_rng << 5; return (float)(p->fx_rng & 0x7fff) / 32767.0f; }
static void fx_rays_one(void *o)
{
    Phys *p = torp(o); float f[3], u[3], r[3], pos[3], sc, spin, cs, sn, life, gap, t = (float)tg_game_time; int i, k, m;
    if (!p || p->dead || !p->fx_nflares || p->fx_kind != 1 || eng_BaseObjectClass_GetContainingSet(o) != fx_set || fx_n + p->fx_nflares > fx_max) return;
    life = p->fx_flare_life > 1e-4f ? p->fx_flare_life : 1e-4f; gap = 2.0f * p->fx_flare_life;
    if (!p->fx_ray_init) {
        float w = p->fx_flare_w, h = p->fx_flare_len;
        for (i = 0; i < p->fx_nflares; i++) {
            float a = rng01(p) * 6.2831853f, b = rng01(p) * 6.2831853f, ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
            static const float q[4][3] = {{0, -1, 0}, {0, -1, 1}, {0, 1, 1}, {0, 1, 0}};
            for (k = 0; k < 4; k++) {
                float x = 0, y = q[k][1] * w, z = q[k][2] * h, x1 = cb * x + sb * z, z1 = -sb * x + cb * z;      /* Y rotation, then Z rotation */
                p->fx_ray_pt[i * 12 + k * 3 + 0] = ca * x1 - sa * y; p->fx_ray_pt[i * 12 + k * 3 + 1] = sa * x1 + ca * y; p->fx_ray_pt[i * 12 + k * 3 + 2] = z1;
            }
            p->fx_ray_start[i] = t - rng01(p) * life;
        }
        p->fx_ray_init = 1;
    }
    tg_space_pose(o, pos, f, u, r, &sc);
    spin = fmodf(p->age * p->fx_spin, 6.2831853f); cs = cosf(spin); sn = sinf(spin);
    for (i = 0; i < p->fx_nflares; i++) {
        float e = t - p->fx_ray_start[i], x, al = 0.0f; TgFxRay *d;
        if (e > life) {                                                /* FUN_004f5600: the life is over; the next one starts after a random gap */
            e -= life;
            if (e > gap + life) e = (gap + life) * rng01(p);
            p->fx_ray_start[i] = (gap * 0.5f + rng01(p) * gap * 0.5f) - e + t; e = 0.0f;
        }
        x = e / life;
        if (x >= 0.0f) { if (x >= 0.3f) { al = 1.0f; if (x >= 0.7f) al = x < 1.0f ? 1.0f - (x - 0.7f) * 3.3333333f : 0.0f; } else al = x * 3.3333333f; }
        if (al <= 0.0f) continue;
        d = &fx_rout[fx_n]; d->tex = p->fx_flare_tex; d->col[0] = p->fx_flare[0]; d->col[1] = p->fx_flare[1]; d->col[2] = p->fx_flare[2]; d->col[3] = al;
        for (k = 0; k < 4; k++) {
            const float *q = &p->fx_ray_pt[i * 12 + k * 3]; float lx = cs * q[0] + sn * q[2], ly = q[1], lz = -sn * q[0] + cs * q[2];   /* the spin (Y rotation) */
            for (m = 0; m < 3; m++) d->v[k][m] = pos[m] + lx * r[m] + ly * f[m] + lz * u[m];
        }
        fx_n++;
    }
}
int tg_fx_rays(void *set, TgFxRay *out, int max) { fx_set = set; fx_rout = out; fx_n = 0; fx_max = max; tg_space_each(0x8009, fx_rays_one); return fx_n; }
