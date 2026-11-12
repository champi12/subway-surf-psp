/* missions.h - misiones del original (Missions.cs) */
#ifndef MISSIONS_H
#define MISSIONS_H

/* objetivos (Missions.MissionTarget) que usa la logica del juego */
enum {
    M_EARNCOIN = 1, M_SPENDCOIN = 2, M_SCORE = 3, M_JUMPTRAIN = 4, M_JUMP = 5, M_ROLL = 6, M_ROLLLEFT = 7,
    M_ROLLCENTER = 8, M_ROLLRIGHT = 9, M_ROLLUNDERBARRIERS = 10, M_JUMPBARRIERS = 11, M_DIETOTRAIN = 12,
    M_JETPACK = 13, M_SUPERSNEAKERS = 14, M_LETTERS = 15, M_MAGNETS = 16, M_MYSTERYBOXES = 17, M_DAILYQUESTS = 19, M_TOKENS = 20,
    M_DODGEBARRIERS = 21, M_CRASHBARRIERS = 22, M_CRASHTRAINS = 23, M_POWERUPS = 24, M_HEADSTART = 25,
    M_COINSWITHMAGNET = 26, M_BUYMYSTERYBOX = 27, M_TIMEDEATH = 29, M_BUMPTRAIN = 30, M_BUMPBUSH = 31,
    M_BUMPLIGHTSIGNAL = 32, M_BUMPBARRIER = 33, M_HOVERBOARD = 34, M_HOVERBOARDEXPIRE = 35,
    M_NOCOINSBEFORESCORE = 36, M_DOUBLEMULTIPLIER = 38, M_GUARDJUMP = 40,
    /* de la 1.4 */
    M_LANDONTRAININROW = 49, M_STAYINONELANE = 50, M_DAILYQUESTINROW = 51, M_NOJUMPSBEFORESCORE = 52,
    M_NOROLLSBEFORESCORE = 53, M_NOPOWERUPSBEFORESCORE = 54, M_ONEOFEACHPOWERUP = 55,
    /* de la 1.10 */
    M_ACTIVEPOWERUPS = 56, M_SCOREBOOSTER = 57, M_COINSLEFT = 58, M_COINSCENTER = 59, M_COINSRIGHT = 60,
    M_SPENDKEYS = 61, M_PICKUPKEYS = 62, M_COINSINAIR = 63, M_EXACTCOINS = 64, M_SKIPMISSION = 65,
    M_SCOREWITHBOOSTER = 66, M_COINSWITHJETPACK = 67, M_BEATHIGHSCORE = 68
};

extern int mission_set;
extern int mission_progress[3];
extern char mission_msg[64];
extern float mission_msg_t;

void missions_did(int target, int magnitude);
void missions_reset(int target);   /* RemoveProgressForThis */
void missions_run_start(void);
void missions_run_end(void);
int missions_multiplier(void);
void missions_text(int i, char *buf, int n);
int missions_progress(int i);
int missions_goal(int i);
int missions_done(int i);
int mission_is_type(int i, int target);   /* la mision i es de ese objetivo (M_...) */
int missions_skip_price(void);
void missions_skip(int i);
void missions_upgrade_110(void);   /* partidas de antes de la 1.10: el grupo repetible en la lista nueva */

#endif
