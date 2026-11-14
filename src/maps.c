/* maps.c - selector de mapas (ajustes)
 * El mapa elegido se guarda en mapa.cfg (en la carpeta del juego) y el juego se vuelve a lanzar entero con
 * sceKernelLoadExec: la consola descarga todo el programa y su memoria y lo carga de cero, asi que no queda nada del
 * mapa anterior (texturas, mallas, sonidos, tramos). Al arrancar, main.c lee mapa.cfg y carga ese mapa en lugar del
 * de la fecha. Sin mapa.cfg (nunca se ha elegido) el juego sigue como siempre: el mapa y el tema segun la fecha */
#include <pspkernel.h>
#include <psploadexec.h>
#include <stdio.h>
#include <string.h>
#include "maps.h"
#include "lang.h"
#include "devlog.h"

/* por version, como en el carrusel */
const MapDef maps_def[] = {
    {"clasico",   "data",      MAP_THEME_NONE,      0,   "Clásico",        "Classic",        "1.0",   NULL},
    {"halloween", "data/whw",  MAP_THEME_HALLOWEEN, 4,   "Halloween",      "Halloween",      "1.4",   "data/hw_tex.bin"},
    {"subway",    "data/w15",  MAP_THEME_NONE,      15,  "Subway Surfers", "Subway Surfers", "1.5.2", NULL},
    {"navidad",   "data/w15",  MAP_THEME_XMAS,      15,  "Navidad",        "Holidays",       "1.5.2", "data/w15/xm_tex.bin"},
    {"nuevayork", "data/w16",  MAP_THEME_NONE,      16,  "Nueva York",     "New York",       "1.6",   NULL},
    {"rio",       "data/w17",  MAP_THEME_NONE,      17,  "Río",            "Rio",            "1.7",   NULL},
    {"roma",      "data/w18",  MAP_THEME_NONE,      18,  "Roma",           "Rome",           "1.8",   NULL},
    {"sidney",    "data/w19",  MAP_THEME_NONE,      19,  "Sídney",         "Sydney",         "1.9",   NULL},
    {"tokio",     "data/w110", MAP_THEME_NONE,      110, "Tokio",          "Tokyo",          "1.10",  NULL},
    {"miami",     "data/w111", MAP_THEME_NONE,      111, "Miami",          "Miami",          "1.11",  NULL},
};
const int maps_n = sizeof(maps_def) / sizeof(maps_def[0]);

int map_forced = -1;
int map_theme_force = -1;

#define MAPS_CFG "mapa.cfg"

static int file_exists(const char *p)
{
    SceUID f = sceIoOpen(p, PSP_O_RDONLY, 0);
    if (f < 0) return 0;
    sceIoClose(f);
    return 1;
}

int maps_available(int i)
{
    static signed char cache[16];
    if (i < 0 || i >= maps_n) return 0;
    if (!cache[i]) {
        char p[64];
        snprintf(p, sizeof(p), "%s/world.bin", maps_def[i].dir);
        cache[i] = file_exists(p) && (!maps_def[i].need || file_exists(maps_def[i].need)) ? 1 : -1;
    }
    return cache[i] > 0;
}

int maps_cfg_read(void)
{
    FILE *f = fopen(MAPS_CFG, "r");
    if (!f) return -1;
    char id[32] = {0};
    int ok = fscanf(f, "%31s", id) == 1;
    fclose(f);
    if (!ok) return -1;
    for (int i = 0; i < maps_n; i++) if (!strcmp(id, maps_def[i].id)) return i;
    return -1;
}

int maps_cfg_write(int i)
{
    if (i < 0 || i >= maps_n) return -1;
    FILE *f = fopen(MAPS_CFG, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", maps_def[i].id);
    fclose(f);
    return 0;
}

int maps_current(void)
{
    extern int game_world, theme_halloween, theme_xmas;
    if (map_forced >= 0) return map_forced;
    for (int i = 0; i < maps_n; i++) {
        const MapDef *m = &maps_def[i];
        if (m->world != game_world) continue;
        if (game_world == 15 && (m->theme == MAP_THEME_XMAS) != (theme_xmas != 0)) continue;
        return i;
    }
    /* el clasico con el tema de Halloween (sin data/whw): el clasico */
    return 0;
}

const char *maps_name(int i) { return i >= 0 && i < maps_n ? TR(maps_def[i].es, maps_def[i].en) : ""; }

static char eboot_path[256];
static int restart_to = -1;

void maps_set_eboot(const char *path)
{
    if (path) snprintf(eboot_path, sizeof(eboot_path), "%s", path);
}

void maps_request_restart(int i)
{
    if (i < 0 || i >= maps_n) return;
    if (maps_cfg_write(i) != 0) dlog_error("No se pudo guardar mapa.cfg");
    restart_to = i;
    dlog("MAPA elegido en el selector: %s (%s): se reinicia el juego", maps_def[i].es, maps_def[i].dir);
}

int maps_restart_pending(void) { return restart_to >= 0; }

int maps_restart_now(void)
{
    const char *p = eboot_path[0] ? eboot_path : "ms0:/PSP/GAME/SUBWAY/EBOOT.PBP";
    struct SceKernelLoadExecParam param;
    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.args = strlen(p) + 1;
    param.argp = (void *)p;
    param.key = NULL;
    int r = sceKernelLoadExec(p, &param);
    return r;   /* solo llega aqui si la consola no lo ha dejado */
}
