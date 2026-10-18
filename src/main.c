/* main.c - Subway Surfers para PSP: arranque, bucle principal, controles y medidor de rendimiento */
#include <pspkernel.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspdebug.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include "render.h"
#include "data.h"
#include "scene.h"
#include "game.h"
#include "audio.h"
#include "hud.h"
#include "devlog.h"
#include "lang.h"
#include "fpu.h"

PSP_MODULE_INFO("SubwaySurfers", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

static volatile int running = 1;
static unsigned int rng_state = 0x12345678;
unsigned int rnd_u32(void)
{
    unsigned int x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static int exit_cb(int a1, int a2, void *c) { running = 0; return 0; }
static int cb_thread(SceSize a, void *p)
{
    fpu_no_exceptions();
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static void fatal(const char *msg)
{
    dlog_error("%s", msg);
    dlog_close();
    pspDebugScreenInit();
    pspDebugScreenPrintf("Subway Surfers PSP\n\nError: %s\n\n", msg);
    pspDebugScreenPrintf("%s", TR("Hay que generar la carpeta data/ con tools/extract.py\na partir del APK original (ver README.md).\n",
                                  "The data/ folder must be generated with tools/extract.py\nfrom the original APK (see README.md).\n"));
    while (running) sceDisplayWaitVblankStart();
    sceKernelExitGame();
}

#include "../exhandler/exhandler.h"
/* Carga exhandler.prx (modulo de kernel): si la CPU lanza una excepcion, en vez de congelarse la consola se
 * escribe crash.txt con la direccion exacta del fallo. Solo en firmware personalizado; si no se puede, se sigue. */
static void load_exhandler(void)
{
    static ExhArgs a;
    char dir[160];
    if (!getcwd(dir, sizeof(dir))) strcpy(dir, "ms0:/PSP/GAME/SUBWAY");
    char prx[200];
    snprintf(prx, sizeof(prx), "%s/exhandler.prx", dir);
    snprintf(a.crash_path, sizeof(a.crash_path), "%s/crash.txt", dir);
    snprintf(a.devlog_path, sizeof(a.devlog_path), "%s/devlog.txt", dir);
    a.phase_main = &dbg_phase_main;
    a.phase_music = &dbg_phase_music;
    a.phase_mixer = &dbg_phase_mixer;
#ifdef EXH_FORCE_KUBRIDGE
    SceUID m = -1;   /* prueba del plan B */
#else
    SceUID m = sceKernelLoadModule(prx, 0, NULL);
#endif
    if (m >= 0) {
        int st = 0;
        int r = sceKernelStartModule(m, sizeof(a), &a, &st, NULL);
        dlog("Capturador de excepciones: %s (carga 0x%08x, inicio 0x%08x, estado %d)",
             r >= 0 && st == 0 ? "activo, un cuelgue escribira crash.txt" : "NO activo", m, r, st);
        return;
    }
    dlog("Capturador de excepciones: sceKernelLoadModule no pudo (0x%08x); se prueba con kubridge", m);
    /* plan B: exloader.prx (modulo de usuario) lo carga con kubridge */
    static ExLoaderArgs la;
    static int res[3] = {-1, -1, -1};
    snprintf(la.prx_path, sizeof(la.prx_path), "%s", prx);
    la.exh = a;
    la.result = res;
    char ldr[200];
    snprintf(ldr, sizeof(ldr), "%s/exloader.prx", dir);
    SceUID lm = sceKernelLoadModule(ldr, 0, NULL);
    ExLoaderArgs *lp = &la;      /* se le pasa el puntero: comparte la memoria de usuario con el juego */
    int lst = 0, lr = lm >= 0 ? sceKernelStartModule(lm, sizeof(lp), &lp, &lst, NULL) : lm;
    if (lr >= 0 && la.load) la.load(&la);   /* ya fuera del module_start de exloader */
    dlog("Capturador de excepciones (kubridge): %s (exloader 0x%08x/0x%08x, carga 0x%08x, inicio 0x%08x, estado %d)",
         res[0] >= 0 && res[1] >= 0 && res[2] == 0 ? "activo, un cuelgue escribira crash.txt" : "NO activo",
         lm, lr, res[0], res[1], res[2]);
}

extern void game_load(void);
unsigned int heap_used_kb(void) { struct mallinfo mi = mallinfo(); return mi.uordblks / 1024; }

int main(int argc, char **argv)
{
    unsigned int fcr31 = fpu_get_fcr31();
    fpu_no_exceptions();
    mem_init();
    SceUID th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    int clk = scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    rng_state ^= sceKernelGetSystemTimeLow();
    dlog_init();
#if DEVLOG && !defined(AUTOTEST)
    /* en la prueba automatica no: PPSSPP no implementa el registro de excepciones del kernel */
    load_exhandler();
#endif
    dlog("FPU: FCR31 al arrancar 0x%08x (excepciones activadas 0x%02x) -> desactivadas en todos los hilos", fcr31, (fcr31 >> 7) & 31);
    lang_init();
    if (clk < 0) dlog("AVISO no se pudo poner la CPU a 333 MHz (0x%08x)", clk);
    unsigned int tl = sceKernelGetSystemTimeLow();
    if (data_load("data") != 0) fatal(data_err());
    if (ui_load("data") != 0) dlog_error("No se pudo cargar data/ui.bin: la interfaz no tendra textos");
    dlog("Datos cargados en %.2f s: %d objetos, %d modelos, %d texturas, %d animaciones, %d sonidos",
         (sceKernelGetSystemTimeLow() - tl) / 1000000.0f, gd.nnodes, gd.nmeshes, gd.ntex, gd.nclips, gd.nsfx);
    r_init();
    {
        extern int r_using_vfpu(void);
        dlog("GE iniciado: VRAM framebuffers+Z %d KB, texturas hasta %d KB | VFPU %s", r_mem.vram_fb / 1024,
             (R_VRAM_TOTAL - r_mem.vram_fb) / 1024, r_using_vfpu() ? "si (autotest correcto)" : "NO (autotest fallido, se usa la CPU)");
    }
    /* texturas que si no se leerian de la Memory Stick en plena carrera (tiron de 50-130 ms en la consola) */
    r_preload_texture("enemies");
    r_preload_texture("jetpackSmoke");
    audio_init();
    scene_init();
    game_load();
    game_init();
    mem_query(&dbg_mem);
    dlog_boot_done();
    dlog("Arranque completo en %.2f s | monton %u/%u KB usado | libre fuera del monton %u KB",
         (sceKernelGetSystemTimeLow() - tl) / 1000000.0f, dbg_mem.heap_used / 1024, dbg_mem.heap_total / 1024,
         dbg_mem.sys_free / 1024);
    dlog_flush();

    unsigned int prev_buttons = 0;
    int stick_state = 0;
    int show_perf = 0;
#ifdef AUTOTEST
    show_perf = 1;
    int frame_no = 0;
#endif
    unsigned int last = sceKernelGetSystemTimeLow();
    float fps = 30, cpu_ms = 0, gpu_ms = 0;
    while (running) {
        unsigned int t0 = sceKernelGetSystemTimeLow();
        float dt = (t0 - last) / 1000000.0f;
        last = t0;
        if (dt <= 0 || dt > 0.25f) dt = 1.0f / 30.0f;
        fps = fps * 0.9f + (1.0f / dt) * 0.1f;

        DBG_PHASE(main, "leyendo los botones");
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int b = pad.Buttons, pr = b & ~prev_buttons;
        prev_buttons = b;
        /* el stick analogico funciona como un deslizamiento del dedo */
        int ax = pad.Lx - 128, ay = pad.Ly - 128, sdir = 0;
        if (ax < -80) sdir = 1; else if (ax > 80) sdir = 2; else if (ay < -80) sdir = 3; else if (ay > 80) sdir = 4;
        int sl = 0, sr = 0, su = 0, sd = 0;
        if (sdir && sdir != stick_state) { sl = sdir == 1; sr = sdir == 2; su = sdir == 3; sd = sdir == 4; }
        if (!sdir || (abs(ax) < 40 && abs(ay) < 40)) stick_state = 0; else stick_state = sdir;
        /* SELECT: resumen -> memoria -> grafica de tiempos -> oculto */
        if (pr & PSP_CTRL_SELECT) show_perf = (show_perf + 1) & 3;
#ifdef AUTOTEST
        /* juego automatico para probar en el emulador sin mandos */
        frame_no++;
#ifdef AT_HANG
        if (frame_no == 400) {   /* prueba del vigilante: el bucle principal se queda parado 5 s */
            DBG_PHASE(main, "prueba de bloqueo (AT_HANG)");
            for (int k = 0; k < 50; k++) sceKernelDelayThread(100000);
        }
#endif
#ifdef AT_UNALIGN
        if (frame_no == 300) { static int buf[8]; static volatile int off = 2; volatile int *bad = (volatile int *)((char *)buf + off); dlog("leyendo %d", *bad); }
#endif
#ifdef AT_BADMEM
        if (frame_no == 300) { volatile int *bad = (volatile int *)0x00000010; dlog("leyendo %d", *bad); }
#endif
#ifdef AT_ROLL
        /* prueba de rodar: en el suelo cada 45 fotogramas y, cada 90, saltar y rodar en el aire */
        if (ginfo.state == GS_RUNNING) {
            if (frame_no % 45 == 0) pr |= PSP_CTRL_DOWN;
            if (frame_no % 90 == 20) pr |= PSP_CTRL_UP;
            if (frame_no % 90 == 26) pr |= PSP_CTRL_DOWN;
        }
#endif
#ifdef AT_PAGES
        if (frame_no % 600 == 0) show_perf = show_perf % 3 + 1;   /* recorre las paginas del medidor */
#endif
#ifdef AT_UI
        if (frame_no == 40) pr |= PSP_CTRL_TRIANGLE;
        if (frame_no >= 60 && frame_no < 160 && frame_no % 10 == 0) pr |= PSP_CTRL_DOWN;   /* recorre toda la tienda */
        if (frame_no == 180) pr |= PSP_CTRL_CIRCLE;
        if (frame_no == 200 || frame_no == 240) pr |= PSP_CTRL_RTRIGGER;           /* ventana de misiones */
        if (ginfo.state == GS_MENU && frame_no > 260) pr |= PSP_CTRL_CROSS;
        if (frame_no == 700) { pr |= PSP_CTRL_START; }
        if (frame_no == 740) { pr |= PSP_CTRL_START; }
#else
        {
            /* AT_MENUBACK: tras la primera carrera se queda en el menu (para ver como vuelve la camara) */
            static int ran;
            if (ginfo.state == GS_RUNNING) ran = 1;
#ifdef AT_MENUBACK
            if (ginfo.state == GS_MENU && frame_no > 60 && !ran) pr |= PSP_CTRL_CROSS;
#else
            (void)ran;
            if (ginfo.state == GS_MENU && frame_no > 60) pr |= PSP_CTRL_CROSS;
#endif
        }
#endif
        if (ginfo.state == GS_GAMEOVER) pr |= PSP_CTRL_CROSS;
        {
            extern void game_debug_powerup(int k);
            static const int at_frames[] = {500, 1100, 1600, 2100, 3000};
            for (int k = 0; k < 5; k++) if (frame_no == at_frames[k]) game_debug_powerup(k == 0 ? 0 : k == 1 ? 1 : k == 2 ? 3 : k == 3 ? 2 : 4);
        }

#endif
        unsigned in = 0;
        if ((pr & PSP_CTRL_LEFT) || sl) in |= IN_LEFT;
        if ((pr & PSP_CTRL_RIGHT) || sr) in |= IN_RIGHT;
        if ((pr & (PSP_CTRL_UP | PSP_CTRL_CROSS)) || su) in |= IN_UP;
        if ((pr & (PSP_CTRL_DOWN | PSP_CTRL_CIRCLE)) || sd) in |= IN_DOWN;
        if (pr & (PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE)) in |= IN_BOARD;
        if (pr & PSP_CTRL_START) in |= IN_START;
        if (pr & PSP_CTRL_CROSS) in |= IN_CROSS;
        if (pr & PSP_CTRL_CIRCLE) in |= IN_CIRCLE;
        if (pr & PSP_CTRL_TRIANGLE) in |= IN_TRIANGLE;
        if (pr & PSP_CTRL_SQUARE) in |= IN_SQUARE;
        if (pr & PSP_CTRL_LTRIGGER) in |= IN_L;
        if (pr & PSP_CTRL_RTRIGGER) in |= IN_R;
        DBG_PHASE(main, "game_input (menus, tienda)");
        game_input(in);

        unsigned int tu0 = sceKernelGetSystemTimeLow();
        DBG_PHASE(main, "game_update (logica, fisica, tramos)");
        game_update(dt);
        unsigned int tu1 = sceKernelGetSystemTimeLow();
        static float upd_ms;
        upd_ms = upd_ms * 0.9f + ((tu1 - tu0) / 1000.0f) * 0.1f;

#ifdef AUTOTEST
        { extern int r_mesh_verts[512]; memset(r_mesh_verts, 0, sizeof(r_mesh_verts)); }
        { extern unsigned int r_prof[8]; memset(r_prof, 0, sizeof(r_prof)); }
#endif
        DBG_PHASE(main, "game_draw (preparar el dibujo)");
#ifdef AT_CRACKS
        r_begin_frame(0xffff00ff);   /* prueba de grietas: fondo magenta */
#else
        r_begin_frame(0xfff6e6bc);   /* color de fondo de la camara (0.737, 0.902, 0.965) */
#endif
        game_draw();
        DBG_PHASE(main, "hud_draw (interfaz)");
        unsigned int th0 = sceKernelGetSystemTimeLow();
        hud_draw();
        dbg_sect_ms[7] = (sceKernelGetSystemTimeLow() - th0) / 1000.0f;
        unsigned int t1 = sceKernelGetSystemTimeLow();
        if (show_perf) perf_draw(show_perf, cpu_ms, gpu_ms, fps);
        r_end_frame();
        cpu_ms = cpu_ms * 0.9f + ((t1 - t0) / 1000.0f) * 0.1f;
        {
            extern float phys_ms;
            dbg_upd_ms = (tu1 - tu0) / 1000.0f;
            dbg_draw_ms = (t1 - tu1) / 1000.0f;
            dbg_phys_ms = phys_ms;
            phys_ms = 0;
        }
        dbg_frame(dt * 1000.0f, (t1 - t0) / 1000.0f, r_wait_ms);
#ifdef AUTOTEST
        static float cpu_max;
        if ((t1 - t0) / 1000.0f > cpu_max) cpu_max = (t1 - t0) / 1000.0f;
        if (frame_no % 300 == 1) cpu_max = 0;
        if (frame_no % 300 == 0) { FILE *f = fopen("perf.log", "a"); if (f) { fprintf(f, "   pico CPU %.2f ms\n", cpu_max); fclose(f); } }
#endif
        gpu_ms = gpu_ms * 0.9f + r_wait_ms * 0.1f;
#ifdef AUTOTEST
#ifndef AT_SHOT
#define AT_SHOT 150
#endif
        if (frame_no % AT_SHOT == 0) {
            void *top; int bw, fmt;
            r_present_last();
            sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME);
            char fn[64];
            snprintf(fn, sizeof(fn), "shot%06d.raw", frame_no);
            FILE *f = fopen(fn, "wb");
            if (f) {
                unsigned short *src = (unsigned short *)((unsigned)top | 0x40000000);
                for (int y = 0; y < SCR_H; y++) fwrite(src + y * bw, 2, SCR_W, f);
                fclose(f);
            }
        }
        if (frame_no % 300 == 0) {
            FILE *f = fopen("perf.log", "a");
            if (f) {
                extern int r_using_vfpu(void);
                fprintf(f, "update %.2f ms vfpu %d\n", upd_ms, r_using_vfpu());
                fprintf(f, "hw verts %d\n", r_stat_hw);
                fprintf(f, "frame %d state %d fps %.1f cpu %.2f espera_GE+vsync %.2f verts %d tris %d draws %d clip %d heap %u KB z %.0f score %d\n",
                        frame_no, ginfo.state, fps, cpu_ms, gpu_ms, r_stat_verts, r_stat_tris, r_stat_draws, r_stat_clipped,
                        heap_used_kb(), (double)scene_char_z, ginfo.score);
                extern unsigned int r_prof[8];
                fprintf(f, "   prof: rapido %.2f recorte %.2f piel %.2f culling %.2f ms\n", r_prof[0] / 1000.0f, r_prof[1] / 1000.0f,
                        r_prof[2] / 1000.0f, r_prof[3] / 1000.0f);
                extern int r_mesh_verts[512];
                for (int k = 0; k < 8; k++) {
                    int best = 0;
                    for (int m = 0; m < gd.nmeshes && m < 512; m++) if (r_mesh_verts[m] > r_mesh_verts[best]) best = m;
                    if (r_mesh_verts[best] <= 0) break;
                    fprintf(f, "   %s %d\n", gd.meshes[best].name, r_mesh_verts[best]);
                    r_mesh_verts[best] = -r_mesh_verts[best];
                }
                fclose(f);
            }
        }
        /* lo que tarda escribir capturas y perf.log no es del juego: no cuenta como tiron */
        if (frame_no % AT_SHOT == 0 || frame_no % 300 == 0) last = sceKernelGetSystemTimeLow();
#endif
    }
    dlog("Salida desde el menu HOME");
    game_save();
    dlog_close();
    audio_shutdown();
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
