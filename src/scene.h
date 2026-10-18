/* scene.h - estado en tiempo de ejecucion de la escena y la pista (Track, TrackChunk, monedas...) */
#ifndef SCENE_H
#define SCENE_H
#include "vmath.h"
#include "data.h"

typedef struct {
    V3 lpos; Q4 lrot; V3 lscl;
    M34 world;
    uint8_t active;      /* GameObject.active (en Unity 3.x no es jerarquico) */
    uint8_t rend;        /* renderer.enabled */
    uint8_t col;         /* collider.enabled */
    uint8_t hidden;      /* ocultado por la tabla (hoverboard) */
    uint8_t taken;       /* moneda suelta (Coin de la escena) ya recogida: ni se dibuja ni choca */
    float rot_phase;     /* PickupRotate */
    uint8_t rotating;
    uint8_t picked;
    int16_t letter_mesh; /* DailyLetterPickup: malla elegida */
} NodeRT;

extern NodeRT *nrt;

/* ---- escena ---- */
void scene_init(void);
void scene_update_world(int node);               /* recalcula el mundo de un subarbol */
void scene_set_active_recursive(int node, int on);
void scene_draw_subtree(int node);               /* dibuja nodos activos con renderer activo */
static inline const M34 *node_world(int n) { return &nrt[n].world; }
static inline V3 node_pos(int n) { return m34_pos(&nrt[n].world); }

/* ---- pista ---- */
#define MAX_ACTIVE_CHUNKS 16
extern int track_active[MAX_ACTIVE_CHUNKS];
extern int track_nactive;
extern float track_spacing;
extern int track_tutorial, track_running_tutorial;
void track_init(void);
void track_restart(void);
void track_lay_chunks(float charZ, float ahead, int jetpack);
float track_lay_jetpack_chunks(float charZ, float flyLength);
void track_deactivate_all(void);
void track_remove_obstacles(float maxZ);
float track_get_x(int idx);
V3 track_pos(float x, float z);
float track_last_checkpoint(float charZ);
float track_ahead_distance(void);

/* ---- monedas (CoinPool) ---- */
typedef struct {
    V3 pos;               /* raiz de la moneda */
    V3 pivot;             /* desplazamiento del pivote (cambia al atraerla el iman) */
    int active, collider, visible;
    int owner;            /* nodo que la genero */
    int moving;           /* MovingCoin: velocidad */
    float rot_z;
    /* atraccion del iman / zapatillas */
    int pulling;
    float pull_t, pull_dur;
    V3 pull_from;
    /* posicion relativa al personaje en el fotograma anterior (para no saltarse monedas rapidas) */
    V3 prel;
    int prel_ok;
} CoinRT;

#define MAX_COINS 600
extern CoinRT coins[MAX_COINS];
extern int coin_mesh_node, coin_glow_node;
int coin_spawn(V3 pos, int owner);
void coin_release_owner(int owner);
void coins_release_inair(void);
void coins_draw(void);
void coins_update(float dt);

/* llamados por la logica del juego */
extern float (*scene_jump_length)(float speed, float height);
extern float scene_jump_height_normal, scene_jump_height_super;
extern float scene_current_level_speed;
extern float scene_char_z;

/* MovingTrain / MovingCoin activos */
void moving_update(float charZ, float dt, float speed);
void moving_autopilot(float charZ);
/* PickupRotate */
void pickups_update(float t);

/* hoverboard: grupos de las super zapatillas */
void scene_ssgroups(int on);
void scene_glow_visible(int glow, int vis);

/* llamado por la pista cuando un tramo pide algo al juego */
void scene_on_chunk_laid(int chunk);

#endif
