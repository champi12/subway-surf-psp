/* render.h - dibujo 3D con transformacion por software (curvatura del mundo del shader "Distorted") */
#ifndef RENDER_H
#define RENDER_H
#include "vmath.h"

#define SCR_W 480
#define SCR_H 272

void r_init(void);
void r_init_materials(void);
void r_tex_reload(int t);   /* cambio de tema: vuelve a leer una textura ya cargada */
void r_tex_unload(int t);
void r_wait_idle(void);     /* espera al GE (antes de liberar algo que este dibujando) */   /* libera una textura de la RAM (se vuelve a leer si se usa) */
void r_begin_frame(unsigned int clear_abgr);
void r_end_frame(void);
void r_present_last(void);   /* espera al GE y muestra el ultimo fotograma (capturas) */
extern float r_wait_ms;
void r_set_camera(V3 pos, V3 target, float fov_deg);
void r_set_camera_matrix(const M34 *cam_world, float fov_deg);
/* dibuja una malla con su matriz de mundo; mats[] por submalla (-1 = no dibujar) */
void r_draw_mesh(int mesh, const M34 *world, const int *mats, int nmats, float alpha_mul);
/* malla con piel: bones[] son las matrices finales (mundo * bindpose) */
void r_draw_skinned(int mesh, const M34 *bones, int nbones, const int *mats, int nmats);
/* sombra/plano proyectado en el suelo (quad) */
void r_flush_transparent(void);
/* estadisticas del ultimo frame */
extern int r_stat_verts, r_stat_tris, r_stat_draws, r_stat_clipped, r_stat_hw;
extern int r_stat_skin_cpu;   /* vertices de personajes con la piel calculada en la CPU (los caros) */
/* uso de memoria del dibujo (pantalla de depuracion y devlog) */
typedef struct {
    int vram_fb;                 /* 2 framebuffers + Z (fijo) */
    int vram_tex, vram_tex_n;    /* texturas copiadas a la VRAM */
    int vram_full;               /* texturas que no cupieron en la VRAM */
    int ram_tex, ram_tex_n;      /* texturas cargadas en la RAM principal */
    int dlist_used, dlist_peak, dlist_max;   /* lista de comandos del GE (bytes) */
    int vring_used, vring_peak, vring_max;   /* buffer de vertices del fotograma */
    int iring_used, iring_peak, iring_max;   /* buffer de indices */
    int dropped;                 /* dibujos descartados este fotograma por falta de espacio */
    int frame_texloads;          /* texturas leidas de la Memory Stick este fotograma */
} RMem;
extern RMem r_mem;
#define R_VRAM_TOTAL 0x200000

/* camara actual (para culling y para la logica) */
extern M34 r_view;
extern V3 r_cam_pos;
extern float r_near, r_far, r_fov;
extern float r_fog_start, r_fog_end;
extern unsigned int r_fog_color;     /* ABGR */
extern unsigned int r_clear_color;   /* ABGR; color de fondo (solo asoma por las juntas) */

/* 2D (pantalla, en pixeles) */
void r_begin_2d(void);
void r_sprite(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned int abgr);
void r_background(int tex);   /* fondo a pantalla completa antes del 3D */
void r_rect(float x, float y, float w, float h, unsigned int abgr);
void r_sprites(int tex, const float *quads, int n, unsigned int abgr);   /* n x (x, y, w, h, u0, v0, u1, v1) */
void r_bind_texture(int tex);
void r_set_nearest(int on);   /* filtro sin interpolar (texto 1:1) */
void r_preload_texture(const char *name);

#endif
