/* render.h - dibujo 3D con transformacion por software (curvatura del mundo del shader "Distorted") */
#ifndef RENDER_H
#define RENDER_H
#include "vmath.h"

#define SCR_W 480
#define SCR_H 272

void r_init(void);
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
/* camara actual (para culling y para la logica) */
extern M34 r_view;
extern V3 r_cam_pos;
extern float r_near, r_far, r_fov;

/* 2D (pantalla, en pixeles) */
void r_begin_2d(void);
void r_sprite(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned int abgr);
void r_rect(float x, float y, float w, float h, unsigned int abgr);
void r_bind_texture(int tex);

#endif
