// Zombies Mode: a round-based survival mode for Ship of Harkinian.
//
//  - Waves of Redeads / Stalchildren spawn around Link and get bigger each round.
//  - Kills earn points (shown as the HUD counter).
//  - Spend points with the D-Pad:
//      D-Down  (250)  refill hearts
//      D-Up    (500)  "Juggernog": +2 max hearts (up to 20)
//  - When Link dies the run resets to round 1.
//
// NOTE: written against the SoH 8.x GameInteractor API from memory and not yet
// compiled against a real tree. Expect to fix small signature differences.

#include "ZombiesMode.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <unordered_set>

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

constexpr int kIntermissionFrames = 20 * 8; // game logic runs at 20 fps
constexpr int kMaxAlive = 24;
constexpr int kPointsPerKill = 60;
constexpr int kHealCost = 250;
constexpr int kJuggerCost = 500;
constexpr int kMaxHearts = 20 * 16;

struct State {
    int round = 0;
    int points = 0;
    int toSpawn = 0;
    int intermission = kIntermissionFrames;
    int spawnCooldown = 0;
    std::unordered_set<Actor*> tracked;
};

State sState;
std::mt19937 sRng{ std::random_device{}() };

bool Enabled() {
    return CVarGetInteger("gEnhancements.ZombiesMode", 0) != 0;
}

bool InGameplay() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr &&
           gPlayState->state.running && gPlayState->pauseCtx.state == 0;
}

void Reset() {
    sState = State{};
}

float RandRange(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(sRng);
}

int ZombiesForRound(int round) {
    return 4 + round * 3;
}

void SpawnZombie(Player* player) {
    float angle = RandRange(0.0f, 2.0f * 3.14159265f);
    float dist = RandRange(350.0f, 650.0f);
    float x = player->actor.world.pos.x + std::sin(angle) * dist;
    float z = player->actor.world.pos.z + std::cos(angle) * dist;
    float y = player->actor.world.pos.y + 30.0f; // gravity drops it to the floor

    // Later rounds mix in Stalchildren.
    bool stalchild = sState.round >= 3 && (sRng() % 3 == 0);
    s16 actorId = stalchild ? ACTOR_EN_SKB : ACTOR_EN_RD;
    s16 faceLink = (s16)(Math_Vec3f_Yaw(&player->actor.world.pos, &player->actor.world.pos));

    // params 0 = default, awake version of both actors.
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, x, y, z, 0, faceLink, 0, 0, false);
    if (a != nullptr) {
        sState.tracked.insert(a);
    }
}

// Tracked actors that vanished from the enemy list were killed (or despawned).
int CollectKills() {
    std::unordered_set<Actor*> alive;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        alive.insert(a);
    }
    int kills = 0;
    for (auto it = sState.tracked.begin(); it != sState.tracked.end();) {
        if (alive.count(*it) == 0) {
            kills++;
            it = sState.tracked.erase(it);
        } else {
            ++it;
        }
    }
    return kills;
}

void HandlePurchases(Player* player) {
    u16 pressed = gPlayState->state.input[0].press.button;
    if ((pressed & BTN_DDOWN) && sState.points >= kHealCost) {
        sState.points -= kHealCost;
        gSaveContext.health = gSaveContext.healthCapacity;
        Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    if ((pressed & BTN_DUP) && sState.points >= kJuggerCost && gSaveContext.healthCapacity < kMaxHearts) {
        sState.points -= kJuggerCost;
        gSaveContext.healthCapacity += 2 * 16;
        gSaveContext.health = gSaveContext.healthCapacity;
        Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

void OnFrame() {
    if (!Enabled() || !InGameplay()) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);

    if (gSaveContext.health <= 0) {
        Reset();
        return;
    }

    sState.points += CollectKills() * kPointsPerKill;
    HandlePurchases(player);

    int alive = (int)sState.tracked.size();

    if (sState.toSpawn == 0 && alive == 0) {
        if (sState.intermission > 0) {
            sState.intermission--;
            return;
        }
        sState.round++;
        sState.toSpawn = ZombiesForRound(sState.round);
        sState.intermission = kIntermissionFrames;
    }

    if (sState.toSpawn > 0 && alive < kMaxAlive && --sState.spawnCooldown <= 0) {
        SpawnZombie(player);
        sState.toSpawn--;
        sState.spawnCooldown = 10;
    }
}

void DrawHud() {
    if (!Enabled() || !InGameplay()) {
        return;
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;
    char buf[96];

    snprintf(buf, sizeof(buf), "ROUND %d", sState.round);
    dl->AddText(ImGui::GetFont(), 48.0f, ImVec2(24.0f, size.y - 90.0f), IM_COL32(190, 20, 20, 255), buf);

    snprintf(buf, sizeof(buf), "%d pts", sState.points);
    dl->AddText(ImGui::GetFont(), 28.0f, ImVec2(size.x - 220.0f, size.y - 70.0f), IM_COL32(255, 220, 90, 255), buf);

    snprintf(buf, sizeof(buf), "D-Down: heal (%d)   D-Up: +2 hearts (%d)", kHealCost, kJuggerCost);
    dl->AddText(ImGui::GetFont(), 18.0f, ImVec2(24.0f, size.y - 30.0f), IM_COL32(230, 230, 230, 200), buf);

    if (sState.toSpawn == 0 && sState.tracked.empty()) {
        snprintf(buf, sizeof(buf), "Next round in %d...", sState.intermission / 20 + 1);
        dl->AddText(ImGui::GetFont(), 36.0f, ImVec2(size.x / 2 - 130.0f, 60.0f), IM_COL32(255, 255, 255, 255), buf);
    }
}

} // namespace

void ZombiesMode_Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { Reset(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t) { Reset(); });
    // HUD is drawn from the ImGui overlay pass; hook it where SoH draws its other overlays.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPresentFrame>(DrawHud);
}
