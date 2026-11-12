/* spawn.c - SpawnPointManager.cs + Upgrades.cs */
#include <float.h>
#include "spawn.h"
#include "scene.h"
#include "devlog.h"

int upgrade_tier[PW_COUNT];

typedef struct { float prob, dist_min, spawn_z; int field; } PickupType;

/* orden de SpawnPoint: dailyLetter, doubleScoreMultiplier, jetpackPickup, jumpBooster, magnetBooster, mysteryBox
 * spawnProbability / minimumMeters de Upgrades.cs de la 1.9 (de la 1.4 a la 1.8: 2x 32, iman 32, zapatillas 22 y
 * caja 20; el port usaba los de la 1.0). Las letras van aparte: SpawnPointManager.CheckLetterSpawnRate */
/* letras: el peso de la 1.0 (15), mezcladas con los power-ups. Desde la 1.4 el original les daba 1.5e9 mientras quedara
 * alguna por coger (CheckLetterSpawnRate): salia letra en cada hueco y no habia power-ups hasta completar la palabra.
 * Se queda la regla de la 1.4 de no sacar letras con la palabra ya completa */
#define LETTER_PROB 15.0f
#define NPK 7
static PickupType pk[NPK] = {
    {LETTER_PROB, 0.0f, 0, 0},                 /* letras: spawnDistanceMin queda en 0 (bug del original) */
    {29.0f, 0.0f * DISTANCE_PER_METER, 0, 1},  /* 2x */
    {14.0f, 1000.0f * DISTANCE_PER_METER, 0, 2},/* jetpack: minimumMeters 1000 */
    {26.0f, 0.0f, 0, 3},                       /* super zapatillas */
    {29.0f, 0.0f, 0, 4},                       /* iman */
    {12.0f, 99999.0f * DISTANCE_PER_METER, 0, 5},/* caja misteriosa */
    {0.0f, 0.0f, 0, 6}                         /* llave (1.9): 2 con Save Me (getKeySpawnProbability), si no 0 */
};
static float spawn_z, spawn_spacing;
/* SpawnPointEgg.lastSpawn (static, a 0 en ResetSpawn al empezar cada carrera) */
static float egg_last;

/* UpgradeFirstSpawnMeters = 250, UpgradeSpawnSpacingMeters = 300 */
void spawn_restart(void)
{
#ifdef AT_LETTERS
    pk[0].prob = 1000.0f;   /* prueba: casi siempre letras */
#endif
    spawn_z = 250.0f * DISTANCE_PER_METER;
    spawn_spacing = 300.0f * DISTANCE_PER_METER;
#ifdef AT_PKTEST
    /* prueba: power-ups muy seguidos y sin letras (el personaje va a por ellos) */
    pk[0].prob = 0; pk[2].dist_min = 0; pk[5].dist_min = 0; pk[5].prob = 25.0f;
    spawn_z = 40.0f * DISTANCE_PER_METER;
    spawn_spacing = 50.0f * DISTANCE_PER_METER;
#endif
    for (int i = 0; i < NPK; i++) pk[i].spawn_z = -FLT_MAX;
    egg_last = 0;
}

/* SpawnPointEgg.PerformSelection: un huevo si desde el ultimo han pasado al menos 80 s de carrera (distancia entre
 * la velocidad del nivel) y la busqueda sigue (temporada, menos de 100 y sin Chicky). En los mapas sin huevos propios
 * el huevo va de hijo de cada SpawnPoint y solo sale si alli no ha salido ningun power-up */
static float doll_last = -1000.0f;   /* SpawnPointEgg.lastSpawn de la 1.10 (static: no se reinicia entre carreras) */
void spawn_egg_selection(int node, int chosen, int *visit, int *nv, int max)
{
    int nw = 0;
    const uint32_t *c = node_comp(node, C_SPAWNEGG, &nw);
    if (!c) return;
    int ge = (int)c[0], gdl = nw >= 2 ? (int)c[1] : -1;   /* [huevo, muneca] */
    int kind = game_hunt_kind();
    int put_egg = 0, put_doll = 0;
    if (kind == 1 && ge >= 0) {
        float z = node_pos(node).z;
        float sp = game_level_speed();
#if defined(AT_EGGSHOW)
        const float egg_gap = 0.0f;   /* prueba: un huevo en cada punto libre, para verlos */
#elif defined(AT_EGGS)
        const float egg_gap = 3.0f;   /* prueba: huevos muy seguidos */
#else
        const float egg_gap = 80.0f;
#endif
        if (chosen < 0 && sp > 0 && (z - egg_last) / sp >= egg_gap) {
            put_egg = 1;
            egg_last = z;
#ifdef AT_EGGS
            dlog("HUEVO sale en z=%.0f", z);
#endif
        }
    } else if (kind == 2 && gdl >= 0 && chosen < 0) {
        /* SpawnPointEgg.PerformSelection de la 1.10: nunca antes de 5 s desde la anterior, seguro pasados 240 s y si no
         * un 4 % en cada punto */
        float t = game_hunt_time();
        int flag;
#if defined(AT_EGGSHOW)
        flag = 1;
#else
        if (t < doll_last + 5.0f) flag = 0;
        else if (t > doll_last + 240.0f) flag = 1;
        else flag = rnd01() <= 0.04f;
#endif
        if (flag) { put_doll = 1; doll_last = t; }
    }
    if (ge >= 0) { if (put_egg) { if (*nv < max) visit[(*nv)++] = ge; } else scene_set_active_recursive(ge, 0); }
    if (gdl >= 0) { if (put_doll) { if (*nv < max) visit[(*nv)++] = gdl; } else scene_set_active_recursive(gdl, 0); }
}

void spawn_perform_selection(int sp, int *visit, int *nv, int max)
{
    int nw = 0;
    const uint32_t *c = node_comp(sp, C_SPAWNPOINT, &nw);
    if (!c) return;
    float z = node_pos(sp).z;
    /* la llave solo en los puntos que la traen (mapa de la 1.9) y con Save Me activo */
#ifdef AT_KEYSHOW
    pk[6].prob = nw >= 7 ? 500.0f : 0.0f;   /* prueba: casi siempre llave */
#else
    pk[6].prob = nw >= 7 && game_saveme_on() ? 2.0f : 0.0f;
#endif
    int chosen = -1;
#if !defined(AT_LETTERS) && !defined(AT_PKTEST)
    /* CheckLetterSpawnRate: con la palabra del dia completa, ninguna letra; si no, el peso de la 1.0 */
    pk[0].prob = game_letter_pending() ? LETTER_PROB : 0.0f;
#endif
#ifdef AT_KEYSHOW
    pk[0].prob = 0;   /* prueba de las llaves: sin letras */
#endif
    if (z > spawn_z) {
        int list[NPK], n = 0;
        float acc[NPK], tot = 0;
        for (int i = 0; i < NPK; i++)
            if (pk[i].spawn_z < z) { list[n] = i; tot += pk[i].prob; acc[n] = tot; n++; }
        if (n > 0) {
            float r = rnd_rangef(0, tot);
            for (int j = 0; j < n; j++)
                if (r < acc[j]) {
                    chosen = list[j];
                    pk[chosen].spawn_z = z + pk[chosen].dist_min;
                    break;
                }
            spawn_z = z + spawn_spacing;
        }
    }
    for (int k = 0; k < NPK && k < nw; k++) {
        int g = (int)c[k];
        if (g < 0) continue;
        if (k == chosen) { if (*nv < max) visit[(*nv)++] = g; }
        else scene_set_active_recursive(g, 0);
    }
    spawn_egg_selection(sp, chosen, visit, nv, max);
}

static const float dur_tab[PW_COUNT][UPGRADE_MAX_TIER + 1] = {
    {0},                                     /* letras */
    {10, 11.5f, 13.4f, 15.8f, 19, 24, 30},   /* 2x (la 1.8 anade el ultimo nivel) */
    {8, 9, 10.5f, 12.5f, 15, 19, 25},        /* jetpack */
    {10, 11.5f, 13.4f, 15.8f, 19, 24, 30},   /* super zapatillas */
    {10, 11.5f, 13.4f, 15.8f, 19, 24, 30},   /* iman */
    {0},
};

float powerup_duration(int type)
{
    if (type < 0 || type >= PW_COUNT) return 0;
    int t = upgrade_tier[type];
    if (t < 0) t = 0;
    if (t > UPGRADE_MAX_TIER) t = UPGRADE_MAX_TIER;
    return dur_tab[type][t];
}
