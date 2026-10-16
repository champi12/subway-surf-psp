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
    pspDebugScreenPrintf("Hay que generar la carpeta data/ con tools/extract.py\n");
    pspDebugScreenPrintf("a partir del APK original (ver README.md).\n");
    while (running) sceDisplayWaitVblankStart();
    sceKernelExitGame();
}

extern void game_load(void);
unsigned int heap_used_kb(void) { struct mallinfo mi = mallinfo(); return mi.uordblks / 1024; }

int main(int argc, char **argv)
{
    mem_init();
    SceUID th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    int clk = scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    rng_state ^= sceKernelGetSystemTimeLow();
    dlog_init();
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
    audio_init();
    scene_init();
    game_load();
    game_init();
    mem_query(&dbg_mem);
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
        if (ginfo.state == GS_MENU && frame_no > 60) pr |= PSP_CTRL_CROSS;
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
        game_input(in);

        unsigned int tu0 = sceKernelGetSystemTimeLow();
        game_update(dt);
        unsigned int tu1 = sceKernelGetSystemTimeLow();
        static float upd_ms;
        upd_ms = upd_ms * 0.9f + ((tu1 - tu0) / 1000.0f) * 0.1f;

#ifdef AUTOTEST
        { extern int r_mesh_verts[512]; memset(r_mesh_verts, 0, sizeof(r_mesh_verts)); }
        { extern unsigned int r_prof[8]; memset(r_prof, 0, sizeof(r_prof)); }
#endif
        r_begin_frame(0xfff6e6bc);   /* color de fondo de la camara (0.737, 0.902, 0.965) */
        game_draw();
        hud_draw();
        unsigned int t1 = sceKernelGetSystemTimeLow();
        if (show_perf) perf_draw(show_perf, cpu_ms, gpu_ms, fps);
        r_end_frame();
        cpu_ms = cpu_ms * 0.9f + ((t1 - t0) / 1000.0f) * 0.1f;
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
