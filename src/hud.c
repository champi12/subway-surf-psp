/* hud.c - carga de ui.bin (UIAtlas / UIFont de NGUI) y dibujo del marcador y los menus */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <pspkernel.h>
#include "hud.h"
#include "render.h"
#include "data.h"
#include "game.h"
#include "missions.h"
#include "devlog.h"
#include "lang.h"
static void spr_c(const char *name, float cx, float cy, float w, float h, unsigned int col);

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

/* Fuentes pre-escaladas por el conversor ("Lilita@20" = Lilita a 20 px exactos). Para cada texto se usa la del
 * tamano mas cercano y se dibuja 1:1 en pixeles enteros: nitido en la pantalla de la PSP. */
static int baked_ready;
static struct { char family[16]; int px, fi; } baked[24];
static int nbaked;
static char font_family[48][16];

static void baked_init(void)
{
    if (baked_ready) return;
    baked_ready = 1;
    for (int i = 0; i < nfnt; i++) {
        const char *n = fnt[i].name;
        int k = 0;
        while (n[k] && ((n[k] >= 'A' && n[k] <= 'Z') || (n[k] >= 'a' && n[k] <= 'z')) && k < 15) { font_family[i][k] = n[k]; k++; }
        font_family[i][k] = 0;
        const char *at = strchr(n, '@');
        if (at && nbaked < 24) {
            snprintf(baked[nbaked].family, sizeof(baked[nbaked].family), "%s", font_family[i]);
            baked[nbaked].px = atoi(at + 1);
            baked[nbaked].fi = i;
            nbaked++;
        }
    }
}

#define MIN_TEXT_PX 14.0f

/* fuente y escala con las que se dibuja realmente; snap = a tamano exacto (1:1 y pixeles enteros) */
static void resolve_font(int *fi, float *scale, int *snap)
{
    *snap = 0;
    baked_init();
    if (*fi < 0 || strchr(fnt[*fi].name, '@')) return;
    float want = fnt[*fi].size * *scale;
    /* en la pantalla de la PSP, por debajo de 14 pixeles cuesta leer (el original escalaba para pantallas de movil) */
    if (want < MIN_TEXT_PX) { want = MIN_TEXT_PX; *scale = want / fnt[*fi].size; }
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < nbaked; i++) {
        if (strcmp(baked[i].family, font_family[*fi])) continue;
        float d = fabsf(baked[i].px - want);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return;
    float ratio = want / baked[best].px;
    if (ratio < 0.8f || ratio > 1.3f) {
        /* mucho mas grande o pequeno (cuenta atras animada): la mas cercana, escalada con filtro */
        *fi = baked[best].fi;
        *scale = ratio;
        return;
    }
    *fi = baked[best].fi;
    if (ratio > 0.88f && ratio < 1.14f) { *scale = 1.0f; *snap = 1; }
    else *scale = ratio;
}

/* siguiente caracter: los textos estan en UTF-8 y las letras del espanol (a con tilde, ene, abrir interrogacion...)
 * son de Latin-1: dos bytes C2/C3 xx -> 0x80-0xFF, que es su indice en la fuente */
static unsigned next_char(const char **ps)
{
    const unsigned char *p = (const unsigned char *)*ps;
    unsigned c = *p++;
    if ((c == 0xc2 || c == 0xc3) && (*p & 0xc0) == 0x80) c = ((c & 0x1f) << 6) | (*p++ & 0x3f);
    *ps = (const char *)p;
    return c;
}

/* si un tamano no tiene la letra con tilde (los grandes no la llevan), la letra sin tilde */
static UGlyph *glyph_of(UFont *t, unsigned c)
{
    UGlyph *g = t->map[c];
    if (g || c < 0xa1) return g;
    static const char fold[] = "!.......ca...r.o.........o....?AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";   /* 0xa1-0xff sin tilde */
    return t->map[(unsigned char)fold[c - 0xa1]];
}

static float text_width_raw(int fi, float scale, const char *s)
{
    UFont *t = &fnt[fi];
    float w = 0;
    while (*s) {
        unsigned c = next_char(&s);
        UGlyph *g = glyph_of(t, c);
        if (g) w += (g->adv + t->spacing) * scale;
        else if (c == ' ') w += t->size * 0.3f * scale;
    }
    return w;
}

float ui_text_width(int fi, float scale, const char *s)
{
    if (fi < 0) return strlen(s) * 8 * scale;
    int snap;
    resolve_font(&fi, &scale, &snap);
    return text_width_raw(fi, scale, s);
}

float ui_text(int fi, float x, float y, float scale, unsigned int col, int align, const char *s)
{
    if (fi < 0) return 0;
    int snap;
    resolve_font(&fi, &scale, &snap);
    UFont *t = &fnt[fi];
    if (t->atlas < 0) return 0;
    UAtlas *a = &atl[t->atlas];
    float w = text_width_raw(fi, scale, s);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    if (snap) { x = floorf(x + 0.5f); y = floorf(y + 0.5f); r_set_nearest(1); }
    /* todas las letras de la frase en una sola llamada de dibujo */
    static float q[64 * 8];
    int nq = 0;
    while (*s) {
        unsigned c = next_char(&s);
        UGlyph *g = glyph_of(t, c);
        if (!g) { if (c == ' ') x += t->size * 0.3f * scale; continue; }
        if (g->w > 0 && g->h > 0) {
            float *o = &q[nq * 8];
            o[0] = x + g->ox * scale; o[1] = y + g->oy * scale; o[2] = g->w * scale; o[3] = g->h * scale;
            o[4] = (t->sx + g->x) / a->w; o[5] = (t->sy + g->y) / a->h;
            o[6] = (t->sx + g->x + g->w) / a->w; o[7] = (t->sy + g->y + g->h) / a->h;
            if (++nq == 64) { r_sprites(a->tex, q, nq, col); nq = 0; }
        }
        x += (g->adv + t->spacing) * scale;
    }
    r_sprites(a->tex, q, nq, col);
    if (snap) r_set_nearest(0);
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
    /* a pixeles enteros: con medios pixeles la consola pintaba dos veces la columna donde se juntan dos trozos y en
     * los recuadros semitransparentes (el del multiplicador) quedaba una franja mas oscura */
    for (int k = 0; k < 4; k++) { xs[k] = floorf(xs[k] + 0.5f); ys[k] = floorf(ys[k] + 0.5f); }
    float us[4] = {s->ox, s->ix, s->ix + s->iw, s->ox + s->ow}, vs[4] = {s->oy, s->iy, s->iy + s->ih, s->oy + s->oh};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
            /* medio texel hacia dentro: estirado, el filtro bilineal de la consola leia el sprite de al lado del atlas
             * (una linea oscura en el recuadro del multiplicador). Los trozos de 1 texel, por su centro */
            float u0 = us[i], u1 = us[i + 1], v0 = vs[j], v1 = vs[j + 1];
            if (u1 - u0 > 1.0f) { u0 += 0.5f; u1 -= 0.5f; } else u0 = u1 = (u0 + u1) * 0.5f;
            if (v1 - v0 > 1.0f) { v0 += 0.5f; v1 -= 0.5f; } else v0 = v1 = (v0 + v1) * 0.5f;
            r_sprite(a->tex, xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], u0 / a->w, v0 / a->h,
                     u1 / a->w, v1 / a->h, col);
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

/* humo al romperse la tabla (hoverboardCrashParticleSystem del original, simulado en game.c): cada mancha de spray
 * (spraySplash) en su sitio del mundo, en perspectiva. MaxParticleSize del original: media pantalla como mucho */
static int fx_tex = -2;
static void draw_board_smoke(void)
{
    V3 p; float size; unsigned int col;
    if (fx_tex == -2) fx_tex = data_tex_find("fx_spray");
    if (fx_tex < 0) return;
    for (int i = 0; game_fx_get(i, &p, &size, &col); i++) {
        float sx, sy;
        if (!r_project(p, &sx, &sy)) continue;
        float sz = r_world_px(p, size);
        if (sz > SCR_H * 0.5f) sz = SCR_H * 0.5f;
        if (sz < 1) continue;
        r_sprite(fx_tex, sx - sz * 0.5f, sy - sz * 0.5f, sz, sz, 0, 0, 1, 1, col);
    }
}

static void draw_ingame(void)
{
    draw_board_smoke();
    /* avisos del tutorial (UIScreenController.QueueMessage) */
    if (ginfo.message && ginfo.time - ginfo.message_t < 2.5f && ginfo.time >= ginfo.message_t) {
        float a = clampf(2.5f - (ginfo.time - ginfo.message_t), 0, 1);
        text_shadow(f_big, SCR_W / 2, 62, 0.75f, ((unsigned)(a * 255) << 24) | 0x00ffffff, 1, ginfo.message);
    }
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
        /* SlideinPowerupHelper de la 1.7: Score Booster (L) y Mega Headstart (R) */
        if (ginfo.boosters > 0) {
            ui_sliced(a_ui, "background_ingame_transparent_withStroke", hx, hy, 60 * UIS, 74 * UIS, UIS, 0xc0000000);
            spr_c("icon_upgrades_doubleScore", hx + 30 * UIS, hy + 30 * UIS, 44 * UIS, 44 * UIS, 0xffffffff);
            text_shadow(f_titan, hx + 30 * UIS, hy + 56 * UIS, 22.0f / fnt[f_titan].size, 0xffffffff, 1, "+5");
            snprintf(buf, sizeof(buf), "L  x%d", ginfo.boosters);
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
    if (ginfo.paused && ginfo.countdown > 0) {
        /* "Starting in" 3, 2, 1 con el numero latiendo como en el original (la pausa en si es draw_pause) */
        text_shadow(f_titan, SCR_W / 2, SCR_H / 2 - 50, 30.0f / fnt[f_titan].size, 0xffffffff, 1, TR("Empieza en", "Starting in"));
        snprintf(buf, sizeof(buf), "%d", (int)ceilf(ginfo.countdown));
        float frac = ginfo.countdown - floorf(ginfo.countdown);
        float sc = 0.8f * ((1.0f - frac) * 0.5f + 1.0f);
        text_shadow(f_titan, SCR_W / 2, SCR_H / 2 - 20, sc, 0xffffffff, 1, buf);
    }
}

/* ------------------------------------------------------------ piezas de la interfaz de la 1.4
 * Las pantallas siguen los prefabs de la 1.4 (FrontUI, ShopFooter, UpgradeButton*, CharacterScreen, GameoverUI,
 * PauseUI, HalloweenThemePopup) pasados de 320x480 en vertical a 480x272 en horizontal: mismas piezas, tamanos y
 * colores, recolocadas. U() pasa unidades del movil a pixeles de la PSP. */
#define U(v) ((v) * UIS)
#define C_WHITE 0xffffffffu
#define C_NAVY 0xff542b0au        /* (0.04, 0.17, 0.33): textos sobre fondo claro */
#define C_SCORE 0xff804a00u       /* (0.00, 0.29, 0.50): "Score", "Missions", "Daily Challenge" */
#define C_NAME 0xff4f360du        /* (0.05, 0.21, 0.31): nombre del personaje */
#define C_LIMITED 0xffa67303u     /* (0.01, 0.45, 0.65): "Halloween Special" */
#define C_UPTITLE 0xff30ccedu     /* (0.93, 0.80, 0.19): titulo de las tarjetas de la tienda */
#define C_ORANGE 0xff0096f7u      /* (0.97, 0.59, 0.00): pegatinas */
#define C_OUTLINE 0xffd6d6d6u     /* contorno de los botones (0.84) */
#define C_EXPIRE 0xffbdedd6u      /* (0.84, 0.93, 0.74): tiempo que queda del evento */
#define C_LETTER_ON 0xff00dbffu   /* letra cogida */
#define C_LETTER_OFF 0xff3b3b3bu  /* letra que falta ([3B3B3B]) */
#define FOOT_Y 228.0f             /* centro de los botones del pie */

/* texto a un tamano en pixeles; titan = fuente Titan (titulos y numeros), si no Lilita. y = arriba del texto */
static float lab(int titan, float x, float y, float px, unsigned int col, int align, int shadow, const char *s)
{
    int f = titan ? f_titan : f_big;
    if (f < 0) return 0;
    float sc = px / fnt[f].size;
    if (shadow) ui_text(f, x + 1, y + 1, sc, (col >> 25) << 24, align, s);
    return ui_text(f, x, y, sc, col, align, s);
}
/* igual con y = centro vertical (pivote "Center" de NGUI) */
static float labc(int titan, float x, float cy, float px, unsigned int col, int align, int shadow, const char *s)
{
    return lab(titan, x, cy - px * 0.55f, px, col, align, shadow, s);
}
static float labw(int titan, float px, const char *s)
{
    int f = titan ? f_titan : f_big;
    return f < 0 ? 0 : ui_text_width(f, px / fnt[f].size, s);
}
/* texto que no puede pasar de un ancho: se reduce */
static void labfit(int titan, float x, float cy, float px, float maxw, unsigned int col, int align, int shadow, const char *s)
{
    float w = labw(titan, px, s);
    if (w > maxw && w > 0) px *= maxw / w;
    labc(titan, x, cy, px, col, align, shadow, s);
}

/* sprite centrado en (cx, cy) */
static void spr_c(const char *name, float cx, float cy, float w, float h, unsigned int col)
{
    ui_sprite_wh(a_ui, name, cx - w * 0.5f, cy - h * 0.5f, w, h, col);
}

/* boton cuadrado del pie (ShopFooter / GameoverUI / PauseUI): relleno 60x46, contorno 72x56, icono y titulo */
static void foot_btn(float nx, const char *fill, const char *icon, float iw, float ih, const char *title, const char *key)
{
    float cx = SCR_W * 0.5f + U(nx), cy = FOOT_Y;
    ui_sliced(a_ui, fill, cx - U(31), cy - U(22), U(60), U(46), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_2_outline", cx - U(34), cy - U(25), U(72), U(56), UIS, C_OUTLINE);
    spr_c(icon, cx, cy, U(iw), U(ih), C_WHITE);
    char t[48];
    if (key) snprintf(t, sizeof(t), "%s (%s)", title, key); else snprintf(t, sizeof(t), "%s", title);
    lab(0, cx, cy + U(30), 14, C_WHITE, 1, 1, t);
}

/* botones verdes de la seccion "ME" de la 1.5.2 (personajes, tablas, trofeos): button_fill_menuMe de la hoja de la
 * libreta; sin el APK de la 1.5.2, el azul de la 1.4 */
static int a_nb = -2;
/* BoardsNotificationHelper de la 1.6: circulo rojo con el numero de tablas nuevas sin ver */
static void new_badge(float x, float y, int n)
{
    char b[8];
    if (n <= 0) return;
    snprintf(b, sizeof(b), "%d", n);
    spr_c("background_sticker_medium", x, y, 22, 22, 0xff2a2ad8u);
    labc(0, x, y, 14, C_WHITE, 1, 1, b);
}

static void foot_btn_me(float nx, const char *icon, float iw, float ih, const char *title, const char *key)
{
    float cx = SCR_W * 0.5f + U(nx), cy = FOOT_Y;
    if (a_nb >= 0 && find_sprite(a_nb, "button_fill_menuMe")) {
        ui_sliced(a_nb, "button_fill_menuMe", cx - U(31), cy - U(22), U(60), U(46), UIS, C_WHITE);
        ui_sliced(a_ui, "button_generic_2_outline", cx - U(34), cy - U(25), U(72), U(56), UIS, C_OUTLINE);
        if (!strcmp(icon, "icon_boards")) ui_sprite_wh(a_nb, icon, cx - U(iw) * 0.5f, cy - U(ih) * 0.5f, U(iw), U(ih), C_WHITE);
        else spr_c(icon, cx, cy, U(iw), U(ih), C_WHITE);
        if (!strcmp(icon, "icon_boards") || !strcmp(icon, "icon_hoverboard")) new_badge(cx + U(28), cy - U(20), board_new_count());
        if (key && !strcmp(key, "[]")) {
            /* la tecla cuadrado: un cuadradito con borde en lugar de "[]" (la letra no tiene ese simbolo) */
            float tw = labw(0, 14, title), x0 = cx - (tw + 14) * 0.5f, ty = cy + U(30);
            lab(0, x0, ty, 14, C_WHITE, 0, 1, title);
            float qx = x0 + tw + 5, qy = ty + 3, q = 8;
            r_rect(qx, qy, q, 1.5f, C_WHITE); r_rect(qx, qy + q - 1.5f, q, 1.5f, C_WHITE);
            r_rect(qx, qy, 1.5f, q, C_WHITE); r_rect(qx + q - 1.5f, qy, 1.5f, q, C_WHITE);
            return;
        }
        char t[48];
        if (key) snprintf(t, sizeof(t), "%s (%s)", title, key); else snprintf(t, sizeof(t), "%s", title);
        lab(0, cx, cy + U(30), 14, C_WHITE, 1, 1, t);
        return;
    }
    foot_btn(nx, "button_fill_menuShop", icon, iw, ih, title, key);
}

/* boton verde ancho del pie ("PLAY", "RESUME") con las dos estrellas */
static void foot_play(float nx, const char *text, float px, const char *key)
{
    float cx = SCR_W * 0.5f + U(nx), cy = FOOT_Y;
    ui_sliced(a_ui, "button_fill_friendslogin", cx - U(70), cy - U(22), U(138), U(46), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_2_outline", cx - U(73), cy - U(25), U(150), U(56), UIS, C_OUTLINE);
    for (int k = 0; k < 2; k++) {
        float sx = cx + U(k ? 50 : -49);
        spr_c("icon_star", sx + U(1), cy + U(1.5f), U(20), U(19), 0xff17380au);   /* sombra (0.03, 0.22, 0.09) */
        spr_c("icon_star", sx, cy + U(0.5f), U(20), U(19), C_WHITE);
    }
    labfit(0, cx, cy, px, U(72), C_WHITE, 1, 1, text);
    if (key) lab(0, cx, cy + U(30), 14, C_WHITE, 1, 1, key);
}

/* CoinBoxQuick: monedas arriba a la derecha (sin el "+" de comprar monedas) */
static int coin_box_value = -1;   /* fin de partida: la hucha sube mientras cuentan las monedas */
static void coin_box(float xr, float cy)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", coin_box_value >= 0 ? coin_box_value : ginfo.total_coins);
    float tw = labw(0, 15, buf), w = tw + U(30);
    ui_sliced(a_ui, "button_gray_gradient", xr - w, cy - U(10), w, U(20), UIS, C_WHITE);
    spr_c("coin_with_background", xr - w, cy, U(26), U(25), C_WHITE);
    labc(0, xr - U(6), cy, 15, 0xfff7f7f7u, 2, 1, buf);
}

/* panel claro de las ventanas (HalloweenThemePopup: whitebox 0.93 + contorno) */
static void panel(float x, float y, float w, float h)
{
    ui_sliced(a_ui, "whitebox", x, y, w, h, UIS, 0xffededed);
    ui_sliced(a_ui, "button_generic_2_outline", x - U(3), y - U(3), w + U(8), h + U(9), UIS, C_OUTLINE);
}

/* ------------------------------------------------------------ fondo desenfocado (BackgroundHelper de la 1.4) */
static int bg_tex = -2, bg_loaded;
static int bg_screen(void)
{
    if (ginfo.paused) return ginfo.countdown <= 0;
    return ginfo.state == GS_SHOP || ginfo.state == GS_CHARS || ginfo.state == GS_GAMEOVER || ginfo.state == GS_TROPHIES ||
           ginfo.state == GS_BOARDS || (ginfo.state == GS_MENU && ginfo.missions_open);
}
/* NotebookPanel2 de la 1.5.2: hoja de libreta (newbackground, 9-slice), el dibujo gris de fondo (menu_decal8) y la
 * espiral metalica arriba (metal_spiral_lo repetida). Son ~40 sprites 2D de una hoja de 512x256 (128 KB, solo en los menus): no se nota
 * en el rendimiento. Sin el APK de la 1.5.2 se queda el fondo de la 1.4 */
static int nb_loaded;
#define NB_X0 8.0f
#define NB_Y0 6.0f
#define NB_X1 472.0f
#define NB_Y1 232.0f
static int notebook_panel(void)
{
    if (a_nb == -2) a_nb = ui_atlas("Notebook15");
    if (a_nb < 0 || !find_sprite(a_nb, "newbackground")) return 0;
    nb_loaded = 1;
    ui_sliced(a_nb, "newbackground", NB_X0, NB_Y0 + 6, NB_X1 - NB_X0, NB_Y1 - NB_Y0 - 6, 1.0f, C_WHITE);
    USprite *d = find_sprite(a_nb, "menu_decal8");
    if (d) {
        float h = (NB_Y1 - NB_Y0) * 0.92f, w = h * d->ow / d->oh;
        ui_sprite_wh(a_nb, "menu_decal8", (NB_X0 + NB_X1 - w) * 0.5f, NB_Y0 + 12, w, h, 0x80ffffffu);
    }
    USprite *sp = find_sprite(a_nb, "metal_spiral_lo");
    if (sp) {
        float h = 20, w = h * sp->ow / sp->oh, step = w * 1.75f;
        for (float x = NB_X0 + 14; x + w < NB_X1 - 10; x += step) ui_sprite_wh(a_nb, "metal_spiral_lo", x, NB_Y0 - 4, w, h, C_WHITE);
    }
    return 1;
}
static void nb_release(void)
{
    if (nb_loaded && !bg_screen()) { r_tex_unload(atl[a_nb].tex); nb_loaded = 0; }
}
static void notebook_bg(void);
int hud_background(void)
{
    if (bg_tex == -2) bg_tex = data_tex_find("ui_bg");
    if (bg_tex < 0 || !bg_screen()) return 0;
    r_background_extra = notebook_bg;   /* la hoja de libreta, debajo del personaje 3D */
    r_background(bg_tex);   /* tambien la deja cargada (una sola: la normal o la de Halloween, 64 KB) */
    bg_loaded = 1;
    return 1;
}
/* fuera de esas pantallas el fondo no ocupa memoria */
static void bg_release(void)
{
    if (bg_loaded && !bg_screen()) { r_tex_unload(bg_tex); bg_loaded = 0; }
}
static void notebook_bg(void) { notebook_panel(); }
static void bg_fallback(void)
{
    if (a_nb >= 0 && bg_tex >= 0) return;   /* la libreta ya va con el fondo (notebook_bg) */
    if (bg_tex < 0) r_rect(0, 0, SCR_W, SCR_H, 0xa0000000);   /* sin el APK de la 1.4: oscurecer el escenario */
}

/* ------------------------------------------------------------ avisos que bajan (UISlideInUnlock / SlideinMission) */
static char slide1[64], slide2[64];
static const char *slide_icon;
static float slide_t = -100;
static void hud_slide_icon(const char *line1, const char *line2, const char *icon)
{
    snprintf(slide1, sizeof(slide1), "%s", line1 ? line1 : "");
    snprintf(slide2, sizeof(slide2), "%s", line2 ? line2 : "");
    slide_icon = icon;
    slide_t = ginfo.time;
}
void hud_slide(const char *line1, const char *line2)
{
    slide_icon = NULL;
    snprintf(slide1, sizeof(slide1), "%s", line1 ? line1 : "");
    snprintf(slide2, sizeof(slide2), "%s", line2 ? line2 : "");
    slide_t = ginfo.time;
}

/* devuelve la parte de abajo del cuadro (para poner otro debajo si coinciden dos avisos) */
static float slide_box(float t, const char *l1, const char *l2, int tick, const char *icon, float top)
{
    if (t < 0 || t > 3.0f) return top;
    float k = t < 0.25f ? t / 0.25f : t > 2.7f ? (3.0f - t) / 0.3f : 1.0f;
    float w1 = labw(0, 18, l1), w2 = labw(0, tick ? 15 : 26, l2) + (tick ? U(30) : 0);
    float iw = icon ? 48 : 0;                   /* hueco a la izquierda para el objeto */
    float w = fmaxf(U(200), fmaxf(w1, w2) + U(24)) + iw, h = U(64);
    float x = (SCR_W - w) * 0.5f, y = top - h + (h + 4) * k, tx = SCR_W * 0.5f + iw * 0.5f;
    ui_sliced(a_ui, "background_ingame_popup", x, y, w, h, UIS, C_WHITE);
    labc(0, tx, y + U(19), 18, C_WHITE, 1, 1, l1);
    if (tick) {
        float lx = SCR_W * 0.5f - w2 * 0.5f;
        spr_c("icon_tick", lx + U(12), y + U(45), U(22), U(16), 0xff4a4a4au);
        spr_c("icon_tick", lx + U(11), y + U(44), U(22), U(16), 0xff4acc8cu);   /* (0.55, 0.80, 0.29) */
        labc(0, lx + U(30), y + U(44), 15, C_WHITE, 0, 1, l2);
    } else labc(0, tx, y + U(40), 26, C_WHITE, 1, 1, l2);
    if (icon) spr_c(icon, x + 8 + 20, y + h * 0.5f, 40, 40, C_WHITE);
    return y + h;
}

static void draw_slides(void)
{
    float top = 0;
    if (mission_msg[0]) top = slide_box(ginfo.time - mission_msg_t, mission_msg, TR("Misión completada", "Mission Complete"), 1, NULL, 0);
    slide_box(ginfo.time - slide_t, slide1, slide2, 0, slide_icon, top > 0 ? top + 2 : 0);
}

/* ------------------------------------------------------------ misiones (UIMissionHelper) */
static void draw_missions_panel(float x, float y, float w)
{
    char buf[64];
    labfit(1, x, y + 12, 24, w - U(124), C_SCORE, 0, 0, TR("Misiones", "Missions"));
    /* MissionPopup de la 1.5.2: "MISSION SET Number N" bajo el titulo */
    snprintf(buf, sizeof(buf), TR("SET DE MISIONES %d", "MISSION SET %d"), mission_set + 1);
    labc(0, x + 2, y + 31, 12, 0xff8a8a8au, 0, 0, buf);
    /* MissionProgress: barra de las misiones hechas y el multiplicador que se gana */
    int n = 0;
    for (int i = 0; i < 3; i++) n += missions_done(i);
    float px = x + w - U(118), py = y + U(14);
    ui_sliced(a_ui, "button_fill_friends_inactive_gray", px, py - U(13), U(114), U(26), UIS, 0xffbb8724u);
    ui_sliced(a_ui, "whitebox", px + U(4), py - U(8), U(64), U(16), UIS, 0xffcff0fau);
    if (n) ui_sliced(a_ui, "whitebox", px + U(4), py - U(8), U(64) * n / 3, U(16), UIS, 0xfff5cf8au);
    snprintf(buf, sizeof(buf), TR("%d%% HECHO", "%d %% DONE"), n * 100 / 3);
    labc(0, px + U(36), py, 14, C_NAVY, 1, 0, buf);
    spr_c("icon_small_superMysteryBox", px + U(96), py - U(2), U(28), U(26), C_WHITE);
    snprintf(buf, sizeof(buf), "x%d", missions_multiplier() + 1);
    labc(0, px + U(96), py + U(1), 15, 0xff00dbffu, 1, 1, buf);
    for (int i = 0; i < 3; i++) {
        float ry = y + U(54) + i * U(42);
        int done = missions_done(i);
        spr_c("checkbox_mission", x + U(17), ry, U(34), U(34), C_WHITE);
        if (done) spr_c("checkbox_checked_mission", x + U(21.5f), ry - U(2), U(41), U(34), C_WHITE);
        else {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            labc(1, x + U(17), ry, 20, 0xff543a08u, 1, 0, buf);
        }
        missions_text(i, buf, sizeof(buf));
        labfit(0, x + U(40), ry - (done ? 0 : U(7)), 16, w - U(44), C_NAVY, 0, 0, buf);
        if (!done) {
            snprintf(buf, sizeof(buf), "%d/%d", missions_progress(i), missions_goal(i));
            labc(0, x + U(40), ry + U(10), 14, C_LIMITED, 0, 0, buf);
        }
    }
    /* el pie del original: las misiones se pueden saltar en la tienda (Saltar mision 1-3) */
    labfit(0, x, y + U(54) + 3 * U(42) - U(12), 13, w - 4, 0xff8a8a8au, 0, 0,
           TR("Las misiones se pueden saltar en la tienda", "Missions can be Skipped in the Shop"));
}

/* ventana de misiones del menu (triangulo) */
static void challenges_page(int pause);
static int a_st = -2;   /* hoja Settings15: iconos de SettingsPopup y PrizeListPopup */

/* sprite de una hoja centrado en (cx, cy) y metido en maxw x maxh sin deformarlo */
static void spr_fit(int ai, const char *name, float cx, float cy, float maxw, float maxh, unsigned int col)
{
    USprite *sp = find_sprite(ai, name);
    if (!sp || sp->ow <= 0 || sp->oh <= 0) return;
    float w = maxw, h = maxw * sp->oh / sp->ow;
    if (h > maxh) { h = maxh; w = maxh * sp->ow / sp->oh; }
    ui_sprite_wh(ai, name, cx - w * 0.5f, cy - h * 0.5f, w, h, col);
}

/* texto partido por palabras en lineas que caben en maxw (hasta 3), centrado en cy. No se encoge: por debajo de 14
 * pixeles el texto se dibuja a 14 igualmente (resolve_font) */
static void lab2(int titan, float x, float cy, float px, float maxw, unsigned int col, const char *s)
{
    char ln[3][96];
    int nl = 0;
    const char *p = s;
    while (*p && nl < 3) {
        int best = -1, k = 0;
        char tmp[96];
        for (k = 0; p[k] && k < 95; k++) {
            if (p[k] != ' ') continue;
            memcpy(tmp, p, k); tmp[k] = 0;
            if (labw(titan, px, tmp) <= maxw) best = k; else break;
        }
        if (!p[k] && labw(titan, px, p) <= maxw) best = k;   /* lo que queda cabe entero */
        if (best <= 0 || nl == 2) best = (int)strlen(p) < 95 ? (int)strlen(p) : 95;
        memcpy(ln[nl], p, best); ln[nl][best] = 0;
        nl++;
        p += best;
        while (*p == ' ') p++;
    }
    float lh = px * 1.1f, y0 = cy - (nl - 1) * lh * 0.5f;
    for (int i = 0; i < nl; i++) labc(titan, x, y0 + i * lh, px, col, 0, 0, ln[i]);
}

/* MissionPopup de la 1.5.2 (triangulo en el menu): titulo, set, el premio (multiplicador) con su progreso y las tres
 * misiones. En horizontal: el premio a la izquierda y las misiones a la derecha */
static void draw_missions_popup(void)
{
    char buf[96];
    if (a_st == -2) a_st = ui_atlas("Settings15");
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float x = 14, y = 8, w = SCR_W - 28, h = SCR_H - 16;
    panel(x, y, w, h);
    float lx = x + 12, lw = 196;
    labc(1, lx + lw * 0.5f, y + 24, 30, C_NAVY, 1, 0, TR("Misiones", "Missions"));
    snprintf(buf, sizeof(buf), TR("SET DE MISIONES Nº %d", "MISSION SET Number %d"), mission_set + 1);
    labfit(1, lx + lw * 0.5f, y + 48, 15, lw, 0xffb57a1au, 1, 0, buf);
    /* el recuadro azul del premio, con su piquito abajo a la izquierda */
    float by = y + 62, bh = 124;
    ui_sliced(a_ui, "whitebox", lx, by, lw, bh, UIS, 0xffc48029u);
    spr_c("background_callout_triangle", lx + 26, by + bh + 4, 20, 14, 0xffc48029u);
    labc(1, lx + 8, by + 16, 17, 0xff00dbffu, 0, 1, TR("PREMIO", "REWARD"));
    labfit(1, lx + 8, by + 42, 20, lw * 0.55f, C_WHITE, 0, 1, TR("Multiplicador", "Score"));
    labfit(1, lx + 8, by + 66, 20, lw * 0.55f, C_WHITE, 0, 1, TR("de puntos", "Multiplier"));
    /* la mancha verde con el multiplicador que se gana */
    float sx = lx + lw - 46, sy = by + 50;
    spr_fit(a_st, "background_splat", sx, sy, 92, 92, 0xff00b866u);
    snprintf(buf, sizeof(buf), "x%d", missions_multiplier() + 1);
    labc(1, sx, sy - 8, 30, 0xff00dbffu, 1, 1, buf);
    labc(1, sx, sy + 16, 16, 0xff00dbffu, 1, 1, TR("Puntos", "Score"));
    /* progreso: cuantas de las tres estan hechas */
    int n = 0;
    for (int i = 0; i < 3; i++) n += missions_done(i);
    float px = lx + 10, pw = lw - 20, py = by + bh - 26;
    r_rect(px, py, pw, 18, 0xffd8f3fbu);
    if (n) r_rect(px, py, pw * n / 3, 18, 0xff8acff5u);
    snprintf(buf, sizeof(buf), TR("%d %%  HECHO", "%d %%  DONE"), n * 100 / 3);
    labc(1, px + pw * 0.5f, py + 9, 14, C_NAVY, 1, 0, buf);
    /* las tres misiones */
    float mx = lx + lw + 18, mw = x + w - 12 - mx;
    for (int i = 0; i < 3; i++) {
        float ry = y + 40 + i * 54;
        int done = missions_done(i);
        spr_c("checkbox_mission", mx + 17, ry, 34, 34, C_WHITE);
        if (done) spr_c("checkbox_checked_mission", mx + 21.5f, ry - 2, 41, 34, C_WHITE);
        else {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            labc(1, mx + 17, ry, 22, 0xff543a08u, 1, 0, buf);
        }
        char t[64];
        missions_text(i, t, sizeof(t));
        if (done) snprintf(buf, sizeof(buf), TR("%s. ¡Hecha!", "%s. Done!"), t);
        else snprintf(buf, sizeof(buf), TR("%s. Faltan %d", "%s. %d left"), t, missions_goal(i) - missions_progress(i));
        lab2(0, mx + 44, ry, 17, mw - 46, C_NAVY, buf);
    }
    lab2(0, mx, y + h - 36, 14, mw, C_NAVY, TR("Las misiones se pueden saltar en la tienda", "Missions can be Skipped in the Shop"));
    labc(0, x + w - 14, y + h - 12, 13, 0xff8a8a8au, 2, 0, TR("O: cerrar", "O: close"));
}

/* ------------------------------------------------------------ menu principal (FrontUI) */
static void top_button(float cx, float cy, float w, const char *label, const char *key)
{
    ui_sliced(a_ui, "background_ingame_button", cx - w * 0.5f, cy - U(22), w, U(44), UIS, C_WHITE);
    if (label) labfit(0, cx, cy - U(1.5f), 15, w - 8, C_WHITE, 1, 1, label);
    if (key) labc(0, cx, cy + U(30), 14, C_WHITE, 1, 1, key);
}

/* botones grandes de abajo (ShopButton / FriendsButton): relleno 138x46, contorno 150x56 */
static void big_button(float cx, float by, const char *fill)
{
    ui_sliced(a_ui, fill, cx - U(69), by - U(39) - U(23), U(138), U(46), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_2_outline", cx - U(73), by - U(37) - U(28), U(150), U(56), UIS, C_OUTLINE);
}

static void draw_halloween_popup(void);
static void draw_xmas_popup(void);
static void draw_boards_popup(void);
static void draw_ny_popup(void);
static void draw_rio_popup(void);

/* SnowEffect de la 1.5.2: nieve cayendo en la portada mientras dura la Navidad. 40 copos (sprites 2D del brillo
 * suave de la interfaz) que caen balanceandose; cada uno con su velocidad y tamano fijos (sin memoria ni CPU que notar) */
extern int theme_xmas;
static void draw_menu_snow(void)
{
    if (!theme_xmas) return;
    for (int i = 0; i < 40; i++) {
        unsigned int h = (unsigned int)i * 2654435761u;
        float sp = 18.0f + (h % 23), sz = 3.0f + (h >> 8) % 4, x0 = (float)((h >> 12) % SCR_W);
        float y = fmodf((float)((h >> 4) % SCR_H) + ginfo.time * sp, (float)SCR_H + 10.0f) - 5.0f;
        float x = x0 + sinf(ginfo.time * 0.8f + i) * 10.0f;
        spr_c("background_superglow", x, y, sz * 2.5f, sz * 2.5f, 0xd0ffffffu);
    }
}

static void draw_menu(void)
{
    char buf[48];
    draw_menu_snow();
    /* arriba a la izquierda: Misiones (triangulo) y el reto diario (cuadrado) */
    top_button(U(46), U(26), U(76), TR("Misiones", "Missions"), TR("triángulo", "triangle"));
    /* FrontUI de la 1.5.2: junto a Misiones el boton "Daily Challenge" (la palabra va en su ventana, no en la portada) */
    top_button(U(144), U(26), U(110), TR("Reto diario", "Daily Challenge"), TR("cuadrado", "square"));
    /* ajustes (SettingsPopup de la 1.5.2): START */
    top_button(SCR_W - U(150), U(26), U(76), TR("Ajustes", "Settings"), "START");
    /* arriba a la derecha: hoverboards (los trofeos estan en la seccion ME, como en la 1.5.2) */
    float hx = SCR_W - U(79), hy = U(26);
    top_button(hx, hy, U(44), NULL, NULL);
    ui_sprite_wh(a_ui, "icon_hoverboard", hx - U(16), hy - U(13), U(32), U(20), C_WHITE);
    snprintf(buf, sizeof(buf), "%d", ginfo.hoverboards);
    labc(0, hx + U(16), hy + U(8), 14, C_WHITE, 2, 1, buf);
    if (game_board_guideline()) {
        /* GuidelineButton de la 1.5.2: el "!" que late sobre el boton de tablas hasta abrir "Out of Boards?" (O) */
        float s = 1.0f + 0.25f * fabsf(sinf(ginfo.time * 5.0f));
        spr_c("background_sticker_medium", hx + U(20), hy - U(18), 22 * s, 22 * s, C_ORANGE);
        labc(1, hx + U(20), hy - U(18), 16 * s, C_WHITE, 1, 1, "!");
        labc(0, hx, hy + U(30), 14, C_WHITE, 1, 1, "O");
    }
    /* "Tap to play" que late */
    float a = 0.55f + 0.45f * sinf(ginfo.time * 4.0f);
    lab(1, SCR_W * 0.5f, SCR_H - U(132), 20, ((unsigned)(a * 255) << 24) | 0xffffff, 1, 1,
        TR("Pulsa X para jugar", "Press X to play"));
    /* FrontUI de la 1.5.2: abajo ME (verde: personajes, tablas y trofeos, L) y SHOP (azul: la tienda, R), cada uno con
     * su fondo y su icono grande (front_fill_* y front_icon_* de la hoja de la libreta) */
    if (a_nb == -2) a_nb = ui_atlas("Notebook15");
    if (a_nb >= 0 && find_sprite(a_nb, "front_icon_me")) {
        /* FRIENDS (amigos en linea) no existe en la PSP: solo ME y SHOP, en su sitio de la derecha */
        static const struct { const char *fill, *icon, *key; float x, w; } fb[2] = {
            {"front_fill_green", "front_icon_me", "L", 0.38f, 0.26f},
            {"front_fill_blue", "front_icon_shop", "R", 0.64f, 0.36f},
        };
        float y1 = SCR_H - 4, h = 52, x0 = 8, W = SCR_W - 16;
        for (int i = 0; i < 2; i++) {
            float bx = x0 + W * fb[i].x + 3, bw = W * fb[i].w - 6;
            ui_sliced(a_nb, fb[i].fill, bx, y1 - h, bw, h, 1.0f, C_WHITE);
            ui_sliced(a_ui, "button_generic_2_outline", bx - 3, y1 - h - 3, bw + 6, h + 6, UIS, C_OUTLINE);
            USprite *ic = find_sprite(a_nb, fb[i].icon);
            if (ic) {
                float ih = h + 16, iw = ih * ic->ow / ic->oh;
                if (iw > bw) { iw = bw; ih = iw * ic->oh / ic->ow; }
                ui_sprite_wh(a_nb, fb[i].icon, bx + (bw - iw) * 0.5f, y1 - ih, iw, ih, C_WHITE);
            }
            lab(0, bx + 6, y1 - h + 2, 14, C_WHITE, 0, 1, fb[i].key);
            if (i == 0) new_badge(bx + bw - 6, y1 - h + 4, board_new_count());   /* tablas nuevas, en ME */
        }
        if (ginfo.missions_open) draw_missions_popup();
        if (ginfo.popup == POPUP_HALLOWEEN) draw_halloween_popup();
        if (ginfo.popup == POPUP_XMAS) draw_xmas_popup();
        if (ginfo.popup == POPUP_BOARDS) draw_boards_popup();
        if (ginfo.popup == POPUP_NY) draw_ny_popup();
        if (ginfo.popup == POPUP_RIO) draw_rio_popup();
        return;
    }
    /* abajo a la derecha: SHOP (R), como en la 1.4 */
    float sx = SCR_W - U(83), by = SCR_H - U(5);
    big_button(sx, by, "button_fill_menuShop");
    ui_sprite_wh(a_ui, "icon_shop", sx - U(60), by - U(77), U(120), U(77), C_WHITE);
    lab(1, sx - U(66), by - U(86), 20, C_WHITE, 0, 1, "R");
    /* abajo a la izquierda (donde la 1.4 tiene FRIENDS): personajes (L) */
    float cx = U(83);
    big_button(cx, by, "button_fill_menuCrew");
    spr_c("icon_characters", cx - U(42), by - U(39), U(50), U(36), C_WHITE);
    labfit(1, cx + U(18), by - U(40), 20, U(84), C_WHITE, 1, 1, TR("Personajes", "Characters"));
    lab(1, cx - U(66), by - U(86), 20, C_WHITE, 0, 1, "L");
    if (ginfo.missions_open) draw_missions_popup();
    if (ginfo.popup == POPUP_HALLOWEEN) draw_halloween_popup();
    if (ginfo.popup == POPUP_XMAS) draw_xmas_popup();
    if (ginfo.popup == POPUP_BOARDS) draw_boards_popup();
    if (ginfo.popup == POPUP_NY) draw_ny_popup();
    if (ginfo.popup == POPUP_RIO) draw_rio_popup();
}

/* ChangelogThemePopup de la 1.5.2: el aviso del evento de Navidad (con Elf Tricky) al abrir el menu la primera vez
 * de la temporada. Misma disposicion que el de Halloween: bocadillo con el titulo, tres lineas y el boton */
static void draw_xmas_popup(void)
{
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float w = 444, h = 214, x = (SCR_W - w) * 0.5f, y = 18;
    panel(x, y, w, h);
    const unsigned C_XRED = 0xff2a2ad8u;
    float bx = x + 82, by = y + 64;
    ui_sliced(a_ui, "backgroud_callout_square", bx - U(80), by - U(62), U(160), U(124), UIS, C_XRED);
    spr_c("background_callout_triangle", bx + U(84), by + U(4), U(20), U(20), C_XRED);
    if (a_nb >= 0 && find_sprite(a_nb, "icon_xmas")) ui_sprite_wh(a_nb, "icon_xmas", bx + U(34), by - U(84), U(48), U(48), C_WHITE);
    labc(1, bx, by - U(32), 24, C_WHITE, 1, 1, TR("¡Felices", "Happy"));
    labfit(1, bx, by, 30, U(150), C_WHITE, 1, 1, TR("fiestas", "Holidays"));
    labc(1, bx, by + U(32), 24, C_WHITE, 1, 1, TR("a todos!", "Everyone!"));
    static const char *const lines[3][4] = {
        {"¡Consigue a Elf Tricky!", "Get Elf Tricky!", "Solo en Personajes por tiempo LIMITADO", "In Characters for a LIMITED time only"},
        {"¡La ciudad se llena de NIEVE!", "The city is covered in SNOW!", "Vías, trenes y guardia de Navidad", "Holiday tracks, trains and inspector"},
        {"¡Hasta el 1 de enero!", "Until January 1!", "Después vuelve el aspecto de siempre", "Then the usual look comes back"},
    };
    float lx = x + 178, ly = y + 22;
    for (int i = 0; i < 3; i++) {
        labfit(0, lx, ly + i * 46, 17, x + w - lx - 10, C_NAVY, 0, 0, TR(lines[i][0], lines[i][1]));
        labfit(0, lx + 10, ly + i * 46 + 18, 14, x + w - lx - 20, 0xffba9e5cu, 0, 0, TR(lines[i][2], lines[i][3]));
    }
    float cx = SCR_W * 0.5f, cy = y + h - 30;
    ui_sliced(a_ui, "button_green_buy", cx - U(118), cy - U(21), U(236), U(42), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_3_outline", cx - U(121), cy - U(23), U(246), U(52), UIS, C_WHITE);
    labc(0, cx, cy, 20, C_WHITE, 1, 1, "OK (X)");
}

/* TutorialHoverboardPopup de la 1.5.2 / 1.6: "Out of Boards?" (o "Need Help?" si nunca se ha usado una tabla), con
 * las 3 tablas de regalo. Misma disposicion que los avisos de temporada: bocadillo naranja con la tabla, tres lineas y
 * el boton verde */
static void draw_boards_popup(void)
{
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float w = 444, h = 214, x = (SCR_W - w) * 0.5f, y = 18;
    panel(x, y, w, h);
    int never = game_boards_used() == 0;
    float bx = x + 82, by = y + 64;
    ui_sliced(a_ui, "backgroud_callout_square", bx - U(80), by - U(62), U(160), U(124), UIS, C_ORANGE);
    spr_c("background_callout_triangle", bx + U(84), by + U(4), U(20), U(20), C_ORANGE);
    /* la nube con la tabla: el brillo azul de la interfaz y el icono de la tabla encima */
    spr_c("background_superglow", bx + U(56), by - U(66), U(64), U(52), 0xffe0a040u);
    ui_sprite_wh(a_ui, "icon_hoverboard", bx + U(56) - U(22), by - U(66) - U(14), U(44), U(28), C_WHITE);
    labc(1, bx, by - U(38), 24, C_WHITE, 1, 1, never ? TR("¿Necesitas", "Need") : TR("¿Sin", "Out of"));
    labfit(1, bx, by - U(10), 28, U(150), C_WHITE, 1, 1, never ? TR("ayuda?", "Help?") : TR("tablas?", "Boards?"));
    /* el subtitulo en dos lineas, como el original */
    labfit(0, bx, by + U(24), 15, U(150), C_WHITE, 1, 1, never ? TR("¡Usa las", "Use the") : TR("¡Aquí tienes 3", "Here's 3 to"));
    labfit(0, bx, by + U(42), 15, U(150), C_WHITE, 1, 1, never ? TR("TABLAS!", "HOVERBOARDS!") : TR("para empezar!", "get you going!"));
    const char *l[3][4] = {
        {"¡ESQUIVA los problemas!", "SURF AWAY from trouble!", "Las tablas te libran de chocar", "Boards save you from crashing"},
        {"¡Otra OPORTUNIDAD!", "Have another GO!", "Empieza volando con 3 tablas GRATIS", "Get a flying start with 3 FREE Boards"},
        {"¿Dónde consigo MÁS?", "Where can I get MORE?", "Compra tablas en la tienda", "Get Hoverboards in the Shop"},
    };
    if (never) {
        l[1][0] = "¿Cómo se ACTIVAN?"; l[1][1] = "How to ACTIVATE?";
        l[1][2] = "Pulsa cuadrado o triángulo al correr"; l[1][3] = "Press square or triangle while running";
    }
    float lx = x + 178, ly = y + 22;
    for (int i = 0; i < 3; i++) {
        labfit(0, lx, ly + i * 46, 17, x + w - lx - 10, C_NAVY, 0, 0, TR(l[i][0], l[i][1]));
        labfit(0, lx + 10, ly + i * 46 + 18, 14, x + w - lx - 20, 0xffba9e5cu, 0, 0, TR(l[i][2], l[i][3]));
    }
    float cx = SCR_W * 0.5f, cy = y + h - 30;
    ui_sliced(a_ui, "button_green_buy", cx - U(118), cy - U(21), U(236), U(42), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_3_outline", cx - U(121), cy - U(23), U(246), U(52), UIS, C_WHITE);
    ui_sprite_wh(a_ui, "icon_hoverboard", cx - U(108), cy - U(10), U(32), U(20), C_WHITE);
    labc(0, cx + U(10), cy, 20, C_WHITE, 1, 1, TR("¡3 gratis! (X)", "Get 3 Free! (X)"));
}

/* ChangelogPopup de la 1.6: "Welcome to NYC!" al empezar la temporada de Nueva York (Tony, 4 tablas nuevas). Misma
 * disposicion que el de Navidad, con el azul de la ciudad */
static void draw_ny_popup(void)
{
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float w = 444, h = 214, x = (SCR_W - w) * 0.5f, y = 18;
    panel(x, y, w, h);
    const unsigned C_NYBLUE = 0xffb86a1eu;
    float bx = x + 82, by = y + 64;
    ui_sliced(a_ui, "backgroud_callout_square", bx - U(80), by - U(62), U(160), U(124), UIS, C_NYBLUE);
    spr_c("background_callout_triangle", bx + U(84), by + U(4), U(20), U(20), C_NYBLUE);
    labc(1, bx, by - U(28), 26, C_WHITE, 1, 1, TR("¡Bienvenido", "Welcome"));
    labfit(1, bx, by + U(8), 26, U(150), 0xff3cd2ffu, 1, 1, TR("a Nueva York!", "to NYC!"));
    static const char *const lines[3][4] = {
        {"Subway Surfers World Tour", "Subway Surfers World Tour", "¡Explora el metro de Nueva York!", "Explore the New York City Subway!"},
        {"¡Conoce a Tony!", "Meet the cool kid, Tony!", "Disponible por tiempo LIMITADO", "Available for a LIMITED time"},
        {"¡Amplía tu colección de tablas!", "Boost your board collection!", "4 TABLAS NUEVAS en la pantalla de tablas", "4 NEW BOARDS in the Board Shop"},
    };
    float lx = x + 178, ly = y + 22;
    for (int i = 0; i < 3; i++) {
        labfit(0, lx, ly + i * 46, 17, x + w - lx - 10, C_NAVY, 0, 0, TR(lines[i][0], lines[i][1]));
        labfit(0, lx + 10, ly + i * 46 + 18, 14, x + w - lx - 20, 0xffba9e5cu, 0, 0, TR(lines[i][2], lines[i][3]));
    }
    float cx = SCR_W * 0.5f, cy = y + h - 30;
    ui_sliced(a_ui, "button_green_buy", cx - U(118), cy - U(21), U(236), U(42), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_3_outline", cx - U(121), cy - U(23), U(246), U(52), UIS, C_WHITE);
    labc(0, cx, cy, 20, C_WHITE, 1, 1, "OK (X)");
}

/* WelcomePopup de la 1.7: aviso de la temporada de Rio (Carmen, las tablas nuevas y el Score Booster). Misma
 * disposicion que los demas avisos de temporada, con el verde de Rio */
static void draw_rio_popup(void)
{
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float w = 444, h = 214, x = (SCR_W - w) * 0.5f, y = 18;
    panel(x, y, w, h);
    const unsigned C_RIO = 0xff3da12bu;
    float bx = x + 82, by = y + 64;
    ui_sliced(a_ui, "backgroud_callout_square", bx - U(80), by - U(62), U(160), U(124), UIS, C_RIO);
    spr_c("background_callout_triangle", bx + U(84), by + U(4), U(20), U(20), C_RIO);
    labc(1, bx, by - U(28), 26, C_WHITE, 1, 1, TR("¡Bienvenido", "Welcome"));
    labfit(1, bx, by + U(8), 26, U(150), 0xff3cd2ffu, 1, 1, TR("a Río!", "to Rio!"));
    static const char *const lines[3][4] = {
        {"Recorre el metro de Río", "Run through Rio's subway", "Nuevo mapa en febrero", "New map in February"},
        {"Conoce a Carmen", "Meet Carmen", "Solo por tiempo LIMITADO", "LIMITED time only"},
        {"Tablas y Score Booster nuevos", "New boards and Score Booster", "Toucan, Lumberjack y Daredevil", "Toucan, Lumberjack and Daredevil"},
    };
    float lx = x + 178, ly = y + 22;
    for (int i = 0; i < 3; i++) {
        labfit(0, lx, ly + i * 46, 17, x + w - lx - 10, C_NAVY, 0, 0, TR(lines[i][0], lines[i][1]));
        labfit(0, lx + 10, ly + i * 46 + 18, 14, x + w - lx - 20, 0xffba9e5cu, 0, 0, TR(lines[i][2], lines[i][3]));
    }
    float cx = SCR_W * 0.5f, cy = y + h - 30;
    ui_sliced(a_ui, "button_green_buy", cx - U(118), cy - U(21), U(236), U(42), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_3_outline", cx - U(121), cy - U(23), U(246), U(52), UIS, C_WHITE);
    labc(0, cx, cy, 20, C_WHITE, 1, 1, "OK (X)");
}

/* HalloweenThemePopup: "Happy Halloween Everyone!" la primera vez de cada temporada */
static void draw_halloween_popup(void)
{
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float w = 444, h = 214, x = (SCR_W - w) * 0.5f, y = 18;
    panel(x, y, w, h);
    /* bocadillo naranja con el titulo y los murcielagos */
    float bx = x + 82, by = y + 64;
    ui_sliced(a_ui, "backgroud_callout_square", bx - U(80), by - U(62), U(160), U(124), UIS, C_ORANGE);
    spr_c("background_callout_triangle", bx + U(84), by + U(4), U(20), U(20), C_ORANGE);
    spr_c("popup_cloud_bats", bx + U(56), by - U(70), U(91), U(80), C_WHITE);
    labc(1, bx, by - U(32), 24, 0xff69f0ffu, 1, 1, TR("¡Feliz", "Happy"));
    labfit(1, bx, by, 30, U(150), 0xff69f0ffu, 1, 1, "Halloween");
    labc(1, bx, by + U(32), 24, 0xff69f0ffu, 1, 1, TR("a todos!", "Everyone!"));
    static const char *const lines[3][4] = {
        {"¡Consigue a Zombie Jake ESPECIAL!", "Get SPECIAL Zombie Jake!", "Solo en la tienda por tiempo LIMITADO", "Only in the Shop for a LIMITED time"},
        {"¡Un aspecto ESPANTOSO!", "SPOOKTACULAR new look!", "¡Ponte a tono para la fiesta!", "Get in the MOOD for the holiday!"},
        {"¡Hasta el 4 de noviembre!", "Until November 4!", "Después vuelve el aspecto CLÁSICO", "Then the CLASSIC look comes back"},
    };
    float lx = x + 178, ly = y + 22;
    for (int i = 0; i < 3; i++) {
        labfit(0, lx, ly + i * 46, 17, x + w - lx - 10, C_NAVY, 0, 0, TR(lines[i][0], lines[i][1]));
        labfit(0, lx + 10, ly + i * 46 + 18, 14, x + w - lx - 20, 0xffba9e5cu, 0, 0, TR(lines[i][2], lines[i][3]));
    }
    float cx = SCR_W * 0.5f, cy = y + h - 30;
    ui_sliced(a_ui, "button_green_buy", cx - U(118), cy - U(21), U(236), U(42), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_3_outline", cx - U(121), cy - U(23), U(246), U(52), UIS, C_WHITE);
    labc(0, cx, cy, 20, C_WHITE, 1, 1, "OK (X)");
}

/* ------------------------------------------------------------ tienda (UpgradesUI_shop + UpgradeButton*) */
#define CARD_W 226.0f
#define CARD_H 50.0f
static void shop_card(int i, float x, float y, int sel)
{
    char buf[48];
    const float K = CARD_W / 290.0f;              /* la tarjeta del original mide 290 de ancho */
    float cx = x + CARD_W * 0.5f, cy = y + CARD_H * 0.5f;
    ui_sliced(a_ui, sel ? "button_fill_shopItem_green" : "button_fill_shopItem_gray", cx - 141 * K, y + 2, 278 * K, CARD_H - 4, K, C_WHITE);
    ui_sliced(a_ui, "button_generic_2_outline", cx - 145 * K, y - 1, 290 * K, CARD_H + 3, K, 0xffbfbfbfu);
    spr_c(shop_items[i].icon, cx - 115 * K, cy - 2, 35 * K * 1.15f, 35 * K * 1.15f, C_WHITE);
    labfit(0, cx - 93 * K, cy - 11, 18, 120, C_UPTITLE, 0, 1, TR(shop_items[i].name, shop_items[i].name_en));
    int am = shop_amount(i), pr = shop_price(i);
    /* precio arriba a la derecha */
    if (pr >= 0) snprintf(buf, sizeof(buf), "%d", pr); else snprintf(buf, sizeof(buf), "MAX");
    labc(0, cx + 108 * K, cy - 13, 14, pr >= 0 && ginfo.total_coins < pr ? 0xff8080ffu : C_WHITE, 2, 1, buf);
    spr_c("coin2d", cx + 122 * K, cy - 13, 15, 15, C_WHITE);
    /* lo que se tiene: niveles (UITierHelper), cantidad ("You have: 0") o la mision que se salta */
    float ly = cy + 9;
    if (shop_items[i].kind == 1) {
        for (int k = 0; k < 5; k++)
            ui_sprite_wh(a_ui, k < am ? "progressbar_bar_on" : "progressbar_bar_off", cx - 93 * K + k * 17, ly - 4, 15, 9, C_WHITE);
    } else if (shop_items[i].kind == 0 || shop_items[i].kind == 2) {
        snprintf(buf, sizeof(buf), TR("Tienes: %d", "You have: %d"), am);
        labc(0, cx - 93 * K, ly, 14, C_WHITE, 0, 1, buf);
    } else {
        if (am) snprintf(buf, sizeof(buf), "%s", TR("Hecha", "Done"));
        else snprintf(buf, sizeof(buf), "%d/%d", missions_progress(shop_items[i].idx), missions_goal(shop_items[i].idx));
        labc(0, cx - 93 * K, ly, 14, am ? 0xff80ff80u : C_WHITE, 0, 1, buf);
    }
    /* boton "Buy" en la marcada */
    if (sel && pr >= 0) {
        const char *b = TR("Comprar", "Buy");
        float bw = fmaxf(52 * K, labw(1, 15, b) + 12), bx = cx + 112 * K - bw, by = cy + 1;
        ui_sliced(a_ui, "button_green_buy", bx, by, bw, 21, K, C_WHITE);
        labc(1, bx + bw * 0.5f, by + 10.5f, 15, C_WHITE, 1, 1, b);
    }
}

static void draw_shop(void)
{
    bg_fallback();
    coin_box(SCR_W - 8, 15);
    lab(0, SCR_W - 150, 34, 13, C_WHITE, 2, 1, TR("Cuadrado: premios", "Square: prizes"));   /* PrizeListPopup */
    /* descripcion de la tarjeta marcada (Upgrades.cs) */
    char buf[64];
    if (shop_items[ginfo.shop_sel].kind == 3) {
        missions_text(shop_items[ginfo.shop_sel].idx, buf, sizeof(buf));
        labfit(0, 10, 15, 15, SCR_W - 120, C_WHITE, 0, 1, buf);
    } else labfit(0, 10, 15, 15, SCR_W - 120, C_WHITE, 0, 1, shop_desc(ginfo.shop_sel));
    /* tarjetas en dos columnas, tres filas a la vista */
    static int top;
    int row = ginfo.shop_sel / 2, rows = (shop_count + 1) / 2;
    if (row < top) top = row;
    if (row > top + 2) top = row - 2;
    if (top > rows - 3) top = rows - 3;
    if (top < 0) top = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 2; c++) {
            int i = (top + r) * 2 + c;
            if (i >= shop_count) continue;
            shop_card(i, c ? SCR_W * 0.5f + 4 : SCR_W * 0.5f - 4 - CARD_W, 34 + r * 56, i == ginfo.shop_sel);
        }
    if (top > 0) lab(0, SCR_W * 0.5f, 22, 14, C_WHITE, 1, 1, "...");
    if (top + 3 < rows) lab(0, SCR_W * 0.5f, 34 + 3 * 56 - 8, 14, C_WHITE, 1, 1, "...");
    /* pie: Menu, Mejoras (esta), Personajes */
    foot_btn(-100, "button_fill_menuBack", "icon_back_menu", 31, 26, TR("Menú", "Menu"), "O");
    foot_btn(0, "button_fill_menuShop", "icon_upgrades", 29, 26, TR("Mejoras", "Boosts"), NULL);
    foot_btn_me(100, "icon_characters", 39, 28, TR("Personajes", "Characters"), "L/R");
    if (ginfo.reward[0] && ginfo.time - ginfo.reward_t < 2.5f) hud_slide_icon(TR("Has conseguido", "You got"), ginfo.reward, ginfo.reward_icon), ginfo.reward[0] = 0;
}

/* ------------------------------------------------------------ personajes (CharacterScreen de la 1.4) */
static void fmt_time(char *buf, int n, int secs, int with_days)
{
    int d = secs / 86400, h = secs / 3600 % 24, m = secs / 60 % 60, s = secs % 60;
    if (with_days) snprintf(buf, n, TR("%d día%s %02d:%02d:%02d", "%d day%s %02d:%02d:%02d"), d, d == 1 ? "" : "s", h, m, s);
    else snprintf(buf, n, "%02d:%02d:%02d", h + d * 24, m, s);
}

/* carrusel de la 1.4 (UICharacterScreen): los personajes en pequeno, de pie y estaticos (miniaturas que el
 * conversor dibuja a partir de los modelos: 256x256 en T8, 64 KB, cargadas solo en esta pantalla). El elegido
 * en el centro a tamano completo; la marca de "tuyo"/"elegido" y la burbuja de edicion limitada como alli */
static int a_thumbs = -2, thumbs_loaded;
static void chars_carousel(int i, int nc)
{
    const float cx = 150, foot = 204, step = 56;
    char nm[40];
    spr_c("background_superglow", cx, foot - 30, 92, 92, 0x60ffffffu);
    for (int k = -2; k <= 2; k++) {
        if (nc < 5 && (k < -(nc - 1) / 2 || k > nc / 2)) continue;
        int j = ((i + k) % nc + nc) % nc;
        float sc = k ? 0.78f : 1.0f, w = 42 * sc, h = 64 * sc, x = cx + k * step;
        snprintf(nm, sizeof(nm), "thumb_%s", char_thumb(j));
        ui_sprite_wh(a_thumbs, nm, x - w * 0.5f, foot - h, w, h, k ? 0xffd8d8d8u : C_WHITE);
        thumbs_loaded = 1;
        if (char_is_selected(j)) spr_c("icon_character_selected", x + w * 0.5f - 4, foot - 6, 15, 15, C_WHITE);
        else if (char_unlocked(j)) spr_c("icon_character_owned", x + w * 0.5f - 4, foot - 6, 15, 15, C_WHITE);
        if (char_is_event(j) && !char_unlocked(j)) spr_c("icon_bubble_ltd", x, foot - h - 6, U(36), U(22), C_WHITE);
    }
    spr_c("button_arrow_left", cx - 2.5f * step - 4, foot - 26, 14, 20, C_WHITE);
    spr_c("button_arrow_right", cx + 2.5f * step + 4, foot - 26, 14, 20, C_WHITE);
}
static void thumbs_release(void)
{
    if (thumbs_loaded && ginfo.state != GS_CHARS) { r_tex_unload(atl[a_thumbs].tex); thumbs_loaded = 0; }
}

/* ------------------------------------------------------------ pantalla de tablas (BoardScreen de la 1.5.2)
 * como la de personajes: la tabla marcada con el personaje encima a la derecha (3D), nombre, efecto, boton de
 * comprar o elegir y el carrusel con las tablas en pequeno (miniaturas del conversor: 256x128, 32 KB, cargadas
 * solo aqui) */
static int a_bthumbs = -2, bthumbs_loaded;
static void draw_boards(void)
{
    char buf[48];
    bg_fallback();
    coin_box(SCR_W - 8, 15);
    if (a_bthumbs == -2) a_bthumbs = ui_atlas("BoardThumbs");
    int i = ginfo.board_sel, nb = board_count();
    lab(0, 14, 8, 30, C_NAME, 0, 0, board_name(i));
    int fx = board_effect(i);
    if (fx) lab(0, 16, 40, 15, C_LIMITED, 0, 0, fx == 1 ? TR("Súper salto", "Super Jump") : fx == 3 ? TR("Más velocidad (en pruebas)", "Speed Boost (test)")
                                                                     : TR("Va agachado", "Stays Low"));
    else if (board_is_season(i)) lab(0, 16, 40, 15, C_LIMITED, 0, 0, board_is_season(i) == 2 ? TR("Especial de Río", "Rio Special")
                                                                                             : TR("Especial de Nueva York", "New York Special"));
    if (board_is_gift(i) && !board_is_owned(i)) {
        /* BoardScreen: pegatina "FREE GIFT" mientras la de regalo no se ha reclamado */
        float sx = 14 + labw(0, 30, board_name(i)) + U(36), sy = 26;
        spr_c("background_sticker_medium", sx, sy, U(57), U(57), C_ORANGE);
        labfit(0, sx, sy - 6, 14, U(48), C_WHITE, 1, 0, TR("GRATIS", "FREE"));
        labfit(0, sx, sy + 7, 14, U(48), C_WHITE, 1, 0, TR("REGALO", "GIFT"));
    }
    /* boton de comprar / elegir (HoverboardBuyButton / HoverboardSelectButton) */
    float bx = U(100), by = 112;
    ui_sliced(a_ui, "background_character_buy", bx - U(77), by - U(26), U(154), U(52), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_2_outline", bx - U(81), by - U(30), U(166), U(64), UIS, C_OUTLINE);
    if (board_is_owned(i)) {
        labc(0, bx, by, 20, C_WHITE, 1, 1, board_is_selected(i) ? TR("ELEGIDA", "SELECTED") : TR("ELEGIR (X)", "SELECT (X)"));
    } else if (board_is_gift(i)) {
        labc(0, bx, by, 20, C_WHITE, 1, 1, TR("GRATIS (X)", "FREE (X)"));
    } else {
        snprintf(buf, sizeof(buf), "%d", board_price(i));
        float pw = labw(0, 20, buf), px = bx + U(12) - pw * 0.5f;
        spr_c("icon_coin_large", px - U(16), by, U(29), U(28), C_WHITE);
        labc(0, px, by, 20, ginfo.total_coins >= board_price(i) ? C_WHITE : 0xff8080ffu, 0, 1, buf);
    }
    /* carrusel: la marcada en el centro a tamano completo, dos a cada lado mas pequenas */
    const float cx = 150, cy = 172, step = 56;
    spr_c("background_superglow", cx, cy, 92, 92, 0x60ffffffu);
    for (int k = -2; k <= 2 && a_bthumbs >= 0; k++) {
        if (nb < 5 && (k < -(nb - 1) / 2 || k > nb / 2)) continue;
        int j = i;
        for (int m = 0; m < (k < 0 ? -k : k); m++) j = board_step(j, k < 0 ? -1 : 1);
        float sc = k ? 0.78f : 1.0f, w = 64 * sc, x = cx + k * step;
        snprintf(buf, sizeof(buf), "board_%d", j);
        USprite *sp = find_sprite(a_bthumbs, buf);
        if (sp) {
            r_sprite(atl[a_bthumbs].tex, x - w * 0.5f, cy - w * 0.5f, w, w, sp->ox / atl[a_bthumbs].w,
                     sp->oy / atl[a_bthumbs].h, (sp->ox + sp->ow) / atl[a_bthumbs].w, (sp->oy + sp->oh) / atl[a_bthumbs].h,
                     k ? 0xffd8d8d8u : C_WHITE);
            bthumbs_loaded = 1;
        }
        if (board_is_selected(j)) spr_c("icon_character_selected", x + w * 0.5f - 6, cy + w * 0.5f - 8, 15, 15, C_WHITE);
        else if (board_is_owned(j)) spr_c("icon_character_owned", x + w * 0.5f - 6, cy + w * 0.5f - 8, 15, 15, C_WHITE);
        else if (board_is_gift(j)) {
            spr_c("background_sticker_medium", x + w * 0.5f - 8, cy - w * 0.5f + 10, 30, 30, C_ORANGE);
            labfit(0, x + w * 0.5f - 8, cy - w * 0.5f + 10, 9, 24, C_WHITE, 1, 1, TR("GRATIS", "FREE"));
        }
        /* HoverboardOverlayHelper de la 1.6: NEW en las nuevas sin ver, LIMITED en la de temporada */
        if (!board_is_owned(j) && board_is_season(j)) {
            spr_c("background_sticker_medium", x + w * 0.5f - 8, cy - w * 0.5f + 10, 30, 30, C_ORANGE);
            labfit(0, x + w * 0.5f - 8, cy - w * 0.5f + 10, 9, 26, C_WHITE, 1, 1, TR("LÍMITE", "LIMITED"));
        } else if (board_is_new(j)) {
            spr_c("background_sticker_medium", x + w * 0.5f - 8, cy - w * 0.5f + 10, 30, 30, 0xff2a2ad8u);
            labfit(0, x + w * 0.5f - 8, cy - w * 0.5f + 10, 9, 26, C_WHITE, 1, 1, TR("NUEVA", "NEW"));
        }
    }
    spr_c("button_arrow_left", cx - 2.5f * step - 4, cy, 14, 20, C_WHITE);
    spr_c("button_arrow_right", cx + 2.5f * step + 4, cy, 14, 20, C_WHITE);
    /* pie de la seccion ME de la 1.5.2: Menu / Personajes / Tablas / Trofeos */
    foot_btn(-150, "button_fill_menuBack", "icon_back_menu", 31, 26, TR("Menú", "Menu"), "O");
    foot_btn_me(-50, "icon_characters", 39, 28, TR("Personajes", "Characters"), "L");
    foot_btn_me(50, (a_nb >= 0 && find_sprite(a_nb, "icon_boards")) ? "icon_boards" : "icon_hoverboard", 33, 31,
                TR("Tablas", "Boards"), NULL);
    foot_btn_me(150, "icon_trophies", 29, 29, TR("Trofeos", "Trophies"), "[]");
}
static void bthumbs_release(void)
{
    if (bthumbs_loaded && ginfo.state != GS_BOARDS) { r_tex_unload(atl[a_bthumbs].tex); bthumbs_loaded = 0; }
}

static void draw_chars(void)
{
    char buf[48];
    bg_fallback();
    coin_box(SCR_W - 8, 15);
    int i = ginfo.char_sel, nc = char_count(), un = char_unlocked(i);
    int limited = char_is_event(i), secs = season_seconds_left();
    /* nombre y "Halloween Special" arriba a la izquierda */
    float nw = lab(0, 14, 8, 30, C_NAME, 0, 0, char_name(i));
    if (limited) lab(0, 16, 40, 15, C_LIMITED, 0, 0, limited == 4 ? TR("Especial de Río", "Rio Special")
                                                   : limited == 3 ? TR("Especial de Nueva York", "New York Special")
                                                   : limited == 2 ? TR("Especial de Navidad", "Holiday Special")
                                                              : TR("Especial de Halloween", "Halloween Special"));
    if (limited && !un && secs > 0) {
        /* pegatina naranja "10 DAYS LEFT" / "LAST DAY" */
        float sx = 14 + nw + U(36), sy = 26;
        spr_c("background_sticker_medium", sx, sy, U(57), U(57), C_ORANGE);
        int d = secs / 86400;
        if (d < 1) { labc(0, sx, sy - 6, 14, C_WHITE, 1, 0, TR("ÚLTIMO", "LAST")); labc(0, sx, sy + 7, 14, C_WHITE, 1, 0, TR("DÍA", "DAY")); }
        else {
            snprintf(buf, sizeof(buf), TR("%d DÍA%s", "%d DAY%s"), d, d == 1 ? "" : "S");
            labfit(0, sx, sy - 6, 14, U(48), C_WHITE, 1, 0, buf);
            labc(0, sx, sy + 7, 14, C_WHITE, 1, 0, TR("QUEDAN", "LEFT"));
        }
    }
    /* boton de comprar / elegir (UICharacterBuyButton / UICharacterSelectButton) */
    float bx = U(100), by = 112;
    ui_sliced(a_ui, "background_character_buy", bx - U(77), by - U(26), U(154), U(52), UIS, C_WHITE);
    ui_sliced(a_ui, "button_generic_2_outline", bx - U(81), by - U(30), U(166), U(64), UIS, C_OUTLINE);
    if (un) {
        labc(0, bx, by, 20, C_WHITE, 1, 1, char_is_selected(i) ? TR("ELEGIDO", "SELECTED") : TR("ELEGIR (X)", "SELECT (X)"));
    } else if (char_unlock_type(i) == UL_COINS) {
        snprintf(buf, sizeof(buf), "%d", char_price(i));
        float pw = labw(0, 20, buf), px = bx + U(12) - pw * 0.5f;
        spr_c("icon_coin_large", px - U(16), by - (limited ? U(4) : 0), U(29), U(28), C_WHITE);
        labc(0, px, by - (limited ? U(4) : 0), 20, ginfo.total_coins >= char_price(i) ? C_WHITE : 0xff8080ffu, 0, 1, buf);
        if (limited) { fmt_time(buf, sizeof(buf), secs, 1); labc(0, bx, by + U(14), 14, C_EXPIRE, 1, 1, buf); }
    } else {
        /* "GET [ficha] FROM [caja]" y cuantas se tienen */
        float lx = bx - U(64);
        lx += lab(0, lx, by - U(16), 14, C_WHITE, 0, 1, TR("CONSIGUE", "GET")) + 4;
        if (char_token_sprite(i)) { spr_c(char_token_sprite(i), lx + U(17), by - U(9), U(35), U(26), C_WHITE); lx += U(36); }
        lx += lab(0, lx, by - U(16), 14, C_WHITE, 0, 1, TR("EN", "FROM")) + 4;
        spr_c("icon_mysteryBox_small", lx + U(14), by - U(9), U(28), U(27), C_WHITE);
        snprintf(buf, sizeof(buf), "%d/%d", char_have(i), char_price(i));
        labc(0, bx, by + U(11), 15, C_WHITE, 1, 1, buf);
    }
    if (a_thumbs >= 0) chars_carousel(i, nc);
    else for (int k = -2; k <= 2; k++) {   /* sin el APK de la 1.4: etiquetas con el nombre */
        int j = ((i + k) % nc + nc) % nc;
        float w = k ? 84 : 100, h = k ? 22 : 26, x = SCR_W * 0.5f + k * 92 - w * 0.5f, y = 186 - h * 0.5f;
        ui_sliced(a_ui, k ? "button_fill_shopItem_gray" : "button_fill_shopItem_green", x, y, w, h, 0.5f, k ? 0xd0ffffffu : C_WHITE);
        float tx = x + w * 0.5f;
        if (char_is_selected(j)) { spr_c("icon_character_selected", x + 9, y + h * 0.5f, 15, 15, C_WHITE); tx += 6; }
        else if (char_unlocked(j)) { spr_c("icon_character_owned", x + 9, y + h * 0.5f, 15, 15, C_WHITE); tx += 6; }
        labfit(0, tx, y + h * 0.5f, k ? 14 : 16, w - 24, char_unlocked(j) ? C_WHITE : 0xffd0d0d0u, 1, 1, char_name(j));
        if (char_is_event(j) && !char_unlocked(j)) spr_c("icon_bubble_ltd", x + w - 8, y - 6, U(36), U(22), C_WHITE);
    }
    if (a_thumbs < 0) {
        lab(0, 8, 178, 14, C_WHITE, 0, 1, "<");
        lab(0, SCR_W - 8, 178, 14, C_WHITE, 2, 1, ">");
    }
    /* pie de la seccion ME de la 1.5.2: Menu / Personajes / Tablas / Trofeos (los trofeos van aqui, no en la portada) */
    foot_btn(-150, "button_fill_menuBack", "icon_back_menu", 31, 26, TR("Menú", "Menu"), "O");
    foot_btn_me(-50, "icon_characters", 39, 28, TR("Personajes", "Characters"), NULL);
    if (board_count() > 0)
        foot_btn_me(50, (a_nb >= 0 && find_sprite(a_nb, "icon_boards")) ? "icon_boards" : "icon_hoverboard", 33, 31,
                    TR("Tablas", "Boards"), "R");
    foot_btn_me(150, "icon_trophies", 29, 29, TR("Trofeos", "Trophies"), "[]");
}

/* las cajas y lo que sale de ellas: los modelos 3D de la 1.4 (MysteryBoxHandler) que el conversor dibuja una vez,
 * los premios en celdas de 72x72 y las cajas (cerrada y con la tapa levantada) de 88x88. Hoja de 512x256 (128 KB)
 * cargada solo mientras se abren cajas. Se dibujan 1:1 o mas pequenos, nunca estirados. Sin el APK de la 1.4, el
 * sprite pequeno de la interfaz con su forma, como mucho a 1,5x */
static int a_rewards = -2, rewards_loaded;
/* w < 0: dado la vuelta (el reves del premio mientras gira) */
static void reward_spr(const char *name, const char *fallback, float cx, float cy, float k, float wk, unsigned int col)
{
    USprite *s = find_sprite(a_rewards, name);
    if (s) {
        UAtlas *a = &atl[a_rewards];
        float w = s->ow * k * wk, h = s->oh * k;
        float u0 = s->ox / a->w, u1 = (s->ox + s->ow) / a->w;
        if (w < 0) { float t = u0; u0 = u1; u1 = t; w = -w; }
        r_sprite(a->tex, (int)(cx - w * 0.5f), (int)(cy - h * 0.5f), w, h, u0, s->oy / a->h, u1, (s->oy + s->oh) / a->h, col);
        rewards_loaded = 1;
        return;
    }
    s = find_sprite(a_ui, fallback);
    if (!s) return;
    float f = 1.5f;
    if (s->ow * f > 80) f = 80 / s->ow;
    if (s->oh * f > 80) f = 80 / s->oh;
    float w = s->ow * f * k * wk;
    spr_c(fallback, cx, cy, w < 0 ? -w : w, s->oh * f * k, col);
}

/* premio girando en 3D (como MysteryBoxHandler): 12 vistas del modelo, cada 30 grados, en "rw3d_<sprite>" (256x256,
 * 64 KB). Solo esta cargada la del premio que se ve; sin ella, el dibujo plano estrechandose (el giro de antes) */
static int spin_tex = -1;
static char spin_name[40];
static void reward_spin_release(void)
{
    if (spin_tex >= 0) { r_wait_idle(); r_tex_unload(spin_tex); }
    spin_tex = -1;
    spin_name[0] = 0;
}
static int reward_spin(const char *name, float cx, float cy, float k, float ang, unsigned int col)
{
    if (strcmp(name, spin_name)) {
        char tn[40];
        reward_spin_release();
        snprintf(spin_name, sizeof(spin_name), "%s", name);
        /* las monedas pocas o muchas son el mismo modelo (rewardCoins): su hoja va con el nombre de la bolsa grande */
        snprintf(tn, sizeof(tn), "rw3d_%s", !strcmp(name, "icon_coin_large") ? "Icon_coin_pouch_larger" : name);
        tn[31] = 0;
        spin_tex = data_tex_find(tn);
    }
    if (spin_tex < 0) return 0;
    float a = fmodf(ang, 6.2831853f);
    if (a < 0) a += 6.2831853f;
    int fr = (int)(a / 6.2831853f * 12.0f + 0.5f) % 12;
    float sz = 64.0f * 1.2f * k;
    r_sprite(spin_tex, (int)(cx - sz * 0.5f), (int)(cy - sz * 0.5f), sz, sz,
             (fr % 4) * 0.25f, (fr / 4) * 0.25f, (fr % 4) * 0.25f + 0.25f, (fr / 4) * 0.25f + 0.25f, col);
    return 1;
}

static float ease_cos(float f) { f = f < 0 ? 0 : f > 1 ? 1 : f; return 0.5f - 0.5f * cosf(f * 3.14159265f); }

/* MysteryBoxHandler._ShowReward: la tapa se levanta (clip "up"), la caja se va hacia abajo (0,7 s), el premio
 * crece desde la caja (2 s) dando vueltas que se frenan (4 s), el brillo aparece a los 0,5 s y el texto a 1,8 s */
/* MysteryBoxPopup de la 1.5.2: remolino de colores a pantalla completa detras de la caja (mysterybox_bg, 256x256
 * 5650 = 128 KB; solo mientras se abren cajas) */
static int mbox_bg = -2, mbox_loaded;
static void mbox_bg_release(int showing)
{
    if (mbox_loaded && !showing) { r_tex_unload(mbox_bg); mbox_loaded = 0; }
}
/* las cajas de 112 px con la tapa abriendose en 4 pasos (clip "up" del original, de 0 a 0,46 s): hoja BoxFrames
 * (128 KB) cargada solo mientras se abren cajas. Sin ella, los dibujos de 88 px de antes */
static int a_boxes = -2, boxes_loaded;
static void boxes_release(void)
{
    if (boxes_loaded && a_boxes >= 0) { r_wait_idle(); r_tex_unload(atl[a_boxes].tex); }
    boxes_loaded = 0;
}
static int box_frame(int sup, int f, float cx, float cy)
{
    if (a_boxes == -2) a_boxes = ui_atlas("BoxFrames");
    char nm[32];
    snprintf(nm, sizeof(nm), "%s_f%d", sup ? "box_super" : "box_mystery", f < 0 ? 0 : f > 3 ? 3 : f);
    USprite *s = find_sprite(a_boxes, nm);
    if (!s) return 0;
    UAtlas *a = &atl[a_boxes];
    r_sprite(a->tex, (int)(cx - s->ow * 0.5f), (int)(cy - s->oh * 0.5f), s->ow, s->oh, s->ox / a->w, s->oy / a->h,
             (s->ox + s->ow) / a->w, (s->oy + s->oh) / a->h, C_WHITE);
    boxes_loaded = 1;
    return 1;
}

static void draw_box_popup(int sup)
{
    char buf[48];
    float bx = SCR_W * 0.5f, cy = 112;
    if (mbox_bg == -2) mbox_bg = data_tex_find("ui_mbox_bg");
    if (mbox_bg >= 0) {
        /* girando despacio, como el fondo del original: se dibuja algo mas grande que la pantalla */
        r_sprite(mbox_bg, 0, 0, SCR_W, SCR_H, 0.06f, 0.2f, 0.94f, 0.8f, C_WHITE);
        mbox_loaded = 1;
    }
    const char *box = sup ? "box_super" : "box_mystery", *box_open = sup ? "box_super_open" : "box_mystery_open";
    const char *old = sup ? "icon_bigSuperMysteryBox" : "icon_upgrades_mysteryBox";
    if (sup) labc(1, bx, 26, 26, 0xff00dbffu, 1, 1, TR("Súper caja misteriosa", "Super Mystery Box"));
    if (!ginfo.reward[0]) {
        /* esperando: la caja flota arriba y abajo (BoxIdleAnimCoroutine) */
        if (!box_frame(sup, 0, bx, cy + sinf(ginfo.time * 2.0f) * 5.0f))
            reward_spr(box, old, bx, cy + sinf(ginfo.time * 2.0f) * 5.0f, 1, 1, C_WHITE);
        labc(1, bx, 186, 30, C_WHITE, 1, 1, TR("abrir (X)", "open (X)"));
        snprintf(buf, sizeof(buf), TR("Cajas: %d", "Boxes: %d"), ginfo.boxes_pending + ginfo.sboxes_pending);
        labc(0, bx, 216, 15, C_WHITE, 1, 1, buf);
        /* MysteryBoxPopup de la 1.5.2: "View Prizes" (boton naranja con la caja) arriba a la izquierda y el contador
         * de monedas arriba a la derecha */
        {
            float px = 12, py = SCR_H - 52, pw = 150, ph = 40;   /* abajo: arriba esta el titulo de la caja */
            ui_sliced(a_ui, "whitebox", px, py, pw, ph, UIS, 0xff1f8df5u);   /* naranja (button_orange_buy) */
            ui_sliced(a_ui, "button_generic_3_outline", px - 3, py - 2, pw + 6, ph + 8, UIS, C_WHITE);
            if (a_st == -2) a_st = ui_atlas("Settings15");
            spr_fit(a_st, "icon_small_mysteryBox", px + 20, py + ph * 0.5f, 26, 24, C_WHITE);
            labfit(0, px + 38, py + ph * 0.5f - 6, 16, pw - 44, C_WHITE, 0, 1, TR("Ver premios", "View Prizes"));
            labc(0, px + 38, py + ph * 0.5f + 10, 12, C_WHITE, 0, 1, TR("cuadrado", "square"));
        }
        coin_box(SCR_W - 8, 15);
        return;
    }
    float t = ginfo.time - ginfo.reward_t;
    /* MysteryBoxPopup._ShowReward de la 1.5.2: la tapa sube (clip "up") y a la mitad (T0) el premio sale creciendo
     * (2 s), la caja se va hacia abajo (0,7 s), el premio gira 1500 grados frenando (4 s) y a los 1,8 s sale el texto
     * (igual para la caja normal y la super) */
    const float T0 = 0.46f;
    /* la caja: la tapa subiendo hasta T0 y luego fuera de la pantalla hacia abajo (MoveGameObject, 0,7 s) */
    if (t < T0 + 0.7f) {
        float y = cy + (t > T0 ? ease_cos((t - T0) / 0.7f) * 200 : 0);
        int f = (int)(t / T0 * 3.0f + 0.5f);
        if (!box_frame(sup, f, bx, y)) reward_spr(t < 0.12f ? box : box_open, old, bx, y, 1, 1, C_WHITE);
    }
    /* el premio: crece de 1/4 a su tamano en 2 s y gira 1500 grados en 4 s, frenando */
    if (t >= T0 && ginfo.reward_icon) {
        float k = 0.25f + 0.75f * ease_cos((t - T0) / 2.0f);
        float fr = (t - T0) / 4.0f;
        if (fr > 1) fr = 1;
        float ang = 26.179939f * (cosf(4.712389f + fr * 1.5707963f) * 0.5f + 0.5f);     /* 1500 grados */
        float wk = cosf(ang);
        if (wk > -0.1f && wk < 0.1f) wk = wk < 0 ? -0.1f : 0.1f;       /* de canto: una raya, no desaparece */
        if (!reward_spin(ginfo.reward_icon, bx, cy - 4, k, ang, C_WHITE))
            reward_spr(ginfo.reward_icon, ginfo.reward_icon, bx, cy - 4, k, wk, C_WHITE);
    }
    if (t >= REWARD_REVEAL) {
        unsigned a = t < REWARD_REVEAL + 0.2f ? (unsigned)(255 * (t - REWARD_REVEAL) / 0.2f) : 255;
        labc(0, bx, 178, 18, (a << 24) | 0xffffffu, 1, 1, TR("Has conseguido", "You got"));
        if (ginfo.reward_trophy) labc(1, bx, 56, 26, (a << 24) | 0x00dbffu, 1, 1, TR("¡NUEVO TROFEO!", "NEW TROPHY!"));
        labfit(1, bx, 204, 30, SCR_W - 40, (a << 24) | 0x00dbffu, 1, 1, ginfo.reward);
        if (t >= REWARD_REVEAL + 0.5f) labc(0, bx, 240, 15, C_WHITE, 1, 1, TR("X continuar", "X continue"));
    }
}
static void rewards_release(void)
{
    if (rewards_loaded && !(ginfo.state == GS_GAMEOVER && (ginfo.reward[0] || ginfo.boxes_pending > 0 || ginfo.sboxes_pending > 0))) {
        r_tex_unload(atl[a_rewards].tex);
        rewards_loaded = 0;
    }
}

/* ------------------------------------------------------------ fin de la carrera (GameoverUI de la 1.4) */
static void draw_gameover(void)
{
    char buf[64];
    bg_fallback();
    /* CountUpCoins: puntos, monedas de la carrera y hucha pasan de "antes" a "despues" */
    float cf = gameover_count();
    int c0 = ginfo.gameover_coins;
    coin_box_value = (int)lroundf(ginfo.total_coins - c0 + c0 * cf);
    coin_box(SCR_W - 8, 15);
    coin_box_value = -1;
    /* PrizeListPopup: debajo del marcador (arriba tapaba el titulo "Puntos") */
    if (!ginfo.boxes_pending && !ginfo.sboxes_pending && !ginfo.reward[0])
        labc(0, 350, 172, 13, C_NAVY, 1, 1, TR("Cuadrado: premios", "Square: prizes"));
    /* GameoverUI de la 1.5.2: el personaje de pie a la izquierda (lo dibuja el 3D) y a la derecha el marcador:
     * "* Score *", la pizarra con los puntos y las monedas, y el record */
    float cx = 350, cy = 104;
    float stw = labw(1, 30, TR("Puntos", "Score")) * 0.5f + U(14);
    spr_c("icon_star", cx - stw, cy - U(72), U(20), U(19), C_SCORE);
    spr_c("icon_star", cx + stw, cy - U(72), U(20), U(19), C_SCORE);
    labc(1, cx, cy - U(72), 30, C_SCORE, 1, 0, TR("Puntos", "Score"));
    spr_c("scoreboard", cx, cy, U(160) * 1.15f, U(121) * 1.15f, C_WHITE);
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(ginfo.gameover_score0 + (ginfo.final_score - ginfo.gameover_score0) * cf));
    labfit(1, cx, cy - U(34), 30, U(150), C_WHITE, 1, 1, buf);
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(c0 * (1 - cf)));
    float tw = labw(0, 20, buf);
    spr_c("coin2d", cx - tw * 0.5f - U(12), cy + U(4), U(18), U(18), C_WHITE);
    labc(0, cx + U(6), cy + U(4), 20, C_WHITE, 1, 1, buf);
    snprintf(buf, sizeof(buf), TR("Récord: %d", "Best: %d"), ginfo.highscore);
    labfit(0, cx, cy + U(40), 15, U(150), C_WHITE, 1, 1, buf);
    foot_btn(-132, "button_fill_menuBack", "icon_house_menu", 32, 32, TR("Inicio", "Home"), "O");
    foot_btn(-46, "button_fill_menuShop", "icon_upgrades", 29, 26, TR("Mejoras", "Boosts"), "R");
    foot_play(74, TR("JUGAR", "PLAY"), 28, "X");
    /* cajas misteriosas de la carrera (MysteryBoxPopup): se abren una a una con X */
    if (ginfo.boxes_pending > 0 || ginfo.sboxes_pending > 0 || ginfo.reward[0]) {
        if (mbox_bg == -2) mbox_bg = data_tex_find("ui_mbox_bg");
        if (mbox_bg < 0) r_rect(0, 0, SCR_W, SCR_H, 0xa0000000);   /* sin el APK de la 1.5.2 */
        /* super caja (SuperMysteryBoxEffect): la plateada con mas brillo; se abren antes que las normales */
        draw_box_popup(ginfo.reward[0] ? ginfo.reward_super : ginfo.sboxes_pending > 0);
    } else { mbox_bg_release(0); reward_spin_release(); boxes_release(); }
}

/* ------------------------------------------------------------ pausa (PauseUI de la 1.4) */
/* pagina de la libreta con el desafio diario y las misiones: la de la pausa y la que abre "Misiones" en la portada
 * (Mission_popup / DailyChallengePopup de la 1.5.2 son la misma hoja) */
static void challenges_page(int pause)
{
    char buf[48];
    bg_fallback();
    /* izquierda: desafio diario (la palabra del dia) */
    float x = 14, y = 12;
    lab(1, x, y, 30, C_SCORE, 0, 0, TR("Palabra del día", "Daily Challenge"));
    const char *w = daily_word();
    unsigned m = daily_word_mask();
    float lbx = x + U(85), lby = y + 62;
    ui_sliced(a_ui, "whitebox", lbx - U(82), lby - U(22), U(160), U(48), UIS, 0xff5e5f61u);
    ui_sliced(a_ui, "button_generic_2_outline", lbx - U(85), lby - U(30), U(170), U(60), UIS, C_OUTLINE);
    {
        char ch[2] = {0, 0};
        float tw = 0;
        for (int i = 0; w[i]; i++) { ch[0] = w[i]; tw += labw(1, 26, ch) + 5; }
        float cx = lbx - tw * 0.5f;
        for (int i = 0; w[i]; i++) {
            ch[0] = w[i];
            lab(1, cx + 1, lby - 13, 26, 0xff727575u, 0, 0, ch);
            lab(1, cx, lby - 15, 26, (m >> i & 1) ? C_LETTER_ON : C_LETTER_OFF, 0, 0, ch);
            cx += labw(1, 26, ch) + 5;
        }
    }
    {
        /* premio: monedas segun la racha, o la super caja desde el quinto dia (DailyWord.REWARDS) */
        int pz = daily_prize_next();
        float rx = 203, ry = lby + 48;
        spr_c("background_splat", rx, ry, U(96), U(98), 0xff00c985u);
        if (pz > 0) {
            spr_c("Icon_coin_pouch_larger", rx, ry - U(10), U(39), U(53), C_WHITE);
            snprintf(buf, sizeof(buf), "%d", pz);
            spr_c("coin2d", rx - U(14), ry + U(26), U(16), U(16), C_WHITE);
            labc(0, rx + U(6), ry + U(26), 15, C_WHITE, 1, 1, buf);
        } else spr_c("icon_bigSuperMysteryBox", rx, ry, U(66), U(62), C_WHITE);
        snprintf(buf, sizeof(buf), TR("Día %d", "Day %d"), daily_day_next());
        labc(0, rx, ry - U(48), 15, a_nb >= 0 ? C_NAVY : C_WHITE, 1, a_nb < 0, buf);
    }
    if (daily_word_complete()) {
        lab(0, x, lby + 34, 17, a_nb >= 0 ? C_NAVY : C_WHITE, 0, a_nb < 0, TR("Siguiente desafío en", "Next challenge in"));
        fmt_time(buf, sizeof(buf), day_seconds_left(), 0);
        lab(1, x, lby + 54, 26, a_nb >= 0 ? C_NAVY : C_WHITE, 0, a_nb < 0, buf);
    } else {
        lab(0, x, lby + 34, 17, a_nb >= 0 ? C_NAVY : C_WHITE, 0, a_nb < 0, TR("Recoge las letras", "Collect Letters"));
        char t[24];
        fmt_time(t, sizeof(t), day_seconds_left(), 0);
        snprintf(buf, sizeof(buf), TR("Tiempo: %s", "Time: %s"), t);
        lab(0, x, lby + 56, 17, a_nb >= 0 ? C_NAVY : C_WHITE, 0, a_nb < 0, buf);
    }
    /* derecha: misiones, directamente sobre la libreta (PauseUI de la 1.5.2), separadas del desafio diario por la
     * linea discontinua (en vertical: en la PSP van en dos columnas) */
    float px = 248, py = 12;
    if (a_nb >= 0) for (float yy = 22; yy < 220; yy += 10) r_rect(px - 8, yy, 2, 6, 0xff5a2e0cu);
    else panel(px, py, 222, 160);
    draw_missions_panel(px + 6, py + 4, 210);
    if (pause) {
        foot_btn(-60, "button_fill_menuBack", "icon_house_menu", 32, 32, TR("Inicio", "Home"), "O");
        foot_play(50, TR("SEGUIR", "RESUME"), 22, "X / START");
    } else foot_btn(0, "button_fill_menuBack", "icon_back_menu", 31, 26, TR("Menú", "Menu"), "O");
}
static void draw_pause(void) { challenges_page(1); }

/* ------------------------------------------------------------ trofeos (TrophyHelper) */
static void draw_trophies(void)
{
    char buf[64];
    bg_fallback();
    lab(1, 14, 8, 30, C_SCORE, 0, 0, TR("Trofeos", "Trophies"));
    snprintf(buf, sizeof(buf), "%d/%d", trophies_collected(), NTROPHIES);
    lab(1, SCR_W - 14, 10, 26, C_WHITE, 2, 1, buf);
    /* rejilla de 4 x 2: los que no se tienen salen con el candado (trophy_locked) */
    const float cw = 100, chh = 60, x0 = (SCR_W - 4 * cw) * 0.5f, y0 = 38;
    for (int i = 0; i < NTROPHIES; i++) {
        float x = x0 + (i % 4) * cw, y = y0 + (i / 4) * chh;
        int sel = i == ginfo.trophy_sel;
        ui_sliced(a_ui, sel ? "button_fill_shopItem_green" : "button_fill_shopItem_gray", x + 4, y, cw - 8, chh - 6, 0.5f,
                  sel ? C_WHITE : 0xd0ffffffu);
        spr_c("icon_trophyBackground", x + cw * 0.5f, y + (chh - 6) * 0.5f, 50, 50, 0x80ffffffu);
        ui_sprite_wh(a_ui, trophy_sprite(i), x + cw * 0.5f - 23, y + 4, 46, 46, trophy_unlocked(i) ? C_WHITE : 0xa0ffffffu);
    }
    int i = ginfo.trophy_sel;
    float ty = y0 + 2 * chh;
    panel(40, ty, SCR_W - 80, 44);
    if (trophy_unlocked(i)) {
        labfit(0, SCR_W * 0.5f, ty + 12, 18, SCR_W - 100, C_NAVY, 1, 0, trophy_name(i));
        labfit(0, SCR_W * 0.5f, ty + 32, 15, SCR_W - 100, C_LIMITED, 1, 0, trophy_desc(i));
    } else {
        labc(0, SCR_W * 0.5f, ty + 12, 18, C_NAVY, 1, 0, "???");
        labc(0, SCR_W * 0.5f, ty + 32, 15, C_LIMITED, 1, 0, TR("Sale en las cajas misteriosas", "Find it in Mystery Boxes"));
    }
    foot_btn(0, "button_fill_menuBack", "icon_back_menu", 31, 26, TR("Menú", "Menu"), "O");
}

/* pantalla de carga: al arrancar y si cambia el tema (Halloween) con el juego abierto. Es un fotograma entero */
/* pantalla de carga de la 1.5.2: la imagen de inicio de fondo y abajo una hoja de libreta con un consejo y la barra
 * de carga. La imagen (256 KB) solo esta en memoria mientras se carga */
static const char *const tips_es[] = {
    "Con la tabla activada, un choque no te para.",
    "Rueda hacia abajo para pasar bajo las barreras.",
    "Mejora los power-ups en la tienda para que duren más.",
    "Las letras de la palabra del día dan premios.",
    "Completar misiones sube tu multiplicador.",
};
static const char *const tips_en[] = {
    "With a hoverboard on, a crash won't stop you.",
    "Roll down to slide under barriers.",
    "Upgrade power-ups in the shop to make them last longer.",
    "The daily word letters give you prizes.",
    "Finishing missions raises your multiplier.",
};
static int splash_tex = -2, splash_tip = -1;
static int loading_15(float p)
{
    if (splash_tex == -2) splash_tex = gd.ntex > 0 ? data_tex_find("ui_splash") : -2;
    if (splash_tex < 0 || f_mid < 0) return 0;
    if (a_nb == -2) a_nb = ui_atlas("Notebook15");
    if (splash_tip < 0) splash_tip = (int)(sceKernelGetSystemTimeLow() / 1000u) % 5;
    r_sprite(splash_tex, 0, 0, SCR_W, SCR_H, 0, 0, 1, 1, C_WHITE);
    /* hoja de libreta abajo con su espiral */
    float y0 = 150, x0 = 24, x1 = SCR_W - 24;
    if (a_nb >= 0 && find_sprite(a_nb, "newbackground")) {
        ui_sliced(a_nb, "newbackground", x0, y0 + 6, x1 - x0, SCR_H - y0, 1.0f, C_WHITE);
        USprite *sp = find_sprite(a_nb, "metal_spiral_lo");
        if (sp) {
            float h = 20, w = h * sp->ow / sp->oh, step = w * 1.75f;
            for (float x = x0 + 12; x + w < x1 - 8; x += step) ui_sprite_wh(a_nb, "metal_spiral_lo", x, y0 - 4, w, h, C_WHITE);
        }
    } else r_rect(x0, y0 + 6, x1 - x0, SCR_H - y0, 0xfff0f0f0u);
    labc(1, SCR_W * 0.5f, y0 + 34, 26, C_NAVY, 1, 0, TR("¡Un consejo!", "Here's a tip!"));
    labfit(0, SCR_W * 0.5f, y0 + 62, 16, x1 - x0 - 30, C_NAVY, 1, 0, TR(tips_es[splash_tip], tips_en[splash_tip]));
    /* barra de carga amarilla sobre fondo crema, como la del original */
    float bw = x1 - x0 - 50, bx = (SCR_W - bw) * 0.5f, by = SCR_H - 30;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    r_rect(bx, by, bw, 18, 0xffd8f3fbu);
    r_rect(bx, by, bw * p, 18, 0xff00cff5u);
    labc(0, SCR_W * 0.5f, by + 9, 13, C_NAVY, 1, 1, TR("CARGANDO...", "LOADING..."));
    return 1;
}

void hud_loading(float p)
{
    /* morada solo si el tema se va a poder poner: en temporada pero sin los datos de la 1.4 parecia que el
     * Halloween cargaba y luego no salia */
    int hw = halloween_season() && data_hw_ok;
    r_begin_frame(hw ? 0xff301028 : 0xff6a3a10);   /* morado de Halloween o azul */
    hud_fonts();
    r_begin_2d();
    /* la pantalla de carga de la 1.5.2 en todos los mapas, tambien en Halloween (la morada solo si falta) */
    if (loading_15(p)) {
        r_end_frame();
        if (p >= 1.0f && splash_tex >= 0) { r_wait_idle(); r_tex_unload(splash_tex); }
        return;
    }
    if (f_big >= 0) {
        text_shadow(f_big, SCR_W / 2, 70, 1.2f, 0xff00dbff, 1, "Subway Surfers");
        if (hw) text_shadow(f_big, SCR_W / 2, 112, 0.9f, 0xff1a8cff, 1, "Halloween");
        text_shadow(f_mid, SCR_W / 2, 176, 0.75f, 0xffffffff, 1, TR("Cargando...", "Loading..."));
    }
    float w = 300, x = (SCR_W - w) * 0.5f, y = 206;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    r_rect(x - 2, y - 2, w + 4, 14, 0xff000000);
    r_rect(x, y, w * p, 10, hw ? 0xff1a8cff : 0xff00dbff);
    r_end_frame();
}

/* ------------------------------------------------------------ ajustes y lista de premios (1.5.2) */
/* SettingsPopup de la 1.5.2: sonido (SoundButton) y, mientras dura la Navidad, la temporada (SeasonButton). Los demas
 * botones del original (mensajes, invitar amigos, Game Center, privacidad) necesitan internet: no estan */
static void draw_settings_popup(void)
{
    if (a_st == -2) a_st = ui_atlas("Settings15");
    if (a_nb == -2) a_nb = ui_atlas("Notebook15");
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    int season = game_season_button();
    int n = season ? 2 : 1;
    float w = 300, h = 96 + n * 62, x = (SCR_W - w) * 0.5f, y = (SCR_H - h) * 0.5f - 8;
    panel(x, y, w, h);
    labc(1, x + w * 0.5f, y + 24, 26, C_NAVY, 1, 0, TR("Ajustes", "Settings"));
    for (int i = 0; i < n; i++) {
        float ry = y + 52 + i * 62, bx = x + 22, sel = ginfo.settings_sel == i;
        if (sel) ui_sliced(a_ui, "whitebox", x + 8, ry - 4, w - 16, 58, UIS, 0xffb8e4f6u);
        ui_sliced(a_ui, "button_fill_menuShop", bx, ry + 1, 48, 48, UIS, C_WHITE);
        ui_sliced(a_ui, "button_generic_2_outline", bx - 4, ry - 3, 56, 56, UIS, sel ? 0xff00c8ffu : C_OUTLINE);
        int off = i == 0 ? ginfo.opt_sound_off : ginfo.opt_season_off;
        if (i == 0) spr_fit(a_st, "icon_soundOn", bx + 24, ry + 25, 34, 27, C_WHITE);
        else spr_fit(a_nb, "icon_xmas", bx + 24, ry + 25, 38, 38, C_WHITE);
        if (off) spr_fit(a_st, "icon_off", bx + 24, ry + 25, 44, 44, C_WHITE);
        const char *t = i == 0 ? (off ? TR("Sonido: NO", "Sound OFF") : TR("Sonido: SÍ", "Sound ON"))
                               : (off ? TR("Navidad: NO", "Holidays OFF") : TR("Navidad: SÍ", "Holidays ON"));
        const char *d = i == 0 ? TR("Todos los efectos y la música", "Affects all sounds and music")
                               : TR("Vías, trenes y cielo de Navidad", "Holiday tracks, trains and sky");
        labfit(0, bx + 62, ry + 14, 19, w - 100, C_NAVY, 0, 0, t);
        labfit(0, bx + 62, ry + 35, 14, w - 100, 0xff8a8a8au, 0, 0, d);
    }
    labc(0, x + 14, y + h - 18, 13, 0xff8a8a8au, 0, 0, TR("X: cambiar   O: cerrar", "X: toggle   O: close"));
    labc(0, x + w - 14, y + h - 18, 12, 0xffa0a0a0u, 2, 0, "v1.5.2 PSP");
}

/* PrizeListPopup de la 1.5.2: lo que puede tocar en las cajas misteriosas, por rareza (los textos e iconos del
 * original, en horizontal para la pantalla de la PSP) */
static void draw_prizes_popup(void)
{
    if (a_st == -2) a_st = ui_atlas("Settings15");
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float x = 14, y = 8, w = SCR_W - 28, h = SCR_H - 16;
    panel(x, y, w, h);
    labc(1, x + w * 0.5f - 40, y + 20, 26, C_NAVY, 1, 0, TR("Premios", "Prizes"));
    labc(0, x + w * 0.5f + 40, y + 22, 15, 0xff8a8a8au, 0, 0, TR("en las cajas misteriosas", "found in Mystery Box"));
    spr_fit(a_st, "icon_small_mysteryBox", x + w - 30, y + 20, 28, 26, C_WHITE);
    static const struct { const char *es, *en; unsigned int col; float hh; } cat[4] = {
        {"¡Premio gordo!", "Jackpot!", 0xff2ab8f0u, 38}, {"Raro", "Rare", 0xffb04a8au, 48},
        {"Especial", "Special", 0xffd08a2au, 48}, {"Común", "Common", 0xff3aa34eu, 40},
    };
    static const struct { int cat; const char *icon, *es1, *en1, *es2, *en2; } it[] = {
        {1, "icon_upgrades_headstart2000", "Mega", "Mega", "Salida", "Headstart"},
        {1, "trophy_goldChain_clock_stroke", "Trofeos", "Trophies", NULL, NULL},
        {1, "icon_loginReward_stroke", "5000", "5000", "monedas", "coins"},
        {2, "icon_upgrades_headstart500", "Salida", "Headstart", NULL, NULL},
        {2, "icon_upgrades_hoverboard", "Hasta 10", "Up to 10", "tablas", "Hoverboards"},
        {2, "Icon_coin_pouch_large_stroke", "Hasta 1500", "Up to 1500", "monedas", "coins"},
        {3, "trophy_fresh_token", NULL, NULL, NULL, NULL}, {3, "trophy_yutani_token", NULL, NULL, NULL, NULL},
        {3, "trophy_spike_token", NULL, NULL, NULL, NULL}, {3, "trophy_tricky_token", NULL, NULL, NULL, NULL},
    };
    float ry = y + 40, lw = 96, ax = x + 12 + lw + 8, aw = x + w - 12 - ax;
    for (int c = 0; c < 4; c++) {
        float hh = cat[c].hh;
        ui_sliced(a_ui, "backgroud_callout_square", x + 10, ry, w - 20, hh, UIS, cat[c].col);
        ui_sliced(a_ui, "whitebox", ax, ry + 4, aw - 2, hh - 8, UIS, 0xfff4f4f4u);
        labfit(1, x + 12 + lw * 0.5f, ry + hh * 0.5f, c == 0 ? 17 : 19, lw - 6, C_WHITE, 1, 1, TR(cat[c].es, cat[c].en));
        if (c == 0) {
            /* Jackpot: 100 000 monedas, con el saco de monedas y monedas sueltas */
            spr_fit(a_st, "icon_loginReward_stroke", ax + 30, ry + hh * 0.5f, 34, 30, C_WHITE);
            for (int k = 0; k < 5; k++)
                spr_fit(a_st, "coin2d", ax + 62 + k * 15, ry + 10 + (k & 1) * 16, 12, 12, C_WHITE);
            labfit(1, ax + aw * 0.5f + 50, ry + hh * 0.5f, 22, aw - 160, 0xff2a9ad8u, 1, 1,
                   TR("100 000 monedas", "100 000 coins!"));
        } else {
            int cnt = 0, k = 0;
            for (int i = 0; i < (int)(sizeof(it) / sizeof(it[0])); i++) cnt += it[i].cat == c;
            for (int i = 0; i < (int)(sizeof(it) / sizeof(it[0])); i++) {
                if (it[i].cat != c) continue;
                float cx = ax + aw * (k + 0.5f) / cnt;
                k++;
                if (!it[i].es1) { spr_fit(a_st, it[i].icon, cx, ry + hh * 0.5f, 44, hh - 10, C_WHITE); continue; }
                spr_fit(a_st, it[i].icon, cx - 34, ry + hh * 0.5f, 34, hh - 12, C_WHITE);
                if (it[i].es2) {
                    labfit(0, cx - 14, ry + hh * 0.5f - 8, 15, aw / cnt - 44, C_NAVY, 0, 0, TR(it[i].es1, it[i].en1));
                    labfit(0, cx - 14, ry + hh * 0.5f + 9, 15, aw / cnt - 44, C_NAVY, 0, 0, TR(it[i].es2, it[i].en2));
                } else labfit(0, cx - 14, ry + hh * 0.5f, 15, aw / cnt - 44, C_NAVY, 0, 0, TR(it[i].es1, it[i].en1));
            }
        }
        ry += hh + 4;
    }
    labc(0, x + w - 14, y + h - 12, 13, 0xff8a8a8au, 2, 0, TR("O: cerrar", "O: close"));
}

/* sprite de la hoja de la interfaz o, si no esta, de la de ajustes y premios */
static void spr_any(const char *name, float cx, float cy, float maxw, float maxh, unsigned int col)
{
    if (find_sprite(a_ui, name)) spr_fit(a_ui, name, cx, cy, maxw, maxh, col);
    else spr_fit(a_st, name, cx, cy, maxw, maxh, col);
}

/* premio de un dia: bolsas de monedas (una mas cada dia) o la super caja misteriosa */
static void daily_reward_icon(int day, float cx, float cy, float s)
{
    int pz = daily_prize(day);
    if (!pz) { spr_any("icon_bigSuperMysteryBox", cx, cy, 70 * s, 66 * s, C_WHITE); return; }
    int n = day < 1 ? 1 : day > 4 ? 4 : day;
    for (int k = n - 1; k >= 0; k--)
        spr_any("Icon_coin_pouch_larger", cx + (k - (n - 1) * 0.5f) * 16 * s, cy - (k & 1) * 4 * s, 34 * s, 46 * s, C_WHITE);
}

/* DailyChallengePopup de la 1.5.2: la palabra de hoy, lo que queda de dia, el premio y la racha de 5 dias */
static void draw_daily_popup(void)
{
    char buf[48];
    if (a_st == -2) a_st = ui_atlas("Settings15");
    r_rect(0, 0, SCR_W, SCR_H, 0x82000000);
    float x = 14, y = 8, w = SCR_W - 28, h = SCR_H - 16;
    panel(x, y, w, h);
    labc(1, x + w * 0.5f, y + 20, 26, C_NAVY, 1, 0, TR("Reto diario", "Daily Challenge"));
    int done = daily_paid_today(), next = daily_day_next(), secs = daily_seconds_left();
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
    float lx = x + 20, lw = w * 0.58f;
    if (!done) {
        /* la palabra: letras cogidas en color, las que faltan en gris (DailyChallengeColorChanger) */
        const char *wd = daily_word();
        unsigned m = daily_word_mask();
        ui_sliced(a_ui, "whitebox", lx + 20, y + 44, lw - 40, 46, UIS, 0xffffffffu);
        ui_sliced(a_ui, "button_generic_2_outline", lx + 15, y + 39, lw - 30, 56, UIS, C_OUTLINE);
        char ch[2] = {0, 0};
        float tw = 0;
        for (int i = 0; wd[i]; i++) { ch[0] = wd[i]; tw += labw(1, 30, ch) + 8; }
        float cx = lx + lw * 0.5f - tw * 0.5f;
        for (int i = 0; wd[i]; i++) {
            ch[0] = wd[i];
            lab(1, cx, y + 50, 30, (m >> i & 1) ? C_LETTER_ON : C_LETTER_OFF, 0, 0, ch);
            cx += labw(1, 30, ch) + 8;
        }
        labc(0, lx + lw * 0.5f, y + 108, 17, C_NAVY, 1, 0, TR("Recoge las letras en la carrera", "Collect letters"));
        char t2[64];
        snprintf(t2, sizeof(t2), TR("Tiempo: %s", "Time: %s"), buf);
        labc(0, lx + lw * 0.5f, y + 128, 17, 0xff8a8a8au, 1, 0, t2);
    } else {
        ui_sliced(a_ui, "backgroud_callout_square", lx + 10, y + 42, lw - 20, 96, UIS, 0xffd08a2au);
        labc(0, lx + lw * 0.5f, y + 60, 17, C_WHITE, 1, 1, TR("PRÓXIMO RETO EN", "NEXT CHALLENGE IN"));
        labc(1, lx + lw * 0.5f, y + 88, 30, C_WHITE, 1, 1, buf);
        labc(0, lx + lw * 0.5f, y + 118, 16, C_WHITE, 1, 1, TR("Vuelve y gana:", "Come back and win:"));
    }
    /* premio del dia que toca (el de hoy, o el de manana si ya esta hecha) */
    float rx = x + w * 0.79f, ry = y + 82;
    spr_any("background_splat", rx, ry, 120, 120, C_WHITE);
    daily_reward_icon(next, rx, ry - 6, 1.0f);
    if (daily_prize(next)) {
        snprintf(buf, sizeof(buf), "%d", daily_prize(next));
        spr_any("coin2d", rx - labw(0, 17, buf) * 0.5f - 10, ry + 38, 16, 16, C_WHITE);
        labc(0, rx + 4, ry + 38, 17, C_NAVY, 1, 0, buf);
    }
    /* la racha: dias 1 a 4 (monedas) y 5+ (super caja) */
    int today = done ? next - 1 : next;   /* el dia que corresponde a hoy */
    float by = y + 150, bw = (w - 40) / 5.0f;
    for (int d = 1; d <= 5; d++) {
        float bx = x + 20 + (d - 1) * bw, cx = bx + bw * 0.5f;
        int act = d == today || (d == 5 && today >= 5), past = d < today && d < 5;
        unsigned bg = d == 5 ? (act ? 0xff05ea9du : 0xff3ee1f8u) : act ? 0xffd08a2au : 0xffe0e0e0u;
        ui_sliced(a_ui, "backgroud_callout_square", bx + 3, by, bw - 6, h - (by - y) - 10, UIS, bg);
        unsigned tc = act || d == 5 ? C_WHITE : 0xff532a09u;
        if (d < 5) snprintf(buf, sizeof(buf), act ? TR("Hoy", "Today") : TR("Día %d", "Day %d"), d);
        else snprintf(buf, sizeof(buf), act ? TR("¡Hoy!", "Today!") : TR("Día 5+", "Day 5+"));
        labc(0, cx, by + 12, 15, tc, 1, d == 5 || act, buf);
        if (d < 5) {
            spr_any("Icon_coin_pouch_large", cx, by + 40, 30, 36, C_WHITE);
            snprintf(buf, sizeof(buf), "%d", daily_prize(d));
            labc(0, cx, by + 68, 15, tc, 1, act, buf);
        } else spr_any("icon_bigSuperMysteryBox", cx, by + 48, 56, 52, C_WHITE);
        if (past || (act && done)) spr_any("checkbox_checked_mission", bx + bw - 16, by + 12, 26, 22, C_WHITE);
    }
    labc(0, x + w - 14, y + h - 12, 13, 0xff8a8a8au, 2, 0, TR("O: cerrar", "O: close"));
}

void hud_draw(void)
{
    hud_fonts();
    r_begin_2d();
    bg_release();
    nb_release();
    if (a_thumbs == -2) a_thumbs = ui_atlas("CharThumbs");
    if (a_rewards == -2) a_rewards = ui_atlas("RewardIcons");
    thumbs_release();
    bthumbs_release();
    rewards_release();
    switch (ginfo.state) {
    case GS_MENU: draw_menu(); break;
    case GS_SHOP: draw_shop(); break;
    case GS_CHARS: draw_chars(); break;
    case GS_BOARDS: draw_boards(); break;
    case GS_TROPHIES: draw_trophies(); break;
    case GS_GAMEOVER: draw_gameover(); break;
    default:
        if (ginfo.paused && ginfo.countdown <= 0) draw_pause();
        else draw_ingame();
        break;
    }
    if (ginfo.popup == POPUP_SETTINGS) draw_settings_popup();
    if (ginfo.popup == POPUP_PRIZES) draw_prizes_popup();
    if (ginfo.popup == POPUP_DAILY) draw_daily_popup();
    if (ginfo.state != GS_MENU || !ginfo.popup) draw_slides();
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
    perf_line(2, level_col(-fps, -29.0f, -24), TR("FPS %.1f  CPU %.1f ms  espera %.1f ms", "FPS %.1f  CPU %.1f ms  wait %.1f ms"), fps, cpu_ms, gpu_ms);
    perf_line(20, 0xffffffff, "vert %d  tri %d  draw %d  clip %d", r_stat_verts, r_stat_tris, r_stat_draws, r_stat_clipped);
    perf_line(38, level_col(hf, 0.75f, 0.9f), "RAM %.1f/%.1f MB   VRAM %d/2048 KB", m->heap_used / 1048576.0f,
              m->heap_total / 1048576.0f, (r_mem.vram_fb + r_mem.vram_tex) / 1024);
    perf_line(56, 0xffc0c0c0, TR("SELECT: memoria", "SELECT: memory"));
}

static void perf_memory(float fps)
{
    const MemInfo *m = &dbg_mem;
    const RMem *r = &r_mem;
    float hf = m->heap_total ? (float)m->heap_used / m->heap_total : 0;
    float vf = (float)(r->vram_fb + r->vram_tex) / R_VRAM_TOTAL;
    r_rect(0, 0, 330, 196, 0xc0000000);
    perf_line(2, 0xffffffff, TR("MEMORIA   (FPS %.1f)", "MEMORY   (FPS %.1f)"), fps);
    perf_line(20, level_col(hf, 0.75f, 0.9f), TR("RAM monton: %.2f / %.2f MB (%.0f%%) pico %.2f", "RAM heap: %.2f / %.2f MB (%.0f%%) peak %.2f"), m->heap_used / 1048576.0f,
              m->heap_total / 1048576.0f, hf * 100, m->heap_peak / 1048576.0f);
    bar(6, 36, 318, 5, hf, level_col(hf, 0.75f, 0.9f));
    perf_line(44, m->sys_free < 256 * 1024 ? C_BAD : 0xffffffff, TR("Sistema libre: %u KB (bloque mayor %u KB)", "System free: %u KB (largest block %u KB)"), m->sys_free / 1024,
              m->sys_max_block / 1024);
    perf_line(62, r->vram_full ? C_WARN : 0xffffffff, TR("VRAM: %d / 2048 KB  pantalla+Z %d  tex %d KB (%d)", "VRAM: %d / 2048 KB  screen+Z %d  tex %d KB (%d)"),
              (r->vram_fb + r->vram_tex) / 1024, r->vram_fb / 1024, r->vram_tex / 1024, r->vram_tex_n);
    bar(6, 78, 318, 5, vf, C_OK);
    perf_line(86, 0xffffffff, TR("Texturas en RAM: %d KB (%d)   sin sitio en VRAM: %d", "Textures in RAM: %d KB (%d)   no room in VRAM: %d"), r->ram_tex / 1024, r->ram_tex_n, r->vram_full);
    float lf = (float)r->dlist_peak / r->dlist_max, vrf = (float)r->vring_peak / r->vring_max, irf = (float)r->iring_peak / r->iring_max;
    perf_line(104, level_col(lf, 0.6f, 0.8f), TR("Lista GE: %d%% (pico %d%%) de %d KB", "GE list: %d%% (peak %d%%) of %d KB"), r->dlist_used * 100 / r->dlist_max,
              (int)(lf * 100), r->dlist_max / 1024);
    perf_line(122, level_col(vrf > irf ? vrf : irf, 0.7f, 0.9f), TR("Buffer vert %d%% (pico %d%%)  ind %d%% (pico %d%%)", "Vert buffer %d%% (peak %d%%)  idx %d%% (peak %d%%)"),
              r->vring_used * 100 / r->vring_max, (int)(vrf * 100), r->iring_used * 100 / r->iring_max, (int)(irf * 100));
    perf_line(140, r->dropped ? C_BAD : 0xffffffff, TR("Dibujos perdidos: %d   tirones (>50 ms): %d", "Dropped draws: %d   hitches (>50 ms): %d"), r->dropped, dbg_hitches);
    if (dbg_bat_pct >= 0)
        perf_line(158, dbg_bat_temp >= 45 ? C_BAD : 0xffffffff, TR("CPU %d MHz  bus %d MHz  bateria %d%% %d C%s", "CPU %d MHz  bus %d MHz  battery %d%% %d C%s"), dbg_cpu_mhz,
                  dbg_bus_mhz, dbg_bat_pct, dbg_bat_temp, dbg_plugged ? TR(" (cargando)", " (charging)") : "");
    else perf_line(158, 0xffffffff, TR("CPU %d MHz  bus %d MHz  sin bateria", "CPU %d MHz  bus %d MHz  no battery"), dbg_cpu_mhz, dbg_bus_mhz);
    perf_line(176, 0xffc0c0c0, TR("SELECT: grafica de tiempos", "SELECT: frame time graph"));
}

static void perf_graph(float cpu_ms, float fps)
{
    /* tiempo de cada fotograma (ultimos 128): verde < 16,7 ms (60 FPS), amarillo < 33,3 ms (30 FPS), rojo mas */
    const float x0 = 8, y0 = 30, w = 384, h = 100, scale = h / 66.6f;
    r_rect(0, 0, x0 * 2 + w, y0 + h + 40, 0xc0000000);
    perf_line(2, 0xffffffff, TR("TIEMPO POR FOTOGRAMA  FPS %.1f  CPU %.1f ms  tirones %d", "FRAME TIME  FPS %.1f  CPU %.1f ms  hitches %d"), fps, cpu_ms, dbg_hitches);
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
    perf_line(y0 + h + 6, 0xffc0c0c0, TR("SELECT: ocultar", "SELECT: hide"));
}

void perf_draw(int page, float cpu_ms, float gpu_ms, float fps)
{
    hud_fonts();
    r_begin_2d();
    if (page == 1) perf_summary(cpu_ms, gpu_ms, fps);
    else if (page == 2) perf_memory(fps);
    else if (page == 3) perf_graph(cpu_ms, fps);
}
