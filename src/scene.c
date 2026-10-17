/* scene.c - estado de la escena y logica de la pista: Track.cs, TrackChunk.cs, TrackChunkCollection.cs,
 * Selector/Randomizer/RandomizerTarget/SpawnPoint, Group, RandomizeOffset, Mirror, MovingTrain, MovingCoin,
 * CoinPool, CoinPlaceholder, CoinLine, CoinJumpCurve, PickupDefault, PickupRotate, RendererActivator.
 */
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "scene.h"
#include "render.h"
#include "spawn.h"
#include "devlog.h"

NodeRT *nrt;

float (*scene_jump_length)(float speed, float height);
float scene_jump_height_normal = 20.0f, scene_jump_height_super = 40.0f;
float scene_current_level_speed = 110.0f;
float scene_char_z;

/* ------------------------------------------------------------ escena */
static void update_world_rec(int n, const M34 *parent)
{
    NodeRT *r = &nrt[n];
    M34 local;
    m34_trs(&local, r->lpos, r->lrot, r->lscl);
    if (parent) m34_mul(&r->world, parent, &local);
    else r->world = local;
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling)
        update_world_rec(c, &r->world);
}

void scene_update_world(int n)
{
    int p = gd.nodes[n].parent;
    update_world_rec(n, p >= 0 ? &nrt[p].world : NULL);
}

void scene_set_active_recursive(int n, int on)
{
    nrt[n].active = on;
    if (!on && node_comp(n, C_GROUP, NULL)) { /* OnDisable -> UpdateChildren */ }
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling)
        scene_set_active_recursive(c, on);
}

static int first_mesh_in_subtree(int n)
{
    if (node_comp(n, C_MESH, NULL)) return n;
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling) {
        int r = first_mesh_in_subtree(c);
        if (r >= 0) return r;
    }
    return -1;
}

static int has_collider(int n)
{
    return node_comp(n, C_BOX, NULL) || node_comp(n, C_SPHERE, NULL) || node_comp(n, C_CAPSULE, NULL);
}

static int find_parent_collider(int n)
{
    for (; n >= 0; n = gd.nodes[n].parent)
        if (has_collider(n)) return n;
    return -1;
}

static int is_selector(int n)
{
    return node_comp(n, C_RANDOMIZER, NULL) || node_comp(n, C_RANDTARGET, NULL) || node_comp(n, C_SPAWNPOINT, NULL);
}

static int is_group(int n) { return node_comp(n, C_GROUP, NULL) || node_comp(n, C_SSGROUP, NULL); }

static int ss_groups_on;

static void group_update_children(int n)
{
    int ga = node_comp(n, C_SSGROUP, NULL) ? ss_groups_on : 0;
    int on = ga && nrt[n].active;
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling)
        scene_set_active_recursive(c, on);
}

void scene_glow_visible(int glow, int vis)
{
    if (glow < 0) return;
    int m = first_mesh_in_subtree(glow);
    if (m >= 0) nrt[m].rend = vis;
}

/* componentes de dibujo de cada nodo y ramas sin nada que dibujar, calculados una vez al arrancar:
 * el recorrido de cada fotograma no tiene que buscar entre los componentes ni bajar por ramas de
 * colliders, triggers y puntos de aparicion (miles de lecturas de memoria dispersas en la PSP real) */
static const uint32_t **cmesh, **cskin;
static unsigned char *subdraw;

static int calc_subdraw(int n)
{
    int any = cmesh[n] || cskin[n];
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling) any |= calc_subdraw(c);
    subdraw[n] = (unsigned char)any;
    return any;
}

void scene_init(void)
{
    nrt = calloc(gd.nnodes, sizeof(NodeRT));
    cmesh = calloc(gd.nnodes, sizeof(*cmesh));
    cskin = calloc(gd.nnodes, sizeof(*cskin));
    subdraw = calloc(gd.nnodes, 1);
    for (int i = 0; i < gd.nnodes; i++) {
        cmesh[i] = node_comp(i, C_MESH, NULL);
        cskin[i] = node_comp(i, C_SKIN, NULL);
    }
    for (int i = 0; i < gd.nnodes; i++) if (gd.nodes[i].parent < 0) calc_subdraw(i);
    for (int i = 0; i < gd.nnodes; i++) {
        NodeFile *f = &gd.nodes[i];
        NodeRT *r = &nrt[i];
        r->lpos = v3(f->pos[0], f->pos[1], f->pos[2]);
        r->lrot = q4(f->rot[0], f->rot[1], f->rot[2], f->rot[3]);
        r->lscl = v3(f->scl[0], f->scl[1], f->scl[2]);
        r->active = f->active;
        const uint32_t *m = node_comp(i, C_MESH, NULL);
        const uint32_t *sk = node_comp(i, C_SKIN, NULL);
        r->rend = m ? m[1] : (sk ? sk[1] : 0);
        const uint32_t *c;
        r->col = 1;
        if ((c = node_comp(i, C_BOX, NULL)) || (c = node_comp(i, C_SPHERE, NULL)) || (c = node_comp(i, C_CAPSULE, NULL)))
            r->col = c[1];
        r->letter_mesh = -1;
    }
    /* efectos de los Awake() del original */
    for (int i = 0; i < gd.nnodes; i++) {
        const uint32_t *c;
        if (node_comp(i, C_RENDACT, NULL)) nrt[i].rend = 0;
        if ((c = node_comp(i, C_PICKUPDEFAULT, NULL))) {
            if ((int)c[0] >= 0) nrt[c[0]].rend = 0;
            scene_glow_visible((int)c[1], 0);
        }
        if ((c = node_comp(i, C_DAILYLETTER, NULL))) {
            if ((int)c[0] >= 0) nrt[c[0]].rend = 0;
            scene_glow_visible((int)c[1], 0);
            if ((int)c[2] >= 0) nrt[c[2]].rend = 0;
        }
        if ((c = node_comp(i, C_MOVINGTRAIN, NULL))) {
            float speed = u2f(c[0]), count = u2f(c[1]);
            int ch = gd.nodes[i].first_child;
            if (ch >= 0) nrt[ch].lpos = v3(0, -200, 0);
            uint32_t *b = (uint32_t *)node_comp(i, C_BOX, NULL);
            if (b) {
                float sz = u2f(b[7]) / (1.0f + speed);
                float cz = (30.0f * count + 1.0f) / (1.0f + speed);
                union { float f; uint32_t u; } cv;
                cv.f = sz; b[7] = cv.u;
                cv.f = 0.0f; b[2] = cv.u;
                cv.f = 15.2f; b[3] = cv.u;
                cv.f = cz; b[4] = cv.u;
            }
        }
        if (node_comp(i, C_MOVINGCOIN, NULL)) {
            int ch = gd.nodes[i].first_child;
            if (ch >= 0) nrt[ch].lpos = v3(0, -200, 0);
        }
    }
    for (int i = 0; i < gd.nnodes; i++) {
        if (is_selector(i))
            for (int c = gd.nodes[i].first_child; c >= 0; c = gd.nodes[c].next_sibling)
                scene_set_active_recursive(c, 0);
        if (is_group(i)) group_update_children(i);
    }
    for (int i = 0; i < gd.nnodes; i++)
        if (gd.nodes[i].parent < 0) scene_update_world(i);
}

void scene_ssgroups(int on)
{
    ss_groups_on = on;
    for (int i = 0; i < gd.nnodes; i++)
        if (node_comp(i, C_SSGROUP, NULL)) group_update_children(i);
}

void scene_draw_subtree(int n)
{
    if (!subdraw[n]) return;
    NodeRT *r = &nrt[n];
    if (r->active && r->rend && !r->hidden) {
        const uint32_t *m = cmesh[n];
        if (m) {
            int mesh = m[0], nm = m[2];
            int mats[4];
            for (int k = 0; k < nm && k < 4; k++) mats[k] = (int)m[3 + k];
            if (r->letter_mesh >= 0) mesh = r->letter_mesh;
            int nsub = m[3 + nm];
            if (nsub > 0) {
                /* malla combinada (static batching): solo las submallas indicadas, en coordenadas de mundo */
                int smats[4] = {-1, -1, -1, -1};
                for (int k = 0; k < nsub && k < 4; k++) {
                    int si = m[4 + nm + k];
                    if (si < 4) smats[si] = mats[k < nm ? k : nm - 1];
                }
                M34 id; m34_identity(&id);
                r_draw_mesh(mesh, &id, smats, 4, 1.0f);
            } else {
                r_draw_mesh(mesh, &r->world, mats, nm, 1.0f);
            }
        }
    }
    if (r->active && r->rend) {
        const uint32_t *sk = cskin[n];
        if (sk) {
            int mesh = sk[0], nm = sk[2];
            int mats[4];
            for (int k = 0; k < nm && k < 4; k++) mats[k] = (int)sk[3 + k];
            int nb = sk[3 + nm];
            static M34 bones[64];
            Mesh *me = &gd.meshes[mesh];
            if (nb > 64) nb = 64;
            for (int k = 0; k < nb; k++) {
                int bn = (int)sk[4 + nm + k];
                const M34 *bw = bn >= 0 ? &nrt[bn].world : &r->world;
                if (k < me->nbind) m34_mul(&bones[k], bw, &me->bind[k]);
                else bones[k] = *bw;
            }
            r_draw_skinned(mesh, bones, nb, mats, nm);
        }
    }
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling)
        scene_draw_subtree(c);
}

/* ------------------------------------------------------------ monedas */
CoinRT coins[MAX_COINS];
int coin_mesh_node = -1, coin_glow_node = -1;
static V3 coin_local[MAX_COINS];

int coin_spawn(V3 pos, int owner)
{
    for (int i = 0; i < MAX_COINS; i++) {
        CoinRT *c = &coins[i];
        if (c->active) continue;
        memset(c, 0, sizeof(*c));
        c->active = 1; c->collider = 1; c->visible = 1;
        c->pos = pos;
        c->owner = owner;
        c->pivot = v3(0, 6, 0);
        c->rot_z = pos.z;
        if (owner >= 0) {
            M34 inv;
            m34_inverse(&inv, &nrt[owner].world);
            coin_local[i] = m34_point(&inv, pos);
        }
        return i;
    }
    static int warned;
    if (!warned++) dlog("AVISO se acabaron las %d monedas del pool: faltaran monedas en la via", MAX_COINS);
    return -1;
}

void coin_release_owner(int owner)
{
    for (int i = 0; i < MAX_COINS; i++)
        if (coins[i].active && coins[i].owner == owner) coins[i].active = 0;
}

void coins_release_inair(void) { coin_release_owner(-2); }

void coins_update(float dt)
{
    (void)dt;
    for (int i = 0; i < MAX_COINS; i++) {
        CoinRT *c = &coins[i];
        if (!c->active || c->owner < 0) continue;
        c->pos = m34_point(&nrt[c->owner].world, coin_local[i]);
    }
}

extern float game_time;

void coins_draw(void)
{
    if (coin_mesh_node < 0) return;
    int nw;
    const uint32_t *m = node_comp(coin_mesh_node, C_MESH, &nw);
    const uint32_t *g = coin_glow_node >= 0 ? node_comp(coin_glow_node, C_MESH, NULL) : NULL;
    if (!m) return;
    int mats[4] = {(int)m[3], -1, -1, -1};
    int gmats[4] = {g ? (int)g[3] : -1, -1, -1, -1};
    V3 cam = r_cam_pos;
    for (int i = 0; i < MAX_COINS; i++) {
        CoinRT *c = &coins[i];
        if (!c->active || !c->visible) continue;
        if (c->pos.z < cam.z - 10.0f || c->pos.z > cam.z + r_far) continue;
        /* PickupRotate: rotacion en Y = tiempo*180 + z*0.9 */
        float ang = game_time * 180.0f + c->rot_z * 0.9f;
        M34 w;
        V3 p = v3add(c->pos, c->pivot);
        m34_trs(&w, p, qeuler(0, ang, 0), v3(1, 1, 1));
        r_draw_mesh(m[0], &w, mats, 1, 1.0f);
        if (g) {
            /* el brillo esta en el pivote, desplazado 3.3 en z y sin rotar */
            M34 gw;
            const NodeRT *gr = &nrt[gd.nodes[coin_glow_node].first_child >= 0 ? coin_glow_node : coin_glow_node];
            (void)gr;
            m34_trs(&gw, v3add(p, v3(0, 0, 3.3f)), qident(), v3(1, 1, 1));
            r_draw_mesh(g[0], &gw, gmats, 1, 1.0f);
        }
    }
}

/* ------------------------------------------------------------ MovingTrain / MovingCoin */
/* el peor caso medido son ~120 (trainTops_moving_multiple + bridge_various, con sus monedas) */
#define MAX_MOVING 256
static int moving_nodes[MAX_MOVING];
static int moving_auto[MAX_MOVING];
static int nmoving;

static void moving_add(int n)
{
    for (int i = 0; i < nmoving; i++) if (moving_nodes[i] == n) { moving_auto[i] = 0; return; }
    static int peak, warned;
    if (nmoving < MAX_MOVING) {
        moving_auto[nmoving] = 0;
        moving_nodes[nmoving++] = n;
        nrt[n].col = 1;
        if (nmoving > peak && nmoving > MAX_MOVING * 3 / 4) dlog("Objetos en movimiento: nuevo pico %d de %d", peak = nmoving, MAX_MOVING);
        return;
    }
    /* sin sitio: el tren se quedaria escondido bajo la via con el collider activo (obstaculo invisible).
     * Mejor quitarle tambien el collider. */
    nrt[n].col = 0;
    if (!warned++) dlog("AVISO lista de objetos en movimiento llena (%d): %s se desactiva", MAX_MOVING, gd.strings[gd.nodes[n].name]);
}

static void moving_remove(int n)
{
    for (int i = 0; i < nmoving; i++)
        if (moving_nodes[i] == n) {
            moving_nodes[i] = moving_nodes[--nmoving];
            moving_auto[i] = moving_auto[nmoving];
            return;
        }
}

static float moving_speed(int n)
{
    const uint32_t *c = node_comp(n, C_MOVINGTRAIN, NULL);
    if (!c) c = node_comp(n, C_MOVINGCOIN, NULL);
    return c ? u2f(c[0]) : 1.0f;
}

void moving_update(float charZ, float dt, float speed)
{
    for (int i = 0; i < nmoving; i++) {
        int n = moving_nodes[i];
        int ch = gd.nodes[n].first_child;
        if (ch < 0) continue;
        float s = moving_speed(n);
        if (moving_auto[i]) {
            /* piloto automatico tras morir: sigue avanzando hacia la camara */
            nrt[ch].lpos.z -= dt * speed * s;
        } else {
            float wz = m34_pos(&nrt[n].world).z;
            nrt[ch].lpos = v3(0, 0, (wz - charZ) * s);
        }
        scene_update_world(ch);
    }
}

void moving_autopilot(float charZ)
{
    for (int i = 0; i < nmoving; i++) {
        int n = moving_nodes[i];
        float z = m34_pos(&nrt[n].world).z;
        const uint32_t *b = node_comp(n, C_BOX, NULL);
        if (b) z += u2f(b[4]) - u2f(b[7]) * 0.5f;
        if (z - charZ < 200.0f) moving_auto[i] = 1;
    }
}

/* ------------------------------------------------------------ PickupRotate */
#define MAX_ROT 256
static int rot_nodes[MAX_ROT], nrot;

void pickups_update(float t)
{
    for (int i = 0; i < nrot; i++) {
        int n = rot_nodes[i];
        const uint32_t *c = node_comp(n, C_PICKUPROTATE, NULL);
        if (!c || !nrt[n].rotating || (int)c[0] < 0) continue;
        int tg = c[0];
        nrt[tg].lrot = qeuler(0, t * u2f(c[1]) + nrt[n].rot_phase * u2f(c[2]), 0);
        scene_update_world(tg);
    }
}

static void rot_add(int n)
{
    for (int i = 0; i < nrot; i++) if (rot_nodes[i] == n) return;
    if (nrot < MAX_ROT) rot_nodes[nrot++] = n;
    else { static int warned; if (!warned++) dlog("AVISO demasiados power-ups girando (%d)", MAX_ROT); }
}

static void rot_remove(int n)
{
    for (int i = 0; i < nrot; i++) if (rot_nodes[i] == n) { rot_nodes[i] = rot_nodes[--nrot]; return; }
}

/* ------------------------------------------------------------ TrackObject */
static int is_trackobject(int n)
{
    return node_comp(n, C_TRACKOBJECT, NULL) || node_comp(n, C_MIRROR, NULL) || node_comp(n, C_RENDACT, NULL) ||
           node_comp(n, C_PICKUPDEFAULT, NULL) || node_comp(n, C_PICKUPROTATE, NULL) || node_comp(n, C_COINPLACE, NULL) ||
           node_comp(n, C_DAILYLETTER, NULL);
}

static void obj_activate(int n)
{
    const uint32_t *c;
    if (node_comp(n, C_MIRROR, NULL)) {
        float sgn = (float)(rnd_range(0, 2) * 2 - 1);
        for (int ch = gd.nodes[n].first_child; ch >= 0; ch = gd.nodes[ch].next_sibling)
            nrt[ch].lpos.x *= sgn;
        scene_update_world(n);
    }
    if (node_comp(n, C_RENDACT, NULL)) nrt[n].rend = 1;
    if ((c = node_comp(n, C_PICKUPDEFAULT, NULL))) {
        int pc = find_parent_collider(n);
        if (pc >= 0) nrt[pc].col = 1;
        if ((int)c[0] >= 0) nrt[c[0]].rend = 1;
        scene_glow_visible((int)c[1], 1);
        nrt[n].picked = 0;
    }
    if ((c = node_comp(n, C_DAILYLETTER, NULL))) {
        /* sin palabra del dia (modo sin conexion): la letra no aparece */
        if ((int)c[0] >= 0) nrt[c[0]].rend = 0;
        int pc = find_parent_collider(n);
        if (pc >= 0) nrt[pc].col = 0;
    }
    if (node_comp(n, C_PICKUPROTATE, NULL)) {
        nrt[n].rot_phase = m34_pos(&nrt[n].world).z;
        nrt[n].rotating = 1;
        rot_add(n);
    }
    if (node_comp(n, C_COINPLACE, NULL)) coin_spawn(m34_pos(&nrt[n].world), n);
    if ((c = node_comp(n, C_COINLINE, NULL))) {
        float len = u2f(c[0]), sp = u2f(c[1]);
        V3 p = m34_pos(&nrt[n].world), f = v3norm(m34_col(&nrt[n].world, 2));
        for (float t = 0; t < len; t += sp) coin_spawn(v3add(p, v3scale(f, t)), n);
    }
    if ((c = node_comp(n, C_COINJUMP, NULL))) {
        float offset = u2f(c[1]), sp = u2f(c[2]), br = u2f(c[3]), er = u2f(c[4]);
        float h = c[5] ? scene_jump_height_super : scene_jump_height_normal;
        float L = scene_jump_length ? scene_jump_length(scene_current_level_speed, h) : 100.0f;
        V3 p = m34_pos(&nrt[n].world);
        V3 f = v3norm(m34_col(&nrt[n].world, 2)), u = v3norm(m34_col(&nrt[n].world, 1));
        for (float t = br * L; t < er * L; t += sp) {
            float r = t / L;
            V3 q = v3add(p, v3add(v3scale(f, L * (r - offset)), v3scale(u, 4.0f * r * (1.0f - r) * h)));
            coin_spawn(q, n);
        }
        /* group.UpdateChildren() */
        for (int pn = n; pn >= 0; pn = gd.nodes[pn].parent)
            if (is_group(pn)) { group_update_children(pn); break; }
    }
    if (node_comp(n, C_MOVINGTRAIN, NULL) || node_comp(n, C_MOVINGCOIN, NULL)) {
        moving_add(n);
        moving_update(scene_char_z, 0, 0);
    }
}

static void obj_deactivate(int n)
{
    const uint32_t *c;
    if (node_comp(n, C_RENDACT, NULL)) nrt[n].rend = 0;
    if ((c = node_comp(n, C_PICKUPDEFAULT, NULL))) {
        if ((int)c[0] >= 0) nrt[c[0]].rend = 0;
        scene_glow_visible((int)c[1], 0);
    }
    if ((c = node_comp(n, C_DAILYLETTER, NULL))) {
        if ((int)c[0] >= 0) nrt[c[0]].rend = 0;
    }
    if (node_comp(n, C_PICKUPROTATE, NULL)) { nrt[n].rotating = 0; rot_remove(n); }
    if (node_comp(n, C_COINPLACE, NULL) || node_comp(n, C_COINLINE, NULL) || node_comp(n, C_COINJUMP, NULL))
        coin_release_owner(n);
    if (node_comp(n, C_MOVINGTRAIN, NULL) || node_comp(n, C_MOVINGCOIN, NULL)) {
        moving_remove(n);
        int ch = gd.nodes[n].first_child;
        if (ch >= 0) { nrt[ch].lpos = v3(0, -100, 0); scene_update_world(ch); }
    }
}

/* ------------------------------------------------------------ TrackChunkCollection */
typedef struct {
    int node;
    float zSize, zMin, zMax;
    int prob, tutorial;
    int *objs, nobjs;
    int ncp; int cp_track[16]; float cp_z[16];
    /* obstaculos escondidos por la tabla */
    int hidden[64]; float hidden_y[64]; int nhidden;
} Chunk;

#define MAX_CHUNKS 96
static Chunk chunks[MAX_CHUNKS];
static int nchunks;
static int order[MAX_CHUNKS];          /* ordenados por zMinimum (como AddToChunks) */
static int coll_active[MAX_CHUNKS], ncoll_active, last_added = -1;
static int random_space[1024], nrandom;

int track_active[MAX_ACTIVE_CHUNKS];
int track_nactive;
float track_spacing = 20.0f;
int track_tutorial, track_running_tutorial;
static float track_chunk_z;
static int first_chunk = 1;
static int track_left = -1, track_right = -1, jetpack_landing = -1, tutorial_chunk = -1;
static float clean_up_distance = 300.0f, track_ahead = 1500.0f;

int track_chunk_node(int ci) { return chunks[ci].node; }
int track_chunk_overlaps(int ci, float z0, float z1)
{
    float a = nrt[chunks[ci].node].lpos.z;
    return a <= z1 && a + chunks[ci].zSize >= z0;
}

static int chunk_of_node(int n)
{
    for (int i = 0; i < nchunks; i++) if (chunks[i].node == n) return i;
    return -1;
}

static void collect_objs(int n, int *buf, int *cnt, int max)
{
    if (is_trackobject(n) || node_comp(n, C_COINLINE, NULL) || node_comp(n, C_COINJUMP, NULL) ||
        node_comp(n, C_MOVINGTRAIN, NULL) || node_comp(n, C_MOVINGCOIN, NULL)) {
        if (*cnt < max) buf[(*cnt)++] = n;
    }
    for (int c = gd.nodes[n].first_child; c >= 0; c = gd.nodes[c].next_sibling) collect_objs(c, buf, cnt, max);
}

static void recalc(void)
{
    nrandom = 0;
    for (int i = 0; i < ncoll_active; i++)
        for (int j = 0; j < chunks[coll_active[i]].prob && nrandom < 1024; j++) random_space[nrandom++] = i;
}

static void coll_initialize(float z)
{
    ncoll_active = 0;
    last_added = -1;
    for (int i = 0; i < nchunks; i++) {
        Chunk *c = &chunks[order[i]];
        if (c->zMin <= z && z < c->zMax) { coll_active[ncoll_active++] = order[i]; last_added = i; }
    }
    recalc();
}

static void coll_move_forward(float z)
{
    int added = 0, removed = 0;
    for (int i = last_added + 1; i < nchunks; i++) {
        Chunk *c = &chunks[order[i]];
        if (c->zMin > z) break;
        coll_active[ncoll_active++] = order[i];
        added++;
        last_added = i;
    }
    for (int i = 0; i < ncoll_active;) {
        if (chunks[coll_active[i]].zMax < z) {
            memmove(&coll_active[i], &coll_active[i + 1], (ncoll_active - i - 1) * sizeof(int));
            ncoll_active--;
            removed++;
        } else i++;
    }
    if (added || removed) recalc();
}

static int coll_random(void)
{
    if (nrandom <= 0) return -1;
    return coll_active[random_space[rnd_range(0, nrandom)]];
}

/* ------------------------------------------------------------ Track */
void track_init(void)
{
    int level = node_find_root("Level");
    track_left = node_find_child(level, "TrackLeft");
    track_right = node_find_child(level, "TrackRight");
    jetpack_landing = node_find_child(level, "JetpackLanding");
    tutorial_chunk = node_find_path(level, "Chunks/Tutorial");
    if (track_left >= 0 && track_right >= 0)
        track_spacing = v3len(v3sub(node_pos(track_right), node_pos(track_left))) / 2.0f;
    static int objbuf[8192];
    int objpos = 0;
    for (int i = 0; i < gd.nnodes && nchunks < MAX_CHUNKS; i++) {
        const uint32_t *c = node_comp(i, C_TRACKCHUNK, NULL);
        if (!c) continue;
        Chunk *ch = &chunks[nchunks];
        memset(ch, 0, sizeof(*ch));
        ch->node = i;
        ch->zSize = u2f(c[0]);
        ch->prob = c[1];
        ch->zMin = u2f(c[2]);
        ch->zMax = c[3] ? u2f(c[4]) : FLT_MAX;
        ch->tutorial = c[5];
        ch->ncp = c[6] > 16 ? 16 : c[6];
        for (int k = 0; k < ch->ncp; k++) { ch->cp_track[k] = c[7 + 2 * k]; ch->cp_z[k] = u2f(c[8 + 2 * k]); }
        ch->objs = &objbuf[objpos];
        collect_objs(i, ch->objs, &ch->nobjs, 8192 - objpos);
        objpos += ch->nobjs;
        /* AddToChunks: insercion ordenada por zMinimum */
        int pos = 0;
        while (pos < nchunks && chunks[order[pos]].zMin < ch->zMin) pos++;
        memmove(&order[pos + 1], &order[pos], (nchunks - pos) * sizeof(int));
        order[pos] = nchunks;
        nchunks++;
    }
    track_tutorial = 0;
}

float track_ahead_distance(void) { return track_ahead; }
float track_get_x(int idx) { return track_spacing * idx; }
V3 track_pos(float x, float z)
{
    V3 l = track_left >= 0 ? node_pos(track_left) : v3(-20, 0, 0);
    return v3(l.x + x, l.y, l.z + z);
}

static void chunk_deactivate(int ci)
{
    Chunk *c = &chunks[ci];
    for (int i = 0; i < c->nobjs; i++) obj_deactivate(c->objs[i]);
}

static void restore_hidden(Chunk *c)
{
    for (int i = 0; i < c->nhidden; i++) {
        nrt[c->hidden[i]].lpos.y = c->hidden_y[i];
        nrt[c->hidden[i]].hidden = 0;
    }
    c->nhidden = 0;
}

/* RandomizeOffset.ChooseRandomOffset */
static void random_offset(int n)
{
    const uint32_t *c = node_comp(n, C_RANDOFFSET, NULL);
    if (!c) return;
    float opts[3]; int k = 0;
    if (c[0]) opts[k++] = -20.0f;
    if (c[1]) opts[k++] = 0.0f;
    if (c[2]) opts[k++] = 20.0f;
    if (k > 0) nrt[n].lpos.x = opts[rnd_range(0, k)];
}

#define MAXV 2048
static void perform_selection(int n, int *visit, int *nv);

static int cmp_z_nodes(const void *a, const void *b)
{
    float za = nrt[*(const int *)a].world.m[11], zb = nrt[*(const int *)b].world.m[11];
    return za < zb ? -1 : (za > zb ? 1 : 0);
}

/* Track.PerformRecursiveSelection */
static void recursive_selection(int parent, int sort_spawn)
{
    static int visit_stack[4][MAXV];
    static int act_stack[4][MAXV];
    static int sp_stack[4][256];
    static int depth = 0;
    if (depth >= 4) return;
    int *visit = visit_stack[depth], *act = act_stack[depth], *sps = sp_stack[depth];
    depth++;
    int head = 0, nv = 0, na = 0, nsp = 0;
    visit[nv++] = parent;
    while (head < nv) {
        int g = visit[head++];
        if (sort_spawn && node_comp(g, C_SPAWNPOINT, NULL)) {
            if (nsp < 256) sps[nsp++] = g;
            continue;
        }
        if (node_comp(g, C_RANDOFFSET, NULL)) { random_offset(g); scene_update_world(g); }
        if (na < MAXV) act[na++] = g;
        if (is_selector(g)) {
            perform_selection(g, visit, &nv);
        } else if (!is_group(g)) {
            for (int c = gd.nodes[g].first_child; c >= 0; c = gd.nodes[c].next_sibling)
                if (nv < MAXV) visit[nv++] = c;
        }
    }
    for (int i = 0; i < na; i++) {
        nrt[act[i]].active = 1;
        if (is_group(act[i])) group_update_children(act[i]);
    }
    if (nsp > 0) {
        qsort(sps, nsp, sizeof(int), cmp_z_nodes);
        for (int i = 0; i < nsp; i++) recursive_selection(sps[i], 0);
    }
    depth--;
}

static void perform_selection(int n, int *visit, int *nv)
{
    const uint32_t *c;
    if (node_comp(n, C_SPAWNPOINT, NULL)) {
        spawn_perform_selection(n, visit, nv, MAXV);
        return;
    }
    if ((c = node_comp(n, C_RANDTARGET, NULL))) {
        int cnt = c[0];
        int pick = rnd_range(0, cnt);
        for (int i = 0; i < cnt; i++) {
            int t = (int)c[1 + i];
            if (t < 0) continue;
            if (i == pick) { if (*nv < MAXV) visit[(*nv)++] = t; }
            else scene_set_active_recursive(t, 0);
        }
        return;
    }
    /* Randomizer */
    int cnt = 0;
    for (int ch = gd.nodes[n].first_child; ch >= 0; ch = gd.nodes[ch].next_sibling) cnt++;
    int pick = rnd_range(0, cnt), i = 0;
    for (int ch = gd.nodes[n].first_child; ch >= 0; ch = gd.nodes[ch].next_sibling, i++) {
        if (i == pick) { if (*nv < MAXV) visit[(*nv)++] = ch; }
        else scene_set_active_recursive(ch, 0);
    }
}

static int cmp_objs_z(const void *a, const void *b) { return cmp_z_nodes(a, b); }

static void lay_chunk(int ci)
{
    Chunk *c = &chunks[ci];
    int n = c->node;
    nrt[n].lpos = v3(0, 0, track_chunk_z);
    /* el padre de los tramos esta en el origen: la posicion local es la del mundo */
    track_chunk_z += c->zSize;
    if (track_nactive < MAX_ACTIVE_CHUNKS) track_active[track_nactive++] = ci;
    else dlog("AVISO demasiados tramos activos (%d): el tramo %s no tendra colisiones", MAX_ACTIVE_CHUNKS, gd.strings[gd.nodes[n].name]);
    dlog("TRAMO %s z=%.0f", gd.strings[gd.nodes[n].name], (double)track_chunk_z);
    restore_hidden(c);
    scene_update_world(n);
    recursive_selection(n, 1);
    scene_update_world(n);
    qsort(c->objs, c->nobjs, sizeof(int), cmp_objs_z);
    for (int i = 0; i < c->nobjs; i++)
        if (nrt[c->objs[i]].active) obj_activate(c->objs[i]);
    scene_update_world(n);
    scene_on_chunk_laid(ci);
}

static void cleanup(float charZ)
{
    float lim = charZ - clean_up_distance;
    for (int i = 0; i < track_nactive;) {
        Chunk *c = &chunks[track_active[i]];
        if (nrt[c->node].lpos.z + c->zSize < lim) {
            if (!c->tutorial) chunk_deactivate(track_active[i]);
            memmove(&track_active[i], &track_active[i + 1], (track_nactive - i - 1) * sizeof(int));
            track_nactive--;
        } else i++;
    }
}

static int is_active_chunk(int ci)
{
    for (int i = 0; i < track_nactive; i++) if (track_active[i] == ci) return 1;
    return 0;
}

static int jet_index_chunk(int idx)
{
    int i = nchunks - 1 - idx;
    if (i < 0) i = 0;
    return order[i];
}

void track_lay_chunks(float charZ, float ahead, int jetpack)
{
    if (nrandom <= 0) return;
    float lim = charZ + ahead;
    if (track_chunk_z < lim) cleanup(charZ);
    int jet = 0, guard = 0;
    while (track_chunk_z < lim && guard++ < 32) {
        coll_move_forward(track_chunk_z);
        int ci;
        if (first_chunk && track_tutorial && tutorial_chunk >= 0) {
            ci = chunk_of_node(tutorial_chunk);
            first_chunk = 0;
            if (chunks[ci].ncp > 0) track_running_tutorial = 1;
        } else if (jetpack) {
            ci = jet_index_chunk(jet++);
        } else {
            ci = coll_random();
#ifdef AT_CHUNK
            for (int q = 0; q < nchunks; q++)
                if (!strcmp(gd.strings[gd.nodes[chunks[q].node].name], AT_CHUNK) && !is_active_chunk(q) && track_chunk_z > 0) ci = q;
#ifdef AT_CHUNK2
            if (is_active_chunk(ci) || strcmp(gd.strings[gd.nodes[chunks[ci].node].name], AT_CHUNK))
                for (int q = 0; q < nchunks; q++)
                    if (!strcmp(gd.strings[gd.nodes[chunks[q].node].name], AT_CHUNK2) && !is_active_chunk(q) && track_chunk_z > 0) ci = q;
#endif
#endif
            int tries = 0;
            while (ci >= 0 && is_active_chunk(ci) && tries < 1000) { ci = coll_random(); tries++; }
        }
        if (ci < 0) break;
        lay_chunk(ci);
    }
}

float track_lay_jetpack_chunks(float charZ, float flyLength)
{
    track_lay_chunks(charZ, flyLength, 1);
    float res = track_chunk_z - charZ;
    int ci = chunk_of_node(jetpack_landing);
    if (ci >= 0) lay_chunk(ci);
    return res;
}

void track_deactivate_all(void)
{
    for (int i = 0; i < track_nactive; i++) chunk_deactivate(track_active[i]);
}

void track_restart(void)
{
    for (int i = 0; i < nchunks; i++) {
        nrt[chunks[i].node].lpos.y = -1000.0f;
        scene_update_world(chunks[i].node);
    }
    track_chunk_z = 0;
    coll_initialize(0);
    for (int i = 0; i < track_nactive; i++) chunk_deactivate(track_active[i]);
    track_nactive = 0;
    first_chunk = 1;
    track_running_tutorial = 0;
    for (int i = 0; i < MAX_COINS; i++) coins[i].active = 0;
    nmoving = 0;
}

/* TrackChunk.DeactivateObstacles (choque con la tabla) */
static void deactivate_obstacles_rec(Chunk *c, int n, float maxZ)
{
    if (!node_comp(n, C_SNAP, NULL)) {
        for (int ch = gd.nodes[n].first_child; ch >= 0; ch = gd.nodes[ch].next_sibling)
            deactivate_obstacles_rec(c, ch, maxZ);
        return;
    }
    float z = m34_pos(&nrt[n].world).z;
    const uint32_t *b = node_comp(n, C_BOX, NULL);
    if (b) {
        /* bounds.min.z del collider */
        V3 ctr = m34_point(&nrt[n].world, v3(u2f(b[2]), u2f(b[3]), u2f(b[4])));
        float ext = fabsf(nrt[n].world.m[8]) * u2f(b[7]) * 0.5f + fabsf(nrt[n].world.m[5]) * u2f(b[6]) * 0.5f +
                    fabsf(nrt[n].world.m[2]) * u2f(b[5]) * 0.5f;
        z = ctr.z - ext;
    }
    if (z < maxZ && gd.nodes[n].layer != 16) {
        int already = 0;
        for (int i = 0; i < c->nhidden; i++) if (c->hidden[i] == n) already = 1;
        if (!already && c->nhidden < 64) { c->hidden[c->nhidden] = n; c->hidden_y[c->nhidden++] = nrt[n].lpos.y; }
        nrt[n].lpos.y = -1000.0f;
        nrt[n].hidden = 1;
        scene_update_world(n);
    }
}

void track_remove_obstacles(float maxZ)
{
    for (int i = 0; i < track_nactive; i++) {
        Chunk *c = &chunks[track_active[i]];
        for (int ch = gd.nodes[c->node].first_child; ch >= 0; ch = gd.nodes[ch].next_sibling)
            deactivate_obstacles_rec(c, ch, maxZ);
    }
    /* las monedas que colgaban de obstaculos escondidos tambien desaparecen */
    for (int i = 0; i < MAX_COINS; i++)
        if (coins[i].active && coins[i].owner >= 0 && coins[i].pos.z < maxZ) {
            int o = coins[i].owner, hid = 0;
            for (int p = o; p >= 0; p = gd.nodes[p].parent) if (nrt[p].hidden) { hid = 1; break; }
            if (hid) coins[i].active = 0;
        }
}

float track_last_checkpoint(float charZ)
{
    for (int i = 0; i < track_nactive; i++) {
        Chunk *c = &chunks[track_active[i]];
        if (track_running_tutorial && c->node == tutorial_chunk) {
            float best = 0; int found = 0;
            for (int k = 0; k < c->ncp; k++)
                if (c->cp_z[k] <= charZ && (!found || c->cp_z[k] >= best)) { best = c->cp_z[k]; found = 1; }
            return found ? best : 0;
        }
    }
    return 0;
}
