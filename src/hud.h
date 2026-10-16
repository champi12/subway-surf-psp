/* hud.h - interfaz con los atlas y fuentes NGUI del juego original */
#ifndef HUD_H
#define HUD_H

int ui_load(const char *dir);
int ui_font(const char *name);
int ui_atlas(const char *name);
/* dibuja texto; align 0 izq, 1 centro, 2 der. scale 1 = tamano original del movil */
float ui_text(int font, float x, float y, float scale, unsigned int abgr, int align, const char *s);
float ui_text_width(int font, float scale, const char *s);
/* sprite de un atlas por nombre; w/h <= 0 usa el tamano del sprite * scale */
int ui_sprite(int atlas, const char *name, float x, float y, float scale, unsigned int abgr);

void hud_draw(void);
void perf_draw(float cpu_ms, float gpu_ms, float fps, unsigned int free_kb, int verts, int tris, int draws, int clipped);

#endif
