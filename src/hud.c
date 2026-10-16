/* hud.c - carga de ui.bin (UIAtlas / UIFont de NGUI) y dibujo del marcador y los menus */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "hud.h"
#include "render.h"
#include "data.h"
#include "game.h"
#include "missions.h"
#include "devlog.h"

typedef struct { char name[32]; float ox, oy, ow, oh, ix, iy, iw, ih; } USprite;
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
            a->sp[k].ix = r[4]; a->sp[k].iy = r[5]; a->sp[k].iw = r[6]; a->sp[k].ih = r[7];
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

/* ------------------------------------------------------------ dibujo con sprites 9-slice (UISlicedSprite) */
static USprite *find_sprite(int ai, const char *name)
{
    if (ai < 0) return NULL;
    UAtlas *a = &atl[ai];
    for (int k = 0; k < a->nsp; k++) if (!strcmp(a->sp[k].name, name)) return &a->sp[k];
    return NULL;
}

static void ui_sliced(int ai, const char *name, float x, float y, float w, float h, float bscale, unsigned int col)
{
    USprite *s = find_sprite(ai, name);
    if (!s) { r_rect(x, y, w, h, col); return; }
    UAtlas *a = &atl[ai];
    float l = s->ix - s->ox, r = s->ox + s->ow - (s->ix + s->iw), t = s->iy - s->oy, b = s->oy + s->oh - (s->iy + s->ih);
    float L = l * bscale, R = r * bscale, T = t * bscale, B = b * bscale;
    if (L + R > w) { float k = w / (L + R); L *= k; R *= k; }
    if (T + B > h) { float k = h / (T + B); T *= k; B *= k; }
    float xs[4] = {x, x + L, x + w - R, x + w}, ys[4] = {y, y + T, y + h - B, y + h};
    float us[4] = {s->ox, s->ix, s->ix + s->iw, s->ox + s->ow}, vs[4] = {s->oy, s->iy, s->iy + s->ih, s->oy + s->oh};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
            r_sprite(a->tex, xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], us[i] / a->w, vs[j] / a->h,
                     us[i + 1] / a->w, vs[j + 1] / a->h, col);
        }
}

/* sprite escalado a un tamano dado */
static void ui_sprite_wh(int ai, const char *name, float x, float y, float w, float h, unsigned int col)
{
    USprite *s = find_sprite(ai, name);
    if (!s) return;
    UAtlas *a = &atl[ai];
    r_sprite(a->tex, x, y, w, h, s->ox / a->w, s->oy / a->h, (s->ox + s->ow) / a->w, (s->oy + s->oh) / a->h, col);
}

/* ------------------------------------------------------------ marcador (IngameUI) */
static int f_big = -2, f_mid, f_small, f_titan, a_ui;
#define UIS 0.85f              /* unidades de la interfaz del movil (320x480) -> pixeles de la PSP */

static void hud_fonts(void)
{
    if (f_big != -2) return;
    f_big = ui_font("Lilita30");
    f_mid = ui_font("Lilita20");
    f_small = ui_font("Lilita15");
    f_titan = ui_font("Titan60");
    if (f_mid < 0) f_mid = f_big;
    if (f_small < 0) f_small = f_mid;
    if (f_titan < 0) f_titan = f_big;
    a_ui = ui_atlas("LowResAtlas");
}

static void text_shadow(int f, float x, float y, float s, unsigned int col, int align, const char *str)
{
    ui_text(f, x + 1.5f, y + 1.5f, s, 0x80000000, align, str);
    ui_text(f, x, y, s, col, align, str);
}

/* tamano de letra del original (23 px) con la fuente Lilita30 (34 px) */
#define LBL (23.0f * UIS / 34.0f)

static const char *pw_icons[6] = {"icon_upgrades_hoverboard", "icon_upgrades_jetpack", "icon_upgrades_superSneakers",
                                  "icon_upgrades_coinMagnet", "icon_upgrades_doubleScore", "icon_upgrades_headstart500"};
#define BOX_COL 0x66000000     /* background_ingame_beatThis, negro al 40 % */

static void draw_ingame(void)
{
    char buf[64];
    /* puntuacion con ceros a la izquierda (UIIngameUpdater.SetScoreLabel) */
    snprintf(buf, sizeof(buf), "%06d", ginfo.score);
    float tw = ui_text_width(f_big, LBL, buf);
    float bh = 30 * UIS, bw = tw + 16 * UIS;
    float x1 = SCR_W - 4, y = 4;
    ui_sliced(a_ui, "background_ingame_beatThis", x1 - bw, y, bw, bh, UIS, BOX_COL);
    ui_text(f_big, x1 - 8 * UIS, y + 3 * UIS, LBL, 0xffffffff, 2, buf);
    /* multiplicador */
    snprintf(buf, sizeof(buf), "x%d", ginfo.multiplier);
    float mw = 40 * UIS;
    ui_sliced(a_ui, "background_ingame_beatThis", x1 - bw - 4 - mw, y, mw, bh, UIS, BOX_COL);
    ui_text(f_big, x1 - bw - 4 - mw * 0.5f, y + 3 * UIS, LBL, 0xff00dbff, 1, buf);
    /* monedas */
    snprintf(buf, sizeof(buf), "%d", ginfo.coins);
    tw = ui_text_width(f_big, LBL, buf);
    float cw = tw + 34 * UIS, cy = y + bh + 5 * UIS;
    ui_sliced(a_ui, "background_ingame_beatThis", x1 - cw, cy, cw, bh, UIS, BOX_COL);
    ui_sprite_wh(a_ui, "coin2d", x1 - cw + 5 * UIS, cy + 6 * UIS, 18 * UIS, 18 * UIS, 0xffffffff);
    ui_text(f_big, x1 - 8 * UIS, cy + 3 * UIS, LBL, 0xffffffff, 2, buf);
    /* boton de pausa (arriba a la izquierda) */
    ui_sliced(a_ui, "background_ingame_button", 4, 4, 40 * UIS, 40 * UIS, UIS, 0xffffffff);
    ui_sprite_wh(a_ui, "pause", 4 + 14 * UIS, 4 + 13 * UIS, 13 * UIS, 14 * UIS, 0xffffffff);
    /* power-ups activos (UIPowerupHandler: 4 huecos abajo en el centro) */
    static const float slot[4][2] = {{-135, 10}, {20, 10}, {-135, 50}, {20, 50}};
    int k = 0;
    for (int i = 5; i >= 0 && k < 4; i--) {
        float left = ginfo.pw_left[i], tot = ginfo.pw_total[i];
        if (left <= 0 || tot <= 0) continue;
        float sx = SCR_W / 2 + slot[k][0] * UIS, sy = SCR_H - (slot[k][1] + 34) * UIS;
        float r = left / tot;
        if (r > 1) r = 1;
        ui_sprite_wh(a_ui, pw_icons[i], sx, sy, 32 * UIS, 32 * UIS, 0xffffffff);
        ui_sliced(a_ui, "progressbar_background", sx + 34 * UIS, sy + 10 * UIS, 80 * UIS, 14 * UIS, UIS * 0.5f, 0xffffffff);
        if (r > 0.02f)
            ui_sliced(a_ui, "progressbar_bar_on", sx + 34 * UIS, sy + 10 * UIS, 80 * UIS * r, 14 * UIS, UIS * 0.5f, 0xffffffff);
        if (i == 0) {
            snprintf(buf, sizeof(buf), "%d", ginfo.hoverboards);
            text_shadow(f_small, sx + 30 * UIS, sy + 18 * UIS, 0.6f, 0xffffffff, 2, buf);
        }
        k++;
    }
    /* headstarts al empezar (UIHeadStartHelper) */
    if (ginfo.time < ginfo.headstart_until) {
        float hx = 6, hy = SCR_H - 100 * UIS;
        if (ginfo.hs500 > 0) {
            ui_sliced(a_ui, "background_ingame_transparent_withStroke", hx, hy, 60 * UIS, 74 * UIS, UIS, 0xc0000000);
            ui_sprite_wh(a_ui, "button_ingame_headStart500", hx + 8 * UIS, hy + 4 * UIS, 44 * UIS, 66 * UIS, 0xffffffff);
            snprintf(buf, sizeof(buf), "L  x%d", ginfo.hs500);
            text_shadow(f_small, hx + 30 * UIS, hy + 76 * UIS, 0.6f, 0xffffffff, 1, buf);
            hx += 70 * UIS;
        }
        if (ginfo.hs2000 > 0) {
            ui_sliced(a_ui, "background_ingame_transparent_withStroke", hx, hy, 60 * UIS, 74 * UIS, UIS, 0xc0000000);
            ui_sprite_wh(a_ui, "button_ingame_headStart2000", hx + 8 * UIS, hy + 4 * UIS, 44 * UIS, 66 * UIS, 0xffffffff);
            snprintf(buf, sizeof(buf), "R  x%d", ginfo.hs2000);
            text_shadow(f_small, hx + 30 * UIS, hy + 76 * UIS, 0.6f, 0xffffffff, 1, buf);
        }
    }
    if (ginfo.paused) {
        r_rect(0, 0, SCR_W, SCR_H, 0x80000000);
        if (ginfo.countdown > 0) {
            /* "Starting in" 3, 2, 1 con el numero latiendo como en el original */
            text_shadow(f_big, SCR_W / 2, SCR_H / 2 - 50, 0.8f, 0xffffffff, 1, "Starting in");
            snprintf(buf, sizeof(buf), "%d", (int)ceilf(ginfo.countdown));
            float frac = ginfo.countdown - floorf(ginfo.countdown);
            float sc = 0.8f * ((1.0f - frac) * 0.5f + 1.0f);
            text_shadow(f_titan, SCR_W / 2, SCR_H / 2 - 20, sc, 0xffffffff, 1, buf);
        } else {
            text_shadow(f_big, SCR_W / 2, SCR_H / 2 - 40, 1.0f, 0xffffffff, 1, "PAUSA");
            text_shadow(f_small, SCR_W / 2, SCR_H / 2 + 10, 0.8f, 0xffc0c0c0, 1, "START o X para seguir");
        }
    }
}

/* ------------------------------------------------------------ misiones (UIMissionsHelper) */
/* boton del menu: icono, multiplicador actual y misiones hechas */
static void draw_missions_button(float x, float y)
{
    char buf[32];
    int n = 0;
    for (int i = 0; i < 3; i++) n += missions_done(i);
    ui_sliced(a_ui, "background_ingame_beatThis", x, y, 150, 44, UIS, 0x99000000);
    ui_sprite_wh(a_ui, "button_mission_firstscreen", x + 4, y + 4, 36, 36, 0xffffffff);
    text_shadow(f_mid, x + 46, y + 4, 0.65f, 0xffffffff, 0, "Misiones");
    snprintf(buf, sizeof(buf), "x%d", missions_multiplier());
    text_shadow(f_big, x + 144, y + 2, 0.6f, 0xff00dbff, 2, buf);
    snprintf(buf, sizeof(buf), "%d/3   L / R", n);
    ui_text(f_small, x + 46, y + 25, 0.55f, 0xffc0c0c0, 0, buf);
}

/* ventana con las 3 misiones del grupo actual */
static void draw_missions_popup(void)
{
    char buf[64];
    r_rect(0, 0, SCR_W, SCR_H, 0x80000000);
    float x = 50, y = 36, w = SCR_W - 100;
    ui_sliced(a_ui, "background_ingame_popup", x, y, w, 200, UIS, 0xffffffff);
    text_shadow(f_big, SCR_W / 2, y + 10, 0.85f, 0xffffffff, 1, "Misiones");
    snprintf(buf, sizeof(buf), "Multiplicador x%d", missions_multiplier());
    text_shadow(f_mid, SCR_W / 2, y + 42, 0.7f, 0xff00dbff, 1, buf);
    for (int i = 0; i < 3; i++) {
        float ry = y + 72 + i * 34;
        int done = missions_done(i);
        ui_sprite_wh(a_ui, done ? "checkbox_checked_mission" : "checkbox_mission", x + 16, ry, 26, 26, 0xffffffff);
        missions_text(i, buf, sizeof(buf));
        text_shadow(f_mid, x + 50, ry + 4, 0.62f, done ? 0xff80ff80 : 0xffffffff, 0, buf);
        if (!done) {
            snprintf(buf, sizeof(buf), "%d/%d", missions_progress(i), missions_goal(i));
            text_shadow(f_mid, x + w - 16, ry + 4, 0.62f, 0xff00dbff, 2, buf);
        }
    }
    ui_text(f_small, SCR_W / 2, y + 180, 0.6f, 0xffc0c0c0, 1, "Completa las 3 para subir el multiplicador");
}

/* aviso de mision completada (UINotification) */
static void draw_mission_msg(void)
{
    if (!mission_msg[0]) return;
    float t = ginfo.time - mission_msg_t;
    if (t < 0 || t > 3.0f) return;
    float a = t < 0.2f ? t / 0.2f : t > 2.6f ? (3.0f - t) / 0.4f : 1.0f;
    float w = ui_text_width(f_mid, 0.65f, mission_msg) + 50;
    float x = (SCR_W - w) * 0.5f, y = 52;
    ui_sliced(a_ui, "background_ingame_beatThis", x, y, w, 26, UIS, ((unsigned)(a * 0xc0) << 24));
    ui_sprite_wh(a_ui, "icon_tick_small", x + 6, y + 4, 18, 18, ((unsigned)(a * 255) << 24) | 0xffffff);
    ui_text(f_mid, x + 30, y + 4, 0.65f, ((unsigned)(a * 255) << 24) | 0x00dbff, 0, mission_msg);
}

/* ------------------------------------------------------------ menu principal (FrontUI) */
static void draw_menu(void)
{
    char buf[64];
    /* hoverboards (arriba a la derecha) */
    float bx = SCR_W - 4 - 44 * UIS, by = 4;
    ui_sliced(a_ui, "background_ingame_button", bx, by, 44 * UIS, 44 * UIS, UIS, 0xffffffff);
    ui_sprite_wh(a_ui, "icon_hoverboard", bx + 6 * UIS, by + 6 * UIS, 32 * UIS, 19 * UIS, 0xffffffff);
    snprintf(buf, sizeof(buf), "%d", ginfo.hoverboards);
    text_shadow(f_small, bx + 30 * UIS, by + 24 * UIS, 0.6f, 0xffffffff, 1, buf);
    /* monedas y record (arriba a la izquierda) */
    snprintf(buf, sizeof(buf), "%d", ginfo.total_coins);
    float tw = ui_text_width(f_big, LBL, buf);
    ui_sliced(a_ui, "background_ingame_beatThis", 4, 4, tw + 34 * UIS, 30 * UIS, UIS, BOX_COL);
    ui_sprite_wh(a_ui, "coin2d", 4 + 5 * UIS, 4 + 6 * UIS, 18 * UIS, 18 * UIS, 0xffffffff);
    ui_text(f_big, 4 + 26 * UIS, 4 + 3 * UIS, LBL, 0xffffffff, 0, buf);
    snprintf(buf, sizeof(buf), "Record %d", ginfo.highscore);
    tw = ui_text_width(f_mid, 0.6f, buf);
    ui_sliced(a_ui, "background_ingame_beatThis", 4, 4 + 34 * UIS, tw + 16, 24 * UIS, UIS, BOX_COL);
    ui_text(f_mid, 12, 4 + 36 * UIS, 0.6f, 0xff00dbff, 0, buf);
    draw_missions_button(4, 4 + 64 * UIS);
    /* "Tap to play" */
    float a = 0.6f + 0.4f * sinf(ginfo.time * 4.0f);
    unsigned int col = ((unsigned)(a * 255) << 24) | 0xffffff;
    text_shadow(f_big, SCR_W / 2, SCR_H - 70, 0.85f, col, 1, "Pulsa X para jugar");
    /* boton de la tienda (abajo a la derecha) */
    float sx = SCR_W - 4 - 130 * UIS, sy = SCR_H - 4 - 44 * UIS;
    ui_sliced(a_ui, "button_fill_menuShop", sx, sy, 130 * UIS, 44 * UIS, UIS, 0xffffffff);
    ui_sliced(a_ui, "button_generic_2_outline", sx - 4, sy - 4, 130 * UIS + 8, 44 * UIS + 8, UIS, 0xffffffff);
    text_shadow(f_mid, sx + 65 * UIS, sy + 8 * UIS, 0.75f, 0xffffffff, 1, "Tienda");
    ui_text(f_small, sx + 65 * UIS, sy - 16, 0.55f, 0xffffffff, 1, "triangulo / cuadrado");
    if (ginfo.missions_open) draw_missions_popup();
}

/* ------------------------------------------------------------ tienda */
static void draw_shop(void)
{
    char buf[64];
    r_rect(0, 0, SCR_W, SCR_H, 0x90000000);
    text_shadow(f_big, SCR_W / 2, 6, 0.8f, 0xffffffff, 1, "Tienda");
    snprintf(buf, sizeof(buf), "%d", ginfo.total_coins);
    float tw = ui_text_width(f_big, LBL, buf);
    ui_sliced(a_ui, "background_ingame_beatThis", SCR_W - 8 - tw - 34 * UIS, 6, tw + 34 * UIS, 30 * UIS, UIS, BOX_COL);
    ui_sprite_wh(a_ui, "coin2d", SCR_W - 8 - tw - 30 * UIS, 6 + 6 * UIS, 18 * UIS, 18 * UIS, 0xffffffff);
    ui_text(f_big, SCR_W - 12, 6 + 3 * UIS, LBL, 0xffffffff, 2, buf);
    /* 7 filas visibles; la lista se desplaza con la seleccion */
    static int top;
    const int rows = 7;
    if (ginfo.shop_sel < top) top = ginfo.shop_sel;
    if (ginfo.shop_sel >= top + rows) top = ginfo.shop_sel - rows + 1;
    if (top > shop_count - rows) top = shop_count - rows;
    if (top < 0) top = 0;
    float y = 40;
    if (top > 0) ui_text(f_small, SCR_W / 2, 30, 0.5f, 0xffc0c0c0, 1, "...");
    if (top + rows < shop_count) ui_text(f_small, SCR_W / 2, 40 + rows * 28 - 4, 0.5f, 0xffc0c0c0, 1, "...");
    for (int i = top; i < shop_count && i < top + rows; i++) {
        int sel = i == ginfo.shop_sel;
        ui_sliced(a_ui, sel ? "button_fill_shopItem_green" : "button_fill_shopItem_gray", 20, y, SCR_W - 40, 26, 0.5f,
                  sel ? 0xffffffff : 0xc0ffffff);
        ui_sprite_wh(a_ui, shop_items[i].icon, 24, y + 1, 24, 24, 0xffffffff);
        ui_text(f_mid, 54, y + 4, 0.62f, 0xffffffff, 0, shop_items[i].name);
        int am = shop_amount(i), pr = shop_price(i);
        if (shop_items[i].kind == 1) {
            /* barra de nivel (5 niveles) */
            for (int k = 0; k < 5; k++)
                ui_sprite_wh(a_ui, k < am ? "progressbar_bar_on" : "progressbar_bar_off", 230 + k * 22, y + 8, 18, 10, 0xffffffff);
        } else if (shop_items[i].kind == 0) {
            snprintf(buf, sizeof(buf), "x%d", am);
            ui_text(f_mid, 300, y + 4, 0.62f, 0xffffffff, 1, buf);
        } else if (shop_items[i].kind == 3) {
            /* saltar mision: progreso de la mision que se salta */
            if (am) snprintf(buf, sizeof(buf), "Hecha");
            else snprintf(buf, sizeof(buf), "%d/%d", missions_progress(shop_items[i].idx), missions_goal(shop_items[i].idx));
            ui_text(f_mid, 300, y + 4, 0.62f, am ? 0xff80ff80 : 0xffffffff, 1, buf);
        }
        if (pr >= 0) snprintf(buf, sizeof(buf), "%d", pr); else snprintf(buf, sizeof(buf), "MAX");
        ui_sprite_wh(a_ui, "coin2d", SCR_W - 90, y + 5, 16, 16, 0xffffffff);
        ui_text(f_mid, SCR_W - 28, y + 4, 0.62f, pr >= 0 && ginfo.total_coins >= pr ? 0xffffffff : 0xff8080ff, 2, buf);
        y += 28;
    }
    if (shop_items[ginfo.shop_sel].kind == 3) {
        /* texto de la mision seleccionada */
        missions_text(shop_items[ginfo.shop_sel].idx, buf, sizeof(buf));
        ui_text(f_small, SCR_W / 2, SCR_H - 32, 0.55f, 0xffffffff, 1, buf);
    }
    ui_text(f_small, SCR_W / 2, SCR_H - 16, 0.6f, 0xffc0c0c0, 1, "X comprar    O volver");
    if (ginfo.reward[0] && ginfo.time - ginfo.reward_t < 2.5f) {
        r_rect(90, 110, 300, 50, 0xd0000000);
        text_shadow(f_big, SCR_W / 2, 122, 0.7f, 0xff00dbff, 1, ginfo.reward);
    }
}

/* ------------------------------------------------------------ fin de la carrera */
static void draw_gameover(void)
{
    char buf[64];
    ui_sliced(a_ui, "background_ingame_popup", 80, 30, 320, 210, UIS, 0xffffffff);
    text_shadow(f_big, SCR_W / 2, 42, 0.85f, 0xffffffff, 1, "Fin de la carrera");
    snprintf(buf, sizeof(buf), "%d", ginfo.final_score);
    text_shadow(f_titan, SCR_W / 2, 80, 0.75f, 0xff00dbff, 1, buf);
    snprintf(buf, sizeof(buf), "%d", ginfo.gameover_coins);
    float tw = ui_text_width(f_big, 0.7f, buf);
    ui_sprite_wh(a_ui, "coin2d", SCR_W / 2 - tw / 2 - 26, 138, 20, 20, 0xffffffff);
    text_shadow(f_big, SCR_W / 2, 134, 0.7f, 0xffffffff, 1, buf);
    snprintf(buf, sizeof(buf), "Record %d", ginfo.highscore);
    text_shadow(f_mid, SCR_W / 2, 170, 0.7f, 0xffffffff, 1, buf);
    if (ginfo.boxes_pending > 0) {
        snprintf(buf, sizeof(buf), "X: abrir caja misteriosa (%d)", ginfo.boxes_pending);
        text_shadow(f_small, SCR_W / 2, 205, 0.7f, 0xff40e0ff, 1, buf);
    } else text_shadow(f_small, SCR_W / 2, 205, 0.7f, 0xffc0c0c0, 1, "Pulsa X para continuar");
    if (ginfo.reward[0] && ginfo.time - ginfo.reward_t < 2.5f) {
        r_rect(90, 110, 300, 50, 0xd0000000);
        text_shadow(f_big, SCR_W / 2, 122, 0.7f, 0xff00dbff, 1, ginfo.reward);
    }
}

void hud_draw(void)
{
    hud_fonts();
    r_begin_2d();
    switch (ginfo.state) {
    case GS_MENU: draw_menu(); break;
    case GS_SHOP: draw_shop(); break;
    case GS_GAMEOVER: draw_gameover(); break;
    default: draw_ingame(); break;
    }
    if (ginfo.state != GS_MENU) draw_mission_msg();
}

/* ------------------------------------------------------------ medidor de rendimiento (SELECT) */
#define C_OK 0xff40ff40
#define C_WARN 0xff40ffff
#define C_BAD 0xff4040ff
static unsigned int level_col(float frac, float warn, float bad) { return frac >= bad ? C_BAD : frac >= warn ? C_WARN : C_OK; }

static void bar(float x, float y, float w, float h, float frac, unsigned int col)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    r_rect(x, y, w, h, 0xff404040);
    if (frac > 0) r_rect(x, y, w * frac, h, col);
}

static void perf_line(float y, unsigned int col, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void perf_line(float y, unsigned int col, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_text(f_small, 4, y, 0.6f, col, 0, buf);
}

static void perf_summary(float cpu_ms, float gpu_ms, float fps)
{
    const MemInfo *m = &dbg_mem;
    float hf = m->heap_total ? (float)m->heap_used / m->heap_total : 0;
    r_rect(0, 0, 268, 74, 0xa0000000);
    perf_line(2, level_col(-fps, -29.5f, -24), "FPS %.1f  CPU %.1f ms  espera %.1f ms", fps, cpu_ms, gpu_ms);
    perf_line(20, 0xffffffff, "vert %d  tri %d  draw %d  clip %d", r_stat_verts, r_stat_tris, r_stat_draws, r_stat_clipped);
    perf_line(38, level_col(hf, 0.75f, 0.9f), "RAM %.1f/%.1f MB   VRAM %d/2048 KB", m->heap_used / 1048576.0f,
              m->heap_total / 1048576.0f, (r_mem.vram_fb + r_mem.vram_tex) / 1024);
    perf_line(56, 0xffc0c0c0, "SELECT: memoria");
}

static void perf_memory(float fps)
{
    const MemInfo *m = &dbg_mem;
    const RMem *r = &r_mem;
    float hf = m->heap_total ? (float)m->heap_used / m->heap_total : 0;
    float vf = (float)(r->vram_fb + r->vram_tex) / R_VRAM_TOTAL;
    r_rect(0, 0, 330, 196, 0xc0000000);
    perf_line(2, 0xffffffff, "MEMORIA   (FPS %.1f)", fps);
    perf_line(20, level_col(hf, 0.75f, 0.9f), "RAM monton: %.2f / %.2f MB (%.0f%%) pico %.2f", m->heap_used / 1048576.0f,
              m->heap_total / 1048576.0f, hf * 100, m->heap_peak / 1048576.0f);
    bar(6, 36, 318, 5, hf, level_col(hf, 0.75f, 0.9f));
    perf_line(44, m->sys_free < 256 * 1024 ? C_BAD : 0xffffffff, "Sistema libre: %u KB (bloque mayor %u KB)", m->sys_free / 1024,
              m->sys_max_block / 1024);
    perf_line(62, r->vram_full ? C_WARN : 0xffffffff, "VRAM: %d / 2048 KB  pantalla+Z %d  tex %d KB (%d)",
              (r->vram_fb + r->vram_tex) / 1024, r->vram_fb / 1024, r->vram_tex / 1024, r->vram_tex_n);
    bar(6, 78, 318, 5, vf, C_OK);
    perf_line(86, 0xffffffff, "Texturas en RAM: %d KB (%d)   sin sitio en VRAM: %d", r->ram_tex / 1024, r->ram_tex_n, r->vram_full);
    float lf = (float)r->dlist_peak / r->dlist_max, vrf = (float)r->vring_peak / r->vring_max, irf = (float)r->iring_peak / r->iring_max;
    perf_line(104, level_col(lf, 0.6f, 0.8f), "Lista GE: %d%% (pico %d%%) de %d KB", r->dlist_used * 100 / r->dlist_max,
              (int)(lf * 100), r->dlist_max / 1024);
    perf_line(122, level_col(vrf > irf ? vrf : irf, 0.7f, 0.9f), "Buffer vert %d%% (pico %d%%)  ind %d%% (pico %d%%)",
              r->vring_used * 100 / r->vring_max, (int)(vrf * 100), r->iring_used * 100 / r->iring_max, (int)(irf * 100));
    perf_line(140, r->dropped ? C_BAD : 0xffffffff, "Dibujos perdidos: %d   tirones (>50 ms): %d", r->dropped, dbg_hitches);
    if (dbg_bat_pct >= 0)
        perf_line(158, dbg_bat_temp >= 45 ? C_BAD : 0xffffffff, "CPU %d MHz  bus %d MHz  bateria %d%% %d C%s", dbg_cpu_mhz,
                  dbg_bus_mhz, dbg_bat_pct, dbg_bat_temp, dbg_plugged ? " (cargando)" : "");
    else perf_line(158, 0xffffffff, "CPU %d MHz  bus %d MHz  sin bateria", dbg_cpu_mhz, dbg_bus_mhz);
    perf_line(176, 0xffc0c0c0, "SELECT: grafica de tiempos");
}

static void perf_graph(float cpu_ms, float fps)
{
    /* tiempo de cada fotograma (ultimos 128): verde < 16,7 ms (60 FPS), amarillo < 33,3 ms (30 FPS), rojo mas */
    const float x0 = 8, y0 = 30, w = 384, h = 100, scale = h / 66.6f;
    r_rect(0, 0, x0 * 2 + w, y0 + h + 40, 0xc0000000);
    perf_line(2, 0xffffffff, "TIEMPO POR FOTOGRAMA  FPS %.1f  CPU %.1f ms  tirones %d", fps, cpu_ms, dbg_hitches);
    for (int i = 0; i < 128; i++) {
        float ms = dbg_frame_hist[(dbg_hist_pos + i) & 127];
        float bh = ms * scale;
        if (bh > h) bh = h;
        unsigned int c = ms < 17.5f ? C_OK : ms < 34.0f ? C_WARN : C_BAD;
        r_rect(x0 + i * 3, y0 + h - bh, 2, bh, c);
    }
    r_rect(x0, y0 + h - 16.7f * scale, w, 1, 0x80ffffff);
    r_rect(x0, y0 + h - 33.3f * scale, w, 1, 0x80ffffff);
    ui_text(f_small, x0 + w - 2, y0 + h - 16.7f * scale - 12, 0.5f, 0xffc0c0c0, 2, "60 FPS");
    ui_text(f_small, x0 + w - 2, y0 + h - 33.3f * scale - 12, 0.5f, 0xffc0c0c0, 2, "30 FPS");
    perf_line(y0 + h + 6, 0xffc0c0c0, "SELECT: ocultar");
}

void perf_draw(int page, float cpu_ms, float gpu_ms, float fps)
{
    hud_fonts();
    r_begin_2d();
    if (page == 1) perf_summary(cpu_ms, gpu_ms, fps);
    else if (page == 2) perf_memory(fps);
    else if (page == 3) perf_graph(cpu_ms, fps);
}
