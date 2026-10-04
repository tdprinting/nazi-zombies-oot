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
    int doorCost = 0;           // 0 = open from the start
    Vec3f door{};               // stand here and use to unlock
    std::vector<Vec3f> spawns;  // zombie spawn points
};

struct MapDef {
    std::string name;
    s16 scene = 0;
    s16 entrance = 0;           // entrance index used by the menu's "Start" warp
    std::vector<MapZone> zones;
    Vec3f station[ST_COUNT]{};
    int stationZone[ST_COUNT]{};
};

// The built-in "Lon Lon Ranch" map (with any saved custom layout applied on top).
MapDef& ZombiesMap_LonLon();
// Map used by the current settings, or nullptr in "any scene" free-play mode.
MapDef* ZombiesMap_Active();

// Custom layout file (text, one record per line). Lives in the SoH app directory.
bool ZombiesMap_SaveCustom(const MapDef& m);
bool ZombiesMap_LoadCustom(MapDef& m);
void ZombiesMap_ResetToDefault(MapDef& m);
