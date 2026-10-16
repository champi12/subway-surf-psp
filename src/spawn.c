/* spawn.c - SpawnPointManager.cs + Upgrades.cs */
#include <float.h>
#include "spawn.h"
#include "scene.h"

int upgrade_tier[PW_COUNT];

typedef struct { float prob, dist_min, spawn_z; int field; } PickupType;

/* orden de SpawnPoint: dailyLetter, doubleScoreMultiplier, jetpackPickup, jumpBooster, magnetBooster, mysteryBox
 * spawnProbability / minimumMeters de Upgrades.cs */
static PickupType pk[6] = {
    {15.0f, 0.0f, 0, 0},                       /* letras: spawnDistanceMin queda en 0 (bug del original) */
    {20.0f, 0.0f * DISTANCE_PER_METER, 0, 1},  /* 2x */
    {10.0f, 1000.0f * DISTANCE_PER_METER, 0, 2},/* jetpack: minimumMeters 1000 */
    {25.0f, 0.0f, 0, 3},                       /* super zapatillas */
    {25.0f, 0.0f, 0, 4},                       /* iman */
    {5.0f, 99999.0f * DISTANCE_PER_METER, 0, 5} /* caja misteriosa */
};
static float spawn_z, spawn_spacing;

/* UpgradeFirstSpawnMeters = 250, UpgradeSpawnSpacingMeters = 300 */
void spawn_restart(void)
{
    spawn_z = 250.0f * DISTANCE_PER_METER;
    spawn_spacing = 300.0f * DISTANCE_PER_METER;
    for (int i = 0; i < 6; i++) pk[i].spawn_z = -FLT_MAX;
}

void spawn_perform_selection(int sp, int *visit, int *nv, int max)
{
    const uint32_t *c = node_comp(sp, C_SPAWNPOINT, NULL);
    if (!c) return;
    float z = node_pos(sp).z;
    int chosen = -1;
    if (z > spawn_z) {
        int list[6], n = 0;
        float acc[6], tot = 0;
        for (int i = 0; i < 6; i++)
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
    for (int k = 0; k < 6; k++) {
        int g = (int)c[k];
        if (g < 0) continue;
        if (k == chosen) { if (*nv < max) visit[(*nv)++] = g; }
        else scene_set_active_recursive(g, 0);
    }
}

static const float dur_tab[PW_COUNT][6] = {
    {0},                                     /* letras */
    {10, 11.5f, 13.4f, 15.8f, 19, 24},       /* 2x */
    {8, 9, 10.5f, 12.5f, 15, 19},            /* jetpack */
    {10, 11.5f, 13.4f, 15.8f, 19, 24},       /* super zapatillas */
    {10, 11.5f, 13.4f, 15.8f, 19, 24},       /* iman */
    {0},
};

float powerup_duration(int type)
{
    if (type < 0 || type >= PW_COUNT) return 0;
    int t = upgrade_tier[type];
    if (t < 0) t = 0;
    if (t > 5) t = 5;
    return dur_tab[type][t];
}
