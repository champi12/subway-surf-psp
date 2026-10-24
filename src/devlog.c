/* devlog.c - registro de desarrollo (devlog.txt) y medidas de memoria para la consola real */
#include <pspkernel.h>
#include <psppower.h>
#include <pspsysmem.h>
#include <pspge.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include "devlog.h"
#include "fpu.h"
#include "render.h"
#include "game.h"
#include "scene.h"

MemInfo dbg_mem;
int dbg_cpu_mhz, dbg_bus_mhz, dbg_bat_pct = -1, dbg_bat_temp, dbg_plugged;
float dbg_frame_hist[128];
float dbg_upd_ms, dbg_phys_ms, dbg_draw_ms;
float dbg_sect_ms[8];
volatile const char *dbg_phase_main = "arranque", *dbg_phase_music = "sin iniciar", *dbg_phase_mixer = "sin iniciar";
volatile unsigned int dbg_count_music, dbg_count_mixer;
volatile int dbg_tracing;
static int trace_frames_left, trace_runs;
static char trace_path[192];
static unsigned long long trace_t0;

/* solo hilo principal; formatos enteros */
static void trace_write(const char *s)
{
    if (!trace_path[0]) return;
    char line[160];
    unsigned long long us = sceKernelGetSystemTimeWide() - trace_t0;
    int n = snprintf(line, sizeof(line), "[%5u.%03u] %s\r\n", (unsigned int)(us / 1000000ULL), (unsigned int)((us / 1000ULL) % 1000), s);
    if (n > (int)sizeof(line)) n = sizeof(line);
    SceUID fd = sceIoOpen(trace_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, n); sceIoClose(fd); }
}

void dbg_trace(const char *phase) { trace_write(phase); }

void dbg_trace_start(const char *why, int frames)
{
    if (trace_runs >= 2 || !trace_path[0]) return;     /* como mucho 2 rastreos por partida (cada linea cuesta ~20 ms) */
    trace_runs++;
    char b[96];
    snprintf(b, sizeof(b), "===== RASTREO %d: %s =====", trace_runs, why);
    trace_write(b);
    trace_frames_left = frames;
    dbg_tracing = 1;
}
static volatile unsigned int dbg_count_main;
int dbg_hist_pos;
int dbg_hitches;

/* ------------------------------------------------------------ memoria */
extern SceUID __psp_heap_blockid;      /* bloque del monton que reserva la libc (libcglue) */
static unsigned heap_total;

void mem_init(void)
{
    /* la libc reserva el monton entero en el primer malloc; luego sbrk lo va entregando.
     * Lo que queda por entregar se mide con sbrk(+n)/sbrk(-n) (sin malloc entre medias, no cambia nada). */
    free(malloc(16));
    char *brk = sbrk(0);
    char *base = brk;
    if (__psp_heap_blockid > 0) base = sceKernelGetBlockHeadAddr(__psp_heap_blockid);
    unsigned lo = 0, hi = 64u << 20;
    while (hi - lo > 4096) {
        unsigned mid = ((lo + hi) / 2) & ~15u;
        if (sbrk(mid) != (void *)-1) { sbrk(-(int)mid); lo = mid; }
        else hi = mid;
    }
    heap_total = (unsigned)(brk - base) + lo;
}

void mem_query(MemInfo *m)
{
    struct mallinfo mi = mallinfo();
    m->heap_total = heap_total;
    m->heap_used = mi.uordblks;
    if (m->heap_used > dbg_mem.heap_peak) dbg_mem.heap_peak = m->heap_used;
    m->heap_peak = dbg_mem.heap_peak;
    m->sys_free = sceKernelTotalFreeMemSize();
    m->sys_max_block = sceKernelMaxFreeMemSize();
}

static void power_query(void)
{
    dbg_cpu_mhz = scePowerGetCpuClockFrequencyInt();
    dbg_bus_mhz = scePowerGetBusClockFrequencyInt();
    dbg_plugged = scePowerIsPowerOnline();
    if (scePowerIsBatteryExist()) {
        dbg_bat_pct = scePowerGetBatteryLifePercent();
        dbg_bat_temp = scePowerGetBatteryTemp();
    } else dbg_bat_pct = -1;
}

#if DEVLOG
/* ------------------------------------------------------------ escritura en segundo plano */
#define BUFSZ 16384
static char bufs[2][BUFSZ];
static int blen[2], bcur;
static volatile int wpend = -1;        /* buffer que esta escribiendo el hilo */
static volatile int wrun;
static SceUID lock = -1, wsema = -1, wthid = -1;
static char path[192];
static int dropped;
static unsigned long long t_start;
static SceUID wdthid = -1;
static volatile int boot_done;

static int writer_thread(SceSize a, void *p)
{
    fpu_no_exceptions();
    while (wrun) {
        sceKernelWaitSema(wsema, 1, 0);
        int b = wpend;
        if (b < 0) continue;
        /* abrir, escribir y cerrar: si el juego se cuelga, lo escrito ya esta en la tarjeta */
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
        if (fd >= 0) { sceIoWrite(fd, bufs[b], blen[b]); sceIoClose(fd); }
        blen[b] = 0;
        wpend = -1;
    }
    return 0;
}

/* con el cerrojo cogido: pasa el buffer lleno al hilo escritor */
static int swap_locked(void)
{
    if (!blen[bcur]) return 1;
    if (wpend >= 0) return 0;
    wpend = bcur;
    bcur ^= 1;
    blen[bcur] = 0;
    sceKernelSignalSema(wsema, 1);
    return 1;
}

/* IMPORTANTE (consola real): newlib tiene una sola estructura global (_impure_ptr) para todos los hilos y al
 * formatear numeros decimales (%f) usa una lista de memoria interna sin cerrojo. Si dos hilos formatean %f a la
 * vez, esa lista se corrompe y el juego se cuelga mas tarde en cualquier sitio. PPSSPP no lo reproduce porque solo
 * cambia de hilo en las llamadas al sistema. Por eso:
 *   - la marca de tiempo se escribe con enteros,
 *   - todo el formateo del devlog se hace dentro del cerrojo,
 *   - los hilos que no son el principal (musica, vigilante) solo usan formatos enteros (%d, %s, %x). */
static void vlog(const char *pre, const char *fmt, va_list ap)
{
    if (lock < 0) return;
    static char line[320];          /* protegida por el cerrojo */
    unsigned long long us = sceKernelGetSystemTimeWide() - t_start;
    unsigned int sec = (unsigned int)(us / 1000000ULL), cs = (unsigned int)((us / 10000ULL) % 100);
    sceKernelWaitSema(lock, 1, 0);
    int n = snprintf(line, sizeof(line), "[%5u.%02u] %s", sec, cs, pre);
    n += vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\r';   /* saltos de linea de Windows: se lee bien en el Bloc de notas */
    line[n++] = '\n';
    if (blen[bcur] + n > BUFSZ && !swap_locked()) dropped++;
    else { memcpy(bufs[bcur] + blen[bcur], line, n); blen[bcur] += n; }
    sceKernelSignalSema(lock, 1);
}

void dlog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("", fmt, ap);
    va_end(ap);
}

void dlog_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("ERROR ", fmt, ap);
    va_end(ap);
    dlog_flush();
}

void dlog_flush(void)
{
    if (lock < 0) return;
    sceKernelWaitSema(lock, 1, 0);
    swap_locked();
    sceKernelSignalSema(lock, 1);
}

void dlog_close(void)
{
    if (lock < 0) return;
    dlog("Fin: %d tirones, %d lineas perdidas por buffer lleno", dbg_hitches, dropped);
    for (int i = 0; i < 200 && wpend >= 0; i++) sceKernelDelayThread(10000);
    dlog_flush();
    for (int i = 0; i < 200 && wpend >= 0; i++) sceKernelDelayThread(10000);
    wrun = 0;
    sceKernelSignalSema(wsema, 1);
    sceKernelWaitThreadEnd(wthid, NULL);
    sceKernelDeleteThread(wthid);
    if (wdthid >= 0) { sceKernelWaitThreadEnd(wdthid, NULL); sceKernelDeleteThread(wdthid); }
}

static const char *state_name(int s);
#define state_name_pub state_name

/* ------------------------------------------------------------ vigilante (watchdog) */
static const char *ge_state(void)
{
    /* sceGeDrawSync(1) solo consulta: 0 terminado, 1 en cola, 2 dibujando, 3 parado esperando datos, 4 en pausa */
    static const char *n[] = {"terminado", "en cola", "dibujando", "atascado (stall)", "en pausa"};
    int s = sceGeDrawSync(1);
    return s >= 0 && s <= 4 ? n[s] : "error";
}

static void write_direct(const char *s, int n)
{
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, s, n); sceIoClose(fd); }
}

/* este hilo no es el principal: solo formatos enteros (ver vlog) */
static void watchdog_report(int half_secs, int music_moving, int mixer_moving)
{
    char line[512];
    unsigned long long us = sceKernelGetSystemTimeWide() - t_start;
    unsigned int sec = (unsigned int)(us / 1000000ULL), cs = (unsigned int)((us / 10000ULL) % 100);
    int n = snprintf(line, sizeof(line),
                     "[%5u.%02u] BLOQUEO el bucle principal lleva %d.%d s parado en: %s | hilo de musica: %s (%s) | "
                     "mezclador de sonidos: %s (%s) | GE: %s | estado %s, %d m\r\n",
                     sec, cs, half_secs / 2, (half_secs & 1) * 5, (const char *)dbg_phase_main, (const char *)dbg_phase_music,
                     music_moving ? "avanza" : "parado", (const char *)dbg_phase_mixer, mixer_moving ? "avanza" : "parado",
                     ge_state(), state_name_pub(ginfo.state), ginfo.meters);
    if (n > (int)sizeof(line)) n = sizeof(line);
    /* las lineas que aun no se habian escrito van primero: dicen que estaba pasando justo antes */
    SceUInt to = 300000;
    int locked = sceKernelWaitSema(lock, 1, &to) >= 0;
    if (locked) {
        if (wpend < 0 && blen[bcur]) { write_direct(bufs[bcur], blen[bcur]); blen[bcur] = 0; }
        write_direct(line, n);
        sceKernelSignalSema(lock, 1);
    } else write_direct(line, n);
}

static int watchdog_thread(SceSize a, void *p)
{
    fpu_no_exceptions();
    unsigned int last_main = dbg_count_main, last_mus = dbg_count_music, last_mix = dbg_count_mixer;
    int stuck = 0, reported = 0;
    while (wrun) {
        sceKernelDelayThread(500000);
        unsigned int m = dbg_count_main, mu = dbg_count_music, mx = dbg_count_mixer;
        int mus_moving = mu != last_mus, mix_moving = mx != last_mix;
        last_mus = mu; last_mix = mx;
        if (m != last_main) {
            if (reported) dlog("RECUPERADO el bucle principal vuelve a avanzar tras %d.%d s", stuck / 2, (stuck & 1) * 5);
            last_main = m; stuck = reported = 0;
            continue;
        }
        stuck++;
        /* al terminar el arranque se empieza a contar de cero (la carga inicial no es un bloqueo) */
        static int was_boot_done;
        if (boot_done && !was_boot_done) { was_boot_done = 1; stuck = 0; continue; }
        if (!reported && stuck >= (boot_done ? 6 : 20)) {
            reported = 1;
            watchdog_report(stuck, mus_moving, mix_moving);
        }
    }
    return 0;
}

void dlog_boot_done(void) { boot_done = 1; }

static const char *fw_str(char *b, int n)
{
    int v = sceKernelDevkitVersion();
    snprintf(b, n, "%d.%d%d (0x%08x)", (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v);
    return b;
}

void dlog_init(void)
{
    t_start = sceKernelGetSystemTimeWide();
    /* los hilos no tienen directorio actual en la PSP: ruta absoluta */
    char dir[160];
    if (!getcwd(dir, sizeof(dir))) strcpy(dir, "ms0:/PSP/GAME/SUBWAY");
    /* se guardan los 3 ultimos: devlog.txt (este), devlog_prev.txt y devlog_prev2.txt */
    char prev[192], prev2[192];
    snprintf(trace_path, sizeof(trace_path), "%s/trace.txt", dir);
    sceIoRemove(trace_path);
    trace_t0 = sceKernelGetSystemTimeWide();
    snprintf(path, sizeof(path), "%s/devlog.txt", dir);
    snprintf(prev, sizeof(prev), "%s/devlog_prev.txt", dir);
    snprintf(prev2, sizeof(prev2), "%s/devlog_prev2.txt", dir);
    sceIoRemove(prev2);
    sceIoRename(prev, prev2);
    if (sceIoRename(path, prev) < 0) sceIoRemove(path);
    lock = sceKernelCreateSema("dlog_lock", 0, 1, 1, NULL);
    wsema = sceKernelCreateSema("dlog_write", 0, 0, 1, NULL);
    wrun = 1;
    /* prioridad baja (numero alto): solo escribe cuando el juego y el audio no necesitan la CPU */
    wthid = sceKernelCreateThread("devlog", writer_thread, 0x30, 0x2000, PSP_THREAD_ATTR_USER, NULL);
    if (wthid >= 0) sceKernelStartThread(wthid, 0, NULL);
    /* vigilante: prioridad mas alta que el juego y que el audio, para poder avisar aunque alguno se quede colgado */
    wdthid = sceKernelCreateThread("watchdog", watchdog_thread, 0x10, 0x2000, PSP_THREAD_ATTR_USER, NULL);
    if (wdthid >= 0) sceKernelStartThread(wdthid, 0, NULL);

    char fw[32];
    power_query();
    mem_query(&dbg_mem);
    dlog("===== Subway Surfers PSP - devlog =====");
    dlog("Compilado: %s %s", __DATE__, __TIME__);
    dlog("Firmware: %s", fw_str(fw, sizeof(fw)));
    dlog("Carpeta: %s", dir);
    dlog("CPU %d MHz, bus %d MHz | bateria %d%% %d C | cargador %s", dbg_cpu_mhz, dbg_bus_mhz, dbg_bat_pct,
         dbg_bat_temp, dbg_plugged ? "si" : "no");
    dlog("RAM: monton de C %u KB (lo que la consola le deja al juego para datos)", dbg_mem.heap_total / 1024);
    dlog("RAM: libre fuera del monton %u KB (bloque mayor %u KB)", dbg_mem.sys_free / 1024, dbg_mem.sys_max_block / 1024);
    dlog("Leyenda de STAT: fps media/min | cpu media/max ms | ge = espera al GE + VBlank | vert/tri/draw maximos "
         "| heap usado/total | vram usada/2048 KB | lista GE, vertices e indices: %% usado (pico) | perd = dibujos perdidos");
    dlog_flush();
}

/* ------------------------------------------------------------ estadisticas periodicas */
static const char *state_name(int s)
{
    static const char *n[] = {"MENU", "INTRO", "CARRERA", "JETPACK", "CAYENDO", "MUERTE", "FIN", "CHECKPOINT", "TIENDA",
                              "PERSONAJES", "TROFEOS"};
    return s >= 0 && s < 11 ? n[s] : "?";
}

static struct {
    int frames, hitch_lines, verts, tris, draws, clip, dropped;
    float sum_ms, max_ms, sum_cpu, max_cpu, sum_wait, sum_upd, sum_phys, sum_draw;
    int sum_vcpu, sum_vhw, sum_vskin, sum_seam;
    float sum_sect[8];
    unsigned long long t0;
} win;
static int last_state = -1;
static float run_start_t;

static void stat_line(void)
{
    float secs = (sceKernelGetSystemTimeWide() - win.t0) / 1000000.0f;
    if (win.frames <= 0 || secs <= 0) return;
    MemInfo *m = &dbg_mem;
    const RMem *r = &r_mem;
    dlog("STAT fps %.1f/%.1f | cpu %.1f/%.1f ms | ge %.1f ms | vert %d tri %d draw %d clip %d | "
         "heap %u/%u KB (pico %u) sis %u KB | vram %u/2048 KB (tex %u KB, %d) | ram tex %u KB (%d) | "
         "lista %d%% (%d%%) vert %d%% (%d%%) ind %d%% (%d%%) perd %d | %s %d m | %d MHz bat %d%% %d C",
         win.frames / secs, win.max_ms > 0 ? 1000.0f / win.max_ms : 0, win.sum_cpu / win.frames, win.max_cpu,
         win.sum_wait / win.frames, win.verts, win.tris, win.draws, win.clip,
         m->heap_used / 1024, m->heap_total / 1024, m->heap_peak / 1024, m->sys_free / 1024,
         (unsigned)(r->vram_fb + r->vram_tex) / 1024, (unsigned)r->vram_tex / 1024, r->vram_tex_n,
         (unsigned)r->ram_tex / 1024, r->ram_tex_n,
         r->dlist_used * 100 / r->dlist_max, r->dlist_peak * 100 / r->dlist_max,
         r->vring_used * 100 / r->vring_max, r->vring_peak * 100 / r->vring_max,
         r->iring_used * 100 / r->iring_max, r->iring_peak * 100 / r->iring_max, win.dropped,
         state_name(ginfo.state), ginfo.meters, dbg_cpu_mhz, dbg_bat_pct, dbg_bat_temp);
    /* en que se va la CPU: logica (con la fisica dentro) y preparar el dibujo (transformar vertices) */
    dlog("PERFIL logica %.1f ms (fisica %.1f) | dibujo %.1f ms | vertices por fotograma: CPU %d, GE %d",
         win.sum_upd / win.frames, win.sum_phys / win.frames, win.sum_draw / win.frames,
         win.sum_vcpu / win.frames, win.sum_vhw / win.frames);
    float f = 1.0f / win.frames;
    dlog("DIBUJO via %.1f | personajes %.1f (piel por CPU %d vert) | monedas %.1f | efectos y transparentes %.1f | entorno menu %.1f | "
         "cielo %.1f | interfaz %.1f ms | recorte por software: %d triangulos (%.1f trozos repetidos en el GE)",
         win.sum_sect[2] * f, win.sum_sect[5] * f, win.sum_vskin / win.frames, win.sum_sect[4] * f, win.sum_sect[6] * f, win.sum_sect[3] * f,
         win.sum_sect[1] * f, win.sum_sect[7] * f, win.clip, win.sum_seam * f);
    /* avisos: lo que en la consola real acabaria en cuelgue o en tirones */
    if (m->heap_total && m->heap_used > m->heap_total / 10 * 9)
        dlog("AVISO memoria casi llena: %u de %u KB", m->heap_used / 1024, m->heap_total / 1024);
    if (m->sys_free < 256 * 1024) dlog("AVISO poca memoria del sistema: %u KB", m->sys_free / 1024);
    if (r->dlist_peak > r->dlist_max / 10 * 8) dlog("AVISO lista del GE al %d%%: si llega al 100%% se corrompe la memoria", r->dlist_peak * 100 / r->dlist_max);
    if (win.dropped) dlog("AVISO %d dibujos descartados por falta de espacio en el buffer de vertices", win.dropped);
    if (dbg_bat_temp >= 45) dlog("AVISO bateria a %d C", dbg_bat_temp);
}

#endif /* DEVLOG */

void dbg_frame(float frame_ms, float cpu_ms, float wait_ms)
{
    static int n;
    n++;
#if DEVLOG
    dbg_count_main++;
    if (dbg_tracing) {
        char b[64];
        snprintf(b, sizeof(b), "--- fotograma %d (%d ms) ---", n, (int)frame_ms);
        trace_write(b);
        if (--trace_frames_left <= 0) { dbg_tracing = 0; trace_write("===== fin del rastreo ====="); }
    }
#endif
    dbg_frame_hist[dbg_hist_pos] = frame_ms;
    dbg_hist_pos = (dbg_hist_pos + 1) & 127;
    if (n % 15 == 1) mem_query(&dbg_mem);       /* mallinfo recorre el monton: 4 veces por segundo basta */
    if (n % 300 == 1) power_query();             /* la bateria se lee por el syscon: es lenta */
    int hitch = n > 5 && frame_ms > 50.0f;
    if (hitch) dbg_hitches++;
#if DEVLOG
    if (!win.t0) win.t0 = sceKernelGetSystemTimeWide();
    win.frames++;
    win.sum_ms += frame_ms;
    if (frame_ms > win.max_ms && n > 5) win.max_ms = frame_ms;
    win.sum_cpu += cpu_ms;
    win.sum_upd += dbg_upd_ms;
    win.sum_phys += dbg_phys_ms;
    win.sum_draw += dbg_draw_ms;
    win.sum_vcpu += r_stat_verts;
    win.sum_vhw += r_stat_hw;
    win.sum_seam += r_stat_seam;
    win.sum_vskin += r_stat_skin_cpu;
    for (int k = 0; k < 8; k++) win.sum_sect[k] += dbg_sect_ms[k];
    if (cpu_ms > win.max_cpu) win.max_cpu = cpu_ms;
    win.sum_wait += wait_ms;
    if (r_stat_verts > win.verts) win.verts = r_stat_verts;
    if (r_stat_tris > win.tris) win.tris = r_stat_tris;
    if (r_stat_draws > win.draws) win.draws = r_stat_draws;
    if (r_stat_clipped > win.clip) win.clip = r_stat_clipped;
    win.dropped += r_mem.dropped;

    /* frame_ms es lo que duro la vuelta ANTERIOR del bucle: su CPU, su espera y sus cargas son las de la llamada previa */
    static float prev_cpu, prev_wait;
    static int prev_loads;
    if (hitch && win.hitch_lines < 5) {
        win.hitch_lines++;
        float other = frame_ms - prev_cpu - prev_wait;
        dlog("TIRON %.0f ms (cpu %.1f ms, ge %.1f ms, otros %.1f ms) en %s a %d m%s", frame_ms, prev_cpu, prev_wait,
             other > 0 ? other : 0, state_name(ginfo.state), ginfo.meters,
             prev_loads ? " - cargando texturas de la Memory Stick" : "");
    }
    prev_cpu = cpu_ms;
    prev_wait = wait_ms;
    prev_loads = r_mem.frame_texloads;
    if (ginfo.state != last_state) {
        dlog("ESTADO %s -> %s", last_state < 0 ? "-" : state_name(last_state), state_name(ginfo.state));
        if (ginfo.state == GS_INTRO || (ginfo.state == GS_RUNNING && last_state == GS_MENU)) run_start_t = ginfo.time;
        if (ginfo.state == GS_GAMEOVER)
            dlog("FIN DE CARRERA puntos %d (final %d) monedas %d metros %d duracion %.0f s", ginfo.score,
                 ginfo.final_score, ginfo.gameover_coins, ginfo.meters, ginfo.time - run_start_t);
        last_state = ginfo.state;
    }
    /* lo escrito llega a la tarjeta cada segundo: si la consola se cuelga se pierde como mucho 1 s */
    static unsigned long long last_flush;
    unsigned long long now = sceKernelGetSystemTimeWide();
    if (now - last_flush >= 1000000ULL) { last_flush = now; dlog_flush(); }
    if ((now - win.t0) >= 5000000ULL) {
        stat_line();
        if (dbg_hitches && win.hitch_lines >= 5) dlog("(mas tirones en este intervalo; total %d)", dbg_hitches);
        memset(&win, 0, sizeof(win));
        win.t0 = sceKernelGetSystemTimeWide();
        dlog_flush();
    }
#else
    (void)cpu_ms; (void)wait_ms;
#endif
}
