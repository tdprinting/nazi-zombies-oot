# Zombies Mode for Ship of Harkinian

A round-based zombie survival mode for Ocarina of Time (Ship of Harkinian).
Survive endless waves of Redeads and Stalchildren, earn points per kill, and spend them on heals and extra hearts.

| Input  | Cost | Effect |
|--------|------|--------|
| D-Down | 250  | Refill hearts |
| D-Up   | 500  | +2 max hearts (cap 20) |

Dying resets you to round 1.

## Status
**Untested.** SoH has no scripting/mod API for gameplay, so this is a C++ enhancement
written from memory of the SoH 8.x `GameInteractor` API. I could not build it here.
Expect to fix a few signatures (`OnPresentFrame` for the HUD, `Actor_Spawn` args, hook names) when you compile.

## Install
1. Clone [HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright) and build it per its docs (you need your own OoT ROM).
2. Copy `soh/soh/Enhancements/ZombiesMode/` into the same path in that tree.
3. Add `ZombiesMode.cpp` to the build (the CMake glob normally picks up new files; re-run CMake).
4. In SoH startup code (where other enhancements register, e.g. `SohGui`/`Enhancements`), add
   `#include "Enhancements/ZombiesMode/ZombiesMode.h"` and call `ZombiesMode_Register();`.
5. Add a menu checkbox bound to the CVar `gEnhancements.ZombiesMode`, or set it to 1 in `shipofharkinian.json`.

## Ideas for next steps
Mystery box, barrier doors per scene, weapon upgrades via Pack-a-Punch-style Great Fairy, power-ups (insta-kill = Megaton Hammer), boss rounds every 5.
