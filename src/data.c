/* data.c - carga de world.bin, mat.bin, mesh.bin, tex.bin, anim.bin y sfx.bin */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <malloc.h>
#include "data.h"
#include "devlog.h"
#include "audio.h"
#include <pspkernel.h>
#include <pspiofilemgr.h>

GameData gd;
static char errbuf[128];
static char basedir[128];

const char *data_err(void) { return errbuf; }

static void *load_file(const char *name, int *size)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", basedir, name);
    unsigned int t0 = sceKernelGetSystemTimeLow();
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(errbuf, sizeof(errbuf), "No se encuentra %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    int n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = memalign(64, n + 16);
    if (!buf) { fclose(f); snprintf(errbuf, sizeof(errbuf), "Sin memoria para %s (%d KB)", name, n / 1024); return NULL; }
    int got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) { snprintf(errbuf, sizeof(errbuf), "Error leyendo %s (%d de %d bytes)", name, got, n); free(buf); return NULL; }
    dlog("CARGA %-10s %6d KB en %4.0f ms", name, n / 1024, (sceKernelGetSystemTimeLow() - t0) / 1000.0f);
    if (size) *size = n;
    return buf;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void *load_file_opt(const char *path);

/* archivo opcional de la carpeta del mundo (fx.bin...): NULL si no esta. Se queda en memoria */
void *data_load_world_opt(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", basedir, name);
    return load_file_opt(path);
}
#define load_file_opt_world(n) ((uint8_t *)data_load_world_opt(n))

/* archivo opcional con su ruta (sin error si no esta); se queda en memoria */
static void *load_file_opt(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    int n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = memalign(64, n + 16);
    if (buf && (int)fread(buf, 1, n, f) != n) { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

static int load_world(void)
{
    int size;
    uint8_t *b = load_file("world.bin", &size);
    if (!b) return -1;
    if (memcmp(b, "SSW1", 4)) { snprintf(errbuf, sizeof(errbuf), "world.bin no valido"); return -1; }
    gd.nnodes = rd32(b + 4);
    uint32_t nblob = rd32(b + 8);
    gd.nodes = (NodeFile *)(b + 12);
    gd.blob = (uint32_t *)(b + 12 + gd.nnodes * sizeof(NodeFile));
    uint8_t *p = (uint8_t *)(gd.blob + nblob);
    gd.nstrings = rd32(p); p += 4;
    gd.strings = malloc(sizeof(char *) * gd.nstrings);
    /* las cadenas se copian a memoria propia terminada en 0 */
    for (int i = 0; i < gd.nstrings; i++) {
        int l = *p++;
        char *s = malloc(l + 1);
        memcpy(s, p, l); s[l] = 0;
        gd.strings[i] = s;
        p += l;
    }
    return 0;
}

static int load_mats(void)
{
    uint8_t *b = load_file("mat.bin", NULL);
    if (!b) return -1;
    gd.nmats = rd32(b + 4);
    gd.mats = calloc(gd.nmats, sizeof(MatFile));
    uint8_t *p = b + 8;
    for (int i = 0; i < gd.nmats; i++) {
        MatFile *m = &gd.mats[i];
        int l = p[0]; if (l > 31) l = 31;
        memcpy(m->name, p + 1, l); m->name[l] = 0;
        p += 32;
        memcpy(&m->tex, p, 4); memcpy(&m->blend, p + 4, 4);
        memcpy(m->st, p + 8, 16); memcpy(m->color, p + 24, 16); memcpy(m->distort, p + 40, 8);
        memcpy(&m->sky, p + 48, 4);
        p += 52;
    }
    free(b);
    return 0;
}

/* lee un archivo SSM3 (mesh.bin o hw_mesh.bin) */
static void parse_mesh(uint8_t *p, Mesh *m);

static Mesh *parse_meshes(uint8_t *b, int *count)
{
    int n = rd32(b + 4);
    Mesh *meshes = calloc(n ? n : 1, sizeof(Mesh));
    *count = n;
    for (int i = 0; i < n; i++) parse_mesh(b + rd32(b + 8 + 4 * i), &meshes[i]);
    return meshes;
}

/* una malla de un archivo SSM3: los vertices quedan apuntando dentro de p (que no se puede liberar mientras se use) */
static void parse_mesh(uint8_t *p, Mesh *m)
{
    {
        int l = p[0]; if (l > 31) l = 31;
        memcpy(m->name, p + 1, l); m->name[l] = 0;
        p += 32;
        uint16_t np, nsub;
        memcpy(&np, p, 2); memcpy(&nsub, p + 2, 2);
        m->skinned = p[4];
        int nlod = p[5] > 2 ? 2 : p[5];
        p += 8;
        float h[10];
        memcpy(h, p, 40); p += 40;
        m->nparts = np; m->nsub = nsub;
        m->center = v3(h[0], h[1], h[2]); m->radius = h[3];
        m->bmin = v3(h[4], h[5], h[6]); m->bmax = v3(h[7], h[8], h[9]);
        m->parts = calloc(np ? np : 1, sizeof(MeshPart));
        for (int k = 0; k < np; k++) {
            MeshPart *pt = &m->parts[k];
            uint16_t si, nv; uint32_t ni; float pc[4];
            memcpy(&si, p, 2); memcpy(&nv, p + 2, 2); memcpy(&ni, p + 4, 4); memcpy(pc, p + 8, 16);
            p += 24;
            pt->sub = si; pt->nv = nv; pt->ni = ni;
            pt->center = v3(pc[0], pc[1], pc[2]); pt->radius = pc[3];
            pt->verts = (MVert *)p;
            p += nv * sizeof(MVert);
            pt->idx = (uint16_t *)p;
            p += ni * 2;
            if (ni & 1) p += 2;
            if (m->skinned) { pt->skin = (MSkin *)p; p += nv * sizeof(MSkin); }
            for (int l = 0; l < nlod; l++) {
                uint32_t li = rd32(p); p += 4;
                pt->lod_ni[l] = (int)li;
                pt->lod_idx[l] = (uint16_t *)p;
                p += li * 2;
                if (li & 1) p += 2;
            }
        }
        if (m->skinned) {
            m->nbatches = rd32(p); p += 4;
            m->batches = calloc(m->nbatches ? m->nbatches : 1, sizeof(SkinBatch));
            for (int k = 0; k < m->nbatches; k++) {
                SkinBatch *sb = &m->batches[k];
                uint16_t hh[4];
                memcpy(hh, p, 8); p += 8;
                sb->sub = hh[0]; sb->nb = hh[1]; sb->nv = hh[2];
                sb->ni = rd32(p); p += 4;
                memcpy(sb->bones, p, 8); p += 8;
                sb->verts = (float *)p;
                p += sb->nv * (sb->nb + 5) * 4;
                sb->idx = (uint16_t *)p;
                p += sb->ni * 2;
                if (sb->ni & 1) p += 2;
            }
        }
        m->nbind = rd32(p); p += 4;
        if (m->nbind) {
            m->bind = malloc(sizeof(M34) * m->nbind);
            for (int k = 0; k < m->nbind; k++) {
                float f[16];
                memcpy(f, p, 64); p += 64;
                for (int c = 0; c < 4; c++)
                    for (int r = 0; r < 3; r++) m->bind[k].m[c * 3 + r] = f[c * 4 + r];
            }
        }
    }
}

/* Cada malla de mesh.bin va en su propio bloque de memoria (no todo el archivo en uno): las del escenario con version
 * de Navidad se cambian leyendo la otra version de su archivo encima, sin tener nunca las dos en memoria (son ~2.9 MB
 * y el monton de la consola es de 18 MB). Como las texturas de los temas */
static uint32_t *mesh_foff, *mesh_flen;     /* posicion y tamano de cada malla en mesh.bin */
static void **mesh_blk;                     /* bloque de memoria de cada malla de mesh.bin */
static int mesh_nfile;                      /* cuantas son de mesh.bin (despues se anaden las de la 1.4) */

/* posiciones y tamanos de las mallas de un archivo SSM3 abierto; devuelve cuantas hay (o -1) */
static int ssm3_index(FILE *f, uint32_t **off, uint32_t **len)
{
    uint8_t h[8];
    fseek(f, 0, SEEK_END);
    uint32_t fsize = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "SSM3", 4)) return -1;
    int n = rd32(h + 4);
    *off = malloc(4 * (n ? n : 1));
    *len = malloc(4 * (n ? n : 1));
    if (!*off || !*len || (int)fread(*off, 4, n, f) != n) return -1;
    for (int i = 0; i < n; i++) {
        uint32_t end = fsize;
        for (int k = 0; k < n; k++)
            if ((*off)[k] > (*off)[i] && (*off)[k] < end) end = (*off)[k];
        (*len)[i] = end - (*off)[i];
    }
    return n;
}

/* lee una malla (off, len) de f en un bloque nuevo con la misma alineacion que tenia en el archivo */
static void *mesh_read(FILE *f, uint32_t off, uint32_t len, Mesh *m)
{
    uint8_t *blk = memalign(64, len + (off & 63) + 16);
    if (!blk) return NULL;
    if (fseek(f, off, SEEK_SET) || fread(blk + (off & 63), 1, len, f) != len) { free(blk); return NULL; }
    memset(m, 0, sizeof(*m));
    parse_mesh(blk + (off & 63), m);
    return blk;
}

static void mesh_free(Mesh *m)
{
    free(m->parts); free(m->batches); free(m->bind);
    memset(m, 0, sizeof(*m));
}

static int load_meshes(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/mesh.bin", basedir);
    unsigned int t0 = sceKernelGetSystemTimeLow();
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(errbuf, sizeof(errbuf), "No se encuentra %s", path); return -1; }
    int n = ssm3_index(f, &mesh_foff, &mesh_flen);
    if (n < 0) { fclose(f); snprintf(errbuf, sizeof(errbuf), "mesh.bin antiguo: vuelve a ejecutar extract.py"); return -1; }
    gd.meshes = calloc(n ? n : 1, sizeof(Mesh));
    mesh_blk = calloc(n ? n : 1, sizeof(void *));
    gd.nmeshes = mesh_nfile = n;
    int kb = 0;
    for (int i = 0; i < n; i++) {
        mesh_blk[i] = mesh_read(f, mesh_foff[i], mesh_flen[i], &gd.meshes[i]);
        if (!mesh_blk[i]) {
            fclose(f);
            snprintf(errbuf, sizeof(errbuf), "Sin memoria para mesh.bin (malla %d de %d)", i, n);
            return -1;
        }
        kb += mesh_flen[i];
    }
    fclose(f);
    dlog("CARGA %-10s %6d KB en %4.0f ms (%d mallas)", "mesh.bin", kb / 1024, (sceKernelGetSystemTimeLow() - t0) / 1000.0f, n);
    return 0;
}

static FILE *texfile, *hwtexfile, *xmtexfile;
static char texpath[256], hwtexpath[256], xmtexpath[256];
extern volatile int io_resume_gen;
static int tex_gen;
/* vuelve a abrir los archivos de texturas (tras el modo reposo los cierra la consola) */
static void tex_files_reopen(void)
{
    if (texfile) { fclose(texfile); texfile = texpath[0] ? fopen(texpath, "rb") : NULL; }
    if (hwtexfile) { fclose(hwtexfile); hwtexfile = hwtexpath[0] ? fopen(hwtexpath, "rb") : NULL; }
    if (xmtexfile) { fclose(xmtexfile); xmtexfile = xmtexpath[0] ? fopen(xmtexpath, "rb") : NULL; }
    tex_gen = io_resume_gen;
    dlog("REPOSO: archivos de texturas abiertos de nuevo");
}

/* directorio de un archivo SST1: llama a cb con la cabecera de cada textura */
static int read_tex_dir(FILE *f, void (*cb)(int i, const char *name, int w, int h, int fmt, int alpha,
                                            uint32_t pal, uint32_t data, uint32_t off))
{
    uint8_t h[8];
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "SST1", 4)) return -1;
    int n = rd32(h + 4);
    uint32_t *offs = malloc(4 * (n ? n : 1));
    fread(offs, 4, n, f);
    for (int i = 0; i < n; i++) {
        uint8_t th[48];
        fseek(f, offs[i], SEEK_SET);
        fread(th, 1, 48, f);
        char name[32];
        int l = th[0]; if (l > 31) l = 31;
        memcpy(name, th + 1, l); name[l] = 0;
        uint16_t w, hh, fmt, al;
        memcpy(&w, th + 32, 2); memcpy(&hh, th + 34, 2); memcpy(&fmt, th + 36, 2); memcpy(&al, th + 38, 2);
        cb(i, name, w, hh, fmt, al, rd32(th + 40), rd32(th + 44), offs[i] + 48);
    }
    free(offs);
    return n;
}

static void tex_dir_cb(int i, const char *name, int w, int h, int fmt, int alpha, uint32_t pal, uint32_t data, uint32_t off)
{
    Texture *t = &gd.tex[i];
    snprintf(t->name, sizeof(t->name), "%s", name);
    t->w = w; t->h = h; t->fmt = fmt; t->alpha = alpha;
    t->palbytes = pal; t->databytes = data;
    t->file_off = off;
}

static int load_textures(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/tex.bin", basedir);
    texfile = fopen(path, "rb");
    snprintf(texpath, sizeof(texpath), "%s", path);
    if (!texfile) { snprintf(errbuf, sizeof(errbuf), "No se encuentra %s", path); return -1; }
    uint8_t h[8];
    fread(h, 1, 8, texfile);
    gd.ntex = rd32(h + 4);
    gd.tex = calloc(gd.ntex, sizeof(Texture));
    fseek(texfile, 0, SEEK_SET);
    if (read_tex_dir(texfile, tex_dir_cb) < 0) { snprintf(errbuf, sizeof(errbuf), "tex.bin no valido"); return -1; }
    return 0;
}

/* lee los datos de la textura i de su archivo (el normal o el del tema) */
/* archivo del que se lee la textura i (para el hilo de carga, que abre el suyo propio) */
void data_tex_path(int i, char *out, int n)
{
    /* ruta completa: sceIoOpen no resuelve las relativas como fopen (la carpeta actual la lleva la libreria de C) */
    static char cwd[160];
    if (!cwd[0] && !getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
    if (gd.tex[i].src) snprintf(out, n, "%s/data/hw_tex.bin", cwd);
    else snprintf(out, n, "%s/%s/tex.bin", cwd, basedir);
}

int data_tex_read(int i, void *pal, void *data)
{
    Texture *t = &gd.tex[i];
    if (tex_gen != io_resume_gen) tex_files_reopen();
    for (int tries = 0; tries < 2; tries++) {
        FILE *f = t->src == 2 ? xmtexfile : t->src ? hwtexfile : texfile;
        if (!f) return -1;
        if (fseek(f, t->file_off, SEEK_SET) == 0 && (!t->palbytes || fread(pal, 1, t->palbytes, f) == t->palbytes) &&
            fread(data, 1, t->databytes, f) == t->databytes)
            return 0;
        tex_files_reopen();   /* un fallo de lectura: se reabre y se prueba otra vez */
    }
    return -1;
}

int data_tex_load(int i)
{
    if (i < 0 || i >= gd.ntex) return -1;
    Texture *t = &gd.tex[i];
    if (t->loaded) return 0;
    if (t->palbytes) {
        t->pal = memalign(64, t->palbytes);
        if (!t->pal) return -1;
    }
    t->data = memalign(64, t->databytes);
    if (!t->data) return -1;
    if (data_tex_read(i, t->pal, t->data) < 0) dlog_error("Error leyendo la textura %s", t->name);
    t->loaded = 1;
    return 0;
}

/* ---- tema de Halloween (hw_tex.bin y hw_mesh.bin, de la 1.4) ----
 * Las texturas del tema tienen el mismo tamano y formato que las normales: al cambiar de tema solo cambia de que
 * archivo se leen, y si ya estaban cargadas se leen otra vez en el mismo sitio (no se tienen nunca las dos en
 * memoria). Las mallas (inspector y perro zombis) son pequenas: se cargan las dos y se intercambian. */
typedef struct { uint32_t off, pal, data; uint16_t fmt, alpha; } TexSrc;
static TexSrc *tex_src[3];          /* [0] normal, [1] Halloween, [2] Navidad (data == 0: no hay) */
static Mesh *mesh_alt;              /* la otra version de cada malla con tema (Halloween) */
static int *mesh_alt_idx, mesh_nalt;
static int theme_cur, tex_nbase;

static void hw_dir_cb(int i, const char *name, int w, int h, int fmt, int alpha, uint32_t pal, uint32_t data, uint32_t off)
{
    (void)i;
    for (int t = 0; t < gd.ntex; t++) {
        Texture *x = &gd.tex[t];
        if (strcmp(x->name, name)) continue;
        if (x->w != w || x->h != h || x->palbytes != pal || x->databytes != data) {
            dlog("AVISO la textura de Halloween %s no es como la normal: no se usa", name);
            return;
        }
        tex_src[1][t] = (TexSrc){off, pal, data, (uint16_t)fmt, (uint16_t)alpha};
        return;
    }
    /* textura nueva de la 1.4 (Zombie Jake): se anade al final y se lee siempre de hw_tex.bin */
    gd.tex = realloc(gd.tex, sizeof(Texture) * (gd.ntex + 1));
    Texture *x = &gd.tex[gd.ntex++];
    memset(x, 0, sizeof(*x));
    snprintf(x->name, sizeof(x->name), "%s", name);
    x->w = w; x->h = h; x->fmt = fmt; x->alpha = alpha; x->palbytes = pal; x->databytes = data;
    x->file_off = off; x->src = 1;
}

/* Navidad (xm_tex.bin, en la carpeta del mundo de la 1.5.2): solo recambios de texturas que ya existen */
static void xm_dir_cb(int i, const char *name, int w, int h, int fmt, int alpha, uint32_t pal, uint32_t data, uint32_t off)
{
    (void)i;
    for (int t = 0; t < tex_nbase; t++) {
        Texture *x = &gd.tex[t];
        if (strcmp(x->name, name)) continue;
        if (x->w != w || x->h != h || x->palbytes != pal || x->databytes != data) {
            dlog("AVISO la textura de Navidad %s no es como la normal: no se usa", name);
            return;
        }
        tex_src[2][t] = (TexSrc){off, pal, data, (uint16_t)fmt, (uint16_t)alpha};
        return;
    }
}

/* 1: texturas de hw_tex.bin (y xm_tex.bin). Despues de leer tex.bin y antes de r_init (puede anadir texturas) */
int data_hw_ok, data_xm_ok;
int data_theme_init_tex(void)
{
    char path[256];
    int n0 = gd.ntex;
    for (int k = 0; k < 3; k++) tex_src[k] = calloc(n0 ? n0 : 1, sizeof(TexSrc));
    for (int t = 0; t < n0; t++) {
        Texture *x = &gd.tex[t];
        tex_src[0][t] = (TexSrc){x->file_off, x->palbytes, x->databytes, (uint16_t)x->fmt, (uint16_t)x->alpha};
    }
    tex_nbase = n0;
    snprintf(path, sizeof(path), "%s/xm_tex.bin", basedir);
    xmtexfile = fopen(path, "rb");
    snprintf(xmtexpath, sizeof(xmtexpath), "%s", path);
    if (xmtexfile) {
        int nx = read_tex_dir(xmtexfile, xm_dir_cb), k = 0;
        for (int t = 0; t < n0; t++) k += tex_src[2][t].data != 0;
        data_xm_ok = nx > 0 && k > 0;
        dlog("Tema de Navidad disponible: %d texturas", k);
    }
    /* en data/ aunque el mundo sea otro (data/w15): texturas de los personajes de la 1.4 y de las tablas */
    snprintf(path, sizeof(path), "data/hw_tex.bin");
    hwtexfile = fopen(path, "rb");
    snprintf(hwtexpath, sizeof(hwtexpath), "%s", path);
    if (!hwtexfile) return 0;
    int nt = read_tex_dir(hwtexfile, hw_dir_cb);
    data_hw_ok = nt > 0;
    return nt > 0;
}

/* mallas de Navidad (xm_mesh.bin): la "xmas_X" sustituye a X (o a normal_X, el escenario de la 1.5.2) mientras dura
 * el tema. Aqui solo se mira cuales hay; se leen al activar el tema, en lugar de la normal */
static int *xm_mesh_idx, xm_nmesh;
static uint32_t *xm_foff, *xm_flen, *xm_src;
int data_xmas_meshes_init(void)
{
    if (xm_mesh_idx) return xm_nmesh;
    char path[256];
    snprintf(path, sizeof(path), "%s/xm_mesh.bin", basedir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = ssm3_index(f, &xm_foff, &xm_flen);
    xm_mesh_idx = calloc(n > 0 ? n : 1, sizeof(int));
    xm_src = calloc(n > 0 ? n : 1, sizeof(uint32_t));
    for (int i = 0; i < n; i++) {
        uint8_t h[32];
        char nm[40];
        if (fseek(f, xm_foff[i], SEEK_SET) || fread(h, 1, 32, f) != 32) break;
        int l = h[0] > 31 ? 31 : h[0];
        memcpy(nm, h + 1, l); nm[l] = 0;
        /* con el nombre de la malla a la que sustituye (datos antiguos: "xmas_X", de X o normal_X) */
        const char *base = strncmp(nm, "xmas_", 5) ? nm : nm + 5;
        char alt[48];
        snprintf(alt, sizeof(alt), "normal_%s", base);
        alt[31] = 0;
        for (int m = 0; m < mesh_nfile; m++)
            if (!strcmp(gd.meshes[m].name, base) || !strcmp(gd.meshes[m].name, alt)) {
                xm_mesh_idx[xm_nmesh] = m;
                xm_src[xm_nmesh++] = i;
                break;
            }
    }
    fclose(f);
    dlog("Tema de Navidad: %d mallas", xm_nmesh);
    return xm_nmesh;
}

/* pone las mallas de Navidad (on) o vuelve a las normales: cada una se lee de su archivo encima de la otra */
static void xmas_meshes_apply(int on)
{
    if (!xm_nmesh) return;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", basedir, on ? "xm_mesh.bin" : "mesh.bin");
    FILE *f = fopen(path, "rb");
    FILE *fn = on ? NULL : f;      /* para volver a la normal si la de Navidad no vale */
    unsigned int t0 = sceKernelGetSystemTimeLow();
    int kb = 0;
    for (int i = 0; i < xm_nmesh; i++) {
        int m = xm_mesh_idx[i];
        Mesh *me = &gd.meshes[m];
        char name[32];
        int nbind = me->nbind;
        memcpy(name, me->name, sizeof(name));
        mesh_free(me);
        free(mesh_blk[m]);
        mesh_blk[m] = NULL;
        if (f) {
            uint32_t off = on ? xm_foff[xm_src[i]] : mesh_foff[m], len = on ? xm_flen[xm_src[i]] : mesh_flen[m];
            mesh_blk[m] = mesh_read(f, off, len, me);
            kb += len;
            if (mesh_blk[m] && on && me->nbind != nbind) {
                dlog("AVISO la malla de Navidad de %s no es como la normal: no se usa", name);
                mesh_free(me); free(mesh_blk[m]); mesh_blk[m] = NULL;
            }
        }
        if (!mesh_blk[m]) {
            /* sin la de Navidad (o sin memoria): la normal otra vez */
            if (!fn) { snprintf(path, sizeof(path), "%s/mesh.bin", basedir); fn = fopen(path, "rb"); }
            if (fn) mesh_blk[m] = mesh_read(fn, mesh_foff[m], mesh_flen[m], me);
            if (!mesh_blk[m]) dlog_error("Sin memoria para la malla %s", name);
        }
        memcpy(me->name, name, sizeof(name));
    }
    if (fn && fn != f) fclose(fn);
    if (f) fclose(f);
    dlog("Mallas %s: %d KB en %.0f ms", on ? "de Navidad" : "normales", kb / 1024, (sceKernelGetSystemTimeLow() - t0) / 1000.0f);
}

/* 2: mallas de hw_mesh.bin, con todo lo demas ya cargado. Las que tienen nombre de una malla de la 1.0 son su
 * version de Halloween; las demas (Zombie Jake) se anaden al final */
static void chars14_init(void);

int data_theme_init(int need_meshes)
{
    if (!hwtexfile) return 0;
    chars14_init();
    /* fuera de Halloween y sin Zombie Jake comprado no hacen falta (son ~350 KB de RAM) */
    uint8_t *b = need_meshes ? load_file("hw_mesh.bin", NULL) : NULL;
    int nextra = 0;
    if (b && !memcmp(b, "SSM3", 4)) {
        int n;
        Mesh *hm = parse_meshes(b, &n);
        mesh_alt = calloc(n ? n : 1, sizeof(Mesh));
        mesh_alt_idx = calloc(n ? n : 1, sizeof(int));
        for (int i = 0; i < n; i++) {
            const char *nm = hm[i].name;
            int halloween = !strncmp(nm, "halloween_", 10);
            if (halloween) nm += 10;
            int found = -1;
            for (int m = 0; m < gd.nmeshes && halloween; m++)
                if (!strcmp(gd.meshes[m].name, nm) && gd.meshes[m].nbind == hm[i].nbind) { found = m; break; }
            if (found >= 0) {
                mesh_alt[mesh_nalt] = hm[i];
                mesh_alt_idx[mesh_nalt++] = found;
            } else if (!halloween) {
                gd.meshes = realloc(gd.meshes, sizeof(Mesh) * (gd.nmeshes + 1));
                gd.meshes[gd.nmeshes++] = hm[i];
                nextra++;
            }
        }
    }
    int nt = 0;
    for (int t = 0; t < tex_nbase; t++) nt += tex_src[1][t].data != 0;
    dlog("Tema de Halloween disponible: %d texturas, %d mallas; contenido nuevo: %d texturas, %d mallas",
         nt, mesh_nalt, gd.ntex - tex_nbase, nextra);
    return nt > 0;
}

/* ---- personajes de la 1.4 (chars14.bin): cada malla se lee solo mientras se usa ----
 * La entrada en gd.meshes existe siempre (con el nombre) pero vacia (nparts = nbatches = 0: no se dibuja nada)
 * hasta data_mesh_acquire; data_mesh_release la vacia otra vez y devuelve la memoria (~140 KB cada una). */
#define MAX_LAZY 48   /* mallas de chars14.bin (personajes y tablas: 35 con la 1.11) */
static FILE *charsfile;
static char charspath[64] = "data/chars14.bin";
static int chars_gen;
static struct { int mesh; uint32_t off, len; uint8_t *buf; } lazy[MAX_LAZY];
static int nlazy;

static void chars14_init(void)
{
    char path[256];
    snprintf(path, sizeof(path), "data/chars14.bin");   /* compartido por todos los mundos */
    charsfile = fopen(path, "rb");
    if (!charsfile) return;
    uint8_t h[8];
    if (fread(h, 1, 8, charsfile) != 8 || memcmp(h, "SSM3", 4)) { fclose(charsfile); charsfile = NULL; return; }
    int n = rd32(h + 4);
    if (n > MAX_LAZY) n = MAX_LAZY;
    uint32_t offs[MAX_LAZY + 1];
    fread(offs, 4, n, charsfile);
    fseek(charsfile, 0, SEEK_END);
    offs[n] = ftell(charsfile);
    for (int i = 0; i < n; i++) {
        uint8_t nb[32];
        fseek(charsfile, offs[i], SEEK_SET);
        fread(nb, 1, 32, charsfile);
        gd.meshes = realloc(gd.meshes, sizeof(Mesh) * (gd.nmeshes + 1));
        Mesh *m = &gd.meshes[gd.nmeshes];
        memset(m, 0, sizeof(*m));
        int l = nb[0]; if (l > 31) l = 31;
        memcpy(m->name, nb + 1, l); m->name[l] = 0;
        m->skinned = 1;
        lazy[nlazy].mesh = gd.nmeshes++;
        lazy[nlazy].off = offs[i];
        lazy[nlazy].len = offs[i + 1] - offs[i];
        lazy[nlazy].buf = NULL;
        nlazy++;
    }
    dlog("Personajes de la 1.4: %d (se cargan al verlos)", nlazy);
}

int data_mesh_acquire(int mesh)
{
    for (int i = 0; i < nlazy; i++) {
        if (lazy[i].mesh != mesh) continue;
        if (lazy[i].buf) return 0;
        uint8_t *b = memalign(64, lazy[i].len + 16);
        if (!b) { dlog_error("Sin memoria para la malla %s (%d KB)", gd.meshes[mesh].name, (int)(lazy[i].len / 1024)); return -1; }
        if (chars_gen != io_resume_gen && charsfile) { fclose(charsfile); charsfile = fopen(charspath, "rb"); chars_gen = io_resume_gen; }
        int ok = 0;
        for (int tries = 0; tries < 2 && !ok; tries++) {
            ok = charsfile && fseek(charsfile, lazy[i].off, SEEK_SET) == 0 && fread(b, 1, lazy[i].len, charsfile) == lazy[i].len;
            if (!ok && tries == 0) { if (charsfile) fclose(charsfile); charsfile = fopen(charspath, "rb"); }
        }
        if (!ok) { free(b); return -1; }
        lazy[i].buf = b;
        parse_mesh(b, &gd.meshes[mesh]);
        return 0;
    }
    return 0;   /* malla normal: siempre cargada */
}

void data_mesh_release(int mesh)
{
    for (int i = 0; i < nlazy; i++) {
        if (lazy[i].mesh != mesh || !lazy[i].buf) continue;
        Mesh *m = &gd.meshes[mesh];
        free(m->parts); free(m->batches); free(m->bind);
        char name[32];
        memcpy(name, m->name, sizeof(name));
        memset(m, 0, sizeof(*m));
        memcpy(m->name, name, sizeof(name));
        m->skinned = 1;
        free(lazy[i].buf);
        lazy[i].buf = NULL;
    }
}

int data_mesh_find(const char *name)
{
    for (int m = 0; m < gd.nmeshes; m++) if (!strcmp(gd.meshes[m].name, name)) return m;
    return -1;
}

int data_tex_find(const char *name)
{
    for (int t = 0; t < gd.ntex; t++) if (!strcmp(gd.tex[t].name, name)) return t;
    return -1;
}

/* material nuevo copiando otro con otra textura (Zombie Jake: el de Jake con avatar_zombieJake) */
int data_mat_clone(const char *base, int tex)
{
    for (int i = 0; i < gd.nmats; i++) {
        if (strcmp(gd.mats[i].name, base)) continue;
        gd.mats = realloc(gd.mats, sizeof(MatFile) * (gd.nmats + 1));
        gd.mats[gd.nmats] = gd.mats[i];
        gd.mats[gd.nmats].tex = tex;
        return gd.nmats++;
    }
    return -1;
}

int data_theme_has_tex(int t)
{
    return t < tex_nbase && ((tex_src[1] && tex_src[1][t].data) || (tex_src[2] && tex_src[2][t].data));
}

static void swap_alt(Mesh *alt, const int *idx, int n)
{
    for (int i = 0; i < n; i++) {
        Mesh tmp = gd.meshes[idx[i]];
        gd.meshes[idx[i]] = alt[i];
        alt[i] = tmp;
    }
}

/* tema: 0 normal, 1 Halloween, 2 Navidad. Cada textura con recambio se lee del archivo de su tema (o de tex.bin) */
void data_theme_set(int theme)
{
    if (theme < 0 || theme > 2 || !tex_src[0]) theme = 0;
    if (theme == theme_cur) return;
    if (theme_cur == 1) swap_alt(mesh_alt, mesh_alt_idx, mesh_nalt);
    if (theme_cur == 2) xmas_meshes_apply(0);
    theme_cur = theme;
    for (int t = 0; t < tex_nbase; t++) {
        if (!data_theme_has_tex(t)) continue;
        int k = theme && tex_src[theme][t].data ? theme : 0;
        const TexSrc *s = &tex_src[k][t];
        Texture *x = &gd.tex[t];
        x->src = k; x->file_off = s->off; x->fmt = s->fmt; x->alpha = s->alpha;
    }
    if (theme == 1) swap_alt(mesh_alt, mesh_alt_idx, mesh_nalt);
    if (theme == 2) xmas_meshes_apply(1);
    /* ThemeAssets de la 1.5.2: en Navidad el material del cielo usa el atlas environment (su malla xmas_skyPlane lee
     * de ahi el degradado morado, del color de la niebla: tapa el final del mundo como en la 1.0) */
    static int sky_mat = -2, sky_tex0;
    if (sky_mat == -2) {
        sky_mat = -1;
        for (int m = 0; m < gd.nmats; m++) if (gd.mats[m].sky) { sky_mat = m; sky_tex0 = gd.mats[m].tex; break; }
    }
    if (sky_mat >= 0) {
        int env = theme == 2 && xm_nmesh ? data_tex_find("environment") : -1;
        int has_sky = 0;
        for (int i = 0; i < xm_nmesh; i++) has_sky |= !strcmp(gd.meshes[xm_mesh_idx[i]].name, "skyPlane");
        gd.mats[sky_mat].tex = env >= 0 && has_sky ? env : sky_tex0;
    }
}

/* un clip en formato SSA1 (los datos se usan desde el buffer: no se copian) */
static void parse_clip(uint8_t *p, Clip *c)
{
    {
        int l = p[0]; if (l > 47) l = 47;
        memcpy(c->name, p + 1, l); c->name[l] = 0;
        p += 48;
        memcpy(&c->length, p, 4);
        c->wrap = rd32(p + 4); c->nframes = rd32(p + 8); c->ntracks = rd32(p + 12); c->nevents = rd32(p + 16);
        memcpy(&c->sample_rate, p + 20, 4);
        p += 24;
        c->events = calloc(c->nevents ? c->nevents : 1, sizeof(AnimEvent));
        for (int e = 0; e < c->nevents; e++) {
            memcpy(&c->events[e].time, p, 4);
            int fl = p[4]; if (fl > 31) fl = 31;
            memcpy(c->events[e].fn, p + 5, fl); c->events[e].fn[fl] = 0;
            p += 36;
        }
        c->tracks = calloc(c->ntracks ? c->ntracks : 1, sizeof(AnimTrack));
        for (int t = 0; t < c->ntracks; t++) {
            AnimTrack *tr = &c->tracks[t];
            memcpy(&tr->path, p, 2); memcpy(&tr->flags, p + 2, 2);
            p += 4;
            tr->stride = ((tr->flags & 1) ? 3 : 0) + ((tr->flags & 2) ? 4 : 0) + ((tr->flags & 4) ? 3 : 0);
            tr->data = (float *)p;
            p += 4 * tr->stride * c->nframes;
        }
    }
}

static int load_anims(void)
{
    uint8_t *b = load_file("anim.bin", NULL);
    if (!b) return -1;
    gd.nclips = rd32(b + 4);
    gd.clips = calloc(gd.nclips, sizeof(Clip));
    for (int i = 0; i < gd.nclips; i++) parse_clip(b + rd32(b + 8 + 4 * i), &gd.clips[i]);
    return 0;
}

/* ---- tablas de la 1.5.2: boards.bin (la lista) y boardanim.bin (sus animaciones, solo las de la elegida) ---- */
BoardDef *gd_boards;
int gd_nboards;

int data_boards_load(void)
{
    uint8_t *b = load_file_opt("data/boards.bin");
    if (!b || memcmp(b, "SSB1", 4)) return 0;
    int n = rd32(b + 4);
    gd_boards = calloc(n ? n : 1, sizeof(BoardDef));
    uint8_t *p = b + 8;
    for (int i = 0; i < n; i++) {
        BoardDef *d = &gd_boards[i];
        memcpy(d->name, p, 32); d->name[31] = 0;
        memcpy(d->mesh, p + 32, 32); d->mesh[31] = 0;
        d->price = (int)rd32(p + 64); d->unlock = rd32(p + 68); d->effect = rd32(p + 72);
        p += 76;
        for (int a = 0; a < BOARD_NACT; a++) {
            d->nact[a] = rd32(p); p += 4;
            d->act[a] = (const char (*)[32])p;   /* nombres de 32 bytes, terminados en 0 */
            p += 32 * d->nact[a];
        }
    }
    gd_nboards = n;
    return n;
}

static int clips_base = -1;        /* gd.nclips antes de las animaciones de la tabla */
static uint8_t *board_clip_buf[BOARD_MAX_CLIPS];
static int board_nbuf;

static int bc_gen;   /* definido abajo con el hilo; aqui solo se sube */
void data_board_clips_free(void)
{
    bc_gen++;   /* lo que este leyendo el hilo se tira al terminar */
    if (clips_base < 0) return;
    for (int i = 0; i < board_nbuf; i++) free(board_clip_buf[i]);
    for (int c = clips_base; c < gd.nclips; c++) { free(gd.clips[c].tracks); free(gd.clips[c].events); }
    board_nbuf = 0;
    gd.nclips = clips_base;
}

/* indice de boardanim.bin (nombre y posicion de cada clip): se lee una vez. Antes se buscaba cada vez con una
 * lectura por clip y en la consola eran ~220 ms cada vez que se movia el carrusel de tablas */
static int bclip_n = -1;
static uint32_t *bclip_off;
static char (*bclip_name)[48];
/* SSA2: las rutas de los huesos van en una tabla propia del archivo (el mismo boardanim.bin sirve para el mundo
 * clasico y el de la 1.5.2, y cada world.bin tiene su tabla de cadenas). bstr_map: indice propio -> gd.strings */
static uint16_t *bstr_map;
static int bstr_n;

/* indice en gd.strings de esa cadena; si no esta se anade al final */
static int gd_string_id(const char *s)
{
    for (int i = 0; i < gd.nstrings; i++) if (!strcmp(gd.strings[i], s)) return i;
    if (gd.nstrings >= 65535) return 0;
    char **ns = realloc(gd.strings, sizeof(char *) * (gd.nstrings + 1));
    char *c = malloc(strlen(s) + 1);
    if (!ns || !c) { if (ns) gd.strings = ns; free(c); return 0; }
    gd.strings = ns;
    strcpy(c, s);
    gd.strings[gd.nstrings] = c;
    return gd.nstrings++;
}

static void board_clip_index(FILE *f)
{
    bclip_n = 0;
    uint8_t h[12];
    if (fread(h, 1, 8, f) != 8) return;
    int v2 = !memcmp(h, "SSA2", 4);
    if (!v2 && memcmp(h, "SSA1", 4)) return;
    int nc = rd32(h + 4);
    uint32_t stroff = 0;
    if (v2) {
        if (fread(h + 8, 1, 4, f) != 4) return;
        stroff = rd32(h + 8);
    }
    bclip_off = malloc(4 * (nc + 1));
    bclip_name = malloc(48 * (nc ? nc : 1));
    if (!bclip_off || !bclip_name || (int)fread(bclip_off, 4, nc, f) != nc) return;
    fseek(f, 0, SEEK_END);
    bclip_off[nc] = v2 ? stroff : (uint32_t)ftell(f);
    for (int k = 0; k < nc; k++) {
        char nm[48];
        fseek(f, bclip_off[k], SEEK_SET);
        if (fread(nm, 1, 48, f) != 48) return;
        int l = (unsigned char)nm[0]; if (l > 47) l = 47;
        memcpy(bclip_name[k], nm + 1, l); bclip_name[k][l] = 0;
    }
    if (v2) {
        uint8_t c4[4];
        fseek(f, stroff, SEEK_SET);
        if (fread(c4, 1, 4, f) != 4) return;
        int ns = rd32(c4);
        bstr_map = malloc(2 * (ns ? ns : 1));
        if (!bstr_map) return;
        for (int i = 0; i < ns; i++) {
            char s[256];
            int l = fgetc(f);
            if (l < 0 || (int)fread(s, 1, l, f) != l) return;
            s[l] = 0;
            bstr_map[i] = (uint16_t)gd_string_id(s);
        }
        bstr_n = ns;
    }
    bclip_n = nc;
}

/* lee de boardanim.bin los clips con esos nombres que no esten ya cargados; devuelve cuantos */
int data_board_clips_load_max(const char *const *names, int n, int maxadd);
int data_board_clips_load(const char *const *names, int n) { return data_board_clips_load_max(names, n, 1 << 30); }
/* como data_board_clips_load pero como mucho maxadd clips nuevos (para repartir la lectura en varios fotogramas) */
int data_board_clips_load_max(const char *const *names, int n, int maxadd)
{
    if (clips_base < 0) clips_base = gd.nclips;
    FILE *f = fopen("data/boardanim.bin", "rb");
    if (!f) return 0;
    if (bclip_n < 0) board_clip_index(f);
    int added = 0;
    {
        int nc = bclip_n;
        uint32_t *offs = bclip_off;
        if (nc > 0) {
            for (int k = 0; k < nc && board_nbuf < BOARD_MAX_CLIPS && added < maxadd; k++) {
                const char *nm = bclip_name[k];
                int want = 0;
                for (int i = 0; i < n && !want; i++) want = names[i] && !strcmp(names[i], nm);
                for (int c = 0; c < gd.nclips && want; c++) if (!strcmp(gd.clips[c].name, nm)) want = 0;
                if (!want) continue;
                uint32_t len = offs[k + 1] - offs[k];
                uint8_t *buf = memalign(16, len);
                if (!buf) break;
                fseek(f, offs[k], SEEK_SET);
                if (fread(buf, 1, len, f) != len) { free(buf); break; }
                Clip *nc2 = realloc(gd.clips, sizeof(Clip) * (gd.nclips + 1));
                if (!nc2) { free(buf); break; }
                gd.clips = nc2;
                memset(&gd.clips[gd.nclips], 0, sizeof(Clip));
                Clip *cl = &gd.clips[gd.nclips++];
                parse_clip(buf, cl);
                /* rutas de los huesos: de la tabla del archivo a la de este mundo (las que no esten, a ninguna) */
                for (int t = 0; t < cl->ntracks && bstr_map; t++)
                    cl->tracks[t].path = cl->tracks[t].path < bstr_n ? bstr_map[cl->tracks[t].path] : 0;
                board_clip_buf[board_nbuf++] = buf;
                added++;
            }
        }
    }
    fclose(f);
    return added;
}

/* ---- animaciones de la tabla en segundo plano ----
 * Al volver de la pantalla de tablas al menu, el resto de animaciones de la tabla elegida se leian de dos en dos en el
 * hilo principal: cada fotograma abria boardanim.bin y leia 2 clips de la Memory Stick (~90 ms de CPU: 5-18 tirones
 * seguidos de 83-100 ms en el menu, en el devlog). Ahora un hilo de menos prioridad solo LEE los bytes de cada clip
 * (por trozos de 32 KB, cediendo, para no dejar sin datos a la musica); el hilo principal solo lo prepara (parse_clip
 * y las rutas de los huesos), que es casi nada. El hilo no toca gd.clips: solo su bufer */
static SceUID bc_thid = -1, bc_sema = -1;
static volatile int bc_state;          /* 0 libre, 1 leyendo, 2 leido, 3 error */
static volatile int bc_k = -1;         /* clip (indice de boardanim.bin) que se esta leyendo */
static uint8_t *volatile bc_buf;
static int bc_req_gen;                 /* data_board_clips_free sube bc_gen: lo que se leia antes se tira */
static char bc_path[200];              /* ruta completa: sceIoOpen no resuelve las relativas como fopen */
static int bc_fails;                   /* lecturas fallidas seguidas: a la tercera se lee de golpe, como antes */

static int bclip_reader(SceSize args, void *argp)
{
    (void)args; (void)argp;
    SceUID fd = -1;
    extern volatile int io_resume_gen;
    int gen = io_resume_gen;
    for (;;) {
        if (sceKernelWaitSema(bc_sema, 1, NULL) < 0) break;
        int k = bc_k;
        if (k < 0 || k >= bclip_n) { bc_state = 3; continue; }
        uint32_t off = bclip_off[k], len = bclip_off[k + 1] - bclip_off[k];
        int ok = 0;
        for (int tries = 0; tries < 2 && !ok; tries++) {
            /* tras el modo reposo (o un fallo) se vuelve a abrir */
            if (fd < 0 || gen != io_resume_gen || tries) {
                if (fd >= 0) sceIoClose(fd);
                fd = sceIoOpen(bc_path, PSP_O_RDONLY, 0);
                gen = io_resume_gen;
            }
            ok = fd >= 0 && sceIoLseek32(fd, off, PSP_SEEK_SET) == (int)off;
            for (uint32_t done = 0; ok && done < len;) {
                uint32_t n = len - done > 32768 ? 32768 : len - done;
                ok = sceIoRead(fd, bc_buf + done, n) == (int)n;
                done += n;
                sceKernelDelayThread(1000);
            }
        }
        bc_state = ok ? 2 : 3;
    }
    return 0;
}

/* mete en gd.clips el clip ya leido (si sigue haciendo falta) */
static int bclip_finish(void)
{
    int added = 0, k = bc_k;
    uint8_t *buf = bc_buf;
    if (bc_state == 2 && bc_req_gen == bc_gen && board_nbuf < BOARD_MAX_CLIPS) {
        int dup = 0;   /* puede que la carrera ya lo haya leido de golpe (data_board_clips_load) */
        for (int c = 0; c < gd.nclips && !dup; c++) dup = !strcmp(gd.clips[c].name, bclip_name[k]);
        Clip *nc2 = dup ? NULL : realloc(gd.clips, sizeof(Clip) * (gd.nclips + 1));
        if (nc2) {
            gd.clips = nc2;
            memset(&gd.clips[gd.nclips], 0, sizeof(Clip));
            Clip *cl = &gd.clips[gd.nclips++];
            parse_clip(buf, cl);
            for (int t = 0; t < cl->ntracks && bstr_map; t++)
                cl->tracks[t].path = cl->tracks[t].path < bstr_n ? bstr_map[cl->tracks[t].path] : 0;
            board_clip_buf[board_nbuf++] = buf;
            buf = NULL;
            added = 1;
        }
    }
    free(buf);   /* tirado: ya no hace falta (otra tabla, o ya estaba) */
    bc_buf = NULL;
    bc_k = -1;
    bc_state = 0;
    return added;
}

/* un paso por fotograma: termina el clip leido y pide el siguiente que falte. Devuelve 1 cuando ya estan todos.
 * *added: cuantos clips nuevos han entrado en gd.clips en este paso (para anim_add_state) */
int data_board_clips_async(const char *const *names, int n, int *added)
{
    *added = 0;
    if (clips_base < 0) clips_base = gd.nclips;
    if (bclip_n < 0) {   /* el indice: normalmente ya esta (lo leyo la vista previa) */
        FILE *f = fopen("data/boardanim.bin", "rb");
        if (!f) return 1;
        board_clip_index(f);
        fclose(f);
    }
    if (bclip_n <= 0) return 1;
    if (bc_sema < 0) {
        char cwd[160];
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
        snprintf(bc_path, sizeof(bc_path), "%s/data/boardanim.bin", cwd);
        bc_sema = sceKernelCreateSema("bclip_loader", 0, 0, 1, NULL);
        bc_thid = sceKernelCreateThread("bclip_loader", bclip_reader, 0x30, 0x2000, PSP_THREAD_ATTR_USER, NULL);
        if (bc_sema < 0 || bc_thid < 0) {   /* sin hilo: como antes, de golpe */
            int c0 = gd.nclips;
            data_board_clips_load(names, n);
            *added = gd.nclips - c0;
            return 1;
        }
        sceKernelStartThread(bc_thid, 0, NULL);
    }
    if (bc_state == 1) return 0;                    /* el hilo sigue leyendo */
    if (bc_state == 3 && ++bc_fails >= 3) {        /* el hilo no puede leer: lo que falte, de golpe */
        bclip_finish();
        dlog_error("No se pueden leer las animaciones de la tabla en segundo plano (%s)", bc_path);
        int c0 = gd.nclips;
        data_board_clips_load(names, n);
        *added = gd.nclips - c0;
        return 1;
    }
    if (bc_state == 2) bc_fails = 0;
    if (bc_state >= 2) *added = bclip_finish();
    /* el siguiente que falte */
    for (int k = 0; k < bclip_n && board_nbuf < BOARD_MAX_CLIPS; k++) {
        const char *nm = bclip_name[k];
        int want = 0;
        for (int i = 0; i < n && !want; i++) want = names[i] && !strcmp(names[i], nm);
        for (int c = 0; c < gd.nclips && want; c++) if (!strcmp(gd.clips[c].name, nm)) want = 0;
        if (!want) continue;
        uint32_t len = bclip_off[k + 1] - bclip_off[k];
        bc_buf = memalign(16, len);
        if (!bc_buf) return 1;
        bc_k = k;
        bc_req_gen = bc_gen;
        bc_state = 1;
        sceKernelSignalSema(bc_sema, 1);
        return 0;
    }
    return 1;
}

/* 1 si el hilo de las animaciones de la tabla esta leyendo (no se puede soltar su bufer) */
int data_board_clips_busy(void) { return bc_state == 1; }

static void sfx_big_open(void);
static int load_sfx(void)
{
    uint8_t *b = load_file("sfx.bin", NULL);
    if (!b) return -1;
    gd.nsfx = rd32(b + 4);
    gd.sfx = calloc(gd.nsfx, sizeof(SfxData));
    for (int i = 0; i < gd.nsfx; i++) {
        uint8_t *p = b + 8 + 40 * i;
        SfxData *s = &gd.sfx[i];
        int l = p[0]; if (l > 31) l = 31;
        memcpy(s->name, p + 1, l); s->name[l] = 0;
        s->off = rd32(p + 32); s->n = rd32(p + 36);
        s->pcm = (int16_t *)(b + s->off);
    }
    sfx_big_open();
    return 0;
}

/* La CPU de la PSP se cuelga (excepcion de direccion) al leer un float o un int de una direccion que no es
 * multiplo de 4; PPSSPP no lo imita. Los modelos, animaciones y sonidos se usan directamente desde el archivo
 * cargado, asi que se comprueba que todo este alineado antes de jugar. */
#define MISALIGNED(p, a) (((uintptr_t)(p)) & ((a) - 1))
static int check_alignment(void)
{
    int bad = 0;
    if (MISALIGNED(gd.nodes, 4) || MISALIGNED(gd.blob, 4)) { bad++; dlog_error("world.bin desalineado"); }
    for (int i = 0; i < gd.nmeshes; i++) {
        const Mesh *m = &gd.meshes[i];
        for (int k = 0; k < m->nparts; k++) {
            const MeshPart *pt = &m->parts[k];
            if (MISALIGNED(pt->verts, 4) || MISALIGNED(pt->idx, 2) || (m->skinned && MISALIGNED(pt->skin, 4))) {
                if (bad++ < 5) dlog_error("Modelo %s trozo %d desalineado", m->name, k);
            }
        }
        for (int k = 0; k < m->nbatches; k++)
            if (MISALIGNED(m->batches[k].verts, 4) || MISALIGNED(m->batches[k].idx, 2)) {
                if (bad++ < 5) dlog_error("Modelo %s lote de huesos %d desalineado", m->name, k);
            }
    }
    for (int i = 0; i < gd.nclips; i++)
        for (int t = 0; t < gd.clips[i].ntracks; t++)
            if (MISALIGNED(gd.clips[i].tracks[t].data, 4)) {
                if (bad++ < 5) dlog_error("Animacion %s pista %d desalineada", gd.clips[i].name, t);
            }
    for (int i = 0; i < gd.nsfx; i++)
        if (MISALIGNED(gd.sfx[i].pcm, 2)) { if (bad++ < 5) dlog_error("Sonido %s desalineado", gd.sfx[i].name); }
    dlog("Alineacion de los datos: %s (%d problemas)", bad ? "MAL" : "correcta", bad);
    return bad;
}

/* por pasos, para ir dibujando la pantalla de carga: primero el directorio de texturas (las letras de la interfaz
 * estan ahi), luego el resto de mas pequeno a mas grande */
int data_load_step(const char *dir, int step)
{
    static int (*const steps[])(void) = {load_textures, load_world, load_mats, load_sfx, load_anims, load_meshes};
    if (step == 0) snprintf(basedir, sizeof(basedir), "%s", dir);
    if (step < DATA_STEPS) return steps[step]();
    if (check_alignment()) { snprintf(errbuf, sizeof(errbuf), "Datos desalineados: vuelve a generar data/ con tools/extract.py"); return -1; }
    return 0;
}

int data_load(const char *dir)
{
    for (int s = 0; s <= DATA_STEPS; s++) if (data_load_step(dir, s)) return -1;
    return 0;
}

const uint32_t *node_comp(int node, int type, int *nwords)
{
    if (node < 0) return NULL;
    const NodeFile *n = &gd.nodes[node];
    const uint32_t *p = gd.blob + n->comp_off;
    for (int i = 0; i < n->ncomp; i++) {
        uint32_t t = p[0], nw = p[1];
        if ((int)t == type) { if (nwords) *nwords = nw; return p + 2; }
        p += 2 + nw;
    }
    return NULL;
}

int node_find_child(int node, const char *name)
{
    for (int c = gd.nodes[node].first_child; c >= 0; c = gd.nodes[c].next_sibling)
        if (!strcmp(gd.strings[gd.nodes[c].name], name)) return c;
    return -1;
}

int node_find_root(const char *name)
{
    for (int i = 0; i < gd.nnodes; i++)
        if (gd.nodes[i].parent < 0 && !strcmp(gd.strings[gd.nodes[i].name], name)) return i;
    return -1;
}

int node_find_path(int from, const char *path)
{
    char part[64];
    int cur = from;
    while (*path) {
        const char *s = strchr(path, '/');
        int l = s ? (int)(s - path) : (int)strlen(path);
        if (l > 63) l = 63;
        memcpy(part, path, l); part[l] = 0;
        cur = (cur < 0) ? node_find_root(part) : node_find_child(cur, part);
        if (cur < 0) return -1;
        path += l;
        if (*path == '/') path++;
    }
    return cur;
}

int clip_find(const char *name)
{
    for (int i = 0; i < gd.nclips; i++) if (!strcmp(gd.clips[i].name, name)) return i;
    return -1;
}

/* ---- efectos largos (sfxbig.bin): la caja misteriosa (6 s, 265 KB) y revivir (3 s) venian en MP3 y se quedaban sin
 * sonido. En sfx.bin estarian siempre en la RAM; aqui solo se leen (sceIoReadAsync, sin parar el juego) mientras el
 * juego los pide con sfx_ready, y se sueltan unos fotogramas despues de dejar de pedirlos y de sonar */
static char sb_path[200];
static int sb_fd = -1, sb_reading = -1, sb_frame;
static int sb_want[64];
static void sfx_big_open(void)
{
    char cwd[160];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
    snprintf(sb_path, sizeof(sb_path), "%s/%s/sfxbig.bin", cwd, basedir);
    FILE *f = fopen(sb_path, "rb");
    if (!f) { snprintf(sb_path, sizeof(sb_path), "%s/data/sfxbig.bin", cwd); f = fopen(sb_path, "rb"); }
    if (!f) { sb_path[0] = 0; return; }
    uint8_t h[8];
    int n = fread(h, 1, 8, f) == 8 && !memcmp(h, "SSB1", 4) ? (int)rd32(h + 4) : 0;
    for (int k = 0; k < n; k++) {
        uint8_t e[40];
        if (fread(e, 1, 40, f) != 40) break;
        char nm[32]; int l = e[0] > 31 ? 31 : e[0];
        memcpy(nm, e + 1, l); nm[l] = 0;
        int i = sfx_find(nm);
        if (i < 0 || gd.sfx[i].n) continue;   /* ya tiene sonido en sfx.bin */
        gd.sfx[i].big = 1; gd.sfx[i].boff = rd32(e + 32); gd.sfx[i].bn = rd32(e + 36);
        dlog("SONIDO largo %s: %u KB, se lee al usarlo", nm, (unsigned)(gd.sfx[i].bn * 2 / 1024));
    }
    fclose(f);
}
int sfx_ready(int i)
{
    if (i < 0 || i >= gd.nsfx) return 0;
    SfxData *s = &gd.sfx[i];
    if (!s->big) return s->n > 0;
    if (i < 64) sb_want[i] = sb_frame;
    if (s->n) return 1;
    if (sb_reading >= 0 || !sb_path[0]) return 0;    /* de uno en uno */
    if (!s->pcm) s->pcm = memalign(64, s->bn * 2);
    if (!s->pcm) return 0;
    sb_fd = sceIoOpen(sb_path, PSP_O_RDONLY, 0);
    if (sb_fd < 0 || sceIoLseek(sb_fd, s->boff, PSP_SEEK_SET) != (SceOff)s->boff || sceIoReadAsync(sb_fd, s->pcm, s->bn * 2) < 0) {
        if (sb_fd >= 0) sceIoClose(sb_fd);
        sb_fd = -1; free(s->pcm); s->pcm = NULL;
        dlog_error("No se puede leer el sonido %s de sfxbig.bin", s->name);
        sb_path[0] = 0;   /* no se vuelve a intentar */
        return 0;
    }
    sb_reading = i;
    return 0;
}
void sfx_big_tick(void)
{
    sb_frame++;
    if (sb_reading >= 0) {
        SceInt64 res = 0;
        if (sceIoPollAsync(sb_fd, &res) == 1) return;   /* aun leyendo */
        SfxData *s = &gd.sfx[sb_reading];
        sceIoClose(sb_fd); sb_fd = -1;
        if (res == (SceInt64)(s->bn * 2)) {
            s->n = s->bn;
            dlog("SONIDO %s leido (%u KB)", s->name, (unsigned)(s->bn * 2 / 1024));
        } else { free(s->pcm); s->pcm = NULL; }
        sb_reading = -1;
    }
    for (int i = 0; i < gd.nsfx && i < 64; i++) {
        SfxData *s = &gd.sfx[i];
        if (!s->big || !s->n || sb_frame - sb_want[i] < 30 || snd_uses(s->pcm)) continue;
        s->n = 0;   /* snd_play ya no lo toca */
        free(s->pcm); s->pcm = NULL;
    }
}
int sfx_find(const char *name)
{
    for (int i = 0; i < gd.nsfx; i++) if (!strcmp(gd.sfx[i].name, name)) return i;
    return -1;
}
