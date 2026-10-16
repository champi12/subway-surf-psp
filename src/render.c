/* render.c - transformacion de vertices por software para reproducir el shader "Distorted" del original
 * (el mundo se curva hacia la izquierda y hacia abajo segun la distancia), recorte por software contra
 * el plano cercano y la banda de guarda de la PSP, y envio al GE ya en espacio de camara.
 */
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "render.h"
#include "data.h"

#define BUF_W 512
#define LIST_WORDS (256 * 1024)
#define VRING_BYTES (2 * 1024 * 1024)
#define IRING_BYTES (512 * 1024)

static unsigned int __attribute__((aligned(64))) dlist[LIST_WORDS];
static unsigned char __attribute__((aligned(64))) vring[VRING_BYTES];
static unsigned char __attribute__((aligned(64))) iring[IRING_BYTES];
static int vpos, ipos;
static void *fbp0, *fbp1, *zbp;
static int cur_fb;

int r_stat_verts, r_stat_tris, r_stat_draws, r_stat_clipped;
M34 r_view;
V3 r_cam_pos;
float r_near = 2.0f, r_far = 1000.0f;

/* constantes de la proyeccion de Unity (para calcular la curvatura igual que el original) */
static float P00, P11, ZA, ZB;
static float gx, gy;            /* banda de guarda en espacio de camara */
#define CLIP_NEAR 4.0f          /* plano cercano real de recorte */
#define GUARD 3.0f              /* en unidades NDC */

typedef struct { float u, v, x, y, z; } OV;   /* vertice enviado al GE */

/* --------------- estado del GE --------------- */
static int cur_tex = -2, cur_blend = -1;
static unsigned int cur_color = 0;
static float cur_st[4] = {-1, -1, -1, -1};
static int vram_tex_next;
static void *vram_tex_ptr[64];

static void *vram_alloc(int bytes)
{
    static unsigned int off = 0;
    if (!off) off = 0xCC000;
    off = (off + 15) & ~15;
    if (off + bytes > 0x200000) return NULL;
    void *p = (void *)(0x04000000 + off);
    off += bytes;
    return p;
}

void r_bind_texture(int t)
{
    if (t == cur_tex) return;
    cur_tex = t;
    if (t < 0) { sceGuDisable(GU_TEXTURE_2D); return; }
    sceGuEnable(GU_TEXTURE_2D);
    Texture *tx = &gd.tex[t];
    if (!tx->loaded) {
        data_tex_load(t);
        /* las texturas de 16 bits del escenario van a la VRAM si caben */
        if (tx->fmt != TEX_T8 && t < 64) {
            void *v = vram_alloc(tx->databytes);
            if (v) {
                memcpy((void *)((unsigned)v | 0x40000000), tx->data, tx->databytes);
                free(tx->data);
                tx->data = v;
                vram_tex_ptr[t] = v;
                vram_tex_next++;
            }
        }
        sceKernelDcacheWritebackAll();
    }
    switch (tx->fmt) {
    case TEX_T8:
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuClutLoad(256 / 8, tx->pal);
        sceGuTexMode(GU_PSM_T8, 0, 0, 1);
        break;
    case TEX_5650: sceGuTexMode(GU_PSM_5650, 0, 0, 1); break;
    case TEX_4444: sceGuTexMode(GU_PSM_4444, 0, 0, 1); break;
    default: sceGuTexMode(GU_PSM_8888, 0, 0, 1); break;
    }
    sceGuTexImage(0, tx->w, tx->h, tx->w, tx->data);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
}

static void set_blend(int b)
{
    if (b == cur_blend) return;
    cur_blend = b;
    switch (b) {
    case BLEND_OPAQUE:
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuDepthMask(GU_FALSE);
        break;
    case BLEND_ALPHA:
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0, 0xff);
        sceGuDepthMask(GU_TRUE);
        break;
    case BLEND_ADD:
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuDepthMask(GU_TRUE);
        break;
    case BLEND_ADDALPHA:
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuDepthMask(GU_TRUE);
        break;
    case BLEND_MUL:
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuDepthMask(GU_TRUE);
        break;
    }
}

static unsigned int col_abgr(const float *c, float amul)
{
    int r = (int)(clamp01(c[0]) * 255.0f), g = (int)(clamp01(c[1]) * 255.0f), b = (int)(clamp01(c[2]) * 255.0f);
    int a = (int)(clamp01(c[3] * amul) * 255.0f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

extern float r_mat_uoff[256];
int r_mesh_verts[512];
static int bind_material(int mi, float amul)
{
    if (mi < 0 || mi >= gd.nmats) return BLEND_OPAQUE;
    const MatFile *m = &gd.mats[mi];
    r_bind_texture(m->tex);
    int blend = m->blend;
    if (amul < 0.999f && blend == BLEND_OPAQUE) blend = BLEND_ALPHA;
    set_blend(blend);
    static const float white[4] = {1, 1, 1, 1};
    unsigned int col = col_abgr((blend == BLEND_OPAQUE || blend == BLEND_ALPHA) && m->color[3] < 0.01f ? white :
                                (blend == BLEND_OPAQUE ? white : m->color), amul);
    if (blend == BLEND_ALPHA && m->color[3] > 0.01f) col = col_abgr(m->color, amul);
    if (col != cur_color) { sceGuColor(col); cur_color = col; }
    /* uv' = uv * escala + desplazamiento (v ya viene invertida: v' = v*sv + 1 - sv - ov) */
    float st[4] = {m->st[0], m->st[1], m->st[2] + (mi < 256 ? r_mat_uoff[mi] : 0), 1.0f - m->st[1] - m->st[3]};
    if (memcmp(st, cur_st, sizeof(st))) {
        memcpy(cur_st, st, sizeof(st));
        sceGuTexScale(st[0], st[1]);
        sceGuTexOffset(st[2], st[3]);
    }
    return blend;
}

/* --------------- inicializacion y frames --------------- */
void r_init(void)
{
    fbp0 = (void *)0;
    fbp1 = (void *)(BUF_W * SCR_H * 2);
    zbp = (void *)(BUF_W * SCR_H * 4);
    sceGuInit();
    sceGuStart(GU_DIRECT, dlist);
    sceGuDrawBuffer(GU_PSM_5650, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
    sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuEnable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_DITHER);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    /* proyeccion de Unity: fov vertical 60, cerca 2, lejos 1000 */
    float f = 1.0f / tanf(60.0f * 0.5f * DEG2RAD);
    P11 = f;
    P00 = f / ((float)SCR_W / SCR_H);
    ZA = (r_far + r_near) / (r_far - r_near);
    ZB = -2.0f * r_far * r_near / (r_far - r_near);
    gx = GUARD / P00;
    gy = GUARD / P11;
}

void r_begin_frame(unsigned int clear)
{
    vpos = ipos = 0;
    r_stat_verts = r_stat_tris = r_stat_draws = r_stat_clipped = 0;
    sceGuStart(GU_DIRECT, dlist);
    cur_tex = -2; cur_blend = -1; cur_color = 0; cur_st[0] = -1;
    sceGuClearColor(clear);
    sceGuClearDepth(0);
    sceGuDepthMask(GU_FALSE);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(60.0f, (float)SCR_W / SCR_H, r_near, r_far);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuColor(0xffffffff); cur_color = 0xffffffff;
}

void r_end_frame(void)
{
    sceKernelDcacheWritebackAll();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}

void r_set_camera_matrix(const M34 *cam, float fov)
{
    (void)fov;
    m34_inverse(&r_view, cam);
    r_cam_pos = m34_pos(cam);
}

void r_set_camera(V3 pos, V3 target, float fov)
{
    /* Transform.LookAt con arriba = (0,1,0) */
    V3 fwd = v3norm(v3sub(target, pos));
    if (v3len(fwd) < 1e-6f) fwd = v3(0, 0, 1);
    V3 right = v3norm(v3cross(v3(0, 1, 0), fwd));
    if (v3len(right) < 1e-6f) right = v3(1, 0, 0);
    V3 up = v3cross(fwd, right);
    M34 c;
    c.m[0] = right.x; c.m[1] = right.y; c.m[2] = right.z;
    c.m[3] = up.x; c.m[4] = up.y; c.m[5] = up.z;
    c.m[6] = fwd.x; c.m[7] = fwd.y; c.m[8] = fwd.z;
    c.m[9] = pos.x; c.m[10] = pos.y; c.m[11] = pos.z;
    r_set_camera_matrix(&c, fov);
}

/* --------------- recorte --------------- */
enum { OC_NEAR = 1, OC_LEFT = 2, OC_RIGHT = 4, OC_BOTTOM = 8, OC_TOP = 16, OC_FAR = 32 };

static inline int outcode(float x, float y, float d)
{
    int c = 0;
    if (d < CLIP_NEAR) c |= OC_NEAR;
    if (d > r_far) c |= OC_FAR;
    float ex = gx * d, ey = gy * d;
    if (x < -ex) c |= OC_LEFT;
    if (x > ex) c |= OC_RIGHT;
    if (y < -ey) c |= OC_BOTTOM;
    if (y > ey) c |= OC_TOP;
    return c;
}

typedef struct { float u, v, x, y, d; } CV;

static inline float plane_dist(const CV *p, int pl)
{
    switch (pl) {
    case OC_NEAR: return p->d - CLIP_NEAR;
    case OC_LEFT: return p->x + gx * p->d;
    case OC_RIGHT: return gx * p->d - p->x;
    case OC_BOTTOM: return p->y + gy * p->d;
    case OC_TOP: return gy * p->d - p->y;
    default: return r_far - p->d;
    }
}

static int clip_plane(const CV *in, int n, CV *out, int pl)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const CV *a = &in[i], *b = &in[(i + 1) % n];
        float da = plane_dist(a, pl), db = plane_dist(b, pl);
        if (da >= 0) out[m++] = *a;
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            CV *o = &out[m++];
            o->u = a->u + (b->u - a->u) * t;
            o->v = a->v + (b->v - a->v) * t;
            o->x = a->x + (b->x - a->x) * t;
            o->y = a->y + (b->y - a->y) * t;
            o->d = a->d + (b->d - a->d) * t;
        }
    }
    return m;
}

/* --------------- envio de triangulos --------------- */
typedef struct {
    OV *verts;
    unsigned char *codes;
    int nv, maxv;
    uint16_t *idx;
    int ni, maxi;
} Batch;

static int batch_begin(Batch *b, int nv, int extra_v, int maxi)
{
    int vbytes = (nv + extra_v) * sizeof(OV);
    int ibytes = maxi * 2;
    if (vpos + vbytes + 64 > VRING_BYTES || ipos + ibytes + 64 > IRING_BYTES) return 0;
    b->verts = (OV *)(vring + vpos);
    b->nv = nv;
    b->maxv = nv + extra_v;
    b->idx = (uint16_t *)(iring + ipos);
    b->ni = 0;
    b->maxi = maxi;
    return 1;
}

static void batch_tris(Batch *b, const uint16_t *src, int n)
{
    OV *V = b->verts;
    const unsigned char *C = b->codes;
    for (int i = 0; i + 2 < n; i += 3) {
        int i0 = src[i], i1 = src[i + 1], i2 = src[i + 2];
        int c0 = C[i0], c1 = C[i1], c2 = C[i2];
        if (c0 & c1 & c2) continue;
        if (!(c0 | c1 | c2) || !((c0 | c1 | c2) & ~OC_FAR)) {
            if (b->ni + 3 > b->maxi) return;
            b->idx[b->ni++] = i0; b->idx[b->ni++] = i1; b->idx[b->ni++] = i2;
            continue;
        }
        /* recorte por software del triangulo */
        CV pa[12], pb[12];
        int ids[3] = {i0, i1, i2};
        for (int k = 0; k < 3; k++) {
            OV *o = &V[ids[k]];
            pa[k].u = o->u; pa[k].v = o->v; pa[k].x = o->x; pa[k].y = o->y; pa[k].d = -o->z;
        }
        int cnt = 3, all = c0 | c1 | c2;
        CV *in = pa, *out = pb;
        static const int planes[5] = {OC_NEAR, OC_LEFT, OC_RIGHT, OC_BOTTOM, OC_TOP};
        for (int p = 0; p < 5 && cnt >= 3; p++) {
            if (!(all & planes[p])) continue;
            cnt = clip_plane(in, cnt, out, planes[p]);
            CV *t = in; in = out; out = t;
        }
        if (cnt < 3) continue;
        if (b->nv + cnt > b->maxv || b->ni + (cnt - 2) * 3 > b->maxi) return;
        int base = b->nv;
        for (int k = 0; k < cnt; k++) {
            OV *o = &V[b->nv++];
            o->u = in[k].u; o->v = in[k].v; o->x = in[k].x; o->y = in[k].y; o->z = -in[k].d;
        }
        for (int k = 1; k + 1 < cnt; k++) {
            b->idx[b->ni++] = base; b->idx[b->ni++] = base + k; b->idx[b->ni++] = base + k + 1;
        }
        r_stat_clipped++;
    }
}

static void batch_commit(Batch *b)
{
    vpos += (b->nv * sizeof(OV) + 63) & ~63;
    ipos += (b->ni * 2 + 63) & ~63;
}

/* --------------- objetos transparentes diferidos --------------- */
typedef struct { int mesh; M34 world; int mats[4]; int nmats; float amul, dist; } Deferred;
#define MAX_DEFER 256
static Deferred defer[MAX_DEFER];
static int ndefer;

static unsigned char codebuf[65536 + 4096];

/* 0 = fuera, 1 = dentro de la banda de guarda (no hace falta recortar), 2 = hay que recortar */
static int sphere_visible(V3 c, float r)
{
    V3 p = m34_point(&r_view, c);
    float d = p.z;
    if (d + r < r_near) return 0;
    if (d - r > r_far) return 0;
    /* se aplica la curvatura al centro (como hace el shader con cada vertice) */
    float zc = ZA * d + ZB, zz = zc * zc;
    float x = p.x + (-0.0005f / P00) * zz, y = p.y + (-0.0003f / P11) * zz;
    /* margen por la curvatura dentro del propio objeto */
    float rr = r * 1.15f + 0.0012f * r * (d > 0 ? d : 0);
    float nx = P00, ny = P11;
    float lx = sqrtf(nx * nx + 1.0f), ly = sqrtf(ny * ny + 1.0f);
    float dl = (nx * x + d) / lx, dr = (-nx * x + d) / lx, db = (ny * y + d) / ly, dt = (-ny * y + d) / ly;
    if (dl < -rr || dr < -rr || db < -rr || dt < -rr) return 0;
    /* completamente dentro de la banda de guarda y delante del plano cercano? */
    if (d - rr <= CLIP_NEAR) return 2;
    float gxn = 1.0f / gx, gyn = 1.0f / gy;     /* planos de la banda: |x| <= gx*d */
    float gl = sqrtf(gxn * gxn + 1.0f), gll = sqrtf(gyn * gyn + 1.0f);
    if ((gxn * x + d) / gl < rr || (-gxn * x + d) / gl < rr) return 2;
    if ((gyn * y + d) / gll < rr || (-gyn * y + d) / gll < rr) return 2;
    return 1;
}

static void transform_fast(OV *o, const MVert *src, int nv, const M34 *mv, float kx, float ky)
{
    const float *m = mv->m;
    const float m0 = m[0], m1 = m[1], m2 = m[2], m3 = m[3], m4 = m[4], m5 = m[5], m6 = m[6], m7 = m[7], m8 = m[8];
    const float m9 = m[9], m10 = m[10], m11 = m[11], za = ZA, zb = ZB;
    for (int i = 0; i < nv; i++, src++, o++) {
        float px = src->x, py = src->y, pz = src->z;
        float d = m2 * px + m5 * py + m8 * pz + m11;
        float zc = za * d + zb;
        float zz = zc * zc;
        o->u = src->u; o->v = src->v;
        o->x = m0 * px + m3 * py + m6 * pz + m9 + kx * zz;
        o->y = m1 * px + m4 * py + m7 * pz + m10 + ky * zz;
        o->z = -d;
    }
}

static void transform_into(Batch *b, const MVert *src, int nv, const M34 *mv, float kx, float ky)
{
    const float *m = mv->m;
    OV *o = b->verts;
    unsigned char *c = codebuf;
    for (int i = 0; i < nv; i++, src++, o++) {
        float px = src->x, py = src->y, pz = src->z;
        float x = m[0] * px + m[3] * py + m[6] * pz + m[9];
        float y = m[1] * px + m[4] * py + m[7] * pz + m[10];
        float d = m[2] * px + m[5] * py + m[8] * pz + m[11];
        float zc = ZA * d + ZB;
        float zz = zc * zc;
        x += kx * zz;
        y += ky * zz;
        o->u = src->u; o->v = src->v; o->x = x; o->y = y; o->z = -d;
        c[i] = outcode(x, y, d);
    }
    b->codes = codebuf;
}

float r_mat_uoff[256];
int r_mesh_verts[512];

static void draw_mesh_now(int mi, const M34 *world, const int *mats, int nmats, float amul)
{
    Mesh *me = &gd.meshes[mi];
    if (me->nparts <= 0) return;
    M34 mv;
    m34_mul(&mv, &r_view, world);
    float kx = -0.0005f / P00, ky = -0.0003f / P11;
    int first_mat = -1;
    for (int s = 0; s < nmats; s++) if (mats[s] >= 0) { first_mat = mats[s]; break; }
    if (first_mat < 0) return;
    int sky = 0;
    if (first_mat < gd.nmats) {
        kx = gd.mats[first_mat].distort[0] / P00;
        ky = gd.mats[first_mat].distort[1] / P11;
        if (gd.mats[first_mat].sky) { kx = ky = 0; sky = 1; }
    }
    if (sky) { sceGuDisable(GU_CULL_FACE); sceGuDisable(GU_DEPTH_TEST); }
    float sc = v3len(m34_col(world, 0));
    float s1 = v3len(m34_col(world, 1)), s2 = v3len(m34_col(world, 2));
    if (s1 > sc) sc = s1;
    if (s2 > sc) sc = s2;
    for (int k = 0; k < me->nparts; k++) {
        MeshPart *pt = &me->parts[k];
        int mat = pt->sub < nmats ? mats[pt->sub] : (nmats > 0 ? mats[nmats - 1] : -1);
        if (mat < 0) continue;
        int vis = sky ? 2 : sphere_visible(m34_point(world, pt->center), pt->radius * sc);
        if (!vis) continue;
        if (vis == 1) {
            /* camino rapido: todo dentro de la banda de guarda, se usan los indices originales */
            int bytes = pt->nv * sizeof(OV);
            if (vpos + bytes + 64 > VRING_BYTES) break;
            OV *ov = (OV *)(vring + vpos);
            transform_fast(ov, pt->verts, pt->nv, &mv, kx, ky);
            vpos += (bytes + 63) & ~63;
            r_stat_verts += pt->nv;
            if (mi < 512) r_mesh_verts[mi] += pt->nv;
            bind_material(mat, amul);
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                           pt->ni, pt->idx, ov);
            r_stat_draws++;
            r_stat_tris += pt->ni / 3;
            continue;
        }
        Batch b;
        if (!batch_begin(&b, pt->nv, 256 + pt->ni / 3, pt->ni + 1024)) break;
        transform_into(&b, pt->verts, pt->nv, &mv, kx, ky);
        r_stat_verts += pt->nv;
        if (mi < 512) r_mesh_verts[mi] += pt->nv;
        batch_tris(&b, pt->idx, pt->ni);
        if (b.ni > 0) {
            bind_material(mat, amul);
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                           b.ni, b.idx, b.verts);
            r_stat_draws++;
            r_stat_tris += b.ni / 3;
        }
        batch_commit(&b);
    }
    if (sky) { sceGuEnable(GU_CULL_FACE); sceGuEnable(GU_DEPTH_TEST); }
}

void r_draw_mesh(int mi, const M34 *world, const int *mats, int nmats, float amul)
{
    if (mi < 0 || mi >= gd.nmeshes) return;
    Mesh *me = &gd.meshes[mi];
    V3 c = m34_point(world, me->center);
    float sc = v3len(m34_col(world, 0));
    float s1 = v3len(m34_col(world, 1)), s2 = v3len(m34_col(world, 2));
    if (s1 > sc) sc = s1;
    if (s2 > sc) sc = s2;
    if (!sphere_visible(c, me->radius * sc)) return;
    int transp = amul < 0.999f;
    for (int s = 0; s < nmats && !transp; s++)
        if (mats[s] >= 0 && mats[s] < gd.nmats && gd.mats[mats[s]].blend != BLEND_OPAQUE) transp = 1;
    if (transp) {
        if (ndefer < MAX_DEFER) {
            Deferred *d = &defer[ndefer++];
            d->mesh = mi; d->world = *world; d->nmats = nmats > 4 ? 4 : nmats;
            for (int s = 0; s < d->nmats; s++) d->mats[s] = mats[s];
            d->amul = amul;
            d->dist = m34_point(&r_view, c).z;
        }
        return;
    }
    draw_mesh_now(mi, world, mats, nmats, amul);
}

static int cmp_defer(const void *a, const void *b)
{
    float da = ((const Deferred *)a)->dist, db = ((const Deferred *)b)->dist;
    return da < db ? 1 : (da > db ? -1 : 0);
}

void r_flush_transparent(void)
{
    qsort(defer, ndefer, sizeof(Deferred), cmp_defer);
    for (int i = 0; i < ndefer; i++)
        draw_mesh_now(defer[i].mesh, &defer[i].world, defer[i].mats, defer[i].nmats, defer[i].amul);
    ndefer = 0;
    set_blend(BLEND_OPAQUE);
}

void r_draw_skinned(int mi, const M34 *bones, int nbones, const int *mats, int nmats)
{
    if (mi < 0 || mi >= gd.nmeshes) return;
    Mesh *me = &gd.meshes[mi];
    if (!me->skinned) return;
    static M34 vb[64];
    int nb = nbones < 64 ? nbones : 64;
    for (int i = 0; i < nb; i++) m34_mul(&vb[i], &r_view, &bones[i]);
    float kx = -0.0005f / P00, ky = -0.0003f / P11;
    for (int k = 0; k < me->nparts; k++) {
        MeshPart *pt = &me->parts[k];
        int mat = pt->sub < nmats ? mats[pt->sub] : (nmats > 0 ? mats[nmats - 1] : -1);
        if (mat < 0) continue;
        Batch b;
        if (!batch_begin(&b, pt->nv, 256 + pt->ni / 3, pt->ni + 1024)) return;
        OV *o = b.verts;
        for (int i = 0; i < pt->nv; i++, o++) {
            const MVert *v = &pt->verts[i];
            const MSkin *sk = &pt->skin[i];
            float x = 0, y = 0, d = 0;
            for (int q = 0; q < 4; q++) {
                float w = sk->w[q];
                if (w <= 0.0f) continue;
                int bi = sk->b[q];
                if (bi >= nb) bi = 0;
                const float *m = vb[bi].m;
                x += w * (m[0] * v->x + m[3] * v->y + m[6] * v->z + m[9]);
                y += w * (m[1] * v->x + m[4] * v->y + m[7] * v->z + m[10]);
                d += w * (m[2] * v->x + m[5] * v->y + m[8] * v->z + m[11]);
            }
            float zc = ZA * d + ZB, zz = zc * zc;
            x += kx * zz; y += ky * zz;
            o->u = v->u; o->v = v->v; o->x = x; o->y = y; o->z = -d;
            codebuf[i] = outcode(x, y, d);
        }
        b.codes = codebuf;
        r_stat_verts += pt->nv;
        batch_tris(&b, pt->idx, pt->ni);
        if (b.ni > 0) {
            bind_material(mat, 1.0f);
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                           b.ni, b.idx, b.verts);
            r_stat_draws++;
            r_stat_tris += b.ni / 3;
        }
        batch_commit(&b);
    }
}

/* --------------- 2D --------------- */
typedef struct { float u, v; unsigned int c; float x, y, z; } SV;

void r_begin_2d(void)
{
    set_blend(BLEND_ALPHA);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    cur_st[0] = -1;
    sceGuColor(0xffffffff); cur_color = 0xffffffff;
}

void r_sprite(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned int abgr)
{
    r_bind_texture(tex);
    SV *v = (SV *)sceGuGetMemory(2 * sizeof(SV));
    float tw = 1, th = 1;
    if (tex >= 0) { tw = gd.tex[tex].w; th = gd.tex[tex].h; }
    v[0].u = u0 * tw; v[0].v = v0 * th; v[0].c = abgr; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].u = u1 * tw; v[1].v = v1 * th; v[1].c = abgr; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}

void r_rect(float x, float y, float w, float h, unsigned int abgr)
{
    r_bind_texture(-1);
    SV *v = (SV *)sceGuGetMemory(2 * sizeof(SV));
    v[0].u = 0; v[0].v = 0; v[0].c = abgr; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].u = 0; v[1].v = 0; v[1].c = abgr; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}
