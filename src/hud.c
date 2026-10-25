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
#include "lang.h"

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
static void coin_box(float xr, float cy)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", ginfo.total_coins);
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
    return ginfo.state == GS_SHOP || ginfo.state == GS_CHARS || ginfo.state == GS_GAMEOVER || ginfo.state == GS_TROPHIES;
}
int hud_background(void)
{
    if (bg_tex == -2) bg_tex = data_tex_find("ui_bg");
    if (bg_tex < 0 || !bg_screen()) return 0;
    r_background(bg_tex);   /* tambien la deja cargada (una sola: la normal o la de Halloween, 64 KB) */
    bg_loaded = 1;
    return 1;
}
/* fuera de esas pantallas el fondo no ocupa memoria */
static void bg_release(void)
{
    if (bg_loaded && !bg_screen()) { r_tex_unload(bg_tex); bg_loaded = 0; }
}
static void bg_fallback(void)
{
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
}

/* ventana de misiones del menu (triangulo) */
static void draw_missions_popup(void)
{
    r_rect(0, 0, SCR_W, SCR_H, 0x80000000);
    float w = 300, h = 186, x = (SCR_W - w) * 0.5f, y = 26;
    panel(x, y, w, h);
    draw_missions_panel(x + 12, y + 8, w - 24);
    lab(0, SCR_W * 0.5f, y + h + 10, 14, C_WHITE, 1, 1, TR("X / O cerrar", "X / O close"));
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

static void draw_menu(void)
{
    char buf[48];
    /* arriba a la izquierda: Misiones (triangulo) y la palabra del dia */
    top_button(U(46), U(26), U(76), TR("Misiones", "Missions"), TR("triángulo", "triangle"));
    top_button(U(144), U(26), U(110), daily_word_complete() ? TR("¡Palabra hecha!", "Word: done!") : TR("Palabra del día", "Daily Challenge"), NULL);
    {
        const char *w = daily_word();
        unsigned m = daily_word_mask();
        char ch[2] = {0, 0};
        float tw = 0;
        for (int i = 0; w[i]; i++) { ch[0] = w[i]; tw += labw(1, 20, ch) + 4; }
        float cx = U(144) - tw * 0.5f;
        for (int i = 0; w[i]; i++) {
            ch[0] = w[i];
            lab(1, cx, U(50), 20, (m >> i & 1) ? C_LETTER_ON : 0xffd0d0d0u, 0, 1, ch);
            cx += labw(1, 20, ch) + 4;
        }
    }
    /* arriba a la derecha: hoverboards y trofeos (cuadrado) */
    float hx = SCR_W - U(79), hy = U(26);
    top_button(hx, hy, U(44), NULL, NULL);
    ui_sprite_wh(a_ui, "icon_hoverboard", hx - U(16), hy - U(13), U(32), U(20), C_WHITE);
    snprintf(buf, sizeof(buf), "%d", ginfo.hoverboards);
    labc(0, hx + U(16), hy + U(8), 14, C_WHITE, 2, 1, buf);
    float tx = SCR_W - U(28);
    top_button(tx, hy, U(44), NULL, TR("cuadrado", "square"));
    spr_c("icon_trophies", tx, hy, U(29), U(29), C_WHITE);
    /* "Tap to play" que late */
    float a = 0.55f + 0.45f * sinf(ginfo.time * 4.0f);
    lab(1, SCR_W * 0.5f, SCR_H - U(132), 20, ((unsigned)(a * 255) << 24) | 0xffffff, 1, 1,
        TR("Pulsa X para jugar", "Press X to play"));
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
    foot_btn(100, "button_fill_menuShop", "icon_characters", 39, 28, TR("Personajes", "Characters"), "L/R");
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

static void draw_chars(void)
{
    char buf[48];
    bg_fallback();
    coin_box(SCR_W - 8, 15);
    int i = ginfo.char_sel, nc = char_count(), un = char_unlocked(i);
    int limited = char_is_event(i), secs = season_seconds_left();
    /* nombre y "Halloween Special" arriba a la izquierda */
    float nw = lab(0, 14, 8, 30, C_NAME, 0, 0, char_name(i));
    if (limited) lab(0, 16, 40, 15, C_LIMITED, 0, 0, TR("Especial de Halloween", "Halloween Special"));
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
    foot_btn(-100, "button_fill_menuBack", "icon_back_menu", 31, 26, TR("Menú", "Menu"), "O");
    foot_btn(0, "button_fill_menuShop", "icon_upgrades", 29, 26, TR("Mejoras", "Boosts"), "L/R");
    foot_btn(100, "button_fill_menuShop", "icon_characters", 39, 28, TR("Personajes", "Characters"), NULL);
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

static float ease_cos(float f) { f = f < 0 ? 0 : f > 1 ? 1 : f; return 0.5f - 0.5f * cosf(f * 3.14159265f); }

/* MysteryBoxHandler._ShowReward: la tapa se levanta (clip "up"), la caja se va hacia abajo (0,7 s), el premio
 * crece desde la caja (2 s) dando vueltas que se frenan (4 s), el brillo aparece a los 0,5 s y el texto a 1,8 s */
static void draw_box_popup(int sup)
{
    char buf[48];
    float bx = SCR_W * 0.5f, cy = 112;
    const char *box = sup ? "box_super" : "box_mystery", *box_open = sup ? "box_super_open" : "box_mystery_open";
    const char *old = sup ? "icon_bigSuperMysteryBox" : "icon_upgrades_mysteryBox";
    float gl = sup ? 1.0f : 0.7f;
    if (sup) labc(1, bx, 26, 26, 0xff00dbffu, 1, 1, TR("Súper caja misteriosa", "Super Mystery Box"));
    if (!ginfo.reward[0]) {
        /* esperando: la caja flota arriba y abajo (BoxIdleAnimCoroutine) */
        spr_c("background_superglow", bx, cy, 150 * gl + 40, 150 * gl + 40, sup ? 0x70ffffffu : 0x40ffffffu);
        reward_spr(box, old, bx, cy + sinf(ginfo.time * 2.0f) * 5.0f, 1, 1, C_WHITE);
        labc(1, bx, 186, 30, C_WHITE, 1, 1, TR("abrir (X)", "open (X)"));
        snprintf(buf, sizeof(buf), TR("Cajas: %d", "Boxes: %d"), ginfo.boxes_pending + ginfo.sboxes_pending);
        labc(0, bx, 216, 15, C_WHITE, 1, 1, buf);
        return;
    }
    float t = ginfo.time - ginfo.reward_t;
    /* el brillo de detras del premio: de negro a blanco en 1,5 s desde los 0,5 s */
    float g = (t - 0.5f) / 1.5f;
    if (g > 0) {
        if (g > 1) g = 1;
        unsigned a = (unsigned)((sup ? 0x90 : 0x60) * g);
        spr_c("background_superglow", bx, cy - 4, 190 * gl, 190 * gl, (a << 24) | 0xffffffu);
    }
    /* la caja: cerrada un instante, abierta despues, y se va hacia abajo */
    if (t < 1.2f) {
        float y = cy + (t > 0.46f ? ease_cos((t - 0.46f) / 0.7f) * 200 : 0);
        reward_spr(t < 0.12f ? box : box_open, old, bx, y, 1, 1, C_WHITE);
    }
    /* el premio: aparece cuando la tapa esta arriba, crece de 1/4 a su tamano y gira frenando */
    if (t >= 0.3f && ginfo.reward_icon) {
        float f = (t - 0.3f) / 2.0f, k = 0.25f + 0.75f * ease_cos(f);
        float y = cy - 4 + (1 - ease_cos((t - 0.3f) / 0.6f)) * 24;
        float r = (t - 0.3f) / 4.0f;
        if (r > 1) r = 1;
        float ang = (1 - (1 - r) * (1 - r)) * 4.0f * 3.14159265f;      /* 2 vueltas que se van frenando */
        float wk = cosf(ang);
        if (wk > -0.1f && wk < 0.1f) wk = wk < 0 ? -0.1f : 0.1f;       /* de canto: una raya, no desaparece */
        reward_spr(ginfo.reward_icon, ginfo.reward_icon, bx, y, k, wk, C_WHITE);
    }
    if (t >= REWARD_REVEAL) {
        unsigned a = t < REWARD_REVEAL + 0.2f ? (unsigned)(255 * (t - REWARD_REVEAL) / 0.2f) : 255;
        labc(0, bx, 178, 18, (a << 24) | 0xffffffu, 1, 1, TR("Has conseguido", "You got"));
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
    coin_box(SCR_W - 8, 15);
    /* marcador: "* Score *", la pizarra con los puntos y las monedas, y el record */
    float cx = 128, cy = 112;
    float stw = labw(1, 30, TR("Puntos", "Score")) * 0.5f + U(14);
    spr_c("icon_star", cx - stw, cy - U(72), U(20), U(19), C_SCORE);
    spr_c("icon_star", cx + stw, cy - U(72), U(20), U(19), C_SCORE);
    labc(1, cx, cy - U(72), 30, C_SCORE, 1, 0, TR("Puntos", "Score"));
    spr_c("scoreboard", cx, cy, U(160) * 1.15f, U(121) * 1.15f, C_WHITE);
    snprintf(buf, sizeof(buf), "%d", ginfo.final_score);
    labfit(1, cx, cy - U(34), 30, U(150), C_WHITE, 1, 1, buf);
    snprintf(buf, sizeof(buf), "%d", ginfo.gameover_coins);
    float tw = labw(0, 20, buf);
    spr_c("coin2d", cx - tw * 0.5f - U(12), cy + U(4), U(18), U(18), C_WHITE);
    labc(0, cx + U(6), cy + U(4), 20, C_WHITE, 1, 1, buf);
    snprintf(buf, sizeof(buf), TR("Récord: %d", "Best: %d"), ginfo.highscore);
    labfit(0, cx, cy + U(40), 15, U(150), C_WHITE, 1, 1, buf);
    /* a la derecha (donde la 1.4 tiene la clasificacion de amigos): las misiones */
    float px = 248, py = 30;
    panel(px, py, 222, 160);
    draw_missions_panel(px + 6, py + 4, 210);
    foot_btn(-132, "button_fill_menuBack", "icon_house_menu", 32, 32, TR("Inicio", "Home"), "O");
    foot_btn(-46, "button_fill_menuShop", "icon_upgrades", 29, 26, TR("Mejoras", "Boosts"), "R");
    foot_play(74, TR("JUGAR", "PLAY"), 28, "X");
    /* cajas misteriosas de la carrera (MysteryBoxPopup): se abren una a una con X */
    if (ginfo.boxes_pending > 0 || ginfo.sboxes_pending > 0 || ginfo.reward[0]) {
        r_rect(0, 0, SCR_W, SCR_H, 0xa0000000);
        /* super caja (SuperMysteryBoxEffect): la plateada con mas brillo; se abren antes que las normales */
        draw_box_popup(ginfo.reward[0] ? ginfo.reward_super : ginfo.sboxes_pending > 0);
    }
}

/* ------------------------------------------------------------ pausa (PauseUI de la 1.4) */
static void draw_pause(void)
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
        labc(0, rx, ry - U(48), 15, C_WHITE, 1, 1, buf);
    }
    if (daily_word_complete()) {
        lab(0, x, lby + 34, 17, C_WHITE, 0, 1, TR("Siguiente desafío en", "Next challenge in"));
        fmt_time(buf, sizeof(buf), day_seconds_left(), 0);
        lab(1, x, lby + 54, 26, C_WHITE, 0, 1, buf);
    } else {
        lab(0, x, lby + 34, 17, C_WHITE, 0, 1, TR("Recoge las letras", "Collect Letters"));
        char t[24];
        fmt_time(t, sizeof(t), day_seconds_left(), 0);
        snprintf(buf, sizeof(buf), TR("Tiempo: %s", "Time: %s"), t);
        lab(0, x, lby + 56, 17, C_WHITE, 0, 1, buf);
    }
    /* derecha: misiones */
    float px = 248, py = 12;
    panel(px, py, 222, 160);
    draw_missions_panel(px + 6, py + 4, 210);
    foot_btn(-60, "button_fill_menuBack", "icon_house_menu", 32, 32, TR("Inicio", "Home"), "O");
    foot_play(50, TR("SEGUIR", "RESUME"), 22, "X / START");
}

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
void hud_loading(float p)
{
    int hw = halloween_season();
    r_begin_frame(hw ? 0xff301028 : 0xff6a3a10);   /* morado de Halloween o azul */
    hud_fonts();
    r_begin_2d();
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

void hud_draw(void)
{
    hud_fonts();
    r_begin_2d();
    bg_release();
    if (a_thumbs == -2) a_thumbs = ui_atlas("CharThumbs");
    if (a_rewards == -2) a_rewards = ui_atlas("RewardIcons");
    thumbs_release();
    rewards_release();
    switch (ginfo.state) {
    case GS_MENU: draw_menu(); break;
    case GS_SHOP: draw_shop(); break;
    case GS_CHARS: draw_chars(); break;
    case GS_TROPHIES: draw_trophies(); break;
    case GS_GAMEOVER: draw_gameover(); break;
    default:
        if (ginfo.paused && ginfo.countdown <= 0) draw_pause();
        else draw_ingame();
        break;
    }
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
