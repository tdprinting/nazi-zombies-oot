#pragma once

// Call once from SoH startup (e.g. next to the other Enhancement registrations
// in soh/soh/SohGui or Enhancements/enhancementTypes). Gated by the CVar
// "gEnhancements.ZombiesMode" (0 = off, 1 = on).
void ZombiesMode_Register();
