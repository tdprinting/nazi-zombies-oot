#include "ZombiesMap.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <libultraship/libultraship.h>

#ifndef SCENE_LON_LON_RANCH
#define SCENE_LON_LON_RANCH 0x63
#endif
#ifndef ENTR_LON_LON_RANCH_0
#define ENTR_LON_LON_RANCH_0 0x0157 // Hyrule Field -> Lon Lon Ranch
#endif

namespace {

Vec3f V(float x, float y, float z) { return Vec3f{ x, y, z }; }

// Four spawn points on a ring around a zone centre.
void RingSpawns(MapZone& z, Vec3f c, float r) {
    for (int i = 0; i < 4; i++) {
        float a = (6.2831853f / 4.0f) * i + 0.4f;
        z.spawns.push_back(V(c.x + std::sin(a) * r, c.y, c.z + std::cos(a) * r));
    }
}

// PLACEHOLDER layout based on the ranch's rough shape (open front yard from the Hyrule Field
// gate, then the corral, the barn and silo area, then Talon's house). Coordinates are guesses:
// use the Map Editor tab in the Zombies menu to record real positions and Save them.
MapDef BuildLonLon() {
    MapDef m;
    m.name = "Lon Lon Ranch";
    m.scene = SCENE_LON_LON_RANCH;
    m.entrance = ENTR_LON_LON_RANCH_0;

    MapZone yard{ "Front Yard", 0, V(0, 0, 0), {} };
    RingSpawns(yard, V(0, 0, -300), 650.0f);
    MapZone corral{ "Corral", 750, V(0, 0, -700), {} };
    RingSpawns(corral, V(0, 0, -1100), 450.0f);
    MapZone barn{ "Barn & Silo", 1250, V(600, 0, -1100), {} };
    RingSpawns(barn, V(900, 0, -1400), 400.0f);
    MapZone house{ "Talon's House", 1750, V(-600, 0, -1100), {} };
    RingSpawns(house, V(-900, 0, -1400), 400.0f);
    m.zones = { yard, corral, barn, house };

    auto place = [&](StationType st, int zone, float x, float z) {
        m.station[st] = V(x, 0, z);
        m.stationZone[st] = zone;
    };
    place(ST_BOX, 0, 150, -150);
    place(ST_JUGG, 0, -150, -150);
    place(ST_WALL_HAMMER, 0, 0, -350);
    place(ST_SPEED, 1, 200, -1000);
    place(ST_REVIVE, 1, -200, -1000);
    place(ST_WALL_BOW, 1, 0, -1250);
    place(ST_DTAP, 2, 900, -1300);
    place(ST_PAP, 3, -900, -1300);
    return m;
}

std::string CustomPath() {
    return Ship::Context::GetPathRelativeToAppDirectory("zombies_lonlon_layout.txt");
}

} // namespace

MapDef& ZombiesMap_LonLon() {
    static MapDef map = [] {
        MapDef m = BuildLonLon();
        ZombiesMap_LoadCustom(m);
        return m;
    }();
    return map;
}

MapDef* ZombiesMap_Active() { return gZombiesCfg.mapMode == 1 ? &ZombiesMap_LonLon() : nullptr; }

void ZombiesMap_ResetToDefault(MapDef& m) { m = BuildLonLon(); }

// Format:
//   zone <index> <doorCost> <name with spaces>
//   door <zone> x y z
//   spawn <zone> x y z            (the first "spawn" for a zone clears its default spawns)
//   station <index> <zone> x y z
bool ZombiesMap_SaveCustom(const MapDef& m) {
    std::ofstream f(CustomPath());
    if (!f) return false;
    for (size_t i = 0; i < m.zones.size(); i++) {
        const MapZone& z = m.zones[i];
        f << "zone " << i << " " << z.doorCost << " " << z.name << "\n";
        f << "door " << i << " " << z.door.x << " " << z.door.y << " " << z.door.z << "\n";
        for (const Vec3f& s : z.spawns) f << "spawn " << i << " " << s.x << " " << s.y << " " << s.z << "\n";
    }
    for (int i = 0; i < ST_COUNT; i++)
        f << "station " << i << " " << m.stationZone[i] << " " << m.station[i].x << " " << m.station[i].y << " "
          << m.station[i].z << "\n";
    return true;
}

bool ZombiesMap_LoadCustom(MapDef& m) {
    std::ifstream f(CustomPath());
    if (!f) return false;
    std::vector<bool> cleared(m.zones.size(), false);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "zone") {
            size_t i; int cost; std::string name;
            ss >> i >> cost;
            std::getline(ss, name);
            if (!name.empty() && name[0] == ' ') name.erase(0, 1);
            if (i >= m.zones.size()) { m.zones.resize(i + 1); cleared.resize(i + 1, false); }
            m.zones[i].doorCost = cost;
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
