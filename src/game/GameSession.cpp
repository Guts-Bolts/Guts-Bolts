#include "GameSession.h"
#include "../core/Pad.h"
#include "../scene/Serializer.h"
#include "../scripting/LuaApi.h"   // SignalKind (tool events)
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "ClickDetectors.h"
#include "../core/Audio.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

GameSession::GameSession(Scene* scene) : m_scene(scene), m_scripts(scene) {
    m_scripts.setPhysics(&m_physics);
    if (ScriptEngine::testHooks)   // (tests: what the mouse would do)
        m_scripts.testInput = [this](const std::string& what, uint64_t part) {
            const bool on = reachableDetector(part) != nullptr;
            if (what == "hover") return hoverPart(part);
            if (what == "click") click(part);
            else if (what == "right") rightClick(part);
            else if (what == "pad") return clickDetector(part);
            else return false;
            return on;
        };
}

void GameSession::start() {
    m_physics.reset();
    m_scene->particles().clear();
    m_scene->blasts().clear();
    m_scene->camera() = ScriptCamera{};   // workspace.CurrentCamera starts fresh: Custom, following you
    m_scene->coreGui() = CoreGuiState{};  // the emote menu and the rest are back on
    if (Player* p = m_scene->player()) {
        // Player.CharacterRemoving: just before coming back to life (CharacterAdded is below).
        p->onRespawning = [this, p]() { if (m_scripts.running()) m_scripts.fireCharacter(false, p->rootId()); };
        p->beginPlay();
        p->consumeRespawned();   // (arriving isn't a respawn: the scripts hear CharacterAdded once they've started)
    }
    m_running = true;
    // Sounds marked "Autoplay" (e.g. background music) start with the game.
    m_scene->forEach([](SceneNode* n) {
        if (!n->isSound() || !n->autoplay) return;
        glm::vec3 at = n->parent ? glm::vec3(n->parent->worldMatrix()[3]) : glm::vec3(0.0f);
        bool in3d = n->parent && n->parent->isPart();
        n->audioHandle = Audio::play(n->soundId, n->volume, n->pitch, n->looped, in3d ? &at : nullptr);
    });
    // Animations played by scripts pose the character from how it was built
    // (not mid-step), and leave the walking arms and legs to the walk cycle.
    Anim::Animator& an = m_scene->animator();
    an.clear();
    an.restFor = [this](uint64_t rig) -> const Anim::RestPose* {
        Player* p = m_scene->player();
        if (p && p->rootId() == rig && !p->restPose().empty()) return &p->restPose();
        Npc* n = m_scene->npcs().find(rig);
        return n && !n->rest.empty() ? &n->rest : nullptr;
    };
    an.drivenElsewhere = [this](uint64_t rig, const SceneNode* part) {
        Player* p = m_scene->player();
        if (p && p->rootId() == rig) return p->drivesPart(part);
        return m_scene->npcs().find(rig) && Player::isLimb(part);
    };
    m_scene->water().begin(*m_scene);   // (everyone runs the waves; the host runs floating)
    // Character-shaped models become NPCs (before the scripts, so they can steer them).
    if (m_role != Role::Client) m_scene->npcs().begin(*m_scene);
    if (m_role != Role::Client) setupTools();   // before the scripts, so tools' scripts start with the rest
    // Who runs which scripts: a joined player runs the LocalScripts (the host runs the
    // Scripts for everyone); a game server machine runs only Scripts; otherwise both.
    using Mode = ScriptEngine::RunMode;
    m_scripts.setRunMode(m_role == Role::Client ? Mode::Client : m_scripts.noLocalPlayer() ? Mode::Server : Mode::All);
    if (m_role == Role::Client)
        if (Player* p = m_scene->player())   // (our copies of our tools: their LocalScripts hear this)
            p->onToolEquip = [this](uint64_t tool, bool equipped) {
                if (m_scripts.running()) m_scripts.fireTool(equipped ? SignalKind::Equipped : SignalKind::Unequipped, tool);
            };
    m_scripts.start();
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

void GameSession::setupTools() {
    m_starterPack.clear();
    m_noPickupUntil.clear();
    m_toolDown = false;
    Player* p = m_runOnly ? nullptr : m_scene->player();
    // StarterPack tools are templates: everyone gets a copy each time they spawn.
    std::vector<SceneNode*> starters;
    m_scene->forEach([&](SceneNode* n) {
        const bool inStarterPack = n->parent && n->parent->name == "StarterPack" && m_scene->isServiceFolder(n->parent);
        if (n->isTool() && (n->starterTool || inStarterPack) && !m_scene->isCharacterPart(n)) starters.push_back(n);
    });
    for (SceneNode* t : starters)
        if (auto owned = m_scene->detach(t)) m_starterPack.push_back(std::move(owned));
    if (!p) return;
    p->onToolEquip = [this](uint64_t tool, bool equipped) {
        if (m_scripts.running()) m_scripts.fireTool(equipped ? SignalKind::Equipped : SignalKind::Unequipped, tool);
    };
    giveStarterTools();
    p->consumeRespawned();
}

void GameSession::giveStarterTools() {
    Player* p = m_scene->player();
    if (!p) return;
    for (const auto* pack : {&m_starterPack, &m_gear})
        for (const auto& tpl : *pack) {
            SceneNode* copy = m_scene->insert(Serializer::clone(*tpl));
            if (!p->give(copy)) { m_scene->removeNode(copy); continue; }
            m_scripts.runScriptsIn(copy);   // (does nothing before the scripts have started)
        }
}

void GameSession::addGear(std::unique_ptr<SceneNode> tool) {
    if (!m_running || m_role == Role::Client || !tool || !tool->isTool()) return;
    tool->starterTool = false;
    m_gear.push_back(std::move(tool));
    Player* p = m_scene->player();
    if (!p) return;
    SceneNode* copy = m_scene->insert(Serializer::clone(*m_gear.back()));
    if (!p->give(copy)) { m_scene->removeNode(copy); return; }
    m_scripts.runScriptsIn(copy);
}

void GameSession::pickUpTools(const std::vector<TouchEvent>& touches) {
    Player* p = m_scene->player();
    SceneNode* me = p ? p->root() : nullptr;
    if (!me || p->isDead()) return;
    for (const TouchEvent& t : touches) {
        SceneNode* part = m_scene->findById(t.partId);
        SceneNode* other = m_scene->findById(t.otherId);
        if (!part || !other || part->name != "Handle" || !part->parent || !part->parent->isTool()) continue;
        if (other != me && other->parent != me) continue;             // only your own character picks things up
        SceneNode* tool = part->parent;
        if (m_scene->isCharacterPart(tool)) continue;                  // someone's already got it
        if (auto it = m_noPickupUntil.find(tool->id); it != m_noPickupUntil.end() && m_time < it->second) continue;
        p->give(tool);
    }
}

void GameSession::reachCheckpoints(const std::vector<TouchEvent>& touches) {
    Player* p = m_scene->player();
    SceneNode* me = p ? p->root() : nullptr;
    if (!me || p->isDead()) return;
    for (const TouchEvent& t : touches) {
        SceneNode* part = m_scene->findById(t.partId);
        SceneNode* other = m_scene->findById(t.otherId);
        if (part && other && part->isSpawnLocation() && (other == me || other->parent == me))
            m_scripts.spawnTouched(me->id, part);   // (AllowTeamChangeOnTouch: onto its team)
        if (!part || !other || part->name != "Checkpoint" || p->checkpoint() == part->id) continue;
        if (other != me && other->parent != me) continue;
        p->setCheckpoint(part->id);
        glm::vec3 at(part->worldMatrix()[3]);
        Audio::play("coin", 0.5f, 1.4f, false, &at);
        if (m_scripts.gui().messageTime <= 0.0f) {   // don't talk over the game's own messages
            m_scripts.gui().message = "Checkpoint!";
            m_scripts.gui().messageTime = 1.5f;
        }
    }
}

void GameSession::selectToolSlot(int slot) {
    if (!m_running) return;
    Player* p = m_scene->player();
    if (!p || p->isDead()) return;
    if (m_role == Role::Client) {   // the host has the real tools: ask it (it sends back what we hold)
        auto list = p->tools();
        if (slot < 0 || slot >= (int)list.size()) return;
        SceneNode* t = list[(size_t)slot];
        if (onToolRequest) onToolRequest("equip", t == p->equippedTool() ? 0 : t->id, false);
        return;
    }
    p->toggleSlot(slot);
}

void GameSession::dropTool() {
    if (!m_running) return;
    Player* p = m_scene->player();
    if (!p) return;
    if (m_role == Role::Client) {
        if (SceneNode* t = p->equippedTool(); t && t->canBeDropped && onToolRequest) onToolRequest("drop", t->id, false);
        return;
    }
    if (SceneNode* t = p->drop()) m_noPickupUntil[t->id] = m_time + 2.0;
}

// ---------------------------------------------------------------------------
// People who joined your game: their tools live on their character here, so the
// tools' scripts run here like everyone else's (tool.Parent is their character).
// ---------------------------------------------------------------------------

std::vector<SceneNode*> GameSession::joinerTools(uint64_t rig) {
    std::vector<SceneNode*> out;
    RemoteCharacter* rc = m_scene->findRemote(rig);
    SceneNode* r = m_scene->findById(rig);
    if (!rc || !r) return out;
    Player::syncSlotList(*m_scene, rc->toolSlots, r);
    for (uint64_t id : rc->toolSlots) if (SceneNode* t = m_scene->findById(id)) out.push_back(t);
    return out;
}

bool GameSession::giveTo(uint64_t rig, SceneNode* tool) {
    SceneNode* r = m_scene->findById(rig);
    if (!r || !tool || !tool->isTool()) return false;
    if ((int)joinerTools(rig).size() >= Player::kMaxTools) return false;
    SceneNode* bag = Player::backpackOf(*m_scene, r);
    if (!bag || tool->parent == bag) return false;
    auto owned = m_scene->detach(tool);
    if (!owned) return false;
    owned->transform = Transform{};
    bag->addChild(std::move(owned));
    m_scene->markDirty();
    joinerTools(rig);
    return true;
}

void GameSession::joinerArrived(uint64_t rig) {
    if (!m_running || m_role == Role::Client) return;
    for (const auto& tpl : m_starterPack) {   // (not your gear: that's yours)
        SceneNode* copy = m_scene->insert(Serializer::clone(*tpl));
        if (!giveTo(rig, copy)) { m_scene->removeNode(copy); continue; }
        m_scripts.runScriptsIn(copy);
    }
}

void GameSession::joinerRespawned(uint64_t rig) {
    if (!m_running || m_role == Role::Client) return;
    m_scripts.fireCharacter(false, rig);   // Player.CharacterRemoving (then CharacterAdded, below)
    for (SceneNode* t : joinerTools(rig)) {
        m_scripts.stopScripts(t);
        m_scene->removeNode(t);
    }
    if (RemoteCharacter* rc = m_scene->findRemote(rig)) rc->toolSlots.clear();
    joinerArrived(rig);
    m_scripts.fireCharacter(true, rig);
}

void GameSession::joinerEquip(uint64_t rig, uint64_t toolId) {
    RemoteCharacter* rc = m_scene->findRemote(rig);
    SceneNode* r = m_scene->findById(rig);
    if (!m_running || m_role == Role::Client || !rc || !r || !rc->alive) return;
    Player::equipOn(*m_scene, r, toolId, [this](uint64_t id, bool on) {
        if (m_scripts.running()) m_scripts.fireTool(on ? SignalKind::Equipped : SignalKind::Unequipped, id);
    });
}

void GameSession::joinerDrop(uint64_t rig) {
    SceneNode* r = m_scene->findById(rig);
    SceneNode* held = Player::heldTool(r);
    if (!m_running || m_role == Role::Client || !held || !held->canBeDropped) return;
    const uint64_t id = held->id;
    if (SceneNode* t = Player::layDown(*m_scene, r)) m_noPickupUntil[t->id] = m_time + 2.0;
    if (m_scripts.running()) m_scripts.fireTool(SignalKind::Unequipped, id);
    joinerTools(rig);
}

void GameSession::joinerUse(uint64_t rig, bool down) {
    RemoteCharacter* rc = m_scene->findRemote(rig);
    SceneNode* t = Player::heldTool(m_scene->findById(rig));
    if (!m_running || m_role == Role::Client || !rc || !t || !m_scripts.running()) return;
    if (down && (!t->enabled || !rc->alive)) return;
    m_scripts.fireTool(down ? SignalKind::Activated : SignalKind::Deactivated, t->id);
}

void GameSession::joinerTouched(uint64_t rig, uint64_t partId) {
    RemoteCharacter* rc = m_scene->findRemote(rig);
    SceneNode* part = m_scene->findById(partId);
    if (!m_running || m_role == Role::Client || !rc || !rc->alive || !part) return;
    if (part->isSpawnLocation()) m_scripts.spawnTouched(rig, part);   // (AllowTeamChangeOnTouch)
    if (part->name != "Handle" || !part->parent || !part->parent->isTool()) return;
    SceneNode* tool = part->parent;
    if (m_scene->isCharacterPart(tool)) return;   // someone's already got it
    if (auto it = m_noPickupUntil.find(tool->id); it != m_noPickupUntil.end() && m_time < it->second) return;
    giveTo(rig, tool);
}

void GameSession::holdTools() {
    // Everyone else's held tool follows their hand (their arm moves with their pose).
    for (const RemoteCharacter& rc : m_scene->remotes())
        if (SceneNode* r = m_scene->findById(rc.rootId))
            if (SceneNode* t = Player::heldTool(r)) Player::placeInHand(r, t);
}

void GameSession::stop() {
    m_scripts.stop();
    m_scene->npcs().end();
    m_scene->water().end();
    m_scene->animator().clear();
    if (Player* p = m_scene->player()) { p->onToolEquip = nullptr; p->onRespawning = nullptr; }
    m_starterPack.clear();
    m_gear.clear();
    Audio::stopAll();
    if (Player* p = m_scene->player()) p->endPlay();
    m_physics.reset();
    m_scene->particles().clear();
    m_scene->blasts().clear();
    m_scene->camera() = ScriptCamera{};
    m_running = false;
    m_hoverDetector = 0;
}

void GameSession::update(float dt, float cameraYaw, bool acceptInput, float swimLook) {
    if (!m_running) return;
    dt = std::min(dt, 1.0f / 30.0f);   // big hitches would let things tunnel

    const bool client = m_role == Role::Client;
    m_time += dt;

    // 1. Scripts: wake up waits, keyboard events, Heartbeat.
    m_scripts.update(dt);   // (a joined player's LocalScripts)

    // 2. Physics for loose (unanchored) parts (the host does this for everyone).
    m_physics.gather(*m_scene);
    if (!client) {
        std::vector<uint64_t> fallen;
        m_physics.stepParts(*m_scene, dt, fallen);
        for (uint64_t id : fallen)
            if (SceneNode* n = m_scene->findById(id)) m_scene->removeNode(n);
        if (!fallen.empty()) m_physics.gather(*m_scene);
    }

    // 3. The character, driven by WASD / Space relative to the camera.
    if (Player* p = m_runOnly ? nullptr : m_scene->player()) {
        glm::vec3 move(0.0f);
        bool jump = false, dive = false;
        if (acceptInput && !ImGui::GetIO().WantTextInput) {
            float yaw = glm::radians(cameraYaw);
            glm::vec3 fwd   = glm::normalize(glm::vec3(-std::cos(yaw), 0.0f, -std::sin(yaw)));
            glm::vec3 right = glm::vec3(-fwd.z, 0.0f, fwd.x);
            if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow))    move += fwd;
            if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow))  move -= fwd;
            if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) move += right;
            if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow))  move -= right;
            jump = ImGui::IsKeyDown(ImGuiKey_Space);
            dive = ImGui::IsKeyDown(ImGuiKey_C) || ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
            // Touch thumbstick: partly pushed = walk slower.
            move += right * m_touchMove.x + fwd * m_touchMove.y;
            jump = jump || m_touchJump;
            // A controller: left stick walks (partly pushed = slower), A jumps, LT dives.
            if (Pad::connected()) {
                const glm::vec2 ls = Pad::leftStick();
                move += right * ls.x + fwd * ls.y;
                jump = jump || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
                dive = dive || Pad::leftTrigger() > 0.5f;
            }
        }
        // Tools: 1-9 picks a slot (again puts it away), Backspace drops the held one.
        if (acceptInput && !ImGui::GetIO().WantTextInput) {
            for (int i = 0; i < Player::kMaxTools; ++i)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) selectToolSlot(i);
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) dropTool();
            // A controller: RB / LB pick the next / last tool, B puts it away, RT uses it.
            if (Pad::connected() && !p->isDead()) {
                const auto tools = p->tools();
                int held = -1;
                for (size_t i = 0; i < tools.size(); ++i) if (tools[i] == p->equippedTool()) held = (int)i;
                const int n = (int)tools.size();
                if (n && ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) selectToolSlot(held < 0 ? 0 : (held + 1) % n);
                if (n && ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) selectToolSlot(held < 0 ? n - 1 : (held + n - 1) % n);
                if (held >= 0 && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) selectToolSlot(held);
                const bool trigger = Pad::rightTrigger() > 0.5f;
                if (trigger && !m_padTrigger) useTool();
                m_padTrigger = trigger;
            }
        }
        if (m_toolDown && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !m_padTrigger) {
            m_toolDown = false;
            if (SceneNode* t = p->equippedTool()) {
                if (client && onToolRequest) onToolRequest("use", t->id, false);
                m_scripts.fireTool(SignalKind::Deactivated, t->id);
            }
        }
        p->setSwimInput(acceptInput ? swimLook : 0.0f, dive);
        p->update(dt, move, jump, m_physics);
        if (p->isDead()) m_scene->animator().stopRig(p->rootId(), false, *m_scene);   // the body falls apart instead
        if (p->consumeDied()) m_scripts.fireDied(p->rootId());
        if (p->consumeRespawned()) {
            if (!client) {
                // Like Roblox: you come back with just the StarterPack tools.
                for (SceneNode* t : p->tools()) m_scripts.stopScripts(t);
                p->clearTools();
                giveStarterTools();
            }
            m_scripts.fireCharacter(true, p->rootId());   // Player.CharacterAdded
        }
    }

    // NPCs walk where their scripts sent them (the host moves them for everyone).
    if (!client) {
        NpcSystem& npcs = m_scene->npcs();
        npcs.update(dt, *m_scene, m_physics);
        for (uint64_t id : npcs.died) {
            m_scene->animator().stopRig(id, false, *m_scene);
            m_scripts.fireDied(id);
        }
        for (auto [id, reached] : npcs.finished) m_scripts.fireMoveToFinished(id, reached);
        npcs.died.clear();
        npcs.finished.clear();
    }

    holdTools();

    // Waves, ripples and splashes.
    m_scene->water().update(dt, *m_scene);

    // Animations (after the character has walked, so they win where they pose).
    m_scene->animator().update(dt, *m_scene);
    if (!client) m_scripts.fireAnimationEvents();
    else m_scene->animator().events.clear();

    // Sounds inside moving parts follow them.
    m_scene->forEach([](SceneNode* n) {
        if (n->isSound() && n->audioHandle && n->parent && n->parent->isPart())
            Audio::setPosition(n->audioHandle, glm::vec3(n->parent->worldMatrix()[3]));
    });

    // Blood, oil, sparks, smoke...
    const WorldSettings& ws = m_scene->world();
    m_scene->particles().setBlood(ws.bloodColor, ws.bloodAmount, ws.bloodStay);
    m_scene->particles().update(dt, m_scene->world().gravity, m_physics);
    // ParticleEmitters, Fire, Smoke, Sparkles: every screen makes its own (a game server
    // with no screen doesn't bother).
    if (!Mesh::headless) m_scene->particles().runEmitters(*m_scene, dt);
    m_scene->blasts().update(dt, *m_scene, m_physics);   // explosions (the host pushes things; everyone sees them)

    // 4. Touched events (after everything has moved).
    std::vector<TouchEvent> touches;
    m_physics.collectTouches(*m_scene, touches);
    if (client) {
        // Only our own character's touches matter here: the host's scripts hear about
        // them, and so do our LocalScripts.
        Player* p = m_scene->player();
        for (const TouchEvent& t : touches) {
            SceneNode* limb = m_scene->findById(t.otherId);
            if (!p || !limb || !limb->parent || limb->parent->id != p->rootId()) continue;
            if (onTouch) onTouch(t.partId, limb->name);
            if (m_scripts.running()) m_scripts.fireTouched(t.partId, t.otherId);
        }
        return;
    }
    for (const TouchEvent& t : touches) {
        if (!m_scripts.running()) break;
        m_scripts.fireTouched(t.partId, t.otherId);
    }
    pickUpTools(touches);
    reachCheckpoints(touches);
}

void GameSession::guiEvents(const std::vector<GameGui::Event>& events) {
    if (!m_running) return;
    for (const GameGui::Event& e : events) {
        if (m_role == Role::Client) {   // the host's scripts hear about clicks and typing (and our LocalScripts, below)
            if (e.kind == GameGui::EventKind::Click && onGuiClick) onGuiClick(e.id);
            if (e.kind == GameGui::EventKind::FocusLost && onGuiText)
                if (SceneNode* box = m_scene->findById(e.id)) onGuiText(e.id, box->gui.text, e.enter);
            if (onPrompt) {
                if (e.kind == GameGui::EventKind::PromptTriggered)    onPrompt(e.id, "t");
                if (e.kind == GameGui::EventKind::PromptTriggerEnded) onPrompt(e.id, "te");
                if (e.kind == GameGui::EventKind::PromptHoldBegan)    onPrompt(e.id, "hb");
                if (e.kind == GameGui::EventKind::PromptHoldEnded)    onPrompt(e.id, "he");
            }
        }
        switch (e.kind) {
            case GameGui::EventKind::Click:       m_scripts.fireGui(SignalKind::GuiClick, e.id); break;
            case GameGui::EventKind::Enter:       m_scripts.fireGui(SignalKind::GuiEnter, e.id); break;
            case GameGui::EventKind::Leave:       m_scripts.fireGui(SignalKind::GuiLeave, e.id); break;
            case GameGui::EventKind::Focused:     m_scripts.fireGui(SignalKind::GuiFocused, e.id); break;
            case GameGui::EventKind::FocusLost:   m_scripts.fireFocusLost(e.id, e.enter); break;
            case GameGui::EventKind::TextChanged: m_scripts.firePropertyChanged(e.id, "Text"); break;
            case GameGui::EventKind::PromptShown:        m_scripts.firePrompt(SignalKind::PromptShown, e.id); break;
            case GameGui::EventKind::PromptHidden:       m_scripts.firePrompt(SignalKind::PromptHidden, e.id); break;
            case GameGui::EventKind::PromptHoldBegan:    m_scripts.firePrompt(SignalKind::PromptHoldBegan, e.id); break;
            case GameGui::EventKind::PromptHoldEnded:    m_scripts.firePrompt(SignalKind::PromptHoldEnded, e.id); break;
            case GameGui::EventKind::PromptTriggered:    m_scripts.firePrompt(SignalKind::PromptTriggered, e.id); break;
            case GameGui::EventKind::PromptTriggerEnded: m_scripts.firePrompt(SignalKind::PromptTriggerEnded, e.id); break;
        }
    }
}

// The ClickDetector this part belongs to, if our character is close enough to click it.
// Like Roblox, not while holding a tool (the click is the tool's).
SceneNode* GameSession::reachableDetector(uint64_t partId) {
    SceneNode* part = partId ? m_scene->findById(partId) : nullptr;
    SceneNode* d = ClickDetectors::find(*m_scene, part);
    if (!d || d->click.range <= 0.0f) return nullptr;
    if (Player* p = m_runOnly ? nullptr : m_scene->player()) {
        if (p->isDead() || !p->root() || p->equippedTool()) return nullptr;
        if (ClickDetectors::distanceToPart(part, p->position()) > d->click.range) return nullptr;
    }
    return d;
}

// Here (our LocalScripts; and the game's Scripts when they run here) and, when we've
// joined someone's game, the host (its Scripts).
void GameSession::fireDetector(SignalKind kind, const char* what, uint64_t detector) {
    if (m_role == Role::Client && onClickDetector) onClickDetector(detector, what);
    m_scripts.fireClickDetector(kind, detector);
}

bool GameSession::hoverPart(uint64_t partId, std::string* cursor) {
    SceneNode* d = m_running ? reachableDetector(partId) : nullptr;
    const uint64_t now = d ? d->id : 0;
    if (now != m_hoverDetector) {
        if (m_hoverDetector && m_scene->findById(m_hoverDetector))
            fireDetector(SignalKind::ClickHoverLeave, "hl", m_hoverDetector);
        m_hoverDetector = now;
        if (now) fireDetector(SignalKind::ClickHoverEnter, "he", now);
    }
    if (cursor) *cursor = d ? d->click.cursor : std::string();
    return d != nullptr;
}

void GameSession::rightClick(uint64_t partId) {
    if (!m_running) return;
    if (SceneNode* d = reachableDetector(partId)) fireDetector(SignalKind::ClickRightClick, "r", d->id);
}

bool GameSession::clickDetector(uint64_t partId) {
    if (!m_running) return false;
    SceneNode* d = reachableDetector(partId);
    if (d) fireDetector(SignalKind::ClickMouseClick, "c", d->id);
    return d != nullptr;
}

void GameSession::click(uint64_t partId) {
    if (!m_running) return;
    clickDetector(partId);
    if (m_role == Role::Client) {
        if (onClick && partId) onClick(partId);
        if (partId) m_scripts.fireClicked(partId);   // (our LocalScripts)
        // Holding a tool: swing here, and the host fires tool.Activated (and so do we, for its LocalScripts).
        useTool();
        return;
    }
    m_scripts.fireClicked(partId);
    useTool();   // holding a tool: clicking uses it (tool.Activated), like Roblox
}

void GameSession::useTool() {
    Player* p = m_runOnly ? nullptr : m_scene->player();
    SceneNode* t = p ? p->equippedTool() : nullptr;
    if (!m_running || !t || !t->enabled || p->isDead()) return;
    if (m_role == Role::Client && onToolRequest) onToolRequest("use", t->id, true);   // the host fires it for everyone
    m_scripts.fireTool(SignalKind::Activated, t->id);
    p->swingTool();
    m_toolDown = true;
}
