/* relaunch.prx - modulo de KERNEL que vuelve a lanzar el EBOOT del juego al cambiar de mapa.
 * argp: la ruta del EBOOT (cadena terminada en 0; el juego le pasa un ExhArgs con la ruta en crash_path, que va al
 * principio, para poder cargarlo tambien con exloader.prx y kubridge como exhandler.prx).
 * La carga se hace desde un hilo, despues de module_start: dentro de module_start el gestor de modulos esta ocupado.
 * El CFW (sctrlKernelLoadExecVSHMs2 / Ef2 en la memoria interna de la PSP Go) descarga el juego entero y lo vuelve a
 * cargar desde cero, como al abrirlo desde el menu */
#include <pspkernel.h>
#include <psploadexec_kernel.h>
#include <systemctrl.h>
#include <string.h>

PSP_MODULE_INFO("SSRelaunch", PSP_MODULE_KERNEL, 1, 0);

static char path[192];
volatile int relaunch_result = 1;   /* 1: aun no; el error si el CFW no lo deja */

static int relaunch_thread(SceSize args, void *argp)
{
    struct SceKernelLoadExecVSHParam p;
    memset(&p, 0, sizeof(p));
    p.size = sizeof(p);
    p.args = strlen(path) + 1;
    p.argp = path;
    p.key = "game";
    int r;
    if (!strncmp(path, "ef0", 3)) r = sctrlKernelLoadExecVSHEf2(path, &p);   /* PSP Go: memoria interna */
    else r = sctrlKernelLoadExecVSHMs2(path, &p);
    relaunch_result = r;   /* solo llega aqui si no ha funcionado */
    return 0;
}

int module_start(SceSize args, void *argp)
{
    if (!argp || args < 4) return 1;
    strncpy(path, (const char *)argp, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if (!path[0]) return 1;
    SceUID th = sceKernelCreateThread("SSRelaunch", relaunch_thread, 0x10, 0x2000, 0, NULL);
    if (th < 0) return 1;
    sceKernelStartThread(th, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
