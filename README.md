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

Stations form a ring around where you start the scene. Walk up to one and press D-Right.

**Controls:** R = fire, D-Left = swap weapon (2 slots), D-Right = buy/use. Dying resets to round 1.

Not included yet: doors/barricades, crawlers, downed-and-revive co-op.

## Status
**Untested.** SoH has no scripting/mod API for gameplay, so this is a C++ enhancement
written from memory of the SoH 8.x `GameInteractor` API. I could not build it here.
Expect to fix a few signatures (`OnPresentFrame`, `Actor_Spawn` args, hook names, SFX names) and the guessed marker actor params (chest/fairies) when you compile.

## Install
1. Clone [HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright) and build it per its docs (you need your own OoT ROM).
2. Copy `soh/soh/Enhancements/ZombiesMode/` into the same path in that tree.
3. Add `ZombiesMode.cpp` to the build (the CMake glob normally picks up new files; re-run CMake).
4. In SoH startup code (where other enhancements register, e.g. `SohGui`/`Enhancements`), add
   `#include "Enhancements/ZombiesMode/ZombiesMode.h"` and call `ZombiesMode_Register();`.
5. Add a menu checkbox bound to the CVar `gEnhancements.ZombiesMode`, or set it to 1 in `shipofharkinian.json`.

## Ideas for next steps
Mystery box, barrier doors per scene, weapon upgrades via Pack-a-Punch-style Great Fairy, power-ups (insta-kill = Megaton Hammer), boss rounds every 5.
