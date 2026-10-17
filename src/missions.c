/* missions.c - Missions.cs: 29 grupos de 3 misiones; al completar un grupo sube el multiplicador */
#include <stdio.h>
#include <string.h>
#include "missions.h"
#include "missions_data.h"
#include "lang.h"

int mission_set;                   /* PlayerInfo.currentMissionSet */
int mission_progress[3];
static int run_progress[3];
static int in_run;
char mission_msg[64];
float mission_msg_t;
extern float game_time;

/* misiones imposibles sin conexion (amigos, retos diarios, letras del dia): se dan por hechas */
static int offline_impossible(int type)
{
    int t = mission_tpl[type].target;
    return t == MT_BeatFriends || t == MT_DailyQuests || t == MT_Letters;
}

int missions_multiplier(void)
{
    int m = mission_set + 1;
    return m > 30 ? 30 : m;
}

int missions_goal(int i) { return mission_set < MISSION_SETS ? mission_sets[mission_set][i].goal : 1; }

static void check_offline(void);

static void set_complete_check(void)
{
    if (mission_set >= MISSION_SETS) return;
    for (int i = 0; i < 3; i++) if (mission_progress[i] < missions_goal(i)) return;
    mission_set++;
    memset(mission_progress, 0, sizeof(mission_progress));
    memset(run_progress, 0, sizeof(run_progress));
    snprintf(mission_msg, sizeof(mission_msg), TR("Multiplicador x%d", "Multiplier x%d"), missions_multiplier());
    mission_msg_t = game_time;
    check_offline();
}

static void complete(int i)
{
    char buf[64];
    missions_text(i, buf, sizeof(buf));
    snprintf(mission_msg, sizeof(mission_msg), TR("Mision: %s", "Mission: %s"), buf);
    mission_msg_t = game_time;
    set_complete_check();
}

static void check_offline(void)
{
    if (mission_set >= MISSION_SETS) return;
    for (int i = 0; i < 3; i++)
        if (offline_impossible(mission_sets[mission_set][i].type)) mission_progress[i] = missions_goal(i);
    set_complete_check();
}

void missions_text(int i, char *buf, int n)
{
    if (mission_set >= MISSION_SETS) { snprintf(buf, n, "%s", TR("Todas las misiones completadas", "All missions completed")); return; }
    int type = mission_sets[mission_set][i].type;
    /* en ingles, el texto original con singular o plural segun el objetivo */
    const char *txt = lang_es ? mission_tpl[type].text : mission_en[type][missions_goal(i) == 1 ? 0 : 1];
    char goal[16];
    snprintf(goal, sizeof(goal), "%d", missions_goal(i));
    const char *p = strstr(txt, "{0}");
    if (!p) { snprintf(buf, n, "%s", txt); return; }
    snprintf(buf, n, "%.*s%s%s", (int)(p - txt), txt, goal, p + 3);
}

int missions_progress(int i)
{
    if (mission_set >= MISSION_SETS) return 0;
    const MissionTpl *t = &mission_tpl[mission_sets[mission_set][i].type];
    if (t->single && in_run && mission_progress[i] < missions_goal(i)) return run_progress[i];
    return mission_progress[i];
}

void missions_did(int target, int magnitude)
{
    if (mission_set >= MISSION_SETS) return;
    for (int i = 0; i < 3; i++) {
        const MissionTpl *t = &mission_tpl[mission_sets[mission_set][i].type];
        int goal = missions_goal(i);
        if ((t->single && !in_run) || t->target != target) continue;
        int prog = mission_progress[i];
        if (t->single && in_run && prog < goal) prog = run_progress[i];
        int np = prog + magnitude;
        if (t->less) {
            if (np > goal) {
                if (t->single && in_run) run_progress[i] = np;
                else mission_progress[i] = np;
                break;
            }
            if (t->single) run_progress[i] = np;
            int was = mission_progress[i];
            mission_progress[i] = goal;
            if (was < goal) complete(i);
        } else if (np < goal) {
            if (t->single) run_progress[i] = np;
            else mission_progress[i] = np;
        } else {
            int was = mission_progress[i];
            mission_progress[i] = goal;
            if (was < goal) complete(i);
        }
        break;
    }
}

void missions_run_start(void) { in_run = 1; memset(run_progress, 0, sizeof(run_progress)); check_offline(); }

void missions_run_end(void)
{
    if (!in_run) return;
    if (mission_set < MISSION_SETS)
        for (int i = 0; i < 3; i++) {
            const MissionTpl *t = &mission_tpl[mission_sets[mission_set][i].type];
            if (t->single && t->less && run_progress[i] < missions_goal(i) && mission_progress[i] < missions_goal(i)) {
                mission_progress[i] = missions_goal(i);
                complete(i);
            }
        }
    in_run = 0;
}

/* Upgrades: saltar mision = 1500 + 100 * grupo actual */
int missions_skip_price(void) { return 1500 + 100 * mission_set; }

void missions_skip(int i)
{
    if (mission_set >= MISSION_SETS || mission_progress[i] >= missions_goal(i)) return;
    mission_progress[i] = missions_goal(i);
    complete(i);
}

int missions_done(int i) { return mission_set >= MISSION_SETS || mission_progress[i] >= missions_goal(i); }
