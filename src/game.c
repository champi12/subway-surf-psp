/* game.c - traduccion de la logica de Subway Surfers 1.0 (Unity/C#) a C.
 * Clases originales: Game, Character, Running, Jetpack, CharacterCamera, FollowingGuard, Hoverboard,
 * SuperSneakers, CoinMagnet, DoubleScoreMultiplier, GameStats, CharacterPickupParticles, RunnerAnimPlayer.
 * Los valores numericos son los guardados en la escena del juego (level0), no los de por defecto del codigo.
 */
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "game.h"
#include "scene.h"
#include "physics.h"
#include "anim.h"
#include "render.h"
#include "audio.h"
#include "spawn.h"
#include "devlog.h"
#include "lang.h"
#include "data.h"
#include "hud.h"
#include "missions.h"

GameInfo ginfo;
float game_time;

/* ------------------------------------------------------------ curvas de animacion (AnimationCurve) */
typedef struct { float t, v, in, out; } Key;

static float curve_eval(const Key *k, int n, float t)
{
    if (n <= 0) return 0;
    if (t <= k[0].t) return k[0].v;
    if (t >= k[n - 1].t) return k[n - 1].v;
    int i = 0;
    while (i < n - 2 && t > k[i + 1].t) i++;
    float dt = k[i + 1].t - k[i].t;
    if (dt <= 0) return k[i].v;
    float s = (t - k[i].t) / dt, s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * k[i].v + (s3 - 2 * s2 + s) * k[i].out * dt + (-2 * s3 + 3 * s2) * k[i + 1].v +
           (s3 - s2) * k[i + 1].in * dt;
}

static const Key superSneakersJumpCurve[] = {{0, 0, 1, 6.312727f}, {0.1369736f, 0.538523f, 1.658518f, 1.658518f}, {1, 1, 0, 0}};
static const Key transitionFromJetpackCurve[] = {{0, 0, 19.99203f, 19.99203f}, {0.04928456f, 0.50026f, 1.818718f, 1.818718f}, {1, 1, 0, 0}};
static const Key flyAheadCameraCurve[] = {{0, 0, 12.01637f, 12.01637f}, {0.07373233f, 0.5023816f, 1.880047f, 1.880047f}, {1, 1, 0, 0}};

/* ------------------------------------------------------------ constantes de la escena */
#define GRAVITY 200.0f
#define JUMP_HEIGHT_NORMAL 20.0f
#define JUMP_HEIGHT_SUPER 40.0f
#define VERTICAL_FALL_SPEED_LIMIT (-10.0f)
#define STUMBLE_CORNER_TOLERANCE 15.0f
#define STUMBLE_DECAY_TIME 5.0f
#define CHARACTER_ANGLE 45.0f
#define COLLIDER_TRACK_WIDTH 17.0f
#define SS_APEX_RATIO 0.5f
#define SPEED_MIN 110.0f
#define SPEED_MAX 220.0f
#define SPEED_RAMP 180.0f
#define CHANGE_TRACK_LENGTH 30.0f
#define CAM_OFFSET_Y 33.0f
#define CAM_OFFSET_Z (-33.0f)
#define CAM_AIM 20.0f
#define CAM_X_SMOOTH 0.05f
#define CAM_Y_SMOOTH 0.1f
#define TUNNEL_DELTA (-20.0f)
#define GUARD_DIST_MIN 10.0f
#define GUARD_DIST_MAX 30.0f
#define GUARD_CATCHUP 0.7f
#define GUARD_RESET 1.5f
#define GUARD_LG_SMOOTH 0.3f
#define GUARD_X_SMOOTH 0.1f
#define GUARD_VOLUME 0.25f
#define JET_SPEEDUP 2.0f
#define JET_FLY_HEIGHT 95.0f
#define JET_COIN_OFFSET 200.0f
#define JET_FLY_AHEAD 1.5f
#define JET_STOP_BEFORE_LANDING 50.0f
#define JET_CHANGE_TRACK_LENGTH 60.0f
#define JET_CAM_AIM 15.0f
#define HB_COOLDOWN_DIST 116.0f
#define HB_SLOWMO_DIST 90.0f
#define HB_REMOVE_DIST 250.0f
#define HB_PARTICLE_DELAY 0.14f
#define HB_COOLDOWN 5.0f
#define MAGNET_PULL 150.0f
#define SNEAKERS_PULL 200.0f
#define INAIR_STAY 60.0f
#define INAIR_COIN_DIST 30.0f

/* ------------------------------------------------------------ sonidos */
static ClipInfo snd_dodgeL, snd_dodgeR, snd_hL, snd_hR, snd_stumble, snd_stumbleBush, snd_stumbleSide, snd_die,
    snd_coin, snd_powerup, snd_powerdown, snd_hcrash, snd_runL, snd_runR, snd_superL, snd_superR, snd_groundL,
    snd_groundR, snd_jump, snd_roll, snd_landing, snd_hroll, snd_hkick, snd_hminikick, snd_hlong, snd_hmid,
    snd_hshort, snd_hlandgrind, snd_superjump, snd_paint, snd_painthigh, snd_shake, snd_deathtrain, snd_bodyfall,
    snd_catch, snd_startpan;
static int sfx_guardloop = -1, sfx_trainpass = -1, sfx_magnetloop = -1, sfx_jetloop = -1, sfx_tap = -1;
static int h_guardloop = -1, h_magnetloop = -1, h_jetloop = -1;

static void ci(ClipInfo *c, const char *name, float p0, float p1, float v0, float v1)
{
    c->sfx = sfx_find(name); c->minPitch = p0; c->maxPitch = p1; c->minVol = v0; c->maxVol = v1;
}

static void sounds_init(void)
{
    ci(&snd_dodgeL, "Hr_run_dodge", 0.8f, 1.0f, 0.7f, 1.0f);
    ci(&snd_dodgeR, "Hr_run_dodge", 0.93f, 1.3f, 0.7f, 1.0f);
    ci(&snd_hL, "Hr_swishFMid", 0.7f, 0.97f, 0.6f, 0.9f);
    ci(&snd_hR, "Hr_swishFMid", 0.93f, 1.3f, 0.6f, 0.9f);
    ci(&snd_stumble, "Hr_stumble", 0.9f, 1.0f, 0.85f, 0.95f);
    ci(&snd_stumbleBush, "Hr_stumble_bush", 0.8f, 1.1f, 0.7f, 0.7f);
    ci(&snd_stumbleSide, "Hr_stumble_side", 0.8f, 1.1f, 0.7f, 0.9f);
    ci(&snd_die, "Hr_death", 0.9f, 1.0f, 0.8f, 0.9f);
    ci(&snd_coin, "Hr_coin", 0.8f, 1.1f, 0.05f, 0.225f);
    ci(&snd_powerup, "Hr_powerUp", 0.8f, 1.1f, 0.6f, 0.7f);
    ci(&snd_powerdown, "Hr_powerDown", 0.8f, 1.1f, 0.5f, 0.7f);
    ci(&snd_hcrash, "Hr_H_crash", 0.9f, 1.0f, 0.5f, 0.6f);
    ci(&snd_runL, "Hr_run_leftFoot", 0.8f, 1.1f, 0.06f, 0.4f);
    ci(&snd_runR, "Hr_run_rightFoot", 0.8f, 1.1f, 0.06f, 0.4f);
    ci(&snd_superL, "Hr_superSneakers_leftFoot", 0.8f, 1.1f, 0.6f, 0.95f);
    ci(&snd_superR, "Hr_superSneakers_rightFoot", 0.8f, 1.1f, 0.6f, 0.95f);
    ci(&snd_groundL, "Hr_run_leftFoot", 0.8f, 0.95f, 0.1f, 0.7f);
    ci(&snd_groundR, "Hr_run_rightFoot", 0.95f, 1.1f, 0.1f, 0.7f);
    ci(&snd_jump, "Hr_run_jump", 0.8f, 1.1f, 0.6f, 0.8f);
    ci(&snd_roll, "Hr_run_roll", 0.8f, 1.1f, 0.5f, 0.7f);
    ci(&snd_landing, "Hr_landing", 0.8f, 1.1f, 0.4f, 0.6f);
    ci(&snd_hroll, "Hr_swishCShort", 1.2f, 1.5f, 0.1f, 0.4f);
    ci(&snd_hkick, "Hr_H_kick", 0.8f, 1.1f, 0.3f, 0.6f);
    ci(&snd_hminikick, "Hr_H_kick", 0.6f, 0.8f, 0.0f, 0.23f);
    ci(&snd_hlong, "Hr_swishDLong", 0.5f, 3.0f, 0.2f, 0.8f);
    ci(&snd_hmid, "Hr_swishFMid", 0.5f, 3.0f, 0.2f, 0.8f);
    ci(&snd_hshort, "Hr_swishCShort", 0.5f, 3.0f, 0.3f, 0.9f);
    ci(&snd_hlandgrind, "Hr_H_land", 0.8f, 1.1f, 0.5f, 0.7f);
    ci(&snd_superjump, "Hr_superSneakers_jump", 0.8f, 1.1f, 0.7f, 1.0f);
    ci(&snd_paint, "Hr_intro_canSpray", 0.8f, 1.4f, 0.05f, 0.12f);
    ci(&snd_painthigh, "Hr_intro_canSpray", 1.1f, 1.4f, 0.05f, 0.12f);
    ci(&snd_shake, "Hr_intro_canShake", 0.8f, 1.1f, 0.1f, 0.2f);
    ci(&snd_deathtrain, "Hr_death_hitCam", 0.8f, 1.1f, 0.8f, 0.9f);
    ci(&snd_bodyfall, "Hr_death_bodyfall", 0.8f, 1.1f, 0.8f, 0.9f);
    ci(&snd_catch, "Hr_Guard_catchPlayer", 0.9f, 1.0f, 0.5f, 0.5f);
    ci(&snd_startpan, "Hr_intro_gameStart", 1.0f, 1.0f, 0.25f, 0.25f);
    sfx_guardloop = sfx_find("Hr_Guard_proximityLoop");
    sfx_trainpass = sfx_find("Hr_trainPass");
    sfx_magnetloop = sfx_find("Hr_magnet_mainLOOP");
    sfx_jetloop = sfx_find("Hr_jetPack_mainLOOP");
    sfx_tap = sfx_find("Tap");
}

/* ------------------------------------------------------------ nodos usados */
static int n_character, n_charModel, n_avatar, n_colChar, n_colMagnet, n_colSneakers, n_shadow, n_sprayCan;
static int n_jroot = -1, n_meshSneakers, n_meshHoverboard, n_meshMagnet, n_meshJetpack, n_spine, n_shoulder;
static int n_enemies, n_guard, n_dog, n_camRig, n_mainCam, n_sky, n_intro_env, n_efx, n_jetParticles, n_ssRing;
static V3 enemy_start[2];
static int n_cloud[2] = {-1, -1}, n_cloud_tgt[2] = {-1, -1}, cloud_mat[2] = {-1, -1};
static Q4 cloud_rot0[2];

/* ParticleFollow + JetPackCloud: estelas del jetpack pegadas a las boquillas */
static void jet_clouds_update(void)
{
    extern float r_mat_uoff[256];
    static const float soff[2] = {5.0f, 0.0f}, sspd[2] = {5.0f, 4.9f};
    static const float start[2] = {1.15f, 1.0f};
    for (int i = 0; i < 2; i++) {
        int n = n_cloud[i];
        if (n < 0 || !nrt[n].active || n_cloud_tgt[i] < 0) continue;
        nrt[n].lpos = node_pos(n_cloud_tgt[i]);    /* EFX esta en el origen */
        float wob = sinf(soff[i] + game_time * sspd[i]) * 5.5f;
        nrt[n].lrot = qmul(nrt[n_cloud_tgt[i]].lrot, qmul(qeuler(0, wob, 0), cloud_rot0[i]));
        scene_update_world(n);
        if (cloud_mat[i] >= 0 && cloud_mat[i] < 256)
            r_mat_uoff[cloud_mat[i]] = fmodf(start[i] - 2.0f * game_time, 1.0f);
    }
}

/* ------------------------------------------------------------ estado */
static Animator anim_char, anim_guard, anim_dog, anim_cam;
static CharCtrl cc;
static V3 cc_center0; static float cc_height0;
static V3 col_center0; static float col_height0, col_height, col_radius = 3.0f; static V3 col_center;

typedef struct {
    const char *runAnimations[4], *landAnimations[4], *jumpAnimations[5], *hangtimeAnimations[4];
    const char *grindAnimations[3], *grindLandAnimations[3], *hoverJump[11], *hoverHang[11];
    const char *jump, *run, *landing, *hangtime, *roll, *dodgeLeft, *dodgeRight, *hitMid, *hitUpper, *hitLower,
        *hitMoving, *stumble, *stumbleDeath, *stumbleLeftSide, *stumbleRightSide, *stumbleLeftCorner, *stumbleRightCorner;
} Anims;

static Anims an = {
    {"run", "run2", "run3", "run4_long"}, {"landing", "landing", "landing", "landing3"},
    {"jump", "jump", "jump_salto", "jump2", "jump3"}, {"hangtime", "hangtime", "hangtime2", "hangtime3"},
    {"h_Grind1", "h_Grind2", "h_Grind3"}, {"landing_grind1", "landing_grind2", "landing_grind3"},
    {"h_jump", "h_jump2_kickflip", "h_jump3_180", "h_jump4_360flip", "h_jump5_Impossible", "h_jump6_nollie",
     "h_jump7_heelflip", "h_jump8_pop shuvit", "h_jump9_fs360", "h_jump10_heel360", "h_jump11_fs salto"},
    {"h_hangtime", "h_jump2_kickflip", "h_jump3_180", "h_jump4_360flip", "h_jump5_Impossible", "h_jump6_nollie",
     "h_jump7_heelflip", "h_jump8_pop shuvit", "h_jump9_fs360", "h_jump10_heel360", "h_jump11_fs salto"},
    "jump", "run", "landing", "hangtime", "roll", "dodgeLeft", "dodgeRight", "death_bounce", "death_upper",
    "death_lower", "death_movingTrain", "stumble_low", "caught", "stumbleSideLeft", "stumbleSideRight",
    "stumbleCornerLeft", "stumbleCornerRight"};

enum { OB_JUMPHIGH, OB_JUMPTRAIN, OB_ROLL, OB_JUMP, OB_NONE };
enum { RP_GROUND, RP_STATION, RP_TRAIN, RP_MOVINGTRAIN, RP_AIR };

static struct {
    int trackIndex, trackIndexTarget;
    float trackIndexPosition, x, z, verticalSpeed, lastGroundedY;
    int isInsideSubway, trackMovement, trackMovementNext;
    float characterRotation, jumpHeight;
    int stumble; float stumble_end;
    int jumping, falling, inAirJump, fallAnim;
    int ssj; float ss_zstart, ss_zlen, ss_zend, ss_ystart;
    int startedJumpFromGround, trainJump; float trainJumpSampleZ;
    int immune, stopColliding;
    int rolling, roll_started; float roll_end;
    /* tween del cambio de carril */
    int tc; float tc_start, tc_dur, tc_ipos0, tc_x0, tc_x1, tc_dir, tc_rot0; int tc_new;
    /* caida tras el tren en marcha */
    int hbt; float hbt_start; V3 hbt_from, hbt_cam;
    /* volver al punto de control (tutorial) */
    int mtp; float mtp_start, mtp_dur, mtp_x0, mtp_z0, mtp_x1, mtp_z1;
    int lastObstacle, lastObstacleTrack, lastHitTag, lastHitName;
    int runPosition;
    int shadow_on;
} ch;

static int state = GS_MENU;
static float state_time, start_time;
static float currentSpeed, currentLevelSpeed = SPEED_MIN;
static int isDead;
#ifdef AT_NOTUT
static int tutorial_completed = 1;   /* prueba automatica: sin tutorial */
#else
static int tutorial_completed = 0;   /* sin partida guardada: primera vez, con tutorial */
#endif
static int tut_board_ok;             /* TutorialEvent.allowHoverboard: la tabla se puede usar desde ese punto */
static int tut_arrow_on, n_tut_arrow = -1;

/* camara */
static int cam_follow;          /* CharacterCamera.enabled */
static V3 cam_pos, cam_target, cam_shake;
static float shake_t = -1, shake_amp_t; static V3 shake_diff;
static float sm_x, sm_xv, sm_y, sm_yv, sm_y_target, aim, aim_v;
static V3 cam_off, cam_off_v;
static int from_jetpack; float from_jet_t; V3 from_jet_pos, from_jet_tgt;
static int tunnel_on; float tunnel_start, tunnel_len;

/* guardia */
static struct {
    int enabled, showing, close;
    float dist, y, vs, lg_smooth, lg_vel, xs, xv;
    int tw; float tw_start, tw_dur, tw_from, tw_to;
    int hide_pending; float hide_at;
    float jump_at; int jump_pending;
    int catching; float catch_start; float catch_dur; int catch_weight; float catch_weight_start;
    int hit_train; float ht_start;
    float vol;
} gu;

/* modificadores (CharacterModifier) */
enum { MOD_MAGNET, MOD_SNEAKERS, MOD_HOVERBOARD, MOD_DOUBLE, MOD_COUNT };
enum { STOP_DONT, STOP_STOP, STOP_NOEND };
static struct {
    int active, paused, stop, phase;
    float t0, dist_left, cooldown_left;
    int did_stop_cd;
} mods[MOD_COUNT];
static float hb_last_end = -1000;

/* power-ups activos (GameStats._listOfActivePowerups) */
enum { PT_HOVERBOARD, PT_JETPACK, PT_SNEAKERS, PT_MAGNET, PT_DOUBLE, PT_HEADSTART, PT_COUNT };
static float pw_left[PT_COUNT];
static int pw_on[PT_COUNT];

/* jetpack */
static struct {
    int phase, headStart;
    float speed, flyDur, startTime, landingZ, extFly, cameraZ, startY;
    V3 cam_off0, cam_pos0, cam_tgt0; float aim0;
    float headStartDistance;
} jp;

/* puntuacion (GameStats) */
static float meters_last;
static int score, coins_run, meters;
static int double_score;
static int coin_stairway, fly_way;

/* entradas pendientes (swipes) */
static int in_left, in_right, in_up, in_down, in_board;
static float last_tap = -100;

/* ------------------------------------------------------------ utilidades */
static float Speed(float t)
{
    if (t < SPEED_RAMP) return t * (SPEED_MAX - SPEED_MIN) / SPEED_RAMP + SPEED_MIN;
    return SPEED_MAX;
}
static float normalized_speed(void) { return currentSpeed / SPEED_MIN; }
static float jump_vs(float h) { return sqrtf(2.0f * h * GRAVITY); }
static float jump_length(float speed, float h) { return speed * 2.0f * jump_vs(h) / GRAVITY; }
static int score_multiplier(void) { return missions_multiplier() * (double_score ? 2 : 1); }
static int mod_active(int m) { return mods[m].active; }
static float bell(float x) { return smoothstepf(0, 1, 1.0f - fabsf(x - 0.5f) / 0.5f); }

static void set_node_active(int n, int on) { if (n >= 0) nrt[n].active = on; }

static void play_ci(const ClipInfo *c) { DBG_PHASE(main, "play_ci (sonido)"); snd_clip(c); }

/* ------------------------------------------------------------ animaciones del personaje */
static int rnd_i(int n) { return rnd_range(0, n); }
static void SetRandomRun(void) { int i = rnd_i(4); an.run = an.runAnimations[i]; an.landing = an.landAnimations[i]; }
static void SetRandomJump(void) { an.jump = an.jumpAnimations[rnd_i(5)]; an.hangtime = an.hangtimeAnimations[rnd_i(4)]; }
static void SetRandomGrind(void) { int i = rnd_i(3); an.run = an.grindAnimations[i]; an.landing = an.grindLandAnimations[i]; }
static void SetRandomHover(void) { an.run = "h_run"; an.landing = "h_landing"; }
static void SetRandomHoverJump(void) { int i = rnd_i(11); an.jump = an.hoverJump[i]; an.hangtime = an.hoverHang[i]; }

static void ChangeAnimations(void)
{
    if (isDead) return;
    if (mods[MOD_HOVERBOARD].active && mod_active(MOD_HOVERBOARD) && 1) {
        an.run = "h_run"; an.roll = "h_roll"; an.dodgeLeft = "h_left"; an.dodgeRight = "h_right";
    } else {
        if (mods[MOD_SNEAKERS].active) { an.run = "superRun"; an.landing = "landing"; }
        else SetRandomRun();
        an.roll = "roll"; an.dodgeLeft = "dodgeLeft"; an.dodgeRight = "dodgeRight";
        SetRandomJump();
    }
    if (cc.grounded) anim_crossfade(&anim_char, an.run, 0.3f);
}

static void SetRunAnim(void)
{
    if (mods[MOD_HOVERBOARD].active) {
        if (cc.pos.y > 20.0f) SetRandomGrind();
        else SetRandomHover();
    } else if (!mods[MOD_SNEAKERS].active) SetRandomRun();
}

/* ------------------------------------------------------------ eventos de animacion (RunnerAnimPlayer) */
typedef struct { int frame; const char *clip; ClipInfo *snd; int kind; } KFA;   /* kind 1: salto, 2: pintura */
static KFA kfa[] = {
    {1, "run", &snd_runL}, {6, "run", &snd_runR}, {1, "run2", &snd_runL}, {6, "run2", &snd_runR},
    {1, "run3", &snd_runL}, {6, "run3", &snd_runR},
    {1, "run4_long", &snd_runL}, {6, "run4_long", &snd_runR}, {10, "run4_long", &snd_runL}, {15, "run4_long", &snd_runR},
    {20, "run4_long", &snd_runL}, {25, "run4_long", &snd_runR}, {30, "run4_long", &snd_runL}, {35, "run4_long", &snd_runR},
    {40, "run4_long", &snd_runL}, {45, "run4_long", &snd_runR}, {50, "run4_long", &snd_runL}, {55, "run4_long", &snd_runR},
    {60, "run4_long", &snd_runL}, {65, "run4_long", &snd_runR}, {70, "run4_long", &snd_runL}, {75, "run4_long", &snd_runR},
    {80, "run4_long", &snd_runL}, {85, "run4_long", &snd_runR}, {90, "run4_long", &snd_runL}, {95, "run4_long", &snd_runR},
    {100, "run4_long", &snd_runL}, {105, "run4_long", &snd_runR}, {110, "run4_long", &snd_runL},
    {1, "superRun", &snd_superR}, {10, "superRun", &snd_superL},
    {0, "dodgeLeft", &snd_groundL}, {0, "dodgeRight", &snd_groundR}, {8, "dodgeLeft", &snd_groundL}, {8, "dodgeRight", &snd_groundR},
    {1, "landing", &snd_landing},
    {0, "jump", &snd_jump, 1}, {0, "jump_salto", &snd_jump, 1}, {0, "jump2", &snd_jump, 1}, {0, "jump3", &snd_jump, 1},
    {0, "jump", &snd_groundL}, {0, "jump_salto", &snd_groundR}, {0, "jump2", &snd_groundL}, {0, "jump3", &snd_groundL},
    {1, "roll", &snd_roll}, {1, "slide_roll2", &snd_roll}, {0, "roll", &snd_groundR}, {0, "slide_roll2", &snd_groundR},
    {0, "h_landing", &snd_hkick}, {0, "h_roll", &snd_hroll}, {4, "h_roll", &snd_hminikick},
    {1, "h_jump", &snd_hkick}, {2, "h_jump", &snd_hlong},
    {1, "h_jump2_kickflip", &snd_hkick}, {4, "h_jump2_kickflip", &snd_hlong}, {21, "h_jump2_kickflip", &snd_hshort},
    {1, "h_jump3_180", &snd_hkick}, {2, "h_jump3_180", &snd_hshort}, {8, "h_jump3_180", &snd_hmid},
    {1, "h_jump4_360flip", &snd_hkick}, {2, "h_jump4_360flip", &snd_hshort}, {8, "h_jump4_360flip", &snd_hshort},
    {13, "h_jump4_360flip", &snd_hminikick}, {19, "h_jump4_360flip", &snd_hmid},
    {1, "h_jump5_Impossible", &snd_hkick}, {2, "h_jump5_Impossible", &snd_hmid}, {8, "h_jump5_Impossible", &snd_hshort},
    {11, "h_jump5_Impossible", &snd_hminikick}, {22, "h_jump5_Impossible", &snd_hmid},
    {1, "h_jump6_nollie", &snd_hkick}, {2, "h_jump6_nollie", &snd_hlong}, {2, "h_jump6_nollie", &snd_hshort},
    {21, "h_jump6_nollie", &snd_hmid},
    {3, "h_jump7_heelflip", &snd_hkick}, {2, "h_jump7_heelflip", &snd_hmid}, {8, "h_jump7_heelflip", &snd_hshort},
    {13, "h_jump7_heelflip", &snd_hminikick}, {18, "h_jump7_heelflip", &snd_hmid},
    {1, "h_jump8_pop shuvit", &snd_hkick}, {2, "h_jump8_pop shuvit", &snd_hshort}, {4, "h_jump8_pop shuvit", &snd_hminikick},
    {10, "h_jump8_pop shuvit", &snd_hmid},
    {2, "h_jump9_fs360", &snd_hkick}, {3, "h_jump9_fs360", &snd_hshort}, {10, "h_jump9_fs360", &snd_hshort},
    {20, "h_jump9_fs360", &snd_hmid},
    {0, "h_jump10_heel360", &snd_hminikick}, {4, "h_jump10_heel360", &snd_hkick}, {5, "h_jump10_heel360", &snd_hshort},
    {8, "h_jump10_heel360", &snd_hmid}, {11, "h_jump10_heel360", &snd_hshort}, {14, "h_jump10_heel360", &snd_hminikick},
    {20, "h_jump10_heel360", &snd_hmid},
    {1, "h_jump11_fs salto", &snd_hkick}, {2, "h_jump11_fs salto", &snd_hlong}, {8, "h_jump11_fs salto", &snd_hshort},
    {1, "landing_grind1", &snd_hlandgrind}, {1, "landing_grind2", &snd_hlandgrind}, {1, "landing_grind3", &snd_hlandgrind},
    {1, "idlePaint", &snd_paint, 2}, {28, "idlePaint", &snd_shake, 2}, {100, "idlePaint", &snd_shake, 2},
    {150, "idlePaint", &snd_painthigh, 2}, {200, "idlePaint", &snd_paint, 2}, {237, "idlePaint", &snd_paint, 2},
    {260, "idlePaint", &snd_shake, 2}, {310, "idlePaint", &snd_paint, 2}, {350, "idlePaint", &snd_paint, 2},
    {13, "death_movingTrain", &snd_deathtrain}, {8, "death_lower", &snd_bodyfall}, {11, "death_bounce", &snd_bodyfall},
    {2, "death_upper", &snd_bodyfall},
};
#define NKFA ((int)(sizeof(kfa) / sizeof(kfa[0])))
static int kfa_ev_index[NKFA];
static int paint_sound = 1;

static void char_anim_event(Animator *a, int state, float t)
{
    for (int i = 0; i < NKFA; i++) {
        if (kfa_ev_index[i] < 0) continue;
        if (a->ev_clip[kfa_ev_index[i]] != state || a->ev_times[kfa_ev_index[i]] != t) continue;
        if (kfa[i].kind == 1) play_ci(mods[MOD_SNEAKERS].active ? &snd_superjump : kfa[i].snd);
        else if (kfa[i].kind == 2) { if (paint_sound) play_ci(kfa[i].snd); }
        else play_ci(kfa[i].snd);
    }
}

static void guard_anim_event(Animator *a, int state, float t) { (void)a; (void)state; (void)t; play_ci(&snd_catch); }
static void cam_anim_event(Animator *a, int state, float t) { (void)a; (void)state; (void)t; play_ci(&snd_startpan); }

/* ------------------------------------------------------------ guardia (FollowingGuard) */
static void show_rec(int n, int vis)
{
    if (node_comp(n, C_SKIN, NULL) || node_comp(n, C_MESH, NULL)) nrt[n].active = vis;
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling) show_rec(c, vis);
}

static void guard_show(int vis)
{
    gu.showing = vis;
    if (n_enemies >= 0) show_rec(n_enemies, vis);
}

static void guard_restart(int close)
{
    gu.tw = 0; gu.hide_pending = 0; gu.jump_pending = 0; gu.catching = 0; gu.hit_train = 0;
    gu.close = close;
    gu.dist = close ? GUARD_DIST_MIN : GUARD_DIST_MAX;
}

static void guard_tween(float to, float dur)
{
    gu.tw = 1; gu.tw_start = game_time; gu.tw_dur = dur; gu.tw_from = gu.dist; gu.tw_to = to;
}

static void guard_catch_up(void)
{
    if (gu.close) return;
    guard_show(1);
    gu.hide_pending = 0;
    anim_play(&anim_guard, "Guard_grap after");
    anim_play_queued(&anim_guard, "Guard_Run");
    if (h_guardloop < 0 || !snd_playing(h_guardloop)) h_guardloop = snd_play(sfx_guardloop, 0, rnd_rangef(0.9f, 1.05f), 1);
    gu.vol = 0;
    guard_tween(GUARD_DIST_MIN, GUARD_CATCHUP);
    gu.close = 1;
}

static void guard_reset_catch_up(void)
{
    if (!gu.close) return;
    gu.close = 0;
    guard_tween(GUARD_DIST_MAX, GUARD_RESET);
    gu.hide_pending = 1;
    gu.hide_at = game_time + GUARD_RESET * 2.0f;
}

static void guard_mute(void) { snd_stop(h_guardloop); h_guardloop = -1; }

static void guard_on_enable(void)
{
    gu.lg_smooth = ch.lastGroundedY; gu.lg_vel = 0;
    gu.y = ch.lastGroundedY;
    gu.xs = cc.pos.x; gu.xv = 0;
    gu.dist = GUARD_DIST_MIN;
    gu.close = 1;
    gu.vs = 0;
}

static void guard_play_intro(void)
{
    nrt[n_enemies].lpos = v3(0, 0, -10);
    if (n_guard >= 0) { nrt[n_guard].lpos = enemy_start[0]; nrt[n_guard].lrot = qident(); }
    if (n_dog >= 0) { nrt[n_dog].lpos = enemy_start[1]; nrt[n_dog].lrot = qident(); }
    scene_update_world(n_enemies);
    anim_play(&anim_guard, "playIntro");
    anim_play(&anim_dog, "playIntro");
    anim_crossfade_queued(&anim_guard, "Guard_Run", 0.2f);
    anim_crossfade_queued(&anim_dog, "Dog_Fast Run", 0.2f);
}

static void guard_jump(void)
{
    if (gu.dist <= GUARD_DIST_MIN) missions_did(M_GUARDJUMP, 1);
    gu.jump_pending = 1;
    gu.jump_at = game_time + gu.dist / (currentSpeed > 1 ? currentSpeed : 1);
}

static void guard_catch_player(float pos)
{
    guard_mute();
    gu.tw = 0; gu.hide_pending = 0; gu.jump_pending = 0;
    anim_stop(&anim_char, "caught");
    anim_stop(&anim_char, "caught2");
    if (pos < 20.0f) {
        anim_crossfade(&anim_guard, "catch2", 0.2f);
        anim_crossfade(&anim_dog, "catch2", 0.2f);
        an.stumbleDeath = "caught2";
    } else {
        anim_crossfade(&anim_guard, "catch", 0.2f);
        anim_crossfade(&anim_dog, "catch", 0.2f);
        an.stumbleDeath = "caught";
    }
    AnimState *s = anim_get(&anim_char, an.stumbleDeath);
    if (s) { s->weight = 0; s->target_weight = 0; s->enabled = 1; s->time = 0; }
    gu.catching = 1; gu.catch_start = game_time; gu.catch_dur = 0.68f;
    gu.catch_weight = 0;
}

static void guard_hit_by_train(void)
{
    guard_mute();
    gu.hit_train = 1; gu.ht_start = game_time;
}

static void guard_late_update(float dt)
{
    /* tweens y corrutinas */
    if (gu.tw) {
        float t = (game_time - gu.tw_start) / gu.tw_dur;
        if (t >= 1) { t = 1; gu.tw = 0; }
        gu.dist = smoothstepf(gu.tw_from, gu.tw_to, t);
        if (h_guardloop >= 0) {
            float v = gu.close ? smoothstepf(0, GUARD_VOLUME, t) : smoothstepf(GUARD_VOLUME, 0, t * 0.5f);
            snd_set(h_guardloop, v, 1.0f);
        }
    }
    if (gu.hide_pending && game_time >= gu.hide_at) {
        gu.hide_pending = 0;
        guard_mute();
        if (!isDead) guard_show(0);
    }
    if (gu.jump_pending && game_time >= gu.jump_at) {
        gu.jump_pending = 0;
        anim_play(&anim_guard, "Guard_jump");
        anim_crossfade_queued(&anim_guard, "Guard_Run", 0.2f);
        anim_play(&anim_dog, "Dog_jump");
        anim_crossfade_queued(&anim_dog, "Dog_Fast Run", 0.2f);
        gu.vs = jump_vs(ch.jumpHeight) * 0.6f;
    }
    if (gu.catching) {
        float t = (game_time - gu.catch_start) / gu.catch_dur;
        if (t > 1) t = 1;
        int ens[2] = {n_guard, n_dog};
        for (int i = 0; i < 2; i++) {
            if (ens[i] < 0) continue;
            V3 wp = node_pos(ens[i]);
            V3 np = v3lerp(wp, cc.pos, t);
            /* pasar la posicion de mundo a local del padre (Enemies) */
            M34 inv; m34_inverse(&inv, &nrt[n_enemies].world);
            nrt[ens[i]].lpos = m34_point(&inv, np);
        }
        if (game_time - gu.catch_start >= gu.catch_dur) {
            float wt = (game_time - gu.catch_start - gu.catch_dur) / 0.2f;
            AnimState *s = anim_get(&anim_char, an.stumbleDeath);
            if (s) { s->enabled = 1; s->weight = clamp01(wt); s->target_weight = s->weight; }
            if (wt >= 1) gu.catching = 0;
        }
    }
    if (gu.hit_train) {
        float e = game_time - gu.ht_start;
        int ens[2] = {n_guard, n_dog};
        if (e < 0.2f) {
            for (int i = 0; i < 2; i++) {
                if (ens[i] < 0) continue;
                V3 np = v3lerp(node_pos(ens[i]), cc.pos, e / 0.2f);
                M34 inv; m34_inverse(&inv, &nrt[n_enemies].world);
                nrt[ens[i]].lpos = m34_point(&inv, np);
            }
        } else if (gu.hit_train == 1) {
            anim_play(&anim_dog, "Dog_death_movingTrain");
            gu.hit_train = 2;
        }
        if (gu.hit_train == 2 && e >= 0.6f) {
            ch.hbt = 2; ch.hbt_start = game_time; ch.hbt_from = cc.pos;
            gu.hit_train = 3;
        }
        if (gu.hit_train == 3 && e >= 0.8f) { anim_play(&anim_guard, "Guard_death_movingTrain"); gu.hit_train = 4; }
    }
    if (!gu.enabled) { scene_update_world(n_enemies); return; }
    gu.xs = smoothdamp(gu.xs, cc.pos.x, &gu.xv, GUARD_X_SMOOTH, dt);
    gu.lg_smooth = smoothdamp(gu.lg_smooth, ch.lastGroundedY, &gu.lg_vel, GUARD_LG_SMOOTH, dt);
    if (gu.y > gu.lg_smooth) gu.vs -= GRAVITY * dt;
    gu.y += gu.vs * dt;
    if (gu.y < gu.lg_smooth) gu.y = gu.lg_smooth;
    nrt[n_enemies].lpos = v3(gu.xs, gu.y, cc.pos.z - gu.dist);
    scene_update_world(n_enemies);
}

/* ------------------------------------------------------------ power-ups activos */
static float trigger_powerup(int type, int pw_type)
{
    pw_on[type] = 1;
    pw_left[type] = powerup_duration(pw_type);
    if (type == PT_HOVERBOARD) pw_left[type] = 30.0f;
    if (type == PT_HEADSTART) pw_left[type] = 0;
    /* AddScoreForPickup */
    if (type == PT_JETPACK || type == PT_SNEAKERS || type == PT_MAGNET || type == PT_DOUBLE) {
        int num = score_multiplier() * 50;
        score += num;
        missions_did(M_SCORE, num);
        if (coins_run <= 0) missions_did(M_NOCOINSBEFORESCORE, num);
    }
    if (type != PT_HEADSTART) missions_did(M_POWERUPS, 1);   /* GameStats.usePowerups */
    return pw_left[type];
}

static void update_powerup_times(float dt)
{
    for (int i = 0; i < PT_COUNT; i++) {
        if (!pw_on[i]) continue;
        if (state == GS_JETPACK && (i == PT_HOVERBOARD || i == PT_SNEAKERS)) continue;
        pw_left[i] -= dt;
        if (pw_left[i] >= 0 || (state == GS_JETPACK && i == PT_JETPACK)) continue;
        if (i == PT_HOVERBOARD && pw_left[i] > -(HB_PARTICLE_DELAY + HB_COOLDOWN)) continue;
        pw_on[i] = 0;
    }
}

static void clear_powerups(void) { for (int i = 0; i < PT_COUNT; i++) pw_on[i] = 0; }

/* ------------------------------------------------------------ Character */
static void char_set_pos(V3 p) { cc.pos = p; nrt[n_character].lpos = p; }

static void stumble_set(int v);

static void character_restart(void)
{
    ch.trackIndex = ch.trackIndexTarget = 1;
    ch.x = track_get_x(1);
    ch.trackIndexPosition = 1;
    ch.z = 0;
    ch.trackMovement = ch.trackMovementNext = 0;
    char_set_pos(v3add(track_pos(ch.x, ch.z), v3(0, 5, 0)));
    cc_move(&cc, v3(0, -5, 0));
    ch.verticalSpeed = 0;
    ch.ssj = 0;
    ch.jumpHeight = JUMP_HEIGHT_NORMAL;
    ch.inAirJump = 0;
    ch.lastGroundedY = 0;
    ch.tc = 0; ch.hbt = 0; ch.mtp = 0;
    ch.characterRotation = 0;
    ch.rolling = 0;
    guard_restart(1);
    stumble_set(1);
    ch.startedJumpFromGround = 0;
}

static void track_change_update(float t)
{
    ch.trackIndexPosition = lerpf(ch.tc_ipos0, (float)ch.tc_new, t);
    ch.x = lerpf(ch.tc_x0, ch.tc_x1, t);
    ch.characterRotation = bell(t) * ch.tc_dir * CHARACTER_ANGLE + lerpf(ch.tc_rot0, 0, t);
    nrt[n_avatar].lrot = qeuler(0, ch.characterRotation, 0);
}

static void notify_stumble(const ClipInfo *snd, const char *name);

static void change_track_start(int move, float duration)
{
    ch.trackMovement = move;
    ch.trackMovementNext = 0;
    int newIdx = ch.trackIndexTarget + move;
    float dist = fabsf((float)newIdx - ch.trackIndexPosition);
    float dir = signf((float)(newIdx - ch.trackIndexTarget));
    if (cc.grounded) {
        const char *da = dir < 0 ? an.dodgeLeft : an.dodgeRight;
        AnimState *s;
        if ((s = anim_get(&anim_char, "dodgeRight"))) s->speed = normalized_speed();
        if ((s = anim_get(&anim_char, "dodgeLeft"))) s->speed = normalized_speed();
        anim_crossfade(&anim_char, da, 0.02f);
    }
    if (!ch.jumping) anim_crossfade_queued(&anim_char, an.run, mods[MOD_HOVERBOARD].active ? 0.4f : 0.02f);
    if (newIdx < 0 || newIdx >= 3) {
        notify_stumble(&snd_stumbleSide, "side");
        if (!mods[MOD_HOVERBOARD].active && state != GS_JETPACK)
            anim_crossfade(&anim_char, dir < 0 ? "stumbleOffLeft" : "stumbleOffRight", 0.2f);
        if (!ch.jumping) anim_crossfade_queued(&anim_char, an.run, mods[MOD_HOVERBOARD].active ? 0.4f : 0.02f);
        /* la corrutina termina aqui: el movimiento sigue marcado */
        ch.tc = 0;
        return;
    }
    if (move < 0) play_ci(mods[MOD_HOVERBOARD].active ? &snd_hL : &snd_dodgeL);
    else play_ci(mods[MOD_HOVERBOARD].active ? &snd_hR : &snd_dodgeR);
    ch.trackIndexTarget = newIdx;
    ch.tc = 1;
    ch.tc_start = game_time;
    ch.tc_dur = dist * duration;
    ch.tc_ipos0 = ch.trackIndexPosition;
    ch.tc_x0 = ch.x;
    ch.tc_x1 = track_get_x(newIdx);
    ch.tc_dir = dir;
    ch.tc_rot0 = ch.characterRotation;
    ch.tc_new = newIdx;
    track_change_update(0);
}

static float tc_duration;

static void stop_all_char_coroutines(void) { ch.tc = 0; ch.mtp = 0; ch.hbt = 0; }

static void force_change_track(int move, float duration)
{
    ch.trackMovement = move;
    ch.trackMovementNext = 0;
    stop_all_char_coroutines();
    tc_duration = duration;
    change_track_start(move, duration);
}

static void change_track(int move, float duration)
{
    if (ch.trackMovement != move) force_change_track(move, duration);
    else ch.trackMovementNext = move;
}

static void track_change_tick(void)
{
    if (!ch.tc) return;
    float t = ch.tc_dur > 0 ? (game_time - ch.tc_start) / ch.tc_dur : 1;
    if (t < 1) { track_change_update(t); return; }
    track_change_update(1);
    ch.tc = 0;
    ch.trackIndex = ch.tc_new;
    ch.trackMovement = 0;
    if (ch.trackMovementNext != 0) {
        int m = ch.trackMovementNext;
        ch.trackMovement = m;
        ch.trackMovementNext = 0;
        change_track_start(m, tc_duration);
    }
}

static int is_running_on_ground(void) { return ch.runPosition == RP_GROUND; }

static void character_jump(void)
{
    ch.fallAnim = 1;
    if (mods[MOD_HOVERBOARD].active) SetRandomHoverJump();
    else SetRandomJump();
    int flag = ch.falling && !ch.jumping && ch.verticalSpeed < 0 && ch.verticalSpeed > -30.0f;
    if (cc.grounded || flag) {
        ch.jumping = 1;
        ch.falling = 0;
        ch.shadow_on = 0;
        anim_crossfade(&anim_char, an.jump, 0.05f);
        if (mods[MOD_SNEAKERS].active) {
            ch.ssj = 1;
            ch.ss_zstart = cc.pos.z;
            ch.ss_zlen = jump_length(currentSpeed, ch.jumpHeight) * SS_APEX_RATIO;
            ch.ss_zend = ch.ss_zstart + ch.ss_zlen;
            ch.ss_ystart = cc.pos.y;
            ch.verticalSpeed = 0;
        } else ch.verticalSpeed = jump_vs(ch.jumpHeight);
        if (is_running_on_ground()) {
            ch.startedJumpFromGround = 1;
            ch.trainJump = 0;
            ch.trainJumpSampleZ = ch.z + 10.0f;
        }
        if (gu.enabled) guard_jump();
        missions_did(M_JUMP, 1);
    } else if (ch.verticalSpeed < 0) ch.inAirJump = 1;
}

static void set_char_capsules(int rolling)
{
    if (rolling) {
        cc.height = 4.0f; cc.center = v3(0, 2, cc_center0.z);
        col_height = 4.0f; col_center = v3(0, 4, col_center0.z);
    } else {
        cc.height = cc_height0; cc.center = cc_center0;
        col_height = col_height0; col_center = col_center0;
    }
}

static void character_roll(void)
{
    if (ch.rolling) return;
    ch.ssj = 0;
    ch.rolling = 1;
    ch.roll_started = 0;
    /* (diagnostico del cuelgue al rodar en la consola real: se escribe en la tarjeta al momento) */
    dlog("RUEDA carril %d altura %d en el aire %d a %d m", ch.trackIndex, (int)cc.pos.y, !cc.grounded, ginfo.meters);
    dlog_flush();
    dbg_trace_start("rodada", 2);
    missions_did(M_ROLL, 1);
    missions_did(ch.trackIndex == 0 ? M_ROLLLEFT : ch.trackIndex == 1 ? M_ROLLCENTER : M_ROLLRIGHT, 1);
}

static void roll_tick(void)
{
    if (!ch.rolling) return;
    if (!ch.roll_started) {
        ch.roll_started = 1;
        DBG_PHASE(main, "roll_tick: anim_crossfade(roll)");
        anim_crossfade(&anim_char, an.roll, 0.1f);
        DBG_PHASE(main, "roll_tick: SetRunAnim");
        SetRunAnim();
        ch.fallAnim = 0;
        DBG_PHASE(main, "roll_tick: anim_crossfade_queued(run)");
        anim_crossfade_queued(&anim_char, an.run, mods[MOD_HOVERBOARD].active ? 0.2f : 0.0f);
        DBG_PHASE(main, "roll_tick: set_char_capsules(1)");
        set_char_capsules(1);
        ch.verticalSpeed = -jump_vs(ch.jumpHeight);
        DBG_PHASE(main, "roll_tick: anim_length");
        ch.roll_end = game_time + anim_length(&anim_char, an.roll);
        DBG_PHASE(main, "roll_tick: inicio hecho");
        return;
    }
    AnimState *rs = anim_get(&anim_char, an.roll);
    if (game_time < ch.roll_end && rs && rs->enabled) return;
    DBG_PHASE(main, "roll_tick: fin de la rodada, cc_move arriba");
    if (cc.enabled) cc_move(&cc, v3(0, 2, 0));
    set_char_capsules(0);
    DBG_PHASE(main, "roll_tick: fin de la rodada, cc_move abajo");
    if (cc.enabled) cc_move(&cc, v3(0, -2, 0));
    ch.rolling = 0;
    ch.fallAnim = 1;
}

static void update_ground_tag(void);

static void on_grounded(void) { update_ground_tag(); }

static void apply_gravity(float dt)
{
    if (ch.verticalSpeed < 0 && cc.grounded) {
        if (ch.startedJumpFromGround && ch.trainJump && is_running_on_ground()) missions_did(M_JUMPTRAIN, 1);
        if (ch.runPosition != RP_AIR) ch.startedJumpFromGround = 0;
        ch.verticalSpeed = 0;
        ch.shadow_on = 1;
        if (ch.jumping || ch.falling) {
            ch.jumping = 0;
            ch.falling = 0;
            on_grounded();
            if (!ch.rolling) {
                SetRunAnim();
                if (ch.fallAnim) {
                    anim_crossfade(&anim_char, an.landing, 0.05f);
                    anim_crossfade_queued(&anim_char, an.run, 0.1f);
                } else {
                    ch.fallAnim = 1;
                    anim_crossfade(&anim_char, an.run, 0.1f);
                }
            }
        }
    } else if (ch.startedJumpFromGround && ch.trainJumpSampleZ < ch.z) {
        float d;
        int hit = phys_raycast_down(cc.pos, 1000.0f, &d, 1);
        if (hit >= 0 && (cols[hit].tag == TAG_HITMOVINGTRAIN || cols[hit].tag == TAG_HITTRAIN)) ch.trainJump = 1;
        ch.trainJumpSampleZ += 10.0f;
    }
    ch.verticalSpeed -= GRAVITY * dt;
    if (!cc.grounded && !ch.falling && ch.verticalSpeed < VERTICAL_FALL_SPEED_LIMIT && !ch.rolling) {
        ch.falling = 1;
        if (ch.fallAnim) {
            anim_crossfade(&anim_char, an.hangtime, 0.2f);
            ch.shadow_on = 0;
        }
    }
}

static void move_with_gravity(float dt)
{
    if (!cc.enabled) return;
    ch.verticalSpeed -= GRAVITY * dt;
    if (ch.verticalSpeed > 0) ch.verticalSpeed = 0;
    cc_move(&cc, v3(0, ch.verticalSpeed * dt, 0));
}

static void move_forward(float dt)
{
    V3 position = cc.pos;
    float nz = ch.z + currentSpeed * dt;
    V3 vert = v3(0, ch.verticalSpeed * dt, 0);
    V3 p2 = track_pos(ch.x, nz);
    V3 v2 = v3(position.x, 0, position.z);
    if (ch.ssj) {
        if (ch.z < ch.ss_zend) {
            float y = curve_eval(superSneakersJumpCurve, 3, (nz - ch.ss_zstart) / ch.ss_zlen) * JUMP_HEIGHT_SUPER + ch.ss_ystart;
            vert = v3(0, y - position.y, 0);
        } else {
            ch.ssj = 0;
            ch.verticalSpeed = 0;
            vert = v3(0, 0, 0);
        }
    }
    V3 v3_ = v3sub(p2, v2);
    if (cc.enabled) cc_move(&cc, v3add(vert, v3_));
    else cc.pos = v3add(cc.pos, v3_);
    ch.z = cc.pos.z;
    if (cc.grounded) ch.lastGroundedY = position.y;
}

static float char_track_x_world(void) { return track_pos(track_get_x(ch.trackIndex), 0).x; }

/* ---- colisiones del personaje (OnCharacterColliderEnter / Exit) ---- */
static void char_bounds(V3 *mn, V3 *mx)
{
    V3 c = v3add(cc.pos, col_center);
    float hh = col_height * 0.5f;
    if (hh < col_radius) hh = col_radius;
    *mn = v3(c.x - col_radius, c.y - hh, c.z - col_radius);
    *mx = v3(c.x + col_radius, c.y + hh, c.z + col_radius);
}

static int obstacle_type(int tag)
{
    switch (tag) {
    case TAG_JUMPTRAIN: return OB_JUMPTRAIN;
    case TAG_ROLLBARRIER: return OB_ROLL;
    case TAG_JUMPBARRIER: return OB_JUMP;
    case TAG_JUMPHIGHBARRIER: return OB_JUMPHIGH;
    }
    return OB_NONE;
}

static void game_die(void);
static void notify_critical_hit(void);
static void hit_by_train_sequence(void);
static void notify_pickup(int node);

static void stumble_set(int v)
{
    if (v) {
        guard_catch_up();
        ch.stumble_end = game_time + STUMBLE_DECAY_TIME;
    } else guard_reset_catch_up();
    ch.stumble = v;
}

static void stumble_death_check(void);

static void notify_stumble(const ClipInfo *snd, const char *name)
{
    (void)name;
    if (state == GS_JETPACK) return;
    play_ci(snd);
    if (track_running_tutorial) return;
    dlog("TROPIEZO contra %s a %d m, carril %d%s", name, ginfo.meters, ch.trackIndex,
         ch.stumble ? " (ya iba tropezado: el inspector le atrapa)" : "");
    if (!strcmp(name, "lightSignal")) missions_did(M_BUMPLIGHTSIGNAL, 1);
    else if (!strcmp(name, "bush") || !strcmp(name, "powerbox")) missions_did(M_BUMPBUSH, 1);
    else if (!strncmp(name, "blocker_", 8)) missions_did(M_BUMPBARRIER, 1);
    else if (strcmp(name, "side") && strcmp(name, "collider")) missions_did(M_BUMPTRAIN, 1);
    stumble_death_check();
    stumble_set(1);
}

static const char *node_name(int n) { return gd.strings[gd.nodes[n].name]; }

static void on_char_enter(const Col *c)
{
    int n = c->node;
    if (c->tag == TAG_SUBWAY) { ch.isInsideSubway = 1; return; }
    if (ch.stopColliding || c->layer == 16) return;
    /* Pickup en el collider o sus hijos */
    for (int k = n, depth = 0; k >= 0 && depth < 1; depth++) {
        /* GetComponentInChildren<Pickup> sobre objetos activos */
        int stack[64], sp = 0;
        stack[sp++] = n;
        while (sp > 0) {
            int q = stack[--sp];
            if (!nrt[q].active) continue;
            if (node_comp(q, C_PICKUP, NULL)) { notify_pickup(q); return; }
            for (int cc_ = gd.nodes[q].first_child; cc_ >= 0 && sp < 64; cc_ = gd.nodes[cc_].next_sibling) stack[sp++] = cc_;
        }
        k = -1;
    }
    if (c->layer == 0) {
        if (c->trigger && cc.grounded) on_grounded();
        if (c->trigger) {
            int ot = obstacle_type(c->tag);
            if (ot != OB_NONE) { ch.lastObstacle = ot; ch.lastObstacleTrack = ch.trackIndex; }
        }
        /* TunnelTrigger */
        if (node_comp(n, C_TUNNEL, NULL)) {
            tunnel_on = 1; tunnel_start = ch.z; tunnel_len = c->bmax.z - c->bmin.z;
        }
        return;
    }
    if (c->trigger) {
        anim_crossfade(&anim_char, an.stumble, 0.05f);
        anim_crossfade_queued(&anim_char, an.run, 0.5f);
        notify_stumble(!strcmp(node_name(n), "bush") ? &snd_stumbleBush : &snd_stumble, node_name(n));
        return;
    }
    ch.lastHitTag = c->tag;
    V3 bmn, bmx;
    char_bounds(&bmn, &bmx);
    /* ImpactX */
    float lo = fmaxf(bmn.x, c->bmin.x), hi = fminf(bmx.x, c->bmax.x);
    float mid = (lo + hi) * 0.5f, ix = mid - c->bmin.x, sx = c->bmax.x - c->bmin.x;
    int impactX = ix > sx - COLLIDER_TRACK_WIDTH * 0.33f ? 2 : (ix < COLLIDER_TRACK_WIDTH * 0.33f ? 0 : 1);
    /* ImpactY */
    lo = fmaxf(bmn.y, c->bmin.y); hi = fminf(bmx.y, c->bmax.y);
    float ry = ((lo + hi) * 0.5f - bmn.y) / (bmx.y - bmn.y);
    int impactY = ry < 0.33f ? 2 : (ry < 0.66f ? 1 : 0);   /* 0 arriba, 1 medio, 2 abajo */
    /* ImpactZ */
    float pz = cc.pos.z, szz = c->bmax.z - c->bmin.z;
    int impactZ;
    if (pz > c->bmax.z - (szz > 30.0f ? STUMBLE_CORNER_TOLERANCE : szz * 0.5f)) impactZ = 2;
    else if (pz < c->bmin.z + STUMBLE_CORNER_TOLERANCE) impactZ = 0;
    else impactZ = 1;
    int num = (c->bmin.x + c->bmax.x) * 0.5f > cc.pos.x ? 1 : -1;
    int flag = ch.trackMovement == num;
    int flag2 = (bmn.z + bmx.z) * 0.5f < c->bmin.z;
    int flag3 = impactZ == 0 && !flag2 && flag;
    if (impactZ == 1 || flag3) {
        if (ch.trackMovement != 0) change_track(-ch.trackMovement, track_running_tutorial ? 0.2f : 0.5f);
        if (impactX == 0) {
            anim_play(&anim_char, an.stumbleLeftSide);
            anim_play_queued(&anim_char, an.run);
            notify_stumble(&snd_stumble, node_name(n));
        } else if (impactX == 2) {
            anim_play(&anim_char, an.stumbleRightSide);
            anim_play_queued(&anim_char, an.run);
            notify_stumble(&snd_stumble, node_name(n));
        }
        return;
    }
    if (impactX == 1) {
        int crit = 1;
        if (!ch.immune) {
            if (impactY == 2) {
                anim_crossfade(&anim_char, an.stumble, 0.05f);
                anim_crossfade_queued(&anim_char, an.run, 0.5f);
                crit = 0;
                ch.verticalSpeed = jump_vs(8.0f);
                notify_stumble(&snd_stumble, node_name(n));
            } else if (c->tag == TAG_HITMOVINGTRAIN) hit_by_train_sequence();
            else if (impactY == 1) anim_crossfade(&anim_char, an.hitMid, 0.07f);
            else anim_crossfade(&anim_char, an.hitUpper, 0.07f);
        }
        if (crit) notify_critical_hit();
        return;
    }
    if (impactZ == 0 && flag) {
        if (c->tag == TAG_HITMOVINGTRAIN) { hit_by_train_sequence(); notify_critical_hit(); }
        else if (c->layer == 13) {
            anim_crossfade(&anim_char, an.stumble, 0.05f);
            anim_crossfade_queued(&anim_char, an.run, 0.5f);
        } else force_change_track(-ch.trackMovement, 0.5f);
    } else if (c->layer == 13) force_change_track(-ch.trackMovement, 0.5f);
    if (impactX == 0) { anim_play(&anim_char, an.stumbleLeftCorner); anim_play_queued(&anim_char, an.run); }
    else if (impactX == 2) { anim_play(&anim_char, an.stumbleRightCorner); anim_play_queued(&anim_char, an.run); }
    notify_stumble(&snd_stumble, node_name(n));
}

static void tutorial_event(const uint32_t *t);

static void on_char_exit(const Col *c)
{
    const uint32_t *te = node_comp(c->node, C_TUTORIAL, NULL);
    if (te && !ch.stopColliding) tutorial_event(te);
    if (c->tag == TAG_SUBWAY) { ch.isInsideSubway = 0; return; }
    int ot = obstacle_type(c->tag);
    if (ot != OB_NONE && ot == ch.lastObstacle && ch.lastObstacleTrack == ch.trackIndex) {
        switch (ot) {
        case OB_JUMP: case OB_JUMPHIGH: missions_did(M_JUMPBARRIERS, 1); missions_did(M_DODGEBARRIERS, 1); break;
        case OB_JUMPTRAIN: missions_did(M_JUMPTRAIN, 1); break;
        case OB_ROLL: missions_did(M_ROLLUNDERBARRIERS, 1); missions_did(M_DODGEBARRIERS, 1); break;
        }
    }
}

static void hit_by_train_sequence(void)
{
    if (mods[MOD_HOVERBOARD].active) return;
    anim_play(&anim_char, an.hitMoving);
    ch.hbt = 1;
    ch.hbt_start = game_time;
    ch.hbt_from = cc.pos;
    ch.hbt_cam = cam_pos;
}

/* triggers: estado anterior */
#define MAX_OVER 64
static int over_prev[MAX_OVER], nover_prev;

static void check_triggers(void)
{
    if (!nrt[n_colChar].active) { nover_prev = 0; return; }
    int over_now[MAX_OVER], nover = 0;
    V3 c = v3add(cc.pos, col_center);
    float half = col_height * 0.5f - col_radius;
    if (half < 0) half = 0;
    V3 a = v3sub(c, v3(0, half, 0)), b = v3add(c, v3(0, half, 0));
    for (int i = 0; i < ncols && nover < MAX_OVER; i++) {
        const Col *k = &cols[i];
        if (!phys_layer_hits(k->layer) || k->layer == 11) continue;
        if (capsule_overlaps(a, b, col_radius, k)) over_now[nover++] = k->node;
#ifdef AT_COINTEST
        /* prueba: una moneda suelta a la altura de Jake (en z) cuenta como tocada, este donde este */
        else if (k->type == COL_SPHERE && node_comp(k->node, C_COIN, NULL) && fabsf(k->p0.z - c.z) < 3.0f) over_now[nover++] = k->node;
#endif
    }
    for (int i = 0; i < nover; i++) {
        int found = 0;
        for (int j = 0; j < nover_prev; j++) if (over_prev[j] == over_now[i]) { found = 1; break; }
        if (!found) {
            for (int k = 0; k < ncols; k++) if (cols[k].node == over_now[i]) { on_char_enter(&cols[k]); break; }
        }
    }
    for (int j = 0; j < nover_prev; j++) {
        int found = 0;
        for (int i = 0; i < nover; i++) if (over_prev[j] == over_now[i]) { found = 1; break; }
        if (!found) for (int k = 0; k < ncols; k++) if (cols[k].node == over_prev[j]) { on_char_exit(&cols[k]); break; }
    }
    memcpy(over_prev, over_now, sizeof(int) * nover);
    nover_prev = nover;
}

/* ---- monedas: recogida, iman y zapatillas ---- */
static void pickup_coin_effect(float y)
{
    static const int pent[17] = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28};
    float pitch;
    if (80.0f < y) {
        coin_stairway = 0;
        /* compressCurve */
        static const Key cc_[] = {{0.005888f, -0.001934f, 1.064513f, 1.064513f}, {0.1921872f, 0.1221492f, 0.3349617f, 0.3349617f},
                                  {4.507387f, 1.110226f, 0.01911923f, 0.01911923f}};
        pitch = powf(2.0f, curve_eval(cc_, 3, fly_way / 48.0f));
        fly_way++;
    } else if (y < 0.1f || (8.795f < y && y < 8.805f) || (9.95f < y && y < 10.05f) || (28.95f < y && y < 29.05f) ||
               (34.95f < y && y < 35.05f)) {
        fly_way = 0; coin_stairway = 0;
        pitch = powf(2.0f, pent[0] / 12.0f) * 0.5f;
    } else {
        fly_way = 0;
        if (coin_stairway < 16) coin_stairway++;
        pitch = powf(2.0f, pent[coin_stairway % 17] / 12.0f) * 0.5f;
    }
    snd_play(snd_coin.sfx, rnd_rangef(snd_coin.minVol, snd_coin.maxVol), pitch, 0);
}

static void collect_coin(int i)
{
    coins_run++;
    missions_did(M_EARNCOIN, 1);
    if (coins[i].pulling && mods[MOD_MAGNET].active) missions_did(M_COINSWITHMAGNET, 1);
    pickup_coin_effect(coins[i].pos.y);
    coins[i].visible = 0;
    coins[i].collider = 0;
    coins[i].pulling = 0;
}

/* esfera de la moneda (r=4.5) contra la capsula del personaje (eje a-b) */
static int coin_touch(V3 sp, V3 a, V3 b)
{
    V3 ab = v3sub(b, a);
    /* al rodar la capsula es un punto (l2 = 0): fmaxf evita la division por cero que -ffast-math adelanta */
    float l2 = v3dot(ab, ab), t = clamp01(v3dot(v3sub(sp, a), ab) / fmaxf(l2, 1e-12f));
    return v3len(v3sub(sp, v3add(a, v3scale(ab, t)))) < 4.5f + col_radius;
}

static void coins_tick(float dt)
{
    (void)dt;
    V3 c = v3add(cc.pos, col_center);
    float half = col_height * 0.5f - col_radius;
    if (half < 0) half = 0;
    V3 a = v3sub(c, v3(0, half, 0)), b = v3add(c, v3(0, half, 0));
    int magnet = mods[MOD_MAGNET].active && !mods[MOD_MAGNET].paused && state != GS_JETPACK;
    int sneak = mods[MOD_SNEAKERS].active && !mods[MOD_SNEAKERS].paused;
    V3 magc = v3add(cc.pos, v3(0, 5.7f + 5.0f, 10.0f));
    if (magnet && !ch.stopColliding) {
        /* el iman tambien atrae las monedas sueltas de la escena (las de los trenes en marcha): CoinMagnet.CoinHit.
         * Aqui se recogen al entrar en su radio, sin el vuelo hasta Jake */
        for (int i = 0; i < ncols; i++) {
            const Col *k = &cols[i];
            if (k->type != COL_SPHERE || nrt[k->node].taken || !node_comp(k->node, C_COIN, NULL)) continue;
            if (v3len(v3sub(k->p0, magc)) < 60.0f + k->r) notify_pickup(k->node);
        }
    }
    for (int i = 0; i < MAX_COINS; i++) {
        CoinRT *k = &coins[i];
        if (!k->active) continue;
        if (k->pulling) {
            float t = (game_time - k->pull_t) / k->pull_dur;
            V3 dst = v3add(cc.pos, v3(0, 7, 2));   /* posicion del objeto del power-up */
            if (t >= 1) { collect_coin(i); k->pulling = 0; continue; }
            V3 p = v3lerp(k->pull_from, dst, t * t);
            k->pivot = v3sub(p, k->pos);
            continue;
        }
        if (!k->collider || !k->visible) continue;
        if (fabsf(k->pos.z - cc.pos.z) > 80.0f) { k->prel_ok = 0; continue; }
        V3 sp = v3add(k->pos, v3(0, 6, 0));
        /* trigger de la moneda (esfera r=4.5) contra la capsula del personaje. Las monedas de los trenes en
         * marcha se acercan al doble de velocidad (~13 unidades por fotograma a 20 FPS, tanto como la zona de
         * recogida): tambien se prueban los puntos intermedios del camino desde el fotograma anterior */
        V3 rel = v3sub(sp, c);
        int got = coin_touch(sp, a, b);
        if (!got && k->prel_ok) {
            V3 dr = v3sub(rel, k->prel);
            int steps = (int)(v3len(dr) * 0.5f);
            if (steps > 16) steps = 16;
            for (int s = 1; s <= steps && !got; s++)
                got = coin_touch(v3add(c, v3add(k->prel, v3scale(dr, (float)s / (steps + 1)))), a, b);
#ifdef AUTOTEST
            if (got) dlog("MONEDA recogida gracias al barrido (se movio %.1f en un fotograma)", v3len(dr));
#endif
        }
        k->prel = rel; k->prel_ok = 1;
        if (got && !ch.stopColliding) { collect_coin(i); continue; }
        if (magnet || sneak) {
            V3 mc = magnet ? magc : v3add(cc.pos, v3(0, -21.0f, 3.0f));
            float r = magnet ? 60.0f : 2.0f;
            int hit;
            if (magnet) hit = v3len(v3sub(sp, mc)) < r + 4.5f;
            else {
                /* capsula de las zapatillas: r=2, h=60, centro (0,-21,3) */
                V3 sa = v3add(cc.pos, v3(0, -21.0f - 28.0f, 3.0f)), sb = v3add(cc.pos, v3(0, -21.0f + 28.0f, 3.0f));
                V3 sab = v3sub(sb, sa);
                float tt = clamp01(v3dot(v3sub(sp, sa), sab) / v3dot(sab, sab));
                hit = v3len(v3sub(sp, v3add(sa, v3scale(sab, tt)))) < 2.0f + 4.5f;
            }
            if (hit) {
                k->collider = 0;
                k->pulling = 1;
                k->pull_t = game_time;
                k->pull_from = sp;
                float dist = v3len(v3sub(sp, cc.pos));
                k->pull_dur = dist / ((magnet ? MAGNET_PULL : SNEAKERS_PULL) * normalized_speed());
                if (k->pull_dur < 0.01f) k->pull_dur = 0.01f;
            }
        }
    }
}

/* ---- power-ups recogidos ---- */
static void mod_add(int m);
static void start_jetpack(int headstart, float hs_dist);

static void notify_pickup(int node)
{
    const uint32_t *p = node_comp(node, C_PICKUP, NULL);
    if (!p || nrt[node].picked) return;
    int kind = (int)p[0];
    /* PickupDefault.OnPickup: desactiva el collider padre y oculta la malla */
    const uint32_t *pd = node_comp(node, C_PICKUPDEFAULT, NULL);
    for (int k = node; k >= 0; k = gd.nodes[k].parent)
        if (node_comp(k, C_BOX, NULL) || node_comp(k, C_SPHERE, NULL) || node_comp(k, C_CAPSULE, NULL)) { nrt[k].col = 0; break; }
    if (pd) {
        if ((int)pd[0] >= 0) nrt[pd[0]].rend = 0;
        scene_glow_visible((int)pd[1], 0);
    }
    nrt[node].picked = 1;
    switch (kind) {
    case PK_MAGNET: mod_add(MOD_MAGNET); play_ci(&snd_powerup); missions_did(M_MAGNETS, 1); break;
    case PK_SNEAKERS: mod_add(MOD_SNEAKERS); play_ci(&snd_powerup); missions_did(M_SUPERSNEAKERS, 1); break;
    case PK_DOUBLE: mod_add(MOD_DOUBLE); play_ci(&snd_powerup); missions_did(M_DOUBLEMULTIPLIER, 1); break;
    case PK_JETPACK: start_jetpack(0, 0); play_ci(&snd_powerup); missions_did(M_JETPACK, 1); break;
    case PK_MYSTERY:
        /* MysteryBoxPickup: se abre al terminar la carrera */
        ginfo.boxes_pending++;
        score += score_multiplier() * 50;
        missions_did(M_SCORE, score_multiplier() * 50);
        missions_did(M_MYSTERYBOXES, 1);
        play_ci(&snd_powerup);
        break;
    case PK_COIN:
        /* moneda suelta de la escena (la de MovingCoin, encima de los trenes en marcha): Coin.OnPickup.
         * Antes no hacia nada: ni sonaba ni sumaba */
        coins_run++;
        missions_did(M_EARNCOIN, 1);
        pickup_coin_effect(m34_pos(&nrt[node].world).y);
        nrt[node].taken = 1;
#ifdef AUTOTEST
        dlog("MONEDA suelta recogida a %d m", ginfo.meters);
#endif
        break;
    default: break;
    }
}

/* ------------------------------------------------------------ modificadores */
static void mod_begin(int m)
{
    mods[m].paused = 0;
    mods[m].stop = STOP_DONT;
    mods[m].phase = 0;
    switch (m) {
    case MOD_MAGNET:
        if (sfx_magnetloop >= 0) { snd_stop(h_magnetloop); h_magnetloop = snd_play(sfx_magnetloop, 0.2f, 0.25f, 1); }
        stumble_set(0);
        set_node_active(n_meshMagnet, 1);
        { AnimState *s = anim_get(&anim_char, "hold_magnet"); if (s) { s->enabled = 1; s->weight = 0.9f; s->target_weight = 0.9f; s->time = 0; } }
        trigger_powerup(PT_MAGNET, PW_MAGNET);
        mods[m].active = 1;
        break;
    case MOD_SNEAKERS:
        stumble_set(0);
        mods[m].active = 1;
        set_node_active(n_meshSneakers, 1);
        ChangeAnimations();
        trigger_powerup(PT_SNEAKERS, PW_SNEAKERS);
        ch.jumpHeight = JUMP_HEIGHT_SUPER;
        scene_ssgroups(1);
        break;
    case MOD_DOUBLE:
        trigger_powerup(PT_DOUBLE, PW_DOUBLE);
        double_score = 1;
        mods[m].active = 1;
        break;
    case MOD_HOVERBOARD: {
        float since = game_time - hb_last_end;
        if (since < HB_PARTICLE_DELAY + HB_COOLDOWN) { mods[m].active = 0; mods[m].phase = 99; return; }
        ginfo.hoverboards--;
        missions_did(M_HOVERBOARD, 1);
        mods[m].paused = 0;
        stumble_set(0);
        mods[m].active = 1;
        ChangeAnimations();
        anim_crossfade(&anim_char, "h_skate_on", 0.06f);
        anim_crossfade_queued(&anim_char, "h_run", 0.2f);
        play_ci(&snd_powerup);
        ch.immune = 1;
        trigger_powerup(PT_HOVERBOARD, 0);
        set_node_active(n_meshHoverboard, 1);
        break;
    }
    }
}

static void mod_end_common(int m)
{
    switch (m) {
    case MOD_MAGNET:
        set_node_active(n_meshMagnet, 0);
        { AnimState *s = anim_get(&anim_char, "hold_magnet"); if (s) { s->enabled = 0; s->weight = 0; } }
        snd_stop(h_magnetloop); h_magnetloop = -1;
        if (pw_left[PT_MAGNET] <= 0) play_ci(&snd_powerdown);
        break;
    case MOD_SNEAKERS:
        scene_ssgroups(0);
        ch.jumpHeight = JUMP_HEIGHT_NORMAL;
        set_node_active(n_meshSneakers, 0);
        mods[m].active = 0;
        ChangeAnimations();
        if (pw_left[PT_SNEAKERS] <= 0) play_ci(&snd_powerdown);
        break;
    case MOD_DOUBLE:
        double_score = 0;
        if (pw_left[PT_DOUBLE] <= 0) play_ci(&snd_powerdown);
        break;
    }
    mods[m].active = 0;
}

static void mod_add(int m)
{
    if (mods[m].active || mods[m].phase > 0) {
        /* Reset + Begin de nuevo */
        if (m == MOD_HOVERBOARD) { ch.immune = 0; cc.enabled = 1; set_node_active(n_meshHoverboard, 0); mods[m].active = 0; }
    }
    mod_begin(m);
}

static void mods_update(float dt)
{
    (void)dt;
    for (int m = 0; m < MOD_COUNT; m++) {
        if (mods[m].paused) continue;
        if (m == MOD_HOVERBOARD) {
            if (mods[m].phase == 99 || (!mods[m].active && mods[m].phase == 0)) continue;
            if (mods[m].phase == 0) {
                if (pw_left[PT_HOVERBOARD] > 0 && mods[m].stop == STOP_DONT) continue;
                if (mods[m].stop == STOP_DONT) { play_ci(&snd_powerdown); missions_did(M_HOVERBOARDEXPIRE, 1); }
                set_node_active(n_meshHoverboard, 0);
                ch.immune = 0;
                mods[m].active = 0;
                ChangeAnimations();
                hb_last_end = game_time;
                if (mods[m].stop != STOP_STOP) { mods[m].phase = 99; continue; }
                play_ci(&snd_hcrash);
                mods[m].phase = 1;
                mods[m].t0 = game_time;
                continue;
            }
            if (mods[m].phase == 1) {
                if (game_time - mods[m].t0 < HB_PARTICLE_DELAY) continue;
                track_remove_obstacles(ch.z + HB_REMOVE_DIST * normalized_speed());
                ch.jumping = 1;
                ch.falling = 0;
                ch.verticalSpeed = jump_vs(10.0f);
                anim_crossfade(&anim_char, an.jump, 0.05f);
                mods[m].dist_left = HB_SLOWMO_DIST * normalized_speed();
                mods[m].cooldown_left = HB_COOLDOWN_DIST * normalized_speed();
                mods[m].did_stop_cd = 0;
                mods[m].phase = 2;
                continue;
            }
            if (mods[m].phase == 2) {
                float d = currentLevelSpeed * dt;
                mods[m].dist_left -= d;
                mods[m].cooldown_left -= d;
                if (mods[m].cooldown_left < 0 && !mods[m].did_stop_cd) { ch.immune = 0; mods[m].did_stop_cd = 1; }
                if (mods[m].dist_left <= 0) mods[m].phase = 99;
            }
            continue;
        }
        if (!mods[m].active) continue;
        int pt = m == MOD_MAGNET ? PT_MAGNET : m == MOD_SNEAKERS ? PT_SNEAKERS : PT_DOUBLE;
        if (pw_left[pt] > 0 && mods[m].stop == STOP_DONT) continue;
        mod_end_common(m);
    }
}

static void mods_pause_jetpack(void)
{
    mods[MOD_HOVERBOARD].paused = mods[MOD_HOVERBOARD].active;
    if (mods[MOD_HOVERBOARD].active) set_node_active(n_meshHoverboard, 0);
    mods[MOD_SNEAKERS].paused = mods[MOD_SNEAKERS].active;
}

static void mods_resume(void)
{
    for (int m = 0; m < MOD_COUNT; m++) mods[m].paused = 0;
    if (mods[MOD_HOVERBOARD].active) set_node_active(n_meshHoverboard, 1);
}

static void mods_stop_no_ending(void)
{
    for (int m = 0; m < MOD_COUNT; m++) if (mods[m].active) mods[m].stop = STOP_NOEND;
}

static void mods_reset_all(void)
{
    for (int m = 0; m < MOD_COUNT; m++) {
        if (mods[m].active) mod_end_common(m);
        memset(&mods[m], 0, sizeof(mods[m]));
    }
    ch.immune = 0;
    set_node_active(n_meshHoverboard, 0);
    set_node_active(n_meshSneakers, 0);
    set_node_active(n_meshMagnet, 0);
    double_score = 0;
    scene_ssgroups(0);
    ch.jumpHeight = JUMP_HEIGHT_NORMAL;
}

/* ------------------------------------------------------------ camara */
static void camera_shake(void)
{
    shake_t = game_time;
    shake_diff = v3(0, 0, 0);
}

static void camera_shake_tick(float dt)
{
    if (shake_t < 0) { cam_shake = v3(0, 0, 0); return; }
    float t = (game_time - shake_t) / 0.3f;
    if (t >= 1) { t = 1; shake_t = -1; }
    shake_diff = v3add(shake_diff, v3(rnd_rangef(-1, 1), rnd_rangef(-1, 1), rnd_rangef(-1, 1)));
    cam_shake = v3scale(shake_diff, (1.0f - t) * 100.0f * dt);
}

/* ------------------------------------------------------------ estados del juego */
static void change_state(int s) { state = s; state_time = game_time; }

static void stage_menu_sequence(void)
{
    AnimState *s = anim_get(&anim_char, an.stumbleDeath);
    if (s) s->enabled = 0;
    ch.shadow_on = 1;
    gu.enabled = 0;
    guard_show(0);
    gu.tw = gu.hide_pending = gu.jump_pending = gu.catching = gu.hit_train = 0;
    stop_all_char_coroutines();
    char_set_pos(v3(0, 0.8f, 0));
    nrt[n_avatar].lrot = qident();
    nrt[n_sprayCan].rend = ginfo.character == 0;
    cam_follow = 0;
    if (n_mainCam >= 0) {
        nrt[n_mainCam].lpos = v3(0, 0.8f + 33.0f + 0.8f, -33.0f);
        nrt[n_mainCam].lrot = qeuler(21.50143f, 0, 0);
    }
    anim_play(&anim_char, "idlePaint");
}

static void game_top_menu(void)
{
    change_state(GS_MENU);
    music_set_volume(0.25f);
    guard_mute();
    track_deactivate_all();
    mods_stop_no_ending();
    mods_reset_all();
    clear_powerups();
    coins_release_inair();
    guard_show(0);
    stage_menu_sequence();
    anim_crossfade(&anim_cam, "menuIdle", 0.1f);
    snd_stop(h_jetloop); h_jetloop = -1;
    ginfo.state = GS_MENU;
}

static void game_start_run(void)
{
    tut_board_ok = 0; tut_arrow_on = 0; ginfo.message = NULL;
    /* Game.Intro */
    score = 0; coins_run = 0; meters = 0; meters_last = 0;
    music_set_volume(0.3f);
    music_restart();
    guard_mute();
    isDead = 0;
    stage_menu_sequence();
    guard_show(1);
    guard_play_intro();
    currentLevelSpeed = Speed(0);
    start_time = game_time;
    character_restart();
    spawn_restart();
    track_restart();
    track_lay_chunks(0, track_ahead_distance(), 0);
    anim_crossfade(&anim_cam, "startPan", 0.2f);
    anim_crossfade(&anim_char, "introRun", 0.2f);
    change_state(GS_INTRO);
    ginfo.state = GS_INTRO;
}

static void running_begin(void);

static void intro_tick(void)
{
    if (game_time - state_time < anim_length(&anim_char, "introRun")) return;
    score = 0; coins_run = 0; meters = 0; meters_last = 0;
    nrt[n_sprayCan].rend = 0;
    gu.enabled = 1;
    guard_on_enable();
    if (track_running_tutorial) { guard_reset_catch_up(); ch.stumble = 0; }
    running_begin();
    ginfo.headstart_until = (ginfo.hs500 > 0 || ginfo.hs2000 > 0) ? game_time + 5.0f : 0;
    missions_run_start();
}

static void running_begin(void)
{
    in_left = in_right = in_up = in_down = in_board = 0;
    from_jetpack = cc.pos.y > 70.0f;
    nrt[n_colChar].active = 1;
    cam_follow = 1;
    cam_off = v3(0, CAM_OFFSET_Y, CAM_OFFSET_Z); cam_off_v = v3(0, 0, 0);
    /* posicion inicial de la camara: la del rig animado */
    if (n_mainCam >= 0) {
        V3 p = node_pos(n_mainCam);
        sm_x = p.x;
        if (!from_jetpack) {
            cam_pos = p;
            /* el primer fotograma mira hacia donde miraba la camara de la intro (antes cam_target aun valia
             * (0,0,0) y la camara apuntaba un instante al origen: el "salto" al empezar la carrera) */
            cam_target = v3add(p, v3scale(v3norm(m34_col(&nrt[n_mainCam].world, 2)), 35.0f));
        }
    }
    sm_xv = 0;
    aim = CAM_AIM; aim_v = 0;
    tunnel_on = 0;
    ch.lastGroundedY = cc.pos.y;
    from_jet_t = 0;
    from_jet_pos = cam_pos;
    from_jet_tgt = cam_target;
    ch.fallAnim = 0;
    cc_move(&cc, v3(0, -2, 0));
    ch.fallAnim = 1;
    ChangeAnimations();
    sm_y = cc.pos.y; sm_yv = 0; sm_y_target = sm_y;
    change_state(GS_RUNNING);
    ginfo.state = GS_RUNNING;
}

#ifdef AUTOTEST
/* piloto automatico para las pruebas: mira los obstaculos de cada carril y decide como el jugador */
static float ap_next;
static void autopilot(void)
{
    if (game_time < ap_next || ch.tc || ch.rolling) return;
    float z = cc.pos.z, look = currentSpeed * 0.9f;
    float firstz[3] = {1e9f, 1e9f, 1e9f};
    int firstcol[3] = {-1, -1, -1};
    for (int i = 0; i < ncols; i++) {
        const Col *k = &cols[i];
        if (k->layer != 10 || k->bmax.z < z || k->bmin.z > z + 400.0f) continue;
        if (k->bmax.y < cc.pos.y + 1.0f || k->bmin.y > cc.pos.y + 9.0f) continue;
        for (int L = 0; L < 3; L++) {
            float lx = track_pos(track_get_x(L), 0).x;
            if (k->bmax.x < lx - 6.0f || k->bmin.x > lx + 6.0f) continue;
            if (k->bmin.z < firstz[L]) { firstz[L] = k->bmin.z; firstcol[L] = i; }
        }
    }
    int L = ch.trackIndex;
    float d = firstz[L] - z;
    if (d > look) return;
    const Col *k = &cols[firstcol[L]];
    float h = k->bmax.y - cc.pos.y;
    const char *nm = gd.strings[gd.nodes[k->node].name];
    if (k->trigger || h < 13.0f || !strcmp(nm, "blocker_jump")) { if (d < 35.0f) { in_up = 1; ap_next = game_time + 0.3f; } return; }
    if (k->bmin.y > cc.pos.y + 6.0f || !strcmp(nm, "blocker_roll")) { if (d < 35.0f) { in_down = 1; ap_next = game_time + 0.3f; } return; }
    /* tren o pared: al carril con mas espacio */
    int best = L;
    for (int c = 0; c < 3; c++) if (firstz[c] > firstz[best] + 20.0f && abs(c - L) == 1) best = c;
    if (best == L) for (int c = 0; c < 3; c++) if (firstz[c] > firstz[best] + 20.0f) best = c;
    if (best < L) in_left = 1; else if (best > L) in_right = 1;
    else if (d < 40.0f) in_up = 1;
    ap_next = game_time + 0.25f;
}
#endif

static void handle_controls_running(void)
{
#ifdef AUTOTEST
    autopilot();
#endif
    float dur = CHANGE_TRACK_LENGTH / (currentSpeed > 1 ? currentSpeed : 1);
    if (in_left) change_track(-1, dur);
    if (in_right) change_track(1, dur);
    if (in_up) character_jump();
    if (in_down) character_roll();
    if (in_board && !mods[MOD_HOVERBOARD].active) {
        /* en el tutorial la tabla se prueba gratis, pero solo desde su aviso (allowHoverboard) */
        if (track_running_tutorial) { if (tut_board_ok) { ginfo.hoverboards++; mod_add(MOD_HOVERBOARD); } }
        else if (ginfo.hoverboards > 0) mod_add(MOD_HOVERBOARD);
    }
    in_left = in_right = in_up = in_down = in_board = 0;
}

static void update_ground_tag(void)
{
    float d;
    int hit = phys_raycast_down(node_pos(n_avatar), 1000.0f, &d, 1);
    if (hit < 0) return;
    switch (cols[hit].tag) {
    case TAG_GROUND: ch.runPosition = RP_GROUND; break;
    case TAG_HITTRAIN: ch.runPosition = RP_TRAIN; break;
    case TAG_HITMOVINGTRAIN: ch.runPosition = RP_MOVINGTRAIN; break;
    case TAG_STATION: ch.runPosition = RP_STATION; break;
    }
}

static void update_meters(void) { meters = (int)roundf(ch.z / DISTANCE_PER_METER); }

static void calculate_score(void)
{
    if (meters_last < meters) {
        float ms = meters - meters_last;
        meters_last = meters;
        int num = (int)(ms * score_multiplier());
        score += num;
        missions_did(M_SCORE, num);
        if (coins_run <= 0) missions_did(M_NOCOINSBEFORESCORE, num);
    }
}

/* camara que sigue al personaje (CharacterCamera): en carrera y al volver al punto de control del tutorial */
static void follow_camera(float dt)
{
    V3 position = cc.pos;
    if (mods[MOD_SNEAKERS].active) sm_y_target = 0.5f * (ch.lastGroundedY + position.y);
    else if (cc.grounded) sm_y_target = ch.lastGroundedY;
    else if (position.y < ch.lastGroundedY) sm_y_target = position.y;
    sm_y = smoothdamp(sm_y, sm_y_target, &sm_yv, CAM_Y_SMOOTH, dt);
    cam_off.x = smoothdamp(cam_off.x, 0, &cam_off_v.x, 0.7f, dt);
    cam_off.y = smoothdamp(cam_off.y, CAM_OFFSET_Y, &cam_off_v.y, 0.7f, dt);
    cam_off.z = smoothdamp(cam_off.z, CAM_OFFSET_Z, &cam_off_v.z, 0.7f, dt);
    aim = smoothdamp(aim, CAM_AIM, &aim_v, 0.7f, dt);
    sm_x = smoothdamp(sm_x, position.x * 0.75f, &sm_xv, CAM_X_SMOOTH, dt);
    /* curva del tunel (Running.StartTunnel) */
    float od = 0;
    if (tunnel_on) {
        static Key k[3];
        k[0] = (Key){0, 0, -1, -1};
        k[1] = (Key){tunnel_len / 2, TUNNEL_DELTA, 0, 0};
        k[2] = (Key){tunnel_len, 0, 0.001f, 0.001f};
        od = curve_eval(k, 3, ch.z - tunnel_start);
        if (ch.z - tunnel_start > tunnel_len) tunnel_on = 0;
    }
    V3 pe = v3add(v3add(v3(sm_x, sm_y, position.z), cam_off), v3(0, od, 0));
    V3 te = v3add(v3add(v3(sm_x, sm_y, position.z), v3(0, aim, 0)), v3(0, od * 0.5f, 0));
    if (ch.isInsideSubway) {
        if (pe.y > 64.0f) pe.y = 64.0f;
        if (te.y > 64.0f) te.y = 64.0f;
    }
    if (from_jetpack) {
        float r = clamp01(from_jet_t / 2.0f);
        float w = curve_eval(transitionFromJetpackCurve, 3, r);
        cam_pos = v3lerp(from_jet_pos, pe, w);
        cam_target = v3lerp(from_jet_tgt, te, w);
        if (r >= 1) from_jetpack = 0;
        from_jet_t += dt;
    } else { cam_pos = pe; cam_target = te; }
}

/* Frenado de frente contra algo solido sin que salte el choque: el aviso de choque solo llega al entrar en el
 * collider, y si se entra por un lado (tropiezo lateral, o al terminar de rodar) y luego el personaje queda delante
 * de el, la capsula lo frena y se quedaba corriendo contra el pilar con el inspector detras. Si casi no avanza
 * durante 0.2 s se trata como el choque frontal que es. */
static float blocked_t;

static void blocked_check(float moved, float want, float dt)
{
    /* (tras romperse la tabla el original espera a las particulas y luego quita los obstaculos de delante) */
    int board_crash = mods[MOD_HOVERBOARD].phase == 1;
    if (state != GS_RUNNING || ch.stopColliding || ch.immune || ch.hbt || board_crash || want < 0.05f ||
        moved > want * 0.25f) {
        blocked_t = 0;
        return;
    }
    blocked_t += dt;
    if (blocked_t < 0.2f) return;
    blocked_t = 0;
    dlog("ATASCADO de frente a %d m, carril %d: cuenta como choque", ginfo.meters, ch.trackIndex);
    /* contra que: el collider que se esta tocando (un tren en marcha se para con el personaje, porque su posicion
     * depende de la de el, y los dos se quedaban quietos) */
    ch.lastHitTag = TAG_UNTAGGED;
    for (int j = 0; j < nover_prev; j++)
        for (int k = 0; k < ncols; k++)
            if (cols[k].node == over_prev[j] && !cols[k].trigger && cols[k].layer != 0 &&
                (cols[k].tag == TAG_HITMOVINGTRAIN || ch.lastHitTag == TAG_UNTAGGED)) ch.lastHitTag = cols[k].tag;
    if (ch.lastHitTag == TAG_HITMOVINGTRAIN) hit_by_train_sequence();
    else anim_crossfade(&anim_char, an.hitMid, 0.07f);
    notify_critical_hit();
}

static void running_tick(float dt)
{
    track_lay_chunks(ch.z, track_ahead_distance(), 0);
    scene_current_level_speed = currentLevelSpeed;
    currentSpeed = currentLevelSpeed;
    handle_controls_running();
    apply_gravity(dt);
    float z0 = ch.z;
    move_forward(dt);
    blocked_check(ch.z - z0, currentSpeed * dt, dt);
    follow_camera(dt);
    /* CheckInAirJump */
    if (cc.grounded && ch.inAirJump) { character_jump(); ch.inAirJump = 0; }
    update_meters();
    if (!cc.grounded) ch.runPosition = RP_AIR;
}

/* ---- Jetpack ---- */
static void start_jetpack(int headstart, float hs_dist)
{
    if (state == GS_JETPACK) return;
    jp.headStart = headstart;
    jp.headStartDistance = hs_dist;
    jp.phase = 0;
    change_state(GS_JETPACK);
    ginfo.state = GS_JETPACK;
    /* Jetpack.Begin (parte sincrona) */
    trigger_powerup(headstart ? PT_HEADSTART : PT_JETPACK, PW_JETPACK);
    if (sfx_jetloop >= 0) { snd_stop(h_jetloop); h_jetloop = snd_play(sfx_jetloop, 0.4f, 1.1f, 1); }
    mods_pause_jetpack();
    stumble_set(0);
    set_node_active(n_meshJetpack, 1);
    ch.shadow_on = 0;
    an.run = "jetpack_forward"; an.dodgeLeft = "jetpack_forward"; an.dodgeRight = "jetpack_forward";
    anim_crossfade(&anim_char, an.run, 0.3f);
    jp.cam_off0 = v3sub(cam_pos, cc.pos);
    jp.aim0 = CAM_AIM;
    jp.startY = cc.pos.y;
    cc.detect = 0;
    nrt[n_colChar].active = 0;
    jp.speed = headstart ? 1000.0f : currentLevelSpeed * JET_SPEEDUP;
    jp.flyDur = headstart ? jp.headStartDistance / 1000.0f : pw_left[PT_JETPACK];
    float flyDist = jp.speed * jp.flyDur;
    float aheadDist = jp.speed * JET_FLY_AHEAD;
    float jd = aheadDist + flyDist;
    jd = track_lay_jetpack_chunks(ch.z, jd) - JET_STOP_BEFORE_LANDING * normalized_speed();
    float extDur = jd / jp.speed;
    jp.extFly = extDur - JET_FLY_AHEAD;
    flyDist = jp.extFly * jp.speed;
    if (!headstart) {
        /* InAirCoinsManager.Spawn */
        float cd = flyDist - JET_COIN_OFFSET, cz = ch.z + aheadDist + JET_COIN_OFFSET;
        float keys_z[256], keys_x[256]; int nk = 0, tr = 1;
        for (float z = cz; z < cz + cd && nk < 250; z += JET_CHANGE_TRACK_LENGTH + INAIR_STAY) {
            keys_z[nk] = z; keys_x[nk++] = track_get_x(tr);
            keys_z[nk] = z + INAIR_STAY; keys_x[nk++] = track_get_x(tr);
            tr = (int)clampf((float)(tr + rnd_range(-1, 2)), 0, 2);
            keys_z[nk] = z + INAIR_STAY + JET_CHANGE_TRACK_LENGTH; keys_x[nk++] = track_get_x(tr);
        }
        for (float z = cz; z < cz + cd; z += INAIR_COIN_DIST) {
            float x = keys_x[0];
            for (int i = 0; i + 1 < nk; i++) {
                if (z >= keys_z[i] && z <= keys_z[i + 1]) {
                    float t = (z - keys_z[i]) / (keys_z[i + 1] - keys_z[i] + 1e-6f);
                    x = keys_x[i] + (keys_x[i + 1] - keys_x[i]) * smoothstepf(0, 1, t);
                    break;
                }
                if (z > keys_z[i + 1]) x = keys_x[i + 1];
            }
            coin_spawn(v3add(track_pos(x, z), v3(0, JET_FLY_HEIGHT, 0)), -2);
        }
    } else play_ci(&snd_powerup);
    jp.landingZ = ch.z + jd;
    jp.cameraZ = ch.z;
    jp.startTime = game_time;
    currentSpeed = jp.speed;
    jp.cam_pos0 = cam_pos;
    jp.cam_tgt0 = cam_target;
    scene_set_active_recursive(n_jetParticles, 1);
    jp.phase = 1;
}

static void jetpack_tick(float dt)
{
    /* HandleControls (solo izquierda/derecha) */
    float dur = JET_CHANGE_TRACK_LENGTH / (currentSpeed > 1 ? currentSpeed : 1);
    if (in_left) change_track(-1, dur);
    if (in_right) change_track(1, dur);
    in_left = in_right = in_up = in_down = in_board = 0;
    if (jp.phase == 1) {
        float ratio = (game_time - jp.startTime) / JET_FLY_AHEAD;
        if (ratio >= 1) {
            nrt[n_colChar].active = 1;
            jp.phase = 2;
            jp.startTime = game_time;
        } else {
            V3 off = v3lerp(jp.cam_off0, v3(0, CAM_OFFSET_Y, CAM_OFFSET_Z), smoothstepf(0, 1, ratio));
            float aimv = lerpf(jp.aim0, JET_CAM_AIM, smoothstepf(0, 1, ratio));
            ch.z += jp.speed * dt;
            V3 pivot = v3add(track_pos(ch.x, ch.z), v3(0, jp.startY + (JET_FLY_HEIGHT - jp.startY) * smoothstepf(0, 1, ratio), 0));
            char_set_pos(pivot);
            V3 pe = v3add(v3(track_pos(ch.x, ch.z).x, pivot.y, ch.z), off);
            V3 te = v3add(pivot, v3(0, aimv, 0));
            float w = curve_eval(flyAheadCameraCurve, 3, ratio);
            V3 np = v3lerp(jp.cam_pos0, pe, w), nt = v3lerp(jp.cam_tgt0, te, w);
            np.x = pe.x; nt.x = pe.x;
            cam_pos = np; cam_target = nt;
            update_meters();
            track_lay_chunks(ch.z, track_ahead_distance(), 0);
            return;
        }
    }
    if (jp.phase == 2) {
        float ratio = (game_time - jp.startTime) / jp.extFly;
        if (ratio < 1) {
            ch.z += jp.speed * dt;
            char_set_pos(v3add(track_pos(ch.x, ch.z), v3(0, JET_FLY_HEIGHT, 0)));
            cam_pos = v3add(cc.pos, v3(0, CAM_OFFSET_Y, CAM_OFFSET_Z));
            cam_target = v3add(cc.pos, v3(0, JET_CAM_AIM, 0));
            update_meters();
            track_lay_chunks(ch.z, track_ahead_distance(), 0);
            return;
        }
        /* fin del jetpack */
        cc.detect = 1;
        scene_set_active_recursive(n_jetParticles, 0);
        set_node_active(n_meshJetpack, 0);
        snd_stop(h_jetloop); h_jetloop = -1;
        coins_release_inair();
        mods_resume();
        pw_on[PT_JETPACK] = 0; pw_on[PT_HEADSTART] = 0;
        running_begin();
        ChangeAnimations();
        play_ci(&snd_powerdown);
    }
}

/* ---- muerte ---- */
static void notify_critical_hit(void)
{
    if (state != GS_RUNNING && state != GS_JETPACK) return;
#ifdef AT_GOD
    return;   /* prueba automatica: invencible para recorrer tramos dificiles */
#endif
    dlog("CHOQUE contra %s en el carril %d a %d m (velocidad %.0f)%s", ch.lastHitTag == TAG_HITTRAIN ? "tren" :
         ch.lastHitTag == TAG_HITMOVINGTRAIN ? "tren en marcha" : ch.lastHitTag == TAG_HITBARRIER ? "barrera" : "obstaculo",
         ch.trackIndex, ginfo.meters, (double)currentSpeed, mods[MOD_HOVERBOARD].active ? " - salva la tabla" : "");
    play_ci(&snd_die);
    if (ch.lastHitTag == TAG_HITTRAIN) missions_did(M_CRASHTRAINS, 1);
    else if (ch.lastHitTag == TAG_HITBARRIER) missions_did(M_CRASHBARRIERS, 1);
    else if (ch.lastHitTag == TAG_HITMOVINGTRAIN) { missions_did(M_DIETOTRAIN, 1); missions_did(M_CRASHTRAINS, 1); }
    if (state == GS_RUNNING) { camera_shake(); game_die(); }
}

static int stumble_pending; static float stumble_pending_t;

static void stumble_death_check(void)
{
    /* Game.OnStumble: si ya estaba tropezado y hay estado -> StumbleDeathSequence */
    if (ch.stumble && (state == GS_RUNNING || state == GS_JETPACK)) {
        stumble_pending = 1;
        stumble_pending_t = game_time + 0.2f;
    }
}

static void back_to_checkpoint(void);

static void game_die(void)
{
    if (mods[MOD_HOVERBOARD].active) {
        guard_mute();
        guard_reset_catch_up();
        ch.stumble = 0;
        guard_restart(0);
        mods[MOD_HOVERBOARD].stop = STOP_STOP;
        pw_on[PT_HOVERBOARD] = 0; pw_left[PT_HOVERBOARD] = 0;
        return;
    }
    if (track_running_tutorial) { back_to_checkpoint(); return; }
    clear_powerups();
    isDead = 1;
    missions_did(M_TIMEDEATH, (int)(game_time - start_time));
    moving_autopilot(ch.z);
    if (gu.showing) {
        if (anim_is_playing(&anim_char, "death_movingTrain")) guard_hit_by_train();
        else guard_catch_player(ch.x - track_get_x(ch.trackIndex) + 20.0f * 0);
    }
    gu.enabled = 0;
    stumble_pending = 0;
    change_state(GS_DIEFALL);
    ginfo.state = GS_DIEFALL;
}


static void die_tick(float dt)
{
    if (state == GS_DIEFALL) {
        if (!cc.grounded) { move_with_gravity(dt); return; }
        change_state(GS_DIE);
        return;
    }
    if (state == GS_DIE) {
        float e = game_time - state_time;
        if (e < 0.5f) return;
        if (e < 2.0f && !in_up) return;
        in_up = 0;
        /* fin de la partida: puntuacion final = puntos + monedas*2*multiplicador */
        missions_run_end();
        ginfo.final_score = score + coins_run * 2 * missions_multiplier();
        ginfo.total_coins += coins_run;
        ginfo.gameover_coins = coins_run;
        ginfo.reward[0] = 0;
        if (ginfo.final_score > ginfo.highscore) ginfo.highscore = ginfo.final_score;
        game_save();
        change_state(GS_GAMEOVER);
        ginfo.state = GS_GAMEOVER;
    }
}

/* ------------------------------------------------------------ tutorial (TutorialEvent.cs, Game.BackToCheckPoint) */
static float tut_arrow_t0, tut_arrow_time, tut_arrow_dir;

static void tut_message(const char *txt)
{
    ginfo.message = txt;
    ginfo.message_t = game_time;
}

/* TutorialEvent.OnTriggerExit: textos del original con los botones de la PSP */
static void tutorial_event(const uint32_t *t)
{
#ifdef AUTOTEST
    dlog("TUTORIAL evento texto%d flecha%d dir %.0f fin%d a %d m", t[0], t[2], u2f(t[4]), t[6], ginfo.meters);
#endif
    if (t[0]) {
        const char *txt = (int)t[1] >= 0 ? gd.strings[t[1]] : "";
        if (strstr(txt, "GO")) tut_message(TR("Genial! Ahora a correr!", "You rock! Now GO!"));
        else if (strstr(txt, "Hoverboard")) tut_message(TR("Cuadrado o triangulo: tabla", "Square or triangle: Hoverboard"));
        else if (strstr(txt, "Fresh")) tut_message(TR("Buenos movimientos!", "Fresh Moves!"));
        else tut_message(txt);
    }
    if (t[2] && (int)t[3] >= 0) {
        /* ShowArrow: la flecha 3D delante de la camara, y el boton de la PSP debajo */
        n_tut_arrow = t[3];
        tut_arrow_dir = u2f(t[4]);
        tut_arrow_time = u2f(t[5]) > 0.05f ? u2f(t[5]) : 1.0f;
        tut_arrow_t0 = game_time;
        tut_arrow_on = 1;
        /* direccion de la flecha: 0 arriba, 180 abajo, 90 izquierda, -90 derecha */
        if (fabsf(tut_arrow_dir) > 135.0f) tut_message(TR("ABAJO o O: rodar", "DOWN or O: roll"));
        else if (tut_arrow_dir > 45.0f) tut_message(TR("IZQUIERDA: cambiar de via", "LEFT: change lane"));
        else if (tut_arrow_dir < -45.0f) tut_message(TR("DERECHA: cambiar de via", "RIGHT: change lane"));
        else tut_message(TR("ARRIBA o X: saltar", "UP or X: jump"));
    }
    if (t[7]) tut_board_ok = 1;
    if (t[6]) {
        /* endTutorial: a partir de aqui, carrera normal; no se vuelve a ver */
        track_running_tutorial = 0;
        track_tutorial = 0;
        tutorial_completed = 1;
        game_save();
        dlog("TUTORIAL completado");
    }
}

/* la flecha del tutorial: hija de la camara, a 20 unidades, girada segun la direccion y desplazandose por ella */
static void tutorial_draw_arrow(void)
{
    if (!tut_arrow_on || n_tut_arrow < 0) return;
    float t = (game_time - tut_arrow_t0) / tut_arrow_time;
    if (t >= 1.0f) { tut_arrow_on = 0; return; }
    Q4 r = qeuler(0, 0, tut_arrow_dir);
    V3 up = qrot(r, v3(0, 1, 0));
    V3 pos = v3add(v3(0, 0, 20), v3scale(up, -5.0f + 10.0f * t));
    M34 cw, local;
    m34_inverse(&cw, &r_view);
    /* el shader de particulas del original no descarta caras: se gira 180 grados en Y para que el plano mire a la camara
     * (la flecha es simetrica, asi que el giro no cambia su direccion) */
    m34_trs(&local, pos, qmul(r, qeuler(0, 180, 0)), nrt[n_tut_arrow].lscl);
    m34_mul(&nrt[n_tut_arrow].world, &cw, &local);
    int was_active = nrt[n_tut_arrow].active, was_rend = nrt[n_tut_arrow].rend;
    nrt[n_tut_arrow].active = 1;
    nrt[n_tut_arrow].rend = 1;
    scene_draw_subtree(n_tut_arrow);
    nrt[n_tut_arrow].active = was_active;
    nrt[n_tut_arrow].rend = was_rend;
}

/* Character.SetBackToCheckPoint: tras 0.7 s, 1 s de vuelta suave al ultimo punto de control, en la via central */
static struct { int phase; float t0, x0, z0, x1, z1; } cpk;

static void back_to_checkpoint(void)
{
    dlog("TUTORIAL vuelta al punto de control desde %d m", ginfo.meters);
    change_state(GS_CHECKPOINT);
    cpk.phase = 0;
    stumble_pending = 0;
    stumble_set(0);
}

static void checkpoint_tick(float dt)
{
    if (cpk.phase == 0) {
        if (game_time - state_time < 0.7f) { follow_camera(dt); return; }
        cpk.phase = 1;
        cpk.t0 = game_time;
        cpk.x0 = ch.x; cpk.z0 = ch.z;
        cpk.z1 = track_last_checkpoint(ch.z);
        ch.trackIndex = ch.trackIndexTarget = 1;
        ch.trackIndexPosition = 1;
        ch.trackMovement = ch.trackMovementNext = 0;
        ch.tc = 0;
        cpk.x1 = track_get_x(1);
        ch.immune = 1;
        ch.stopColliding = 1;
        anim_crossfade(&anim_char, an.run, 1.0f);
    }
    float t = clamp01(game_time - cpk.t0), s = t * t * (3.0f - 2.0f * t);
    ch.x = lerpf(cpk.x0, cpk.x1, s);
    ch.z = lerpf(cpk.z0, cpk.z1, s);
    V3 p = track_pos(ch.x, ch.z);
    char_set_pos(v3(p.x, cc.pos.y, p.z));
    follow_camera(dt);
    if (t >= 1.0f) {
        ch.immune = 0;
        ch.stopColliding = 0;
        anim_play(&anim_char, an.run);
        change_state(GS_RUNNING);
        ginfo.state = GS_RUNNING;
    }
}

/* ------------------------------------------------------------ inicializacion */
static int find(const char *path)
{
    int n = node_find_path(-1, path);
    return n;
}

static int n_avatars = -1;            /* Character Model/avatar_prefab/avatars: un modelo por personaje */
static void char_show_model(int i);

void game_init(void)
{
    sounds_init();
    scene_jump_length = jump_length;
    n_character = find("Character");
    n_charModel = find("Character/Character Model");
    n_avatar = find("Character/Character Model/avatar_prefab");
    n_colChar = find("Character/Collider Character");
    n_colMagnet = find("Character/Collider Coin Magnet");
    n_colSneakers = find("Character/Collider Super Sneakers");
    n_shadow = find("Character/Character Model/shadowPlane_blob");
    n_spine = find("Character/Character Model/avatar_prefab/gSkeleton_game/jRoot/jLowerSpine");
    n_shoulder = node_find_path(n_spine, "jShoulder_R");
    n_jroot = find("Character/Character Model/avatar_prefab/gSkeleton_game/jRoot");
    n_sprayCan = node_find_path(n_shoulder, "jElbow_R/jHand_R/sprayCan");
    n_meshMagnet = node_find_path(n_shoulder, "jElbow_R/jHand_R/magnetBooster");
    n_meshJetpack = node_find_path(n_spine, "sprayCanBooster");
    n_meshHoverboard = find("Character/Character Model/avatar_prefab/powerups/hoverBoard");
    n_meshSneakers = find("Character/Character Model/avatar_prefab/powerups/jumpBooster");
    n_enemies = find("Enemies");
    n_guard = find("Enemies/guard_prefab");
    n_dog = find("Enemies/dog_prefab");
    n_camRig = find("cameraRig_startGame");
    n_mainCam = find("cameraRig_startGame/Main Camera");
    n_sky = find("cameraRig_startGame/Main Camera/Sky");
    n_intro_env = find("Level/Intro/Intro_environment");
    n_efx = find("EFX");
    n_jetParticles = find("EFX/Jetpack");
    n_ssRing = find("EFX/superSneakersRing");
    n_cloud[0] = find("EFX/Jetpack/jetpackCloud_L");
    n_cloud[1] = find("EFX/Jetpack/jetpackCloud_R");
    n_cloud_tgt[0] = node_find_path(n_spine, "sprayCanBooster/jetpackCloud_posL");
    n_cloud_tgt[1] = node_find_path(n_spine, "sprayCanBooster/jetpackCloud_posR");
    for (int i = 0; i < 2; i++) {
        if (n_cloud[i] < 0) continue;
        cloud_rot0[i] = nrt[n_cloud[i]].lrot;
        int pl = gd.nodes[n_cloud[i]].first_child;
        const uint32_t *m = pl >= 0 ? node_comp(pl, C_MESH, NULL) : NULL;
        if (m) cloud_mat[i] = (int)m[3];
    }
    if (n_jetParticles >= 0) scene_set_active_recursive(n_jetParticles, 0);
    if (n_guard >= 0) enemy_start[0] = nrt[n_guard].lpos;
    if (n_dog >= 0) enemy_start[1] = nrt[n_dog].lpos;
    /* monedas: prefab "coin" */
    int coinroot = node_find_root("coin");
    if (coinroot >= 0) {
        int piv = node_find_child(coinroot, "pivot");
        coin_mesh_node = piv >= 0 ? node_find_child(piv, "coin") : -1;
        int glow = piv >= 0 ? node_find_child(piv, "Glow") : -1;
        coin_glow_node = glow >= 0 ? gd.nodes[glow].first_child : -1;
    }
    /* CharacterController y capsula de colision */
    const uint32_t *c = node_comp(n_character, C_CHARCTRL, NULL);
    memset(&cc, 0, sizeof(cc));
    cc.height = c ? u2f(c[0]) : 9.0f; cc.radius = c ? u2f(c[1]) : 2.0f; cc.slope = c ? u2f(c[2]) : 70.0f;
    cc.step = c ? u2f(c[3]) : 5.0f; cc.skin = c ? u2f(c[4]) : 0.2f;
    cc.center = c ? v3(u2f(c[5]), u2f(c[6]), u2f(c[7])) : v3(0, 4.5f, 0);
    cc.enabled = 1; cc.detect = 1;
    cc_center0 = cc.center; cc_height0 = cc.height;
    const uint32_t *cap = node_comp(n_colChar, C_CAPSULE, NULL);
    col_center0 = cap ? v3(u2f(cap[2]), u2f(cap[3]), u2f(cap[4])) : v3(0, 5.5f, 1);
    col_radius = cap ? u2f(cap[5]) : 3.0f;
    col_height0 = cap ? u2f(cap[6]) : 10.0f;
    col_center = col_center0; col_height = col_height0;
    /* animadores */
    anim_init(&anim_char, n_avatar);
    n_avatars = node_find_child(n_avatar, "avatars");
    char_show_model(ginfo.character);   /* game_load ya ha leido el personaje elegido */
    anim_init(&anim_guard, n_guard);
    anim_init(&anim_dog, n_dog);
    anim_init(&anim_cam, n_camRig);
    AnimState *s;
    if ((s = anim_get(&anim_char, "caught"))) { s->layer = 4; s->enabled = 0; }
    if ((s = anim_get(&anim_char, "caught2"))) { s->layer = 4; s->enabled = 0; }
    anim_add_mixing(&anim_char, "stumbleCornerLeft", n_spine);
    anim_add_mixing(&anim_char, "stumbleCornerRight", n_spine);
    if ((s = anim_get(&anim_char, "stumbleCornerLeft"))) { s->layer = 2; s->weight = 1; }
    if ((s = anim_get(&anim_char, "stumbleCornerRight"))) { s->layer = 2; s->weight = 1; }
    anim_add_mixing(&anim_char, "hold_magnet", n_shoulder);
    if ((s = anim_get(&anim_char, "hold_magnet"))) { s->layer = 3; s->weight = 0.9f; s->enabled = 0; }
    for (int i = 0; i < NKFA; i++) {
        kfa_ev_index[i] = -1;
        int st = anim_find(&anim_char, kfa[i].clip);
        if (st < 0) continue;
        float rate = gd.clips[anim_char.st[st].clip].sample_rate;
        if (rate <= 0) rate = 30;
        kfa_ev_index[i] = anim_char.nev;
        anim_add_event(&anim_char, kfa[i].clip, kfa[i].frame / rate);
    }
    anim_char.on_event = char_anim_event;
    anim_add_event(&anim_guard, "catch", 0);
    anim_add_event(&anim_guard, "catch2", 0);
    anim_guard.on_event = guard_anim_event;
    anim_add_event(&anim_cam, "startPan", 0);
    anim_cam.on_event = cam_anim_event;
    track_init();
    track_tutorial = !tutorial_completed;
    track_restart();
    character_restart();
    game_top_menu();
    music_play("Hr_music_newFunky", 1);
    music_set_volume(0.25f);
}

/* ------------------------------------------------------------ personajes (CharacterModels.cs, PlayerInfo.cs) */
/* fichas por tipo (CharacterModels.TokenType); a los de monedas se les "dan" sus fichas al comprarlos, como en
 * PlayerInfo: un personaje esta desbloqueado cuando sus fichas llegan al precio */
enum { TK_JAKE, TK_TRICKY, TK_FRESH, TK_SPIKE, TK_YUTANI, TK_FRANK, TK_FRIZZY, TK_KING, TK_LUCY, TK_NINJA, TK_COUNT };
typedef struct { const char *model, *name; int unlock, token, price; const char *tok_es, *tok_en, *tok_sprite; } CharDef;
static const CharDef chars[] = {
    {"slick", "Jake", UL_FREE, TK_JAKE, 0, "", "", NULL},
    {"tricky", "Tricky", UL_TOKENS, TK_TRICKY, 3, "Gorra de Tricky", "Tricky's Hat", "trophy_tricky_token"},
    {"fresh", "Fresh", UL_TOKENS, TK_FRESH, 50, "Radio de Fresh", "Fresh's Stereo", "trophy_fresh_token"},
    {"spike", "Spike", UL_TOKENS, TK_SPIKE, 200, "Guitarra de Spike", "Spike's Guitar", "trophy_spike_token"},
    {"yutani", "Yutani", UL_TOKENS, TK_YUTANI, 500, "OVNI de Yutani", "Yutani's UFO", "trophy_yutani_token"},
    {"lucy", "Lucy", UL_COINS, TK_LUCY, 7000, "", "", NULL},
    {"ninja", "Ninja", UL_COINS, TK_NINJA, 20000, "", "", NULL},
    {"frank", "Frank", UL_COINS, TK_FRANK, 40000, "", "", NULL},
    {"king", "King", UL_COINS, TK_KING, 80000, "", "", NULL},
    {"frizzy", "Frizzy", UL_COINS, TK_FRIZZY, 150000, "", "", NULL},
};
#define NCHARS ((int)(sizeof(chars) / sizeof(chars[0])))
static int char_tokens[TK_COUNT];

int char_count(void) { return NCHARS; }
const char *char_name(int i) { return chars[i].name; }
int char_unlock_type(int i) { return chars[i].unlock; }
int char_price(int i) { return chars[i].price; }
int char_have(int i) { return char_tokens[chars[i].token]; }
int char_unlocked(int i) { return chars[i].unlock == UL_FREE || char_tokens[chars[i].token] >= chars[i].price; }
const char *char_token_name(int i) { return TR(chars[i].tok_es, chars[i].tok_en); }
const char *char_token_sprite(int i) { return chars[i].tok_sprite; }

/* CharacterModel.ChangeCharacterModel: todos los modelos comparten el esqueleto; se ve solo el elegido.
 * El spray solo lo lleva Jake (HideAllPowerups) */
static void char_show_model(int i)
{
    if (i < 0 || i >= NCHARS) i = 0;
    if (n_avatars >= 0)
        for (int c = gd.nodes[n_avatars].first_child; c >= 0; c = gd.nodes[c].next_sibling)
            nrt[c].rend = !strcmp(gd.strings[gd.nodes[c].name], chars[i].model);
    if (n_sprayCan >= 0 && (state == GS_MENU || state == GS_CHARS || state == GS_SHOP)) nrt[n_sprayCan].rend = i == 0;
}

/* MysteryBox.Roll: ficha de un personaje que aun la necesite; -1 si ya no queda ninguno */
static int char_useful_token(void)
{
    int opts[4], n = 0;
    for (int i = 0; i < NCHARS; i++)
        if (chars[i].unlock == UL_TOKENS && char_tokens[chars[i].token] < chars[i].price) opts[n++] = i;
    return n ? opts[rnd_range(0, n)] : -1;
}

static void open_shop(void) { ginfo.shop_sel = 0; change_state(GS_SHOP); ginfo.state = GS_SHOP; }
static void open_chars(void)
{
    ginfo.char_sel = ginfo.character;
    char_show_model(ginfo.char_sel);
    change_state(GS_CHARS); ginfo.state = GS_CHARS;
}

static void chars_input(unsigned in, int confirm)
{
    if (in & IN_UP) { ginfo.char_sel = (ginfo.char_sel + NCHARS - 1) % NCHARS; char_show_model(ginfo.char_sel); snd_play(sfx_tap, 0.5f, 1, 0); }
    if (in & IN_DOWN) { ginfo.char_sel = (ginfo.char_sel + 1) % NCHARS; char_show_model(ginfo.char_sel); snd_play(sfx_tap, 0.5f, 1, 0); }
    int i = ginfo.char_sel;
    if (confirm) {
        if (char_unlocked(i)) {
            ginfo.character = i;
            snd_play(sfx_find("Hr_gui_coin"), 0.6f, 1.2f, 0);
            game_save();
        } else if (chars[i].unlock == UL_COINS && ginfo.total_coins >= chars[i].price) {
            /* UICharacterBuyButton: se paga y queda elegido */
            ginfo.total_coins -= chars[i].price;
            missions_did(M_SPENDCOIN, chars[i].price);
            char_tokens[chars[i].token] = chars[i].price;
            ginfo.character = i;
            snd_play(sfx_find("Hr_gui_cash"), 0.8f, 1.0f, 0);
            game_save();
        } else snd_play(sfx_find("Hr_stumble_side"), 0.5f, 1.2f, 0);
    }
    if (in & IN_R) {
        snd_play(sfx_tap, 1, 1, 0);
        char_show_model(ginfo.character);
        open_shop();
    } else if (in & (IN_CIRCLE | IN_L | IN_START)) {
        snd_play(sfx_tap, 1, 1, 0);
        change_state(GS_MENU); ginfo.state = GS_MENU;
        char_show_model(ginfo.character);
    }
}

/* ------------------------------------------------------------ trofeos (Trophies.cs) */
static const struct { const char *name, *name_en, *desc, *desc_en, *sprite; } trophies[NTROPHIES] = {
    {"Gema", "Gemstone", "Su sitio esta en un museo!", "It belongs in a museum!", "trophy_diamond"},
    {"Lingote de oro", "Gold Bar", "Directo de Fort Knox.", "Directly from Fort Knox.", "trophy_gold"},
    {"Reloj con clase", "Classy Clock", "Luce estilo como Flavor Flav!", "Show your style like Flavor Flav!", "trophy_goldChainClock"},
    {"Cadena de dolar", "Dollar Chain", "Pon brillo en la fiesta.", "Bring some bling to the party.", "trophy_goldChainDollar"},
    {"El pisapapeles", "The Paperweight", "Un pequeno Gold Sapiens.", "A little Gold Sapiens.", "trophy_goldSkull"},
    {"Auriculares", "Headphones", "El ritmo esta en el corazon.", "Groove is in the heart.", "trophy_headphones"},
    {"Disco de platino", "Platinum Record", "Que vuelva el ritmo!", "Bring the beat back!", "trophy_lpBlack"},
    {"Cinta de casete", "Cassette Tape", "Musica retro.", "Retro musicology.", "trophy_tapeBlack"},
};
static int trophies_mask;

int trophy_unlocked(int i) { return i >= 0 && i < NTROPHIES && (trophies_mask >> i & 1); }
int trophies_collected(void) { int n = 0; for (int i = 0; i < NTROPHIES; i++) n += trophy_unlocked(i); return n; }
const char *trophy_name(int i) { return TR(trophies[i].name, trophies[i].name_en); }
const char *trophy_desc(int i) { return TR(trophies[i].desc, trophies[i].desc_en); }
const char *trophy_sprite(int i) { return trophy_unlocked(i) ? trophies[i].sprite : "trophy_locked"; }

/* MysteryBox.Roll: un trofeo que aun no se tenga, al azar; -1 si ya estan todos */
static int trophy_roll(void)
{
    int left[NTROPHIES], n = 0;
    for (int i = 0; i < NTROPHIES; i++) if (!trophy_unlocked(i)) left[n++] = i;
    return n ? left[rnd_range(0, n)] : -1;
}

/* ------------------------------------------------------------ tienda (Upgrades.cs) */
enum { SK_AMOUNT, SK_TIER, SK_BOX, SK_SKIP };
const ShopItem shop_items[] = {
    /* nombres en ingles: los de Upgrades.cs del original */
    {"Hoverboard", "Hoverboard", "icon_upgrades_hoverboard", SK_AMOUNT, 0},
    {"Headstart 250", "Headstart 250", "icon_upgrades_headstart500", SK_AMOUNT, 1},
    {"Headstart 1000", "Headstart 1000", "icon_upgrades_headstart2000", SK_AMOUNT, 2},
    {"Caja misteriosa", "Mystery Box", "icon_upgrades_mysteryBox", SK_BOX, 0},
    {"Jetpack", "Jetpack", "icon_upgrades_jetpack", SK_TIER, PW_JETPACK},
    {"Super Sneakers", "Super Sneakers", "icon_upgrades_superSneakers", SK_TIER, PW_SNEAKERS},
    {"Coin Magnet", "Coin Magnet", "icon_upgrades_coinMagnet", SK_TIER, PW_MAGNET},
    {"2x Multiplier", "2x Multiplier", "icon_upgrades_doubleScore", SK_TIER, PW_DOUBLE},
    {"Saltar mision 1", "Skip Mission 1", "icon_upgrades_skipMission1", SK_SKIP, 0},
    {"Saltar mision 2", "Skip Mission 2", "icon_upgrades_skipMission2", SK_SKIP, 1},
    {"Saltar mision 3", "Skip Mission 3", "icon_upgrades_skipMission3", SK_SKIP, 2},
};
const int shop_count = sizeof(shop_items) / sizeof(shop_items[0]);
static const int tier_prices[6] = {0, 500, 1500, 3000, 10000, 30000};

int shop_price(int i)
{
    const ShopItem *it = &shop_items[i];
    if (it->kind == SK_BOX) return 500;
    if (it->kind == SK_SKIP) return missions_done(it->idx) ? -1 : missions_skip_price();
    if (it->kind == SK_AMOUNT) return it->idx == 0 ? 300 : it->idx == 1 ? 400 : 2000;
    int t = upgrade_tier[it->idx];
    return t >= 5 ? -1 : tier_prices[t + 1];
}

int shop_amount(int i)
{
    const ShopItem *it = &shop_items[i];
    if (it->kind == SK_AMOUNT) return it->idx == 0 ? ginfo.hoverboards : it->idx == 1 ? ginfo.hs500 : ginfo.hs2000;
    if (it->kind == SK_TIER) return upgrade_tier[it->idx];
    if (it->kind == SK_SKIP) return missions_done(it->idx);
    return ginfo.boxes_pending;
}

/* MysteryBox.Roll() (los personajes y trofeos del original se convierten en monedas) */
static void mystery_open(void)
{
    if (ginfo.boxes_pending <= 0) return;
    ginfo.boxes_pending--;
    ginfo.boxes_opened++;
    int coins = 0, hb = 0, h5 = 0, h20 = 0, tok = 0, trophy = -1;
    if (ginfo.boxes_opened == 1) tok = 1;
    else if (ginfo.boxes_opened == 2) h5 = 1;
    else if (ginfo.boxes_opened == 3) coins = 600;
    else {
        float r = rnd01();
        if (r < 0.40f) tok = 1;
        else if (r < 0.45f) {
            trophy = trophy_roll();
            if (trophy < 0) coins = 100;   /* ya estan todos: 100 monedas, como el original */
        }
        else if (r < 0.69f) coins = rnd_range(200, 300);
        else if (r < 0.73f) h5 = 1;
        else if (r < 0.80f) coins = 500;
        else if (r < 0.87f) hb = 2;
        else if (r < 0.92f) coins = 1000;
        else if (r < 0.95f) coins = 1500;
        else if (r < 0.96f) hb = rnd_range(0, 7) + 3;
        else if (r < 0.97f) h20 = 1;
        else if (r < 0.98f) coins = 2500;
        else if (r < 0.988f) hb = 10;
        else if (r < 0.993f) coins = 5000;
        else if (r < 0.998f) h20 = 3;
        else coins = 10000;
    }
    int tc = -1;
    if (tok) {
        /* ficha de un personaje que aun la necesite; si ya no queda ninguno, 100 monedas (como el original) */
        tc = char_useful_token();
        if (tc < 0) { tok = 0; coins = 100; }
        else char_tokens[chars[tc].token]++;
    }
    ginfo.total_coins += coins; ginfo.hoverboards += hb; ginfo.hs500 += h5; ginfo.hs2000 += h20; ginfo.tokens += tok;
    if (coins) missions_did(M_EARNCOIN, coins);
    if (tok) missions_did(M_TOKENS, 1);
    if (trophy >= 0) {
        trophies_mask |= 1 << trophy;
        dlog("TROFEO %s (%d de %d)", trophies[trophy].name, trophies_collected(), NTROPHIES);
        snprintf(ginfo.reward, sizeof(ginfo.reward), TR("Trofeo: %s", "Trophy: %s"), trophy_name(trophy));
    } else if (tok) snprintf(ginfo.reward, sizeof(ginfo.reward), "1x %s", char_token_name(tc));
    else if (coins) snprintf(ginfo.reward, sizeof(ginfo.reward), TR("%d monedas", "%d coins"), coins);
    else if (hb) snprintf(ginfo.reward, sizeof(ginfo.reward), "%d hoverboard%s", hb, hb > 1 ? "s" : "");
    else if (h5) snprintf(ginfo.reward, sizeof(ginfo.reward), "Headstart 250");
    else snprintf(ginfo.reward, sizeof(ginfo.reward), "%d Headstart 1000", h20);
    ginfo.reward_t = game_time;
    snd_play(sfx_find("Hr_powerUp"), 0.8f, 1.0f, 0);
    game_save();
}

static void shop_buy(int i)
{
    int price = shop_price(i);
    if (price < 0 || ginfo.total_coins < price) { snd_play(sfx_find("Hr_stumble_side"), 0.5f, 1.2f, 0); return; }
    ginfo.total_coins -= price;
    missions_did(M_SPENDCOIN, price);
    const ShopItem *it = &shop_items[i];
    if (it->kind == SK_AMOUNT) {
        if (it->idx == 0) ginfo.hoverboards++;
        else if (it->idx == 1) ginfo.hs500++;
        else ginfo.hs2000++;
    } else if (it->kind == SK_TIER) upgrade_tier[it->idx]++;
    else if (it->kind == SK_SKIP) missions_skip(it->idx);
    else { ginfo.boxes_pending++; missions_did(M_BUYMYSTERYBOX, 1); mystery_open(); }
    snd_play(sfx_find("Hr_gui_coin"), 0.8f, 1.0f, 0);
    game_save();
}

/* ------------------------------------------------------------ entradas */
static void use_headstart(int big)
{
    if (state != GS_RUNNING || game_time > ginfo.headstart_until) return;
    if (big ? ginfo.hs2000 <= 0 : ginfo.hs500 <= 0) return;
    if (big) ginfo.hs2000--; else ginfo.hs500--;
    ginfo.headstart_until = 0;
    /* Game.StartHeadStart500/2000: distancia = duracion (metros) * 8, velocidad 1000 */
    start_jetpack(1, (big ? 1000.0f : 250.0f) * DISTANCE_PER_METER);
    missions_did(M_HEADSTART, 1);
    game_save();
}

void game_input(unsigned in)
{
    int confirm = (in & IN_CROSS) != 0;
    if ((in & IN_START) && (state == GS_RUNNING || state == GS_JETPACK || state == GS_INTRO)) {
        if (!ginfo.paused) {
            ginfo.paused = 1;
            snd_pause_all(1);
            music_pause(1);
        } else if (ginfo.countdown <= 0) ginfo.countdown = 3.0f;   /* "Starting in" 3, 2, 1 */
        return;
    }
    if (ginfo.paused) {
        if (confirm && ginfo.countdown <= 0) ginfo.countdown = 3.0f;
        return;
    }
    switch (state) {
    case GS_MENU:
        /* menu: X jugar, triangulo misiones, L personajes y R tienda (como pestanas a izquierda y derecha) */
        if (ginfo.missions_open) {
            if (in & (IN_TRIANGLE | IN_CROSS | IN_CIRCLE | IN_START)) { snd_play(sfx_tap, 1, 1, 0); ginfo.missions_open = 0; }
            break;
        }
        if (in & IN_TRIANGLE) { snd_play(sfx_tap, 1, 1, 0); ginfo.missions_open = 1; }
        else if (confirm || (in & IN_START)) { snd_play(sfx_tap, 1, 1, 0); game_start_run(); }
        else if (in & IN_R) { snd_play(sfx_tap, 1, 1, 0); open_shop(); }
        else if (in & IN_L) { snd_play(sfx_tap, 1, 1, 0); open_chars(); }
        else if (in & IN_SQUARE) { snd_play(sfx_tap, 1, 1, 0); ginfo.trophy_sel = 0; change_state(GS_TROPHIES); ginfo.state = GS_TROPHIES; }
        break;
    case GS_CHARS:
        chars_input(in, confirm);
        break;
    case GS_TROPHIES:
        /* rejilla de 4 x 2 */
        if (in & IN_LEFT) ginfo.trophy_sel = (ginfo.trophy_sel + NTROPHIES - 1) % NTROPHIES;
        if (in & IN_RIGHT) ginfo.trophy_sel = (ginfo.trophy_sel + 1) % NTROPHIES;
        if ((in & (IN_UP | IN_DOWN)) && !(in & (IN_CROSS | IN_CIRCLE))) ginfo.trophy_sel = (ginfo.trophy_sel + 4) % NTROPHIES;
        if ((in & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN)) && !(in & (IN_CROSS | IN_CIRCLE))) snd_play(sfx_tap, 0.5f, 1, 0);
        if (in & (IN_CIRCLE | IN_SQUARE | IN_CROSS | IN_START)) { snd_play(sfx_tap, 1, 1, 0); change_state(GS_MENU); ginfo.state = GS_MENU; }
        break;
    case GS_SHOP:
        if ((in & IN_UP) && !(in & IN_CROSS)) ginfo.shop_sel = (ginfo.shop_sel + shop_count - 1) % shop_count;
        if ((in & IN_DOWN) && !(in & IN_CIRCLE)) ginfo.shop_sel = (ginfo.shop_sel + 1) % shop_count;
        if (confirm) shop_buy(ginfo.shop_sel);
        if (in & IN_L) { snd_play(sfx_tap, 1, 1, 0); open_chars(); }
        else if (in & (IN_CIRCLE | IN_R | IN_START)) { snd_play(sfx_tap, 1, 1, 0); change_state(GS_MENU); ginfo.state = GS_MENU; }
        break;
    case GS_GAMEOVER:
        if (confirm || (in & IN_START)) {
            if (ginfo.boxes_pending > 0) mystery_open();
            else { snd_play(sfx_tap, 1, 1, 0); game_top_menu(); }
        }
        break;
    case GS_DIE:
        if (in & (IN_UP | IN_CROSS)) in_up = 1;
        break;
    case GS_INTRO:
    case GS_CHECKPOINT:
        /* como en el original, mientras la intro o la vuelta al punto de control no se puede mover: si se guardara
         * la pulsacion, el personaje saltaria, rodaria o cambiaria de via nada mas empezar a correr */
        break;
    default:
        if (in & IN_L) use_headstart(0);
        if (in & IN_R) use_headstart(1);
        in_left |= (in & IN_LEFT) != 0;
        in_right |= (in & IN_RIGHT) != 0;
        in_up |= (in & IN_UP) != 0;
        in_down |= (in & IN_DOWN) != 0;
        in_board |= (in & IN_BOARD) != 0;
        break;
    }
}

/* ------------------------------------------------------------ bucle */
static int extra_cols[8];
/* solo los tramos cerca del personaje entran en la fisica (el piloto automatico mira mas lejos) */
#define PHYS_BEHIND 80.0f
#ifdef AUTOTEST
#define PHYS_AHEAD 450.0f
#else
#define PHYS_AHEAD 250.0f
#endif

void game_update(float dt)
{
    if (ginfo.paused) {
        if (ginfo.countdown > 0) {
            ginfo.countdown -= dt;
            if (ginfo.countdown <= 0) { ginfo.countdown = 0; ginfo.paused = 0; snd_pause_all(0); music_pause(0); }
        }
        return;
    }
    if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
    game_time += dt;
    scene_char_z = ch.z;
    float t = game_time - start_time;
    currentLevelSpeed = Speed(t);
    /* colisiones de los tramos activos y del entorno inicial */
    extra_cols[0] = n_intro_env;
    DBG_PHASE(main, "game_update: phys_build");
    phys_build(extra_cols, 1, ch.z - PHYS_BEHIND, ch.z + PHYS_AHEAD);
    DBG_PHASE(main, "game_update: running_tick (movimiento y colisiones)");
    switch (state) {
    case GS_INTRO: intro_tick(); break;
    case GS_RUNNING: running_tick(dt); break;
    case GS_JETPACK: jetpack_tick(dt); break;
    case GS_DIEFALL: case GS_DIE: die_tick(dt); break;
    case GS_CHECKPOINT: checkpoint_tick(dt); break;
    default: break;
    }
    DBG_PHASE(main, "game_update: power-ups");
    if (state == GS_RUNNING || state == GS_JETPACK) mods_update(dt);
    update_powerup_times(dt);
    /* corrutinas del personaje */
    track_change_tick();
    DBG_PHASE(main, "game_update: roll_tick (rodar)");
    if (state == GS_RUNNING || state == GS_JETPACK || state == GS_DIEFALL) roll_tick();
    DBG_PHASE(main, "game_update: personaje");
    if (ch.stumble && game_time >= ch.stumble_end) { ch.stumble = 0; guard_reset_catch_up(); }
    if (stumble_pending && game_time >= stumble_pending_t) {
        stumble_pending = 0;
#ifdef AT_GOD
        if (0) {
#else
        if (state == GS_RUNNING) {
#endif
            dlog("MUERTE por tropezar dos veces seguidas: te atrapa el inspector a %d m", ginfo.meters);
            anim_crossfade(&anim_char, "stumbleFall", 0.2f);
            game_die();
        }
    }
    if (ch.hbt == 1) {
        float e = clamp01((game_time - ch.hbt_start) / 0.5f);
        V3 tgt = v3(ch.hbt_cam.x, ch.hbt_cam.y - 33.0f, ch.hbt_from.z);
        char_set_pos(v3lerp(ch.hbt_from, tgt, e));
    } else if (ch.hbt == 2) {
        float e = clamp01(game_time - ch.hbt_start);
        char_set_pos(v3lerp(ch.hbt_from, v3(ch.hbt_from.x, -5.0f, ch.hbt_from.z), e));
    }
    /* posicion del personaje */
    if (cc.pos.y < 0 && state != GS_MENU) cc.pos.y = 1.0f;
    nrt[n_character].lpos = cc.pos;
    set_node_active(n_shadow, ch.shadow_on);
    scene_update_world(n_character);
    /* objetos moviles, pickups y monedas */
    DBG_PHASE(main, "game_update: trenes, power-ups y monedas");
    moving_update(ch.z, dt, currentSpeed);
    pickups_update(game_time);
    coins_update(dt);
    if (state == GS_RUNNING || state == GS_JETPACK) {
        phys_build(extra_cols, 1, ch.z - PHYS_BEHIND, ch.z + PHYS_AHEAD);
        DBG_PHASE(main, "game_update: check_triggers");
        check_triggers();
        DBG_PHASE(main, "game_update: coins_tick (monedas e iman)");
        coins_tick(dt);
    }
    DBG_PHASE(main, "game_update: calculate_score");
    calculate_score();
    /* animaciones */
    DBG_PHASE(main, "game_update: animaciones");
    anim_update(&anim_char, dt);
    DBG_PHASE(main, "game_update: anim_update guardia/perro/camara");
    anim_update(&anim_guard, dt);
    anim_update(&anim_dog, dt);
    anim_update(&anim_cam, dt);
    DBG_PHASE(main, "game_update: anim_apply Jake");
    anim_apply(&anim_char);
    DBG_PHASE(main, "game_update: anim_apply guardia/perro");
    anim_apply(&anim_guard);
    anim_apply(&anim_dog);
    DBG_PHASE(main, "game_update: despues de las animaciones");
    if (!cam_follow) anim_apply(&anim_cam);
    scene_update_world(n_character);
    guard_late_update(dt);
    scene_update_world(n_camRig);
    jet_clouds_update();
    /* PointConstraint de la sombra: debajo de jRoot, a la altura de los pies (+0.3 para no pelear con el suelo) */
    if (n_shadow >= 0 && n_jroot >= 0) {
        V3 jr = node_pos(n_jroot), cm = node_pos(n_charModel);
        nrt[n_shadow].lpos = v3(jr.x - cm.x, 0.3f, jr.z - cm.z);
        scene_update_world(n_shadow);
    }
    camera_shake_tick(dt);
    /* datos para la interfaz */
    ginfo.score = score; ginfo.coins = coins_run; ginfo.meters = meters;
    ginfo.multiplier = score_multiplier();
    ginfo.time = game_time;
    for (int i = 0; i < PT_COUNT && i < 8; i++) {
        ginfo.pw_left[i] = pw_on[i] ? pw_left[i] : 0;
        ginfo.pw_total[i] = i == PT_HOVERBOARD ? 30.0f : powerup_duration(i == PT_JETPACK ? PW_JETPACK : i == PT_SNEAKERS ? PW_SNEAKERS :
                                                                      i == PT_MAGNET ? PW_MAGNET : PW_DOUBLE);
    }
}

void game_draw(void)
{
    /* camara */
#ifdef AT_CAMLOG
    {
        static int fr;
        fr++;
        if (state == GS_INTRO || (state == GS_RUNNING && game_time - state_time < 0.5f)) {
            V3 mp = n_mainCam >= 0 ? node_pos(n_mainCam) : v3(0, 0, 0);
            dlog("CAM f%d st%d follow%d t%.3f pos %.1f %.1f %.1f tgt %.1f %.1f %.1f main %.1f %.1f %.1f", fr, state, cam_follow,
                 game_time - state_time, cam_pos.x, cam_pos.y, cam_pos.z, cam_target.x, cam_target.y, cam_target.z, mp.x, mp.y, mp.z);
        }
    }
#endif
    if (cam_follow) {
        r_set_camera(v3add(cam_pos, cam_shake), v3add(cam_target, cam_shake), 60.0f);
    } else if (n_mainCam >= 0) {
        r_set_camera_matrix(&nrt[n_mainCam].world, 60.0f);
    }
    /* cronometro por bloques para el devlog (pocas lecturas del reloj por fotograma) */
    unsigned int tsec = sceKernelGetSystemTimeLow(), tnow;
#define SECT(i) (tnow = sceKernelGetSystemTimeLow(), dbg_sect_ms[i] = (tnow - tsec) / 1000.0f, tsec = tnow, \
                 DBG_PHASE(main, sect_phase[i]))
    static const char *const sect_phase[7] = {"game_draw: cielo", "game_draw: via", "game_draw: entorno del menu",
                                              "game_draw: monedas", "game_draw: personajes (Jake, inspector, perro)",
                                              "game_draw: efectos y transparentes", "game_draw: fin"};
    SECT(0);   /* camara */
    /* el cielo va pegado a la camara: se dibuja primero */
#ifdef AT_CRACKS
    if (0) {   /* prueba de grietas: sin cielo, el fondo magenta se ve por cualquier hueco */
#else
    if (n_sky >= 0) {
#endif
        if (cam_follow) {
            /* con CharacterCamera el cielo sigue a la camara */
            M34 cw; m34_inverse(&cw, &r_view);
            M34 local; m34_trs(&local, nrt[n_sky].lpos, nrt[n_sky].lrot, nrt[n_sky].lscl);
            m34_mul(&nrt[n_sky].world, &cw, &local);
        }
        scene_draw_subtree(n_sky);
    }
    SECT(1);
    if (state != GS_MENU) for (int i = 0; i < track_nactive; i++) {
        extern int track_chunk_node(int ci);
        scene_draw_subtree(track_chunk_node(track_active[i]));
    }
    SECT(2);
    if (n_intro_env >= 0) scene_draw_subtree(n_intro_env);
    SECT(3);
    coins_draw();
    SECT(4);
    scene_draw_subtree(n_character);
    if (gu.showing || state == GS_DIE || state == GS_DIEFALL) scene_draw_subtree(n_enemies);
    SECT(5);
    if (n_efx >= 0) scene_draw_subtree(n_efx);
    tutorial_draw_arrow();
    r_flush_transparent();
    SECT(6);
#undef SECT
}

/* ------------------------------------------------------------ partida guardada */
#define SAVE_MAGIC 0x53535033   /* v3: + fichas por personaje (v2 sin ellas se sigue leyendo) */
void game_save(void)
{
    FILE *f = fopen("save.dat", "wb");
    if (!f) return;
    int d[32] = {SAVE_MAGIC, ginfo.highscore, ginfo.total_coins, ginfo.hoverboards, ginfo.hs500, ginfo.hs2000,
                 ginfo.boxes_pending, ginfo.boxes_opened, upgrade_tier[PW_JETPACK], upgrade_tier[PW_SNEAKERS],
                 upgrade_tier[PW_MAGNET], upgrade_tier[PW_DOUBLE], tutorial_completed, mission_set, ginfo.tokens,
                 mission_progress[0], mission_progress[1], mission_progress[2], ginfo.character};
    for (int i = 0; i < TK_COUNT; i++) d[20 + i] = char_tokens[i];
    d[30] = trophies_mask;
    fwrite(d, sizeof(d), 1, f);
    fclose(f);
}

void game_load(void)
{
    FILE *f = fopen("save.dat", "rb");
    if (!f) return;
    int d[32];
    memset(d, 0, sizeof(d));
    int n = fread(d, 4, 32, f);
    fclose(f);
    if (n >= 4 && d[0] == 0x53535031) { ginfo.highscore = d[1]; ginfo.total_coins = d[2]; ginfo.hoverboards = d[3]; tutorial_completed = 1; return; }
    int v2 = d[0] == 0x53535032;
    if (n < 13 || (d[0] != SAVE_MAGIC && !v2)) return;
    ginfo.highscore = d[1]; ginfo.total_coins = d[2]; ginfo.hoverboards = d[3]; ginfo.hs500 = d[4]; ginfo.hs2000 = d[5];
    ginfo.boxes_pending = d[6]; ginfo.boxes_opened = d[7];
    upgrade_tier[PW_JETPACK] = d[8]; upgrade_tier[PW_SNEAKERS] = d[9]; upgrade_tier[PW_MAGNET] = d[10]; upgrade_tier[PW_DOUBLE] = d[11];
    tutorial_completed = v2 ? 1 : d[12];   /* quien ya jugo con la version anterior no repite el tutorial */
    if (n >= 20) {
        mission_set = d[13]; ginfo.tokens = d[14];
        mission_progress[0] = d[15]; mission_progress[1] = d[16]; mission_progress[2] = d[17]; ginfo.character = d[18];
    }
    if (!v2 && n >= 20 + TK_COUNT) for (int i = 0; i < TK_COUNT; i++) char_tokens[i] = d[20 + i];
    else if (v2) char_tokens[TK_TRICKY] = ginfo.tokens;   /* las fichas sin tipo de la v2 van a Tricky */
    if (!v2 && n >= 31) trophies_mask = d[30] & ((1 << NTROPHIES) - 1);
    /* como CharacterModel.Awake: si el elegido no esta desbloqueado, se vuelve a Jake */
    if (ginfo.character < 0 || ginfo.character >= NCHARS || !char_unlocked(ginfo.character)) ginfo.character = 0;
}

void scene_on_chunk_laid(int chunk) { (void)chunk; }

#ifdef AUTOTEST
/* para la prueba automatica: abrir n cajas misteriosas de golpe */
void game_debug_boxes(int n)
{
    for (int i = 0; i < n; i++) { ginfo.boxes_pending++; mystery_open(); }
    dlog("PRUEBA %d cajas abiertas: %d trofeos, monedas %d", n, trophies_collected(), ginfo.total_coins);
}

/* para la prueba automatica: activar power-ups a voluntad */
void game_debug_powerup(int k)
{
    if (state != GS_RUNNING) return;
    switch (k) {
    case 0: mod_add(MOD_MAGNET); break;
    case 1: mod_add(MOD_SNEAKERS); break;
    case 2: mod_add(MOD_DOUBLE); break;
    case 3: start_jetpack(0, 0); break;
    case 4: if (ginfo.hoverboards < 1) ginfo.hoverboards = 1; in_board = 1; break;
    }
}
#endif
