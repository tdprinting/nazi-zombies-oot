#include "ZombiesMenu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "ZombiesConfig.h"
#include "ZombiesMap.h"
#include "ZombiesMode.h"

namespace {

bool sOpen = false;
bool sDirty = false;
int sSelZone = 0;
int sSelStation = 0;
char sMsg[96] = "";

// Wrap a widget: remember that something changed so we can autosave once the user lets go.
template <typename T> void Touch(T changed) { if (changed) sDirty = true; }

int c_idx() { return std::clamp(gZombiesCfg.mapMode - 1, 0, ZombiesMap_Count() - 1); }

void Msg(const char* m) { snprintf(sMsg, sizeof(sMsg), "%s", m); }

void TabPlay() {
    ZombiesConfig& c = gZombiesCfg;
    ZombiesStatus st = ZombiesMode_GetStatus();

    ImGui::TextWrapped("Call-of-Duty-style zombies for Ocarina of Time. Survive rounds, earn points, "
                       "open the ranch, pack-a-punch the Ray Gun.");
    ImGui::Separator();

    Touch(ImGui::Checkbox("Zombies Mode enabled", &c.enabled));

    {
        const char* cur = c.mapMode == 0 ? "Any scene (free play)" : ZombiesMap_Get(std::clamp(c.mapMode - 1, 0, ZombiesMap_Count() - 1)).name.c_str();
        if (ImGui::BeginCombo("Map", cur)) {
            if (ImGui::Selectable("Any scene (free play)", c.mapMode == 0)) { c.mapMode = 0; sDirty = true; }
            for (int i = 0; i < ZombiesMap_Count(); i++)
                if (ImGui::Selectable(ZombiesMap_Get(i).name.c_str(), c.mapMode == i + 1)) { c.mapMode = i + 1; sDirty = true; }
            ImGui::EndCombo();
        }
        if (MapDef* am = ZombiesMap_Active()) ImGui::TextDisabled("%s", am->description.c_str());
    }
    Touch(ImGui::Checkbox("Force night time", &c.forceNight));

    ImGui::Spacing();
    if (!st.mapSceneSet)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "This map has no scene number set (Map Editor tab).");
    else if (!st.canWarp && !st.sceneMatches)
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "No warp entrance set: walk into the map yourself (or set the entrance in the Map Editor).");
    if (!st.inGame) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Load a save file first, then press Start.");
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Start / warp to map", ImVec2(180, 0))) ZombiesMode_StartMatch();
    ImGui::SameLine();
    if (ImGui::Button("Restart round 1", ImVec2(150, 0))) ZombiesMode_RestartMatch();
    if (!st.inGame) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Text("Status: %s", st.active ? "ACTIVE" : (c.enabled ? "waiting (wrong scene)" : "off"));
    if (st.active) ImGui::Text("Round %d   Points %d   Alive %d   To spawn %d", st.round, st.points, st.alive, st.toSpawn);

    ImGui::Separator();
    ImGui::Text("Difficulty preset");
    for (int i = 0; i < kPresetCount; i++) {
        if (i) ImGui::SameLine();
        bool sel = c.preset == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1));
        if (ImGui::Button(kPresetNames[i])) { ZombiesConfig_ApplyPreset(i); sDirty = true; }
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (ImGui::Button("Reset ALL settings to defaults")) { ZombiesConfig_ResetDefaults(); sDirty = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("(F8 hides this window)");
}

void TabRules() {
    ZombiesConfig& c = gZombiesCfg;
    ImGui::SeparatorText("Points");
    Touch(ImGui::SliderInt("Starting points", &c.startPoints, 0, 10000));
    Touch(ImGui::SliderInt("Points per hit", &c.hitPoints, 0, 100));
    Touch(ImGui::SliderInt("Points per kill", &c.killPoints, 0, 500));

    ImGui::SeparatorText("Rounds");
    Touch(ImGui::SliderInt("Zombies in round 1", &c.baseZombies, 1, 30));
    Touch(ImGui::SliderInt("Extra zombies per round", &c.zombiesPerRound, 0, 15));
    Touch(ImGui::SliderInt("Max alive at once", &c.maxAlive, 1, 60));
    Touch(ImGui::SliderInt("Time between rounds (s)", &c.intermissionSec, 1, 30));
    Touch(ImGui::SliderFloat("Zombie health multiplier", &c.healthMult, 0.25f, 4.0f, "%.2fx"));
    Touch(ImGui::SliderInt("Dog round every N rounds (0 = never)", &c.dogInterval, 0, 15));

    ImGui::SeparatorText("Zombie types");
    Touch(ImGui::Checkbox("Redeads", &c.useRedead));
    ImGui::SameLine();
    Touch(ImGui::Checkbox("Stalchildren", &c.useStalchild));
    if (c.useStalchild) Touch(ImGui::SliderInt("Stalchildren appear from round", &c.stalchildFromRound, 1, 20));
    if (!c.useRedead && !c.useStalchild) ImGui::TextDisabled("(both off: Redeads are used anyway)");

    ImGui::SeparatorText("Power-ups");
    Touch(ImGui::SliderInt("Drop chance per kill (%)", &c.powerupChance, 0, 100));
    Touch(ImGui::SliderInt("Max drops per round", &c.powerupMaxPerRound, 0, 20));
    Touch(ImGui::SliderInt("Insta-Kill / Double Points duration (s)", &c.powerupSec, 5, 120));
    for (int i = 0; i < PU_COUNT; i++) {
        if (i) ImGui::SameLine();
        Touch(ImGui::Checkbox(kPowerupNames[i], &c.powerupOn[i]));
    }

    ImGui::SeparatorText("Perks");
    Touch(ImGui::SliderInt("Juggernog bonus hearts", &c.juggHearts, 1, 10));
    Touch(ImGui::Checkbox("Quick Revive is single-use", &c.quickReviveOnce));
}

void TabWeapons() {
    ZombiesConfig& c = gZombiesCfg;
    ImGui::SeparatorText("Loadout");
    Touch(ImGui::Combo("Starting weapon", &c.startWeapon, kWeaponNames, W_COUNT));
    Touch(ImGui::SliderFloat("Weapon damage multiplier", &c.damageMult, 0.25f, 5.0f, "%.2fx"));

    ImGui::SeparatorText("Mystery Box odds (relative weights, 0 = never)");
    int total = 0;
    for (int i = 0; i < W_COUNT; i++) total += std::max(0, c.boxWeight[i]);
    for (int i = 0; i < W_COUNT; i++) {
        ImGui::PushID(i);
        Touch(ImGui::SliderInt(kWeaponNames[i], &c.boxWeight[i], 0, 10));
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f%%", total > 0 ? 100.0f * std::max(0, c.boxWeight[i]) / total : 0.0f);
        ImGui::PopID();
    }

    ImGui::SeparatorText("Shop");
    if (ImGui::BeginTable("shop", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Station");
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("Cost");
        ImGui::TableHeadersRow();
        for (int i = 0; i < ST_COUNT; i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kStationNames[i]);
            ImGui::TableNextColumn(); Touch(ImGui::Checkbox("##on", &c.stationOn[i]));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            Touch(ImGui::DragInt("##cost", &c.stationCost[i], 10.0f, 0, 50000));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset shop prices")) {
        for (int i = 0; i < ST_COUNT; i++) c.stationCost[i] = kStationDefaultCost[i];
        sDirty = true;
    }
    ImGui::TextDisabled("Station on/off applies the next time you enter the map.");
}

void TabControls() {
    ZombiesConfig& c = gZombiesCfg;
    ImGui::TextWrapped("Pick the N64 button for each action. Fire is held, the others are pressed. "
                       "Remember these buttons keep their normal Zelda function too.");
    Touch(ImGui::Combo("Fire", &c.keyFire, kButtonNames, kButtonCount));
    Touch(ImGui::Combo("Swap weapon", &c.keySwap, kButtonNames, kButtonCount));
    Touch(ImGui::Combo("Buy / use / open door", &c.keyUse, kButtonNames, kButtonCount));
    if (c.keyFire == c.keySwap || c.keyFire == c.keyUse || c.keySwap == c.keyUse)
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Two actions share a button.");
    if (ImGui::Button("Defaults (R / D-Left / D-Right)")) {
        c.keyFire = 0; c.keySwap = 4; c.keyUse = 5;
        sDirty = true;
    }
}

void TabHud() {
    ZombiesConfig& c = gZombiesCfg;
    Touch(ImGui::Checkbox("Show HUD", &c.hudOn));
    Touch(ImGui::Checkbox("Show buy prompts near stations and doors", &c.showHints));
    Touch(ImGui::SliderFloat("HUD scale", &c.hudScale, 0.5f, 2.0f, "%.2fx"));
    Touch(ImGui::ColorEdit4("Round number", c.colRound));
    Touch(ImGui::ColorEdit4("Points", c.colPoints));
    Touch(ImGui::ColorEdit4("Text", c.colText));
    if (ImGui::Button("Reset HUD colors")) {
        ZombiesConfig d;
        std::memcpy(c.colRound, d.colRound, sizeof(c.colRound));
        std::memcpy(c.colPoints, d.colPoints, sizeof(c.colPoints));
        std::memcpy(c.colText, d.colText, sizeof(c.colText));
        c.hudScale = 1.0f;
        sDirty = true;
    }
}

void TabMapEditor() {
    MapDef* mp = ZombiesMap_Active();
    if (mp == nullptr) {
        ImGui::TextDisabled("Pick a map on the Play tab to edit its layout.");
        return;
    }
    MapDef& m = *mp;
    Vec3f rel;
    bool have = ZombiesMode_GetPlayerRel(&rel);

    ImGui::Text("Editing: %s", m.name.c_str());
    ImGui::TextWrapped("Walk Link to a spot in the map, then record it here. Positions are saved relative "
                       "to where you entered the scene (from Hyrule Field), so always enter the same way. "
                       "The default layouts are rough placeholders.");
    if (have) ImGui::Text("Link (relative): %.0f, %.0f, %.0f", rel.x, rel.y, rel.z);
    else ImGui::TextDisabled("Enter the map to record positions.");

    ImGui::SeparatorText("Scene");
    ImGui::InputInt("Scene number", &m.scene);
    ImGui::InputInt("Warp entrance index (0 = no warp)", &m.entrance);
    {
        int cur = ZombiesMode_CurrentScene();
        if (cur < 0) ImGui::BeginDisabled();
        if (ImGui::Button("Use current scene as this map's scene")) { m.scene = cur; Msg("Scene set (Save layout to keep it)"); }
        if (cur < 0) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(now in scene %d)", cur);
    }
    if (m.hasPower) {
        ImGui::Text("Teleporter destination: %.0f, %.0f, %.0f", m.teleportDest.x, m.teleportDest.y, m.teleportDest.z);
        if (!have) ImGui::BeginDisabled();
        if (ImGui::Button("Set teleporter destination here")) { m.teleportDest = rel; Msg("Teleporter destination moved"); }
        if (!have) ImGui::EndDisabled();
    }

    if (m.zones.empty()) return;
    sSelZone = std::clamp(sSelZone, 0, (int)m.zones.size() - 1);

    ImGui::SeparatorText("Zones (areas / doors)");
    if (ImGui::BeginCombo("Zone", m.zones[sSelZone].name.c_str())) {
        for (int i = 0; i < (int)m.zones.size(); i++) {
            char label[96];
            snprintf(label, sizeof(label), "%d: %s", i, m.zones[i].name.c_str());
            if (ImGui::Selectable(label, i == sSelZone)) sSelZone = i;
        }
        ImGui::EndCombo();
    }
    MapZone& z = m.zones[sSelZone];
    char nameBuf[48];
    snprintf(nameBuf, sizeof(nameBuf), "%s", z.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) z.name = nameBuf;
    ImGui::DragInt("Door cost (0 = open, -1 = no door)", &z.doorCost, 10.0f, -1, 20000);
    ImGui::Checkbox("No zombie spawns in this zone", &z.noSpawn);
    ImGui::Text("Door at %.0f, %.0f, %.0f   |   %d spawn point(s)", z.door.x, z.door.y, z.door.z, (int)z.spawns.size());

    if (!have) ImGui::BeginDisabled();
    if (ImGui::Button("Set door here")) { z.door = rel; Msg("Door moved"); }
    ImGui::SameLine();
    if (ImGui::Button("Add spawn point here")) { z.spawns.push_back(rel); Msg("Spawn point added"); }
    if (!have) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear spawns")) { z.spawns.clear(); Msg("Spawns cleared (free-play ring is used if all empty)"); }
    if (ImGui::Button("Add zone")) {
        m.zones.push_back(MapZone{ "New Zone", 1000, false, Vec3f{}, {} });
        sSelZone = (int)m.zones.size() - 1;
    }

    ImGui::SeparatorText("Stations");
    sSelStation = std::clamp(sSelStation, 0, (int)ST_COUNT - 1);
    ImGui::Combo("Station", &sSelStation, kStationNames, ST_COUNT);
    ImGui::Text("At %.0f, %.0f, %.0f  in zone %d", m.station[sSelStation].x, m.station[sSelStation].y,
                m.station[sSelStation].z, m.stationZone[sSelStation]);
    ImGui::SliderInt("Belongs to zone", &m.stationZone[sSelStation], 0, (int)m.zones.size() - 1);
    if (!have) ImGui::BeginDisabled();
    if (ImGui::Button("Place station here")) { m.station[sSelStation] = rel; Msg("Station moved"); }
    if (!have) ImGui::EndDisabled();
    ImGui::TextDisabled("Station markers update the next time you enter the map.");

    ImGui::SeparatorText("Layout file");
    if (ImGui::Button("Save layout")) Msg(ZombiesMap_SaveCustom(m) ? "Saved layout file" : "Save FAILED");
    ImGui::SameLine();
    if (ImGui::Button("Reload from file")) {
        ZombiesMap_ResetToDefault(c_idx());
        Msg(ZombiesMap_LoadCustom(ZombiesMap_Get(c_idx())) ? "Loaded saved layout" : "No saved layout; defaults restored");
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults")) { ZombiesMap_ResetToDefault(c_idx()); Msg("Defaults restored (not saved)"); }
    if (sMsg[0]) ImGui::TextColored(ImVec4(0.5f, 1, 0.6f, 1), "%s", sMsg);
}

} // namespace

void ZombiesMenu_DrawContents() {
    if (ImGui::BeginTabBar("zombies_tabs")) {
        if (ImGui::BeginTabItem("Play")) { TabPlay(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Rules")) { TabRules(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Weapons & Shop")) { TabWeapons(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Controls")) { TabControls(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("HUD")) { TabHud(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Map Editor")) { TabMapEditor(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

void ZombiesMenu_Draw() {
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) sOpen = !sOpen;
    if (!sOpen) return;

    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Zombies Mode", &sOpen)) ZombiesMenu_DrawContents();
    ImGui::End();

    // Autosave once the user has let go of whatever they were dragging.
    if (sDirty && !ImGui::IsAnyItemActive()) {
        ZombiesConfig_Save();
        sDirty = false;
    }
}
