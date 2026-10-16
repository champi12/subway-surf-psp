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
#include <stdio.h>
#include "render.h"
#include "data.h"

#define BUF_W 512
#define LIST_WORDS (256 * 1024)
#define VRING_BYTES (2 * 1024 * 1024)
#define IRING_BYTES (512 * 1024)

/* todo va por duplicado: la CPU prepara un fotograma mientras el GE dibuja el anterior */
static unsigned int __attribute__((aligned(64))) dlists[2][LIST_WORDS];
static unsigned int __attribute__((aligned(64))) kick_list[2][64];
static unsigned char __attribute__((aligned(64))) vrings[2][VRING_BYTES];
static unsigned char __attribute__((aligned(64))) irings[2][IRING_BYTES];
static unsigned char *vring = vrings[0], *iring = irings[0];
static unsigned int *dlist = dlists[0];
static int frame_par, ge_busy;
static void vfpu_selftest(void);
static int model_set;
static int vpos, ipos;
static void *fbp0, *fbp1, *zbp;
static int cur_fb;

int r_stat_verts, r_stat_tris, r_stat_draws, r_stat_clipped;
M34 r_view;
V3 r_cam_pos;
float r_near = 2.0f, r_far = 1000.0f;
/* el original es vertical (retrato) con fov 60; en la pantalla apaisada de la PSP 50 encuadra parecido */
float r_fov = 50.0f;

/* constantes de la proyeccion de Unity (para calcular la curvatura igual que el original) */
static float P00, P11, ZA, ZB;
static float gx, gy;            /* banda de guarda en espacio de camara */
#ifndef HW_OK
#define HW_OK 1
#endif
#ifndef FAST_OK
#define FAST_OK 1
#endif
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
extern int r_mesh_verts[512];
extern unsigned int r_prof[8];
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
    sceGuStart(GU_DIRECT, dlists[0]);
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
    /* proyeccion de Unity: cerca 2, lejos 1000 */
    float f = 1.0f / tanf(r_fov * 0.5f * DEG2RAD);
    P11 = f;
    P00 = f / ((float)SCR_W / SCR_H);
    ZA = (r_far + r_near) / (r_far - r_near);
    ZB = -2.0f * r_far * r_near / (r_far - r_near);
    gx = GUARD / P00;
    gy = GUARD / P11;
    vfpu_selftest();
}

void r_begin_frame(unsigned int clear)
{
    vpos = ipos = 0;
    vring = vrings[frame_par];
    iring = irings[frame_par];
    dlist = dlists[frame_par];
    r_stat_verts = r_stat_tris = r_stat_draws = r_stat_clipped = r_stat_hw = 0;
    model_set = 0;
    /* lista "llamable": se construye mientras el GE sigue con el fotograma anterior */
    sceGuStart(GU_CALL, dlist);
    cur_tex = -2; cur_blend = -1; cur_color = 0; cur_st[0] = -1;
    sceGuDrawBufferList(GU_PSM_5650, frame_par ? fbp1 : fbp0, BUF_W);
    sceGuClearColor(clear);
    sceGuClearDepth(0);
    sceGuDepthMask(GU_FALSE);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(r_fov, (float)SCR_W / SCR_H, r_near, r_far);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuColor(0xffffffff); cur_color = 0xffffffff;
}

float r_wait_ms;

void r_end_frame(void)
{
    sceGuFinish();
    sceKernelDcacheWritebackAll();
    unsigned int t0 = sceKernelGetSystemTimeLow();
    /* espera a que el GE termine el fotograma anterior y lo muestra en el siguiente VBlank */
    if (ge_busy) sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    if (ge_busy) sceDisplaySetFrameBuf((void *)(0x04000000 + (unsigned)(frame_par ? fbp0 : fbp1)), BUF_W,
                                       PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_IMMEDIATE);
    r_wait_ms = (sceKernelGetSystemTimeLow() - t0) / 1000.0f;
    /* lanza el fotograma nuevo */
    sceGuStart(GU_DIRECT, kick_list[frame_par]);
    sceGuCallList(dlist);
    sceGuFinish();
    ge_busy = 1;
    frame_par ^= 1;
}

void r_present_last(void)
{
    if (!ge_busy) return;
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf((void *)(0x04000000 + (unsigned)(frame_par ? fbp0 : fbp1)), BUF_W,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_IMMEDIATE);
    ge_busy = 0;
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

/* Version VFPU: la matriz 4x4 entera en un registro de matriz, una instruccion por vertice */
static int use_vfpu = 0;
static float __attribute__((aligned(16))) vf_mat[16];
static float __attribute__((aligned(16))) vf_par[4];

static void transform_vfpu(OV *o, const MVert *src, int nv, const M34 *mv, float kx, float ky)
{
    const float *m = mv->m;
    for (int c = 0; c < 4; c++) {
        vf_mat[c * 4 + 0] = m[c * 3 + 0];
        vf_mat[c * 4 + 1] = m[c * 3 + 1];
        vf_mat[c * 4 + 2] = m[c * 3 + 2];
        vf_mat[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
    }
    vf_par[0] = kx; vf_par[1] = ky; vf_par[2] = ZA; vf_par[3] = ZB;
    __asm__ volatile(
        "lv.q   C000, 0(%0)\n"
        "lv.q   C010, 16(%0)\n"
        "lv.q   C020, 32(%0)\n"
        "lv.q   C030, 48(%0)\n"
        "lv.q   C200, 0(%1)\n"
        : : "r"(vf_mat), "r"(vf_par) : "memory");
    for (int i = 0; i < nv; i++, src++, o++) {
        __asm__ volatile(
            "lv.s   S100, 8(%1)\n"
            "lv.s   S101, 12(%1)\n"
            "lv.s   S102, 16(%1)\n"
            "vhtfm4.q C110, E000, C100\n"     /* (x, y, d, 1) */
            "vmul.s S120, S112, S202\n"       /* d * za */
            "vadd.s S120, S120, S203\n"       /* zc */
            "vmul.s S120, S120, S120\n"       /* zc^2 */
            "vscl.p C132, C200, S120\n"       /* (kx, ky) * zc^2 */
            "vadd.p C110, C110, C132\n"       /* x, y curvados */
            "vneg.s S112, S112\n"
            "sv.s   S110, 8(%0)\n"
            "sv.s   S111, 12(%0)\n"
            "sv.s   S112, 16(%0)\n"
            : : "r"(o), "r"(src) : "memory");
        o->u = src->u; o->v = src->v;
    }
}

static void transform_fast_c(OV *o, const MVert *src, int nv, const M34 *mv, float kx, float ky);

/* Matriz del GE que aproxima la curvatura alrededor de un punto (desarrollo de Taylor de primer orden):
 * x' = x + kx*zc^2 con zc = ZA*d + ZB  ->  lineal en d cerca de d0. Error kx*ZA^2*(d-d0)^2: para objetos de
 * menos de 20 unidades es menor que 0.2 unidades (invisible). */
static void bent_gumatrix(ScePspFMatrix4 *out, const M34 *mv, float d0, float kx, float ky)
{
    float zc0 = ZA * d0 + ZB;
    float gxk = 2.0f * kx * zc0 * ZA, cxk = kx * (zc0 * zc0 - 2.0f * zc0 * ZA * d0);
    float gyk = 2.0f * ky * zc0 * ZA, cyk = ky * (zc0 * zc0 - 2.0f * zc0 * ZA * d0);
    const float *m = mv->m;
    float *o = (float *)out;
    for (int c = 0; c < 4; c++) {
        float x = m[c * 3], y = m[c * 3 + 1], d = m[c * 3 + 2];
        o[c * 4 + 0] = x + gxk * d + (c == 3 ? cxk : 0.0f);
        o[c * 4 + 1] = y + gyk * d + (c == 3 ? cyk : 0.0f);
        o[c * 4 + 2] = -d;
        o[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
    }
}

static void model_identity(void)
{
    if (!model_set) return;
    ScePspFMatrix4 id;
    memset(&id, 0, sizeof(id));
    id.x.x = id.y.y = id.z.z = id.w.w = 1.0f;
    sceGuSetMatrix(GU_MODEL, &id);
    model_set = 0;
}

int r_stat_hw;

static void transform_fast(OV *o, const MVert *src, int nv, const M34 *mv, float kx, float ky)
{
    if (use_vfpu) transform_vfpu(o, src, nv, mv, kx, ky);
    else transform_fast_c(o, src, nv, mv, kx, ky);
}

/* comprueba al arrancar que la VFPU da lo mismo que la version en C */
static void vfpu_selftest(void)
{
    MVert v[3] = {{0.1f, 0.2f, 3.0f, -4.0f, 50.0f}, {0.5f, 0.5f, -20.0f, 10.0f, 300.0f}, {0, 1, 7.5f, 2.5f, 900.0f}};
    OV a[3], b[3];
    M34 m;
    m34_trs(&m, v3(1, -33, 33), qeuler(21.5f, 3.0f, 0), v3(1, 1, 1));
    transform_fast_c(a, v, 3, &m, -0.0005f / P00, -0.0003f / P11);
    transform_vfpu(b, v, 3, &m, -0.0005f / P00, -0.0003f / P11);
    use_vfpu = 1;
#ifdef AUTOTEST
    {
        FILE *f = fopen("vfpu.log", "w");
        if (f) {
            for (int i = 0; i < 3; i++) fprintf(f, "C %f %f %f %f %f | V %f %f %f %f %f\n", a[i].u, a[i].v, a[i].x, a[i].y, a[i].z,
                                                b[i].u, b[i].v, b[i].x, b[i].y, b[i].z);
            fclose(f);
        }
    }
#endif
    for (int i = 0; i < 3; i++)
        if (fabsf(a[i].x - b[i].x) > 0.01f || fabsf(a[i].y - b[i].y) > 0.01f || fabsf(a[i].z - b[i].z) > 0.01f ||
            a[i].u != b[i].u) use_vfpu = 0;
}

int r_using_vfpu(void) { return use_vfpu; }

static void transform_fast_c(OV *o, const MVert *src, int nv, const M34 *mv, float kx, float ky)
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
unsigned int r_prof[8];   /* microsegundos: 0 rapido, 1 recorte, 2 piel, 3 culling */

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
        unsigned int tq0 = sceKernelGetSystemTimeLow();
        int vis = sky ? 2 : sphere_visible(m34_point(world, pt->center), pt->radius * sc);
        r_prof[3] += sceKernelGetSystemTimeLow() - tq0;
        if (!vis) continue;
        float d0 = vis == 1 ? m34_point(&mv, pt->center).z : 0;
        float pr = pt->radius * sc;
        /* objetos pequenos o bloques lejanos: el error de linealizar la curvatura es menor de un pixel */
        if (vis == 1 && HW_OK && (pr <= 20.0f || (pr <= 64.0f && d0 - pr > 550.0f))) {
            /* lo transforma el GE con la curvatura linealizada (sin trabajo de CPU) */
            ScePspFMatrix4 *gm = (ScePspFMatrix4 *)sceGuGetMemory(sizeof(ScePspFMatrix4));
            bent_gumatrix(gm, &mv, d0, kx, ky);
            sceGuSetMatrix(GU_MODEL, gm);
            model_set = 1;
            bind_material(mat, amul);
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                           pt->ni, pt->idx, pt->verts);
            r_stat_draws++;
            r_stat_hw += pt->nv;
            r_stat_tris += pt->ni / 3;
            continue;
        }
        model_identity();
        if (vis == 1 && FAST_OK) {
            /* camino rapido: todo dentro de la banda de guarda, se usan los indices originales */
            int bytes = pt->nv * sizeof(OV);
            if (vpos + bytes + 64 > VRING_BYTES) break;
            OV *ov = (OV *)(vring + vpos);
            unsigned int tp0 = sceKernelGetSystemTimeLow();
            transform_fast(ov, pt->verts, pt->nv, &mv, kx, ky);
            r_prof[0] += sceKernelGetSystemTimeLow() - tp0;
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
        unsigned int tc0 = sceKernelGetSystemTimeLow();
        transform_into(&b, pt->verts, pt->nv, &mv, kx, ky);
        r_stat_verts += pt->nv;
        if (mi < 512) r_mesh_verts[mi] += pt->nv;
        batch_tris(&b, pt->idx, pt->ni);
        r_prof[1] += sceKernelGetSystemTimeLow() - tc0;
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
    model_identity();
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
    unsigned int ts0 = sceKernelGetSystemTimeLow();
    int nb = nbones < 64 ? nbones : 64;
    if (me->nbatches > 0 && nb > 0) {
        /* centro aproximado: media de las posiciones de los huesos */
        V3 c = v3(0, 0, 0);
        for (int i = 0; i < nb; i++) c = v3add(c, m34_pos(&bones[i]));
        c = v3scale(c, 1.0f / nb);
        if (sphere_visible(c, 12.0f) == 1) {
            float d0 = m34_point(&r_view, c).z;
            M34 idm; m34_identity(&idm);
            ScePspFMatrix4 bent;
            bent_gumatrix(&bent, &r_view, d0, -0.0005f / P00, -0.0003f / P11);
            /* la matriz "bent" (camara curvada) por cada hueso */
            M34 bv;
            const float *B = (const float *)&bent;
            for (int c2 = 0; c2 < 4; c2++)
                for (int r = 0; r < 3; r++) bv.m[c2 * 3 + r] = B[c2 * 4 + r];
            /* bent tiene z = -d: el GE recibe directamente coordenadas de vista */
            ScePspFMatrix4 id;
            memset(&id, 0, sizeof(id));
            id.x.x = id.y.y = id.z.z = id.w.w = 1.0f;
            sceGuSetMatrix(GU_MODEL, &id);
            model_set = 0;
            for (int k = 0; k < me->nbatches; k++) {
                SkinBatch *sb = &me->batches[k];
                int mat = sb->sub < nmats ? mats[sb->sub] : (nmats > 0 ? mats[nmats - 1] : -1);
                if (mat < 0) continue;
                for (int j = 0; j < sb->nb; j++) {
                    int bi = sb->bones[j] < nb ? sb->bones[j] : 0;
                    M34 r2;
                    m34_mul(&r2, &bv, &bones[bi]);
                    ScePspFMatrix4 *gm = (ScePspFMatrix4 *)sceGuGetMemory(sizeof(ScePspFMatrix4));
                    float *o = (float *)gm;
                    for (int c2 = 0; c2 < 4; c2++) {
                        o[c2 * 4 + 0] = r2.m[c2 * 3]; o[c2 * 4 + 1] = r2.m[c2 * 3 + 1]; o[c2 * 4 + 2] = r2.m[c2 * 3 + 2];
                        o[c2 * 4 + 3] = c2 == 3 ? 1.0f : 0.0f;
                    }
                    sceGuBoneMatrix(j, gm);
                }
                bind_material(mat, 1.0f);
                sceGuDrawArray(GU_TRIANGLES, GU_WEIGHTS(sb->nb) | GU_WEIGHT_32BITF | GU_TEXTURE_32BITF | GU_VERTEX_32BITF |
                               GU_INDEX_16BIT | GU_TRANSFORM_3D, sb->ni, sb->idx, sb->verts);
                r_stat_draws++;
                r_stat_hw += sb->nv;
                r_stat_tris += sb->ni / 3;
            }
            r_prof[2] += sceKernelGetSystemTimeLow() - ts0;
            return;
        }
    }
    model_identity();
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
    r_prof[2] += sceKernelGetSystemTimeLow() - ts0;
}

/* --------------- 2D --------------- */
typedef struct { float u, v; unsigned int c; float x, y, z; } SV;

void r_begin_2d(void)
{
    model_identity();
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
