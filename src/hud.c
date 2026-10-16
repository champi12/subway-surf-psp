/* hud.c - carga de ui.bin (UIAtlas / UIFont de NGUI) y dibujo del marcador y los menus */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "render.h"
#include "data.h"
#include "game.h"

typedef struct { char name[32]; float ox, oy, ow, oh; } USprite;
typedef struct { char name[32]; int tex, w, h; float pixel; int nsp; USprite *sp; } UAtlas;
typedef struct { short index, x, y, w, h, ox, oy, adv; } UGlyph;
typedef struct {
    char name[32], sprite[32];
    int atlas, size, base, fw, fh, spacing, ng;
    UGlyph *g;
    UGlyph *map[256];
    float sx, sy;            /* origen del sprite de la fuente en el atlas */
} UFont;

static UAtlas atl[32];
static int natl;
static UFont fnt[48];
static int nfnt;

static void rdname(const unsigned char *p, char *out)
{
    int l = p[0]; if (l > 31) l = 31;
    memcpy(out, p + 1, l); out[l] = 0;
}

int ui_load(const char *dir)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/ui.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    int n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n);
    fread(b, 1, n, f);
    fclose(f);
    unsigned char *p = b + 4;
    unsigned na, nf;
    memcpy(&na, p, 4); memcpy(&nf, p + 4, 4); p += 8;
    for (unsigned i = 0; i < na; i++) {
        UAtlas *a = &atl[natl < 32 ? natl : 31];
        rdname(p, a->name); p += 32;
        memcpy(&a->tex, p, 4); memcpy(&a->w, p + 4, 4); memcpy(&a->h, p + 8, 4); memcpy(&a->pixel, p + 12, 4);
        memcpy(&a->nsp, p + 16, 4); p += 20;
        a->sp = malloc(sizeof(USprite) * (a->nsp ? a->nsp : 1));
        for (int k = 0; k < a->nsp; k++) {
            rdname(p, a->sp[k].name); p += 32;
            float r[8]; memcpy(r, p, 32); p += 32;
            a->sp[k].ox = r[0]; a->sp[k].oy = r[1]; a->sp[k].ow = r[2]; a->sp[k].oh = r[3];
        }
        if (natl < 32) natl++;
    }
    for (unsigned i = 0; i < nf; i++) {
        UFont *t = &fnt[nfnt < 48 ? nfnt : 47];
        rdname(p, t->name); p += 32;
        rdname(p, t->sprite); p += 32;
        int v[7]; memcpy(v, p, 28); p += 28;
        t->atlas = v[0]; t->size = v[1]; t->base = v[2]; t->fw = v[3]; t->fh = v[4]; t->spacing = v[5]; t->ng = v[6];
        t->g = malloc(sizeof(UGlyph) * (t->ng ? t->ng : 1));
        memset(t->map, 0, sizeof(t->map));
        for (int k = 0; k < t->ng; k++) {
            short s[9]; memcpy(s, p, 18); p += 18;
            UGlyph *g = &t->g[k];
            g->index = s[0]; g->x = s[1]; g->y = s[2]; g->w = s[3]; g->h = s[4]; g->ox = s[5]; g->oy = s[6]; g->adv = s[7];
            if (g->index >= 0 && g->index < 256) t->map[g->index] = g;
        }
        t->sx = t->sy = 0;
        if (t->atlas >= 0 && t->atlas < natl) {
            UAtlas *a = &atl[t->atlas];
            for (int k = 0; k < a->nsp; k++)
                if (!strcmp(a->sp[k].name, t->sprite)) { t->sx = a->sp[k].ox; t->sy = a->sp[k].oy; break; }
        }
        if (nfnt < 48) nfnt++;
    }
    free(b);
    return 0;
}

int ui_font(const char *name)
{
    for (int i = 0; i < nfnt; i++) if (!strcmp(fnt[i].name, name)) return i;
    return -1;
}

int ui_atlas(const char *name)
{
    for (int i = 0; i < natl; i++) if (!strcmp(atl[i].name, name)) return i;
    return -1;
}

float ui_text_width(int fi, float scale, const char *s)
{
    if (fi < 0) return strlen(s) * 8 * scale;
    UFont *t = &fnt[fi];
    float w = 0;
    for (; *s; s++) {
        UGlyph *g = t->map[(unsigned char)*s];
        if (g) w += (g->adv + t->spacing) * scale;
        else if (*s == ' ') w += t->size * 0.3f * scale;
    }
    return w;
}

float ui_text(int fi, float x, float y, float scale, unsigned int col, int align, const char *s)
{
    if (fi < 0) return 0;
    UFont *t = &fnt[fi];
    if (t->atlas < 0) return 0;
    UAtlas *a = &atl[t->atlas];
    float w = ui_text_width(fi, scale, s);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    for (; *s; s++) {
        UGlyph *g = t->map[(unsigned char)*s];
        if (!g) { if (*s == ' ') x += t->size * 0.3f * scale; continue; }
        float u0 = (t->sx + g->x) / a->w, v0 = (t->sy + g->y) / a->h;
        float u1 = (t->sx + g->x + g->w) / a->w, v1 = (t->sy + g->y + g->h) / a->h;
        r_sprite(a->tex, x + g->ox * scale, y + g->oy * scale, g->w * scale, g->h * scale, u0, v0, u1, v1, col);
        x += (g->adv + t->spacing) * scale;
    }
    return w;
}

int ui_sprite(int ai, const char *name, float x, float y, float scale, unsigned int col)
{
    if (ai < 0) return 0;
    UAtlas *a = &atl[ai];
    for (int k = 0; k < a->nsp; k++) {
        USprite *s = &a->sp[k];
        if (strcmp(s->name, name)) continue;
        r_sprite(a->tex, x, y, s->ow * scale * a->pixel, s->oh * scale * a->pixel, s->ox / a->w, s->oy / a->h,
                 (s->ox + s->ow) / a->w, (s->oy + s->oh) / a->h, col);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ marcador */
static int f_big = -2, f_mid, f_small, a_ui;

static void hud_fonts(void)
{
    if (f_big != -2) return;
    f_big = ui_font("Lilita30");
    f_mid = ui_font("Lilita20");
    f_small = ui_font("Lilita15");
    if (f_mid < 0) f_mid = f_big;
    if (f_small < 0) f_small = f_mid;
    a_ui = ui_atlas("LowResAtlas");
}

static void text_shadow(int f, float x, float y, float s, unsigned int col, int align, const char *str)
{
    ui_text(f, x + 1.5f, y + 1.5f, s, 0x80000000, align, str);
    ui_text(f, x, y, s, col, align, str);
}

static const char *pw_icons[6] = {"icon_upgrades_hoverboard", "icon_upgrades_jetpack", "icon_upgrades_superSneakers",
                                  "icon_upgrades_coinMagnet", "icon_upgrades_doubleScore", "icon_upgrades_headstart500"};

void hud_draw(void)
{
    hud_fonts();
    char buf[64];
    r_begin_2d();
    int st = ginfo.state;
    if (st == GS_MENU) {
        text_shadow(f_big, SCR_W / 2, 18, 0.9f, 0xffffffff, 1, "SUBWAY SURFERS");
        if (((int)(ginfo.time * 2)) & 1) text_shadow(f_mid, SCR_W / 2, 220, 0.8f, 0xff40e0ff, 1, "Pulsa X para correr");
        snprintf(buf, sizeof(buf), "Record: %d", ginfo.highscore);
        text_shadow(f_small, 10, 236, 0.8f, 0xffffffff, 0, buf);
        snprintf(buf, sizeof(buf), "Monedas: %d   Tablas: %d", ginfo.total_coins, ginfo.hoverboards);
        text_shadow(f_small, 10, 252, 0.8f, 0xffffffff, 0, buf);
        text_shadow(f_small, SCR_W - 10, 252, 0.7f, 0xffc0c0c0, 2, "[] comprar tabla (300)");
        return;
    }
    if (st == GS_GAMEOVER) {
        r_rect(90, 50, 300, 170, 0xa0000000);
        text_shadow(f_big, SCR_W / 2, 60, 0.9f, 0xffffffff, 1, "FIN DE LA CARRERA");
        snprintf(buf, sizeof(buf), "Puntos: %d", ginfo.final_score);
        text_shadow(f_mid, SCR_W / 2, 105, 0.9f, 0xff40e0ff, 1, buf);
        snprintf(buf, sizeof(buf), "Monedas: %d", ginfo.coins);
        text_shadow(f_mid, SCR_W / 2, 135, 0.8f, 0xffffffff, 1, buf);
        snprintf(buf, sizeof(buf), "Record: %d", ginfo.highscore);
        text_shadow(f_small, SCR_W / 2, 165, 0.8f, 0xffffffff, 1, buf);
        text_shadow(f_small, SCR_W / 2, 195, 0.8f, 0xffc0c0c0, 1, "Pulsa X para continuar");
        return;
    }
    /* en partida */
    snprintf(buf, sizeof(buf), "%06d", ginfo.score);
    text_shadow(f_big, SCR_W - 8, 4, 0.75f, 0xffffffff, 2, buf);
    if (ginfo.multiplier > 1) {
        snprintf(buf, sizeof(buf), "x%d", ginfo.multiplier);
        text_shadow(f_mid, SCR_W - 8, 30, 0.7f, 0xff40e0ff, 2, buf);
    }
    snprintf(buf, sizeof(buf), "%d", ginfo.coins);
    float w = ui_text_width(f_mid, 0.7f, buf);
    ui_sprite(a_ui, "icon_coins", SCR_W - 8 - w - 22, 46, 0.35f, 0xffffffff);
    text_shadow(f_mid, SCR_W - 8, 46, 0.7f, 0xff60f0ff, 2, buf);
    /* barras de power-ups (lado derecho) */
    float y = 80;
    for (int i = 0; i < 6; i++) {
        if (ginfo.pw_left[i] <= 0 || ginfo.pw_total[i] <= 0) continue;
        float r = ginfo.pw_left[i] / ginfo.pw_total[i];
        if (r > 1) r = 1;
        ui_sprite(a_ui, pw_icons[i], SCR_W - 110, y, 0.35f, 0xffffffff);
        r_rect(SCR_W - 82, y + 8, 74, 8, 0x80000000);
        r_rect(SCR_W - 82, y + 8, 74 * r, 8, 0xff30d0ff);
        y += 26;
    }
    if (ginfo.message && ginfo.time - ginfo.message_t < 2.5f)
        text_shadow(f_mid, SCR_W / 2, 60, 0.8f, 0xffffffff, 1, ginfo.message);
    if (ginfo.paused) {
        r_rect(0, 0, SCR_W, SCR_H, 0x80000000);
        text_shadow(f_big, SCR_W / 2, 100, 1.0f, 0xffffffff, 1, "PAUSA");
        text_shadow(f_small, SCR_W / 2, 150, 0.8f, 0xffc0c0c0, 1, "START o X para seguir");
    }
}

/* ------------------------------------------------------------ medidor de rendimiento */
void perf_draw(float cpu_ms, float gpu_ms, float fps, unsigned int free_kb, int verts, int tris, int draws, int clipped)
{
    hud_fonts();
    char buf[128];
    r_begin_2d();
    r_rect(0, 0, 250, 74, 0xa0000000);
    unsigned int c = fps >= 29.5f ? 0xff40ff40 : (fps >= 24 ? 0xff40ffff : 0xff4040ff);
    snprintf(buf, sizeof(buf), "FPS %.1f  CPU %.1f ms  espera %.1f ms", fps, cpu_ms, gpu_ms);
    ui_text(f_small, 4, 2, 0.6f, c, 0, buf);
    snprintf(buf, sizeof(buf), "vert %d  tri %d  draw %d  clip %d", verts, tris, draws, clipped);
    ui_text(f_small, 4, 20, 0.6f, 0xffffffff, 0, buf);
    snprintf(buf, sizeof(buf), "RAM libre %u KB", free_kb);
    ui_text(f_small, 4, 38, 0.6f, free_kb > 2048 ? 0xffffffff : 0xff4040ff, 0, buf);
    ui_text(f_small, 4, 56, 0.6f, 0xffc0c0c0, 0, "L+SELECT: ocultar");
}
