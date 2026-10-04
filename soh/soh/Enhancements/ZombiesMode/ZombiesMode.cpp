// Zombies Mode: Call-of-Duty-Zombies-style survival for Ship of Harkinian.
//
// Zelda stand-ins for the CoD staples:
//   Zombies         -> Redeads (Stalchildren from round 3), Wolfos on "dog rounds" (every 5th)
//   M1911           -> Fairy Slingshot (starter)
//   Olympia/Shotgun -> Megaton Hammer          Sniper -> Hookshot        Crossbow -> Fairy Bow
//   RAY GUN         -> "Light Ray": Light Arrow energy, green bolt, splash damage
//   Mystery Box     -> Treasure Chest (950)    Pack-a-Punch -> Great Fairy (5000)
//   Perks           -> fairies: Juggernog / Speed Cola / Double Tap / Quick Revive
//   Power-ups       -> Max Ammo, Insta-Kill, Double Points, Nuke (auto-collected on drop)
//
// Controls:  R = fire   D-Left = swap weapon   D-Right = buy / use station you stand at.
//
// NOTE: written against the SoH 8.x GameInteractor API from memory, NOT compiled.
// Marker actor params (chest, fairies) are best guesses; see SpawnMarker().

#include "ZombiesMode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <unordered_map>

#include <libultraship/bridge.h>
#include <imgui.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

// ---------------------------------------------------------------- tuning
constexpr int kFps = 20; // OoT game logic rate
constexpr int kIntermission = kFps * 8;
constexpr int kMaxAlive = 24;
constexpr int kHitPoints = 10;
constexpr int kKillPoints = 60;
constexpr int kStartPoints = 500;
constexpr int kPowerupSeconds = 30;
constexpr int kMaxHeartsCap = 20 * 16;
constexpr float kStationRing = 240.0f;
constexpr float kStationUseRange = 70.0f;

// ---------------------------------------------------------------- weapons
struct WeaponDef {
    const char* name;
    int damage;       // native enemy-health points per hit
    int cooldown;     // frames between shots
    float range;      // units
    int coneBinang;   // half-angle, 0x10000 = 360deg
    float splash;     // splash radius around primary target (0 = none)
    int mag;
    int reserve;
    u16 sfx;
};

enum WeaponId { W_SLINGSHOT, W_BOW, W_HAMMER, W_HOOKSHOT, W_RAYGUN, W_COUNT };

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

// ---------------------------------------------------------------- stations
enum StationType { ST_BOX, ST_PAP, ST_JUGG, ST_SPEED, ST_DTAP, ST_REVIVE, ST_WALL_HAMMER, ST_WALL_BOW, ST_COUNT };
enum Perk { P_JUGG, P_SPEED, P_DTAP, P_REVIVE, P_COUNT };

struct StationDef {
    const char* name;
    int cost;
};
const StationDef kStations[ST_COUNT] = {
    { "Mystery Box", 950 },    { "Pack-a-Punch (Great Fairy)", 5000 }, { "Juggernog", 2500 },
    { "Speed Cola", 3000 },    { "Double Tap", 2000 },                 { "Quick Revive", 1500 },
    { "Wall Buy: Megaton Hammer", 500 }, { "Wall Buy: Fairy Bow", 1000 },
};
const char* kPerkNames[P_COUNT] = { "Juggernog", "Speed Cola", "Double Tap", "Quick Revive" };

enum Powerup { PU_MAXAMMO, PU_INSTAKILL, PU_DOUBLEPTS, PU_NUKE, PU_COUNT };
const char* kPowerupNames[PU_COUNT] = { "MAX AMMO", "INSTA-KILL", "DOUBLE POINTS", "KA-BOOM" };

struct Zombie {
    int lastHealth = 0;
};

struct State {
    int round = 0;
    int points = kStartPoints;
    int toSpawn = 0;
    int intermission = kIntermission;
    int spawnCooldown = 0;
    int powerupsThisRound = 0;
    bool dogRound = false;
    bool dogBonusGiven = false;

    bool anchorSet = false;
    Vec3f anchor{};
    Vec3f stationPos[ST_COUNT]{};

    Weapon slots[2];
    int cur = 0;
    int fireCooldown = 0;
    int flash = 0; // ray gun screen flash frames
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

bool Enabled() { return CVarGetInteger("gEnhancements.ZombiesMode", 0) != 0; }

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
    GiveWeapon(S.slots[0], W_SLINGSHOT);
}

int Damage(const Weapon& w) {
    float d = (float)kWeapons[w.id].damage;
    if (w.punched) d *= 2.5f;
    if (S.perks[P_DTAP]) d *= 1.5f;
    return (int)std::ceil(d);
}

// ---------------------------------------------------------------- zombies
int HealthForRound(int round) { return 6 + std::min(round, 10) * 2 + std::max(0, round - 10); }
int CountForRound(int round) { return 4 + round * 3; }

void SpawnZombie(Player* player) {
    float ang = Rand(0.0f, 6.2831853f);
    float dist = Rand(350.0f, 650.0f);
    Vec3f pos = player->actor.world.pos;
    pos.x += std::sin(ang) * dist;
    pos.z += std::cos(ang) * dist;
    pos.y += 30.0f; // gravity drops it to the floor

    s16 id = ACTOR_EN_RD;
    if (S.dogRound) id = ACTOR_EN_WF;
    else if (S.round >= 3 && RandInt(3) == 0) id = ACTOR_EN_SKB;

    s16 faceLink = Math_Vec3f_Yaw(&pos, &player->actor.world.pos);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, id, pos.x, pos.y, pos.z, 0, faceLink, 0, 0, false);
    if (a == nullptr) return;
    a->colChkInfo.health = (u8)std::min(HealthForRound(S.round), 250);
    S.zombies[a] = Zombie{ a->colChkInfo.health };
}

void KillZombie(Actor* a) {
    a->colChkInfo.health = 0;
    Play(NA_SE_EN_REDEAD_DEAD);
    Actor_Kill(a);
}

int Points(int base) { return S.doublePts > 0 ? base * 2 : base; }

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
        case PU_INSTAKILL: S.instaKill = kPowerupSeconds * kFps; break;
        case PU_DOUBLEPTS: S.doublePts = kPowerupSeconds * kFps; break;
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
        if (a->colChkInfo.health < it->second.lastHealth) S.points += Points(kHitPoints);
        it->second.lastHealth = a->colChkInfo.health;
        ++it;
    }
    for (int i = 0; i < kills; i++) {
        S.points += Points(kKillPoints);
        if (S.powerupsThisRound < 4 && RandInt(100) < 4) {
            S.powerupsThisRound++;
            ApplyPowerup((Powerup)RandInt(PU_COUNT));
        }
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

    // Hitscan: closest zombie inside the weapon's cone and range.
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
    if (def.splash > 0.0f) {
        for (auto& kv : S.zombies)
            if (kv.first != best && Math_Vec3f_DistXZ(&best->world.pos, &kv.first->world.pos) <= def.splash)
                victims.push_back(kv.first);
    }
    for (Actor* v : victims) DamageActor(v, dmg);
}

// ---------------------------------------------------------------- stations
void SpawnMarker(s16 actorId, s16 params, const Vec3f& p) {
    // Visual only. Params are guesses: adjust if the chest/fairies look wrong in-game.
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, p.x, p.y, p.z, 0, 0, 0, params, false);
}

void SetupStations(Player* player) {
    S.anchor = player->actor.world.pos;
    for (int i = 0; i < ST_COUNT; i++) {
        float ang = (6.2831853f / ST_COUNT) * i;
        Vec3f p = S.anchor;
        p.x += std::sin(ang) * kStationRing;
        p.z += std::cos(ang) * kStationRing;
        S.stationPos[i] = p;
        if (i == ST_BOX) SpawnMarker(ACTOR_EN_BOX, 0x0000, p);
        else if (i == ST_PAP) SpawnMarker(ACTOR_EN_ELF, 0x0004, p); // Great-Fairy-ish
        else SpawnMarker(ACTOR_EN_ELF, 0x0000, p);
    }
    S.anchorSet = true;
}

int NearStation(Player* player) {
    for (int i = 0; i < ST_COUNT; i++)
        if (Math_Vec3f_DistXZ(&player->actor.world.pos, &S.stationPos[i]) <= kStationUseRange) return i;
    return -1;
}

void GiveToHand(int id) {
    Weapon& cur = S.slots[S.cur];
    int slot = S.slots[1].id < 0 ? 1 : S.cur; // fill empty slot first, else replace current
    if (cur.id < 0) slot = S.cur;
    GiveWeapon(S.slots[slot], id);
    S.cur = slot;
}

void UseStation(int st) {
    const StationDef& def = kStations[st];
    if (S.points < def.cost) {
        Toast("Not enough points");
        Play(NA_SE_SY_ERROR);
        return;
    }
    switch (st) {
        case ST_BOX: {
            // Ray Gun is rare, like the real box.
            static const int pool[] = { W_BOW, W_BOW, W_HAMMER, W_HAMMER, W_HOOKSHOT, W_HOOKSHOT, W_SLINGSHOT, W_RAYGUN };
            int id = pool[RandInt((int)(sizeof(pool) / sizeof(pool[0])))];
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
                gSaveContext.healthCapacity = std::min<int>(kMaxHeartsCap, gSaveContext.healthCapacity + 4 * 16);
                gSaveContext.health = gSaveContext.healthCapacity;
            }
            char b[80];
            snprintf(b, sizeof(b), "Perk: %s", kPerkNames[p]);
            Toast(b);
            break;
        }
        case ST_WALL_HAMMER: GiveToHand(W_HAMMER); Toast("Bought Megaton Hammer"); break;
        case ST_WALL_BOW: GiveToHand(W_BOW); Toast("Bought Fairy Bow"); break;
    }
    S.points -= def.cost;
    Play(NA_SE_SY_GET_ITEM);
}

// ---------------------------------------------------------------- main loop
void OnFrame() {
    if (!Enabled() || !InGameplay()) return;
    Player* player = GET_PLAYER(gPlayState);

    if (!S.anchorSet) SetupStations(player);

    if (gSaveContext.health <= 0) {
        if (S.perks[P_REVIVE]) { // Quick Revive: one second chance
            S.perks[P_REVIVE] = false;
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

    Input* in = &gPlayState->state.input[0];
    if (in->cur.button & BTN_R) Fire(player);
    if ((in->press.button & BTN_DLEFT) && S.slots[1 - S.cur].id >= 0) S.cur = 1 - S.cur;
    if (in->press.button & BTN_DRIGHT) {
        int st = NearStation(player);
        if (st >= 0) UseStation(st);
    }

    int alive = (int)S.zombies.size();
    if (S.toSpawn == 0 && alive == 0) {
        if (S.round > 0 && S.dogRound && !S.dogBonusGiven) { // dog rounds always end with Max Ammo
            S.dogBonusGiven = true;
            ApplyPowerup(PU_MAXAMMO);
        }
        if (S.intermission > 0) { S.intermission--; return; }
        S.round++;
        S.dogRound = (S.round % 5 == 0);
        S.dogBonusGiven = false;
        S.powerupsThisRound = 0;
        S.toSpawn = S.dogRound ? 6 + S.round / 5 * 2 : CountForRound(S.round);
        S.intermission = kIntermission;
        Play(NA_SE_SY_CORRECT_CHIME);
    }
    if (S.toSpawn > 0 && alive < kMaxAlive && --S.spawnCooldown <= 0) {
        SpawnZombie(player);
        S.toSpawn--;
        S.spawnCooldown = 10;
    }
}

// ---------------------------------------------------------------- HUD
void DrawHud() {
    if (!Enabled() || !InGameplay()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 sz = ImGui::GetIO().DisplaySize;
    ImFont* f = ImGui::GetFont();
    char b[128];

    if (S.flash > 0) { // Ray Gun bolt + glow
        dl->AddLine(ImVec2(sz.x * 0.62f, sz.y * 0.80f), ImVec2(sz.x * 0.5f, sz.y * 0.5f), IM_COL32(80, 255, 120, 220), 6.0f);
        dl->AddCircleFilled(ImVec2(sz.x * 0.5f, sz.y * 0.5f), 18.0f, IM_COL32(120, 255, 160, 160));
    }

    snprintf(b, sizeof(b), "%d", S.round);
    dl->AddText(f, 72.0f, ImVec2(24.0f, sz.y - 110.0f), IM_COL32(190, 20, 20, 255), b);

    snprintf(b, sizeof(b), "%d", S.points);
    dl->AddText(f, 34.0f, ImVec2(24.0f, sz.y - 170.0f), IM_COL32(255, 220, 90, 255), b);

    const Weapon& w = S.slots[S.cur];
    if (w.id >= 0) {
        snprintf(b, sizeof(b), "%s%s", w.punched ? "[PaP] " : "", kWeapons[w.id].name);
        dl->AddText(f, 22.0f, ImVec2(sz.x - 380.0f, sz.y - 90.0f), IM_COL32(255, 255, 255, 230), b);
        snprintf(b, sizeof(b), w.reload > 0 ? "RELOADING" : "%d / %d", w.mag, w.reserve);
        dl->AddText(f, 34.0f, ImVec2(sz.x - 380.0f, sz.y - 62.0f), IM_COL32(255, 255, 255, 255), b);
    }

    float py = 20.0f;
    for (int p = 0; p < P_COUNT; p++)
        if (S.perks[p]) {
            dl->AddText(f, 20.0f, ImVec2(sz.x - 200.0f, py), IM_COL32(120, 200, 255, 230), kPerkNames[p]);
            py += 22.0f;
        }
    if (S.instaKill > 0) {
        snprintf(b, sizeof(b), "INSTA-KILL %ds", S.instaKill / kFps);
        dl->AddText(f, 20.0f, ImVec2(sz.x - 200.0f, py), IM_COL32(255, 80, 80, 230), b);
        py += 22.0f;
    }
    if (S.doublePts > 0) {
        snprintf(b, sizeof(b), "2X POINTS %ds", S.doublePts / kFps);
        dl->AddText(f, 20.0f, ImVec2(sz.x - 200.0f, py), IM_COL32(255, 220, 90, 230), b);
    }

    if (S.toSpawn == 0 && S.zombies.empty() && S.intermission < kIntermission) {
        snprintf(b, sizeof(b), S.dogRound ? "Round clear" : "Next round in %d", S.intermission / kFps + 1);
        dl->AddText(f, 36.0f, ImVec2(sz.x / 2 - 130.0f, 60.0f), IM_COL32(255, 255, 255, 255), b);
    }
    if (S.bannerFrames > 0)
        dl->AddText(f, 56.0f, ImVec2(sz.x / 2 - 170.0f, sz.y * 0.25f), IM_COL32(255, 240, 120, 255), S.banner);
    if (S.round > 0 && S.round % 5 == 0 && S.dogRound)
        dl->AddText(f, 26.0f, ImVec2(sz.x / 2 - 90.0f, 20.0f), IM_COL32(255, 60, 60, 255), "DOG ROUND");
    if (S.toastFrames > 0)
        dl->AddText(f, 26.0f, ImVec2(sz.x / 2 - 200.0f, sz.y * 0.65f), IM_COL32(255, 255, 255, 255), S.toast);

    Player* player = GET_PLAYER(gPlayState);
    int st = S.anchorSet ? NearStation(player) : -1;
    if (st >= 0) {
        snprintf(b, sizeof(b), "D-Right: %s [%d]", kStations[st].name, kStations[st].cost);
        dl->AddText(f, 26.0f, ImVec2(sz.x / 2 - 220.0f, sz.y * 0.75f), IM_COL32(255, 255, 255, 255), b);
    }
}

} // namespace

void ZombiesMode_Register() {
    Reset();
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { Reset(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t) { Reset(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPresentFrame>(DrawHud);
}
