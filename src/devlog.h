/* devlog.h - registro de desarrollo en la Memory Stick (devlog.txt) y medicion de memoria
 *
 * Escribe en ms0:/PSP/GAME/<carpeta>/devlog.txt (el de la partida anterior queda como devlog_prev.txt):
 *   - al arrancar: firmware, reloj de la CPU, bateria, RAM, VRAM y tiempo de carga de cada archivo
 *   - cada 5 s: FPS, ms de CPU y de GPU, vertices, memoria, VRAM, buffers del GE, bateria
 *   - eventos: cambios de estado, muertes, texturas cargadas en plena partida, tirones, avisos y errores
 * Escribe un hilo de baja prioridad cada 5 s (o al momento si hay un error) para no provocar tirones.
 * Viene activado; "make DEVLOG=0" lo quita del todo.
 */
#ifndef DEVLOG_H
#define DEVLOG_H

typedef struct {
    unsigned heap_total;     /* tamano del monton de C (bytes) */
    unsigned heap_used;      /* ocupado ahora */
    unsigned heap_peak;      /* maximo visto */
    unsigned sys_free;       /* memoria libre del sistema fuera del monton (pilas de hilos, audio...) */
    unsigned sys_max_block;  /* bloque libre mas grande fuera del monton */
} MemInfo;

/* datos para la pantalla de depuracion (se actualizan en dbg_frame) */
extern MemInfo dbg_mem;
extern int dbg_cpu_mhz, dbg_bus_mhz, dbg_bat_pct, dbg_bat_temp, dbg_plugged;
extern float dbg_frame_hist[128];   /* ms de cada fotograma (grafica) */
extern int dbg_hist_pos;
extern int dbg_hitches;             /* fotogramas de mas de 50 ms desde el arranque */

void mem_init(void);                /* lo primero en main: mide el tamano real del monton */
void mem_query(MemInfo *m);
/* una vez por fotograma: frame_ms = tiempo real, cpu_ms = trabajo de la CPU, wait_ms = espera al GE + VBlank */
void dbg_frame(float frame_ms, float cpu_ms, float wait_ms);

#ifndef DEVLOG
#define DEVLOG 1
#endif

#if DEVLOG
void dlog_init(void);
void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dlog_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* se escribe al momento */
void dlog_flush(void);
void dlog_close(void);
#else
#define dlog_init() ((void)0)
#define dlog(...) ((void)0)
#define dlog_error(...) ((void)0)
#define dlog_flush() ((void)0)
#define dlog_close() ((void)0)
#endif

#endif
