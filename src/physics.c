/* physics.c - sustituto ligero de PhysX para lo que usa Subway Surfers:
 * - CharacterController.Move: capsula que se desliza contra cajas (incluidas las giradas de las rampas)
 * - OnTriggerEnter/Exit de la capsula del personaje
 * - Physics.Raycast hacia abajo (etiqueta del suelo, saltos sobre trenes)
 */
#include <string.h>
#include "devlog.h"
#include "physics.h"
#include "scene.h"

Col cols[MAX_COLS];
int ncols;

/* matriz de colisiones de capas del juego (fila 11 = Character) */
int phys_layer_hits(int layer)
{
    static const unsigned mask = (1u << 0) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 10) |
                                 (1u << 13) | (1u << 16) | (1u << 17);
    return layer >= 0 && layer < 32 && (mask >> layer) & 1;
}

static void aabb_from_box(Col *c)
{
    V3 e = v3(0, 0, 0);
    for (int i = 0; i < 3; i++) {
        V3 a = v3scale(c->ax[i], (&c->he.x)[i]);
        e.x += fabsf(a.x); e.y += fabsf(a.y); e.z += fabsf(a.z);
    }
    c->bmin = v3sub(c->c, e);
    c->bmax = v3add(c->c, e);
}

static void add_node_cols(int n)
{
    const NodeRT *r = &nrt[n];
    if (!r->active || !r->col || r->hidden || ncols >= MAX_COLS) return;
    const uint32_t *b;
    const M34 *w = &r->world;
    if ((b = node_comp(n, C_BOX, NULL)) && b[1]) {
        Col *c = &cols[ncols++];
        c->node = n; c->type = COL_BOX; c->trigger = b[0];
        c->layer = gd.nodes[n].layer; c->tag = gd.nodes[n].tag;
        c->c = m34_point(w, v3(u2f(b[2]), u2f(b[3]), u2f(b[4])));
        float sz[3] = {u2f(b[5]), u2f(b[6]), u2f(b[7])};
        for (int i = 0; i < 3; i++) {
            V3 a = m34_col(w, i);
            float l = v3len(a);
            c->ax[i] = l > 1e-6f ? v3scale(a, 1.0f / l) : v3(i == 0, i == 1, i == 2);
            (&c->he.x)[i] = fabsf(sz[i] * l * 0.5f);
        }
        aabb_from_box(c);
    } else if ((b = node_comp(n, C_SPHERE, NULL)) && b[1]) {
        Col *c = &cols[ncols++];
        c->node = n; c->type = COL_SPHERE; c->trigger = b[0];
        c->layer = gd.nodes[n].layer; c->tag = gd.nodes[n].tag;
        float s = v3len(m34_col(w, 0));
        float s1 = v3len(m34_col(w, 1)), s2 = v3len(m34_col(w, 2));
        if (s1 > s) s = s1;
        if (s2 > s) s = s2;
        c->p0 = c->p1 = m34_point(w, v3(u2f(b[2]), u2f(b[3]), u2f(b[4])));
        c->r = u2f(b[5]) * s;
        c->bmin = v3sub(c->p0, v3(c->r, c->r, c->r));
        c->bmax = v3add(c->p0, v3(c->r, c->r, c->r));
    } else if ((b = node_comp(n, C_CAPSULE, NULL)) && b[1]) {
        Col *c = &cols[ncols++];
        c->node = n; c->type = COL_CAPSULE; c->trigger = b[0];
        c->layer = gd.nodes[n].layer; c->tag = gd.nodes[n].tag;
        V3 ctr = v3(u2f(b[2]), u2f(b[3]), u2f(b[4]));
        float rad = u2f(b[5]), h = u2f(b[6]);
        int dir = b[7];
        float half = h * 0.5f - rad;
        if (half < 0) half = 0;
        V3 d = v3(dir == 0, dir == 1, dir == 2);
        c->p0 = m34_point(w, v3sub(ctr, v3scale(d, half)));
        c->p1 = m34_point(w, v3add(ctr, v3scale(d, half)));
        float s = v3len(m34_col(w, dir == 0 ? 1 : 0));
        c->r = rad * s;
        V3 mn = v3(fminf(c->p0.x, c->p1.x), fminf(c->p0.y, c->p1.y), fminf(c->p0.z, c->p1.z));
        V3 mx = v3(fmaxf(c->p0.x, c->p1.x), fmaxf(c->p0.y, c->p1.y), fmaxf(c->p0.z, c->p1.z));
        c->bmin = v3sub(mn, v3(c->r, c->r, c->r));
        c->bmax = v3add(mx, v3(c->r, c->r, c->r));
    }
}

static void add_subtree(int n)
{
    add_node_cols(n);
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling) add_subtree(c);
}

void phys_build(const int *extra, int nextra)
{
    ncols = 0;
    for (int i = 0; i < track_nactive; i++) {
        extern int track_chunk_node(int ci);
        add_subtree(track_chunk_node(track_active[i]));
    }
    for (int i = 0; i < nextra; i++) if (extra[i] >= 0) add_subtree(extra[i]);
    static int warned;
    if (ncols >= MAX_COLS && !warned++) dlog("AVISO demasiados colliders (%d): algunos obstaculos no chocaran", MAX_COLS);
}

/* --------- consultas geometricas --------- */
static V3 closest_on_seg(V3 a, V3 b, V3 p)
{
    V3 ab = v3sub(b, a);
    float l2 = v3dot(ab, ab);
    if (l2 < 1e-9f) return a;
    float t = clamp01(v3dot(v3sub(p, a), ab) / l2);
    return v3add(a, v3scale(ab, t));
}

/* esfera contra collider: normal de empuje (hacia fuera del collider) y profundidad */
int sphere_vs_col(V3 p, float r, const Col *c, V3 *n, float *depth)
{
    if (c->type == COL_BOX) {
        V3 d = v3sub(p, c->c);
        float l[3], q[3];
        int inside = 1;
        for (int i = 0; i < 3; i++) {
            l[i] = v3dot(d, c->ax[i]);
            float h = (&c->he.x)[i];
            q[i] = clampf(l[i], -h, h);
            if (q[i] != l[i]) inside = 0;
        }
        if (!inside) {
            V3 cp = c->c;
            for (int i = 0; i < 3; i++) cp = v3add(cp, v3scale(c->ax[i], q[i]));
            V3 dv = v3sub(p, cp);
            float dist = v3len(dv);
            if (dist >= r) return 0;
            *n = dist > 1e-6f ? v3scale(dv, 1.0f / dist) : v3(0, 1, 0);
            *depth = r - dist;
            return 1;
        }
        /* centro dentro de la caja: salir por la cara mas cercana */
        int best = 0; float bd = 1e30f;
        for (int i = 0; i < 3; i++) {
            float pen = (&c->he.x)[i] - fabsf(l[i]);
            if (pen < bd) { bd = pen; best = i; }
        }
        *n = v3scale(c->ax[best], l[best] >= 0 ? 1.0f : -1.0f);
        *depth = bd + r;
        return 1;
    }
    V3 cp = c->type == COL_SPHERE ? c->p0 : closest_on_seg(c->p0, c->p1, p);
    V3 dv = v3sub(p, cp);
    float dist = v3len(dv), rr = r + c->r;
    if (dist >= rr) return 0;
    *n = dist > 1e-6f ? v3scale(dv, 1.0f / dist) : v3(0, 1, 0);
    *depth = rr - dist;
    return 1;
}

#define NSAMP 5
static int capsule_pen(V3 a, V3 b, float r, const Col *c, V3 *n, float *depth)
{
    int hit = 0;
    float best = 0;
    for (int i = 0; i < NSAMP; i++) {
        V3 p = v3lerp(a, b, (float)i / (NSAMP - 1));
        V3 nn; float dd;
        if (sphere_vs_col(p, r, c, &nn, &dd) && dd > best) { best = dd; *n = nn; hit = 1; }
    }
    if (hit) *depth = best;
    return hit;
}

int capsule_overlaps(V3 a, V3 b, float r, const Col *c)
{
    V3 mn = v3(fminf(a.x, b.x) - r, fminf(a.y, b.y) - r, fminf(a.z, b.z) - r);
    V3 mx = v3(fmaxf(a.x, b.x) + r, fmaxf(a.y, b.y) + r, fmaxf(a.z, b.z) + r);
    if (mx.x < c->bmin.x || mn.x > c->bmax.x || mx.y < c->bmin.y || mn.y > c->bmax.y || mx.z < c->bmin.z || mn.z > c->bmax.z)
        return 0;
    V3 n; float d;
    return capsule_pen(a, b, r, c, &n, &d);
}

/* --------- CharacterController.Move --------- */
static void cc_capsule(const CharCtrl *cc, V3 *a, V3 *b)
{
    float half = cc->height * 0.5f - cc->radius;
    if (half < 0) half = 0;
    V3 ctr = v3add(cc->pos, cc->center);
    *a = v3sub(ctr, v3(0, half, 0));
    *b = v3add(ctr, v3(0, half, 0));
}

static void cc_resolve(CharCtrl *cc, int was_grounded)
{
    float slope_cos = cosf(cc->slope * DEG2RAD);
    for (int it = 0; it < 4; it++) {
        V3 a, b;
        cc_capsule(cc, &a, &b);
        float r = cc->radius;
        V3 mn = v3(fminf(a.x, b.x) - r, fminf(a.y, b.y) - r, fminf(a.z, b.z) - r);
        V3 mx = v3(fmaxf(a.x, b.x) + r, fmaxf(a.y, b.y) + r, fmaxf(a.z, b.z) + r);
        int any = 0;
        for (int i = 0; i < ncols; i++) {
            const Col *c = &cols[i];
            if (c->trigger || !phys_layer_hits(c->layer)) continue;
            if (mx.x < c->bmin.x || mn.x > c->bmax.x || mx.y < c->bmin.y || mn.y > c->bmax.y ||
                mx.z < c->bmin.z || mn.z > c->bmax.z) continue;
            V3 n; float d;
            if (!capsule_pen(a, b, r, c, &n, &d)) continue;
            /* escalon: obstaculo bajo (stepOffset) mientras se esta en el suelo */
            float feet = cc->pos.y;
            if (fabsf(n.y) < 0.3f && was_grounded && c->bmax.y - feet <= cc->step && c->bmax.y > feet) {
                cc->pos.y = c->bmax.y + 0.01f;
                any = 1;
                cc_capsule(cc, &a, &b);
                continue;
            }
            cc->pos = v3add(cc->pos, v3scale(n, d + 0.001f));
            if (n.y > slope_cos) { cc->grounded = 1; cc->last_ground_col = i; }
            any = 1;
            cc_capsule(cc, &a, &b);
            mn = v3(fminf(a.x, b.x) - r, fminf(a.y, b.y) - r, fminf(a.z, b.z) - r);
            mx = v3(fmaxf(a.x, b.x) + r, fmaxf(a.y, b.y) + r, fmaxf(a.z, b.z) + r);
        }
        if (!any) break;
    }
}

void cc_move(CharCtrl *cc, V3 motion)
{
    if (!cc->enabled) { cc->pos = v3add(cc->pos, motion); return; }
    int was_grounded = cc->grounded;
    cc->grounded = 0;
    cc->last_ground_col = -1;
    float len = v3len(motion);
    int steps = (int)(len / 1.0f) + 1;
    if (steps > 64) steps = 64;
    V3 sub = v3scale(motion, 1.0f / steps);
    for (int s = 0; s < steps; s++) {
        cc->pos = v3add(cc->pos, sub);
        cc_resolve(cc, was_grounded);
    }
}

/* --------- rayo hacia abajo --------- */
int phys_raycast_down(V3 o, float maxd, float *out_d, int include_triggers)
{
    int best = -1;
    float bd = maxd;
    for (int i = 0; i < ncols; i++) {
        const Col *c = &cols[i];
        if (!include_triggers && c->trigger) continue;
        if (c->layer == 2) continue;     /* Ignore Raycast */
        if (o.x < c->bmin.x || o.x > c->bmax.x || o.z < c->bmin.z || o.z > c->bmax.z || c->bmin.y > o.y) continue;
        float t = -1;
        if (c->type == COL_BOX) {
            /* rayo (o, -y) contra caja orientada: metodo de las placas */
            float tmin = 0, tmax = bd;
            V3 d = v3sub(o, c->c);
            int ok = 1;
            for (int k = 0; k < 3 && ok; k++) {
                float e = v3dot(c->ax[k], d), f = -c->ax[k].y, h = (&c->he.x)[k];
                if (fabsf(f) > 1e-6f) {
                    float t1 = (-h - e) / f, t2 = (h - e) / f;
                    if (t1 > t2) { float tt = t1; t1 = t2; t2 = tt; }
                    if (t1 > tmin) tmin = t1;
                    if (t2 < tmax) tmax = t2;
                    if (tmin > tmax) ok = 0;
                } else if (e < -h || e > h) ok = 0;
            }
            if (ok) t = tmin;
        } else {
            V3 p = c->p0;
            float dx = o.x - p.x, dz = o.z - p.z, r2 = c->r * c->r - dx * dx - dz * dz;
            if (r2 >= 0) t = (o.y - p.y) - sqrtf(r2);
            if (t < 0 && r2 >= 0) t = 0;
        }
        if (t >= 0 && t < bd) { bd = t; best = i; }
    }
    if (out_d) *out_d = bd;
    return best;
}
