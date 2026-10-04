#pragma once

// Draws the Zombies Mode settings window (toggle with F8). Called every frame from the ImGui
// overlay pass. If you'd rather embed it in SoH's own menu, call ZombiesMenu_DrawContents()
// from inside your own window instead and stop calling ZombiesMenu_Draw().
void ZombiesMenu_Draw();
void ZombiesMenu_DrawContents();
