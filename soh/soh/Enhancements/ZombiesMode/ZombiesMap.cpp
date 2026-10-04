#include "ZombiesMap.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <libultraship/libultraship.h>

// Scene numbers are enum values from the decomp headers (z64scene / scene_table). If either
// name fails to compile on your tree, replace it with the numeric scene id; the Map Editor can
// also overwrite scene/entrance at runtime ("Use current scene").
#define LONLON_SCENE SCENE_LON_LON_RANCH
#define LONLON_ENTR 0x0157 // Hyrule Field -> Lon Lon Ranch
#define KINO_SCENE SCENE_MARKET_RUINS
#define KINO_ENTR 0 // not set: enter the ruined market yourself (e.g. Temple of Time -> adult), or set it in the editor

namespace {

Vec3f V(float x, float y, float z) { return Vec3f{ x, y, z }; }

// Four spawn points on a ring around a zone centre.
void RingSpawns(MapZone& z, Vec3f c, float r, float phase = 0.4f) {
    for (int i = 0; i < 4; i++) {
        float a = (6.2831853f / 4.0f) * i + phase;
        z.spawns.push_back(V(c.x + std::sin(a) * r, c.y, c.z + std::cos(a) * r));
    }
}

void Place(MapDef& m, StationType st, int zone, float x, float z) {
    m.station[st] = V(x, 0, z);
    m.stationZone[st] = zone;
}

// PLACEHOLDER layout based on the ranch's rough shape. Use the Map Editor to calibrate.
MapDef BuildLonLon() {
    MapDef m;
    m.name = "Lon Lon Ranch";
    m.slug = "lonlon";
    m.description = "Open ranch at night. Four zones, no power switch. Good first map.";
    m.scene = LONLON_SCENE;
    m.entrance = LONLON_ENTR;

    MapZone yard{ "Front Yard", 0, false, V(0, 0, 0), {} };
    RingSpawns(yard, V(0, 0, -300), 650.0f);
    MapZone corral{ "Corral", 750, false, V(0, 0, -700), {} };
    RingSpawns(corral, V(0, 0, -1100), 450.0f);
    MapZone barn{ "Barn & Silo", 1250, false, V(600, 0, -1100), {} };
    RingSpawns(barn, V(900, 0, -1400), 400.0f);
    MapZone house{ "Talon's House", 1750, false, V(-600, 0, -1100), {} };
    RingSpawns(house, V(-900, 0, -1400), 400.0f);
    m.zones = { yard, corral, barn, house };

    Place(m, ST_BOX, 0, 150, -150);
    Place(m, ST_JUGG, 0, -150, -150);
    Place(m, ST_WALL_HAMMER, 0, 0, -350);
    Place(m, ST_SPEED, 1, 200, -1000);
    Place(m, ST_REVIVE, 1, -200, -1000);
    Place(m, ST_WALL_BOW, 1, 0, -1250);
    Place(m, ST_DTAP, 2, 900, -1300);
    Place(m, ST_PAP, 3, -900, -1300);
    return m;
}

// "Kino der Toten" in Hyrule: the ruined Castle Town Market stands in for the bombed-out
// theater. Same flow as the original: start in the lobby, buy your way through the foyer,
// theater and stage, flip the power switch on the stage, then ride the teleporter from the
// theater to the hidden projection room where Pack-a-Punch lives. Layout is a placeholder.
MapDef BuildKino() {
    MapDef m;
    m.name = "Kino der Toten (Ruined Market)";
    m.slug = "kino";
    m.description = "Ruined Castle Town as the theater. Power switch on the stage, teleporter to the "
                    "Pack-a-Punch room. Set the scene/entrance in the Map Editor if the warp doesn't work.";
    m.scene = KINO_SCENE;
    m.entrance = KINO_ENTR;
    m.hasPower = true;

    MapZone lobby{ "Lobby", 0, false, V(0, 0, 0), {} };
    RingSpawns(lobby, V(0, 0, -250), 550.0f);
    MapZone foyer{ "Foyer", 750, false, V(0, 0, -600), {} };
    RingSpawns(foyer, V(0, 0, -950), 450.0f, 0.9f);
    MapZone theater{ "Theater", 1250, false, V(0, 0, -1300), {} };
    RingSpawns(theater, V(0, 0, -1750), 650.0f, 0.2f);
    MapZone stage{ "Stage", 1500, false, V(0, 0, -2050), {} };
    RingSpawns(stage, V(0, 0, -2450), 450.0f, 0.7f);
    MapZone alley{ "Alley", 1000, false, V(800, 0, -1500), {} };
    RingSpawns(alley, V(1150, 0, -1800), 400.0f, 0.5f);
    // Only reachable by teleporter; zombies never spawn in here.
    MapZone projection{ "Projection Room", -1, true, V(-1400, 0, -2700), {} };
    m.zones = { lobby, foyer, theater, stage, alley, projection };

    Place(m, ST_WALL_HAMMER, 0, -150, -350);
    Place(m, ST_WALL_BOW, 0, 150, -350);
    Place(m, ST_BOX, 1, 200, -1000);
    Place(m, ST_JUGG, 1, -200, -1000);
    Place(m, ST_DTAP, 2, -300, -1800);
    Place(m, ST_TELEPORT, 2, 300, -1800);
    Place(m, ST_SPEED, 3, -250, -2500);
    Place(m, ST_POWER, 3, 250, -2500);
    Place(m, ST_REVIVE, 4, 1150, -1850);
    Place(m, ST_PAP, 5, -1400, -2800);
    m.teleportDest = V(-1400, 0, -2650);
    return m;
}

std::vector<MapDef>& Maps() {
    static std::vector<MapDef> maps = [] {
        std::vector<MapDef> v{ BuildLonLon(), BuildKino() };
        for (MapDef& m : v) ZombiesMap_LoadCustom(m);
        return v;
    }();
    return maps;
}

std::string CustomPath(const MapDef& m) {
    return Ship::Context::GetPathRelativeToAppDirectory("zombies_" + m.slug + "_layout.txt");
}

} // namespace

int ZombiesMap_Count() { return (int)Maps().size(); }
MapDef& ZombiesMap_Get(int index) { return Maps()[(size_t)index]; }

MapDef* ZombiesMap_Active() {
    int i = gZombiesCfg.mapMode - 1;
    if (i < 0 || i >= ZombiesMap_Count()) return nullptr;
    return &ZombiesMap_Get(i);
}

void ZombiesMap_ResetToDefault(int index) {
    MapDef fresh = index == 0 ? BuildLonLon() : BuildKino();
    Maps()[(size_t)index] = fresh;
}

// Format:
//   scene <n>  /  entrance <n>  /  teleport x y z
//   zone <index> <doorCost> <noSpawn 0|1> <name with spaces>
//   door <zone> x y z
//   spawn <zone> x y z            (the first "spawn" for a zone clears its default spawns)
//   station <index> <zone> x y z
bool ZombiesMap_SaveCustom(const MapDef& m) {
    std::ofstream f(CustomPath(m));
    if (!f) return false;
    f << "scene " << m.scene << "\nentrance " << m.entrance << "\n";
    f << "teleport " << m.teleportDest.x << " " << m.teleportDest.y << " " << m.teleportDest.z << "\n";
    for (size_t i = 0; i < m.zones.size(); i++) {
        const MapZone& z = m.zones[i];
        f << "zone " << i << " " << z.doorCost << " " << (z.noSpawn ? 1 : 0) << " " << z.name << "\n";
        f << "door " << i << " " << z.door.x << " " << z.door.y << " " << z.door.z << "\n";
        for (const Vec3f& s : z.spawns) f << "spawn " << i << " " << s.x << " " << s.y << " " << s.z << "\n";
    }
    for (int i = 0; i < ST_COUNT; i++)
        f << "station " << i << " " << m.stationZone[i] << " " << m.station[i].x << " " << m.station[i].y << " "
          << m.station[i].z << "\n";
    return true;
}

bool ZombiesMap_LoadCustom(MapDef& m) {
    std::ifstream f(CustomPath(m));
    if (!f) return false;
    std::vector<bool> cleared(m.zones.size(), false);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "scene") {
            ss >> m.scene;
        } else if (tag == "entrance") {
            ss >> m.entrance;
        } else if (tag == "teleport") {
            ss >> m.teleportDest.x >> m.teleportDest.y >> m.teleportDest.z;
        } else if (tag == "zone") {
            size_t i; int cost, ns; std::string name;
            ss >> i >> cost >> ns;
            std::getline(ss, name);
            if (!name.empty() && name[0] == ' ') name.erase(0, 1);
            if (i >= m.zones.size()) { m.zones.resize(i + 1); cleared.resize(i + 1, false); }
            m.zones[i].doorCost = cost;
            m.zones[i].noSpawn = ns != 0;
            if (!name.empty()) m.zones[i].name = name;
        } else if (tag == "door") {
            size_t i; Vec3f p;
            if (ss >> i >> p.x >> p.y >> p.z && i < m.zones.size()) m.zones[i].door = p;
        } else if (tag == "spawn") {
            size_t i; Vec3f p;
            if (ss >> i >> p.x >> p.y >> p.z && i < m.zones.size()) {
                if (!cleared[i]) { m.zones[i].spawns.clear(); cleared[i] = true; }
                m.zones[i].spawns.push_back(p);
            }
        } else if (tag == "station") {
            int i, z; Vec3f p;
            if (ss >> i >> z >> p.x >> p.y >> p.z && i >= 0 && i < ST_COUNT) {
                m.station[i] = p;
                m.stationZone[i] = z;
            }
        }
    }
    return true;
}
