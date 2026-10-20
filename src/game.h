/* game.h - logica del juego (Game.cs, Character.cs, Running.cs, Jetpack.cs, modificadores, guardia) */
#ifndef GAME_H
#define GAME_H
#include "vmath.h"

enum { GS_MENU, GS_INTRO, GS_RUNNING, GS_JETPACK, GS_DIEFALL, GS_DIE, GS_GAMEOVER, GS_CHECKPOINT, GS_SHOP, GS_CHARS, GS_TROPHIES };

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
    int tokens, character;        /* tokens: fichas de personaje recogidas (total); character: personaje elegido */
    int char_sel;                 /* personaje marcado en la pantalla de personajes */
    int trophy_sel;               /* trofeo marcado en la pantalla de trofeos */
    int missions_open;            /* ventana de misiones abierta en el menu */
} GameInfo;

typedef struct { const char *name, *name_en, *icon; int kind, idx; } ShopItem;
extern const ShopItem shop_items[];
extern const int shop_count;
int shop_price(int i);
int shop_amount(int i);

/* personajes (CharacterModels.cs): Jake gratis, Tricky/Fresh/Spike/Yutani con fichas de las cajas misteriosas,
 * Lucy/Ninja/Frank/King/Frizzy con monedas */
enum { UL_FREE, UL_TOKENS, UL_COINS };
int char_count(void);
const char *char_name(int i);
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
