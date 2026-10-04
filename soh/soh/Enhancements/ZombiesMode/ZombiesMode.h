#pragma once

extern "C" {
#include "z64.h"
}

// Call once from SoH startup (next to the other Enhancement registrations).
// Zombies Mode is toggled from its own menu (F8) and saved as CVar "gZombies.Enabled".
void ZombiesMode_Register();

struct ZombiesStatus {
    bool active = false;       // mode is on AND we are in a valid scene
    bool inGame = false;       // a save file is loaded (gPlayState exists)
    bool sceneMatches = false; // currently inside the selected map's scene
    int round = 0, points = 0, alive = 0, toSpawn = 0;
};

ZombiesStatus ZombiesMode_GetStatus();
bool ZombiesMode_GetPlayerRel(Vec3f* rel); // Link's position relative to the map anchor
void ZombiesMode_StartMatch();             // enables the mode and warps to the map if needed
void ZombiesMode_RestartMatch();
