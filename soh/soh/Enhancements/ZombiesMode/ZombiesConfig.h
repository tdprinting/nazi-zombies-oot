#pragma once
// Every gameplay/HUD/control setting for Zombies Mode, persisted as CVars "gZombies.*".

enum WeaponId { W_SLINGSHOT, W_BOW, W_HAMMER, W_HOOKSHOT, W_RAYGUN, W_COUNT };
enum StationType { ST_BOX, ST_PAP, ST_JUGG, ST_SPEED, ST_DTAP, ST_REVIVE, ST_WALL_HAMMER, ST_WALL_BOW, ST_POWER, ST_TELEPORT, ST_COUNT };
enum Perk { P_JUGG, P_SPEED, P_DTAP, P_REVIVE, P_COUNT };
enum Powerup { PU_MAXAMMO, PU_INSTAKILL, PU_DOUBLEPTS, PU_NUKE, PU_COUNT };

inline const char* const kWeaponNames[W_COUNT] = { "Fairy Slingshot", "Fairy Bow", "Megaton Hammer", "Hookshot",
                                                   "RAY GUN (Light Ray)" };
inline const char* const kStationNames[ST_COUNT] = { "Mystery Box", "Pack-a-Punch", "Juggernog", "Speed Cola",
                                                     "Double Tap", "Quick Revive", "Wall Buy: Hammer",
                                                     "Wall Buy: Bow", "Power Switch", "Teleporter" };
inline const int kStationDefaultCost[ST_COUNT] = { 950, 5000, 2500, 3000, 2000, 1500, 500, 1000, 0, 0 };
inline const char* const kPowerupNames[PU_COUNT] = { "MAX AMMO", "INSTA-KILL", "DOUBLE POINTS", "KA-BOOM" };

// Rebindable buttons (index into this list is what gets saved).
inline const char* const kButtonNames[] = { "R", "L", "D-Up", "D-Down", "D-Left", "D-Right",
                                            "C-Up", "C-Down", "C-Left", "C-Right" };
constexpr int kButtonCount = 10;

struct ZombiesConfig {
    // --- game
    bool enabled = false;
    int mapMode = 1;       // 0 = any scene (free play), N = Nth map in the registry (ZombiesMap_Get(N-1))
    bool forceNight = true;
    int preset = 1;        // last applied preset (display only)

    // --- rules
    int startPoints = 500;
    int hitPoints = 10;
    int killPoints = 60;
    int maxAlive = 24;
    int intermissionSec = 8;
    int baseZombies = 4;
    int zombiesPerRound = 3;
    float healthMult = 1.0f;
    int dogInterval = 5;   // 0 disables dog rounds
    bool useRedead = true;
    bool useStalchild = true;
    int stalchildFromRound = 3;
    int powerupChance = 4; // percent per kill
    int powerupMaxPerRound = 4;
    int powerupSec = 30;
    bool powerupOn[PU_COUNT] = { true, true, true, true };
    int juggHearts = 4;
    bool quickReviveOnce = true; // false = revive every time
    int startWeapon = W_SLINGSHOT;

    // --- weapons / shop
    float damageMult = 1.0f;
    int boxWeight[W_COUNT] = { 1, 2, 2, 2, 1 }; // relative chances, 0 = never
    int stationCost[ST_COUNT] = { 950, 5000, 2500, 3000, 2000, 1500, 500, 1000, 0, 0 };
    bool stationOn[ST_COUNT] = { true, true, true, true, true, true, true, true, true, true };

    // --- controls (indices into kButtonNames)
    int keyFire = 0;
    int keySwap = 4;
    int keyUse = 5;

    // --- HUD
    bool hudOn = true;
    float hudScale = 1.0f;
    bool showHints = true;
    float colRound[4] = { 0.75f, 0.08f, 0.08f, 1.0f };
    float colPoints[4] = { 1.0f, 0.86f, 0.35f, 1.0f };
    float colText[4] = { 1.0f, 1.0f, 1.0f, 0.9f };
};

extern ZombiesConfig gZombiesCfg;

void ZombiesConfig_Load();
void ZombiesConfig_Save();
void ZombiesConfig_ApplyPreset(int preset); // 0 Easy, 1 Normal, 2 Hard, 3 Insane, 4 Classic CoD
void ZombiesConfig_ResetDefaults();
inline const char* const kPresetNames[] = { "Easy", "Normal", "Hard", "Insane", "Classic CoD" };
constexpr int kPresetCount = 5;
