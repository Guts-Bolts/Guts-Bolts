#include "PlayerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/Player.h"

#include "../PropGrid.h"
#include <imgui.h>
#include <optional>
#include <algorithm>
#include <cstdio>

PlayerPanel::PlayerPanel(Scene* scene) : m_scene(scene) {}

void PlayerPanel::render() {
    ImGui::Begin("Player");

    Player* player = m_scene->player();
    if (!player) {
        PG::TextDisabled("No player in the scene.");
        ImGui::End();
        return;
    }

    std::optional<PG::Style> grid(std::in_place);   // name on the left, value on the right (PropGrid.h)
    Humanoid& h = player->humanoid();

    if (PG::CollapsingHeader("Humanoid", ImGuiTreeNodeFlags_DefaultOpen)) {
        PG::SliderFloat("Walk Speed", &h.walkSpeed, 0.0f, 30.0f);
        PG::Checkbox("Use Jump Power", &h.useJumpPower);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: set how high to jump (Jump Height).\nOn: set how fast you leave the ground (Jump Power).");
        if (h.useJumpPower) PG::SliderFloat("Jump Power", &h.jumpPower, 0.0f, 40.0f);
        else                PG::SliderFloat("Jump Height", &h.jumpHeight, 0.0f, 20.0f, "%.1f studs");
        ImGui::Checkbox   ("Auto Rotate", &h.autoRotate);
    }

    if (PG::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
        PG::SliderFloat("Max Health", &h.maxHealth, 1.0f, 500.0f);
        if (h.health > h.maxHealth) h.health = h.maxHealth;
        // "##value": the header above is also called Health, and two things with
        // the same name in one window make Dear ImGui show a "conflicting ID" error.
        PG::SliderFloat("Health##value", &h.health, 0.0f, h.maxHealth);

        float frac = (h.maxHealth > 0.0f) ? h.health / h.maxHealth : 0.0f;
        ImVec4 bar = frac > 0.5f ? ImVec4(0.30f, 0.80f, 0.30f, 1.0f)
                   : frac > 0.2f ? ImVec4(0.90f, 0.75f, 0.20f, 1.0f)
                                 : ImVec4(0.85f, 0.25f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar);
        ImGui::ProgressBar(frac, ImVec2(-1, 0));
        ImGui::PopStyleColor();
    }

    if (PG::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        PG::TextDisabled("Outfit presets");
        int i = 0;
        for (const auto& [name, colors] : Player::colorPresets()) {
            if (PG::Button(name)) player->setBodyColors(colors);
            if (++i % 3 != 0) ImGui::SameLine();
        }
        ImGui::NewLine();

        BodyColors c = player->bodyColors();
        bool changed = false;
        changed |= PG::ColorEdit3("Head",      &c.head.x,     ImGuiColorEditFlags_NoInputs);
        changed |= PG::ColorEdit3("Torso",     &c.torso.x,    ImGuiColorEditFlags_NoInputs);
        changed |= PG::ColorEdit3("Left Arm",  &c.leftArm.x,  ImGuiColorEditFlags_NoInputs);
        changed |= PG::ColorEdit3("Right Arm", &c.rightArm.x, ImGuiColorEditFlags_NoInputs);
        changed |= PG::ColorEdit3("Left Leg",  &c.leftLeg.x,  ImGuiColorEditFlags_NoInputs);
        changed |= PG::ColorEdit3("Right Leg", &c.rightLeg.x, ImGuiColorEditFlags_NoInputs);
        if (changed) player->setBodyColors(c);

        PG::Spacing();
        const char* hats[] = {"None", "Top Hat", "Cap", "Crown"};
        int hat = (int)player->hat();
        if (PG::Combo("Hat", &hat, hats, IM_ARRAYSIZE(hats)))
            player->setHat((HatStyle)hat);
    }

    if (PG::CollapsingHeader("Death & Gore", ImGuiTreeNodeFlags_DefaultOpen)) drawGameRules(m_scene->world());

    if (PG::CollapsingHeader("Character")) {
        if (PG::Button("Rebuild Character")) player->build();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Put back a fresh character (if you deleted or broke parts of it)");
        PG::TextDisabled("The character appears on the part named");
        PG::TextDisabled("\"SpawnLocation\" when you press Play.");
    }

    PG::Spacing();
    PG::TextDisabled("Press Play, then move with WASD + Space.");

    grid.reset();
    ImGui::End();
}

void drawGameRules(WorldSettings& w) {
    PG::SeparatorText("Death");
    const char* styles[] = {"Classic (fall apart)", "Ragdoll"};
    int ds = (int)w.deathStyle;
    if (PG::Combo("Death Style", &ds, styles, 2)) w.deathStyle = (DeathStyle)ds;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Classic: the character falls to pieces like in Roblox.\n"
                          "Ragdoll: the body goes limp and tumbles with real joints.");
    ImGui::BeginDisabled(w.deathStyle != DeathStyle::Ragdoll);
    PG::Checkbox("Knockdowns", &w.knockdowns);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Big hits (explosions, crashes, hard landings) throw you around as a ragdoll:\n"
                          "you put your arms out to catch yourself, then get back up.");
    ImGui::EndDisabled();
    PG::SliderFloat("Spawn ForceField", &w.spawnForceField, 0.0f, 20.0f, "%.0f s");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A glowing shield for a few seconds after spawning.\n"
                          "Blocks damage from TakeDamage, explosions and falls.");

    PG::SeparatorText("Players");
    PG::Checkbox("Player Collisions", &w.playerCollisions);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: players bump into each other (and can stand on each other's heads).\n"
                          "Off: players walk right through each other.");
    PG::Checkbox("Voice Chat", &w.voiceChat);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: players who turned voice chat on can talk to each other (their voices come from their characters).\n"
                          "Off: nobody can talk in this game. Scripts: workspace.VoiceChatEnabled = false");

    PG::SeparatorText("Player list");
    {
        PlayerListStyle& pl = w.playerList;
        PG::Checkbox("Show player list", &pl.show);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The leaderboard in the top right: everyone's name, team and leaderstats.\n"
                              "Off: this game has no player list. A LocalScript can also hide it:\n"
                              "StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.PlayerList, false)");
        ImGui::BeginDisabled(!pl.show);
        char title[64];
        std::snprintf(title, sizeof(title), "%s", pl.title.c_str());
        if (PG::InputText("Title", title, sizeof(title))) pl.title = title;
        PG::Checkbox("Group by team", &pl.teams);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Players are listed under their team's name, in the team's colour,\n"
                              "with the team's total for each stat.");
        PG::ColorEdit3("Background", &pl.background.x, ImGuiColorEditFlags_NoInputs);
        PG::ColorEdit3("Title colour", &pl.titleColor.x, ImGuiColorEditFlags_NoInputs);
        PG::ColorEdit3("Text", &pl.textColor.x, ImGuiColorEditFlags_NoInputs);
        PG::SliderFloat("See-through", &pl.transparency, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How see-through the background is (0 = solid).\n"
                              "Scripts: StarterGui:SetCore(\"PlayerListStyle\", { BackgroundTransparency = 0.3 })");
        if (ImGui::SmallButton("Default look")) { const bool shown = pl.show; pl = PlayerListStyle{}; pl.show = shown; }
        ImGui::EndDisabled();
    }

    PG::SeparatorText("Camera");
    PG::Checkbox("Orthographic", &w.orthographic);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("No perspective: far things aren't smaller. For 2D, isometric and puzzle games.\n"
                          "Scripts: workspace.Orthographic = true");
    if (w.orthographic) {
        PG::DragFloat("Orthographic Size", &w.orthographicSize, 0.5f, 0.0f, 2000.0f, w.orthographicSize > 0 ? "%.1f studs" : "zoom freely");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How many studs tall the screen is. 0 = players zoom with the mouse wheel.\n"
                              "Scripts: workspace.OrthographicSize = 40");
    }
    // Every player's camera to start with (Roblox's StarterPlayer camera settings).
    CameraOptions& pc = w.playerCamera;
    const char* modes[] = {"Classic", "Lock First Person"};
    int mode = pc.lockFirstPerson ? 1 : 0;
    if (PG::Combo("Camera Mode", &mode, modes, 2)) pc.lockFirstPerson = mode == 1;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Classic: zoom in and out with the mouse wheel (all the way in = first person).\n"
                          "Lock First Person: players always see through their character's eyes.\n"
                          "Scripts: player.CameraMode = Enum.CameraMode.LockFirstPerson");
    ImGui::BeginDisabled(pc.lockFirstPerson);
    PG::DragFloat("Min Zoom Distance", &pc.minZoom, 0.1f, 0.0f, pc.maxZoom, "%.1f studs");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How close players can zoom in. Under 1.2 lets them zoom into first person.\n"
                          "Scripts: player.CameraMinZoomDistance = 10");
    PG::DragFloat("Max Zoom Distance", &pc.maxZoom, 1.0f, pc.minZoom, CameraOptions::kLimit, "%.0f studs");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How far out players can zoom.\nScripts: player.CameraMaxZoomDistance = 30");
    ImGui::EndDisabled();
    pc.minZoom = std::clamp(pc.minZoom, 0.0f, CameraOptions::kLimit);
    pc.maxZoom = std::clamp(pc.maxZoom, pc.minZoom, CameraOptions::kLimit);

    PG::SeparatorText("Fall damage");
    PG::Checkbox("Fall Damage", &w.fallDamage);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn off and players can fall from any height unhurt.");
    ImGui::BeginDisabled(!w.fallDamage);
    PG::SliderFloat("Safe Fall Speed", &w.fallDamageSpeed, 5.0f, 150.0f, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Landing slower than this doesn't hurt. Higher = safer falls.");
    PG::SliderFloat("Fall Damage Strength", &w.fallDamageScale, 0.1f, 5.0f, "x%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much a hard landing hurts. 2 = twice as much, 0.5 = half.");
    ImGui::EndDisabled();

    PG::SeparatorText("Gore");
    const char* gore[] = {"Off", "Oil & Bolts", "Blood"};
    int g = (int)w.gore;
    if (PG::Combo("Gore", &g, gore, 3)) w.gore = (GoreLevel)g;
    ImGui::BeginDisabled(w.gore == GoreLevel::Off);
    PG::Checkbox("Dismemberment", &w.dismemberment);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Big hits (explosions, long falls, saws) knock limbs off");
    PG::SliderFloat("Blood Amount", &w.bloodAmount, 0.2f, 3.0f, "x%.1f");
    PG::SliderFloat("Pools Last", &w.bloodStay, 3.0f, 120.0f, "%.0f s");
    PG::Checkbox("Liquid Blood", &w.liquidBlood);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Blood also runs as real liquid: it drips, streams downhill and pools.\n"
                          "Uses the water simulation, so it costs more on slow computers.");
    PG::Checkbox("Soft Tissue", &w.softTissue);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dents from hits slowly spring back out, and very big hits tear.\n"
                          "Heads are hard, bellies are soft. Off = dents stay until respawning.");
    PG::Checkbox("Injuries", &w.injuries);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A hurt leg limps (and walks slower), a hurt arm or side is held.\n"
                          "Heals over about a minute and a half, and on respawning.");
    PG::Checkbox("Stamina", &w.stamina);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Running, jumping and swimming tire you out: slower, lower jumps\n"
                          "and panting until you get your breath back. Scripts: Humanoid.Stamina.");
    PG::Checkbox("Fire Spreads", &w.fireSpread);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fires set wood, cloth, grass and plastic touching them alight.\n"
                          "Burning things char black and fall apart; water puts them out.");
    PG::Checkbox("Impact Sounds", &w.impactSounds);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Loose parts knocking into things make the sound of what they're made of.");
    PG::Checkbox("Room Acoustics", &w.acoustics);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sounds behind walls are muffled, rooms and caves echo,\n"
                          "and things racing past bend in pitch.");
    PG::TextDisabled("A character can bleed its own way: give its Model the attributes");
    PG::TextDisabled("Gore (\"Blood\" / \"Oil\" / \"Off\"), BloodColor and BloodAmount.");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(w.gore != GoreLevel::Blood);
    PG::ColorEdit3("Blood Color", &w.bloodColor.x, ImGuiColorEditFlags_NoInputs);
    struct Preset { const char* name; glm::vec3 c; };
    const Preset presets[] = {{"Red", {0.50f, 0.02f, 0.03f}}, {"Slime", {0.25f, 0.65f, 0.08f}},
                              {"Alien", {0.10f, 0.30f, 0.85f}}, {"Ink", {0.05f, 0.05f, 0.08f}}};
    for (const Preset& p : presets) {
        if (&p != presets) ImGui::SameLine();
        if (ImGui::SmallButton(p.name)) w.bloodColor = p.c;
    }
    ImGui::EndDisabled();

    PG::Spacing();
    if (PG::Button("Classic Roblox rules")) {
        w.deathStyle = DeathStyle::Classic; w.gore = GoreLevel::Off; w.fallDamage = false;
    }
    ImGui::SameLine();
    if (PG::Button("Full carnage")) {
        w.deathStyle = DeathStyle::Ragdoll; w.gore = GoreLevel::Blood;
        w.dismemberment = true; w.fallDamage = true;
    }
}
