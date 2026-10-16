/* data.c - carga de world.bin, mat.bin, mesh.bin, tex.bin, anim.bin y sfx.bin */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "data.h"

GameData gd;
static char errbuf[128];
static char basedir[128];

const char *data_err(void) { return errbuf; }

static void *load_file(const char *name, int *size)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", basedir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(errbuf, sizeof(errbuf), "No se encuentra %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    int n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = memalign(64, n + 16);
    if (!buf) { fclose(f); snprintf(errbuf, sizeof(errbuf), "Sin memoria para %s (%d KB)", name, n / 1024); return NULL; }
    fread(buf, 1, n, f);
    fclose(f);
    if (size) *size = n;
    return buf;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

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

static int load_meshes(void)
{
    uint8_t *b = load_file("mesh.bin", NULL);
    if (!b) return -1;
    if (memcmp(b, "SSM2", 4)) { snprintf(errbuf, sizeof(errbuf), "mesh.bin antiguo: vuelve a ejecutar extract.py"); return -1; }
    gd.nmeshes = rd32(b + 4);
    gd.meshes = calloc(gd.nmeshes, sizeof(Mesh));
    for (int i = 0; i < gd.nmeshes; i++) {
        uint8_t *p = b + rd32(b + 8 + 4 * i);
        Mesh *m = &gd.meshes[i];
        int l = p[0]; if (l > 31) l = 31;
        memcpy(m->name, p + 1, l); m->name[l] = 0;
        p += 32;
        uint16_t np, nsub;
        memcpy(&np, p, 2); memcpy(&nsub, p + 2, 2);
        m->skinned = p[4];
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
    return 0;
}

static FILE *texfile;

static int load_textures(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/tex.bin", basedir);
    texfile = fopen(path, "rb");
    if (!texfile) { snprintf(errbuf, sizeof(errbuf), "No se encuentra %s", path); return -1; }
    uint8_t h[8];
    fread(h, 1, 8, texfile);
    gd.ntex = rd32(h + 4);
    gd.tex = calloc(gd.ntex, sizeof(Texture));
    uint32_t *offs = malloc(4 * gd.ntex);
    fread(offs, 4, gd.ntex, texfile);
    for (int i = 0; i < gd.ntex; i++) {
        uint8_t th[48];
        fseek(texfile, offs[i], SEEK_SET);
        fread(th, 1, 48, texfile);
        Texture *t = &gd.tex[i];
        int l = th[0]; if (l > 31) l = 31;
        memcpy(t->name, th + 1, l); t->name[l] = 0;
        uint16_t w, hh, fmt, al;
        memcpy(&w, th + 32, 2); memcpy(&hh, th + 34, 2); memcpy(&fmt, th + 36, 2); memcpy(&al, th + 38, 2);
        t->w = w; t->h = hh; t->fmt = fmt; t->alpha = al;
        t->palbytes = rd32(th + 40); t->databytes = rd32(th + 44);
        t->file_off = offs[i] + 48;
    }
    free(offs);
    return 0;
}

int data_tex_load(int i)
{
    if (i < 0 || i >= gd.ntex) return -1;
    Texture *t = &gd.tex[i];
    if (t->loaded) return 0;
    fseek(texfile, t->file_off, SEEK_SET);
    if (t->palbytes) {
        t->pal = memalign(64, t->palbytes);
        fread(t->pal, 1, t->palbytes, texfile);
    }
    t->data = memalign(64, t->databytes);
    if (!t->data) return -1;
    fread(t->data, 1, t->databytes, texfile);
    t->loaded = 1;
    return 0;
}

static int load_anims(void)
{
    uint8_t *b = load_file("anim.bin", NULL);
    if (!b) return -1;
    gd.nclips = rd32(b + 4);
    gd.clips = calloc(gd.nclips, sizeof(Clip));
    for (int i = 0; i < gd.nclips; i++) {
        uint8_t *p = b + rd32(b + 8 + 4 * i);
        Clip *c = &gd.clips[i];
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
    return 0;
}

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
    return 0;
}

int data_load(const char *dir)
{
    snprintf(basedir, sizeof(basedir), "%s", dir);
    if (load_world() || load_mats() || load_meshes() || load_textures() || load_anims() || load_sfx()) return -1;
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

int sfx_find(const char *name)
{
    for (int i = 0; i < gd.nsfx; i++) if (!strcmp(gd.sfx[i].name, name)) return i;
    return -1;
}
