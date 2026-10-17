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

#endif
