/* game.h - logica del juego (Game.cs, Character.cs, Running.cs, Jetpack.cs, modificadores, guardia) */
#ifndef GAME_H
#define GAME_H
#include "vmath.h"

enum { GS_MENU, GS_INTRO, GS_RUNNING, GS_JETPACK, GS_DIEFALL, GS_DIE, GS_GAMEOVER, GS_CHECKPOINT };

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
} GameInfo;

extern GameInfo ginfo;
extern float game_time;

void game_init(void);
void game_update(float dt);
void game_draw(void);
void game_input(int left, int right, int up, int down, int board, int start, int confirm);
void game_save(void);

#endif
