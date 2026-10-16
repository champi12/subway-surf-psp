/* spawn.h - SpawnPointManager (que power-up aparece en cada punto) y Upgrades */
#ifndef SPAWN_H
#define SPAWN_H

enum { PW_LETTERS, PW_DOUBLE, PW_JETPACK, PW_SNEAKERS, PW_MAGNET, PW_MYSTERY, PW_COUNT };

void spawn_restart(void);
void spawn_perform_selection(int spawnpoint, int *visit, int *nv, int max);
/* duraciones de los power-ups (Upgrades.cs, nivel 0 si no se ha mejorado) */
float powerup_duration(int type);
extern int upgrade_tier[PW_COUNT];
#define DISTANCE_PER_METER 8.0f

#endif
