/* maps.c - selector de mapas (ajustes)
 * El mapa elegido se guarda en mapa.cfg (en la carpeta del juego) y el juego se vuelve a lanzar entero con
 * sceKernelLoadExec: la consola descarga todo el programa y su memoria y lo carga de cero, asi que no queda nada del
 * mapa anterior (texturas, mallas, sonidos, tramos). Al arrancar, main.c lee mapa.cfg y carga ese mapa en lugar del
 * de la fecha. Sin mapa.cfg (nunca se ha elegido) el juego sigue como siempre: el mapa y el tema segun la fecha */
#include <pspkernel.h>
#include <psploadexec.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "maps.h"
#include "lang.h"
#include "devlog.h"
#include "../exhandler/exhandler.h"

/* por version, como en el carrusel */
const MapDef maps_def[] = {
    {"clasico",   "data",      MAP_THEME_NONE,      0,   "Clásico",        "Classic",        "1.0",   NULL,                  "data/thumb.bin"},
    {"halloween", "data/whw",  MAP_THEME_HALLOWEEN, 4,   "Halloween",      "Halloween",      "1.4",   "data/hw_tex.bin",     "data/whw/thumb.bin"},
    {"subway",    "data/w15",  MAP_THEME_NONE,      15,  "Subway Surfers", "Subway Surfers", "1.5.2", NULL,                  "data/w15/thumb.bin"},
    {"navidad",   "data/w15",  MAP_THEME_XMAS,      15,  "Navidad",        "Holidays",       "1.5.2", "data/w15/xm_tex.bin", "data/w15/thumb_xmas.bin"},
    {"nuevayork", "data/w16",  MAP_THEME_NONE,      16,  "Nueva York",     "New York",       "1.6",   NULL,                  "data/w16/thumb.bin"},
    {"rio",       "data/w17",  MAP_THEME_NONE,      17,  "Río",            "Rio",            "1.7",   NULL,                  "data/w17/thumb.bin"},
    {"roma",      "data/w18",  MAP_THEME_NONE,      18,  "Roma",           "Rome",           "1.8",   NULL,                  "data/w18/thumb.bin"},
    {"sidney",    "data/w19",  MAP_THEME_NONE,      19,  "Sídney",         "Sydney",         "1.9",   NULL,                  "data/w19/thumb.bin"},
    {"tokio",     "data/w110", MAP_THEME_NONE,      110, "Tokio",          "Tokyo",          "1.10",  NULL,                  "data/w110/thumb.bin"},
    {"miami",     "data/w111", MAP_THEME_NONE,      111, "Miami",          "Miami",          "1.11",  NULL,                  "data/w111/thumb.bin"},
    {"paris",     "data/w112", MAP_THEME_NONE,      112, "París",          "Paris",          "1.12",  NULL,                  "data/w112/thumb.bin"},
    {"pekin",     "data/w113", MAP_THEME_NONE,      113, "Pekín",          "Beijing",        "1.13",  NULL,                  "data/w113/thumb.bin"},
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
        /* cada mapa es independiente: sale solo si su carpeta trae el mapa y su miniatura (y lo que necesite su tema).
         * Asi, compilado sin algun APK (o solo con el clasico), no aparecen mapas ni cuadros vacios */
        cache[i] = file_exists(p) && file_exists(maps_def[i].thumb) &&
                   (!maps_def[i].need || file_exists(maps_def[i].need)) ? 1 : -1;
    }
    return cache[i] > 0;
}

void maps_init_thumbs(void)
{
    extern int data_add_thumb(const char *path);
    int n = 0;
    for (int i = 0; i < maps_n; i++)
        if (maps_available(i) && data_add_thumb(maps_def[i].thumb) > 0) n++;
    dlog("Selector de mapas: %d mapas con su miniatura", n);
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

/* exloader.prx ya cargado al arrancar (version de pruebas, plan B del capturador de cuelgues): su funcion de carga */
void (*maps_exloader_load)(void *self);

/* carga y arranca un modulo de kernel con la ruta del EBOOT: directo, o con exloader.prx y kubridge si el CFW no
 * deja cargar modulos de kernel desde un juego (como exhandler.prx en main.c) */
static int start_relaunch(const char *dir, const char *eboot)
{
    static ExhArgs a;
    memset(&a, 0, sizeof(a));
    snprintf(a.crash_path, sizeof(a.crash_path), "%s", eboot);   /* relaunch.prx lee la ruta del principio */
    char prx[200];
    snprintf(prx, sizeof(prx), "%s/relaunch.prx", dir);
    SceUID m = sceKernelLoadModule(prx, 0, NULL);
    if (m >= 0) {
        int st = 0, r = sceKernelStartModule(m, sizeof(a), &a, &st, NULL);
        dlog("REINICIO relaunch.prx: carga 0x%08x, inicio 0x%08x, estado %d", m, r, st);
        return r >= 0 && st == 0 ? 0 : -1;
    }
    static ExLoaderArgs la;
    static int res[3];
    res[0] = res[1] = res[2] = -1;
    memset(&la, 0, sizeof(la));
    snprintf(la.prx_path, sizeof(la.prx_path), "%s", prx);
    la.exh = a;
    la.result = res;
    char ldr[200];
    snprintf(ldr, sizeof(ldr), "%s/exloader.prx", dir);
    SceUID lm = sceKernelLoadModule(ldr, 0, NULL);
    ExLoaderArgs *lp = &la;
    int lst = 0, lr = lm >= 0 ? sceKernelStartModule(lm, sizeof(lp), &lp, &lst, NULL) : lm;
    if (lr >= 0 && la.load) la.load(&la);
    else if (maps_exloader_load) maps_exloader_load(&la);   /* ya estaba cargado: el mismo */
    dlog("REINICIO relaunch.prx con kubridge: directo 0x%08x, exloader 0x%08x/0x%08x, carga 0x%08x, inicio 0x%08x, estado %d",
         m, lm, lr, res[0], res[1], res[2]);
    return res[0] >= 0 && res[1] >= 0 && res[2] == 0 ? 0 : -1;
}

int maps_restart_now(void)
{
    const char *p = eboot_path[0] ? eboot_path : "ms0:/PSP/GAME/SUBWAY/EBOOT.PBP";
    /* 1: desde kernel (relaunch.prx): lo que si deja la consola. El relanzamiento va en un hilo del modulo; si
     * funciona, la consola cierra el juego mientras se espera aqui */
    char dir[160];
    if (!getcwd(dir, sizeof(dir))) strcpy(dir, "ms0:/PSP/GAME/SUBWAY");
    if (start_relaunch(dir, p) == 0) {
        dlog_flush();
        for (int i = 0; i < 40; i++) sceKernelDelayThread(100000);   /* hasta 4 s */
        dlog("AVISO relaunch.prx arranco pero el juego no se reinicio");
    }
    /* 2: en modo usuario (la mayoria de consolas no lo dejan a un juego de la Memory Stick) */
    struct SceKernelLoadExecParam param;
    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.args = strlen(p) + 1;
    param.argp = (void *)p;
    param.key = NULL;
    int r = sceKernelLoadExec(p, &param);
    return r;   /* solo llega aqui si la consola no lo ha dejado */
}
