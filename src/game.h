/* game.h - logica del juego (Game.cs, Character.cs, Running.cs, Jetpack.cs, modificadores, guardia) */
#ifndef GAME_H
#define GAME_H
#include "vmath.h"

enum { GS_MENU, GS_INTRO, GS_RUNNING, GS_JETPACK, GS_DIEFALL, GS_DIE, GS_GAMEOVER, GS_CHECKPOINT, GS_SHOP };

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
    int tokens, character;
    int missions_open;            /* ventana de misiones abierta en el menu */
} GameInfo;

typedef struct { const char *name, *icon; int kind, idx; } ShopItem;
extern const ShopItem shop_items[];
extern const int shop_count;
int shop_price(int i);
int shop_amount(int i);

extern GameInfo ginfo;
extern float game_time;

void game_init(void);
void game_update(float dt);
void game_draw(void);
void game_input(unsigned in);
void game_save(void);

#endif
