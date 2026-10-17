/* exhandler.prx - captura las excepciones de la CPU de la PSP (lo que en la consola real congela todo) y escribe
 * en crash.txt donde fallo: tipo de excepcion, direccion (EPC), direccion de memoria (BadVAddr), hilo, fase de
 * cada hilo del juego, registros y pila de llamadas. Con subway.elf, tools/crash.sh traduce las direcciones a
 * funcion y linea del codigo. Es un modulo de kernel: solo funciona en firmware personalizado (CFW). */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspthreadman_kernel.h>
#include <string.h>
#include "exhandler.h"

PSP_MODULE_INFO("SSExHandler", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

static ExhArgs cfg;
static char out[6144];
static int olen;

static void put(const char *s)
{
    while (*s && olen < (int)sizeof(out) - 1) out[olen++] = *s++;
    out[olen] = 0;
}

static void hex(unsigned int v)
{
    char b[9];
    for (int i = 7; i >= 0; i--) { int d = v & 15; b[i] = d < 10 ? '0' + d : 'A' + d - 10; v >>= 4; }
    b[8] = 0;
    put(b);
}

static void dec(int v)
{
    char b[12]; int n = 0;
    if (v < 0) { put("-"); v = -v; }
    do { b[n++] = '0' + v % 10; v /= 10; } while (v && n < 11);
    char r[12];
    for (int i = 0; i < n; i++) r[i] = b[n - 1 - i];
    r[n] = 0;
    put(r);
}

static const char *exc_name(int c)
{
    switch (c) {
    case 4: return "AdEL: lectura en direccion desalineada o prohibida / unaligned or invalid load";
    case 5: return "AdES: escritura en direccion desalineada o prohibida / unaligned or invalid store";
    case 6: return "IBE: error de bus al leer instruccion / instruction bus error";
    case 7: return "DBE: error de bus de datos / data bus error";
    case 9: return "Bp: instruccion break (division por cero?) / break";
    case 10: return "RI: instruccion no valida / reserved instruction";
    case 11: return "CpU: coprocesador no disponible (FPU/VFPU) / coprocessor unusable";
    case 12: return "Ov: desbordamiento aritmetico / overflow";
    case 13: return "Tr: trap";
    case 15: return "FPE: excepcion de coma flotante / floating point";
    default: return "?";
    }
}

static const char *safe_str(volatile const char **pp)
{
    if (!pp) return "?";
    const char *p = (const char *)*pp;
    unsigned int a = (unsigned int)p & 0x3FFFFFFF;
    if (a < 0x08800000 || a >= 0x0C000000) return "?";
    return p;
}

static void write_file(const char *path, int flags)
{
    if (!path[0]) return;
    SceUID fd = sceIoOpen(path, flags, 0777);
    if (fd >= 0) { sceIoWrite(fd, out, olen); sceIoClose(fd); }
}

static void handler(PspDebugRegBlock *r)
{
    static const char *names[32] = {"zr", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4",
                                    "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9",
                                    "k0", "k1", "gp", "sp", "fp", "ra"};
    olen = 0;
    int code = (r->cause >> 2) & 31;
    put("\r\n===== CRASH / EXCEPCION DE LA CPU =====\r\n");
    put("Tipo: "); dec(code); put(" - "); put(exc_name(code));
    if (code == 11) { put(" (coprocesador "); dec((r->cause >> 28) & 3); put(")"); }
    put("\r\nEPC (instruccion que fallo): 0x"); hex(r->epc);
    put("\r\nBadVAddr (direccion de memoria): 0x"); hex(r->badvaddr);
    put("\r\nRA (volver a): 0x"); hex(r->r[31]); put("   SP: 0x"); hex(r->r[29]);
    put("\r\nCause 0x"); hex(r->cause); put("  Status 0x"); hex(r->status);
    unsigned int epc = r->epc;
    if ((epc & 3) == 0 && (epc & 0x3FFFFFFF) >= 0x08800000 && (epc & 0x3FFFFFFF) < 0x0C000000) {
        put("  instruccion 0x"); hex(*(volatile unsigned int *)epc);
    }
    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    SceUID th = sceKernelGetThreadId();
    put("\r\nHilo: ");
    if (sceKernelReferThreadStatus(th, &ti) >= 0) put(ti.name); else put("?");
    put("\r\nFase del juego: principal = "); put(safe_str(cfg.phase_main));
    put(" | musica = "); put(safe_str(cfg.phase_music));
    put(" | mezclador = "); put(safe_str(cfg.phase_mixer));
    put("\r\nRegistros:\r\n");
    for (int i = 0; i < 32; i++) {
        put(names[i]); put("="); hex(r->r[i]); put((i & 3) == 3 ? "\r\n" : "  ");
    }
    PspDebugStackTrace tr[16];
    int n = pspDebugGetStackTrace2(r, tr, 16);
    put("Pila de llamadas (llamada desde -> funcion):\r\n");
    for (int i = 0; i < n; i++) { put("  0x"); hex(tr[i].call_addr); put(" -> 0x"); hex(tr[i].func_addr); put("\r\n"); }
    put("Pila (16 palabras desde SP):\r\n");
    unsigned int sp = r->r[29];
    if ((sp & 3) == 0 && (sp & 0x3FFFFFFF) >= 0x08800000 && (sp & 0x3FFFFFFF) < 0x0C000000 - 64)
        for (int i = 0; i < 16; i++) { hex(((volatile unsigned int *)sp)[i]); put((i & 3) == 3 ? "\r\n" : " "); }
    put("=======================================\r\n");
    write_file(cfg.crash_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC);
    write_file(cfg.devlog_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND);
    pspDebugScreenInit();
    pspDebugScreenPuts("Subway Surfers PSP has crashed / se ha colgado\n");
    pspDebugScreenPuts(out);
    pspDebugScreenPuts("\nSend crash.txt and devlog.txt / Envia crash.txt y devlog.txt\n");
    pspDebugScreenPuts("Hold POWER to turn off / Manten POWER para apagar\n");
    for (;;) sceKernelDelayThread(1000000);
}

/* la libc del kernel no trae vsnprintf y la pantalla de depuracion la referencia; aqui solo se usa
 * pspDebugScreenPuts con texto ya formateado, asi que basta con copiar la cadena */
#include <stdarg.h>
int vsnprintf(char *buf, unsigned int n, const char *fmt, va_list ap)
{
    unsigned int i = 0;
    (void)ap;
    if (!n) return 0;
    while (fmt[i] && i < n - 1) { buf[i] = fmt[i]; i++; }
    buf[i] = 0;
    return i;
}

int module_start(SceSize args, void *argp)
{
    memset(&cfg, 0, sizeof(cfg));
    if (argp && args >= sizeof(cfg)) memcpy(&cfg, argp, sizeof(cfg));
    return pspDebugInstallErrorHandler(handler) < 0 ? 1 : 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
