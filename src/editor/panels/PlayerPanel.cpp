#include "PlayerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/Player.h"

#include <imgui.h>
#include <algorithm>

PlayerPanel::PlayerPanel(Scene* scene) : m_scene(scene) {}

void PlayerPanel::render() {
    ImGui::Begin("Player");

    Player* player = m_scene->player();
    if (!player) {
        ImGui::TextDisabled("No player in the scene.");
        ImGui::End();
        return;
    }

    Humanoid& h = player->humanoid();

    if (ImGui::CollapsingHeader("Humanoid", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Walk Speed", &h.walkSpeed, 0.0f, 30.0f);
        ImGui::Checkbox("Use Jump Power", &h.useJumpPower);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: set how high to jump (Jump Height).\nOn: set how fast you leave the ground (Jump Power).");
        if (h.useJumpPower) ImGui::SliderFloat("Jump Power", &h.jumpPower, 0.0f, 40.0f);
        else                ImGui::SliderFloat("Jump Height", &h.jumpHeight, 0.0f, 20.0f, "%.1f studs");
        ImGui::Checkbox   ("Auto Rotate", &h.autoRotate);
    }

    if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Max Health", &h.maxHealth, 1.0f, 500.0f);
        if (h.health > h.maxHealth) h.health = h.maxHealth;
        // "##value": the header above is also called Health, and two things with
        // the same name in one window make Dear ImGui show a "conflicting ID" error.
        ImGui::SliderFloat("Health##value", &h.health, 0.0f, h.maxHealth);

        float frac = (h.maxHealth > 0.0f) ? h.health / h.maxHealth : 0.0f;
        ImVec4 bar = frac > 0.5f ? ImVec4(0.30f, 0.80f, 0.30f, 1.0f)
                   : frac > 0.2f ? ImVec4(0.90f, 0.75f, 0.20f, 1.0f)
                                 : ImVec4(0.85f, 0.25f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar);
        ImGui::ProgressBar(frac, ImVec2(-1, 0));
        ImGui::PopStyleColor();
    }

    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Outfit presets");
        int i = 0;
        for (const auto& [name, colors] : Player::colorPresets()) {
            if (ImGui::Button(name)) player->setBodyColors(colors);
            if (++i % 3 != 0) ImGui::SameLine();
        }
        ImGui::NewLine();

        BodyColors c = player->bodyColors();
        bool changed = false;
        changed |= ImGui::ColorEdit3("Head",      &c.head.x,     ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine(150);
        changed |= ImGui::ColorEdit3("Torso",     &c.torso.x,    ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Left Arm",  &c.leftArm.x,  ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine(150);
        changed |= ImGui::ColorEdit3("Right Arm", &c.rightArm.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Left Leg",  &c.leftLeg.x,  ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine(150);
        changed |= ImGui::ColorEdit3("Right Leg", &c.rightLeg.x, ImGuiColorEditFlags_NoInputs);
        if (changed) player->setBodyColors(c);

        ImGui::Spacing();
        const char* hats[] = {"None", "Top Hat", "Cap", "Crown"};
        int hat = (int)player->hat();
        if (ImGui::Combo("Hat", &hat, hats, IM_ARRAYSIZE(hats)))
            player->setHat((HatStyle)hat);
    }

    if (ImGui::CollapsingHeader("Death & Gore", ImGuiTreeNodeFlags_DefaultOpen)) drawGameRules(m_scene->world());

    if (ImGui::CollapsingHeader("Character")) {
        if (ImGui::Button("Rebuild Character")) player->build();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Put back a fresh character (if you deleted or broke parts of it)");
        ImGui::TextDisabled("The character appears on the part named");
        ImGui::TextDisabled("\"SpawnLocation\" when you press Play.");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Press Play, then move with WASD + Space.");

    ImGui::End();
}

void drawGameRules(WorldSettings& w) {
    ImGui::SeparatorText("Death");
    const char* styles[] = {"Classic (fall apart)", "Ragdoll"};
    int ds = (int)w.deathStyle;
    if (ImGui::Combo("Death Style", &ds, styles, 2)) w.deathStyle = (DeathStyle)ds;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Classic: the character falls to pieces like in Roblox.\n"
                          "Ragdoll: the body goes limp and tumbles with real joints.");
    ImGui::BeginDisabled(w.deathStyle != DeathStyle::Ragdoll);
    ImGui::Checkbox("Knockdowns", &w.knockdowns);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Big hits (explosions, crashes, hard landings) throw you around as a ragdoll:\n"
                          "you put your arms out to catch yourself, then get back up.");
    ImGui::EndDisabled();
    ImGui::SliderFloat("Spawn ForceField", &w.spawnForceField, 0.0f, 20.0f, "%.0f s");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A glowing shield for a few seconds after spawning.\n"
                          "Blocks damage from TakeDamage, explosions and falls.");

    ImGui::SeparatorText("Players");
    ImGui::Checkbox("Player Collisions", &w.playerCollisions);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: players bump into each other (and can stand on each other's heads).\n"
                          "Off: players walk right through each other.");
    ImGui::Checkbox("Voice Chat", &w.voiceChat);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: players who turned voice chat on can talk to each other (their voices come from their characters).\n"
                          "Off: nobody can talk in this game. Scripts: workspace.VoiceChatEnabled = false");

    ImGui::SeparatorText("Camera");
    ImGui::Checkbox("Orthographic", &w.orthographic);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("No perspective: far things aren't smaller. For 2D, isometric and puzzle games.\n"
                          "Scripts: workspace.Orthographic = true");
    if (w.orthographic) {
        ImGui::DragFloat("Orthographic Size", &w.orthographicSize, 0.5f, 0.0f, 2000.0f, w.orthographicSize > 0 ? "%.1f studs" : "zoom freely");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How many studs tall the screen is. 0 = players zoom with the mouse wheel.\n"
                              "Scripts: workspace.OrthographicSize = 40");
    }
    // Every player's camera to start with (Roblox's StarterPlayer camera settings).
    CameraOptions& pc = w.playerCamera;
    const char* modes[] = {"Classic", "Lock First Person"};
    int mode = pc.lockFirstPerson ? 1 : 0;
    if (ImGui::Combo("Camera Mode", &mode, modes, 2)) pc.lockFirstPerson = mode == 1;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Classic: zoom in and out with the mouse wheel (all the way in = first person).\n"
                          "Lock First Person: players always see through their character's eyes.\n"
                          "Scripts: player.CameraMode = Enum.CameraMode.LockFirstPerson");
    ImGui::BeginDisabled(pc.lockFirstPerson);
    ImGui::DragFloat("Min Zoom Distance", &pc.minZoom, 0.1f, 0.0f, pc.maxZoom, "%.1f studs");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How close players can zoom in. Under 1.2 lets them zoom into first person.\n"
                          "Scripts: player.CameraMinZoomDistance = 10");
    ImGui::DragFloat("Max Zoom Distance", &pc.maxZoom, 1.0f, pc.minZoom, CameraOptions::kLimit, "%.0f studs");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How far out players can zoom.\nScripts: player.CameraMaxZoomDistance = 30");
    ImGui::EndDisabled();
    pc.minZoom = std::clamp(pc.minZoom, 0.0f, CameraOptions::kLimit);
    pc.maxZoom = std::clamp(pc.maxZoom, pc.minZoom, CameraOptions::kLimit);

    ImGui::SeparatorText("Fall damage");
    ImGui::Checkbox("Fall Damage", &w.fallDamage);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn off and players can fall from any height unhurt.");
    ImGui::BeginDisabled(!w.fallDamage);
    ImGui::SliderFloat("Safe Fall Speed", &w.fallDamageSpeed, 5.0f, 60.0f, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Landing slower than this doesn't hurt. Higher = safer falls.");
    ImGui::SliderFloat("Fall Damage Strength", &w.fallDamageScale, 0.1f, 5.0f, "x%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much a hard landing hurts. 2 = twice as much, 0.5 = half.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Gore");
    const char* gore[] = {"Off", "Oil & Bolts", "Blood"};
    int g = (int)w.gore;
    if (ImGui::Combo("Gore", &g, gore, 3)) w.gore = (GoreLevel)g;
    ImGui::BeginDisabled(w.gore == GoreLevel::Off);
    ImGui::Checkbox("Dismemberment", &w.dismemberment);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Big hits (explosions, long falls, saws) knock limbs off");
    ImGui::SliderFloat("Blood Amount", &w.bloodAmount, 0.2f, 3.0f, "x%.1f");
    ImGui::SliderFloat("Pools Last", &w.bloodStay, 3.0f, 120.0f, "%.0f s");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(w.gore != GoreLevel::Blood);
    ImGui::ColorEdit3("Blood Color", &w.bloodColor.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    struct Preset { const char* name; glm::vec3 c; };
    const Preset presets[] = {{"Red", {0.50f, 0.02f, 0.03f}}, {"Slime", {0.25f, 0.65f, 0.08f}},
                              {"Alien", {0.10f, 0.30f, 0.85f}}, {"Ink", {0.05f, 0.05f, 0.08f}}};
    for (const Preset& p : presets) {
        ImGui::SameLine();
        if (ImGui::SmallButton(p.name)) w.bloodColor = p.c;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Classic Roblox rules")) {
        w.deathStyle = DeathStyle::Classic; w.gore = GoreLevel::Off; w.fallDamage = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Full carnage")) {
        w.deathStyle = DeathStyle::Ragdoll; w.gore = GoreLevel::Blood;
        w.dismemberment = true; w.fallDamage = true;
    }
}
