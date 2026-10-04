#include "ZombiesConfig.h"

#include <cstdio>
#include <libultraship/bridge.h>

ZombiesConfig gZombiesCfg;

namespace {
void I(const char* k, int& v, bool load) {
    char key[64];
    snprintf(key, sizeof(key), "gZombies.%s", k);
    if (load) v = CVarGetInteger(key, v);
    else CVarSetInteger(key, v);
}
void B(const char* k, bool& v, bool load) {
    int t = v;
    I(k, t, load);
    v = t != 0;
}
void F(const char* k, float& v, bool load) {
    char key[64];
    snprintf(key, sizeof(key), "gZombies.%s", k);
    if (load) v = CVarGetFloat(key, v);
    else CVarSetFloat(key, v);
}
void Sync(bool load) {
    ZombiesConfig& c = gZombiesCfg;
    char k[48];
    B("Enabled", c.enabled, load);
    I("MapMode", c.mapMode, load);
    B("ForceNight", c.forceNight, load);
    I("Preset", c.preset, load);
    I("StartPoints", c.startPoints, load);
    I("HitPoints", c.hitPoints, load);
    I("KillPoints", c.killPoints, load);
    I("MaxAlive", c.maxAlive, load);
    I("IntermissionSec", c.intermissionSec, load);
    I("BaseZombies", c.baseZombies, load);
    I("ZombiesPerRound", c.zombiesPerRound, load);
    F("HealthMult", c.healthMult, load);
    I("DogInterval", c.dogInterval, load);
    B("UseRedead", c.useRedead, load);
    B("UseStalchild", c.useStalchild, load);
    I("StalchildFromRound", c.stalchildFromRound, load);
    I("PowerupChance", c.powerupChance, load);
    I("PowerupMaxPerRound", c.powerupMaxPerRound, load);
    I("PowerupSec", c.powerupSec, load);
    I("JuggHearts", c.juggHearts, load);
    B("QuickReviveOnce", c.quickReviveOnce, load);
    I("StartWeapon", c.startWeapon, load);
    F("DamageMult", c.damageMult, load);
    I("KeyFire", c.keyFire, load);
    I("KeySwap", c.keySwap, load);
    I("KeyUse", c.keyUse, load);
    B("HudOn", c.hudOn, load);
    F("HudScale", c.hudScale, load);
    B("ShowHints", c.showHints, load);
    for (int i = 0; i < PU_COUNT; i++) { snprintf(k, sizeof(k), "PowerupOn%d", i); B(k, c.powerupOn[i], load); }
    for (int i = 0; i < W_COUNT; i++) { snprintf(k, sizeof(k), "BoxWeight%d", i); I(k, c.boxWeight[i], load); }
    for (int i = 0; i < ST_COUNT; i++) {
        snprintf(k, sizeof(k), "StationCost%d", i); I(k, c.stationCost[i], load);
        snprintf(k, sizeof(k), "StationOn%d", i); B(k, c.stationOn[i], load);
    }
    for (int i = 0; i < 4; i++) {
        snprintf(k, sizeof(k), "ColRound%d", i); F(k, c.colRound[i], load);
        snprintf(k, sizeof(k), "ColPoints%d", i); F(k, c.colPoints[i], load);
        snprintf(k, sizeof(k), "ColText%d", i); F(k, c.colText[i], load);
    }
}
} // namespace

void ZombiesConfig_Load() { Sync(true); }
void ZombiesConfig_Save() {
    Sync(false);
    CVarSave();
}

void ZombiesConfig_ResetDefaults() {
    bool en = gZombiesCfg.enabled;
    gZombiesCfg = ZombiesConfig{};
    gZombiesCfg.enabled = en;
}

void ZombiesConfig_ApplyPreset(int preset) {
    ZombiesConfig& c = gZombiesCfg;
    // Only touch the difficulty knobs; leave controls/HUD/shop prices alone.
    struct P { int startPts, maxAlive, base, perRound, dog, pu; float hp, dmg; };
    static const P presets[kPresetCount] = {
        { 1500, 16, 3, 2, 0, 8, 0.7f, 1.3f },   // Easy
        { 500, 24, 4, 3, 5, 4, 1.0f, 1.0f },    // Normal
        { 500, 30, 6, 4, 4, 3, 1.4f, 1.0f },    // Hard
        { 0, 40, 10, 6, 3, 2, 2.0f, 0.8f },     // Insane
        { 500, 24, 6, 6, 5, 4, 1.0f, 1.0f },    // Classic CoD
    };
    if (preset < 0 || preset >= kPresetCount) return;
    const P& p = presets[preset];
    c.preset = preset;
    c.startPoints = p.startPts;
    c.maxAlive = p.maxAlive;
    c.baseZombies = p.base;
    c.zombiesPerRound = p.perRound;
    c.dogInterval = p.dog;
    c.powerupChance = p.pu;
    c.healthMult = p.hp;
    c.damageMult = p.dmg;
}
