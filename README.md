# Zombies Mode for Ship of Harkinian

Call of Duty Zombies, rebuilt with Zelda stand-ins. Survive endless rounds of Redeads and Stalchildren, with a Wolfos "dog round" every 5th round.

| CoD | Zelda version |
|---|---|
| Points (10 per hit, 60 per kill) | HUD counter, you start with 500 |
| M1911 / Olympia / Crossbow / Sniper | Fairy Slingshot / Megaton Hammer / Fairy Bow / Hookshot |
| **Ray Gun** | **Light Ray**: 20-round mag, long range, splash damage, green bolt. Only from the box |
| Mystery Box (950) | Treasure Chest |
| Pack-a-Punch (5000) | Great Fairy: 2.5x damage, double reserve ammo |
| Juggernog 2500 / Speed Cola 3000 / Double Tap 2000 / Quick Revive 1500 | Fairies: +4 hearts / faster fire and reload / 1.5x damage / one self-revive |
| Wall buys | Hammer 500, Bow 1000 |
| Power-ups (4% drop) | Max Ammo, Insta-Kill, Double Points, Nuke. Auto-collected on drop |
| Dog rounds | Wolfos every 5th round, ends with a guaranteed Max Ammo |
| Zombie health scaling | Native enemy health rises each round |

## Menu (press F8)
| Tab | What you can change |
|---|---|
| Play | Enable, map (Lon Lon Ranch / any scene), force night, **Start / warp to map**, restart, difficulty presets (Easy, Normal, Hard, Insane, Classic CoD), reset all |
| Rules | Starting points, points per hit/kill, zombies per round, max alive, round delay, zombie health, dog-round interval, Redead/Stalchild toggles, power-up chance and which ones, Juggernog hearts, Quick Revive single-use |
| Weapons & Shop | Starting weapon, damage multiplier, Mystery Box odds per weapon (Ray Gun included), every station's price and on/off |
| Controls | Rebind fire / swap / use to any of R, L, D-pad, C buttons |
| HUD | Show/hide, scale, round/points/text colours, buy prompts |
| Map Editor | Edit zones, door prices, spawn points and station positions by standing in the ranch and clicking; save/load a layout file |

All settings autosave as CVars (`gZombies.*`).

## Map: Lon Lon Ranch
Press **Start** in the menu (with a save loaded) to warp to the ranch. It starts at night. Four zones:

| Zone | Door cost | Stations |
|---|---|---|
| Front Yard | open | Mystery Box, Juggernog, Hammer wall buy |
| Corral | 750 | Speed Cola, Quick Revive, Bow wall buy |
| Barn & Silo | 1250 | Double Tap |
| Talon's House | 1750 | Pack-a-Punch |

Zombies only spawn in zones you've unlocked. Stand at a door and press the use button to buy it.

## Map: Kino der Toten (Ruined Market)
Hyrule's ruined Castle Town stands in for the bombed-out theater, with the original's flow:

| Zone | Door cost | Stations |
|---|---|---|
| Lobby | open | Hammer and Bow wall buys |
| Foyer | 750 | Mystery Box, Juggernog |
| Theater | 1250 | Double Tap, **Teleporter** |
| Stage | 1500 | Speed Cola, **Power Switch** |
| Alley | 1000 | Quick Revive |
| Projection Room | no door | **Pack-a-Punch** (zombies never spawn here) |

- **Power:** perks and Pack-a-Punch are dead until you flip the power switch (HUD shows POWER: ON/OFF).
- **Teleporter:** needs power. It sends you to the Projection Room (and opens it) for 30 seconds, then pulls you back to the theater. Buy Pack-a-Punch in that window.
- The scene is the Market Ruins scene (`SCENE_MARKET_RUINS`). I don't know its entrance index, so **Start** can't warp there: enter the ruined market yourself (e.g. via the Temple of Time as adult), then press Start. You can also set a warp entrance in the Map Editor.
- As with Lon Lon, the coordinates are placeholders. Calibrate in the Map Editor tab, which also has scene number, warp entrance, teleporter destination and per-zone "no spawns" / "no door" options. Layouts save to `zombies_<map>_layout.txt`.
- Not copied from Kino: rotating box locations, windows to barricade, Nova crawlers, the monkey bomb.

Controls default to R = fire, D-Left = swap, D-Right = buy/use. Dying resets to round 1.

Not included yet: physical barricades, crawlers, co-op revive, zombie speed scaling, more maps.

## Status
**Untested.** SoH has no scripting/mod API for gameplay, so this is a C++ enhancement
written from memory of the SoH 8.x `GameInteractor` API. I could not build it here.
Expect to fix a few signatures (`OnPresentFrame`, `Actor_Spawn` args, hook names, SFX names) and the guessed marker actor params (chest/fairies) when you compile.

## Install
1. Clone [HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright) and build it per its docs (you need your own OoT ROM).
2. Copy `soh/soh/Enhancements/ZombiesMode/` into the same path in that tree.
3. Add all `ZombiesMode/*.cpp` files to the build (the CMake glob normally picks up new files; re-run CMake).
4. In SoH startup code (where other enhancements register, e.g. `SohGui`/`Enhancements`), add
   `#include "Enhancements/ZombiesMode/ZombiesMode.h"` and call `ZombiesMode_Register();`.
5. Launch, load a save, press F8. (To embed the menu in SoH's own UI instead, call `ZombiesMenu_DrawContents()` from your window.)

## Ideas for next steps
Mystery box, barrier doors per scene, weapon upgrades via Pack-a-Punch-style Great Fairy, power-ups (insta-kill = Megaton Hammer), boss rounds every 5.

## Getting real map coordinates from your ROM (no install)
Open **`tools/oot-coords.html`** in any browser (double-click it), drop your ROM on it, pick a scene, and click the map to place stations, doors, spawn points and the teleporter destination. **Export > Layout file** writes the `zombies_<map>_layout.txt` the mod reads (put it in the SoH app folder, then use "Reload from file" in the Map Editor tab). Everything runs locally; the ROM is never uploaded.

### Alternative: Python script
`tools/extract_oot_coords.py` reads your own OoT ROM (`.z64`/`.v64`/`.n64`, any version) and writes real spawn points,
actor positions, doors, floor collision and the warp entrance index, all relative to a spawn point (the way Zombies Mode stores layouts).
No packages needed (Pillow optional, for PNG maps). **Keep the ROM out of git**; `.gitignore` already blocks ROM files and `oot_coords_out/`.

```
python3 tools/extract_oot_coords.py oot.z64 --list                      # stats for every scene
python3 tools/extract_oot_coords.py oot.z64 --find-entrance 0x157        # which scene/spawn an entrance goes to
python3 tools/extract_oot_coords.py oot.z64 --scene 0x63 --render        # Lon Lon Ranch -> oot_coords_out/
python3 tools/extract_oot_coords.py oot.z64 --scene 0x49 --setup 3 --render   # ruined market, adult night
```
Send back the `scene_*.json` files (and `.png`/`.svg` maps), which contain only numbers, and the layouts can be built from them.
Scene names in `--list` are from memory, so use the room/size stats and the rendered map to confirm which scene is which.
`python3 tools/test_extract.py` self-tests the script against a synthetic ROM.
