/* anim.c - Animation de Unity: Play, CrossFade, PlayQueued/CrossFadeQueued, capas, pesos y mixing transforms */
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "data.h"
#include "scene.h"

enum { WRAP_DEFAULT = 0, WRAP_ONCE = 1, WRAP_LOOP = 2, WRAP_PINGPONG = 4, WRAP_CLAMPFOREVER = 8 };

static int is_descendant(int n, int root)
{
    for (; n >= 0; n = gd.nodes[n].parent) if (n == root) return 1;
    return 0;
}

static void collect_bones(Animator *a)
{
    /* conjunto de nodos tocados por cualquier clip */
    static int tmp[512];
    int nb = 0;
    for (int s = 0; s < a->nstates; s++) {
        AnimState *st = &a->st[s];
        Clip *c = &gd.clips[st->clip];
        for (int t = 0; t < c->ntracks; t++) {
            int n = st->track_node[t];
            if (n < 0) continue;
            int found = 0;
            for (int k = 0; k < nb; k++) if (tmp[k] == n) { found = 1; break; }
            if (!found && nb < 512) tmp[nb++] = n;
        }
    }
    a->nbones = nb;
    a->bones = malloc(sizeof(int) * nb);
    memcpy(a->bones, tmp, sizeof(int) * nb);
    a->bind_pos = malloc(sizeof(V3) * nb);
    a->bind_rot = malloc(sizeof(Q4) * nb);
    a->bind_scl = malloc(sizeof(V3) * nb);
    for (int k = 0; k < nb; k++) {
        a->bind_pos[k] = nrt[tmp[k]].lpos;
        a->bind_rot[k] = nrt[tmp[k]].lrot;
        a->bind_scl[k] = nrt[tmp[k]].lscl;
    }
}

void anim_init(Animator *a, int root)
{
    memset(a, 0, sizeof(*a));
    a->root = root;
    int nw;
    const uint32_t *c = node_comp(root, C_ANIMATION, &nw);
    if (!c) return;
    a->default_wrap = c[2];
    int n = c[3];
    for (int i = 0; i < n && a->nstates < ANIM_MAX_STATES; i++) {
        int ci = (int)c[4 + i];
        if (ci < 0 || ci >= gd.nclips) continue;
        /* evita duplicados por nombre (el clip por defecto suele estar dos veces) */
        int dup = 0;
        for (int s = 0; s < a->nstates; s++) if (!strcmp(gd.clips[a->st[s].clip].name, gd.clips[ci].name)) dup = 1;
        if (dup) continue;
        AnimState *st = &a->st[a->nstates++];
        memset(st, 0, sizeof(*st));
        st->clip = ci;
        st->speed = 1.0f;
        st->mix_root = -1;
        Clip *cl = &gd.clips[ci];
        st->wrap = cl->wrap ? cl->wrap : a->default_wrap;
        st->track_node = malloc(sizeof(int) * (cl->ntracks + 1));
        st->track_mask = malloc(cl->ntracks + 1);
        for (int t = 0; t < cl->ntracks; t++) {
            const char *path = gd.strings[cl->tracks[t].path];
            st->track_node[t] = path[0] ? node_find_path(root, path) : root;
            st->track_mask[t] = 1;
        }
    }
    collect_bones(a);
    if (c[1] && (int)c[0] >= 0) {
        /* playAutomatically */
        for (int s = 0; s < a->nstates; s++)
            if (!strcmp(gd.clips[a->st[s].clip].name, gd.clips[c[0]].name)) {
                anim_play(a, gd.clips[c[0]].name);
                break;
            }
    }
}

int anim_find(Animator *a, const char *name)
{
    for (int s = 0; s < a->nstates; s++) if (!strcmp(gd.clips[a->st[s].clip].name, name)) return s;
    return -1;
}

/* el estado s pasa a reproducir otro clip que mueve los mismos huesos (las poses de los personajes de la pantalla
 * de personajes comparten un solo estado: cada estado cuesta memoria en todos los Animator) */
int anim_set_clip(Animator *a, int s, int ci)
{
    if (s < 0 || s >= a->nstates || ci < 0 || ci >= gd.nclips) return -1;
    AnimState *st = &a->st[s];
    if (st->clip == ci) return 0;
    Clip *cl = &gd.clips[ci];
    int *tn = realloc(st->track_node, sizeof(int) * (cl->ntracks + 1));
    if (!tn) return -1;
    st->track_node = tn;
    unsigned char *tm = realloc(st->track_mask, cl->ntracks + 1);
    if (!tm) return -1;
    st->track_mask = tm;
    for (int t = 0; t < cl->ntracks; t++) {
        const char *path = gd.strings[cl->tracks[t].path];
        tn[t] = path[0] ? node_find_path(a->root, path) : a->root;
        tm[t] = 1;
    }
    st->clip = ci;
    st->wrap = cl->wrap ? cl->wrap : a->default_wrap;
    return 0;
}

AnimState *anim_get(Animator *a, const char *name)
{
    int s = anim_find(a, name);
    return s >= 0 ? &a->st[s] : NULL;
}

float anim_length(Animator *a, const char *name)
{
    int s = anim_find(a, name);
    return s >= 0 ? gd.clips[a->st[s].clip].length : 0.0f;
}

static void clear_queue_layer(Animator *a, int layer)
{
    for (int i = 0; i < a->nqueue;) {
        if (a->st[a->queue[i].state].layer == layer) {
            memmove(&a->queue[i], &a->queue[i + 1], (a->nqueue - i - 1) * sizeof(AnimQueued));
            a->nqueue--;
        } else i++;
    }
}

static void play_state(Animator *a, int s)
{
    AnimState *t = &a->st[s];
    for (int i = 0; i < a->nstates; i++) {
        AnimState *o = &a->st[i];
        if (i == s || o->layer != t->layer) continue;
        o->enabled = 0; o->time = 0; o->weight = 0; o->target_weight = 0;
    }
    if (!t->enabled) t->time = 0;
    t->enabled = 1; t->weight = 1; t->target_weight = 1; t->fade_rate = 0; t->stop_when_faded = 0;
}

static void crossfade_state(Animator *a, int s, float fade)
{
    if (fade <= 0.0001f) { play_state(a, s); return; }
    AnimState *t = &a->st[s];
    for (int i = 0; i < a->nstates; i++) {
        AnimState *o = &a->st[i];
        if (i == s || o->layer != t->layer || !o->enabled) continue;
        o->target_weight = 0;
        o->fade_rate = 1.0f / fade;
        o->stop_when_faded = 1;
    }
    if (!t->enabled) { t->time = 0; t->weight = 0; }
    t->enabled = 1;
    t->target_weight = 1;
    t->fade_rate = 1.0f / fade;
    t->stop_when_faded = 0;
}

void anim_play(Animator *a, const char *name)
{
    int s = anim_find(a, name);
    if (s < 0) return;
    clear_queue_layer(a, a->st[s].layer);
    play_state(a, s);
}

void anim_crossfade(Animator *a, const char *name, float fade)
{
    int s = anim_find(a, name);
    if (s < 0) return;
    clear_queue_layer(a, a->st[s].layer);
    crossfade_state(a, s, fade);
}

static void enqueue(Animator *a, const char *name, float fade, int cf)
{
    int s = anim_find(a, name);
    if (s < 0) return;
    if (a->nqueue >= ANIM_MAX_QUEUE) {
        memmove(&a->queue[0], &a->queue[1], (ANIM_MAX_QUEUE - 1) * sizeof(AnimQueued));
        a->nqueue--;
    }
    a->queue[a->nqueue].state = s;
    a->queue[a->nqueue].fade = fade;
    a->queue[a->nqueue].crossfade = cf;
    a->nqueue++;
}

void anim_play_queued(Animator *a, const char *name) { enqueue(a, name, 0, 0); }
void anim_crossfade_queued(Animator *a, const char *name, float fade) { enqueue(a, name, fade, 1); }

void anim_stop(Animator *a, const char *name)
{
    int s = anim_find(a, name);
    if (s < 0) return;
    AnimState *t = &a->st[s];
    t->enabled = 0; t->time = 0; t->weight = 0; t->target_weight = 0;
}

void anim_stop_all(Animator *a)
{
    for (int i = 0; i < a->nstates; i++) {
        AnimState *t = &a->st[i];
        t->enabled = 0; t->time = 0; t->weight = 0; t->target_weight = 0;
    }
    a->nqueue = 0;
}

int anim_is_playing(Animator *a, const char *name)
{
    int s = anim_find(a, name);
    return s >= 0 && a->st[s].enabled;
}

void anim_add_mixing(Animator *a, const char *name, int node)
{
    int s = anim_find(a, name);
    if (s < 0) return;
    AnimState *st = &a->st[s];
    st->mix_root = node;
    Clip *cl = &gd.clips[st->clip];
    for (int t = 0; t < cl->ntracks; t++)
        st->track_mask[t] = st->track_node[t] >= 0 && is_descendant(st->track_node[t], node);
}

void anim_add_event(Animator *a, const char *clip, float time)
{
    int s = anim_find(a, clip);
    if (s < 0 || a->nev >= 64) return;
    a->ev_clip[a->nev] = s;
    a->ev_times[a->nev] = time;
    a->nev++;
}

static int state_finished(const AnimState *s)
{
    const Clip *c = &gd.clips[s->clip];
    if (s->wrap == WRAP_LOOP || s->wrap == WRAP_PINGPONG) return 0;
    return s->time >= c->length;
}

void anim_update(Animator *a, float dt)
{
    /* cola: arranca cuando todo lo demas de su capa ha terminado */
    for (int q = 0; q < a->nqueue;) {
        int s = a->queue[q].state;
        int layer = a->st[s].layer, busy = 0;
        for (int i = 0; i < a->nstates; i++) {
            AnimState *o = &a->st[i];
            if (o->layer != layer || !o->enabled || o->target_weight <= 0.0f) continue;
            if (i == s && a->queue[q].crossfade == 0 && 0) continue;
            if (!state_finished(o)) { busy = 1; break; }
        }
        if (busy) { q++; continue; }
        AnimQueued e = a->queue[q];
        memmove(&a->queue[q], &a->queue[q + 1], (a->nqueue - q - 1) * sizeof(AnimQueued));
        a->nqueue--;
        AnimState *t = &a->st[e.state];
        t->enabled = 0;           /* el clon del original empieza desde 0 */
        if (e.crossfade) crossfade_state(a, e.state, e.fade);
        else play_state(a, e.state);
    }
    for (int i = 0; i < a->nstates; i++) {
        AnimState *s = &a->st[i];
        if (!s->enabled) continue;
        const Clip *c = &gd.clips[s->clip];
        float prev = s->time;
        s->time += dt * s->speed;
        /* eventos registrados (pasos, etc.) */
        if (a->on_event && s->weight > 0.01f) {
            for (int e = 0; e < a->nev; e++) {
                if (a->ev_clip[e] != i) continue;
                float et = a->ev_times[e];
                if (s->wrap == WRAP_LOOP && c->length > 0) {
                    float p = fmodf(prev, c->length), n = p + (s->time - prev);
                    if ((et >= p && et < n) || (n >= c->length && et < n - c->length)) a->on_event(a, i, et);
                } else if (et >= prev && et < s->time) a->on_event(a, i, et);
            }
        }
        if (s->fade_rate > 0) {
            if (s->weight < s->target_weight) {
                s->weight += s->fade_rate * dt;
                if (s->weight >= s->target_weight) { s->weight = s->target_weight; s->fade_rate = 0; }
            } else if (s->weight > s->target_weight) {
                s->weight -= s->fade_rate * dt;
                if (s->weight <= s->target_weight) {
                    s->weight = s->target_weight; s->fade_rate = 0;
                    if (s->stop_when_faded && s->weight <= 0) { s->enabled = 0; s->time = 0; }
                }
            }
        }
        if ((s->wrap == WRAP_ONCE || s->wrap == WRAP_DEFAULT) && s->time >= c->length && c->length > 0) {
            /* Once: se para y se rebobina al terminar (salvo si hay algo en cola que lo sustituya) */
            int queued_here = 0;
            for (int q = 0; q < a->nqueue; q++) if (a->st[a->queue[q].state].layer == s->layer) queued_here = 1;
            if (!queued_here) { s->enabled = 0; s->time = 0; s->weight = 0; s->target_weight = 0; }
        }
    }
}

static void sample(const Clip *c, const AnimTrack *tr, float t, int wrap, V3 *pos, Q4 *rot, V3 *scl)
{
    float len = c->length;
    if (len > 0) {
        if (wrap == WRAP_LOOP) { t = fmodf(t, len); if (t < 0) t += len; }
        else if (wrap == WRAP_PINGPONG) {
            float m = fmodf(t, 2 * len); if (m < 0) m += 2 * len;
            t = m > len ? 2 * len - m : m;
        } else if (t > len) t = len;
    }
    if (t < 0) t = 0;
    float f = t * 30.0f;
    int f0 = (int)f;
    if (f0 >= c->nframes - 1) { f0 = c->nframes - 1; f = (float)f0; }
    int f1 = f0 + 1 < c->nframes ? f0 + 1 : f0;
    float k = f - f0;
    const float *d0 = tr->data + f0 * tr->stride, *d1 = tr->data + f1 * tr->stride;
    int o = 0;
    if (tr->flags & 1) {
        *pos = v3(lerpu(d0[0], d1[0], k), lerpu(d0[1], d1[1], k), lerpu(d0[2], d1[2], k));
        o = 3;
    }
    if (tr->flags & 2) {
        Q4 a = q4(d0[o], d0[o + 1], d0[o + 2], d0[o + 3]), b = q4(d1[o], d1[o + 1], d1[o + 2], d1[o + 3]);
        if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = q4(-b.x, -b.y, -b.z, -b.w);
        *rot = qnorm(q4(lerpu(a.x, b.x, k), lerpu(a.y, b.y, k), lerpu(a.z, b.z, k), lerpu(a.w, b.w, k)));
        o += 4;
    }
    if (tr->flags & 4) *scl = v3(lerpu(d0[o], d1[o], k), lerpu(d0[o + 1], d1[o + 1], k), lerpu(d0[o + 2], d1[o + 2], k));
}

typedef struct { V3 p, s; Q4 r; float wp, wr, ws; } Acc;

void anim_apply(Animator *a)
{
    static Acc acc[512];
    static float layer_used[512];
    int nb = a->nbones;
    if (nb <= 0) return;
    memset(acc, 0, sizeof(Acc) * nb);
    for (int b = 0; b < nb; b++) layer_used[b] = 0;
    /* capas de mayor a menor */
    int maxl = 0;
    for (int i = 0; i < a->nstates; i++) if (a->st[i].enabled && a->st[i].layer > maxl) maxl = a->st[i].layer;
    static float lsum[512];
    for (int L = maxl; L >= 0; L--) {
        /* suma de pesos de la capa por hueso */
        int any = 0;
        for (int i = 0; i < a->nstates; i++) if (a->st[i].enabled && a->st[i].layer == L && a->st[i].weight > 0) any = 1;
        if (!any) continue;
        memset(lsum, 0, sizeof(float) * nb);
        for (int i = 0; i < a->nstates; i++) {
            AnimState *s = &a->st[i];
            if (!s->enabled || s->layer != L || s->weight <= 0) continue;
            const Clip *c = &gd.clips[s->clip];
            for (int t = 0; t < c->ntracks; t++) {
                int n = s->track_node[t];
                if (n < 0 || !s->track_mask[t]) continue;
                for (int b = 0; b < nb; b++) if (a->bones[b] == n) { lsum[b] += s->weight; break; }
            }
        }
        for (int i = 0; i < a->nstates; i++) {
            AnimState *s = &a->st[i];
            if (!s->enabled || s->layer != L || s->weight <= 0) continue;
            const Clip *c = &gd.clips[s->clip];
            for (int t = 0; t < c->ntracks; t++) {
                int n = s->track_node[t];
                if (n < 0 || !s->track_mask[t]) continue;
                int b;
                for (b = 0; b < nb; b++) if (a->bones[b] == n) break;
                if (b >= nb) continue;
                float rem = 1.0f - layer_used[b];
                if (rem <= 0) continue;
                float sum = lsum[b];
                float w = s->weight * (sum > 1.0f ? 1.0f / sum : 1.0f) * rem;
                V3 p = a->bind_pos[b], sc = a->bind_scl[b];
                Q4 r = a->bind_rot[b];
                sample(c, &c->tracks[t], s->time, s->wrap, &p, &r, &sc);
                Acc *A = &acc[b];
                const AnimTrack *tr = &c->tracks[t];
                if (tr->flags & 1) { A->p = v3add(A->p, v3scale(p, w)); A->wp += w; }
                if (tr->flags & 2) {
                    if (A->wr > 0 && A->r.x * r.x + A->r.y * r.y + A->r.z * r.z + A->r.w * r.w < 0)
                        r = q4(-r.x, -r.y, -r.z, -r.w);
                    A->r = q4(A->r.x + r.x * w, A->r.y + r.y * w, A->r.z + r.z * w, A->r.w + r.w * w);
                    A->wr += w;
                }
                if (tr->flags & 4) { A->s = v3add(A->s, v3scale(sc, w)); A->ws += w; }
            }
        }
        for (int b = 0; b < nb; b++) {
            float sum = lsum[b];
            if (sum > 0) layer_used[b] += (1.0f - layer_used[b]) * (sum > 1.0f ? 1.0f : sum);
        }
    }
    /* Lo que no cubren las animaciones se queda como estaba, como en la Animation de Unity: un hueso sin ninguna
     * animacion activa no se toca. Antes volvia a la pose de reposo, que es la T: el personaje en el aire con las
     * zapatillas (el salto acaba antes de aterrizar) y el inspector al terminar de atraparte se quedaban en T. */
    for (int b = 0; b < nb; b++) {
        Acc *A = &acc[b];
        NodeRT *r = &nrt[a->bones[b]];
        if (A->wp > 0) {
            float rest = 1.0f - A->wp;
            r->lpos = rest > 0.001f ? v3add(A->p, v3scale(r->lpos, rest)) : v3scale(A->p, 1.0f / A->wp);
        }
        if (A->wr > 0) {
            float rest = 1.0f - A->wr;
            Q4 q = A->r, br = r->lrot;
            if (rest > 0.001f) {
                if (q.x * br.x + q.y * br.y + q.z * br.z + q.w * br.w < 0) br = q4(-br.x, -br.y, -br.z, -br.w);
                q = q4(q.x + br.x * rest, q.y + br.y * rest, q.z + br.z * rest, q.w + br.w * rest);
            }
            r->lrot = qnorm(q);
        }
        if (A->ws > 0) {
            float rest = 1.0f - A->ws;
            r->lscl = rest > 0.001f ? v3add(A->s, v3scale(r->lscl, rest)) : v3scale(A->s, 1.0f / A->ws);
        }
    }
}
