// Emotes you equipped from the catalog show and play as themselves in a game, not as
// "Emote" placeholders. (The bug: an emote file already in this computer's downloads
// folder came back without its name, so every slot was called "Emote", the emote menu
// showed 8 "Emote"s, "/e robot" didn't work, and every slot played the first one.)
//
//   EquippedEmotesTests <a game file with a baseplate and a player>
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Animation.h"
#include "scene/Emotes.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

void play(GameSession& session, float seconds) {
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

// The starter catalog emote with this id, as the file the server stores for it.
std::shared_ptr<const Anim::Clip> starterClip(const std::string& id) {
    for (const Emotes::StarterItem& it : Emotes::starterItems())
        if (it.id == id) {
            Anim::Clip c;
            if (Emotes::parseEmoteFile(it.file.dump(), c)) return std::make_shared<const Anim::Clip>(std::move(c));
        }
    return nullptr;
}

// The name of the emote track playing on the player right now ("" = none).
std::string playingTrack(Scene& scene, Player* p) {
    Anim::Animator& an = scene.animator();
    for (int t : an.playingOn(p->root()->id))
        if (const Anim::Animator::Track* tr = an.track(t)) return tr->name;
    return "";
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: EquippedEmotesTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // --- Filling the slots (what the Player app does when the files arrive) ---
    auto robot = starterClip("emote-robot"), salute = starterClip("emote-salute"), jacks = starterClip("emote-jumping-jacks");
    check(robot && salute && jacks, "the starter Robot, Salute and Jumping Jacks emotes load");
    if (!robot || !salute || !jacks) return 1;

    Emotes::Loadout& l = Emotes::mine();
    check(Emotes::menu().size() == Emotes::builtIn().size() && Emotes::menu()[0].title == "Dance",
          "with nothing equipped, the menu shows the built-in emotes");
    l.slots[0] = Emotes::Slot{"emote-robot", "", nullptr};
    l.slots[1] = Emotes::Slot{"emote-salute", "", nullptr};
    l.slots[3] = Emotes::Slot{"emote-jumping-jacks", "", nullptr};
    check(Emotes::fillSlot(0, "emote-robot", "Robot", robot), "slot 1 takes Robot");
    check(Emotes::fillSlot(1, "emote-salute", "Salute", salute), "slot 2 takes Salute");
    check(Emotes::fillSlot(3, "emote-jumping-jacks", "Jumping Jacks", jacks), "slot 4 takes Jumping Jacks");
    check(!Emotes::fillSlot(2, "emote-shrug", "Shrug", robot), "a file for a slot that changed meanwhile is ignored");

    const auto menu = Emotes::menu();
    check(menu.size() == (size_t)Emotes::kSlots, "the menu has 8 slots");
    check(menu[0].title == "Robot" && menu[0].name == "robot", "slot 1 says Robot");
    check(menu[1].title == "Salute" && menu[1].name == "salute", "slot 2 says Salute");
    check(menu[2].name.empty(), "slot 3 is empty");
    check(menu[3].title == "Jumping Jacks" && menu[3].name == "jumpingjacks", "slot 4 says Jumping Jacks");
    int placeholders = 0;
    for (const auto& e : menu) if (e.title == "Emote") ++placeholders;
    check(placeholders == 0, "no slot is an \"Emote\" placeholder");

    // --- Playing them in a game ---
    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.start();
    Player* p = scene.player();
    if (!p || !p->root()) { std::cerr << "no player in that game\n"; return 1; }
    play(session, 1.5f);   // (land on the ground)

    for (const auto& e : menu) {
        if (e.name.empty()) continue;
        check(p->playEmote(e.name), "the menu's " + e.title + " plays");
        play(session, 0.3f);
        check(p->emote() == e.name && playingTrack(scene, p) == e.title, e.title + " is the move playing (not slot 1's)");
    }
    check(p->playEmote("Robot") && playingTrack(scene, p) == "Robot", "\"/e robot\" finds the equipped Robot");
    check(p->playEmote("dance"), "the built-in Dance still works too");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
