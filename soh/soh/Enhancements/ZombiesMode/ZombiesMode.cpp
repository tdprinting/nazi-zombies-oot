// Zombies Mode: Call-of-Duty-Zombies-style survival for Ship of Harkinian.
//
//   Zombies         -> Redeads (Stalchildren later), Wolfos on "dog rounds"
//   M1911/Olympia/Crossbow/Sniper -> Fairy Slingshot / Megaton Hammer / Fairy Bow / Hookshot
//   RAY GUN         -> "Light Ray": Light Arrow energy, green bolt, splash damage
//   Mystery Box     -> Treasure Chest        Pack-a-Punch -> Great Fairy
//   Perks           -> fairies               Power-ups -> auto-collected on drop
//   Doors           -> zone gates you unlock with points (Lon Lon Ranch map)
//
// Everything is configurable from the in-game menu (F8), see ZombiesMenu.cpp.
// NOTE: written against the SoH 8.x GameInteractor API from memory, NOT compiled.

#include "ZombiesMode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <unordered_map>
#include <vector>

#include <libultraship/bridge.h>
#include <imgui.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "ZombiesConfig.h"
#include "ZombiesMap.h"
#include "ZombiesMenu.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kFps = 20; // OoT game logic rate
constexpr float kStationRing = 240.0f; // free-play layout
constexpr float kUseRange = 70.0f;
constexpr int kMaxHeartsCap = 20 * 16;

const u16 kButtonMasks[kButtonCount] = { BTN_R,    BTN_L,     BTN_DUP,    BTN_DDOWN,  BTN_DLEFT,
                                         BTN_DRIGHT, BTN_CUP, BTN_CDOWN, BTN_CLEFT, BTN_CRIGHT };

// ---------------------------------------------------------------- weapons
struct WeaponDef {
    const char* name;
    int damage;     // native enemy-health points per hit
    int cooldown;   // frames between shots
    float range;
    int coneBinang; // half-angle, 0x10000 = 360deg
    float splash;
    int mag;
    int reserve;
    u16 sfx;
};

const WeaponDef kWeapons[W_COUNT] = {
    { "Fairy Slingshot (M1911)", 2, 8, 600.0f, 0x0500, 0.0f, 8, 80, NA_SE_IT_SLING_SHOOT },
    { "Fairy Bow (Crossbow)", 5, 14, 900.0f, 0x0400, 0.0f, 6, 36, NA_SE_IT_ARROW_SHOOT },
    { "Megaton Hammer (Shotgun)", 7, 18, 170.0f, 0x2000, 0.0f, 6, 36, NA_SE_IT_HAMMER_SWING },
    { "Hookshot (Sniper)", 12, 28, 1400.0f, 0x0200, 0.0f, 5, 30, NA_SE_IT_HOOKSHOT_STICK_OBJ },
    { "RAY GUN", 10, 8, 1100.0f, 0x0400, 90.0f, 20, 160, NA_SE_IT_MAGIC_ARROW_SHOOT },
};

struct Weapon {
    int id = -1; // -1 = empty slot
    bool punched = false;
    int mag = 0;
    int reserve = 0;
    int reload = 0;
};

const char* kPerkNames[P_COUNT] = { "Juggernog", "Speed Cola", "Double Tap", "Quick Revive" };

struct Zombie {
    int lastHealth = 0;
};

struct State {
    int round = 0;
    int points = 0;
    int toSpawn = 0;
    int intermission = 0;
    int spawnCooldown = 0;
    int powerupsThisRound = 0;
    bool dogRound = false;
    bool dogBonusGiven = false;

    bool anchorSet = false;
    bool nightSet = false;
    bool power = false;
    int teleportBack = 0;   // frames until Link is sent back from the teleporter
    Vec3f teleportReturn{};
    Vec3f anchor{};
    std::vector<bool> unlocked; // per zone of the active map
    Vec3f stationPos[ST_COUNT]{};
    int stationZone[ST_COUNT]{};

    Weapon slots[2];
    int cur = 0;
    int fireCooldown = 0;
    int flash = 0;
    bool perks[P_COUNT]{};

    int instaKill = 0;
    int doublePts = 0;
    char banner[48]{};
    int bannerFrames = 0;
    char toast[80]{};
    int toastFrames = 0;

    std::unordered_map<Actor*, Zombie> zombies;
};

State S;
std::mt19937 rng{ std::random_device{}() };

float Rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
int RandInt(int n) { return (int)(rng() % (unsigned)n); }
const ZombiesConfig& C() { return gZombiesCfg; }
int Inter() { return C().intermissionSec * kFps; }

bool Enabled() {
    if (!C().enabled || gPlayState == nullptr) return false;
    if (C().mapMode == 1) {
        MapDef* m = ZombiesMap_Active();
        return m != nullptr && gPlayState->sceneNum == m->scene;
    }
    return true;
}

bool InGameplay() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gPlayState->state.running &&
           gPlayState->pauseCtx.state == 0;
}

void Play(u16 sfx) {
    Audio_PlaySoundGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}
void Toast(const char* msg) {
    snprintf(S.toast, sizeof(S.toast), "%s", msg);
    S.toastFrames = kFps * 3;
}
void Banner(const char* msg) {
    snprintf(S.banner, sizeof(S.banner), "%s", msg);
    S.bannerFrames = kFps * 3;
}

void GiveWeapon(Weapon& w, int id) {
    w = Weapon{};
    w.id = id;
    w.mag = kWeapons[id].mag;
    w.reserve = kWeapons[id].reserve;
}

void Reset() {
    S = State{};
    S.points = C().startPoints;
    S.intermission = Inter();
    GiveWeapon(S.slots[0], std::clamp(C().startWeapon, 0, (int)W_COUNT - 1));
}

Vec3f Abs(const Vec3f& rel) { return Vec3f{ S.anchor.x + rel.x, S.anchor.y + rel.y, S.anchor.z + rel.z }; }

int Damage(const Weapon& w) {
    float d = (float)kWeapons[w.id].damage * C().damageMult;
    if (w.punched) d *= 2.5f;
    if (S.perks[P_DTAP]) d *= 1.5f;
    return std::max(1, (int)std::ceil(d));
}

int Points(int base) { return S.doublePts > 0 ? base * 2 : base; }

// ---------------------------------------------------------------- zombies
int HealthForRound(int round) {
    int base = 6 + std::min(round, 10) * 2 + std::max(0, round - 10);
    return std::clamp((int)std::lround(base * C().healthMult), 1, 250);
}
int CountForRound(int round) { return C().baseZombies + round * C().zombiesPerRound; }

bool PickSpawn(Player* player, Vec3f* out) {
    MapDef* m = ZombiesMap_Active();
    std::vector<Vec3f> cands;
    if (m != nullptr) {
        for (size_t z = 0; z < m->zones.size() && z < S.unlocked.size(); z++)
            if (S.unlocked[z] && !m->zones[z].noSpawn)
                for (const Vec3f& s : m->zones[z].spawns) cands.push_back(Abs(s));
    }
    if (cands.empty()) { // free play (or empty zone): ring around Link
        float ang = Rand(0.0f, 6.2831853f);
        float dist = Rand(350.0f, 650.0f);
        *out = player->actor.world.pos;
        out->x += std::sin(ang) * dist;
        out->z += std::cos(ang) * dist;
        return true;
    }
    // prefer points at least 250 units from Link; otherwise the farthest one
    std::vector<Vec3f> far;
    Vec3f farthest = cands[0];
    float fd = -1.0f;
    for (const Vec3f& c : cands) {
        float d = Math_Vec3f_DistXZ(&player->actor.world.pos, const_cast<Vec3f*>(&c));
        if (d >= 250.0f) far.push_back(c);
        if (d > fd) { fd = d; farthest = c; }
    }
    *out = far.empty() ? farthest : far[RandInt((int)far.size())];
    return true;
}

void SpawnZombie(Player* player) {
    Vec3f pos;
    PickSpawn(player, &pos);
    pos.y += 30.0f; // gravity drops it to the floor

    s16 id = ACTOR_EN_RD;
    if (S.dogRound) id = ACTOR_EN_WF;
    else if (C().useStalchild && S.round >= C().stalchildFromRound && (!C().useRedead || RandInt(3) == 0))
        id = ACTOR_EN_SKB;

    s16 faceLink = Math_Vec3f_Yaw(&pos, &player->actor.world.pos);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, id, pos.x, pos.y, pos.z, 0, faceLink, 0, 0, false);
    if (a == nullptr) return;
    a->colChkInfo.health = (u8)HealthForRound(S.round);
    S.zombies[a] = Zombie{ a->colChkInfo.health };
}

void KillZombie(Actor* a) {
    a->colChkInfo.health = 0;
    Play(NA_SE_EN_REDEAD_DEAD);
    Actor_Kill(a);
}

void RefillAmmo() {
    for (auto& w : S.slots)
        if (w.id >= 0) {
            w.mag = kWeapons[w.id].mag;
            w.reserve = kWeapons[w.id].reserve * (w.punched ? 2 : 1);
        }
}

void ApplyPowerup(Powerup p) {
    Banner(kPowerupNames[p]);
    Play(NA_SE_SY_GET_ITEM);
    switch (p) {
        case PU_MAXAMMO: RefillAmmo(); break;
        case PU_INSTAKILL: S.instaKill = C().powerupSec * kFps; break;
        case PU_DOUBLEPTS: S.doublePts = C().powerupSec * kFps; break;
        case PU_NUKE: {
            std::vector<Actor*> all;
            for (auto& kv : S.zombies) all.push_back(kv.first);
            for (Actor* a : all) KillZombie(a);
            S.points += Points(400);
            break;
        }
        default: break;
    }
}

void MaybeDropPowerup() {
    if (S.powerupsThisRound >= C().powerupMaxPerRound || RandInt(100) >= C().powerupChance) return;
    std::vector<int> on;
    for (int i = 0; i < PU_COUNT; i++)
        if (C().powerupOn[i]) on.push_back(i);
    if (on.empty()) return;
    S.powerupsThisRound++;
    ApplyPowerup((Powerup)on[RandInt((int)on.size())]);
}

// Zombies that left the enemy list died (sword, bombs, ray gun, nuke...).
void ScanZombies() {
    std::unordered_map<Actor*, bool> alive;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) alive[a] = true;

    int kills = 0;
    for (auto it = S.zombies.begin(); it != S.zombies.end();) {
        Actor* a = it->first;
        if (!alive.count(a)) {
            kills++;
            it = S.zombies.erase(it);
            continue;
        }
        if (S.instaKill > 0) a->colChkInfo.health = std::min<u8>(a->colChkInfo.health, 1);
        if (a->colChkInfo.health < it->second.lastHealth) S.points += Points(C().hitPoints);
        it->second.lastHealth = a->colChkInfo.health;
        ++it;
    }
    for (int i = 0; i < kills; i++) {
        S.points += Points(C().killPoints);
        MaybeDropPowerup();
    }
}

// ---------------------------------------------------------------- shooting
void DamageActor(Actor* a, int dmg) {
    if (S.instaKill > 0) dmg = 9999;
    int hp = (int)a->colChkInfo.health - dmg;
    if (hp <= 0) KillZombie(a);
    else a->colChkInfo.health = (u8)hp;
}

void Fire(Player* player) {
    Weapon& w = S.slots[S.cur];
    if (w.id < 0 || S.fireCooldown > 0 || w.reload > 0) return;
    const WeaponDef& def = kWeapons[w.id];
    if (w.mag <= 0) {
        if (w.reserve > 0) {
            w.reload = (S.perks[P_SPEED] ? 20 : 40);
            int take = std::min(def.mag, w.reserve);
            w.mag = take;
            w.reserve -= take;
        }
        return;
    }
    w.mag--;
    S.fireCooldown = S.perks[P_SPEED] ? std::max(2, def.cooldown / 2) : def.cooldown;
    Play(def.sfx);
    if (w.id == W_RAYGUN) S.flash = 3;

    Actor* best = nullptr;
    float bestDist = def.range;
    for (auto& kv : S.zombies) {
        Actor* a = kv.first;
        float d = Math_Vec3f_DistXZ(&player->actor.world.pos, &a->world.pos);
        if (d > bestDist) continue;
        s16 toZ = Math_Vec3f_Yaw(&player->actor.world.pos, &a->world.pos);
        if (std::abs((s16)(toZ - player->actor.shape.rot.y)) > def.coneBinang) continue;
        if (std::fabs(a->world.pos.y - player->actor.world.pos.y) > 120.0f) continue;
        best = a;
        bestDist = d;
    }
    if (best == nullptr) return;

    int dmg = Damage(w);
    std::vector<Actor*> victims{ best };
    if (def.splash > 0.0f)
        for (auto& kv : S.zombies)
            if (kv.first != best && Math_Vec3f_DistXZ(&best->world.pos, &kv.first->world.pos) <= def.splash)
                victims.push_back(kv.first);
    for (Actor* v : victims) DamageActor(v, dmg);
}

// ---------------------------------------------------------------- map / stations
void SpawnMarker(s16 actorId, s16 params, const Vec3f& p) {
    // Visual only. Params are guesses: adjust if the chest/fairies look wrong in-game.
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, p.x, p.y, p.z, 0, 0, 0, params, false);
}

void SetupMap(Player* player) {
    S.anchor = player->actor.world.pos;
    MapDef* m = ZombiesMap_Active();
    if (m != nullptr) {
        S.unlocked.assign(m->zones.size(), false);
        for (size_t i = 0; i < m->zones.size(); i++) S.unlocked[i] = m->zones[i].doorCost == 0;
        for (int i = 0; i < ST_COUNT; i++) {
            S.stationPos[i] = Abs(m->station[i]);
            S.stationZone[i] = m->stationZone[i];
        }
    } else {
        S.unlocked.assign(1, true);
        for (int i = 0; i < ST_COUNT; i++) {
            float ang = (6.2831853f / ST_COUNT) * i;
            S.stationPos[i] = S.anchor;
            S.stationPos[i].x += std::sin(ang) * kStationRing;
            S.stationPos[i].z += std::cos(ang) * kStationRing;
            S.stationZone[i] = 0;
        }
    }
    for (int i = 0; i < ST_COUNT; i++) {
        if (!StationAvailable(i)) continue;
        if (i == ST_BOX) SpawnMarker(ACTOR_EN_BOX, 0x0000, S.stationPos[i]);
        else if (i == ST_PAP) SpawnMarker(ACTOR_EN_ELF, 0x0004, S.stationPos[i]); // Great-Fairy-ish
        else SpawnMarker(ACTOR_EN_ELF, 0x0000, S.stationPos[i]);
    }
    S.anchorSet = true;
}

bool ZoneOpen(int z) { return z < 0 || z >= (int)S.unlocked.size() || S.unlocked[z]; }

// Power switch / teleporter only exist on maps with hasPower (Kino).
bool StationAvailable(int i) {
    if (!C().stationOn[i]) return false;
    if (i == ST_POWER || i == ST_TELEPORT) {
        MapDef* m = ZombiesMap_Active();
        return m != nullptr && m->hasPower;
    }
    return true;
}

bool NeedsPower(int st) { return st == ST_PAP || (st >= ST_JUGG && st <= ST_REVIVE); }

bool PowerRequired() {
    MapDef* m = ZombiesMap_Active();
    return m != nullptr && m->hasPower && !S.power;
}

int NearStation(Player* player) {
    for (int i = 0; i < ST_COUNT; i++)
        if (StationAvailable(i) && ZoneOpen(S.stationZone[i]) &&
            Math_Vec3f_DistXZ(&player->actor.world.pos, &S.stationPos[i]) <= kUseRange)
            return i;
    return -1;
}

int NearDoor(Player* player) {
    MapDef* m = ZombiesMap_Active();
    if (m == nullptr) return -1;
    for (size_t z = 0; z < m->zones.size() && z < S.unlocked.size(); z++) {
        if (S.unlocked[z] || m->zones[z].doorCost < 0) continue; // negative = no door
        Vec3f d = Abs(m->zones[z].door);
        if (Math_Vec3f_DistXZ(&player->actor.world.pos, &d) <= kUseRange) return (int)z;
    }
    return -1;
}

void GiveToHand(int id) {
    Weapon& cur = S.slots[S.cur];
    int slot = S.slots[1].id < 0 ? 1 : S.cur; // fill empty slot first, else replace current
    if (cur.id < 0) slot = S.cur;
    GiveWeapon(S.slots[slot], id);
    S.cur = slot;
}

int PickBoxWeapon() {
    int total = 0;
    for (int i = 0; i < W_COUNT; i++) total += std::max(0, C().boxWeight[i]);
    if (total <= 0) return W_SLINGSHOT;
    int r = RandInt(total);
    for (int i = 0; i < W_COUNT; i++) {
        r -= std::max(0, C().boxWeight[i]);
        if (r < 0) return i;
    }
    return W_SLINGSHOT;
}

void UseDoor(int zone) {
    MapDef* m = ZombiesMap_Active();
    int cost = m->zones[zone].doorCost;
    if (S.points < cost) { Toast("Not enough points"); Play(NA_SE_SY_ERROR); return; }
    S.points -= cost;
    S.unlocked[zone] = true;
    char b[80];
    snprintf(b, sizeof(b), "Unlocked: %s", m->zones[zone].name.c_str());
    Banner(b);
    Play(NA_SE_SY_GET_ITEM);
}

void Teleport(Player* player, const Vec3f& dest) {
    player->actor.world.pos = dest;
    player->actor.prevPos = dest;
    player->actor.speedXZ = 0.0f;
}

void UseStation(int st) {
    int cost = C().stationCost[st];
    if (NeedsPower(st) && PowerRequired()) { Toast("The power is off. Find the power switch!"); Play(NA_SE_SY_ERROR); return; }
    if (S.points < cost) { Toast("Not enough points"); Play(NA_SE_SY_ERROR); return; }
    switch (st) {
        case ST_BOX: {
            int id = PickBoxWeapon();
            GiveToHand(id);
            char b[80];
            snprintf(b, sizeof(b), "Mystery Box: %s!", kWeapons[id].name);
            Toast(b);
            break;
        }
        case ST_PAP: {
            Weapon& w = S.slots[S.cur];
            if (w.id < 0 || w.punched) { Toast("Nothing to upgrade"); return; }
            w.punched = true;
            w.mag = kWeapons[w.id].mag;
            w.reserve = kWeapons[w.id].reserve * 2;
            Toast("Pack-a-Punched! (2.5x damage)");
            break;
        }
        case ST_JUGG:
        case ST_SPEED:
        case ST_DTAP:
        case ST_REVIVE: {
            Perk p = (Perk)(st - ST_JUGG);
            if (S.perks[p]) { Toast("Already owned"); return; }
            S.perks[p] = true;
            if (p == P_JUGG) {
                gSaveContext.healthCapacity =
                    std::min<int>(kMaxHeartsCap, gSaveContext.healthCapacity + C().juggHearts * 16);
                gSaveContext.health = gSaveContext.healthCapacity;
            }
            char b[80];
            snprintf(b, sizeof(b), "Perk: %s", kPerkNames[p]);
            Toast(b);
            break;
        }
        case ST_WALL_HAMMER: GiveToHand(W_HAMMER); Toast("Bought Megaton Hammer"); break;
        case ST_WALL_BOW: GiveToHand(W_BOW); Toast("Bought Fairy Bow"); break;
        case ST_POWER:
            if (S.power) { Toast("Power is already on"); return; }
            S.power = true;
            Banner("POWER ON");
            break;
        case ST_TELEPORT: {
            MapDef* m = ZombiesMap_Active();
            if (m == nullptr || S.teleportBack > 0) return;
            if (!S.power) { Toast("The teleporter needs power"); return; }
            Player* player = GET_PLAYER(gPlayState);
            S.teleportReturn = player->actor.world.pos;
            S.teleportBack = 30 * kFps;
            for (size_t z = 0; z < m->zones.size() && z < S.unlocked.size(); z++)
                if (m->zones[z].doorCost < 0) S.unlocked[z] = true; // teleporter-only zones open up
            Teleport(player, Abs(m->teleportDest));
            Banner("TELEPORTING...");
            break;
        }
    }
    S.points -= cost;
    Play(NA_SE_SY_GET_ITEM);
}

// ---------------------------------------------------------------- main loop
void OnFrame() {
    if (!Enabled() || !InGameplay()) return;
    Player* player = GET_PLAYER(gPlayState);

    if (!S.anchorSet) SetupMap(player);
    if (C().forceNight && !S.nightSet) {
        gSaveContext.dayTime = 0x0000; // midnight
        gSaveContext.skyboxTime = 0x0000;
        S.nightSet = true;
    }

    if (gSaveContext.health <= 0) {
        if (S.perks[P_REVIVE]) { // Quick Revive: self-revive
            if (C().quickReviveOnce) S.perks[P_REVIVE] = false;
            gSaveContext.health = gSaveContext.healthCapacity;
            Banner("QUICK REVIVE");
        } else {
            Reset();
            return;
        }
    }

    if (S.fireCooldown > 0) S.fireCooldown--;
    if (S.instaKill > 0) S.instaKill--;
    if (S.doublePts > 0) S.doublePts--;
    if (S.toastFrames > 0) S.toastFrames--;
    if (S.bannerFrames > 0) S.bannerFrames--;
    if (S.flash > 0) S.flash--;
    for (auto& w : S.slots)
        if (w.reload > 0) w.reload--;

    ScanZombies();

    if (S.teleportBack > 0 && --S.teleportBack == 0) {
        Teleport(player, S.teleportReturn);
        Banner("BACK TO THE THEATER");
        Play(NA_SE_SY_CORRECT_CHIME);
    }

    Input* in = &gPlayState->state.input[0];
    if (in->cur.button & kButtonMasks[std::clamp(C().keyFire, 0, kButtonCount - 1)]) Fire(player);
    if ((in->press.button & kButtonMasks[std::clamp(C().keySwap, 0, kButtonCount - 1)]) && S.slots[1 - S.cur].id >= 0)
        S.cur = 1 - S.cur;
    if (in->press.button & kButtonMasks[std::clamp(C().keyUse, 0, kButtonCount - 1)]) {
        int st = NearStation(player);
        if (st >= 0) UseStation(st);
        else {
            int d = NearDoor(player);
            if (d >= 0) UseDoor(d);
        }
    }

    int alive = (int)S.zombies.size();
    if (S.toSpawn == 0 && alive == 0) {
        if (S.round > 0 && S.dogRound && !S.dogBonusGiven) { // dog rounds end with Max Ammo
            S.dogBonusGiven = true;
            ApplyPowerup(PU_MAXAMMO);
        }
        if (S.intermission > 0) { S.intermission--; return; }
        S.round++;
        S.dogRound = C().dogInterval > 0 && (S.round % C().dogInterval == 0);
        S.dogBonusGiven = false;
        S.powerupsThisRound = 0;
        S.toSpawn = S.dogRound ? 6 + S.round / std::max(1, C().dogInterval) * 2 : CountForRound(S.round);
        S.intermission = Inter();
        Play(NA_SE_SY_CORRECT_CHIME);
    }
    if (S.toSpawn > 0 && alive < C().maxAlive && --S.spawnCooldown <= 0) {
        SpawnZombie(player);
        S.toSpawn--;
        S.spawnCooldown = 10;
    }
}

// ---------------------------------------------------------------- HUD
ImU32 Col(const float c[4], float alphaMul = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alphaMul));
}

void DrawHud() {
    if (!Enabled() || !InGameplay() || !C().hudOn) return;
    const float k = C().hudScale;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 sz = ImGui::GetIO().DisplaySize;
    ImFont* f = ImGui::GetFont();
    char b[128];

    if (S.flash > 0) { // Ray Gun bolt + glow
        dl->AddLine(ImVec2(sz.x * 0.62f, sz.y * 0.80f), ImVec2(sz.x * 0.5f, sz.y * 0.5f), IM_COL32(80, 255, 120, 220),
                    6.0f * k);
        dl->AddCircleFilled(ImVec2(sz.x * 0.5f, sz.y * 0.5f), 18.0f * k, IM_COL32(120, 255, 160, 160));
    }

    snprintf(b, sizeof(b), "%d", S.round);
    dl->AddText(f, 72.0f * k, ImVec2(24.0f, sz.y - 110.0f * k), Col(C().colRound), b);
    snprintf(b, sizeof(b), "%d", S.points);
    dl->AddText(f, 34.0f * k, ImVec2(24.0f, sz.y - 150.0f * k - 20.0f), Col(C().colPoints), b);

    const Weapon& w = S.slots[S.cur];
    if (w.id >= 0) {
        snprintf(b, sizeof(b), "%s%s", w.punched ? "[PaP] " : "", kWeapons[w.id].name);
        dl->AddText(f, 22.0f * k, ImVec2(sz.x - 400.0f * k, sz.y - 92.0f * k), Col(C().colText), b);
        snprintf(b, sizeof(b), w.reload > 0 ? "RELOADING" : "%d / %d", w.mag, w.reserve);
        dl->AddText(f, 34.0f * k, ImVec2(sz.x - 400.0f * k, sz.y - 64.0f * k), Col(C().colText, 1.1f), b);
    }

    float py = 20.0f;
    const float px = sz.x - 210.0f * k;
    for (int p = 0; p < P_COUNT; p++)
        if (S.perks[p]) {
            dl->AddText(f, 20.0f * k, ImVec2(px, py), IM_COL32(120, 200, 255, 230), kPerkNames[p]);
            py += 22.0f * k;
        }
    if (S.instaKill > 0) {
        snprintf(b, sizeof(b), "INSTA-KILL %ds", S.instaKill / kFps);
        dl->AddText(f, 20.0f * k, ImVec2(px, py), IM_COL32(255, 80, 80, 230), b);
        py += 22.0f * k;
    }
    if (S.doublePts > 0) {
        snprintf(b, sizeof(b), "2X POINTS %ds", S.doublePts / kFps);
        dl->AddText(f, 20.0f * k, ImVec2(px, py), Col(C().colPoints), b);
    }

    {
        MapDef* hm = ZombiesMap_Active();
        if (hm != nullptr && hm->hasPower)
            dl->AddText(f, 22.0f * k, ImVec2(24.0f, 20.0f), S.power ? IM_COL32(120, 255, 140, 230) : IM_COL32(255, 120, 90, 230),
                        S.power ? "POWER: ON" : "POWER: OFF");
        if (S.teleportBack > 0) {
            snprintf(b, sizeof(b), "Teleporter: back in %ds", S.teleportBack / kFps + 1);
            dl->AddText(f, 22.0f * k, ImVec2(24.0f, 46.0f * k), IM_COL32(160, 200, 255, 230), b);
        }
    }

    if (S.toSpawn == 0 && S.zombies.empty() && S.intermission < Inter()) {
        snprintf(b, sizeof(b), "Next round in %d", S.intermission / kFps + 1);
        dl->AddText(f, 36.0f * k, ImVec2(sz.x / 2 - 130.0f * k, 60.0f), Col(C().colText, 1.1f), b);
    }
    if (S.bannerFrames > 0)
        dl->AddText(f, 56.0f * k, ImVec2(sz.x / 2 - 170.0f * k, sz.y * 0.25f), IM_COL32(255, 240, 120, 255), S.banner);
    if (S.dogRound && S.round > 0)
        dl->AddText(f, 26.0f * k, ImVec2(sz.x / 2 - 60.0f * k, 20.0f), IM_COL32(255, 60, 60, 255), "DOG ROUND");
    if (S.toastFrames > 0)
        dl->AddText(f, 26.0f * k, ImVec2(sz.x / 2 - 200.0f * k, sz.y * 0.65f), Col(C().colText, 1.1f), S.toast);

    if (!C().showHints || !S.anchorSet) return;
    Player* player = GET_PLAYER(gPlayState);
    int st = NearStation(player);
    if (st >= 0) {
        const char* key = kButtonNames[std::clamp(C().keyUse, 0, kButtonCount - 1)];
        if (NeedsPower(st) && PowerRequired()) snprintf(b, sizeof(b), "%s (needs power)", kStationNames[st]);
        else if (C().stationCost[st] > 0) snprintf(b, sizeof(b), "%s: %s [%d]", key, kStationNames[st], C().stationCost[st]);
        else snprintf(b, sizeof(b), "%s: %s", key, kStationNames[st]);
        dl->AddText(f, 26.0f * k, ImVec2(sz.x / 2 - 220.0f * k, sz.y * 0.75f), Col(C().colText, 1.1f), b);
        return;
    }
    int d = NearDoor(player);
    if (d >= 0) {
        MapDef* m = ZombiesMap_Active();
        snprintf(b, sizeof(b), "%s: Unlock %s [%d]", kButtonNames[std::clamp(C().keyUse, 0, kButtonCount - 1)],
                 m->zones[d].name.c_str(), m->zones[d].doorCost);
        dl->AddText(f, 26.0f * k, ImVec2(sz.x / 2 - 220.0f * k, sz.y * 0.75f), Col(C().colText, 1.1f), b);
    }
}

void OnPresent() {
    DrawHud();
    ZombiesMenu_Draw(); // always drawn, so the menu works from the pause screen / title too
}

} // namespace

// ---------------------------------------------------------------- menu-facing API
ZombiesStatus ZombiesMode_GetStatus() {
    ZombiesStatus s;
    s.active = Enabled();
    s.round = S.round;
    s.points = S.points;
    s.alive = (int)S.zombies.size();
    s.toSpawn = S.toSpawn;
    s.inGame = gPlayState != nullptr;
    MapDef* m = ZombiesMap_Active();
    s.sceneMatches = m != nullptr && gPlayState != nullptr && gPlayState->sceneNum == m->scene;
    s.mapSceneSet = m == nullptr || m->scene >= 0;
    s.canWarp = m == nullptr || (m->scene >= 0 && m->entrance > 0);
    return s;
}

bool ZombiesMode_GetPlayerRel(Vec3f* rel) {
    if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr || !S.anchorSet) return false;
    Vec3f p = GET_PLAYER(gPlayState)->actor.world.pos;
    *rel = Vec3f{ p.x - S.anchor.x, p.y - S.anchor.y, p.z - S.anchor.z };
    return true;
}

void ZombiesMode_StartMatch() {
    gZombiesCfg.enabled = true;
    ZombiesConfig_Save();
    MapDef* m = ZombiesMap_Active();
    if (m != nullptr && gPlayState != nullptr && gPlayState->sceneNum != m->scene) {
        if (m->entrance <= 0) return; // no warp known: the player has to walk there
        // Warp to the map; the scene-init hook resets the match on arrival.
        gPlayState->nextEntranceIndex = m->entrance;
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
        return;
    }
    Reset();
}

void ZombiesMode_RestartMatch() { Reset(); }

int ZombiesMode_CurrentScene() { return gPlayState != nullptr ? (int)gPlayState->sceneNum : -1; }

void ZombiesMode_Register() {
    ZombiesConfig_Load();
    Reset();
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { Reset(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t) { Reset(); });
    // HUD + menu are drawn from the ImGui overlay pass; hook it where SoH draws its other overlays.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPresentFrame>(OnPresent);
}
