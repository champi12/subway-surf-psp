/* game.h - logica del juego (Game.cs, Character.cs, Running.cs, Jetpack.cs, modificadores, guardia) */
#ifndef GAME_H
#define GAME_H
#include "vmath.h"

enum { GS_MENU, GS_INTRO, GS_RUNNING, GS_JETPACK, GS_DIEFALL, GS_DIE, GS_GAMEOVER, GS_CHECKPOINT, GS_SHOP, GS_CHARS, GS_TROPHIES,
       GS_BOARDS };

/* botones (ya filtrados: solo los recien pulsados) */
enum { IN_LEFT = 1, IN_RIGHT = 2, IN_UP = 4, IN_DOWN = 8, IN_BOARD = 16, IN_START = 32, IN_CROSS = 64, IN_CIRCLE = 128,
       IN_TRIANGLE = 256, IN_SQUARE = 512, IN_L = 1024, IN_R = 2048 };

typedef struct {
    int score, coins, meters;
    int multiplier;
    int highscore, total_coins, hoverboards;
    float pw_left[8], pw_total[8];      /* power-ups activos (para la interfaz) */
    int state;
    int paused;
    float time;
    int final_score;
    const char *message;  float message_t;
    int hs500, hs2000, boxes_pending, boxes_opened;
    int shop_sel;
    float countdown;              /* cuenta atras al quitar la pausa */
    float headstart_until;        /* botones de headstart visibles hasta este momento */
    char reward[64]; float reward_t;
    int gameover_coins;
    int boosters;                 /* Score Boosters (1.7): +5 al multiplicador en una carrera */
    float gameover_t;             /* ginfo.time al llegar al fin de partida (CountUpCoins) */
    int gameover_score0;          /* puntos sin las monedas (de ahi sube hasta final_score) */
    int tokens, character;        /* tokens: fichas de personaje recogidas (total); character: personaje elegido */
    int char_sel;                 /* personaje marcado en la pantalla de personajes */
    int board_sel;                /* tabla marcada en la pantalla de tablas */
    int trophy_sel;               /* trofeo marcado en la pantalla de trofeos */
    int missions_open;            /* ventana de misiones abierta en el menu */
    int popup;                    /* ventana encima del menu (POPUP_...) */
    int settings_sel;             /* opcion marcada en los ajustes */
    int opt_sound_off, opt_season_off;   /* ajustes de la 1.5.2: sonido y temporada (Navidad) apagados */
    int sboxes_pending, sboxes_opened;   /* super cajas misteriosas (1.4) */
    int reward_super;             /* el ultimo premio era de una super caja */
    int reward_trophy;            /* el ultimo premio es un trofeo nuevo ("NEW TROPHY!") */
    const char *reward_icon;      /* sprite del objeto del premio (ficha, trofeo, monedas...) */
#define REWARD_REVEAL 2.26f       /* hasta el texto: la tapa a la mitad (0,46 s) + 0,5 + 1,3 s (MysteryBoxPopup) */
} GameInfo;
enum { POPUP_NONE, POPUP_HALLOWEEN, POPUP_XMAS, POPUP_SETTINGS, POPUP_PRIZES, POPUP_DAILY, POPUP_BOARDS, POPUP_NY, POPUP_RIO, POPUP_ROME, POPUP_SYDNEY };
/* TutorialHoverboardPopup ("Out of Boards?"): aviso en el boton de tablas del menu y cuantas se han usado (0: la
 * version "Need Help?") */
int game_board_guideline(void);
int game_booster_on(void);        /* Score Booster activo en esta carrera */
float game_booster_time(void);    /* ginfo.time al activarlo */
float gameover_count(void);      /* CountUpCoins: 0 antes de contar, sube a 1 (suavizado) mientras pasan las monedas */
int game_boards_used(void);
int board_is_new(int row);       /* tabla nueva de la 1.6 aun no vista (pegatina NEW) */
int board_new_count(void);       /* cuantas nuevas sin ver (numero en los botones ME y Tablas) */
int game_season_button(void);    /* 1: los ajustes llevan el boton de temporada (Navidad disponible en este mundo) */

typedef struct { const char *name, *name_en, *icon; int kind, idx; } ShopItem;
extern const ShopItem shop_items[];
extern const int shop_count;
int shop_price(int i);
int shop_amount(int i);

/* personajes (CharacterModels.cs): Jake gratis, Tricky/Fresh/Spike/Yutani con fichas de las cajas misteriosas,
 * Lucy/Ninja/Frank/King/Frizzy con monedas */
enum { UL_FREE, UL_TOKENS, UL_COINS };
int char_count(void);
/* tablas de la 1.5.2 (fila = orden de la tienda); board_count 0 si no hay boards.bin */
int board_count(void);
const char *board_name(int row);
int board_price(int row);
int board_is_owned(int row);
int board_is_selected(int row);
int board_effect(int row);
int board_is_gift(int row);
int board_step(int row, int dir);   /* siguiente tabla a la venta (salta las de temporada fuera de fecha) */
int board_is_season(int row);
int ny_season(void);
int rio_season(void);
int rome_season(void);
int sydney_season(void);
/* busqueda de huevos de la 1.8 */
int game_eggs(void);
int game_eggs_max(void);
int game_egg_counter(void);
int game_cloud_tex(void);         /* nubes del jetpack */
int game_fx_get(int i, V3 *pos, float *size, unsigned int *col);   /* particulas del humo de la tabla */      /* la de regalo (FREE GIFT) */
const char *char_name(int i);
const char *char_thumb(int row);   /* nombre de su miniatura (thumb_<nombre> en CharThumbs) */
int char_unlock_type(int i);
int char_price(int i);            /* fichas o monedas necesarias */
int char_have(int i);             /* fichas que se tienen (o el precio si ya esta comprado) */
int char_unlocked(int i);
int char_is_event(int i);
int char_is_selected(int i);     /* fila i = personaje elegido */
/* palabra del dia */
const char *daily_word(void);
unsigned daily_word_mask(void);
int daily_word_complete(void);         /* contenido de Halloween: solo se compra en esas fechas */
const char *char_token_name(int i);
const char *char_token_sprite(int i);

int halloween_season(void);
int season_seconds_left(void);    /* lo que queda del evento de Halloween (0 fuera de temporada) */
int day_seconds_left(void);       /* lo que queda del dia (palabra del dia) */
int daily_prize(int day);         /* premio del dia n de la racha: monedas, o 0 = super caja */
int daily_prize_next(void);
int daily_day_next(void);         /* dia de la racha de ese premio (1, 2, 3...) */
int daily_streak(void);
int daily_paid_today(void);     /* la palabra de hoy ya se completo y se pago */
int daily_seconds_left(void);   /* hasta medianoche */
const char *shop_desc(int i);     /* descripcion de Upgrades.cs */

/* trofeos (Trophies.cs): 8 objetos de oro que salen en las cajas misteriosas */
#define NTROPHIES 8
int trophy_unlocked(int i);
int trophies_collected(void);
const char *trophy_name(int i);
const char *trophy_desc(int i);
const char *trophy_sprite(int i);   /* "trophy_locked" si aun no se tiene */

extern GameInfo ginfo;
extern float game_time;

void game_init(void);
void game_update(float dt);
void game_draw(void);
void game_input(unsigned in);
void game_save(void);

#endif
