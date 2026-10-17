/* exhandler.h - argumentos que el juego pasa al modulo capturador de excepciones (exhandler.prx) */
#ifndef EXHANDLER_H
#define EXHANDLER_H

typedef struct {
    char crash_path[192];                    /* crash.txt (se crea de nuevo) */
    char devlog_path[192];                   /* devlog.txt (se anade al final) */
    volatile const char **phase_main;        /* fase de cada hilo del juego (devlog.h) */
    volatile const char **phase_music;
    volatile const char **phase_mixer;
} ExhArgs;

/* argumentos de exloader.prx (plan B con kubridge) */
typedef struct {
    char prx_path[192];      /* ruta de exhandler.prx */
    ExhArgs exh;             /* lo que recibira exhandler.prx */
    int *result;             /* resultado: carga, inicio, estado */
    /* la rellena exloader: el juego la llama DESPUES de arrancarlo (cargar otro modulo dentro de module_start
     * bloquea el gestor de modulos) */
    void (*load)(void *self);
} ExLoaderArgs;

#endif
