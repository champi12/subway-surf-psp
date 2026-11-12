/* spawn.h - SpawnPointManager (que power-up aparece en cada punto) y Upgrades */
#ifndef SPAWN_H
#define SPAWN_H

enum { PW_LETTERS, PW_DOUBLE, PW_JETPACK, PW_SNEAKERS, PW_MAGNET, PW_MYSTERY, PW_COUNT };

void spawn_restart(void);
void spawn_perform_selection(int spawnpoint, int *visit, int *nv, int max);
/* SpawnPointEgg (1.8): chosen >= 0 si en ese punto ya ha salido un power-up (los huevos anadidos a los SpawnPoint) */
void spawn_egg_selection(int node, int chosen, int *visit, int *nv, int max);
int egg_should_spawn(void);
int game_letter_pending(void);
int game_saveme_on(void);
int game_hunt_kind(void);        /* 0 nada, 1 huevos (Roma), 2 munecas (caza semanal de la 1.10) */
float game_hunt_time(void);         /* Save Me de la 1.9 activo (game.c) */   /* PlayerInfo.GetNewDailyLetter != 0 (game.c) */
#define UPGRADE_MAX_TIER 6   /* niveles 0-6 (la 1.8 anadio el sexto) */   /* EggHuntManager.ShouldSpawnEggs (game.c) */
float game_level_speed(void);
/* duraciones de los power-ups (Upgrades.cs, nivel 0 si no se ha mejorado) */
float powerup_duration(int type);
extern int upgrade_tier[PW_COUNT];
#define DISTANCE_PER_METER 8.0f

#endif
