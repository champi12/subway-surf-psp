/* anim.h - reimplementacion del componente Animation (legacy) de Unity 3.5 */
#ifndef ANIM_H
#define ANIM_H
#include "vmath.h"

#define ANIM_MAX_STATES 72
#define ANIM_MAX_QUEUE 4

typedef struct {
    int clip;
    float time, speed, weight, target_weight, fade_rate;
    int enabled, layer, wrap;
    int mix_root;          /* AddMixingTransform (-1 = todo el esqueleto) */
    int stop_when_faded;
    int *track_node;       /* nodo de cada pista del clip */
    unsigned char *track_mask;
} AnimState;

typedef struct { int state; float fade; int crossfade; } AnimQueued;

typedef struct Animator {
    int root;              /* nodo con el componente Animation */
    int default_wrap;
    int nstates;
    AnimState st[ANIM_MAX_STATES];
    AnimQueued queue[ANIM_MAX_QUEUE];
    int nqueue;
    /* huesos afectados (para mezclar) */
    int nbones;
    int *bones;
    V3 *bind_pos; Q4 *bind_rot; V3 *bind_scl;
    void (*on_event)(struct Animator *a, int state, float t);  /* para sonidos de pasos, etc. */
    float ev_times[64]; int ev_clip[64]; int nev;               /* eventos registrados */
    void *user;
} Animator;

void anim_init(Animator *a, int root);
int anim_find(Animator *a, const char *name);      /* indice de estado o -1 */
AnimState *anim_get(Animator *a, const char *name);
void anim_play(Animator *a, const char *name);
void anim_crossfade(Animator *a, const char *name, float fade);
void anim_play_queued(Animator *a, const char *name);
void anim_crossfade_queued(Animator *a, const char *name, float fade);
void anim_stop(Animator *a, const char *name);
void anim_stop_all(Animator *a);
int anim_is_playing(Animator *a, const char *name);
float anim_length(Animator *a, const char *name);
void anim_add_mixing(Animator *a, const char *name, int node);
void anim_add_event(Animator *a, const char *clip, float time);
void anim_update(Animator *a, float dt);
void anim_apply(Animator *a);                        /* escribe las transformaciones locales de los huesos */

#endif
