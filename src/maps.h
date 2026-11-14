/* maps.h - selector de mapas (ajustes): la lista de mapas, la eleccion guardada (mapa.cfg) y el reinicio del juego */
#ifndef MAPS_H
#define MAPS_H

enum { MAP_THEME_NONE = 0, MAP_THEME_HALLOWEEN = 1, MAP_THEME_XMAS = 2 };

typedef struct {
    const char *id;         /* en mapa.cfg y en la miniatura (map_<id> en maps_tex.bin) */
    const char *dir;        /* carpeta del mundo */
    int theme;              /* tema que lleva siempre (Halloween o Navidad) */
    int world;              /* game_world de ese mundo */
    const char *es, *en;    /* nombre */
    const char *ver;        /* version del juego de la que sale */
    const char *need;       /* archivo que tiene que existir (ademas de dir/world.bin), o NULL */
    const char *thumb;      /* su miniatura, en la carpeta del mapa (sin ella no sale en el selector) */
} MapDef;

extern const MapDef maps_def[];
extern const int maps_n;

extern int map_forced;          /* mapa elegido en el selector y en uso (-1: ninguno, el de la fecha) */
extern int map_theme_force;     /* -1: el tema por la fecha; si no, MAP_THEME_* del mapa elegido */

int maps_available(int i);              /* 1 si el mapa y su miniatura estan en la Memory Stick */
void maps_init_thumbs(void);            /* anade las miniaturas de los mapas que hay (antes de r_init) */
int maps_cfg_read(void);                /* indice del mapa guardado en mapa.cfg, o -1 */
int maps_cfg_write(int i);              /* guarda la eleccion: 0 bien */
int maps_current(void);                 /* indice del mapa que se esta jugando (por el mundo y el tema) */
const char *maps_name(int i);           /* en el idioma de la consola */

void maps_set_eboot(const char *path);  /* argv[0]: para volver a lanzar el juego */
void maps_request_restart(int i);       /* guarda i y pide al bucle principal que reinicie */
int maps_restart_pending(void);
/* vuelve a lanzar el EBOOT (la PSP descarga el juego entero de la memoria y lo carga de cero). Solo vuelve si la
 * consola no lo deja (devuelve el error) */
int maps_restart_now(void);

#endif
