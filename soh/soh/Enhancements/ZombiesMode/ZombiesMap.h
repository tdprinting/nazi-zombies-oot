#pragma once
// Zombies Mode maps. A map is a set of zones (areas unlocked with points, like CoD doors),
// zombie spawn points per zone, and a position for every shop station.
// ALL positions are stored RELATIVE to the "anchor" = Link's position on the first gameplay
// frame after entering the scene, so a layout stays valid as long as you enter the same way.

#include <string>
#include <vector>

extern "C" {
#include "z64.h"
}

#include "ZombiesConfig.h"

struct MapZone {
    std::string name;
    int doorCost = 0;           // 0 = open from the start, <0 = no door (opened some other way, e.g. teleporter)
    bool noSpawn = false;       // zombies never spawn here (e.g. the Pack-a-Punch room)
    Vec3f door{};               // stand here and use to unlock
    std::vector<Vec3f> spawns;  // zombie spawn points
};

struct MapDef {
    std::string name;
    std::string slug;           // file name part: zombies_<slug>_layout.txt
    std::string description;
    int scene = -1;             // OoT scene number, -1 = not set (map inactive until set)
    int entrance = 0;           // entrance index used by the menu's "Start" warp, 0 = not set (no warp)
    bool hasPower = false;      // Kino-style: perks/Pack-a-Punch need the power switch, teleporter exists
    Vec3f teleportDest{};       // where the teleporter sends Link (relative)
    std::vector<MapZone> zones;
    Vec3f station[ST_COUNT]{};
    int stationZone[ST_COUNT]{};
};

// Registry: 0 = Lon Lon Ranch, 1 = Kino der Toten (Ruined Market).
int ZombiesMap_Count();
MapDef& ZombiesMap_Get(int index);
// Map used by the current settings, or nullptr in "any scene" free-play mode.
MapDef* ZombiesMap_Active();

// Custom layout file per map (text, one record per line). Lives in the SoH app directory.
bool ZombiesMap_SaveCustom(const MapDef& m);
bool ZombiesMap_LoadCustom(MapDef& m);
void ZombiesMap_ResetToDefault(int index);
