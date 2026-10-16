/* physics.h - colisiones: cajas orientadas, esferas y capsulas de la escena, CharacterController y triggers */
#ifndef PHYSICS_H
#define PHYSICS_H
#include "vmath.h"

enum { COL_BOX, COL_SPHERE, COL_CAPSULE };

typedef struct {
    int node, type, trigger, layer, tag;
    V3 c, ax[3], he;          /* caja: centro, ejes unitarios y semiejes */
    V3 p0, p1; float r;       /* esfera (p0) / capsula (segmento p0-p1) */
    V3 bmin, bmax;            /* AABB de mundo (Collider.bounds) */
} Col;

#define MAX_COLS 2048
extern Col cols[MAX_COLS];
extern int ncols;

/* reconstruye la lista de colliders de los tramos activos y de los nodos fijos */
void phys_build(const int *extra_roots, int nextra);
int phys_layer_hits(int layer);   /* colisiona con la capa del personaje (11)? */

typedef struct {
    V3 pos;                 /* transform.position (pies) */
    float height, radius, step, slope, skin;
    V3 center;
    int grounded, enabled, detect;
    int last_ground_col;    /* collider sobre el que se apoya */
} CharCtrl;

void cc_move(CharCtrl *cc, V3 motion);

/* capsula (trigger) contra un collider: 1 si se solapan */
int capsule_overlaps(V3 a, V3 b, float r, const Col *c);
/* rayo hacia abajo: devuelve el indice del collider o -1 */
int phys_raycast_down(V3 from, float maxd, float *dist, int include_triggers);
/* distancia entre punto y caja */
int sphere_vs_col(V3 p, float r, const Col *c, V3 *n, float *depth);

#endif
