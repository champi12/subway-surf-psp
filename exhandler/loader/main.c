/* exloader.prx - modulo de USUARIO que carga exhandler.prx (kernel) con kubridge, para los CFW que no dejan cargar
 * modulos de kernel con sceKernelLoadModule desde un juego. Va aparte: si el CFW no tiene kubridge, solo falla la
 * carga de este modulo y el juego sigue.
 * Sin el arranque de pspsdk (-nostartfiles): su main() termina con exit(), que descarga el modulo en vez de volver
 * de module_start y deja al juego esperando. Aqui module_start solo anota la funcion de carga y vuelve; el juego la
 * llama despues (cargar otro modulo dentro de module_start bloquearia el gestor de modulos). */
#include <pspkernel.h>
#include <kubridge.h>
#include "../exhandler.h"

PSP_MODULE_INFO("SSExLoader", 0, 1, 0);

static void load_exhandler(void *self)
{
    ExLoaderArgs *a = (ExLoaderArgs *)self;
    SceUID m = kuKernelLoadModule(a->prx_path, 0, NULL);
    if (a->result) a->result[0] = m;
    if (m < 0) return;
    int st = 0;
    int r = sceKernelStartModule(m, sizeof(a->exh), &a->exh, &st, NULL);
    if (a->result) { a->result[1] = r; a->result[2] = st; }
}

/* argp: copia de un puntero a ExLoaderArgs del juego (mismo espacio de memoria de usuario) */
int module_start(SceSize args, void *argp)
{
    if (args < sizeof(void *) || !argp) return 1;
    ExLoaderArgs *a = *(ExLoaderArgs **)argp;
    if (!a) return 1;
    a->load = load_exhandler;
    return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
