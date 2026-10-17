/* missions_data.h - generado a partir de Missions.cs del juego original */
enum {MT_none, MT_EarnCoin, MT_SpendCoin, MT_Score, MT_JumpTrain, MT_Jump, MT_Roll, MT_RollLeft, MT_RollCenter, MT_RollRight, MT_RollUnderBarriers, MT_JumpBarriers, MT_DieToTrain, MT_Jetpack, MT_SuperSneakers, MT_Letters, MT_Magnets, MT_MysteryBoxes, MT_BeatFriends, MT_DailyQuests, MT_Tokens, MT_DodgeBarriers, MT_CrashBarriers, MT_CrashTrains, MT_Powerups, MT_Headstart, MT_CoinsWithMagnet, MT_BuyMysterybox, MT_CollectCoinPouch, MT_TimeDeath, MT_BumpTrain, MT_BumpBush, MT_BumpLightSignal, MT_BumpBarrier, MT_HoverBoard, MT_HoverBoardExpire, MT_NoCoinsBeforeScore, MT_Trophies, MT_DoubleMultiplier, MT_ReachMissionSet, MT_GuardJump, MT_HaveUpgrades, MT_HaveHeadStartLarge, MT_HaveKing, MT_HavePinkGirl, MT_HaveNinja, MT_HaveFrank, MT_HaveBlackGirl, MT_PokeFriend, MT_COUNT };
enum {MS_none, MS_EarnCoin, MS_EarnCoinSingleRun, MS_SpendCoin, MS_ScoreCumulative, MS_JumpTrain, MS_JumpTrainSingleRun, MS_Jump, MS_JumpSingleRun, MS_Roll, MS_RollSingleRun, MS_RollLeft, MS_RollCenter, MS_RollRight, MS_RollUnderBarriers, MS_JumpBarriers, MS_DieToTrain, MS_Jetpack, MS_JetpackSingleRun, MS_SuperSneakers, MS_SuperSneakersSingleRun, MS_Letters, MS_Magnets, MS_MagnetsSingleRun, MS_MysteryBoxes, MS_BeatFriends, MS_BeatFriendsSingleRun, MS_DailyQuests, MS_Tokens, MS_DodgeBarriers, MS_CrashBarriers, MS_CrashBarriersSingleRun, MS_CrashTrains, MS_BumpTrainsSingleRun, MS_Powerups, MS_Headstart, MS_CoinsWithMagnet, MS_BuyMysterybox, MS_CollectCoinPouch, MS_TimeDeath, MS_BumpTrain, MS_BumpBush, MS_BumpLightSignal, MS_BumpBarrier, MS_ScoreSingleRun, MS_HoverBoard, MS_HoverBoardExpire, MS_NoCoinsBeforeScore, MS_DoubleMultiplierSingleRun, MS_ReachMissionSet, MS_GuardJump, MS_HaveUpgrades, MS_HaveHeadStartLarge, MS_HaveKing, MS_HavePinkGirl, MS_HaveNinja, MS_HaveFrank, MS_HaveBlackGirl, MS_PokeFriend, MS_COUNT };
typedef struct { int target, single, less; const char *text; } MissionTpl;
static const MissionTpl mission_tpl[MS_COUNT] = {
    {MT_none, 0, 0, ""},
    {MT_EarnCoin, 0, 0, "Recoge {0} monedas"},
    {MT_EarnCoin, 1, 0, "Recoge {0} monedas en una carrera"},
    {MT_SpendCoin, 0, 0, "Gasta {0} monedas"},
    {MT_Score, 0, 0, "Consigue {0} puntos"},
    {MT_JumpTrain, 0, 0, "Salta {0} trenes"},
    {MT_JumpTrain, 1, 0, "Salta {0} trenes en una carrera"},
    {MT_Jump, 0, 0, "Salta {0} veces"},
    {MT_Jump, 1, 0, "Salta {0} veces en una carrera"},
    {MT_Roll, 0, 0, "Rueda {0} veces"},
    {MT_Roll, 1, 0, "Rueda {0} veces en una carrera"},
    {MT_RollLeft, 0, 0, "Rueda {0} veces a la izquierda"},
    {MT_RollCenter, 0, 0, "Rueda {0} veces en el centro"},
    {MT_RollRight, 0, 0, "Rueda {0} veces a la derecha"},
    {MT_RollUnderBarriers, 0, 0, "Pasa rodando {0} barreras"},
    {MT_JumpBarriers, 0, 0, "Salta {0} barreras"},
    {MT_DieToTrain, 0, 0, "Que te atropellen {0} veces"},
    {MT_Jetpack, 0, 0, "Coge {0} jetpacks"},
    {MT_Jetpack, 1, 0, "Coge {0} jetpacks en una carrera"},
    {MT_SuperSneakers, 0, 0, "Coge {0} zapatillas"},
    {MT_SuperSneakers, 1, 0, "Coge {0} zapatillas en una carrera"},
    {MT_Letters, 0, 0, "Coge {0} letras"},
    {MT_Magnets, 0, 0, "Coge {0} imanes"},
    {MT_Magnets, 1, 0, "Coge {0} imanes en una carrera"},
    {MT_MysteryBoxes, 0, 0, "{0} cajas misteriosas"},
    {MT_BeatFriends, 0, 0, "Supera a {0} amigos"},
    {MT_BeatFriends, 1, 0, "Supera a {0} amigos"},
    {MT_DailyQuests, 0, 0, "{0} retos diarios"},
    {MT_Tokens, 0, 0, "Consigue {0} fichas"},
    {MT_DodgeBarriers, 0, 0, "Esquiva {0} barreras"},
    {MT_CrashBarriers, 0, 0, "Choca con {0} barreras"},
    {MT_CrashBarriers, 1, 0, "Choca con {0} barreras"},
    {MT_CrashTrains, 0, 0, "Choca con {0} trenes"},
    {MT_BumpTrain, 1, 0, "Roza {0} trenes en una carrera"},
    {MT_Powerups, 0, 0, "Coge {0} power-ups"},
    {MT_Headstart, 0, 0, "Usa {0} headstarts"},
    {MT_CoinsWithMagnet, 0, 0, "{0} monedas con el iman"},
    {MT_BuyMysterybox, 0, 0, "Compra {0} cajas misteriosas"},
    {MT_CollectCoinPouch, 0, 0, "{0} bolsas de monedas"},
    {MT_TimeDeath, 1, 1, "Que te pillen en {0} s"},
    {MT_BumpTrain, 0, 0, "Roza {0} trenes"},
    {MT_BumpBush, 0, 0, "Tropieza con {0} arbustos"},
    {MT_BumpLightSignal, 0, 0, "Tropieza con {0} semaforos"},
    {MT_BumpBarrier, 0, 0, "Tropieza con {0} barreras"},
    {MT_Score, 1, 0, "{0} puntos en una carrera"},
    {MT_HoverBoard, 0, 0, "Usa {0} hoverboards"},
    {MT_HoverBoardExpire, 0, 0, "{0} hoverboards sin chocar"},
    {MT_NoCoinsBeforeScore, 1, 0, "{0} puntos sin monedas"},
    {MT_DoubleMultiplier, 1, 0, ""},
    {MT_ReachMissionSet, 0, 0, ""},
    {MT_GuardJump, 0, 0, ""},
    {MT_HaveUpgrades, 0, 0, ""},
    {MT_HaveHeadStartLarge, 0, 0, ""},
    {MT_HaveKing, 0, 0, ""},
    {MT_HavePinkGirl, 0, 0, ""},
    {MT_HaveNinja, 0, 0, ""},
    {MT_HaveFrank, 0, 0, ""},
    {MT_HaveBlackGirl, 0, 0, ""},
    {MT_PokeFriend, 0, 0, ""},
};
#define MISSION_SETS 29
static const struct { int type, goal; } mission_sets[MISSION_SETS][3] = {
    {{MS_EarnCoin, 500}, {MS_ScoreSingleRun, 1000}, {MS_Powerups, 2}},
    {{MS_EarnCoinSingleRun, 200}, {MS_Jump, 20}, {MS_SuperSneakers, 2}},
    {{MS_Tokens, 2}, {MS_Roll, 30}, {MS_SpendCoin, 2000}},
    {{MS_DailyQuests, 1}, {MS_DodgeBarriers, 20}, {MS_ScoreSingleRun, 6000}},
    {{MS_EarnCoin, 2500}, {MS_JumpSingleRun, 30}, {MS_BuyMysterybox, 1}},
    {{MS_HoverBoard, 1}, {MS_Magnets, 5}, {MS_BumpBarrier, 4}},
    {{MS_Jetpack, 2}, {MS_BeatFriends, 1}, {MS_Headstart, 1}},
    {{MS_BumpTrainsSingleRun, 8}, {MS_CoinsWithMagnet, 40}, {MS_TimeDeath, 10}},
    {{MS_HoverBoardExpire, 1}, {MS_MysteryBoxes, 2}, {MS_RollSingleRun, 30}},
    {{MS_ScoreSingleRun, 20000}, {MS_Powerups, 12}, {MS_JumpTrain, 2}},
    {{MS_NoCoinsBeforeScore, 4000}, {MS_RollCenter, 50}, {MS_EarnCoin, 5000}},
    {{MS_DailyQuests, 2}, {MS_DodgeBarriers, 40}, {MS_SuperSneakers, 5}},
    {{MS_BumpBush, 2}, {MS_CoinsWithMagnet, 160}, {MS_MagnetsSingleRun, 2}},
    {{MS_MysteryBoxes, 4}, {MS_RollSingleRun, 40}, {MS_EarnCoinSingleRun, 400}},
    {{MS_ScoreCumulative, 100000}, {MS_Jetpack, 5}, {MS_BumpLightSignal, 12}},
    {{MS_DailyQuests, 3}, {MS_SuperSneakersSingleRun, 3}, {MS_JumpTrain, 4}},
    {{MS_ScoreSingleRun, 50000}, {MS_SpendCoin, 4000}, {MS_Magnets, 15}},
    {{MS_JetpackSingleRun, 2}, {MS_BumpTrainsSingleRun, 12}, {MS_CrashTrains, 20}},
    {{MS_HoverBoard, 5}, {MS_MagnetsSingleRun, 3}, {MS_Tokens, 5}},
    {{MS_ScoreCumulative, 250000}, {MS_JumpSingleRun, 40}, {MS_Powerups, 25}},
    {{MS_BumpBarrier, 15}, {MS_BuyMysterybox, 3}, {MS_CoinsWithMagnet, 240}},
    {{MS_DodgeBarriers, 80}, {MS_SpendCoin, 8000}, {MS_SuperSneakersSingleRun, 4}},
    {{MS_Headstart, 8}, {MS_JumpTrain, 10}, {MS_Jetpack, 15}},
    {{MS_MysteryBoxes, 8}, {MS_RollCenter, 200}, {MS_DailyQuests, 4}},
    {{MS_EarnCoin, 15000}, {MS_ScoreSingleRun, 120000}, {MS_JumpTrainSingleRun, 3}},
    {{MS_RollSingleRun, 50}, {MS_ScoreCumulative, 500000}, {MS_NoCoinsBeforeScore, 12000}},
    {{MS_Tokens, 10}, {MS_BuyMysterybox, 6}, {MS_BumpLightSignal, 20}},
    {{MS_JumpSingleRun, 50}, {MS_HoverBoard, 12}, {MS_HoverBoardExpire, 4}},
    {{MS_EarnCoinSingleRun, 750}, {MS_BumpBarrier, 25}, {MS_ScoreSingleRun, 250000}},
};

/* textos originales en ingles (Missions.cs: descriptionSingle / description sin ". {1} left") */
static const char *const mission_en[MS_COUNT][2] = {
    {"", ""},   /* none */
    {"Collect {0} coin", "Collect {0} coins"},   /* EarnCoin */
    {"Collect {0} coin in a one run", "Collect {0} coins in a one run"},   /* EarnCoinSingleRun */
    {"Spend {0} coin", "Spend {0} coins"},   /* SpendCoin */
    {"Collect {0} point", "Collect {0} points"},   /* ScoreCumulative */
    {"Jump over {0} train", "Jump over {0} trains"},   /* JumpTrain */
    {"Jump over {0} train in one run", "Jump over {0} trains in one run"},   /* JumpTrainSingleRun */
    {"Jump {0} time", "Jump {0} times"},   /* Jump */
    {"Jump {0} time in one run", "Jump {0} times in one run"},   /* JumpSingleRun */
    {"Roll {0} time", "Roll {0} times in total"},   /* Roll */
    {"Roll {0} time in a single run", "Roll {0} times in a single run"},   /* RollSingleRun */
    {"Roll {0} time in left lane", "Roll {0} times in left lane"},   /* RollLeft */
    {"Roll {0} time in center lane", "Roll {0} times in center lane"},   /* RollCenter */
    {"Roll {0} time in right lane", "Roll {0} times in right lane"},   /* RollRight */
    {"Roll under {0} barrier", "Roll under {0} barriers"},   /* RollUnderBarriers */
    {"Jump over {0} barrier", "Jump over {0} barriers"},   /* JumpBarriers */
    {"Get run over by {0} train", "Get run over by {0} trains"},   /* DieToTrain */
    {"Pick up {0} Jetpack", "Pick up {0} Jetpacks"},   /* Jetpack */
    {"Pick up {0} Jetpack in one run", "Pick up {0} Jetpacks in one run"},   /* JetpackSingleRun */
    {"Pick up {0} Super Sneaker", "Pick up {0} Super Sneakers"},   /* SuperSneakers */
    {"Pick up {0} Super Sneaker in one run", "Pick up {0} Super Sneakers in one run"},   /* SuperSneakersSingleRun */
    {"Pick up {0} Daily Letter", "Pick up {0} Daily Letters"},   /* Letters */
    {"Pick up {0} Coin Magnet", "Pick up {0} Coin Magnets"},   /* Magnets */
    {"Pick up {0} Magnet in one run", "Pick up {0} Magnets in one run"},   /* MagnetsSingleRun */
    {"Pickup {0} Mystery box", "Pickup {0} Mystery boxes"},   /* MysteryBoxes */
    {"Beat {0} friend", "Beat {0} friends"},   /* BeatFriends */
    {"Beat {0} friend in one run", "Beat {0} friends in one run"},   /* BeatFriendsSingleRun */
    {"Complete {0} Daily Challenge", "Complete {0} Daily Challenges"},   /* DailyQuests */
    {"Get {0} character token", "Get {0} character tokens"},   /* Tokens */
    {"Dodge {0} barrier", "Dodge {0} barriers"},   /* DodgeBarriers */
    {"Crash into {0} barrier", "Crash into {0} barriers"},   /* CrashBarriers */
    {"Crash into {0} barrier in one run", "Crash into {0} barriers in one run"},   /* CrashBarriersSingleRun */
    {"Crash into {0} train", "Crash into {0} trains"},   /* CrashTrains */
    {"Bump into {0} train in one run", "Bump into {0} trains in one run"},   /* BumpTrainsSingleRun */
    {"Pickup {0} powerup", "Pickup {0} powerups"},   /* Powerups */
    {"Use {0} Headstart", "Use {0} Headstarts"},   /* Headstart */
    {"Pickup {0} coin with a Magnet", "Pickup {0} coins with a Magnet"},   /* CoinsWithMagnet */
    {"Buy {0} Mystery box", "Buy {0} Mystery boxes"},   /* BuyMysterybox */
    {"Pickup {0} coin pouch", "Pickup {0} coin pouches in total"},   /* CollectCoinPouch */
    {"Get caught in first {0} second of run", "Get caught in first {0} seconds of run"},   /* TimeDeath */
    {"Bump {0} train", "Bump {0} trains"},   /* BumpTrain */
    {"Bump {0} bush", "Bump {0} bushes"},   /* BumpBush */
    {"Bump into {0} Light Signal", "Bump into {0} Light Signals"},   /* BumpLightSignal */
    {"Stumble into {0} Barrier", "Stumble into {0} Barriers"},   /* BumpBarrier */
    {"Score {0} point in single run", "Score {0} points in single run"},   /* ScoreSingleRun */
    {"Use {0} Hoverboard", "Use {0} Hoverboards"},   /* HoverBoard */
    {"Use {0} Hoverboard without crashing", "Use {0} Hoverboards without crashing"},   /* HoverBoardExpire */
    {"Score {0} point without collecting coins", "Score {0} points without collecting coins"},   /* NoCoinsBeforeScore */
    {"", ""},   /* DoubleMultiplierSingleRun */
    {"", ""},   /* ReachMissionSet */
    {"", ""},   /* GuardJump */
    {"", ""},   /* HaveUpgrades */
    {"", ""},   /* HaveHeadStartLarge */
    {"", ""},   /* HaveKing */
    {"", ""},   /* HavePinkGirl */
    {"", ""},   /* HaveNinja */
    {"", ""},   /* HaveFrank */
    {"", ""},   /* HaveBlackGirl */
    {"", ""},   /* PokeFriend */
};
