// Scene objects for scripts: Instance (parts, models, scripts), Humanoid,
// Lighting, and the Signal / Connection event objects.
#include "LuaApi.h"
#include "../online/Protocol.h"
#include "ScriptEngine.h"
#include "../scene/Animation.h"
#include "../scene/Physics.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../scene/RobloxMesh.h"
#include "../scene/EditMesh.h"
#include "../scene/Skin.h"
#include "../scene/Guis.h"
#include "../game/GameGui.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Audio.h"
#include "../scene/SoundMix.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr const char* kInst  = "Instance";
constexpr const char* kSig   = "RBXScriptSignal";
constexpr const char* kConn  = "RBXScriptConnection";
constexpr const char* kHum   = "Humanoid";
constexpr const char* kLight = "Lighting";
constexpr const char* kCam   = "Camera";
constexpr const char* kTrack = "AnimationTrack";
constexpr const char* kAnimr = "Animator";

struct InstRef   { uint64_t id; };
struct SignalRef { SignalKind kind; uint64_t id; };
struct ConnRef   { int index; };

bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

bool parseMaterial(const std::string& s, Material& out) { return materialFromName(s, out); }

const char* shapeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Sphere:   return "Ball";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Plane:    return "Plane";
        default:                      return "Block";
    }
}
bool parseShape(const std::string& s, PrimitiveType& out) {
    if (s == "Block" || s == "Cube" || s == "Part") { out = PrimitiveType::Cube;     return true; }
    if (s == "Ball"  || s == "Sphere")              { out = PrimitiveType::Sphere;   return true; }
    if (s == "Cylinder")                            { out = PrimitiveType::Cylinder; return true; }
    if (s == "Plane")                               { out = PrimitiveType::Plane;    return true; }
    return false;
}

ScriptEngine* E(lua_State* L) { return LuaApi::engine(L); }

// Apply MeshId / TextureId writes to imported Roblox mesh instances. SpecialMesh
// properties are represented by a child Model; MeshPart properties live on the Part.
void refreshRobloxMeshProperty(lua_State* L, SceneNode* instance, const std::string& property) {
    if (!instance || (property != "MeshId" && property != "TextureId" && property != "TextureID")) return;
    const Attribute* classAttr = instance->findAttribute("RobloxClass");
    if (!classAttr || classAttr->type != Attribute::String) return;
    const bool meshPart = classAttr->s == "MeshPart";
    if (!meshPart && classAttr->s != "SpecialMesh" && classAttr->s != "FileMesh") return;
    SceneNode* part = meshPart ? instance : instance->parent;
    if (!part || !part->isPart()) return;

    auto stringAttr = [](const SceneNode* n, const char* name) -> std::string {
        const Attribute* a = n ? n->findAttribute(name) : nullptr;
        return a && a->type == Attribute::String ? a->s : std::string{};
    };
    auto vectorAttr = [](const SceneNode* n, const char* name, glm::vec3 fallback) -> glm::vec3 {
        const Attribute* a = n ? n->findAttribute(name) : nullptr;
        return a && (a->type == Attribute::Vector3 || a->type == Attribute::Color3) ? a->v : fallback;
    };
    auto setVectorAttr = [](SceneNode* n, const char* name, glm::vec3 value) {
        for (Attribute& a : n->attributes) if (a.name == name) { a.type = Attribute::Vector3; a.v = value; return; }
        Attribute a; a.name = name; a.type = Attribute::Vector3; a.v = value; n->attributes.push_back(a);
    };

    if (property == "TextureId" || property == "TextureID") {
        const std::string url = stringAttr(instance, property.c_str());
        const std::string id = RobloxMesh::assetId(url);
        if (!url.empty() && id.empty()) return; // don't clear a working texture for an unsupported built-in URI
        const std::string file = id.empty() ? std::string{} : RobloxMesh::fetch(id, ".png");
        part->texture = file;
        if (!meshPart) {
            part->color = !file.empty() ? vectorAttr(instance, "VertexColor", glm::vec3(1.0f))
                                        : vectorAttr(part, "RobloxBaseColor", part->color);
        }
        E(L)->scene()->markDirty();
        return;
    }

    if (!meshPart && classAttr->s == "SpecialMesh") {
        const Attribute* type = instance->findAttribute("MeshType");
        if (type && type->type == Attribute::Number && (int)type->n != 5) return;
    }
    const std::string url = stringAttr(instance, "MeshId");
    const std::string id = RobloxMesh::assetId(url);
    if (id.empty()) return;
    std::string error;
    RobloxMesh::Shape shape = RobloxMesh::loadAsset(url, &error);
    if (!shape.mesh) return;  // Keep the working mesh if a replacement asset cannot be resolved.

    if (!meshPart) {
        const glm::vec3 scale = glm::abs(vectorAttr(instance, "Scale", glm::vec3(1.0f)));
        const glm::vec3 oldCenter = vectorAttr(instance, "MeshCenter", glm::vec3(0.0f));
        glm::mat4 rotation(1.0f);
        rotation = glm::rotate(rotation, glm::radians(part->transform.rotation.z), {0, 0, 1});
        rotation = glm::rotate(rotation, glm::radians(part->transform.rotation.y), {0, 1, 0});
        rotation = glm::rotate(rotation, glm::radians(part->transform.rotation.x), {1, 0, 0});
        const glm::vec3 offsetDelta = (shape.center - oldCenter) * scale * 0.5f;
        part->transform.position += glm::vec3(rotation * glm::vec4(offsetDelta, 0.0f));
        part->transform.scale = glm::max(shape.size * scale * 0.5f, glm::vec3(1e-3f));
        setVectorAttr(instance, "MeshCenter", shape.center);
    } else {
        setVectorAttr(instance, "MeshCenter", shape.center);
    }

    MeshEdit::attach(*part, shape.mesh);
    part->primitiveType = PrimitiveType::Mesh;

    auto embeddedBone = [](SceneNode* n) {
        return n && n->robloxEmbeddedMeshBone;
    };
    auto containsAuthoredBone = [&](auto&& self, SceneNode* n) -> bool {
        if (!n) return false;
        if (n->isBone() && !embeddedBone(n)) return true;
        for (auto& child : n->children) if (self(self, child.get())) return true;
        return false;
    };
    bool hasAuthoredBones = false;
    for (auto& child : part->children)
        if (containsAuthoredBone(containsAuthoredBone, child.get())) { hasAuthoredBones = true; break; }

    // Remove only joints generated from the previous mesh. Preserve Bone instances
    // in the imported model because scripts and animations may refer to those nodes.
    for (auto it = part->children.begin(); it != part->children.end();) {
        if ((*it)->isBone() && embeddedBone(it->get())) it = part->children.erase(it);
        else ++it;
    }
    if (!shape.bones.empty() && part->editMesh && !hasAuthoredBones) {
        std::vector<SceneNode*> imported(shape.bones.size(), nullptr);
        std::vector<glm::mat4> sourceFrames(shape.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < shape.bones.size(); ++i) {
            const auto& source = shape.bones[i];
            glm::vec3 normalized = (source.position - shape.center) /
                                   glm::max(shape.size, glm::vec3(1e-5f));
            glm::mat4 worldFrame = glm::translate(glm::mat4(1.0f), normalized * part->transform.scale) *
                                   glm::mat4(source.rotation);
            SceneNode* parent = source.parent >= 0 && (size_t)source.parent < i ? imported[(size_t)source.parent] : nullptr;
            glm::mat4 localFrame = parent ? glm::inverse(sourceFrames[(size_t)source.parent]) * worldFrame : worldFrame;
            auto bone = std::make_unique<SceneNode>(source.name, NodeKind::Attachment);
            bone->bone = true;
            bone->robloxEmbeddedMeshBone = true;
            bone->transform.position = glm::vec3(localFrame[3]);
            float rz, ry, rx;
            glm::extractEulerAngleZYX(localFrame, rz, ry, rx);
            bone->transform.rotation = glm::degrees(glm::vec3(rx, ry, rz));
            sourceFrames[i] = worldFrame;
            imported[i] = parent ? parent->addChild(std::move(bone)) : part->addChild(std::move(bone));
        }
    }
    if (!shape.bones.empty() && part->editMesh) {
        const std::vector<SceneNode*> bones = Skin::bones(part);
        part->editMesh->bind.resize(part->editMesh->bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < part->editMesh->bones.size(); ++i)
            for (SceneNode* bone : bones)
                if (bone && bone->name == part->editMesh->bones[i]) {
                    part->editMesh->bind[i] = glm::inverse(Skin::frame(bone, part));
                    break;
                }
        MeshEdit::refresh(*part);
    }
    E(L)->scene()->markDirty();
}

bool isCharacterRoot(lua_State* L, const SceneNode* n) {
    return E(L)->scene()->isCharacterRoot(n->id);
}

// A player's character or an NPC: something with a Humanoid.
bool hasHumanoid(lua_State* L, const SceneNode* n) {
    return isCharacterRoot(L, n) || (n->kind == NodeKind::Model && E(L)->scene()->npcs().find(*E(L)->scene(), n->id));
}

// FluidVolume (Instance.new("FluidVolume")): a water part with the FluidVolume tag.
bool isFluidVolume(const SceneNode* n) {
    return n->isPart() && std::find(n->tags.begin(), n->tags.end(), "FluidVolume") != n->tags.end();
}
float numAttr(const SceneNode* n, const char* name, float fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Number ? (float)a->n : fallback;
}
void setAttr(SceneNode* n, const char* name, Attribute::Type t, double num, glm::vec3 v = glm::vec3(0.0f)) {
    for (Attribute& a : n->attributes)
        if (a.name == name) { a.type = t; a.n = num; a.v = v; return; }
    Attribute a;
    a.name = name; a.type = t; a.n = num; a.v = v;
    n->attributes.push_back(a);
}

const char* className(lua_State* L, const SceneNode* n) {
    if (n == E(L)->scene()->root()) return "Workspace";
    if (n == E(L)->scene()->cameraNode()) return "Camera";
    if (E(L)->scene()->isServiceFolder(n)) return n->name.c_str();   // StarterPack, ServerScriptService...
    switch (n->kind) {
        case NodeKind::Model:
            // A stand-in for a Roblox object we don't have (an imported SpecialMesh): it
            // says what it was, and keeps its properties as attributes.
            for (const Attribute& a : n->attributes)
                if (a.name == "RobloxClass" && a.type == Attribute::String) {
                    static std::string cls;
                    cls = a.s;
                    return cls.c_str();
                }
            return "Model";
        case NodeKind::Script: return n->isModule ? "ModuleScript" : n->isLocal ? "LocalScript" : "Script";
        case NodeKind::Remote: return n->remoteFunction ? "RemoteFunction" : "RemoteEvent";
        case NodeKind::Prompt: return "ProximityPrompt";
        case NodeKind::Highlight: return "Highlight";
        case NodeKind::ClickDetector: return "ClickDetector";
        case NodeKind::Trail:     return "Trail";
        case NodeKind::Beam:      return "Beam";
        case NodeKind::PostEffect: return kPostEffectClassNames[(int)n->post.type];
        case NodeKind::Sky:        return "Sky";
        case NodeKind::Particles: return n->particleClass();
        case NodeKind::Light:  return n->lightType == LightType::Spot ? "SpotLight" : "PointLight";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Tool:       return "Tool";
        case NodeKind::Value:      return n->valueClass();
        case NodeKind::Decal:      return "Decal";
        case NodeKind::Animation:  return "Animation";
        case NodeKind::Gui:        return kGuiClassNames[(int)n->gui.type];
        case NodeKind::Camera:     return "Camera";
        case NodeKind::Sound:      return "Sound";
        case NodeKind::SoundGroup: return "SoundGroup";
        case NodeKind::SoundEffect: return SoundFx::kClassNames[(int)n->soundFx.type];
        case NodeKind::Attachment: return n->bone ? "Bone" : "Attachment";
        case NodeKind::FluidSystem:  return "FluidSystem";
        case NodeKind::FluidEmitter: return "FluidEmitter";
        case NodeKind::Mover:        return kMoverClassNames[(int)n->mover.type];
        case NodeKind::Constraint:
            return kConstraintClassNames[std::clamp((int)n->constraintType, 0, kConstraintTypeCount - 1)];
        default:
            if (isFluidVolume(n)) return "FluidVolume";
            if (n->isSpawnLocation()) return "SpawnLocation";
            return n->primitiveType == PrimitiveType::Mesh ? "MeshPart" : "Part";
    }
}

bool isA(lua_State* L, const SceneNode* n, const std::string& cls) {
    if (cls == "Instance") return true;
    std::string mine = className(L, n);
    if (cls == mine) return true;
    if (n->kind == NodeKind::Part && (cls == "BasePart" || cls == "Part" || cls == "PVInstance")) return true;
    if (n->kind == NodeKind::Model && (cls == "Model" || cls == "Folder" || cls == "PVInstance")) return true;
    if (n->kind == NodeKind::Model && cls == "WorldRoot" && mine == "WorldModel") return true;
    if (n->kind == NodeKind::Script && (cls == "LuaSourceContainer" || (cls == "BaseScript" && !n->isModule) ||
                                        (cls == "Script" && n->isLocal))) return true;   // LocalScript is a Script
    if (n->kind == NodeKind::Light && cls == "Light") return true;
    if (n->isBone() && cls == "Attachment") return true;   // a Bone is an Attachment
    if (n->isPostEffect() && cls == "PostEffect") return true;
    if (n->kind == NodeKind::Constraint && cls == "Constraint") return true;
    if (n->isMover() && cls == (isBodyMover(n->mover.type) ? "BodyMover" : "Constraint")) return true;
    if (n->kind == NodeKind::Tool && cls == "BackpackItem") return true;
    if (n->kind == NodeKind::Value && cls == "ValueBase") return true;
    if (n->kind == NodeKind::Decal && cls == "FaceInstance") return true;
    if (n->isSoundEffect() && cls == "SoundEffect") return true;
    if (n->isGui()) {
        if (cls == "GuiBase" || (!isGuiModifier(n->gui.type) && cls == "GuiBase2d")) return true;
        if (n->gui.type == GuiType::ScreenGui && (cls == "LayerCollector" || cls == "BasePlayerGui")) return true;
        if (isGuiLayer(n->gui.type) && cls == "LayerCollector") return true;
        if (n->gui.type == GuiType::SurfaceGui && cls == "SurfaceGuiBase") return true;
        if (n->isGuiObject() && cls == "GuiObject") return true;
        if (n->isGuiButton() && cls == "GuiButton") return true;
        if (isGuiModifier(n->gui.type) && cls == "UIComponent") return true;
        if (isGuiLayout(n->gui.type) && (cls == "UIGridStyleLayout" || cls == "UILayout")) return true;
        if (n->gui.type == GuiType::UIAspectRatioConstraint && cls == "UIConstraint") return true;
    }
    return false;
}

// Rotation-only matrix from Euler degrees (same order as Transform::matrix).
glm::mat4 rotationMatrix(const glm::vec3& deg) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(deg.z), {0, 0, 1});
    m = glm::rotate(m, glm::radians(deg.y), {0, 1, 0});
    m = glm::rotate(m, glm::radians(deg.x), {1, 0, 0});
    return m;
}

glm::vec3 worldPosition(const SceneNode* n) { return glm::vec3(n->worldMatrix()[3]); }

void setWorldPosition(lua_State* L, SceneNode* n, const glm::vec3& p) {
    // Moving the character's HumanoidRootPart teleports the whole character.
    if (n->parent && hasHumanoid(L, n->parent) && n->name == "HumanoidRootPart") {
        n->parent->transform.position += p - worldPosition(n);
        return;
    }
    glm::vec3 local = p;
    if (n->parent) local = glm::vec3(glm::inverse(n->parentFrame()) * glm::vec4(p, 1.0f));
    n->transform.position = local;
}

glm::mat4 getCFrame(const SceneNode* n) {
    glm::mat4 m = rotationMatrix(n->transform.rotation);
    m[3] = glm::vec4(worldPosition(n), 1.0f);
    return m;
}

void setCFrame(lua_State* L, SceneNode* n, const glm::mat4& cf) {
    glm::mat3 r(cf);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    float z, y, x;
    glm::extractEulerAngleZYX(glm::mat4(r), z, y, x);
    n->transform.rotation = glm::degrees(glm::vec3(x, y, z));
    setWorldPosition(L, n, glm::vec3(cf[3]));
}

// ===========================================================================
// Instance methods
// ===========================================================================

int m_FindFirstChild(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    bool recursive = lua_toboolean(L, 3);
    if (name == "Humanoid" && hasHumanoid(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    SceneNode* c = n->findChild(name, recursive);
    LuaApi::pushInstance(L, c ? c->id : 0);
    return 1;
}

int m_FindFirstChildOfClass(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string cls = luaL_checkstring(L, 2);
    if (cls == "Humanoid" && hasHumanoid(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    for (auto& c : n->children)
        if (!c->internal && isA(L, c.get(), cls)) { LuaApi::pushInstance(L, c->id); return 1; }
    lua_pushnil(L);
    return 1;
}

int m_WaitForChild(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    if (name == "Humanoid" && hasHumanoid(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    SceneNode* c = n->findChild(name);
    if (!c) return luaL_error(L, "'%s' has no child called '%s'", n->name.c_str(), name.c_str());
    LuaApi::pushInstance(L, c->id);
    return 1;
}

int m_GetChildren(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    int i = 1;
    for (auto& c : n->children) {
        if (c->internal) continue;
        LuaApi::pushInstance(L, c->id);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

void collectDescendants(lua_State* L, SceneNode* n, int& i) {
    for (auto& c : n->children) {
        if (c->internal) continue;
        LuaApi::pushInstance(L, c->id);
        lua_rawseti(L, -2, i++);
        collectDescendants(L, c.get(), i);
    }
}
int m_GetDescendants(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    int i = 1;
    collectDescendants(L, n, i);
    return 1;
}

int m_Destroy(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string err;
    if (!E(L)->destroy(n, err)) return luaL_error(L, "%s", err.c_str());
    return 0;
}

int m_ClearAllChildren(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::vector<SceneNode*> kids;
    for (auto& c : n->children) if (!c->internal) kids.push_back(c.get());
    std::string err;
    for (auto* k : kids) E(L)->destroy(k, err);
    return 0;
}

int m_Clone(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (isCharacterRoot(L, n) || n == E(L)->scene()->root())
        return luaL_error(L, "You can't Clone %s", n->name.c_str());
    SceneNode* copy = E(L)->adopt(Serializer::clone(*n));
    LuaApi::pushInstance(L, copy->id);
    return 1;
}

// part:ApplyImpulse(Vector3) / part:ApplyAngularImpulse(Vector3)
float partMass(const SceneNode* n) {
    glm::vec3 s(glm::length(glm::vec3(n->worldMatrix()[0])), glm::length(glm::vec3(n->worldMatrix()[1])),
                glm::length(glm::vec3(n->worldMatrix()[2])));
    float d = Physics::densityOf(n);
    return std::max(0.001f, d * s.x * s.y * s.z);
}
int m_ApplyImpulse(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isPart()) return 0;
    n->velocity += LuaApi::checkVector3(L, 2) / partMass(n);
    n->sleepTime = 0;
    return 0;
}
int m_ApplyAngularImpulse(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isPart()) return 0;
    n->angularVelocity += LuaApi::checkVector3(L, 2) / partMass(n);
    n->sleepTime = 0;
    return 0;
}

// sound:Play() / sound:Stop()
void startSound(lua_State* L, SceneNode* n) {
    Audio::stop(n->audioHandle);
    bool in3d = n->parent && n->parent->isPart();
    glm::vec3 at = in3d ? glm::vec3(n->parent->worldMatrix()[3]) : glm::vec3(0.0f);
    Scene& scene = *E(L)->scene();
    const float volume = SoundMix::volume(scene, n);   // (times its SoundGroup's Volume)
    n->audioHandle = Audio::play(n->soundId, volume, n->pitch, n->looped, in3d ? &at : nullptr);
    SoundMix::apply(scene, n, n->audioHandle);          // its effects, and its group's
    if (!n->looped) scene.pushFx(FxEvent::Sound, at, volume, n->soundId, n->id, in3d);   // multiplayer
}
int m_Play(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isSound()) return luaL_error(L, "Play only works on Sound objects");
    startSound(L, n);
    return 0;
}
int m_Stop(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (n->isSound()) { Audio::stop(n->audioHandle); n->audioHandle = 0; }
    return 0;
}

// character:BreakJoints() — kill the character violently.
int m_BreakJoints(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (hasHumanoid(L, n)) E(L)->scene()->killCharacter(n->id, 1.0f, glm::vec3(0, 4, 0));
    return 0;
}

int m_IsA(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_pushboolean(L, isA(L, n, luaL_checkstring(L, 2)));
    return 1;
}

int m_IsDescendantOf(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    SceneNode* other = LuaApi::checkNode(L, 2);
    lua_pushboolean(L, other->isAncestorOf(n));
    return 1;
}

int m_GetFullName(lua_State* L) {
    lua_pushstring(L, LuaApi::checkNode(L, 1)->fullName().c_str());
    return 1;
}

// Model.PrimaryPart: the part set as it (if it's still inside the model). A character
// without one goes by its HumanoidRootPart, like Roblox's.
SceneNode* primaryOf(lua_State* L, const SceneNode* n) {
    if (n->kind != NodeKind::Model) return nullptr;
    if (n->primaryPart)
        if (SceneNode* p = E(L)->resolve(n->primaryPart); p && p->isPart() && n->isAncestorOf(p)) return p;
    if (hasHumanoid(L, n))
        if (SceneNode* hrp = n->findChild("HumanoidRootPart"); hrp && hrp->isPart()) return hrp;
    return nullptr;
}
// Where something really is and which way it faces (no size), in world space.
glm::mat4 worldCFrame(const SceneNode* n) {
    glm::mat4 m = n->worldMatrix();
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(glm::vec3(m[i]));
        m[i] = len > 1e-8f ? m[i] / len : glm::vec4(i == 0, i == 1, i == 2, 0);
    }
    return m;
}
// A model's pivot is its PrimaryPart's CFrame when it has one (Roblox's rule), else its own.
glm::mat4 pivotOf(lua_State* L, const SceneNode* n) {
    if (SceneNode* pp = primaryOf(L, n)) return worldCFrame(pp);
    return getCFrame(n);
}
void pivotTo(lua_State* L, SceneNode* n, const glm::mat4& cf) {
    if (SceneNode* pp = primaryOf(L, n)) {   // move the model so its PrimaryPart lands on cf
        const glm::mat4 offset = glm::inverse(worldCFrame(n)) * worldCFrame(pp);
        setCFrame(L, n, cf * glm::inverse(offset));
        return;
    }
    setCFrame(L, n, cf);
}
int m_GetPivot(lua_State* L) {
    LuaApi::pushCFrame(L, pivotOf(L, LuaApi::checkNode(L, 1)));
    return 1;
}
int m_PivotTo(lua_State* L) {
    pivotTo(L, LuaApi::checkNode(L, 1), LuaApi::checkCFrame(L, 2));
    return 0;
}
// model:SetPrimaryPartCFrame(cf) / model:GetPrimaryPartCFrame() (the older names).
int m_SetPrimaryPartCFrame(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!primaryOf(L, n)) return luaL_error(L, "%s has no PrimaryPart (set model.PrimaryPart first)", n->name.c_str());
    pivotTo(L, n, LuaApi::checkCFrame(L, 2));
    return 0;
}
int m_GetPrimaryPartCFrame(lua_State* L) {
    SceneNode* pp = primaryOf(L, LuaApi::checkNode(L, 1));
    if (!pp) { lua_pushnil(L); return 1; }
    LuaApi::pushCFrame(L, worldCFrame(pp));
    return 1;
}

// --- Attributes: obj:SetAttribute("Coins", 5), obj:GetAttribute("Coins") ---

void pushAttribute(lua_State* L, const Attribute& a) {
    switch (a.type) {
        case Attribute::Bool:    lua_pushboolean(L, a.b); break;
        case Attribute::Number:   // whole numbers come back as integers (5, not 5.0)
            if (a.n == std::floor(a.n) && std::fabs(a.n) < 9e15) lua_pushinteger(L, (lua_Integer)a.n);
            else lua_pushnumber(L, a.n);
            break;
        case Attribute::String:  lua_pushstring(L, a.s.c_str()); break;
        case Attribute::Vector3: LuaApi::pushVector3(L, a.v); break;
        case Attribute::Color3:  LuaApi::pushColor3(L, a.v); break;
    }
}

int m_GetAttribute(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    const Attribute* a = n->findAttribute(luaL_checkstring(L, 2));
    if (a) pushAttribute(L, *a); else lua_pushnil(L);
    return 1;
}

int m_GetAttributes(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    for (const auto& a : n->attributes) { pushAttribute(L, a); lua_setfield(L, -2, a.name.c_str()); }
    return 1;
}

int m_SetAttribute(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    if (name.empty() || name.size() > 100 || name.rfind("RBX", 0) == 0)
        return luaL_error(L, "'%s' can't be used as an attribute name", name.c_str());
    for (char c : name)
        if (!std::isalnum((unsigned char)c) && c != '_')
            return luaL_error(L, "Attribute names can only use letters, numbers and _ ('%s')", name.c_str());

    auto it = std::find_if(n->attributes.begin(), n->attributes.end(), [&](const Attribute& a) { return a.name == name; });
    if (lua_isnoneornil(L, 3)) {                 // SetAttribute(name, nil) removes it
        if (it == n->attributes.end()) return 0;
        n->attributes.erase(it);
        E(L)->fireAttributeChanged(n->id, name);
        refreshRobloxMeshProperty(L, n, name);
        return 0;
    }
    Attribute v;
    v.name = name;
    if (lua_type(L, 3) == LUA_TBOOLEAN)      { v.type = Attribute::Bool;   v.b = lua_toboolean(L, 3); }
    else if (lua_type(L, 3) == LUA_TNUMBER)  { v.type = Attribute::Number; v.n = lua_tonumber(L, 3); }
    else if (lua_type(L, 3) == LUA_TSTRING)  { v.type = Attribute::String; v.s = lua_tostring(L, 3); }
    else if (glm::vec3* p = LuaApi::toVector3(L, 3)) { v.type = Attribute::Vector3; v.v = *p; }
    else if (glm::vec3* c = LuaApi::toColor3(L, 3))  { v.type = Attribute::Color3;  v.v = *c; }
    else return luaL_error(L, "Attributes can hold true/false, numbers, text, Vector3 or Color3 (got %s)",
                           luaL_typename(L, 3));
    if (it != n->attributes.end()) {
        const Attribute& o = *it;
        bool same = o.type == v.type && o.b == v.b && o.n == v.n && o.s == v.s && o.v == v.v;
        *it = v;
        if (same) return 0;
    } else {
        n->attributes.push_back(v);
    }
    E(L)->fireAttributeChanged(n->id, name);
    refreshRobloxMeshProperty(L, n, name);
    return 0;
}

int m_GetAttributeChangedSignal(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    const char* name = luaL_checkstring(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.attrSignal");
    LuaApi::pushSignal(L, SignalKind::AttributeChanged, n->id);
    lua_pushstring(L, name);
    lua_call(L, 2, 1);
    return 1;
}

// obj:GetPropertyChangedSignal("TextureId"): a property kept as an attribute has its own
// signal; anything else gets the object's Changed.
int m_GetPropertyChangedSignal(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    const std::string name = luaL_checkstring(L, 2);
    for (const Attribute& a : n->attributes)
        if (a.name == name) return m_GetAttributeChangedSignal(L);
    LuaApi::pushSignal(L, SignalKind::Changed, n->id);
    return 1;
}

// --- Tags: obj:AddTag("Enemy"), obj:HasTag("Enemy") ---

int m_HasTag(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string tag = luaL_checkstring(L, 2);
    lua_pushboolean(L, std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end());
    return 1;
}
int m_AddTag(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string tag = luaL_checkstring(L, 2);
    if (std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end()) return 0;
    n->tags.push_back(tag);
    E(L)->fireTag(true, n->id, tag);
    return 0;
}
int m_RemoveTag(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string tag = luaL_checkstring(L, 2);
    auto it = std::find(n->tags.begin(), n->tags.end(), tag);
    if (it == n->tags.end()) return 0;
    n->tags.erase(it);
    E(L)->fireTag(false, n->id, tag);
    return 0;
}
int m_GetTags(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    for (size_t i = 0; i < n->tags.size(); ++i) { lua_pushstring(L, n->tags[i].c_str()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}

int m_LoadAnimation(lua_State* L);   // (with the animation code below)
int m_SetCoreGuiEnabled(lua_State* L);   // (with the Humanoid code below)
int m_GetCoreGuiEnabled(lua_State* L);
bool parsePriority(const char* s, Anim::Priority& out);

// river:ParentTo(workspace): the same as river.Parent = workspace.
int m_ParentTo(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string err;
    if (!E(L)->setParent(n, LuaApi::checkNode(L, 2), err)) return luaL_error(L, "%s", err.c_str());
    return 0;
}

float massOf(const SceneNode* n);   // (below)
int m_GetMass(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_pushnumber(L, n->isPart() ? massOf(n) : 0.0);
    return 1;
}

// --- workspace.Terrain ---------------------------------------------------------
// Hills you sculpt in Studio, or build from a script. Materials are names
// ("Grass", Enum.Material.Rock...); "Air" digs instead of filling.
Terrain& terrainOf(lua_State* L) {
    LuaApi::checkNode(L, 1);   // (called as workspace.Terrain:Something)
    return E(L)->scene()->terrain();
}
// The material argument at `idx`: false means "Air" (dig out).
bool terrainMaterialArg(lua_State* L, int idx, TerrainMaterial& m) {
    m = TerrainMaterial::Grass;
    if (lua_isnoneornil(L, idx)) return true;
    const std::string name = luaL_checkstring(L, idx);
    if (name == "Air") return false;
    if (name == "Water") luaL_error(L, "Terrain can't be water here: use a part called Water (it gets waves and swimming)");
    if (!Terrain::parseMaterial(name, m))
        luaL_error(L, "Unknown terrain material '%s' (try Grass, Dirt, Sand, Rock, Snow, Mud or Air)", name.c_str());
    return true;
}
// No terrain yet? Start a flat one under the first thing a script fills.
void terrainReady(Terrain& t, float bottom, TerrainMaterial m) {
    if (t.empty()) t.create(128, Terrain::kDefaultCell, bottom, m);
}
int t_FillBlock(lua_State* L) {
    Terrain& t = terrainOf(L);
    const glm::mat4 cf = LuaApi::checkCFrame(L, 2);
    const glm::vec3 size = glm::abs(LuaApi::checkVector3(L, 3));
    TerrainMaterial m;
    const bool fill = terrainMaterialArg(L, 4, m);
    // (a turned block fills the box around it)
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
        glm::vec3 w = glm::vec3(cf * glm::vec4(c * size, 1.0f));
        mn = glm::min(mn, w);
        mx = glm::max(mx, w);
    }
    if (fill) { terrainReady(t, mn.y, m); t.fillBox(mn, mx, m); }
    else t.digBox(mn, mx);
    return 0;
}
int t_FillBall(lua_State* L) {
    Terrain& t = terrainOf(L);
    const glm::vec3 c = LuaApi::checkVector3(L, 2);
    const float r = (float)luaL_checknumber(L, 3);
    TerrainMaterial m;
    if (terrainMaterialArg(L, 4, m)) { terrainReady(t, c.y - r, m); t.fillBall(c, r, m); }
    else t.digBall(c, r);
    return 0;
}
int t_Clear(lua_State* L) { terrainOf(L).clear(); return 0; }
// Terrain:Generate(seed, hills, size): rolling hills. hills 0..1, size in studs across.
int t_Generate(lua_State* L) {
    Terrain& t = terrainOf(L);
    const uint32_t seed = (uint32_t)luaL_optinteger(L, 2, 1);
    const float hills = (float)luaL_optnumber(L, 3, 0.5);
    const float size = (float)luaL_optnumber(L, 4, 512.0);
    t.generate((int)(size / Terrain::kDefaultCell), Terrain::kDefaultCell, seed, hills);
    return 0;
}
// Terrain:GetHeight(x, z) or (position): the ground height there, or nil off the edge.
int t_GetHeight(lua_State* L) {
    Terrain& t = terrainOf(L);
    float x, z;
    if (lua_isnumber(L, 2)) { x = (float)lua_tonumber(L, 2); z = (float)luaL_checknumber(L, 3); }
    else { glm::vec3 p = LuaApi::checkVector3(L, 2); x = p.x; z = p.z; }
    float h;
    if (!t.heightAt(x, z, h)) return 0;
    lua_pushnumber(L, h);
    return 1;
}
int t_GetMaterial(lua_State* L) {
    Terrain& t = terrainOf(L);
    const glm::vec3 p = LuaApi::checkVector3(L, 2);
    float h;
    if (!t.heightAt(p.x, p.z, h)) { lua_pushstring(L, "Air"); return 1; }
    lua_pushstring(L, kTerrainMaterialNames[(int)t.materialAt(p.x, p.z)]);
    return 1;
}
// Terrain:Sculpt(brush, position, radius, strength, material): one dab of a Studio
// brush. brush = "Raise", "Lower", "Smooth", "Flatten" or "Paint".
int t_Sculpt(lua_State* L) {
    Terrain& t = terrainOf(L);
    const std::string b = luaL_checkstring(L, 2);
    const glm::vec3 p = LuaApi::checkVector3(L, 3);
    const float r = (float)luaL_checknumber(L, 4);
    const float strength = (float)luaL_optnumber(L, 5, 1.0);
    TerrainMaterial m;
    terrainMaterialArg(L, 6, m);
    Terrain::Brush brush;
    if (b == "Raise") brush = Terrain::Brush::Raise;
    else if (b == "Lower") brush = Terrain::Brush::Lower;
    else if (b == "Smooth") brush = Terrain::Brush::Smooth;
    else if (b == "Flatten") brush = Terrain::Brush::Flatten;
    else if (b == "Paint") brush = Terrain::Brush::Paint;
    else return luaL_error(L, "Unknown brush '%s' (try Raise, Lower, Smooth, Flatten or Paint)", b.c_str());
    t.brush(brush, p, r, strength, m);
    return 0;
}
const luaL_Reg kTerrainMethods[] = {
    {"FillBlock", t_FillBlock}, {"FillBall", t_FillBall}, {"Clear", t_Clear}, {"Generate", t_Generate},
    {"GetHeight", t_GetHeight}, {"GetMaterial", t_GetMaterial}, {"Sculpt", t_Sculpt},
    {nullptr, nullptr}};

const luaL_Reg kMethods[] = {
    {"FindFirstChild", m_FindFirstChild}, {"FindFirstChildOfClass", m_FindFirstChildOfClass},
    {"WaitForChild", m_WaitForChild}, {"GetChildren", m_GetChildren},
    {"GetDescendants", m_GetDescendants}, {"Destroy", m_Destroy}, {"Remove", m_Destroy}, {"ParentTo", m_ParentTo},
    {"ClearAllChildren", m_ClearAllChildren}, {"Clone", m_Clone}, {"IsA", m_IsA},
    {"IsDescendantOf", m_IsDescendantOf}, {"GetFullName", m_GetFullName},
    {"GetPivot", m_GetPivot}, {"PivotTo", m_PivotTo}, {"BreakJoints", m_BreakJoints},
    {"SetPrimaryPartCFrame", m_SetPrimaryPartCFrame}, {"GetPrimaryPartCFrame", m_GetPrimaryPartCFrame},
    {"Play", m_Play}, {"Stop", m_Stop},
    {"ApplyImpulse", m_ApplyImpulse}, {"ApplyAngularImpulse", m_ApplyAngularImpulse},
    {"GetAttribute", m_GetAttribute}, {"SetAttribute", m_SetAttribute}, {"GetAttributes", m_GetAttributes},
    {"GetAttributeChangedSignal", m_GetAttributeChangedSignal},
    {"HasTag", m_HasTag}, {"AddTag", m_AddTag}, {"RemoveTag", m_RemoveTag}, {"GetTags", m_GetTags},
    {"LoadAnimation", m_LoadAnimation}, {"GetMass", m_GetMass},
    {"SetCoreGuiEnabled", m_SetCoreGuiEnabled}, {"GetCoreGuiEnabled", m_GetCoreGuiEnabled},
    {nullptr, nullptr}};

// ===========================================================================
// Instance properties
// ===========================================================================

// --- Game UI properties (ScreenGui, Frame, TextLabel, TextButton, ImageLabel...) ---

const char* alignName(int a, bool x) { return a == 0 ? (x ? "Left" : "Top") : a == 2 ? (x ? "Right" : "Bottom") : "Center"; }
int parseAlign(const char* s) {
    if (is(s, "Left") || is(s, "Top")) return 0;
    if (is(s, "Right") || is(s, "Bottom")) return 2;
    return 1;
}
// UICorner's own corners: TopLeft, TopRight, BottomRight, BottomLeft (-1 if `k` isn't one).
int cornerIndex(const char* k) {
    if (is(k, "TopLeft")) return 0;
    if (is(k, "TopRight")) return 1;
    if (is(k, "BottomRight")) return 2;
    if (is(k, "BottomLeft")) return 3;
    return -1;
}
bool hasText(const SceneNode* n) { return guiHasText(n->gui.type); }

// TextBox methods.
int box_CaptureFocus(lua_State* L) { GameGui::focus(LuaApi::checkNode(L, 1)->id); return 0; }
int box_ReleaseFocus(lua_State* L) {
    if (GameGui::focused() == LuaApi::checkNode(L, 1)->id) GameGui::focus(0, lua_toboolean(L, 2));
    return 0;
}
int box_IsFocused(lua_State* L) { lua_pushboolean(L, GameGui::focused() == LuaApi::checkNode(L, 1)->id); return 1; }
int gui_ApplyLayout(lua_State* L) { (void)L; GameGui::refresh(*E(L)->scene()); return 0; }

// Layout enums: FillDirection, HorizontalAlignment, VerticalAlignment, ScrollingDirection, AutomaticSize.
const char* hAlignName(int a) { return a == 1 ? "Center" : a == 2 ? "Right" : "Left"; }
const char* vAlignName(int a) { return a == 1 ? "Center" : a == 2 ? "Bottom" : "Top"; }
const char* axesName(int a) { return a == 1 ? "X" : a == 2 ? "Y" : a == 3 ? "XY" : "None"; }
int parseAxes(const char* s) { return is(s, "X") ? 1 : is(s, "Y") ? 2 : is(s, "XY") ? 3 : 0; }
void pushUDimOf(lua_State* L, const UDim2& u, bool y) { LuaApi::pushUDim(L, y ? u.ys : u.xs, y ? u.yo : u.xo); }
bool hasImage(const SceneNode* n) { return n->gui.type == GuiType::ImageLabel || n->gui.type == GuiType::ImageButton; }

// ---- Movers: BodyVelocity, BodyGyro... and LinearVelocity, AlignPosition... -------
// (Old scripts often wrote these in lower case: bv.velocity, bg.cframe, bf.force.)
bool ieq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    return *a == *b;
}
float finite(float v) { return std::clamp(v, -1e30f, 1e30f); }   // (math.huge -> very big; saves can't hold infinity)
glm::vec3 finite(glm::vec3 v) { for (int i = 0; i < 3; ++i) v[i] = finite(v[i]); return v; }

bool moverIndex(lua_State* L, SceneNode* n, const char* k) {
    const MoverProps& m = n->mover;
    const MoverType t = m.type;
    const bool classic = isBodyMover(t);
    if (ieq(k, "Enabled")) { lua_pushboolean(L, n->enabled); return true; }
    if (!classic && ieq(k, "Attachment0")) { LuaApi::pushInstance(L, n->ref0); return true; }
    if (!classic && ieq(k, "Attachment1")) { LuaApi::pushInstance(L, n->ref1); return true; }
    if (!classic && ieq(k, "Active")) { lua_pushboolean(L, n->enabled); return true; }
    auto v3 = [&](const glm::vec3& v) { LuaApi::pushVector3(L, v); return true; };
    auto num = [&](float v) { lua_pushnumber(L, v); return true; };
    switch (t) {
        case MoverType::BodyVelocity:
            if (ieq(k, "Velocity")) return v3(m.value);
            if (ieq(k, "MaxForce")) return v3(m.maxAxes);
            if (ieq(k, "P")) return num(m.p);
            break;
        case MoverType::BodyPosition:
            if (ieq(k, "Position")) return v3(m.value);
            if (ieq(k, "MaxForce")) return v3(m.maxAxes);
            if (ieq(k, "P")) return num(m.p);
            if (ieq(k, "D")) return num(m.d);
            break;
        case MoverType::BodyGyro:
            if (ieq(k, "CFrame")) { LuaApi::pushCFrame(L, rotationMatrix(m.rotation)); return true; }
            if (ieq(k, "MaxTorque")) return v3(m.maxAxes);
            if (ieq(k, "P")) return num(m.p);
            if (ieq(k, "D")) return num(m.d);
            break;
        case MoverType::BodyAngularVelocity:
            if (ieq(k, "AngularVelocity")) return v3(m.value);
            if (ieq(k, "MaxTorque")) return v3(m.maxAxes);
            if (ieq(k, "P")) return num(m.p);
            break;
        case MoverType::BodyThrust:
            if (ieq(k, "Force")) return v3(m.value);
            if (ieq(k, "Location")) return v3(m.location);
            break;
        case MoverType::BodyForce:
            if (ieq(k, "Force")) return v3(m.value);
            break;
        case MoverType::LinearVelocity:
            if (ieq(k, "VectorVelocity")) return v3(m.value);
            if (ieq(k, "MaxForce")) return num(m.maxForce);
            if (ieq(k, "VelocityConstraintMode")) { lua_pushstring(L, "Vector"); return true; }
            break;
        case MoverType::AlignPosition:
            if (ieq(k, "Position")) return v3(m.value);
            if (ieq(k, "MaxForce")) return num(m.maxForce);
            if (ieq(k, "MaxVelocity")) return num(m.maxVelocity > 0 ? m.maxVelocity : 1e30f);
            if (ieq(k, "Responsiveness")) return num(m.responsiveness);
            if (ieq(k, "RigidityEnabled")) { lua_pushboolean(L, m.rigid); return true; }
            if (ieq(k, "Mode")) { lua_pushstring(L, n->ref1 ? "TwoAttachment" : "OneAttachment"); return true; }
            break;
        case MoverType::AlignOrientation:
            if (ieq(k, "CFrame")) { LuaApi::pushCFrame(L, rotationMatrix(m.rotation)); return true; }
            if (ieq(k, "MaxTorque")) return num(m.maxForce);
            if (ieq(k, "MaxAngularVelocity")) return num(m.maxVelocity > 0 ? m.maxVelocity : 1e30f);
            if (ieq(k, "Responsiveness")) return num(m.responsiveness);
            if (ieq(k, "RigidityEnabled")) { lua_pushboolean(L, m.rigid); return true; }
            if (ieq(k, "Mode")) { lua_pushstring(L, n->ref1 ? "TwoAttachment" : "OneAttachment"); return true; }
            break;
        case MoverType::AngularVelocity:
            if (ieq(k, "AngularVelocity")) return v3(m.value);
            if (ieq(k, "MaxTorque")) return num(m.maxForce);
            break;
        case MoverType::VectorForce:
            if (ieq(k, "Force")) return v3(m.value);
            if (ieq(k, "ApplyAtCenterOfMass")) { lua_pushboolean(L, m.atCenterOfMass); return true; }
            break;
        case MoverType::Torque:
            if (ieq(k, "Torque")) return v3(m.value);
            break;
    }
    if (!classic && ieq(k, "RelativeTo")) { lua_pushstring(L, m.relativeToAttachment ? "Attachment0" : "World"); return true; }
    return false;
}

bool moverNewIndex(lua_State* L, SceneNode* n, const char* k) {
    MoverProps& m = n->mover;
    const MoverType t = m.type;
    const bool classic = isBodyMover(t);
    auto v3 = [&]() { return finite(LuaApi::checkVector3(L, 3)); };
    auto num = [&]() { return finite((float)luaL_checknumber(L, 3)); };
    auto rot = [&]() {   // the turn part of a CFrame
        glm::mat3 r(LuaApi::checkCFrame(L, 3));
        for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
        float z, y, x;
        glm::extractEulerAngleZYX(glm::mat4(r), z, y, x);
        return glm::degrees(glm::vec3(x, y, z));
    };
    if (ieq(k, "Enabled") || (!classic && ieq(k, "Active"))) { n->enabled = lua_toboolean(L, 3); return true; }
    if (!classic && (ieq(k, "Attachment0") || ieq(k, "Attachment1"))) {
        SceneNode* a = lua_isnil(L, 3) ? nullptr : LuaApi::checkNode(L, 3);
        (ieq(k, "Attachment0") ? n->ref0 : n->ref1) = a ? a->id : 0;
        return true;
    }
    if (!classic && ieq(k, "RelativeTo")) { m.relativeToAttachment = std::string(luaL_checkstring(L, 3)) != "World"; return true; }
    switch (t) {
        case MoverType::BodyVelocity:
            if (ieq(k, "Velocity")) { m.value = v3(); return true; }
            if (ieq(k, "MaxForce")) { m.maxAxes = glm::max(v3(), glm::vec3(0.0f)); return true; }
            if (ieq(k, "P")) { m.p = std::max(0.0f, num()); return true; }
            break;
        case MoverType::BodyPosition:
            if (ieq(k, "Position")) { m.value = v3(); return true; }
            if (ieq(k, "MaxForce")) { m.maxAxes = glm::max(v3(), glm::vec3(0.0f)); return true; }
            if (ieq(k, "P")) { m.p = std::max(0.0f, num()); return true; }
            if (ieq(k, "D")) { m.d = std::max(0.0f, num()); return true; }
            break;
        case MoverType::BodyGyro:
            if (ieq(k, "CFrame")) { m.rotation = rot(); return true; }
            if (ieq(k, "MaxTorque")) { m.maxAxes = glm::max(v3(), glm::vec3(0.0f)); return true; }
            if (ieq(k, "P")) { m.p = std::max(0.0f, num()); return true; }
            if (ieq(k, "D")) { m.d = std::max(0.0f, num()); return true; }
            break;
        case MoverType::BodyAngularVelocity:
            if (ieq(k, "AngularVelocity")) { m.value = v3(); return true; }
            if (ieq(k, "MaxTorque")) { m.maxAxes = glm::max(v3(), glm::vec3(0.0f)); return true; }
            if (ieq(k, "P")) { m.p = std::max(0.0f, num()); return true; }
            break;
        case MoverType::BodyThrust:
            if (ieq(k, "Force")) { m.value = v3(); return true; }
            if (ieq(k, "Location")) { m.location = v3(); return true; }
            break;
        case MoverType::BodyForce:
            if (ieq(k, "Force")) { m.value = v3(); return true; }
            break;
        case MoverType::LinearVelocity:
            if (ieq(k, "VectorVelocity")) { m.value = v3(); return true; }
            if (ieq(k, "MaxForce")) { m.maxForce = std::max(0.0f, num()); return true; }
            if (ieq(k, "VelocityConstraintMode") || ieq(k, "ForceLimitsEnabled") || ieq(k, "ForceLimitMode")) return true;   // (accepted)
            break;
        case MoverType::AlignPosition:
        case MoverType::AlignOrientation: {
            const bool pos = t == MoverType::AlignPosition;
            if (pos && ieq(k, "Position")) { m.value = v3(); return true; }
            if (!pos && ieq(k, "CFrame")) { m.rotation = rot(); return true; }
            if (ieq(k, pos ? "MaxForce" : "MaxTorque")) { m.maxForce = std::max(0.0f, num()); return true; }
            if (ieq(k, pos ? "MaxVelocity" : "MaxAngularVelocity")) { const float v = num(); m.maxVelocity = v >= 1e29f ? 0.0f : std::max(0.0f, v); return true; }
            if (ieq(k, "Responsiveness")) { m.responsiveness = std::clamp(num(), 0.0f, 200.0f); return true; }
            if (ieq(k, "RigidityEnabled")) { m.rigid = lua_toboolean(L, 3); return true; }
            if (ieq(k, "Mode")) {
                if (std::string(luaL_checkstring(L, 3)) == "OneAttachment") n->ref1 = 0;   // (then Position / CFrame is the target)
                return true;
            }
            if (ieq(k, "ReactionForceEnabled") || ieq(k, "ReactionTorqueEnabled") || ieq(k, "ApplyAtCenterOfMass") ||
                ieq(k, "PrimaryAxisOnly") || ieq(k, "AlignType")) return true;   // (accepted)
            break;
        }
        case MoverType::AngularVelocity:
            if (ieq(k, "AngularVelocity")) { m.value = v3(); return true; }
            if (ieq(k, "MaxTorque")) { m.maxForce = std::max(0.0f, num()); return true; }
            if (ieq(k, "ReactionTorqueEnabled")) return true;
            break;
        case MoverType::VectorForce:
            if (ieq(k, "Force")) { m.value = v3(); return true; }
            if (ieq(k, "ApplyAtCenterOfMass")) { m.atCenterOfMass = lua_toboolean(L, 3); return true; }
            break;
        case MoverType::Torque:
            if (ieq(k, "Torque")) { m.value = v3(); return true; }
            break;
    }
    return false;
}

// ---- BallSocketConstraint, PrismaticConstraint, CylindricalConstraint --------------
// (Attachment0 / Attachment1, Enabled, Visible and Color are shared with the other
// constraints, further down.)
bool jointIndex(lua_State* L, SceneNode* n, const char* k) {
    const JointProps& p = n->joint;
    const ConstraintType t = n->constraintType;
    auto num = [&](float v) { lua_pushnumber(L, v); return true; };
    auto flag = [&](bool v) { lua_pushboolean(L, v); return true; };
    auto act = [&](Actuator a) { lua_pushstring(L, kActuatorNames[(int)a]); return true; };
    if (is(k, "LimitsEnabled")) return flag(p.limitsEnabled);
    if (is(k, "Restitution"))   return num(p.restitution);
    if (t == ConstraintType::BallSocket) {
        if (is(k, "UpperAngle"))         return num(p.upperAngle);
        if (is(k, "TwistLimitsEnabled")) return flag(p.twistLimitsEnabled);
        if (is(k, "TwistLowerAngle"))    return num(p.twistLowerAngle);
        if (is(k, "TwistUpperAngle"))    return num(p.twistUpperAngle);
        return false;
    }
    if (is(k, "LowerLimit"))      return num(p.lowerLimit);
    if (is(k, "UpperLimit"))      return num(p.upperLimit);
    if (is(k, "ActuatorType"))    return act(p.actuator);
    if (is(k, "MotorMaxForce"))   return num(p.motorMaxForce);
    if (is(k, "Velocity"))        return num(p.velocity);
    if (is(k, "Speed"))           return num(p.speed);
    if (is(k, "ServoMaxForce"))   return num(p.servoMaxForce);
    if (is(k, "TargetPosition"))  return num(p.targetPosition);
    if (is(k, "CurrentPosition")) return num(p.currentPosition);
    if (is(k, "MotorMaxAcceleration")) return num((float)HUGE_VAL);   // (no limit here)
    if (t != ConstraintType::Cylindrical) return false;
    if (is(k, "AngularActuatorType"))  return act(p.angularActuator);
    if (is(k, "AngularVelocity"))      return num(p.angularVelocity);
    if (is(k, "MotorMaxTorque"))       return num(p.motorMaxTorque);
    if (is(k, "AngularSpeed"))         return num(p.angularSpeed);
    if (is(k, "TargetAngle"))          return num(p.targetAngle);
    if (is(k, "ServoMaxTorque"))       return num(p.servoMaxTorque);
    if (is(k, "InclinationAngle"))     return num(p.inclinationAngle);
    if (is(k, "AngularLimitsEnabled")) return flag(p.angularLimitsEnabled);
    if (is(k, "LowerAngle"))           return num(p.lowerAngle);
    if (is(k, "UpperAngle"))           return num(p.upperAngle);
    if (is(k, "AngularRestitution"))   return num(p.restitution);
    if (is(k, "CurrentAngle"))         return num(p.currentAngle);
    if (is(k, "MotorMaxAngularAcceleration")) return num((float)HUGE_VAL);
    return false;
}

// Enum.ActuatorType.Servo, "Servo" or 2.
Actuator checkActuator(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TNUMBER) {
        const int v = (int)lua_tointeger(L, idx);
        if (v >= 0 && v <= 2) return (Actuator)v;
    } else {
        std::string s = luaL_checkstring(L, idx);
        if (const size_t dot = s.rfind('.'); dot != std::string::npos) s = s.substr(dot + 1);
        for (int i = 0; i < 3; ++i) if (s == kActuatorNames[i]) return (Actuator)i;
    }
    luaL_error(L, "ActuatorType must be None, Motor or Servo");
    return Actuator::None;
}

bool jointNewIndex(lua_State* L, SceneNode* n, const char* k) {
    JointProps& p = n->joint;
    const ConstraintType t = n->constraintType;
    auto num = [&](float& out, float lo = -1e30f, float hi = 1e30f) {
        out = std::clamp((float)luaL_checknumber(L, 3), lo, hi);
        return true;
    };
    auto flag = [&](bool& out) { out = lua_toboolean(L, 3); return true; };
    if (is(k, "LimitsEnabled")) return flag(p.limitsEnabled);
    if (is(k, "Restitution"))   return num(p.restitution, 0.0f, 1.0f);
    if (t == ConstraintType::BallSocket) {
        if (is(k, "UpperAngle"))         return num(p.upperAngle, 0.0f, 180.0f);
        if (is(k, "TwistLimitsEnabled")) return flag(p.twistLimitsEnabled);
        if (is(k, "TwistLowerAngle"))    return num(p.twistLowerAngle, -180.0f, 180.0f);
        if (is(k, "TwistUpperAngle"))    return num(p.twistUpperAngle, -180.0f, 180.0f);
        return false;
    }
    if (is(k, "LowerLimit"))      return num(p.lowerLimit);
    if (is(k, "UpperLimit"))      return num(p.upperLimit);
    if (is(k, "ActuatorType"))    { p.actuator = checkActuator(L, 3); return true; }
    if (is(k, "MotorMaxForce"))   return num(p.motorMaxForce, 0.0f);
    if (is(k, "Velocity"))        return num(p.velocity);
    if (is(k, "Speed"))           return num(p.speed, 0.0f);
    if (is(k, "ServoMaxForce"))   return num(p.servoMaxForce, 0.0f);
    if (is(k, "TargetPosition"))  return num(p.targetPosition);
    if (is(k, "MotorMaxAcceleration")) return true;   // (accepted, no limit here)
    if (t != ConstraintType::Cylindrical) return false;
    if (is(k, "AngularActuatorType"))  { p.angularActuator = checkActuator(L, 3); return true; }
    if (is(k, "AngularVelocity"))      return num(p.angularVelocity);
    if (is(k, "MotorMaxTorque"))       return num(p.motorMaxTorque, 0.0f);
    if (is(k, "AngularSpeed"))         return num(p.angularSpeed, 0.0f);
    if (is(k, "TargetAngle"))          return num(p.targetAngle, -180.0f, 180.0f);
    if (is(k, "ServoMaxTorque"))       return num(p.servoMaxTorque, 0.0f);
    if (is(k, "InclinationAngle"))     return num(p.inclinationAngle, -180.0f, 180.0f);
    if (is(k, "AngularLimitsEnabled")) return flag(p.angularLimitsEnabled);
    if (is(k, "LowerAngle"))           return num(p.lowerAngle, -180.0f, 180.0f);
    if (is(k, "UpperAngle"))           return num(p.upperAngle, -180.0f, 180.0f);
    if (is(k, "AngularRestitution"))   return num(p.restitution, 0.0f, 1.0f);
    if (is(k, "MotorMaxAngularAcceleration")) return true;
    return false;
}

// part:GetMass(): how heavy it is (its Density times its size).
float massOf(const SceneNode* n) {
    return Physics::densityOf(n) * n->transform.scale.x * n->transform.scale.y * n->transform.scale.z;
}

void pushColorSequence(lua_State* L, const std::vector<ColorKey>& keys);
void pushNumberSequence(lua_State* L, const std::vector<NumberKey>& keys);
std::vector<ColorKey> checkColorSequence(lua_State* L, int idx);
std::vector<NumberKey> checkNumberSequence(lua_State* L, int idx, float lo, float hi);

// Push a UI property; false if `k` isn't one.
// --- Screen effects (ColorCorrectionEffect, SunRaysEffect...) and the Sky ---

bool postIndex(lua_State* L, SceneNode* n, const char* k) {
    const PostEffectProps& p = n->post;
    if (is(k, "Enabled")) { lua_pushboolean(L, n->enabled); return true; }
    switch (p.type) {
        case PostEffectType::ColorCorrection:
            if (is(k, "Brightness")) { lua_pushnumber(L, p.brightness); return true; }
            if (is(k, "Contrast"))   { lua_pushnumber(L, p.contrast); return true; }
            if (is(k, "Saturation")) { lua_pushnumber(L, p.saturation); return true; }
            if (is(k, "TintColor"))  { LuaApi::pushColor3(L, p.tint); return true; }
            break;
        case PostEffectType::SunRays:
            if (is(k, "Intensity")) { lua_pushnumber(L, p.intensity); return true; }
            if (is(k, "Spread"))    { lua_pushnumber(L, p.spread); return true; }
            break;
        case PostEffectType::DepthOfField:
            if (is(k, "FarIntensity"))  { lua_pushnumber(L, p.farIntensity); return true; }
            if (is(k, "FocusDistance")) { lua_pushnumber(L, p.focusDistance); return true; }
            if (is(k, "InFocusRadius")) { lua_pushnumber(L, p.inFocusRadius); return true; }
            if (is(k, "NearIntensity")) { lua_pushnumber(L, p.nearIntensity); return true; }
            break;
        case PostEffectType::Bloom:
            if (is(k, "Intensity")) { lua_pushnumber(L, p.intensity); return true; }
            if (is(k, "Size"))      { lua_pushnumber(L, p.size); return true; }
            if (is(k, "Threshold")) { lua_pushnumber(L, p.threshold); return true; }
            break;
        case PostEffectType::Blur:
            if (is(k, "Size")) { lua_pushnumber(L, p.size); return true; }
            break;
    }
    return false;
}

bool postNewIndex(lua_State* L, SceneNode* n, const char* k) {
    PostEffectProps& p = n->post;
    auto num = [&](float& f) { f = (float)luaL_checknumber(L, 3); clampPostEffect(p); return true; };
    if (is(k, "Enabled")) { n->enabled = lua_toboolean(L, 3); return true; }
    switch (p.type) {
        case PostEffectType::ColorCorrection:
            if (is(k, "Brightness")) return num(p.brightness);
            if (is(k, "Contrast"))   return num(p.contrast);
            if (is(k, "Saturation")) return num(p.saturation);
            if (is(k, "TintColor"))  { p.tint = LuaApi::checkColor3(L, 3); clampPostEffect(p); return true; }
            break;
        case PostEffectType::SunRays:
            if (is(k, "Intensity")) return num(p.intensity);
            if (is(k, "Spread"))    return num(p.spread);
            break;
        case PostEffectType::DepthOfField:
            if (is(k, "FarIntensity"))  return num(p.farIntensity);
            if (is(k, "FocusDistance")) return num(p.focusDistance);
            if (is(k, "InFocusRadius")) return num(p.inFocusRadius);
            if (is(k, "NearIntensity")) return num(p.nearIntensity);
            break;
        case PostEffectType::Bloom:
            if (is(k, "Intensity")) return num(p.intensity);
            if (is(k, "Size"))      return num(p.size);
            if (is(k, "Threshold")) return num(p.threshold);
            break;
        case PostEffectType::Blur:
            if (is(k, "Size")) return num(p.size);
            break;
    }
    return false;
}

std::string* skyPicture(SkyProps& s, const char* k) {
    if (is(k, "SkyboxBk")) return &s.bk;
    if (is(k, "SkyboxDn")) return &s.dn;
    if (is(k, "SkyboxFt")) return &s.ft;
    if (is(k, "SkyboxLf")) return &s.lf;
    if (is(k, "SkyboxRt")) return &s.rt;
    if (is(k, "SkyboxUp")) return &s.up;
    if (is(k, "SunTextureId")) return &s.sun;
    if (is(k, "MoonTextureId")) return &s.moon;
    return nullptr;
}

bool skyIndex(lua_State* L, SceneNode* n, const char* k) {
    SkyProps& s = n->sky;
    if (std::string* pic = skyPicture(s, k)) { lua_pushstring(L, pic->c_str()); return true; }
    if (is(k, "StarCount"))            { lua_pushinteger(L, s.starCount); return true; }
    if (is(k, "CelestialBodiesShown")) { lua_pushboolean(L, s.celestial); return true; }
    if (is(k, "SunAngularSize"))       { lua_pushnumber(L, s.sunSize); return true; }
    if (is(k, "MoonAngularSize"))      { lua_pushnumber(L, s.moonSize); return true; }
    if (is(k, "SkyboxOrientation"))    { LuaApi::pushVector3(L, glm::vec3(0.0f)); return true; }
    return false;
}

bool skyNewIndex(lua_State* L, SceneNode* n, const char* k) {
    SkyProps& s = n->sky;
    if (std::string* pic = skyPicture(s, k)) { *pic = luaL_checkstring(L, 3); clampSky(s); return true; }
    if (is(k, "StarCount"))            { s.starCount = (int)luaL_checknumber(L, 3); clampSky(s); return true; }
    if (is(k, "CelestialBodiesShown")) { s.celestial = lua_toboolean(L, 3); return true; }
    if (is(k, "SunAngularSize"))       { s.sunSize = (float)luaL_checknumber(L, 3); clampSky(s); return true; }
    if (is(k, "MoonAngularSize"))      { s.moonSize = (float)luaL_checknumber(L, 3); clampSky(s); return true; }
    if (is(k, "SkyboxOrientation"))    return true;   // (accepted so scripts run; the skybox doesn't turn)
    return false;
}

bool guiIndex(lua_State* L, SceneNode* n, const char* k) {
    const GuiProps& g = n->gui;
    if (is(k, "AbsoluteSize") || is(k, "AbsolutePosition")) GameGui::refresh(*E(L)->scene());
    if (g.type == GuiType::BillboardGui || g.type == GuiType::SurfaceGui) {
        const bool bb = g.type == GuiType::BillboardGui;
        if (is(k, "Enabled"))      { lua_pushboolean(L, n->enabled); return true; }
        if (is(k, "Adornee"))      { if (g.adornee) LuaApi::pushInstance(L, g.adornee); else lua_pushnil(L); return true; }
        if (is(k, "AlwaysOnTop"))  { lua_pushboolean(L, g.alwaysOnTop); return true; }
        if (is(k, "MaxDistance"))  { lua_pushnumber(L, g.maxDistance > 0 ? g.maxDistance : HUGE_VAL); return true; }
        if (is(k, "AbsoluteSize")) { GameGui::refresh(*E(L)->scene()); LuaApi::pushVector2(L, g.absSize); return true; }
        if (is(k, "AbsolutePosition")) { GameGui::refresh(*E(L)->scene()); LuaApi::pushVector2(L, g.absPos); return true; }
        if (is(k, "LightInfluence") || is(k, "Brightness")) { lua_pushnumber(L, is(k, "Brightness") ? 1.0 : 0.0); return true; }
        if (is(k, "ResetOnSpawn") || is(k, "ClipsDescendants")) { lua_pushboolean(L, is(k, "ClipsDescendants")); return true; }
        if (bb) {
            if (is(k, "Size"))                  { LuaApi::pushUDim2(L, g.size); return true; }
            if (is(k, "StudsOffset"))           { LuaApi::pushVector3(L, g.studsOffset); return true; }
            if (is(k, "StudsOffsetWorldSpace")) { LuaApi::pushVector3(L, g.worldOffset); return true; }
        } else {
            if (is(k, "Face"))          { lua_pushstring(L, kFaceNames[std::clamp(g.face, 0, 5)]); return true; }
            if (is(k, "CanvasSize"))    { LuaApi::pushVector2(L, g.surfaceCanvas); return true; }
            if (is(k, "SizingMode"))    { lua_pushstring(L, g.perStud ? "PixelsPerStud" : "FixedSize"); return true; }
            if (is(k, "PixelsPerStud")) { lua_pushnumber(L, g.pixelsPerStud); return true; }
        }
        return false;
    }
    if (g.type == GuiType::ScreenGui) {
        if (is(k, "Enabled"))      { lua_pushboolean(L, n->enabled); return true; }
        if (is(k, "DisplayOrder")) { lua_pushinteger(L, g.displayOrder); return true; }
        if (is(k, "ResetOnSpawn") || is(k, "IgnoreGuiInset")) { lua_pushboolean(L, false); return true; }
        if (is(k, "AbsoluteSize")) { LuaApi::pushVector2(L, g.absSize); return true; }
        if (is(k, "AbsolutePosition")) { LuaApi::pushVector2(L, g.absPos); return true; }
        return false;
    }
    if (g.type == GuiType::UICorner) {
        if (is(k, "CornerRadius")) { LuaApi::pushUDim(L, g.corner.xs, g.corner.xo); return true; }
        if (int c = cornerIndex(k); c >= 0) {   // one corner on its own (CornerRadius when it isn't set)
            if (g.corners[c] < 0) LuaApi::pushUDim(L, g.corner.xs, g.corner.xo);
            else LuaApi::pushUDim(L, g.cornerScales[c], g.corners[c]);
            return true;
        }
        return false;
    }
    if (g.type == GuiType::UIShadow) {
        if (is(k, "Color"))        { LuaApi::pushColor3(L, g.bg); return true; }
        if (is(k, "Transparency")) { lua_pushnumber(L, g.bgTransparency); return true; }
        if (is(k, "Offset"))       { LuaApi::pushVector2(L, g.shadowOffset); return true; }
        if (is(k, "Blur") || is(k, "Size")) { lua_pushnumber(L, g.shadowBlur); return true; }
        if (is(k, "Spread"))       { lua_pushnumber(L, g.shadowSpread); return true; }
        if (is(k, "Enabled"))      { lua_pushboolean(L, n->enabled); return true; }
        return false;
    }
    if (g.type == GuiType::UIBlur) {
        if (is(k, "Size"))         { lua_pushnumber(L, g.blurSize); return true; }
        if (is(k, "Enabled"))      { lua_pushboolean(L, n->enabled); return true; }
        return false;
    }
    if (g.type == GuiType::UIStroke) {
        if (is(k, "Color"))        { LuaApi::pushColor3(L, g.borderColor); return true; }
        if (is(k, "Thickness"))    { lua_pushnumber(L, g.thickness); return true; }
        if (is(k, "Transparency")) { lua_pushnumber(L, g.bgTransparency); return true; }
        if (is(k, "Enabled"))      { lua_pushboolean(L, n->enabled); return true; }
        return false;
    }
    if (g.type == GuiType::UIGradient) {
        if (is(k, "Color"))        { pushColorSequence(L, g.gradColor); return true; }
        if (is(k, "Transparency")) { pushNumberSequence(L, g.gradTransparency); return true; }
        if (is(k, "Rotation"))     { lua_pushnumber(L, g.gradRotation); return true; }
        if (is(k, "Offset"))       { LuaApi::pushVector2(L, g.gradOffset); return true; }
        if (is(k, "Enabled"))      { lua_pushboolean(L, n->enabled); return true; }
        return false;
    }
    if (g.type == GuiType::UIScale) {
        if (is(k, "Scale")) { lua_pushnumber(L, g.scale); return true; }
        return false;
    }
    if (g.type == GuiType::UIAspectRatioConstraint) {
        if (is(k, "AspectRatio"))  { lua_pushnumber(L, g.aspectRatio); return true; }
        if (is(k, "AspectType"))   { lua_pushstring(L, g.aspectType ? "ScaleWithParentSize" : "FitWithinMaxSize"); return true; }
        if (is(k, "DominantAxis")) { lua_pushstring(L, g.dominantAxis ? "Height" : "Width"); return true; }
        return false;
    }
    if (g.type == GuiType::ViewportFrame) {
        if (is(k, "CurrentCamera"))  { if (g.camera) LuaApi::pushInstance(L, g.camera); else lua_pushnil(L); return true; }
        if (is(k, "Ambient"))        { LuaApi::pushColor3(L, g.ambient); return true; }
        if (is(k, "LightColor"))     { LuaApi::pushColor3(L, g.lightColor); return true; }
        if (is(k, "LightDirection")) { LuaApi::pushVector3(L, g.lightDir); return true; }
        if (is(k, "ImageColor3"))    { LuaApi::pushColor3(L, g.imageColor); return true; }
        if (is(k, "ImageTransparency")) { lua_pushnumber(L, g.imageTransparency); return true; }
    }
    if (isGuiLayout(g.type)) {
        if (is(k, "AbsoluteContentSize")) { GameGui::refresh(*E(L)->scene()); LuaApi::pushVector2(L, g.contentSize); return true; }
        if (is(k, "FillDirection"))       { lua_pushstring(L, g.fill ? "Horizontal" : "Vertical"); return true; }
        if (is(k, "HorizontalAlignment")) { lua_pushstring(L, hAlignName(g.hAlign)); return true; }
        if (is(k, "VerticalAlignment"))   { lua_pushstring(L, vAlignName(g.vAlign)); return true; }
        if (is(k, "SortOrder"))           { lua_pushstring(L, g.sortByName ? "Name" : "LayoutOrder"); return true; }
        if (is(k, "ApplyLayout"))         { lua_pushcfunction(L, gui_ApplyLayout); return true; }
        if (g.type == GuiType::UIListLayout && is(k, "Padding")) { pushUDimOf(L, g.padding, false); return true; }
        if (g.type == GuiType::UIGridLayout) {
            if (is(k, "CellSize"))    { LuaApi::pushUDim2(L, g.cellSize); return true; }
            if (is(k, "CellPadding")) { LuaApi::pushUDim2(L, g.padding); return true; }
            if (is(k, "FillDirectionMaxCells")) { lua_pushinteger(L, g.maxCells); return true; }
            if (is(k, "StartCorner")) { lua_pushstring(L, "TopLeft"); return true; }
        }
        return false;
    }
    if (g.type == GuiType::UIPadding) {
        static const char* const kSides[4] = {"PaddingLeft", "PaddingTop", "PaddingRight", "PaddingBottom"};
        for (int i = 0; i < 4; ++i)
            if (is(k, kSides[i])) { LuaApi::pushUDim(L, g.padScale[i], g.padPx[i]); return true; }
        return false;
    }
    if (g.type == GuiType::TextBox) {
        if (is(k, "PlaceholderText"))   { lua_pushstring(L, g.placeholder.c_str()); return true; }
        if (is(k, "PlaceholderColor3")) { LuaApi::pushColor3(L, g.placeholderColor); return true; }
        if (is(k, "ClearTextOnFocus"))  { lua_pushboolean(L, g.clearOnFocus); return true; }
        if (is(k, "TextEditable"))      { lua_pushboolean(L, g.editable); return true; }
        if (is(k, "MultiLine"))         { lua_pushboolean(L, g.multiLine); return true; }
        if (is(k, "Focused"))           { LuaApi::pushSignal(L, SignalKind::GuiFocused, n->id); return true; }
        if (is(k, "FocusLost"))         { LuaApi::pushSignal(L, SignalKind::GuiFocusLost, n->id); return true; }
        if (is(k, "CaptureFocus"))      { lua_pushcfunction(L, box_CaptureFocus); return true; }
        if (is(k, "ReleaseFocus"))      { lua_pushcfunction(L, box_ReleaseFocus); return true; }
        if (is(k, "IsFocused"))         { lua_pushcfunction(L, box_IsFocused); return true; }
    }
    if (g.type == GuiType::ScrollingFrame) {
        if (is(k, "CanvasSize"))        { LuaApi::pushUDim2(L, g.canvasSize); return true; }
        if (is(k, "CanvasPosition"))    { LuaApi::pushVector2(L, g.canvasPos); return true; }
        if (is(k, "AbsoluteCanvasSize")) { GameGui::refresh(*E(L)->scene()); LuaApi::pushVector2(L, g.contentSize); return true; }
        if (is(k, "AbsoluteWindowSize")) { GameGui::refresh(*E(L)->scene()); LuaApi::pushVector2(L, g.absSize); return true; }
        if (is(k, "ScrollBarThickness")) { lua_pushinteger(L, g.scrollBar); return true; }
        if (is(k, "ScrollBarImageColor3")) { LuaApi::pushColor3(L, g.scrollColor); return true; }
        if (is(k, "ScrollBarImageTransparency")) { lua_pushnumber(L, g.scrollTransparency); return true; }
        if (is(k, "ScrollingDirection")) { lua_pushstring(L, axesName(g.scrollDir)); return true; }
        if (is(k, "AutomaticCanvasSize")) { lua_pushstring(L, axesName(g.autoCanvas)); return true; }
        if (is(k, "ScrollingEnabled"))  { lua_pushboolean(L, g.scrolling); return true; }
    }
    if (is(k, "LayoutOrder"))      { lua_pushinteger(L, g.layoutOrder); return true; }
    if (is(k, "Position"))         { LuaApi::pushUDim2(L, g.pos); return true; }
    if (is(k, "Size"))             { LuaApi::pushUDim2(L, g.size); return true; }
    if (is(k, "AnchorPoint"))      { LuaApi::pushVector2(L, g.anchor); return true; }
    if (is(k, "AbsolutePosition")) { LuaApi::pushVector2(L, g.absPos); return true; }
    if (is(k, "AbsoluteSize"))     { LuaApi::pushVector2(L, g.absSize); return true; }
    if (is(k, "Visible"))          { lua_pushboolean(L, n->visible); return true; }
    if (is(k, "BackgroundColor3")) { LuaApi::pushColor3(L, g.bg); return true; }
    if (is(k, "BackgroundTransparency")) { lua_pushnumber(L, g.bgTransparency); return true; }
    if (is(k, "BorderColor3"))     { LuaApi::pushColor3(L, g.borderColor); return true; }
    if (is(k, "BorderSizePixel"))  { lua_pushinteger(L, g.border); return true; }
    if (is(k, "ZIndex"))           { lua_pushinteger(L, g.zIndex); return true; }
    if (is(k, "ClipsDescendants")) { lua_pushboolean(L, g.clips); return true; }
    if (is(k, "Active"))           { lua_pushboolean(L, n->isGuiButton()); return true; }
    if (is(k, "MouseEnter"))       { LuaApi::pushSignal(L, SignalKind::GuiEnter, n->id); return true; }
    if (is(k, "MouseLeave"))       { LuaApi::pushSignal(L, SignalKind::GuiLeave, n->id); return true; }
    if (hasText(n)) {
        if (is(k, "Text"))             { lua_pushstring(L, g.text.c_str()); return true; }
        if (is(k, "TextColor3"))       { LuaApi::pushColor3(L, g.textColor); return true; }
        if (is(k, "TextSize") || is(k, "FontSize")) { lua_pushnumber(L, g.textSize); return true; }
        if (is(k, "TextScaled"))       { lua_pushboolean(L, g.textScaled); return true; }
        if (is(k, "TextWrapped"))      { lua_pushboolean(L, g.textWrapped); return true; }
        if (is(k, "TextXAlignment"))   { lua_pushstring(L, alignName(g.xAlign, true)); return true; }
        if (is(k, "TextYAlignment"))   { lua_pushstring(L, alignName(g.yAlign, false)); return true; }
        if (is(k, "TextTransparency")) { lua_pushnumber(L, g.textTransparency); return true; }
        if (is(k, "TextStrokeColor3")) { LuaApi::pushColor3(L, g.strokeColor); return true; }
        if (is(k, "TextStrokeTransparency")) { lua_pushnumber(L, g.strokeTransparency); return true; }
        if (is(k, "Font"))             { lua_pushstring(L, g.bold ? "SourceSansBold" : "SourceSans"); return true; }
        if (is(k, "Bold"))             { lua_pushboolean(L, g.bold); return true; }   // (Font gives the same: ...Bold)
        if (is(k, "ContentText"))      { lua_pushstring(L, g.text.c_str()); return true; }
    }
    if (hasImage(n)) {
        if (is(k, "Image"))             { lua_pushstring(L, g.image.c_str()); return true; }
        if (is(k, "ImageColor3"))       { LuaApi::pushColor3(L, g.imageColor); return true; }
        if (is(k, "ImageTransparency")) { lua_pushnumber(L, g.imageTransparency); return true; }
    }
    if (n->isGuiButton()) {
        if (is(k, "MouseButton1Click") || is(k, "Activated")) { LuaApi::pushSignal(L, SignalKind::GuiClick, n->id); return true; }
        if (is(k, "AutoButtonColor")) { lua_pushboolean(L, g.autoButtonColor); return true; }
    }
    return false;
}

// Set a UI property; false if `k` isn't one.
bool guiNewIndex(lua_State* L, SceneNode* n, const char* k) {
    GuiProps& g = n->gui;
    auto num = [&]() { return (float)luaL_checknumber(L, 3); };
    auto t01 = [&]() { return std::clamp(num(), 0.0f, 1.0f); };
    if (g.type == GuiType::BillboardGui || g.type == GuiType::SurfaceGui) {
        const bool bb = g.type == GuiType::BillboardGui;
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return true; }
        if (is(k, "Adornee"))      { SceneNode* a = lua_isnoneornil(L, 3) ? nullptr : LuaApi::checkNode(L, 3); g.adornee = a ? a->id : 0; return true; }
        if (is(k, "AlwaysOnTop"))  { g.alwaysOnTop = lua_toboolean(L, 3); return true; }
        if (is(k, "MaxDistance"))  { const float d = num(); g.maxDistance = d >= 1e29f ? 0.0f : std::max(0.0f, d); return true; }
        if (is(k, "LightInfluence") || is(k, "Brightness") || is(k, "ResetOnSpawn") || is(k, "ClipsDescendants") ||
            is(k, "ZIndexBehavior") || is(k, "Active") || is(k, "ExtentsOffset") || is(k, "ExtentsOffsetWorldSpace") ||
            is(k, "SizeOffset") || is(k, "ToolPunchThroughDistance") || is(k, "PlayerToHideFrom")) return true;   // (accepted)
        if (bb) {
            if (is(k, "Size"))                  { g.size = LuaApi::checkUDim2(L, 3); return true; }
            if (is(k, "StudsOffset"))           { g.studsOffset = LuaApi::checkVector3(L, 3); return true; }
            if (is(k, "StudsOffsetWorldSpace")) { g.worldOffset = LuaApi::checkVector3(L, 3); return true; }
        } else {
            if (is(k, "Face")) {
                const char* f = luaL_checkstring(L, 3);
                for (int i = 0; i < 6; ++i) if (is(f, kFaceNames[i])) g.face = i;
                return true;
            }
            if (is(k, "CanvasSize"))    { g.surfaceCanvas = glm::max(glm::vec2(1.0f), LuaApi::checkVector2(L, 3)); return true; }
            if (is(k, "SizingMode"))    { g.perStud = is(luaL_checkstring(L, 3), "PixelsPerStud"); return true; }
            if (is(k, "PixelsPerStud")) { g.pixelsPerStud = std::clamp(num(), 1.0f, 1000.0f); return true; }
        }
        return false;
    }
    if (g.type == GuiType::ScreenGui) {
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return true; }
        if (is(k, "DisplayOrder")) { g.displayOrder = (int)luaL_checkinteger(L, 3); return true; }
        if (is(k, "ResetOnSpawn") || is(k, "IgnoreGuiInset") || is(k, "ZIndexBehavior")) return true;   // (accepted, no effect)
        return false;
    }
    if (g.type == GuiType::UICorner) {
        if (is(k, "CornerRadius")) { glm::vec2 u = LuaApi::checkUDim(L, 3); g.corner = {u.x, u.y, 0, 0}; return true; }
        if (int c = cornerIndex(k); c >= 0) {   // a UDim, a number of pixels, or nil (back to CornerRadius)
            if (lua_isnoneornil(L, 3)) { g.corners[c] = -1; g.cornerScales[c] = 0; }
            else if (lua_isnumber(L, 3)) { g.corners[c] = std::max(0.0f, num()); g.cornerScales[c] = 0; }
            else { glm::vec2 u = LuaApi::checkUDim(L, 3); g.cornerScales[c] = std::max(0.0f, u.x); g.corners[c] = std::max(0.0f, u.y); }
            return true;
        }
        return false;
    }
    if (g.type == GuiType::UIShadow) {
        if (is(k, "Color"))        { g.bg = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "Transparency")) { g.bgTransparency = t01(); return true; }
        if (is(k, "Offset"))       { g.shadowOffset = LuaApi::checkVector2(L, 3); return true; }
        if (is(k, "Blur") || is(k, "Size")) { g.shadowBlur = std::clamp(num(), 0.0f, 100.0f); return true; }
        if (is(k, "Spread"))       { g.shadowSpread = std::clamp(num(), -100.0f, 100.0f); return true; }
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return true; }
        return false;
    }
    if (g.type == GuiType::UIBlur) {
        if (is(k, "Size"))         { g.blurSize = std::clamp(num(), 0.0f, 100.0f); return true; }
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return true; }
        return false;
    }
    if (g.type == GuiType::UIStroke) {
        if (is(k, "Color"))        { g.borderColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "Thickness"))    { g.thickness = std::max(0.0f, num()); return true; }
        if (is(k, "Transparency")) { g.bgTransparency = t01(); return true; }
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return true; }
        if (is(k, "ApplyStrokeMode") || is(k, "LineJoinMode")) return true;
        return false;
    }
    if (g.type == GuiType::UIGradient) {   // Color: a ColorSequence (or a Color3); Transparency: a NumberSequence (or a number)
        if (is(k, "Color"))        { g.gradColor = checkColorSequence(L, 3); return true; }
        if (is(k, "Transparency")) { g.gradTransparency = checkNumberSequence(L, 3, 0.0f, 1.0f); return true; }
        if (is(k, "Rotation"))     { const float r = num(); g.gradRotation = std::isfinite(r) ? std::fmod(r, 360.0f) : 0.0f; return true; }
        if (is(k, "Offset"))       { g.gradOffset = LuaApi::checkVector2(L, 3); return true; }
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return true; }
        return false;
    }
    if (g.type == GuiType::UIScale) {
        if (is(k, "Scale")) {
            const float v = num();
            if (!std::isfinite(v) || v < 0.0f) luaL_error(L, "UIScale.Scale must be 0 or more (got %f)", (double)v);
            g.scale = std::min(v, 100.0f);
            return true;
        }
        return false;
    }
    if (g.type == GuiType::UIAspectRatioConstraint) {
        if (is(k, "AspectRatio")) {
            const float v = num();
            if (!std::isfinite(v) || v <= 0.0f) luaL_error(L, "AspectRatio must be more than 0 (width / height, got %f)", (double)v);
            g.aspectRatio = std::clamp(v, 0.001f, 1000.0f);
            return true;
        }
        if (is(k, "AspectType")) {   // Enum.AspectType.FitWithinMaxSize / ScaleWithParentSize
            const char* v = luaL_checkstring(L, 3);
            if (is(v, "FitWithinMaxSize")) g.aspectType = 0;
            else if (is(v, "ScaleWithParentSize")) g.aspectType = 1;
            else luaL_error(L, "'%s' isn't an AspectType (FitWithinMaxSize or ScaleWithParentSize)", v);
            return true;
        }
        if (is(k, "DominantAxis")) {   // Enum.DominantAxis.Width / Height
            const char* v = luaL_checkstring(L, 3);
            if (is(v, "Width")) g.dominantAxis = 0;
            else if (is(v, "Height")) g.dominantAxis = 1;
            else luaL_error(L, "'%s' isn't a DominantAxis (Width or Height)", v);
            return true;
        }
        return false;
    }
    if (g.type == GuiType::ViewportFrame) {
        if (is(k, "CurrentCamera")) {   // a Camera (made with Instance.new("Camera")), or nil
            if (lua_isnoneornil(L, 3)) { g.camera = 0; return true; }
            if (luaL_testudata(L, 3, kCam))
                luaL_error(L, "CurrentCamera: use a Camera made with Instance.new(\"Camera\") (workspace.CurrentCamera is the player's own)");
            SceneNode* c = LuaApi::checkNode(L, 3);
            if (!c->isCamera()) luaL_error(L, "CurrentCamera must be a Camera, not %s \"%s\"", className(L, c), c->name.c_str());
            g.camera = c->id;
            if (!c->parent) {   // (a Camera with no Parent goes in the frame, so it's saved and copied with it)
                std::string err;
                E(L)->setParent(c, n, err);
            }
            return true;
        }
        if (is(k, "Ambient"))        { g.ambient = glm::clamp(LuaApi::checkColor3(L, 3), 0.0f, 1.0f); return true; }
        if (is(k, "LightColor"))     { g.lightColor = glm::clamp(LuaApi::checkColor3(L, 3), 0.0f, 1.0f); return true; }
        if (is(k, "LightDirection")) { g.lightDir = LuaApi::checkVector3(L, 3); return true; }
        if (is(k, "ImageColor3"))    { g.imageColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "ImageTransparency")) { g.imageTransparency = t01(); return true; }
    }
    if (isGuiLayout(g.type)) {
        if (is(k, "FillDirection"))       { g.fill = is(luaL_checkstring(L, 3), "Horizontal") ? 1 : 0; return true; }
        if (is(k, "HorizontalAlignment")) { g.hAlign = parseAlign(luaL_checkstring(L, 3)); return true; }
        if (is(k, "VerticalAlignment"))   { g.vAlign = parseAlign(luaL_checkstring(L, 3)); return true; }
        if (is(k, "SortOrder"))           { g.sortByName = is(luaL_checkstring(L, 3), "Name"); return true; }
        if (g.type == GuiType::UIListLayout && is(k, "Padding")) {
            glm::vec2 u = LuaApi::checkUDim(L, 3);
            g.padding = {u.x, u.y, u.x, u.y};
            return true;
        }
        if (g.type == GuiType::UIGridLayout) {
            if (is(k, "CellSize"))    { g.cellSize = LuaApi::checkUDim2(L, 3); return true; }
            if (is(k, "CellPadding")) { g.padding = LuaApi::checkUDim2(L, 3); return true; }
            if (is(k, "FillDirectionMaxCells")) { g.maxCells = std::max(0, (int)luaL_checkinteger(L, 3)); return true; }
            if (is(k, "StartCorner")) return true;   // (accepted: always the top left)
        }
        if (is(k, "Wraps") || is(k, "HorizontalFlex") || is(k, "VerticalFlex") || is(k, "ItemLineAlignment")) return true;
        return false;
    }
    if (g.type == GuiType::UIPadding) {
        static const char* const kSides[4] = {"PaddingLeft", "PaddingTop", "PaddingRight", "PaddingBottom"};
        for (int i = 0; i < 4; ++i)
            if (is(k, kSides[i])) {
                glm::vec2 u = LuaApi::checkUDim(L, 3);
                g.padScale[i] = u.x;
                g.padPx[i] = u.y;
                return true;
            }
        return false;
    }
    if (g.type == GuiType::TextBox) {
        if (is(k, "PlaceholderText"))   { g.placeholder = luaL_tolstring(L, 3, nullptr); lua_pop(L, 1); return true; }
        if (is(k, "PlaceholderColor3")) { g.placeholderColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "ClearTextOnFocus"))  { g.clearOnFocus = lua_toboolean(L, 3); return true; }
        if (is(k, "TextEditable"))      { g.editable = lua_toboolean(L, 3); return true; }
        if (is(k, "MultiLine"))         { g.multiLine = lua_toboolean(L, 3); return true; }
        if (is(k, "Text")) {   // (Changed fires, like when someone types)
            std::string v = luaL_tolstring(L, 3, nullptr);
            lua_pop(L, 1);
            if (v != g.text) { g.text = v; E(L)->firePropertyChanged(n->id, "Text"); }
            return true;
        }
    }
    if (g.type == GuiType::ScrollingFrame) {
        if (is(k, "CanvasSize"))        { g.canvasSize = LuaApi::checkUDim2(L, 3); return true; }
        if (is(k, "CanvasPosition"))    { g.canvasPos = glm::max(glm::vec2(0.0f), LuaApi::checkVector2(L, 3)); return true; }
        if (is(k, "ScrollBarThickness")) { g.scrollBar = std::clamp((int)luaL_checkinteger(L, 3), 0, 100); return true; }
        if (is(k, "ScrollBarImageColor3")) { g.scrollColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "ScrollBarImageTransparency")) { g.scrollTransparency = t01(); return true; }
        if (is(k, "ScrollingDirection")) { g.scrollDir = std::max(1, parseAxes(luaL_checkstring(L, 3))); return true; }
        if (is(k, "AutomaticCanvasSize")) { g.autoCanvas = parseAxes(luaL_checkstring(L, 3)); return true; }
        if (is(k, "ScrollingEnabled"))  { g.scrolling = lua_toboolean(L, 3); return true; }
        if (is(k, "ElasticBehavior") || is(k, "VerticalScrollBarInset") || is(k, "HorizontalScrollBarInset") ||
            is(k, "TopImage") || is(k, "MidImage") || is(k, "BottomImage") || is(k, "VerticalScrollBarPosition")) return true;
    }
    if (is(k, "LayoutOrder"))      { g.layoutOrder = (int)luaL_checkinteger(L, 3); return true; }
    if (is(k, "Position"))         { g.pos = LuaApi::checkUDim2(L, 3); return true; }
    if (is(k, "Size"))             { g.size = LuaApi::checkUDim2(L, 3); return true; }
    if (is(k, "AnchorPoint"))      { g.anchor = LuaApi::checkVector2(L, 3); return true; }
    if (is(k, "Visible"))          { n->visible = lua_toboolean(L, 3); return true; }
    if (is(k, "BackgroundColor3")) { g.bg = LuaApi::checkColor3(L, 3); return true; }
    if (is(k, "BackgroundTransparency")) { g.bgTransparency = t01(); return true; }
    if (is(k, "BorderColor3"))     { g.borderColor = LuaApi::checkColor3(L, 3); return true; }
    if (is(k, "BorderSizePixel"))  { g.border = std::max(0, (int)luaL_checkinteger(L, 3)); return true; }
    if (is(k, "ZIndex"))           { g.zIndex = (int)luaL_checkinteger(L, 3); return true; }
    if (is(k, "ClipsDescendants")) { g.clips = lua_toboolean(L, 3); return true; }
    if (is(k, "Active") || is(k, "Selectable")) return true;
    if (hasText(n)) {
        if (is(k, "Text"))             { g.text = luaL_tolstring(L, 3, nullptr); lua_pop(L, 1); return true; }
        if (is(k, "TextColor3"))       { g.textColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "TextSize"))         { g.textSize = std::clamp(num(), 1.0f, 200.0f); return true; }
        if (is(k, "TextScaled"))       { g.textScaled = lua_toboolean(L, 3); return true; }
        if (is(k, "TextWrapped"))      { g.textWrapped = lua_toboolean(L, 3); return true; }
        if (is(k, "TextXAlignment"))   { g.xAlign = parseAlign(luaL_checkstring(L, 3)); return true; }
        if (is(k, "TextYAlignment"))   { g.yAlign = parseAlign(luaL_checkstring(L, 3)); return true; }
        if (is(k, "TextTransparency")) { g.textTransparency = t01(); return true; }
        if (is(k, "TextStrokeColor3")) { g.strokeColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "TextStrokeTransparency")) { g.strokeTransparency = t01(); return true; }
        if (is(k, "Bold"))             { g.bold = lua_toboolean(L, 3); return true; }   // (Font then reads SourceSansBold)
        if (is(k, "Font")) {   // Enum.Font.SourceSansBold -> bold; every font looks the same otherwise
            std::string f = luaL_tolstring(L, 3, nullptr);
            lua_pop(L, 1);
            g.bold = f.find("Bold") != std::string::npos || f.find("Black") != std::string::npos || f == "Arcade" ||
                     f == "FredokaOne" || f == "LuckiestGuy";
            return true;
        }
    }
    if (hasImage(n)) {
        if (is(k, "Image"))             { g.image = Online::assetRef(luaL_checkstring(L, 3)); return true; }
        if (is(k, "ImageColor3"))       { g.imageColor = LuaApi::checkColor3(L, 3); return true; }
        if (is(k, "ImageTransparency")) { g.imageTransparency = t01(); return true; }
        if (is(k, "ScaleType")) return true;
    }
    if (n->isGuiButton() && is(k, "AutoButtonColor")) { g.autoButtonColor = lua_toboolean(L, 3); return true; }
    return false;
}

// seat:Sit(humanoid): put that character on the seat (our own character only;
// other players sit by touching it themselves).
// ColorSequence / NumberSequence <-> keypoints (the Lua side is in the prelude).
void pushColorSequence(lua_State* L, const std::vector<ColorKey>& keys) {
    lua_getglobal(L, "ColorSequence");
    lua_getfield(L, -1, "new");
    lua_remove(L, -2);
    lua_createtable(L, (int)keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        lua_getglobal(L, "ColorSequenceKeypoint");
        lua_getfield(L, -1, "new");
        lua_remove(L, -2);
        lua_pushnumber(L, keys[i].t);
        LuaApi::pushColor3(L, keys[i].c);
        lua_call(L, 2, 1);
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_call(L, 1, 1);
}
void pushNumberSequence(lua_State* L, const std::vector<NumberKey>& keys) {
    lua_getglobal(L, "NumberSequence");
    lua_getfield(L, -1, "new");
    lua_remove(L, -2);
    lua_createtable(L, (int)keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        lua_getglobal(L, "NumberSequenceKeypoint");
        lua_getfield(L, -1, "new");
        lua_remove(L, -2);
        lua_pushnumber(L, keys[i].t);
        lua_pushnumber(L, keys[i].v);
        lua_call(L, 2, 1);
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_call(L, 1, 1);
}
// Reads a sequence's keypoints at idx (or a plain Color3 / number: the same all along).
template <typename Key, typename Read>
std::vector<Key> readSequence(lua_State* L, int idx, const char* what, Read read) {
    std::vector<Key> keys;
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "Keypoints");
        if (lua_istable(L, -1)) {
            const int n = std::min((int)lua_rawlen(L, -1), 20);
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, i);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "Time");
                    lua_getfield(L, -2, "Value");
                    keys.push_back({std::clamp((float)lua_tonumber(L, -2), 0.0f, 1.0f), read(L, lua_gettop(L))});
                    lua_pop(L, 2);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    } else {
        keys.push_back({0.0f, read(L, idx)});
        keys.push_back({1.0f, read(L, idx)});
    }
    if (keys.empty()) luaL_error(L, "expected a %s", what);
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
    return keys;
}
std::vector<ColorKey> checkColorSequence(lua_State* L, int idx) {
    return readSequence<ColorKey>(L, idx, "ColorSequence", [](lua_State* L, int i) { return glm::clamp(LuaApi::checkColor3(L, i), 0.0f, 1.0f); });
}
std::vector<NumberKey> checkNumberSequence(lua_State* L, int idx, float lo, float hi) {
    return readSequence<NumberKey>(L, idx, "NumberSequence", [lo, hi](lua_State* L, int i) { return std::clamp((float)luaL_checknumber(L, i), lo, hi); });
}
SceneNode* attachmentArg(lua_State* L, int idx) {
    if (lua_isnoneornil(L, idx)) return nullptr;
    SceneNode* a = LuaApi::checkNode(L, idx);
    if (!a->isAttachment()) luaL_error(L, "%s is not an Attachment", a->name.c_str());
    return a;
}
int trail_Clear(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (n->kind == NodeKind::Trail) ++n->effect.clears;
    return 0;
}
int noop(lua_State*) { return 0; }

// --- ParticleEmitter / Fire / Smoke / Sparkles ---------------------------------------
void pushNumberRange(lua_State* L, glm::vec2 r) {
    lua_getglobal(L, "NumberRange");
    lua_getfield(L, -1, "new");
    lua_remove(L, -2);
    lua_pushnumber(L, r.x);
    lua_pushnumber(L, r.y);
    lua_call(L, 2, 1);
}
// A NumberRange (or a plain number: the same both ends).
glm::vec2 checkNumberRange(lua_State* L, int idx, float lo, float hi) {
    glm::vec2 r;
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "Min");
        lua_getfield(L, idx, "Max");
        if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1)) luaL_error(L, "expected a NumberRange");
        r = {(float)lua_tonumber(L, -2), (float)lua_tonumber(L, -1)};
        lua_pop(L, 2);
    } else {
        if (!lua_isnumber(L, idx)) luaL_error(L, "expected a NumberRange (NumberRange.new(min, max)), got %s", luaL_typename(L, idx));
        r = glm::vec2((float)lua_tonumber(L, idx));
    }
    r = glm::clamp(r, lo, hi);
    if (r.y < r.x) std::swap(r.x, r.y);
    return r;
}
// Enum.NormalId.Top / "Top" / 1 -> Face::Top
bool parseFace(lua_State* L, int idx, Face& out) {
    if (lua_type(L, idx) == LUA_TNUMBER) {
        const int i = (int)lua_tointeger(L, idx);
        if (i < 0 || i > 5) return false;
        out = (Face)i;
        return true;
    }
    std::string s = luaL_tolstring(L, idx, nullptr);
    lua_pop(L, 1);
    if (const size_t dot = s.rfind('.'); dot != std::string::npos) s = s.substr(dot + 1);
    for (int i = 0; i < 6; ++i) if (s == kFaceNames[i]) { out = (Face)i; return true; }
    return false;
}
// emitter:Emit(count = 16): that many particles at once (on every player's screen).
int particle_Emit(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isParticles() || n->particle.type != ParticleType::ParticleEmitter)
        return luaL_error(L, "Emit is a ParticleEmitter method (%s is a %s)", n->name.c_str(), className(L, n));
    const double want = luaL_optnumber(L, 2, 16.0);
    if (want != want) return luaL_error(L, "Emit needs a number of particles");
    const int count = (int)std::clamp(want, 0.0, (double)kMaxSpritesPerEmitter);
    n->particle.emits += count;
    n->particle.pendingEmit = std::min(n->particle.pendingEmit + count, kMaxSpritesPerEmitter);
    return 0;
}
// emitter:Clear(): its particles vanish.
int particle_Clear(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isParticles()) return luaL_error(L, "Clear: %s isn't a ParticleEmitter", n->name.c_str());
    ++n->particle.clears;
    n->particle.pendingClear = true;
    return 0;
}
bool particleIndex(lua_State* L, SceneNode* n, const char* k) {
    const ParticleProps& p = n->particle;
    if (is(k, "Enabled"))   { lua_pushboolean(L, n->enabled); return true; }
    if (is(k, "TimeScale")) { lua_pushnumber(L, 1); return true; }
    switch (p.type) {
        case ParticleType::Fire:
            if (is(k, "Color"))          { LuaApi::pushColor3(L, p.color1); return true; }
            if (is(k, "SecondaryColor")) { LuaApi::pushColor3(L, p.color2); return true; }
            if (is(k, "Size"))           { lua_pushnumber(L, p.fxSize); return true; }
            if (is(k, "Heat"))           { lua_pushnumber(L, p.rise); return true; }
            return false;
        case ParticleType::Smoke:
            if (is(k, "Color"))        { LuaApi::pushColor3(L, p.color1); return true; }
            if (is(k, "Opacity"))      { lua_pushnumber(L, p.opacity); return true; }
            if (is(k, "RiseVelocity")) { lua_pushnumber(L, p.rise); return true; }
            if (is(k, "Size"))         { lua_pushnumber(L, p.fxSize); return true; }
            return false;
        case ParticleType::Sparkles:
            if (is(k, "SparkleColor") || is(k, "Color")) { LuaApi::pushColor3(L, p.color1); return true; }
            return false;
        default: break;
    }
    if (is(k, "Rate"))              { lua_pushnumber(L, p.rate); return true; }
    if (is(k, "Lifetime"))          { pushNumberRange(L, p.lifetime); return true; }
    if (is(k, "Speed"))             { pushNumberRange(L, p.speed); return true; }
    if (is(k, "SpreadAngle"))       { LuaApi::pushVector2(L, p.spread); return true; }
    if (is(k, "Rotation"))          { pushNumberRange(L, p.rotation); return true; }
    if (is(k, "RotSpeed"))          { pushNumberRange(L, p.rotSpeed); return true; }
    if (is(k, "Color"))             { pushColorSequence(L, p.color); return true; }
    if (is(k, "Size"))              { pushNumberSequence(L, p.size); return true; }
    if (is(k, "Transparency"))      { pushNumberSequence(L, p.transparency); return true; }
    if (is(k, "Texture"))           { lua_pushstring(L, p.texture.c_str()); return true; }
    if (is(k, "LightEmission"))     { lua_pushnumber(L, p.lightEmission); return true; }
    if (is(k, "LightInfluence"))    { lua_pushnumber(L, 1.0 - p.lightEmission); return true; }
    if (is(k, "Acceleration"))      { LuaApi::pushVector3(L, p.acceleration); return true; }
    if (is(k, "Drag"))              { lua_pushnumber(L, p.drag); return true; }
    if (is(k, "EmissionDirection")) { lua_pushstring(L, kFaceNames[(int)p.direction]); return true; }
    if (is(k, "LockedToPart"))      { lua_pushboolean(L, p.locked); return true; }
    if (is(k, "Emit"))              { lua_pushcfunction(L, particle_Emit); return true; }
    if (is(k, "Clear"))             { lua_pushcfunction(L, particle_Clear); return true; }
    if (is(k, "ZOffset") || is(k, "VelocityInheritance") || is(k, "Squash")) { lua_pushnumber(L, 0); return true; }
    if (is(k, "Brightness"))        { lua_pushnumber(L, 1); return true; }
    return false;
}
bool particleNewIndex(lua_State* L, SceneNode* n, const char* k) {
    ParticleProps& p = n->particle;
    auto num = [&](float lo, float hi) { return std::clamp((float)luaL_checknumber(L, 3), lo, hi); };
    auto color = [&] { return glm::clamp(LuaApi::checkColor3(L, 3), 0.0f, 1.0f); };
    if (is(k, "Enabled"))   { n->enabled = lua_toboolean(L, 3); return true; }
    if (is(k, "TimeScale")) return true;   // (always normal speed)
    switch (p.type) {
        case ParticleType::Fire:
            if (is(k, "Color"))          { p.color1 = color(); return true; }
            if (is(k, "SecondaryColor")) { p.color2 = color(); return true; }
            if (is(k, "Size"))           { p.fxSize = num(2.0f, 30.0f); return true; }
            if (is(k, "Heat"))           { p.rise = num(-25.0f, 25.0f); return true; }
            return false;
        case ParticleType::Smoke:
            if (is(k, "Color"))        { p.color1 = color(); return true; }
            if (is(k, "Opacity"))      { p.opacity = num(0.0f, 1.0f); return true; }
            if (is(k, "RiseVelocity")) { p.rise = num(-25.0f, 25.0f); return true; }
            if (is(k, "Size"))         { p.fxSize = num(0.1f, 100.0f); return true; }
            return false;
        case ParticleType::Sparkles:
            if (is(k, "SparkleColor") || is(k, "Color")) { p.color1 = color(); return true; }
            return false;
        default: break;
    }
    if (is(k, "Rate"))          { p.rate = num(0.0f, 1e6f); return true; }   // (how many can be out at once is capped)
    if (is(k, "Lifetime"))      { p.lifetime = checkNumberRange(L, 3, 0.0f, 1000.0f); return true; }
    if (is(k, "Speed"))         { p.speed = checkNumberRange(L, 3, -1e4f, 1e4f); return true; }
    if (is(k, "SpreadAngle"))   { p.spread = glm::clamp(LuaApi::checkVector2(L, 3), -360.0f, 360.0f); return true; }
    if (is(k, "Rotation"))      { p.rotation = checkNumberRange(L, 3, -36000.0f, 36000.0f); return true; }
    if (is(k, "RotSpeed"))      { p.rotSpeed = checkNumberRange(L, 3, -36000.0f, 36000.0f); return true; }
    if (is(k, "Color"))         { p.color = checkColorSequence(L, 3); return true; }
    if (is(k, "Size"))          { p.size = checkNumberSequence(L, 3, 0.0f, 1000.0f); return true; }
    if (is(k, "Transparency"))  { p.transparency = checkNumberSequence(L, 3, 0.0f, 1.0f); return true; }
    if (is(k, "Texture"))       { p.texture = particleTexture(Online::assetRef(luaL_checkstring(L, 3))); return true; }
    if (is(k, "LightEmission")) { p.lightEmission = num(0.0f, 1.0f); return true; }
    if (is(k, "Acceleration"))  { p.acceleration = glm::clamp(LuaApi::checkVector3(L, 3), -1e4f, 1e4f); return true; }
    if (is(k, "Drag"))          { p.drag = num(-100.0f, 100.0f); return true; }
    if (is(k, "EmissionDirection")) {
        if (!parseFace(L, 3, p.direction)) luaL_error(L, "EmissionDirection must be an Enum.NormalId (Top, Bottom, Left, Right, Front or Back)");
        return true;
    }
    if (is(k, "LockedToPart"))  { p.locked = lua_toboolean(L, 3); return true; }
    // Accepted so Roblox scripts run, but they don't change how it looks here.
    if (is(k, "LightInfluence") || is(k, "ZOffset") || is(k, "VelocityInheritance") || is(k, "Squash") ||
        is(k, "Brightness") || is(k, "Orientation") || is(k, "Shape") || is(k, "ShapeStyle") || is(k, "ShapeInOut") ||
        is(k, "ShapePartial") || is(k, "FlipbookLayout") || is(k, "FlipbookMode") || is(k, "FlipbookFramerate") ||
        is(k, "FlipbookStartRandom") || is(k, "WindAffectsDrag")) return true;
    return false;
}

int seat_sit(lua_State* L) {
    SceneNode* seat = LuaApi::checkNode(L, 1);
    uint64_t who = 0;
    if (auto* h = static_cast<uint64_t*>(luaL_testudata(L, 2, kHum))) who = *h;
    Player* p = E(L)->scene()->player();
    if (p && who && p->rootId() == who) p->sit(seat);
    return 0;
}

int inst_index(lua_State* L) {
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, 1, kInst));
    const char* k = luaL_checkstring(L, 2);

    // Methods first (they work the same for every object).
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_getfield(L, -1, k) != LUA_TNIL) return 1;
    lua_pop(L, 2);
    if (ref->id == Scene::kTerrainId) {   // workspace.Terrain: its own methods, and it lives in the Workspace
        lua_getfield(L, LUA_REGISTRYINDEX, "GB.TerrainMethods");
        if (lua_getfield(L, -1, k) != LUA_TNIL) return 1;
        lua_pop(L, 2);
        if (is(k, "Parent")) { LuaApi::pushInstance(L, E(L)->scene()->root()->id); return 1; }
        if (is(k, "ClassName")) { lua_pushstring(L, "Terrain"); return 1; }
    }

    SceneNode* n = E(L)->resolve(ref->id);
    if (!n) {
        if (is(k, "Parent")) { lua_pushnil(L); return 1; }
        return luaL_error(L, "Tried to read '%s' of an object that has been destroyed", k);
    }
    bool part = n->kind == NodeKind::Part;

    if (is(k, "Name"))      { lua_pushstring(L, n->name.c_str()); return 1; }
    if (is(k, "ClassName")) { lua_pushstring(L, className(L, n)); return 1; }
    if (is(k, "AttributeChanged")) { LuaApi::pushSignal(L, SignalKind::AttributeChanged, n->id); return 1; }
    if (n->isAttachment()) {
        if (is(k, "Position"))      { LuaApi::pushVector3(L, n->transform.position); return 1; }
        if (is(k, "WorldPosition")) { LuaApi::pushVector3(L, worldPosition(n)); return 1; }
    }
    if (n->isGui() && guiIndex(L, n, k)) return 1;
    if (n->isCamera()) {   // a Camera object (a ViewportFrame's CurrentCamera)
        if (is(k, "FieldOfView")) { lua_pushnumber(L, n->fieldOfView); return 1; }
        if (is(k, "Focus"))       { LuaApi::pushCFrame(L, getCFrame(n)); return 1; }
        if (is(k, "CameraType"))  { lua_pushstring(L, "Fixed"); return 1; }
        if (is(k, "ViewportSize")) {   // the ViewportFrame it's in, as last drawn
            const SceneNode* f = n->parent && n->parent->isViewportFrame() ? n->parent : nullptr;
            LuaApi::pushVector2(L, f ? f->gui.absSize : glm::vec2(0.0f));
            return 1;
        }
    }
    if (n->isMover() && moverIndex(L, n, k)) return 1;
    if (n->isPostEffect() && postIndex(L, n, k)) return 1;
    if (n->isSky() && skyIndex(L, n, k)) return 1;
    if (n->isConstraint() && usesJointProps(n->constraintType) && jointIndex(L, n, k)) return 1;
    if (n->isParticles() && particleIndex(L, n, k)) return 1;
    if (n->isConstraint()) {
        bool weld = n->constraintType == ConstraintType::Weld;
        const bool hingeProps = !usesJointProps(n->constraintType);
        if ((!weld && is(k, "Attachment0")) || (weld && is(k, "Part0"))) { LuaApi::pushInstance(L, n->ref0); return 1; }
        if ((!weld && is(k, "Attachment1")) || (weld && is(k, "Part1"))) { LuaApi::pushInstance(L, n->ref1); return 1; }
        if (is(k, "Length") || is(k, "FreeLength")) { lua_pushnumber(L, n->length); return 1; }
        if (is(k, "Stiffness"))      { lua_pushnumber(L, n->stiffness); return 1; }
        if (is(k, "Damping"))        { lua_pushnumber(L, n->damping); return 1; }
        if (hingeProps && is(k, "AngularVelocity")){ lua_pushnumber(L, n->motorSpeed); return 1; }
        if (hingeProps && is(k, "MotorMaxTorque")) { lua_pushnumber(L, n->motorTorque); return 1; }
        if (hingeProps && is(k, "ActuatorType"))   { lua_pushstring(L, n->motorTorque > 0 ? "Motor" : "None"); return 1; }
        if (is(k, "Thickness"))      { lua_pushnumber(L, n->thickness); return 1; }
        if (is(k, "BreakForce"))     { lua_pushnumber(L, n->breakForce); return 1; }
        if (is(k, "Broken"))         { lua_pushboolean(L, n->broken && !n->enabled); return 1; }
        if (is(k, "Color"))          { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Enabled"))        { lua_pushboolean(L, n->enabled); return 1; }
    }
    // SoundGroups and sound effects (Sound.SoundGroup is with the other Sound properties).
    if (n->isSoundGroup() && is(k, "Volume")) { lua_pushnumber(L, n->volume); return 1; }
    if (n->isSoundEffect()) {
        if (is(k, "Enabled"))  { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Priority")) { lua_pushinteger(L, n->soundFx.priority); return 1; }
        const int i = SoundFx::paramIndex(n->soundFx.type, k);
        if (i >= 0) { lua_pushnumber(L, n->soundFx.v[i]); return 1; }
    }
    if (is(k, "Parent")) {
        // Things in Lighting or the Camera: their Parent is that service / the camera itself.
        if (n->parent && n->parent == E(L)->scene()->cameraNode()) { LuaApi::pushCamera(L); return 1; }
        if (n->parent && n->parent->name == "Lighting" && E(L)->scene()->isServiceFolder(n->parent)) { LuaApi::pushLighting(L); return 1; }
        LuaApi::pushInstance(L, n->parent ? n->parent->id : 0);
        return 1;
    }
    if (is(k, "Position"))  { LuaApi::pushVector3(L, worldPosition(n)); return 1; }
    if (is(k, "Orientation") || is(k, "Rotation")) { LuaApi::pushVector3(L, n->transform.rotation); return 1; }
    if (is(k, "Size"))      { LuaApi::pushVector3(L, n->transform.scale); return 1; }
    if (is(k, "CFrame"))    { LuaApi::pushCFrame(L, getCFrame(n)); return 1; }
    if (is(k, "Visible"))   { lua_pushboolean(L, n->visible); return 1; }

    bool hrp = n->parent && hasHumanoid(L, n->parent) && n->name == "HumanoidRootPart";
    if (hrp && (is(k, "Velocity") || is(k, "AssemblyLinearVelocity"))) {
        Player* me = E(L)->scene()->player();
        Npc* npc = E(L)->scene()->npcs().find(n->parent->id);
        LuaApi::pushVector3(L, me && n->parent->id == me->rootId() ? me->velocity()
                               : npc ? npc->velocity : glm::vec3(0.0f));
        return 1;
    }

    if (part && Player::isSeat(n)) {   // Seat / VehicleSeat
        if (is(k, "Occupant")) {   // the Humanoid sitting on it, or nil
            Player* p = E(L)->scene()->player();
            if (p && p->seatId() == n->id) LuaApi::pushHumanoid(L, p->rootId()); else lua_pushnil(L);
            return 1;
        }
        if (is(k, "Disabled")) { lua_pushboolean(L, Player::seatDisabled(n)); return 1; }
        if (is(k, "Sit")) { lua_pushcfunction(L, seat_sit); return 1; }
    }
    if (part) {
        if (is(k, "Color"))        { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Transparency")) { lua_pushnumber(L, n->transparency); return 1; }
        if (is(k, "Anchored"))     { lua_pushboolean(L, n->anchored); return 1; }
        if (is(k, "CanCollide"))   { lua_pushboolean(L, n->canCollide); return 1; }
        if (is(k, "CollisionGroup")) { lua_pushstring(L, n->collisionGroup.c_str()); return 1; }
        if (is(k, "CollisionGroupId")) {   // (the old way: the group's number)
            lua_pushinteger(L, E(L)->scene()->world().collisionGroups.slotOf(n->collisionGroup));
            return 1;
        }
        if (is(k, "CastShadow"))   { lua_pushboolean(L, n->castShadow); return 1; }
        if (is(k, "Material"))     { lua_pushstring(L, kMaterialNames[(int)n->material]); return 1; }
        if (is(k, "Shape"))        { lua_pushstring(L, shapeName(n->primitiveType)); return 1; }
        if (is(k, "Velocity") || is(k, "AssemblyLinearVelocity")) { LuaApi::pushVector3(L, n->velocity); return 1; }
        if (is(k, "RotVelocity") || is(k, "AssemblyAngularVelocity")) { LuaApi::pushVector3(L, n->angularVelocity); return 1; }
        if (is(k, "Density"))    { lua_pushnumber(L, Physics::densityOf(n)); return 1; }   // (water is 1.3)
        if (is(k, "Mass") || is(k, "AssemblyMass")) { lua_pushnumber(L, massOf(n)); return 1; }
        if (Player::isWater(n)) {   // water parts and FluidVolumes (stored as attributes)
            if (is(k, "FlowVelocity")) { const Attribute* a = n->findAttribute("Flow"); LuaApi::pushVector3(L, a && a->type == Attribute::Vector3 ? a->v : glm::vec3(0.0f)); return 1; }
            if (is(k, "Clarity"))      { lua_pushnumber(L, numAttr(n, "Clarity", std::clamp(0.2f + n->transparency, 0.0f, 1.0f))); return 1; }
            if (is(k, "WaveScale"))    { lua_pushnumber(L, numAttr(n, "WaveScale", numAttr(n, "Waves", 0.0f) * 2.0f)); return 1; }
            if (is(k, "Drag"))         { lua_pushnumber(L, numAttr(n, "Drag", 1.0f)); return 1; }   // how hard it pushes things (1 = water)
        }
        if (is(k, "Friction"))   { lua_pushnumber(L, Physics::frictionOf(n)); return 1; }
        if (is(k, "Elasticity")) { lua_pushnumber(L, Physics::elasticityOf(n)); return 1; }
        // PhysicalProperties: Custom (nil = the material's own) and Current (what's used).
        if (is(k, "CustomPhysicalProperties") || is(k, "CurrentPhysicalProperties")) {
            if (is(k, "CustomPhysicalProperties") && !n->hasCustomPhysics()) { lua_pushnil(L); return 1; }
            lua_getfield(L, LUA_REGISTRYINDEX, "GB.physNew");
            lua_pushnumber(L, Physics::densityOf(n));
            lua_pushnumber(L, Physics::frictionOf(n));
            lua_pushnumber(L, Physics::elasticityOf(n));
            lua_pushnumber(L, n->frictionWeight);
            lua_pushnumber(L, n->elasticityWeight);
            lua_call(L, 5, 1);
            return 1;
        }
        if (n->isSpawnLocation()) {
            if (is(k, "Neutral"))                { lua_pushboolean(L, n->spawnNeutral); return 1; }
            if (is(k, "TeamColor"))              { LuaApi::pushColor3(L, n->teamColor); return 1; }
            if (is(k, "AllowTeamChangeOnTouch")) { lua_pushboolean(L, n->allowTeamChange); return 1; }
        }
        if (is(k, "Touched"))      { LuaApi::pushSignal(L, SignalKind::Touched, n->id); return 1; }
        if (is(k, "Clicked"))      { LuaApi::pushSignal(L, SignalKind::Clicked, n->id); return 1; }
    }
    if (n->kind == NodeKind::Model && is(k, "PrimaryPart")) {
        SceneNode* pp = primaryOf(L, n);
        LuaApi::pushInstance(L, pp ? pp->id : 0);
        return 1;
    }
    if (n->isValue()) {
        if (is(k, "Value"))   { LuaApi::pushValue(L, *n); return 1; }
        if (is(k, "Changed")) { LuaApi::pushSignal(L, SignalKind::Changed, n->id); return 1; }
    }
    if (n->isAnimation()) {
        Anim::Clip c = Anim::parse(n->source);
        if (is(k, "Looped"))      { lua_pushboolean(L, c.loop); return 1; }
        if (is(k, "Priority"))    { lua_pushstring(L, Anim::kPriorityNames[(int)c.priority]); return 1; }
        if (is(k, "Length"))      { lua_pushnumber(L, c.length()); return 1; }
        if (is(k, "AnimationId")) { lua_pushstring(L, ""); return 1; }
    }
    if (n->isDecal()) {
        if (is(k, "Texture"))      { lua_pushstring(L, n->texture.c_str()); return 1; }
        if (is(k, "Face"))         { lua_pushstring(L, kFaceNames[(int)n->face]); return 1; }
        if (is(k, "Color3"))       { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Transparency")) { lua_pushnumber(L, n->transparency); return 1; }
    }
    if (n->isTool()) {
        if (is(k, "Enabled"))        { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "ToolTip"))        { lua_pushstring(L, n->toolTip.c_str()); return 1; }
        if (is(k, "CanBeDropped"))   { lua_pushboolean(L, n->canBeDropped); return 1; }
        if (is(k, "RequiresHandle")) { lua_pushboolean(L, true); return 1; }
        if (is(k, "GripPos"))        { LuaApi::pushVector3(L, n->gripPos); return 1; }
        // Tool.Grip (a CFrame) and its axes, exactly like Roblox's.
        if (is(k, "Grip")) {
            glm::mat4 m(n->gripRot);
            m[3] = glm::vec4(n->gripPos, 1.0f);
            LuaApi::pushCFrame(L, m);
            return 1;
        }
        if (is(k, "GripRight"))      { LuaApi::pushVector3(L, n->gripRot[0]); return 1; }
        if (is(k, "GripUp"))         { LuaApi::pushVector3(L, n->gripRot[1]); return 1; }
        if (is(k, "GripForward"))    { LuaApi::pushVector3(L, n->gripRot[2]); return 1; }
        if (is(k, "Activated"))      { LuaApi::pushSignal(L, SignalKind::Activated, n->id); return 1; }
        if (is(k, "Deactivated"))    { LuaApi::pushSignal(L, SignalKind::Deactivated, n->id); return 1; }
        if (is(k, "Equipped"))       { LuaApi::pushSignal(L, SignalKind::Equipped, n->id); return 1; }
        if (is(k, "Unequipped"))     { LuaApi::pushSignal(L, SignalKind::Unequipped, n->id); return 1; }
    }
    if (n->isPrompt()) {
        const PromptProps& p = n->prompt;
        if (is(k, "ActionText"))            { lua_pushstring(L, p.action.c_str()); return 1; }
        if (is(k, "ObjectText"))            { lua_pushstring(L, p.object.c_str()); return 1; }
        if (is(k, "KeyboardKeyCode"))       { lua_pushstring(L, p.key.c_str()); return 1; }
        if (is(k, "GamepadKeyCode"))        { lua_pushstring(L, "ButtonX"); return 1; }
        if (is(k, "HoldDuration"))          { lua_pushnumber(L, p.hold); return 1; }
        if (is(k, "MaxActivationDistance")) { lua_pushnumber(L, p.range); return 1; }
        if (is(k, "RequiresLineOfSight"))   { lua_pushboolean(L, p.lineOfSight); return 1; }
        if (is(k, "ClickablePrompt"))       { lua_pushboolean(L, p.clickable); return 1; }
        if (is(k, "Enabled"))               { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Style"))                 { lua_pushstring(L, "Default"); return 1; }
        if (is(k, "Exclusivity"))           { lua_pushstring(L, "OnePerButton"); return 1; }
        if (is(k, "AutoLocalize"))          { lua_pushboolean(L, 1); return 1; }
        if (is(k, "UIOffset"))              { LuaApi::pushVector2(L, glm::vec2(0.0f)); return 1; }
        if (is(k, "Triggered"))             { LuaApi::pushSignal(L, SignalKind::PromptTriggered, n->id); return 1; }
        if (is(k, "TriggerEnded"))          { LuaApi::pushSignal(L, SignalKind::PromptTriggerEnded, n->id); return 1; }
        if (is(k, "PromptButtonHoldBegan")) { LuaApi::pushSignal(L, SignalKind::PromptHoldBegan, n->id); return 1; }
        if (is(k, "PromptButtonHoldEnded")) { LuaApi::pushSignal(L, SignalKind::PromptHoldEnded, n->id); return 1; }
        if (is(k, "PromptShown"))           { LuaApi::pushSignal(L, SignalKind::PromptShown, n->id); return 1; }
        if (is(k, "PromptHidden"))          { LuaApi::pushSignal(L, SignalKind::PromptHidden, n->id); return 1; }
    }
    if (n->isClickDetector()) {
        if (is(k, "MaxActivationDistance")) { lua_pushnumber(L, n->click.range); return 1; }
        if (is(k, "CursorIcon"))            { lua_pushstring(L, n->click.cursor.c_str()); return 1; }
        if (is(k, "MouseClick"))            { LuaApi::pushSignal(L, SignalKind::ClickMouseClick, n->id); return 1; }
        if (is(k, "RightMouseClick"))       { LuaApi::pushSignal(L, SignalKind::ClickRightClick, n->id); return 1; }
        if (is(k, "MouseHoverEnter"))       { LuaApi::pushSignal(L, SignalKind::ClickHoverEnter, n->id); return 1; }
        if (is(k, "MouseHoverLeave"))       { LuaApi::pushSignal(L, SignalKind::ClickHoverLeave, n->id); return 1; }
    }
    if (n->isHighlight()) {
        const HighlightProps& h = n->highlight;
        if (is(k, "FillColor"))           { LuaApi::pushColor3(L, h.fill); return 1; }
        if (is(k, "OutlineColor"))        { LuaApi::pushColor3(L, h.outline); return 1; }
        if (is(k, "FillTransparency"))    { lua_pushnumber(L, h.fillTransparency); return 1; }
        if (is(k, "OutlineTransparency")) { lua_pushnumber(L, h.outlineTransparency); return 1; }
        if (is(k, "DepthMode"))           { lua_pushstring(L, h.onTop ? "AlwaysOnTop" : "Occluded"); return 1; }
        if (is(k, "Enabled"))             { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Adornee"))             { if (h.adornee && E(L)->scene()->findById(h.adornee)) LuaApi::pushInstance(L, h.adornee); else lua_pushnil(L); return 1; }
    }
    if (n->isEffect()) {
        const EffectProps& e = n->effect;
        const bool beam = n->kind == NodeKind::Beam;
        auto att = [&](uint64_t id) { if (id && E(L)->scene()->findById(id)) LuaApi::pushInstance(L, id); else lua_pushnil(L); return 1; };
        if (is(k, "Attachment0"))   return att(e.a0);
        if (is(k, "Attachment1"))   return att(e.a1);
        if (is(k, "Color"))         { pushColorSequence(L, e.color); return 1; }
        if (is(k, "Transparency"))  { pushNumberSequence(L, e.transparency); return 1; }
        if (is(k, "Texture"))       { lua_pushstring(L, e.texture.c_str()); return 1; }
        if (is(k, "LightEmission")) { lua_pushnumber(L, e.lightEmission); return 1; }
        if (is(k, "LightInfluence")) { lua_pushnumber(L, 1.0 - e.lightEmission); return 1; }
        if (is(k, "Brightness"))    { lua_pushnumber(L, 1); return 1; }
        if (is(k, "FaceCamera"))    { lua_pushboolean(L, e.faceCamera); return 1; }
        if (is(k, "Enabled"))       { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "TextureMode"))   { lua_pushstring(L, e.textureWrap ? "Wrap" : "Stretch"); return 1; }
        if (is(k, "TextureLength")) { lua_pushnumber(L, e.textureLength); return 1; }
        if (is(k, "ZOffset"))       { lua_pushnumber(L, 0); return 1; }
        if (!beam) {
            if (is(k, "Lifetime"))   { lua_pushnumber(L, e.lifetime); return 1; }
            if (is(k, "MinLength"))  { lua_pushnumber(L, e.minLength); return 1; }
            if (is(k, "MaxLength"))  { lua_pushnumber(L, e.maxLength); return 1; }
            if (is(k, "WidthScale")) { pushNumberSequence(L, e.widthScale); return 1; }
            if (is(k, "Clear"))      { lua_pushcfunction(L, trail_Clear); return 1; }
        } else {
            if (is(k, "Width0"))       { lua_pushnumber(L, e.width0); return 1; }
            if (is(k, "Width1"))       { lua_pushnumber(L, e.width1); return 1; }
            if (is(k, "CurveSize0"))   { lua_pushnumber(L, e.curve0); return 1; }
            if (is(k, "CurveSize1"))   { lua_pushnumber(L, e.curve1); return 1; }
            if (is(k, "Segments"))     { lua_pushinteger(L, e.segments); return 1; }
            if (is(k, "TextureSpeed")) { lua_pushnumber(L, e.textureSpeed); return 1; }
            if (is(k, "SetTextureOffset")) { lua_pushcfunction(L, noop); return 1; }
        }
    }
    if (n->kind == NodeKind::Remote) {
        if (is(k, "OnServerEvent"))  { LuaApi::pushSignal(L, SignalKind::RemoteServer, n->id); return 1; }
        if (is(k, "OnClientEvent"))  { LuaApi::pushSignal(L, SignalKind::RemoteClient, n->id); return 1; }
        if (is(k, "OnServerInvoke")) {
            lua_getfield(L, LUA_REGISTRYINDEX, "GB.invokers");
            lua_pushvalue(L, 1);
            lua_rawget(L, -2);
            return 1;
        }
    }
    if (n->kind == NodeKind::Script) {
        if (is(k, "Enabled"))  { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Disabled")) { lua_pushboolean(L, !n->enabled); return 1; }
        if (is(k, "Source"))   { lua_pushstring(L, n->source.c_str()); return 1; }
    }
    if (n->isSound()) {
        if (is(k, "SoundId"))       { lua_pushstring(L, n->soundId.c_str()); return 1; }
        if (is(k, "Volume"))        { lua_pushnumber(L, n->volume); return 1; }
        if (is(k, "PlaybackSpeed") || is(k, "Pitch")) { lua_pushnumber(L, n->pitch); return 1; }
        if (is(k, "Looped"))        { lua_pushboolean(L, n->looped); return 1; }
        if (is(k, "Playing") || is(k, "IsPlaying")) { lua_pushboolean(L, Audio::isPlaying(n->audioHandle)); return 1; }
        if (is(k, "SoundGroup")) {
            SceneNode* g = n->soundGroup ? E(L)->resolve(n->soundGroup) : nullptr;
            if (g && g->isSoundGroup()) LuaApi::pushInstance(L, g->id); else lua_pushnil(L);
            return 1;
        }
    }
    if (n->kind == NodeKind::FluidSystem) {
        if (is(k, "Color"))          { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Viscosity"))      { lua_pushnumber(L, n->viscosity); return 1; }
        if (is(k, "SurfaceTension")) { lua_pushnumber(L, n->surfaceTension); return 1; }
    }
    if (n->kind == NodeKind::FluidEmitter) {
        if (is(k, "Rate"))        { lua_pushnumber(L, n->fluidRate); return 1; }
        if (is(k, "Velocity"))    { LuaApi::pushVector3(L, n->fluidVelocity); return 1; }
        if (is(k, "Size"))        { LuaApi::pushVector3(L, n->transform.scale); return 1; }
        if (is(k, "Position"))    { LuaApi::pushVector3(L, worldPosition(n)); return 1; }
        if (is(k, "Enabled"))     { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "FluidSystem")) { LuaApi::pushInstance(L, n->fluidSystem); return 1; }
    }
    if (n->isLight()) {
        if (is(k, "Enabled"))    { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Brightness")) { lua_pushnumber(L, n->brightness); return 1; }
        if (is(k, "Range"))      { lua_pushnumber(L, n->range); return 1; }
        if (is(k, "Angle"))      { lua_pushnumber(L, n->spotAngle); return 1; }
        if (is(k, "Color"))      { LuaApi::pushColor3(L, n->color); return 1; }
    }
    if (n == E(L)->scene()->root()) {
        const WorldSettings& w = E(L)->scene()->world();
        if (is(k, "Gravity"))    { lua_pushnumber(L, w.gravity); return 1; }
        if (is(k, "DeathStyle")) { lua_pushstring(L, w.deathStyle == DeathStyle::Ragdoll ? "Ragdoll" : "Classic"); return 1; }
        if (is(k, "Knockdowns")) { lua_pushboolean(L, w.knockdowns); return 1; }
        if (is(k, "Gore"))       { lua_pushstring(L, w.gore == GoreLevel::Blood ? "Blood" : w.gore == GoreLevel::OilAndBolts ? "Oil" : "Off"); return 1; }
        if (is(k, "FallDamage")) { lua_pushboolean(L, w.fallDamage); return 1; }
        if (is(k, "SafeFallSpeed"))   { lua_pushnumber(L, w.fallDamageSpeed); return 1; }
        if (is(k, "FallDamageScale")) { lua_pushnumber(L, w.fallDamageScale); return 1; }
        if (is(k, "PlayerCollisions")) { lua_pushboolean(L, w.playerCollisions); return 1; }
        if (is(k, "VoiceChatEnabled")) { lua_pushboolean(L, w.voiceChat); return 1; }
        if (is(k, "Orthographic"))     { lua_pushboolean(L, w.orthographic); return 1; }
        if (is(k, "CurrentCamera"))    { LuaApi::pushCamera(L); return 1; }
        if (is(k, "OrthographicSize")) { lua_pushnumber(L, w.orthographicSize); return 1; }
        if (is(k, "BloodColor"))  { LuaApi::pushColor3(L, w.bloodColor); return 1; }
        if (is(k, "BloodAmount")) { lua_pushnumber(L, w.bloodAmount); return 1; }
        if (is(k, "MaxFluidParticles")) { lua_pushinteger(L, w.maxFluidParticles); return 1; }
    }
    if (is(k, "Humanoid") && hasHumanoid(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    // Properties we keep as attributes (an imported Roblox SpecialMesh's MeshId, TextureId,
    // Scale...): read them like real properties.
    for (const Attribute& a : n->attributes)
        if (a.name == k) { pushAttribute(L, a); return 1; }
    // Every object has Changed / GetPropertyChangedSignal, like Roblox (values fire Changed;
    // properties kept as attributes fire their own signal).
    if (is(k, "Changed")) { LuaApi::pushSignal(L, SignalKind::Changed, n->id); return 1; }
    if (is(k, "GetPropertyChangedSignal")) { lua_pushcfunction(L, m_GetPropertyChangedSignal); return 1; }

    // Like Roblox, `workspace.Door` finds a child called "Door".
    if (SceneNode* c = n->findChild(k)) { LuaApi::pushInstance(L, c->id); return 1; }
    if (n == E(L)->scene()->root() && is(k, "Terrain")) { LuaApi::pushInstance(L, Scene::kTerrainId); return 1; }
    return luaL_error(L, "'%s' is not a valid member of %s \"%s\"", k, className(L, n), n->name.c_str());
}

int inst_newindex(lua_State* L) {
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, 1, kInst));
    const char* k = luaL_checkstring(L, 2);
    SceneNode* n = E(L)->resolve(ref->id);
    if (!n) return luaL_error(L, "Tried to set '%s' of an object that has been destroyed", k);
    if (ref->id == Scene::kTerrainId) return luaL_error(L, "Terrain's %s can't be changed (use its methods, like FillBlock)", k);
    bool part = n->kind == NodeKind::Part;
    Scene* scene = E(L)->scene();

    if (n->isAnimation() && (is(k, "Looped") || is(k, "Priority"))) {
        Anim::Clip c = Anim::parse(n->source);
        if (is(k, "Looped")) c.loop = lua_toboolean(L, 3);
        else {
            Anim::Priority pr;
            if (!parsePriority(luaL_checkstring(L, 3), pr)) return luaL_error(L, "Priority must be Core, Idle, Movement or Action");
            c.priority = pr;
        }
        n->source = Anim::dump(c);
        return 0;
    }
    if (n->isAttachment()) {
        if (is(k, "Position"))      { n->transform.position = LuaApi::checkVector3(L, 3); return 0; }
        if (is(k, "WorldPosition")) { setWorldPosition(L, n, LuaApi::checkVector3(L, 3)); return 0; }
    }
    if (n->isGui() && guiNewIndex(L, n, k)) return 0;
    if (n->isCamera()) {
        if (is(k, "FieldOfView")) { n->fieldOfView = std::clamp((float)luaL_checknumber(L, 3), 1.0f, 120.0f); return 0; }
        if (is(k, "Focus") || is(k, "CameraType") || is(k, "CameraSubject") || is(k, "FieldOfViewMode") ||
            is(k, "DiagonalFieldOfView") || is(k, "MaxAxisFieldOfView") || is(k, "HeadLocked")) return 0;   // (accepted)
    }
    if (n->isMover() && moverNewIndex(L, n, k)) return 0;
    if (n->isPostEffect() && postNewIndex(L, n, k)) return 0;
    if (n->isSky() && skyNewIndex(L, n, k)) return 0;
    if (n->isConstraint() && usesJointProps(n->constraintType) && jointNewIndex(L, n, k)) return 0;
    if (n->isParticles() && particleNewIndex(L, n, k)) return 0;
    if (n->isConstraint()) {
        bool weld = n->constraintType == ConstraintType::Weld;
        const bool hingeProps = !usesJointProps(n->constraintType);
        auto ref = [&](uint64_t& r) {
            SceneNode* t = lua_isnil(L, 3) ? nullptr : LuaApi::checkNode(L, 3);
            r = t ? t->id : 0;
            n->jointReady = false;
        };
        if ((!weld && is(k, "Attachment0")) || (weld && is(k, "Part0"))) { ref(n->ref0); return 0; }
        if ((!weld && is(k, "Attachment1")) || (weld && is(k, "Part1"))) { ref(n->ref1); return 0; }
        if (is(k, "Length") || is(k, "FreeLength")) { n->length = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "Stiffness"))      { n->stiffness = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "Damping"))        { n->damping = (float)luaL_checknumber(L, 3); return 0; }
        if (hingeProps && is(k, "AngularVelocity")){ n->motorSpeed = (float)luaL_checknumber(L, 3); return 0; }
        if (hingeProps && is(k, "MotorMaxTorque")) { n->motorTorque = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (hingeProps && is(k, "ActuatorType")) {
            bool motor = std::string(luaL_checkstring(L, 3)) == "Motor";
            n->motorTorque = motor ? (n->motorTorque > 0 ? n->motorTorque : 2000.0f) : 0.0f;
            return 0;
        }
        if (is(k, "Thickness"))      { n->thickness = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "BreakForce"))     { n->breakForce = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Color"))          { n->color = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "Enabled"))        { n->enabled = lua_toboolean(L, 3); return 0; }
    }
    // SoundGroups and sound effects (the mixer picks changes up every frame, see SoundMix).
    if (n->isSoundGroup() && is(k, "Volume")) { n->volume = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 10.0f); return 0; }
    if (n->isSoundEffect()) {
        if (is(k, "Enabled"))  { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "Priority")) { n->soundFx.priority = (int)luaL_checknumber(L, 3); return 0; }
        const int i = SoundFx::paramIndex(n->soundFx.type, k);
        if (i >= 0) {
            const SoundFx::Param& p = SoundFx::params(n->soundFx.type)[i];
            n->soundFx.v[i] = std::clamp((float)luaL_checknumber(L, 3), p.lo, p.hi);   // (Roblox keeps them in range too)
            return 0;
        }
    }
    if (is(k, "Name")) {
        if (scene->isProtected(n)) return luaL_error(L, "You can't rename %s", n->name.c_str());
        n->name = luaL_checkstring(L, 3);
        return 0;
    }
    if (is(k, "Parent")) {
        SceneNode* p = lua_isnil(L, 3) ? nullptr : LuaApi::checkNode(L, 3);
        std::string err;
        if (!E(L)->setParent(n, p, err)) return luaL_error(L, "%s", err.c_str());
        return 0;
    }
    if (is(k, "Position"))  { setWorldPosition(L, n, LuaApi::checkVector3(L, 3)); return 0; }
    if (is(k, "Orientation") || is(k, "Rotation")) { n->transform.rotation = LuaApi::checkVector3(L, 3); return 0; }
    if (is(k, "Size"))      { n->transform.scale = glm::max(LuaApi::checkVector3(L, 3), glm::vec3(0.001f)); return 0; }
    if (is(k, "CFrame"))    { setCFrame(L, n, LuaApi::checkCFrame(L, 3)); return 0; }
    if (is(k, "Visible"))   { n->visible = lua_toboolean(L, 3); return 0; }

    // Setting the HumanoidRootPart's velocity launches the character.
    bool hrp = n->parent && hasHumanoid(L, n->parent) && n->name == "HumanoidRootPart";
    if (hrp && (is(k, "Velocity") || is(k, "AssemblyLinearVelocity"))) {
        Player* me = scene->player();
        if (me && n->parent->id == me->rootId()) me->launch(LuaApi::checkVector3(L, 3));
        else if (Npc* npc = scene->npcs().find(n->parent->id)) { npc->velocity = LuaApi::checkVector3(L, 3); npc->grounded = false; }
        else if (RemoteCharacter* rc = scene->findRemote(n->parent->id))
            rc->kills.push_back({-1.0f, LuaApi::checkVector3(L, 3)});   // force < 0 = just a push
        return 0;
    }

    if (part) {
        if (is(k, "Color"))        { n->color = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "Transparency")) { n->transparency = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (Player::isWater(n)) {
            if (is(k, "FlowVelocity")) { setAttr(n, "Flow", Attribute::Vector3, 0.0, LuaApi::checkVector3(L, 3)); return 0; }
            if (is(k, "Clarity"))      { setAttr(n, "Clarity", Attribute::Number, std::clamp(luaL_checknumber(L, 3), 0.0, 1.0)); return 0; }
            if (is(k, "Drag"))         { setAttr(n, "Drag", Attribute::Number, std::clamp(luaL_checknumber(L, 3), 0.0, 20.0)); return 0; }
            if (is(k, "WaveScale")) {
                setAttr(n, "WaveScale", Attribute::Number, std::max(0.0, luaL_checknumber(L, 3)));
                n->attributes.erase(std::remove_if(n->attributes.begin(), n->attributes.end(),
                                                   [](const Attribute& a) { return a.name == "Waves"; }), n->attributes.end());
                return 0;
            }
        }
        if (is(k, "Disabled") && Player::isSeat(n)) {   // switch a seat off (whoever's on it gets up)
            Attribute* a = nullptr;
            for (auto& x : n->attributes) if (x.name == "Disabled") a = &x;
            if (!a) { n->attributes.push_back(Attribute{}); a = &n->attributes.back(); a->name = "Disabled"; }
            a->type = Attribute::Bool;
            a->b = lua_toboolean(L, 3);
            return 0;
        }
        if (is(k, "Anchored"))     { n->anchored = lua_toboolean(L, 3); if (n->anchored) n->velocity = glm::vec3(0.0f); return 0; }
        if (is(k, "CanCollide"))   { n->canCollide = lua_toboolean(L, 3); return 0; }
        if (is(k, "CollisionGroup")) {
            if (lua_type(L, 3) != LUA_TSTRING) return luaL_error(L, "CollisionGroup must be a string (a group's name)");
            const std::string g = lua_tostring(L, 3);
            if (g.size() > CollisionGroups::kMaxNameLength) return luaL_error(L, "collision group names can be at most 100 characters");
            n->collisionGroup = g.empty() ? std::string(CollisionGroups::kDefault) : g;
            return 0;
        }
        if (is(k, "CollisionGroupId")) {
            const CollisionGroups& groups = E(L)->scene()->world().collisionGroups;
            const lua_Integer id = luaL_checkinteger(L, 3);
            if (id < 0 || id >= CollisionGroups::kMax || groups.names[id].empty())
                return luaL_error(L, "there's no collision group with id %d", (int)id);
            n->collisionGroup = groups.names[id];
            return 0;
        }
        if (is(k, "CastShadow"))   { n->castShadow = lua_toboolean(L, 3); return 0; }
        if (is(k, "Material")) {
            if (!parseMaterial(luaL_checkstring(L, 3), n->material))
                return luaL_error(L, "Unknown material '%s' (the materials are %s)", lua_tostring(L, 3), materialNameList());
            return 0;
        }
        if (is(k, "Shape")) {
            PrimitiveType t;
            if (!parseShape(luaL_checkstring(L, 3), t))
                return luaL_error(L, "Unknown shape '%s' (try Block, Ball, Cylinder or Plane)", lua_tostring(L, 3));
            n->primitiveType = t;
            n->mesh = MeshLibrary::get(t);
            return 0;
        }
        if (is(k, "Velocity") || is(k, "AssemblyLinearVelocity")) { n->velocity = LuaApi::checkVector3(L, 3); n->sleepTime = 0; return 0; }
        if (is(k, "RotVelocity") || is(k, "AssemblyAngularVelocity")) { n->angularVelocity = LuaApi::checkVector3(L, 3); n->sleepTime = 0; return 0; }
        if (is(k, "Density"))    { n->density = std::max(0.01f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Friction"))   { n->friction = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Elasticity")) { n->elasticity = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "CustomPhysicalProperties")) {   // PhysicalProperties.new(...), or nil = the material's own
            if (lua_isnil(L, 3)) {
                n->density = n->friction = n->elasticity = -1.0f;
                n->frictionWeight = n->elasticityWeight = 1.0f;
                return 0;
            }
            auto field = [&](const char* name, float lo, float hi) {
                lua_getfield(L, 3, name);
                if (!lua_isnumber(L, -1)) luaL_error(L, "CustomPhysicalProperties takes PhysicalProperties.new(density, friction, elasticity)");
                const float v = std::clamp((float)lua_tonumber(L, -1), lo, hi);
                lua_pop(L, 1);
                return v;
            };
            if (!lua_istable(L, 3)) return luaL_error(L, "CustomPhysicalProperties takes PhysicalProperties.new(density, friction, elasticity)");
            n->density          = field("Density", 0.01f, 100.0f);
            n->friction         = field("Friction", 0.0f, 2.0f);
            n->elasticity       = field("Elasticity", 0.0f, 1.0f);
            n->frictionWeight   = field("FrictionWeight", 0.0f, 100.0f);
            n->elasticityWeight = field("ElasticityWeight", 0.0f, 100.0f);
            n->sleepTime = 0;
            return 0;
        }
        if (n->isSpawnLocation()) {
            if (is(k, "Neutral"))                { n->spawnNeutral = lua_toboolean(L, 3); return 0; }
            if (is(k, "TeamColor"))              { n->teamColor = glm::clamp(LuaApi::checkColor3(L, 3), 0.0f, 1.0f); return 0; }
            if (is(k, "AllowTeamChangeOnTouch")) { n->allowTeamChange = lua_toboolean(L, 3); return 0; }
        }
    }
    if (n->kind == NodeKind::Model && is(k, "PrimaryPart")) {
        if (lua_isnil(L, 3)) { n->primaryPart = 0; return 0; }
        SceneNode* pp = LuaApi::checkNode(L, 3);
        if (!pp->isPart() || !n->isAncestorOf(pp))
            return luaL_error(L, "PrimaryPart must be a part inside %s", n->name.c_str());
        n->primaryPart = pp->id;
        return 0;
    }
    if (n->isPrompt()) {
        PromptProps& p = n->prompt;
        auto text = [&](size_t max) {
            std::string s = luaL_tolstring(L, 3, nullptr);
            lua_pop(L, 1);
            if (s.size() > max) s.resize(max);
            return s;
        };
        if (is(k, "ActionText"))            { p.action = text(100); return 0; }
        if (is(k, "ObjectText"))            { p.object = text(100); return 0; }
        if (is(k, "KeyboardKeyCode"))       { p.key = text(30); return 0; }
        if (is(k, "HoldDuration"))          { p.hold = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 60.0f); return 0; }
        if (is(k, "MaxActivationDistance")) { p.range = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 500.0f); return 0; }
        if (is(k, "RequiresLineOfSight"))   { p.lineOfSight = lua_toboolean(L, 3); return 0; }
        if (is(k, "ClickablePrompt"))       { p.clickable = lua_toboolean(L, 3); return 0; }
        if (is(k, "Enabled"))               { n->enabled = lua_toboolean(L, 3); return 0; }
        // Looks and gamepads: accepted so Roblox scripts run (the card always looks the same).
        if (is(k, "Style") || is(k, "Exclusivity") || is(k, "GamepadKeyCode") || is(k, "UIOffset") ||
            is(k, "AutoLocalize") || is(k, "RootLocalizationTable")) return 0;
    }
    if (n->isClickDetector()) {
        if (is(k, "MaxActivationDistance")) { n->click.range = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 500.0f); return 0; }
        if (is(k, "CursorIcon")) {
            std::string c = luaL_tolstring(L, 3, nullptr);
            lua_pop(L, 1);
            if (c.size() > 300) c.resize(300);
            n->click.cursor = c;
            return 0;
        }
    }
    if (n->isHighlight()) {
        HighlightProps& h = n->highlight;
        if (is(k, "FillColor"))           { h.fill = glm::clamp(LuaApi::checkColor3(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "OutlineColor"))        { h.outline = glm::clamp(LuaApi::checkColor3(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "FillTransparency"))    { h.fillTransparency = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "OutlineTransparency")) { h.outlineTransparency = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "DepthMode")) {   // Enum.HighlightDepthMode.AlwaysOnTop / Occluded
            std::string m = luaL_tolstring(L, 3, nullptr);
            lua_pop(L, 1);
            h.onTop = m.find("Occluded") == std::string::npos && m != "1";
            return 0;
        }
        if (is(k, "Enabled"))             { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "Adornee"))             { SceneNode* a = lua_isnoneornil(L, 3) ? nullptr : LuaApi::checkNode(L, 3); h.adornee = a ? a->id : 0; return 0; }
    }
    if (n->isEffect()) {
        EffectProps& e = n->effect;
        const bool beam = n->kind == NodeKind::Beam;
        auto num = [&](float lo, float hi) { return std::clamp((float)luaL_checknumber(L, 3), lo, hi); };
        if (is(k, "Attachment0"))   { SceneNode* a = attachmentArg(L, 3); e.a0 = a ? a->id : 0; return 0; }
        if (is(k, "Attachment1"))   { SceneNode* a = attachmentArg(L, 3); e.a1 = a ? a->id : 0; return 0; }
        if (is(k, "Color"))         { e.color = checkColorSequence(L, 3); return 0; }
        if (is(k, "Transparency"))  { e.transparency = checkNumberSequence(L, 3, 0.0f, 1.0f); return 0; }
        if (is(k, "Texture"))       { e.texture = Online::assetRef(luaL_checkstring(L, 3)); return 0; }
        if (is(k, "LightEmission")) { e.lightEmission = num(0.0f, 1.0f); return 0; }
        if (is(k, "FaceCamera"))    { e.faceCamera = lua_toboolean(L, 3); return 0; }
        if (is(k, "Enabled"))       { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "TextureMode")) {   // Enum.TextureMode.Wrap / Stretch / Static
            std::string m = luaL_tolstring(L, 3, nullptr);
            lua_pop(L, 1);
            e.textureWrap = m.find("Stretch") == std::string::npos;
            return 0;
        }
        if (is(k, "TextureLength")) { e.textureLength = num(0.001f, 10000.0f); return 0; }
        if (is(k, "LightInfluence") || is(k, "Brightness") || is(k, "ZOffset")) return 0;   // (looks the same)
        if (!beam) {
            if (is(k, "Lifetime"))   { e.lifetime = num(0.0f, 20.0f); return 0; }
            if (is(k, "MinLength"))  { e.minLength = num(0.0f, 1000.0f); return 0; }
            if (is(k, "MaxLength"))  { e.maxLength = num(0.0f, 10000.0f); return 0; }
            if (is(k, "WidthScale")) { e.widthScale = checkNumberSequence(L, 3, 0.0f, 100.0f); return 0; }
        } else {
            if (is(k, "Width0"))       { e.width0 = num(0.0f, 1000.0f); return 0; }
            if (is(k, "Width1"))       { e.width1 = num(0.0f, 1000.0f); return 0; }
            if (is(k, "CurveSize0"))   { e.curve0 = num(-1000.0f, 1000.0f); return 0; }
            if (is(k, "CurveSize1"))   { e.curve1 = num(-1000.0f, 1000.0f); return 0; }
            if (is(k, "Segments"))     { e.segments = (int)num(1.0f, 1000.0f); return 0; }
            if (is(k, "TextureSpeed")) { e.textureSpeed = num(-1000.0f, 1000.0f); return 0; }
        }
    }
    if (n->kind == NodeKind::Remote) {
        if (is(k, "OnServerInvoke")) {
            if (!lua_isnil(L, 3)) luaL_checktype(L, 3, LUA_TFUNCTION);
            lua_getfield(L, LUA_REGISTRYINDEX, "GB.invokers");
            lua_pushvalue(L, 1);
            lua_pushvalue(L, 3);
            lua_rawset(L, -3);
            return 0;
        }
        if (is(k, "OnClientInvoke")) return 0;   // InvokeClient isn't supported, so nothing calls it
    }
    if (n->kind == NodeKind::Script) {
        const bool legacyControlScript = n->name == "ControlScript" && n->parent && n->parent->name == "PlayerScripts";
        if (is(k, "Enabled")) {
            const bool enabled = lua_toboolean(L, 3);
            LuaApi::engine(L)->setScriptEnabled(n, enabled);
            if (legacyControlScript) if (Player* p = E(L)->scene()->player()) p->setNativeControlsEnabled(enabled);
            return 0;
        }
        if (is(k, "Disabled")) {
            const bool enabled = !lua_toboolean(L, 3);
            LuaApi::engine(L)->setScriptEnabled(n, enabled);
            if (legacyControlScript) if (Player* p = E(L)->scene()->player()) p->setNativeControlsEnabled(enabled);
            return 0;
        }
        if (is(k, "Source")) {   // like Roblox: Studio's command bar and plugins may, game scripts may not
            if (E(L)->currentScript()) return luaL_error(L, "Source can only be set from the command bar or a plugin");
            n->source = luaL_checkstring(L, 3);
            return 0;
        }
    }
    if (n->kind == NodeKind::FluidSystem) {
        if (is(k, "Color"))          { n->color = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "Viscosity"))      { n->viscosity = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "SurfaceTension")) { n->surfaceTension = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
    }
    if (n->kind == NodeKind::FluidEmitter) {
        if (is(k, "Rate"))     { n->fluidRate = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 100000.0f); return 0; }
        if (is(k, "Velocity")) { n->fluidVelocity = LuaApi::checkVector3(L, 3); return 0; }
        if (is(k, "Size"))     { n->transform.scale = glm::max(LuaApi::checkVector3(L, 3), glm::vec3(0.1f)); return 0; }
        if (is(k, "Position")) { setWorldPosition(L, n, LuaApi::checkVector3(L, 3)); return 0; }
        if (is(k, "Enabled"))  { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "FluidSystem")) {
            if (lua_isnil(L, 3)) { n->fluidSystem = 0; return 0; }
            SceneNode* sys = LuaApi::checkNode(L, 3);
            if (sys->kind != NodeKind::FluidSystem) return luaL_error(L, "FluidEmitter.FluidSystem must be a FluidSystem");
            n->fluidSystem = sys->id;
            return 0;
        }
    }
    if (n->isSound()) {
        if (is(k, "SoundId"))  { n->soundId = Online::assetRef(luaL_checkstring(L, 3)); return 0; }
        if (is(k, "Volume"))   { n->volume = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 10.0f); Audio::setVolume(n->audioHandle, SoundMix::volume(*E(L)->scene(), n)); return 0; }
        if (is(k, "PlaybackSpeed") || is(k, "Pitch")) { n->pitch = std::max(0.05f, (float)luaL_checknumber(L, 3)); Audio::setPitch(n->audioHandle, n->pitch); return 0; }
        if (is(k, "Looped"))   { n->looped = lua_toboolean(L, 3); return 0; }
        if (is(k, "SoundGroup")) {
            SceneNode* g = lua_isnoneornil(L, 3) ? nullptr : LuaApi::checkNode(L, 3);
            if (g && !g->isSoundGroup()) return luaL_error(L, "Sound.SoundGroup must be a SoundGroup (or nil)");
            n->soundGroup = g ? g->id : 0;
            if (n->audioHandle) SoundMix::apply(*scene, n, n->audioHandle);
            return 0;
        }
        if (is(k, "Playing"))  {
            if (lua_toboolean(L, 3)) startSound(L, n);
            else { Audio::stop(n->audioHandle); n->audioHandle = 0; }
            return 0;
        }
    }
    if (n->isValue() && is(k, "Value")) {
        Attribute& v = n->value;
        switch (v.type) {
            case Attribute::Bool:   v.b = lua_toboolean(L, 3); break;
            case Attribute::String: v.s = luaL_tolstring(L, 3, nullptr); lua_pop(L, 1); break;
            case Attribute::Vector3: v.v = LuaApi::checkVector3(L, 3); break;
            case Attribute::Color3: v.v = LuaApi::checkColor3(L, 3); break;
            default: {
                double d = luaL_checknumber(L, 3);
                v.n = n->intValue ? (double)(long long)std::llround(d) : d;
            }
        }
        LuaApi::engine(L)->fireValueChanged(n->id);
        return 0;
    }
    if (n->isDecal()) {
        if (is(k, "Texture"))      { n->texture = Online::assetRef(luaL_checkstring(L, 3)); return 0; }
        if (is(k, "Color3"))       { n->color = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "Transparency")) { n->transparency = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "Face")) {
            std::string f = luaL_checkstring(L, 3);   // Enum.NormalId.Top -> "Top"
            for (int i = 0; i < 6; ++i) if (f == kFaceNames[i]) n->face = (Face)i;
            return 0;
        }
    }
    if (n->isTool()) {
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "ToolTip"))      { n->toolTip = luaL_checkstring(L, 3); return 0; }
        if (is(k, "CanBeDropped")) { n->canBeDropped = lua_toboolean(L, 3); return 0; }
        if (is(k, "GripPos"))      { n->gripPos = LuaApi::checkVector3(L, 3); return 0; }
        if (is(k, "Grip")) {
            glm::mat4 m = LuaApi::checkCFrame(L, 3);
            n->gripRot = glm::mat3(m);
            n->gripPos = glm::vec3(m[3]);
            return 0;
        }
        if (is(k, "GripRight") || is(k, "GripUp") || is(k, "GripForward")) {
            const int c = is(k, "GripRight") ? 0 : is(k, "GripUp") ? 1 : 2;
            glm::vec3 v = LuaApi::checkVector3(L, 3);
            if (glm::length(v) > 1e-6f) n->gripRot[c] = glm::normalize(v);
            return 0;
        }
    }
    if (n->isLight()) {
        if (is(k, "Enabled"))    { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "Brightness")) { n->brightness = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Range"))      { n->range = std::max(0.1f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Angle"))      { n->spotAngle = glm::clamp((float)luaL_checknumber(L, 3), 1.0f, 179.0f); return 0; }
        if (is(k, "Color"))      { n->color = LuaApi::checkColor3(L, 3); return 0; }
    }
    if (n == scene->root()) {
        WorldSettings& w = scene->world();
        if (is(k, "Gravity"))    { w.gravity = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "DeathStyle")) { w.deathStyle = std::string(luaL_checkstring(L, 3)) == "Classic" ? DeathStyle::Classic : DeathStyle::Ragdoll; return 0; }
        if (is(k, "Knockdowns")) { w.knockdowns = lua_toboolean(L, 3); return 0; }
        if (is(k, "Gore")) {
            std::string g = luaL_checkstring(L, 3);
            w.gore = g == "Blood" ? GoreLevel::Blood : g == "Off" ? GoreLevel::Off : GoreLevel::OilAndBolts;
            return 0;
        }
        if (is(k, "FallDamage")) { w.fallDamage = lua_toboolean(L, 3); return 0; }
        if (is(k, "SafeFallSpeed"))   { w.fallDamageSpeed = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "FallDamageScale")) { w.fallDamageScale = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "PlayerCollisions")) { w.playerCollisions = lua_toboolean(L, 3); return 0; }
        if (is(k, "VoiceChatEnabled")) { w.voiceChat = lua_toboolean(L, 3); return 0; }
        if (is(k, "Orthographic"))     { w.orthographic = lua_toboolean(L, 3); return 0; }
        if (is(k, "CurrentCamera"))    return 0;   // (there's only ever the one camera)
        if (is(k, "OrthographicSize")) { w.orthographicSize = std::clamp((float)luaL_checknumber(L, 3), 0.0f, 2000.0f); return 0; }
        if (is(k, "BloodColor"))  { w.bloodColor = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "BloodAmount")) { w.bloodAmount = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 5.0f); return 0; }
        if (is(k, "MaxFluidParticles")) { w.maxFluidParticles = std::clamp((int)luaL_checkinteger(L, 3), 0, 1 << 20); return 0; }
    }
    // A property kept as an attribute (see inst_index): set it the same way.
    for (const Attribute& a : n->attributes)
        if (a.name == k) {
            lua_pushcfunction(L, m_SetAttribute);
            lua_pushvalue(L, 1);
            lua_pushvalue(L, 2);
            lua_pushvalue(L, 3);
            lua_call(L, 3, 0);
            return 0;
        }
    return luaL_error(L, "'%s' can't be set on %s \"%s\"", k, className(L, n), n->name.c_str());
}

int inst_eq(lua_State* L) {
    auto* a = static_cast<InstRef*>(luaL_testudata(L, 1, kInst));
    auto* b = static_cast<InstRef*>(luaL_testudata(L, 2, kInst));
    lua_pushboolean(L, a && b && a->id == b->id);
    return 1;
}

int inst_tostring(lua_State* L) {
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, 1, kInst));
    SceneNode* n = E(L)->resolve(ref->id);
    lua_pushstring(L, n ? n->name.c_str() : "(destroyed)");
    return 1;
}

// Instance.new("Part", parent)
int inst_new(lua_State* L) {
    std::string cls = luaL_checkstring(L, 1);
    std::unique_ptr<SceneNode> n;
    PrimitiveType shape;
    if (cls == "Model" || cls == "Folder") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Model);
    } else if (cls == "WorldModel") {   // (a Model in a ViewportFrame; ours are all the same)
        n = std::make_unique<SceneNode>(cls, NodeKind::Model);
        Attribute a;
        a.name = "RobloxClass"; a.type = Attribute::String; a.s = "WorldModel";
        n->attributes.push_back(a);
    } else if (cls == "Camera") {   // for a ViewportFrame's CurrentCamera
        n = std::make_unique<SceneNode>(cls, NodeKind::Camera);
    } else if (cls == "Script" || cls == "LocalScript" || cls == "ModuleScript") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Script);
        n->isLocal = cls == "LocalScript";
        n->isModule = cls == "ModuleScript";
    } else if (cls == "RemoteEvent" || cls == "RemoteFunction") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Remote);
        n->remoteFunction = cls == "RemoteFunction";
    } else if (cls == "ProximityPrompt") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Prompt);
    } else if (cls == "Highlight") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Highlight);
    } else if (cls == "ClickDetector") {
        n = std::make_unique<SceneNode>(cls, NodeKind::ClickDetector);
    } else if (cls == "Trail" || cls == "Beam") {
        n = std::make_unique<SceneNode>(cls, cls == "Trail" ? NodeKind::Trail : NodeKind::Beam);
    } else if (std::find(std::begin(kPostEffectClassNames), std::end(kPostEffectClassNames), cls) != std::end(kPostEffectClassNames)) {
        n = std::make_unique<SceneNode>(cls, NodeKind::PostEffect);
        for (int i = 0; i < kPostEffectCount; ++i) if (cls == kPostEffectClassNames[i]) n->post = postEffectDefaults((PostEffectType)i);
    } else if (cls == "Sky") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Sky);
    } else if (std::find(std::begin(kParticleClassNames), std::end(kParticleClassNames), cls) != std::end(kParticleClassNames)) {
        n = std::make_unique<SceneNode>(cls, NodeKind::Particles);
        for (int i = 0; i < kParticleTypeCount; ++i) if (cls == kParticleClassNames[i]) n->particle = particleDefaults((ParticleType)i);
    } else if (cls == "Attachment" || cls == "Bone") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Attachment);
        n->bone = cls == "Bone";
    } else if (auto ct = std::find(std::begin(kConstraintClassNames), std::end(kConstraintClassNames), cls);
               ct != std::end(kConstraintClassNames)) {
        n = std::make_unique<SceneNode>(cls, NodeKind::Constraint);
        n->constraintType = (ConstraintType)(ct - std::begin(kConstraintClassNames));
        n->color = n->constraintType == ConstraintType::Rope ? glm::vec3(0.45f, 0.32f, 0.2f) : glm::vec3(0.6f);
    } else if (cls == "FluidSystem") {
        n = std::make_unique<SceneNode>(cls, NodeKind::FluidSystem);
        n->color = {0.12f, 0.56f, 1.0f};
    } else if (cls == "FluidVolume") {
        // A block of water: swim in it, float things on it. Its waves, clarity and
        // current live in attributes (so Studio shows them and they're saved).
        n = std::make_unique<SceneNode>("FluidVolume");
        n->primitiveType = PrimitiveType::Cube;
        n->mesh = MeshLibrary::get(PrimitiveType::Cube);
        n->anchored = true;
        n->canCollide = false;
        n->castShadow = false;
        n->material = Material::Glass;
        n->color = {0.13f, 0.45f, 0.62f};
        n->transparency = 0.45f;
        n->transform.scale = {10.0f, 5.0f, 10.0f};
        n->transform.position = {0.0f, 2.5f, 0.0f};
        n->tags = {"Water", "FluidVolume"};
        setAttr(n.get(), "Clarity", Attribute::Number, 0.8);
        setAttr(n.get(), "WaveScale", Attribute::Number, 1.0);
        setAttr(n.get(), "Flow", Attribute::Vector3, 0.0, glm::vec3(0.0f));
        setAttr(n.get(), "Drag", Attribute::Number, 1.0);
    } else if (cls == "FluidEmitter") {
        n = std::make_unique<SceneNode>(cls, NodeKind::FluidEmitter);
        n->transform.scale = {1.0f, 1.0f, 1.0f};
    } else if (cls == "Sound") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Sound);
    } else if (cls == "SoundGroup") {
        n = std::make_unique<SceneNode>(cls, NodeKind::SoundGroup);
        n->volume = 0.5f;   // (Roblox's starting Volume for a group)
    } else if (std::find(std::begin(SoundFx::kClassNames), std::end(SoundFx::kClassNames), cls) != std::end(SoundFx::kClassNames)) {
        n = std::make_unique<SceneNode>(cls, NodeKind::SoundEffect);
        bool found = false;
        n->soundFx = soundFxDefaults(SoundFx::typeFromClass(cls.c_str(), found));
    } else if (cls == "ForceField") {
        n = std::make_unique<SceneNode>(cls, NodeKind::ForceField);
    } else if (cls == "Tool") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Tool);
    } else if (cls == "Animation") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Animation);
        n->source = Anim::emptyClipText();
    } else if (std::find(std::begin(kGuiClassNames), std::end(kGuiClassNames), cls) != std::end(kGuiClassNames)) {
        n = std::make_unique<SceneNode>(cls, NodeKind::Gui);
        for (int i = 0; i < kGuiTypeCount; ++i) if (cls == kGuiClassNames[i]) n->gui.type = (GuiType)i;
        Guis::setDefaults(*n);
    } else if (std::find(std::begin(kMoverClassNames), std::end(kMoverClassNames), cls) != std::end(kMoverClassNames)) {
        n = std::make_unique<SceneNode>(cls, NodeKind::Mover);
        for (int i = 0; i < kMoverTypeCount; ++i) if (cls == kMoverClassNames[i]) n->mover = moverDefaults((MoverType)i);
    } else if (cls == "Decal") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Decal);
        n->color = {1.0f, 1.0f, 1.0f};
    } else if (cls == "IntValue" || cls == "NumberValue" || cls == "StringValue" || cls == "BoolValue" ||
               cls == "Vector3Value" || cls == "Color3Value") {
        n = std::make_unique<SceneNode>("Value", NodeKind::Value);
        n->intValue = cls == "IntValue";
        n->value.type = cls == "StringValue" ? Attribute::String : cls == "BoolValue" ? Attribute::Bool
                      : cls == "Vector3Value" ? Attribute::Vector3 : cls == "Color3Value" ? Attribute::Color3 : Attribute::Number;
    } else if (cls == "PointLight" || cls == "SpotLight") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Light);
        n->lightType = cls == "SpotLight" ? LightType::Spot : LightType::Point;
        n->color = {1.0f, 1.0f, 1.0f};
    } else if (parseShape(cls, shape) || cls == "SpawnLocation" || cls == "WedgePart" || cls == "TrussPart") {
        if (cls == "SpawnLocation" || cls == "WedgePart" || cls == "TrussPart") shape = PrimitiveType::Cube;
        n = std::make_unique<SceneNode>(cls == "Block" || cls == "Cube" ? "Part" : cls);
        n->primitiveType = shape;
        n->mesh     = MeshLibrary::get(shape);
        n->anchored = false;                    // new parts fall, like in Roblox
        n->color    = {0.64f, 0.64f, 0.66f};
        n->transform.position = {0.0f, 0.5f, 0.0f};
        if (cls == "SpawnLocation") n->tags.push_back("SpawnLocation");   // (still one when renamed)
    } else {
        return luaL_error(L, "Instance.new: unknown class '%s' (try \"Part\", \"Ball\", "
                             "\"Cylinder\", \"Model\", \"Folder\" or \"PointLight\")", cls.c_str());
    }
    SceneNode* raw = E(L)->adopt(std::move(n));
    if (!lua_isnoneornil(L, 2)) {
        std::string err;
        if (!E(L)->setParent(raw, LuaApi::checkNode(L, 2), err)) return luaL_error(L, "%s", err.c_str());
    }
    LuaApi::pushInstance(L, raw->id);
    return 1;
}

// ===========================================================================
// Signals & connections
// ===========================================================================

int sig_connect_impl(lua_State* L, bool once) {
    auto* s = static_cast<SignalRef*>(luaL_checkudata(L, 1, kSig));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
    int idx = E(L)->connect(s->kind, s->id, fnRef, once);
    auto* c = static_cast<ConnRef*>(lua_newuserdatauv(L, sizeof(ConnRef), 0));
    c->index = idx;
    luaL_setmetatable(L, kConn);
    return 1;
}
int sig_connect(lua_State* L) { return sig_connect_impl(L, false); }
int sig_once   (lua_State* L) { return sig_connect_impl(L, true); }

int sig_wait(lua_State* L) {
    auto* s = static_cast<SignalRef*>(luaL_checkudata(L, 1, kSig));
    if (!lua_isyieldable(L)) return luaL_error(L, ":Wait() can't be used here");
    E(L)->connectWaiter(s->kind, s->id, L);
    lua_settop(L, 0);
    return lua_yield(L, 0);
}

int sig_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "Connect") || is(k, "connect")) { lua_pushcfunction(L, sig_connect); return 1; }
    if (is(k, "Once"))                        { lua_pushcfunction(L, sig_once);    return 1; }
    if (is(k, "Wait"))                        { lua_pushcfunction(L, sig_wait);    return 1; }
    return luaL_error(L, "'%s' is not a valid member of an event (try :Connect)", k);
}

int conn_disconnect(lua_State* L) {
    auto* c = static_cast<ConnRef*>(luaL_checkudata(L, 1, kConn));
    E(L)->disconnect(c->index);
    return 0;
}
int conn_index(lua_State* L) {
    auto* c = static_cast<ConnRef*>(luaL_checkudata(L, 1, kConn));
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "Disconnect") || is(k, "disconnect")) { lua_pushcfunction(L, conn_disconnect); return 1; }
    if (is(k, "Connected")) { lua_pushboolean(L, E(L)->connected(c->index)); return 1; }
    return luaL_error(L, "'%s' is not a valid member of a connection", k);
}


// ===========================================================================
// Animations: Animator (humanoid.Animator / humanoid:LoadAnimation) and
// AnimationTrack (what LoadAnimation gives: :Play(), :Stop(), .Stopped...)
// ===========================================================================

struct TrackRef    { int id; };
struct AnimatorRef { uint64_t rig; };

Anim::Animator& animator(lua_State* L) { return E(L)->scene()->animator(); }

void pushTrack(lua_State* L, int id) {
    auto* t = static_cast<TrackRef*>(lua_newuserdatauv(L, sizeof(TrackRef), 0));
    t->id = id;
    luaL_setmetatable(L, kTrack);
}

void pushAnimator(lua_State* L, uint64_t rig) {
    auto* a = static_cast<AnimatorRef*>(lua_newuserdatauv(L, sizeof(AnimatorRef), 0));
    a->rig = rig;
    luaL_setmetatable(L, kAnimr);
}

// Load `animation` (the object at idx) onto a rig and push the track.
int loadOnto(lua_State* L, uint64_t rig, int idx) {
    SceneNode* a = LuaApi::checkNode(L, idx);
    if (!a->isAnimation()) return luaL_error(L, "LoadAnimation needs an Animation object (got %s \"%s\")",
                                             className(L, a), a->name.c_str());
    SceneNode* r = E(L)->scene()->findById(rig);
    if (!r) return luaL_error(L, "LoadAnimation: this character or model isn't in the game");
    pushTrack(L, animator(L).load(rig, *a));
    return 1;
}

int pushPlaying(lua_State* L, uint64_t rig) {
    lua_newtable(L);
    int i = 1;
    for (int id : animator(L).playingOn(rig)) { pushTrack(L, id); lua_rawseti(L, -2, i++); }
    return 1;
}

int animr_load(lua_State* L) {
    auto* a = static_cast<AnimatorRef*>(luaL_checkudata(L, 1, kAnimr));
    return loadOnto(L, a->rig, 2);
}
int animr_playing(lua_State* L) {
    auto* a = static_cast<AnimatorRef*>(luaL_checkudata(L, 1, kAnimr));
    return pushPlaying(L, a->rig);
}
int animr_isA(lua_State* L) {
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, is(c, "Animator") || is(c, "Instance"));
    return 1;
}
int animr_index(lua_State* L) {
    auto* a = static_cast<AnimatorRef*>(luaL_checkudata(L, 1, kAnimr));
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "LoadAnimation"))             { lua_pushcfunction(L, animr_load); return 1; }
    if (is(k, "GetPlayingAnimationTracks")) { lua_pushcfunction(L, animr_playing); return 1; }
    if (is(k, "IsA"))                       { lua_pushcfunction(L, animr_isA); return 1; }
    if (is(k, "Name") || is(k, "ClassName")) { lua_pushstring(L, "Animator"); return 1; }
    if (is(k, "Parent")) {
        if (E(L)->scene()->humanoidOf(a->rig)) LuaApi::pushHumanoid(L, a->rig);
        else LuaApi::pushInstance(L, a->rig);
        return 1;
    }
    return luaL_error(L, "'%s' is not a valid member of Animator", k);
}

Anim::Animator::Track& track(lua_State* L) {
    auto* t = static_cast<TrackRef*>(luaL_checkudata(L, 1, kTrack));
    Anim::Animator::Track* tr = animator(L).track(t->id);
    if (!tr) luaL_error(L, "This AnimationTrack no longer exists (the game restarted)");
    return *tr;
}
int trackId(lua_State* L) { return static_cast<TrackRef*>(luaL_checkudata(L, 1, kTrack))->id; }

// track:Play(fadeTime = 0.1, weight = 1, speed = 1)
int track_play(lua_State* L) {
    track(L);
    animator(L).play(trackId(L), (float)luaL_optnumber(L, 2, 0.1), (float)luaL_optnumber(L, 3, 1.0),
                     (float)luaL_optnumber(L, 4, 1.0));
    return 0;
}
int track_stop(lua_State* L) {
    track(L);
    animator(L).stop(trackId(L), (float)luaL_optnumber(L, 2, 0.1));
    return 0;
}
int track_adjustSpeed(lua_State* L) {
    track(L);
    animator(L).adjustSpeed(trackId(L), (float)luaL_optnumber(L, 2, 1.0));
    return 0;
}
int track_adjustWeight(lua_State* L) {
    track(L);
    animator(L).adjustWeight(trackId(L), (float)luaL_optnumber(L, 2, 1.0), (float)luaL_optnumber(L, 3, 0.1));
    return 0;
}
int track_timeOfKeyframe(lua_State* L) {
    Anim::Animator::Track& t = track(L);
    std::string name = luaL_checkstring(L, 2);
    for (const Anim::Keyframe& k : t.clip.keys)
        if (k.name == name) { lua_pushnumber(L, k.time); return 1; }
    return luaL_error(L, "GetTimeOfKeyframe: no keyframe called \"%s\"", name.c_str());
}
int track_markerSignal(lua_State* L) {   // (all named keyframes; the name is passed to the callback)
    LuaApi::pushSignal(L, SignalKind::KeyframeReached, (uint64_t)trackId(L));
    return 1;
}

bool parsePriority(const char* s, Anim::Priority& out) {
    for (int i = 0; i < 4; ++i) if (is(s, Anim::kPriorityNames[i])) { out = (Anim::Priority)i; return true; }
    return false;
}

int track_index(lua_State* L) {
    Anim::Animator::Track& t = track(L);
    uint64_t id = (uint64_t)trackId(L);
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "Play"))              { lua_pushcfunction(L, track_play); return 1; }
    if (is(k, "Stop"))              { lua_pushcfunction(L, track_stop); return 1; }
    if (is(k, "AdjustSpeed"))       { lua_pushcfunction(L, track_adjustSpeed); return 1; }
    if (is(k, "AdjustWeight"))      { lua_pushcfunction(L, track_adjustWeight); return 1; }
    if (is(k, "GetTimeOfKeyframe")) { lua_pushcfunction(L, track_timeOfKeyframe); return 1; }
    if (is(k, "GetMarkerReachedSignal")) { lua_pushcfunction(L, track_markerSignal); return 1; }
    if (is(k, "IsPlaying"))    { lua_pushboolean(L, t.playing && t.target > 0.0f); return 1; }
    if (is(k, "Length"))       { lua_pushnumber(L, t.clip.length()); return 1; }
    if (is(k, "Looped"))       { lua_pushboolean(L, t.looped); return 1; }
    if (is(k, "Speed"))        { lua_pushnumber(L, t.speed); return 1; }
    if (is(k, "TimePosition")) { lua_pushnumber(L, t.time); return 1; }
    if (is(k, "WeightCurrent")) { lua_pushnumber(L, t.weight); return 1; }
    if (is(k, "WeightTarget")) { lua_pushnumber(L, t.target); return 1; }
    if (is(k, "Priority"))     { lua_pushstring(L, Anim::kPriorityNames[(int)t.priority]); return 1; }
    if (is(k, "Name"))         { lua_pushstring(L, t.name.c_str()); return 1; }
    if (is(k, "ClassName"))    { lua_pushstring(L, "AnimationTrack"); return 1; }
    if (is(k, "Animation"))    { LuaApi::pushInstance(L, E(L)->resolve(t.animation) ? t.animation : 0); return 1; }
    if (is(k, "Stopped"))      { LuaApi::pushSignal(L, SignalKind::AnimStopped, id); return 1; }
    if (is(k, "Ended"))        { LuaApi::pushSignal(L, SignalKind::AnimEnded, id); return 1; }
    if (is(k, "DidLoop"))      { LuaApi::pushSignal(L, SignalKind::AnimDidLoop, id); return 1; }
    if (is(k, "KeyframeReached")) { LuaApi::pushSignal(L, SignalKind::KeyframeReached, id); return 1; }
    return luaL_error(L, "'%s' is not a valid member of AnimationTrack", k);
}

int track_newindex(lua_State* L) {
    Anim::Animator::Track& t = track(L);
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "Looped"))            t.looped = lua_toboolean(L, 3);
    else if (is(k, "TimePosition")) t.time = std::clamp((float)luaL_checknumber(L, 3), 0.0f, t.clip.length());
    else if (is(k, "Priority")) {
        if (!parsePriority(luaL_checkstring(L, 3), t.priority))
            return luaL_error(L, "Priority must be Core, Idle, Movement or Action");
    }
    else return luaL_error(L, "'%s' can't be set on AnimationTrack", k);
    return 0;
}

int track_eq(lua_State* L) {
    auto* a = static_cast<TrackRef*>(luaL_testudata(L, 1, kTrack));
    auto* b = static_cast<TrackRef*>(luaL_testudata(L, 2, kTrack));
    lua_pushboolean(L, a && b && a->id == b->id);
    return 1;
}

// model:LoadAnimation(anim) — any rig (a Model of parts), not just characters.
int m_LoadAnimation(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (n->kind != NodeKind::Model) return luaL_error(L, "LoadAnimation works on a Model (a rig) or a Humanoid");
    return loadOnto(L, n->id, 2);
}

// ===========================================================================
// Humanoid (the character's health / speed)
// ===========================================================================

struct HumRef { uint64_t rootId; };

uint64_t humRoot(lua_State* L) {
    return static_cast<HumRef*>(luaL_checkudata(L, 1, kHum))->rootId;
}

Humanoid& hum(lua_State* L) {
    Humanoid* h = E(L)->scene()->humanoidOf(humRoot(L));
    if (!h) luaL_error(L, "This character has left the game");
    return *h;
}

void touched(lua_State* L) { E(L)->scene()->markHumanoidEdited(humRoot(L), E(L)->time()); }

int hum_takeDamage(lua_State* L) {
    Humanoid& h = hum(L);
    // Like Roblox: TakeDamage does nothing while the character has a ForceField.
    if (SceneNode* r = E(L)->scene()->findById(humRoot(L)); r && r->hasForceField()) return 0;
    h.health = glm::clamp(h.health - (float)luaL_checknumber(L, 2), 0.0f, h.maxHealth);
    touched(L);
    return 0;
}

int hum_breakJoints(lua_State* L) {
    E(L)->scene()->killCharacter(humRoot(L), 1.0f, glm::vec3(0, 4, 0));
    return 0;
}

// humanoid:EquipTool(tool) / humanoid:UnequipTools(): yours, or (hosting) someone who joined.
Player* toolPlayer(lua_State* L) {
    Player* p = E(L)->scene()->player();
    return p && p->rootId() == humRoot(L) ? p : nullptr;
}
void joinerEquip(lua_State* L, uint64_t toolId) {
    Scene& scene = *E(L)->scene();
    RemoteCharacter* rc = scene.findRemote(humRoot(L));
    SceneNode* r = rc && rc->alive ? scene.findById(rc->rootId) : nullptr;
    if (!r) return;
    SceneNode* bag = Player::backpackOf(scene, r);
    if (SceneNode* t = toolId ? scene.findById(toolId) : nullptr; t && t->isTool() && bag && t->parent != bag) {
        auto owned = scene.detach(t);   // (one lying about: into their backpack first, like Roblox)
        if (!owned) return;
        owned->transform = Transform{};
        bag->addChild(std::move(owned));
    }
    ScriptEngine* e = E(L);
    Player::equipOn(scene, r, toolId, [e](uint64_t id, bool on) { e->fireTool(on ? SignalKind::Equipped : SignalKind::Unequipped, id); });
}
int hum_equipTool(lua_State* L) {
    SceneNode* tool = LuaApi::checkNode(L, 2);
    if (!tool->isTool()) return 0;
    if (Player* p = toolPlayer(L)) p->equip(tool->id);
    else joinerEquip(L, tool->id);
    return 0;
}
int hum_unequipTools(lua_State* L) {
    if (Player* p = toolPlayer(L)) p->equip(0);
    else joinerEquip(L, 0);
    return 0;
}

int hum_loadAnimation(lua_State* L) { return loadOnto(L, humRoot(L), 2); }
int hum_playing(lua_State* L) { return pushPlaying(L, humRoot(L)); }
// humanoid:WaitForChild("Animator") / FindFirstChildOfClass("Animator"), like Roblox code does.
int hum_findChild(lua_State* L) {
    const char* n = luaL_checkstring(L, 2);
    if (is(n, "Animator")) pushAnimator(L, humRoot(L));
    else lua_pushnil(L);
    return 1;
}

int hum_isA(lua_State* L) {
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, is(c, "Humanoid") || is(c, "Instance"));
    return 1;
}

// humanoid:GetState() -> "Climbing", "Swimming", "Freefall", "Running", "Dead" (Enum.HumanoidStateType names)
int hum_getState(lua_State* L) {
    Player* p = E(L)->scene()->player();
    const bool mine = p && p->rootId() == humRoot(L);
    const char* st = "Running";
    if (hum(L).health <= 0.0f) st = "Dead";
    else if (mine) st = p->stateName();
    else if (mine && p->climbing()) st = "Climbing";
    else if (mine && p->swimming()) st = "Swimming";
    else if (mine && !p->grounded()) st = "Freefall";
    else if (Npc* n = E(L)->scene()->npcs().find(humRoot(L)); n && !n->grounded) st = "Freefall";
    lua_pushstring(L, st);
    return 1;
}

// humanoid:ChangeState(Enum.HumanoidStateType.FallingDown): "FallingDown" / "Ragdoll" knock
// the character over for a couple of seconds, "GettingUp" stands it back up, "Dead" kills it.
int hum_changeState(lua_State* L) {
    const char* st = luaL_checkstring(L, 2);
    Player* p = E(L)->scene()->player();
    const bool mine = p && p->rootId() == humRoot(L);
    if (is(st, "Dead")) { hum(L).health = 0.0f; return 0; }
    if (!mine) return 0;
    if (is(st, "FallingDown") || is(st, "Ragdoll") || is(st, "Physics")) p->trip(2.0f);
    else if (is(st, "GettingUp") || is(st, "Running")) { p->trip(0.0f); hum(L).platformStand = false; }
    else if (is(st, "PlatformStanding")) hum(L).platformStand = true;
    return 0;
}
// humanoid:PlayEmote("dance") -> true if it plays: the built-in emotes (dance, dance2, dance3,
// laugh, cheer, wave, point) and the ones the player equipped from the catalog, by name.
// Only on the player's own computer (a LocalScript, or playing solo); everyone sees it.
int hum_playEmote(lua_State* L) {
    Player* p = E(L)->scene()->player();
    const bool mine = p && p->rootId() == humRoot(L);
    lua_pushboolean(L, mine && p->playEmote(luaL_checkstring(L, 2)));
    return 1;
}

// StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false) turns the emote menu
// (B, and its button on phones) off for this player; GetCoreGuiEnabled reads it back.
SceneNode* checkStarterGui(lua_State* L, const char* method) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (n->name != "StarterGui") luaL_error(L, "%s is a StarterGui method: game:GetService(\"StarterGui\"):%s(...)", method, method);
    return n;
}
int m_SetCoreGuiEnabled(lua_State* L) {
    checkStarterGui(L, "SetCoreGuiEnabled");
    const std::string type = luaL_checkstring(L, 2);
    if (!CoreGuiState::known(type))
        return luaL_error(L, "'%s' is not a CoreGuiType (EmotesMenu, PlayerList, Health, Backpack, Chat, SelfView, Captures or All)", type.c_str());
    luaL_checkany(L, 3);
    E(L)->scene()->coreGui().set(type, lua_toboolean(L, 3));
    return 0;
}
int m_GetCoreGuiEnabled(lua_State* L) {
    checkStarterGui(L, "GetCoreGuiEnabled");
    const std::string type = luaL_checkstring(L, 2);
    if (!CoreGuiState::known(type)) return luaL_error(L, "'%s' is not a CoreGuiType", type.c_str());
    lua_pushboolean(L, E(L)->scene()->coreGui().enabled(type));
    return 1;
}

// NPCs: humanoid:MoveTo(point [, part]) walks there (MoveToFinished fires when it
// gets there, or gives up after 8 seconds); humanoid:Move(direction) keeps walking that way.
Npc* npcOf(lua_State* L) { return E(L)->scene()->npcs().find(*E(L)->scene(), humRoot(L)); }
int hum_moveTo(lua_State* L) {
    glm::vec3 p = LuaApi::checkVector3(L, 2);
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) p = worldPosition(LuaApi::checkNode(L, 3));   // follow a part
    if (Npc* n = npcOf(L)) {
        n->target = p; n->hasTarget = true; n->targetTime = 0.0f; n->moveDir = glm::vec3(0.0f);
        n->route.state = Npc::Route::Idle;   // a plain MoveTo takes over from PathfindTo
    }
    return 0;
}
// humanoid:PathfindStart(target [, params]): start walking a navmesh route (PathfindTo waits for it).
int hum_pathfindStart(lua_State* L) {
    Npc* n = npcOf(L);
    if (!n) { lua_pushboolean(L, false); return 1; }
    glm::vec3 goal(0.0f);
    uint64_t follow = 0;
    if (glm::vec3* v = LuaApi::toVector3(L, 2)) goal = *v;
    else follow = LuaApi::checkNode(L, 2)->id;
    NpcSystem::startRoute(*n, goal, follow, LuaApi::checkAgent(L, 3));
    lua_pushboolean(L, true);
    return 1;
}
int hum_stopPathfinding(lua_State* L) {
    if (Npc* n = npcOf(L)) { n->route.state = Npc::Route::Idle; n->route.waypoints.clear(); }
    return 0;
}
int hum_move(lua_State* L) {
    glm::vec3 d = LuaApi::checkVector3(L, 2);
    if (Npc* n = npcOf(L)) { n->moveDir = d; n->hasTarget = false; n->route.state = Npc::Route::Idle; }
    else if (Player* p = E(L)->scene()->player(); p && p->rootId() == humRoot(L)) p->scriptMove(d);   // this computer's player
    return 0;
}

int hum_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Humanoid& h = hum(L);
    if (is(k, "Health"))     { lua_pushnumber(L, h.health);     return 1; }
    if (is(k, "MaxHealth"))  { lua_pushnumber(L, h.maxHealth);  return 1; }
    if (is(k, "WalkSpeed"))  { lua_pushnumber(L, h.walkSpeed);  return 1; }
    if (is(k, "JumpPower"))  { lua_pushnumber(L, h.jumpPower);  return 1; }
    if (is(k, "JumpHeight")) { lua_pushnumber(L, h.jumpHeight); return 1; }
    if (is(k, "Sit") || is(k, "SeatPart")) {   // sitting on a Seat (our own character)
        Player* p = E(L)->scene()->player();
        const bool mine = p && p->rootId() == humRoot(L);
        if (is(k, "Sit")) lua_pushboolean(L, mine && p->sitting());
        else if (mine && p->seatId()) LuaApi::pushInstance(L, p->seatId());
        else lua_pushnil(L);
        return 1;
    }
    if (is(k, "UseJumpPower")) { lua_pushboolean(L, h.useJumpPower); return 1; }
    if (is(k, "AutoRotate")) { lua_pushboolean(L, h.autoRotate); return 1; }
    if (is(k, "PlatformStand")) { lua_pushboolean(L, h.platformStand); return 1; }
    if (is(k, "ChangeState")) { lua_pushcfunction(L, hum_changeState); return 1; }
    if (is(k, "PlayEmote"))  { lua_pushcfunction(L, hum_playEmote); return 1; }
    if (is(k, "Name") || is(k, "ClassName")) { lua_pushstring(L, "Humanoid"); return 1; }
    if (is(k, "Parent"))     { LuaApi::pushInstance(L, humRoot(L)); return 1; }
    if (is(k, "Died"))       { LuaApi::pushSignal(L, SignalKind::Died, humRoot(L)); return 1; }
    if (is(k, "TakeDamage")) { lua_pushcfunction(L, hum_takeDamage); return 1; }
    if (is(k, "BreakJoints")) { lua_pushcfunction(L, hum_breakJoints); return 1; }
    if (is(k, "EquipTool"))   { lua_pushcfunction(L, hum_equipTool); return 1; }
    if (is(k, "UnequipTools")) { lua_pushcfunction(L, hum_unequipTools); return 1; }
    if (is(k, "IsA"))        { lua_pushcfunction(L, hum_isA); return 1; }
    if (is(k, "LoadAnimation")) { lua_pushcfunction(L, hum_loadAnimation); return 1; }
    if (is(k, "GetState"))   { lua_pushcfunction(L, hum_getState); return 1; }
    if (is(k, "MoveTo"))     { lua_pushcfunction(L, hum_moveTo); return 1; }
    if (is(k, "PathfindTo")) { lua_getglobal(L, "__gb_pathfindTo"); return 1; }
    if (is(k, "PathfindStart")) { lua_pushcfunction(L, hum_pathfindStart); return 1; }
    if (is(k, "StopPathfinding")) { lua_pushcfunction(L, hum_stopPathfinding); return 1; }
    if (is(k, "PathfindStatus")) {   // "Idle", "Walking", "Arrived" or "Failed"
        Npc* n = npcOf(L);
        static const char* names[] = {"Idle", "Walking", "Arrived", "Failed"};
        lua_pushstring(L, n ? names[n->route.state] : "Idle");
        return 1;
    }
    if (is(k, "Move"))       { lua_pushcfunction(L, hum_move); return 1; }
    if (is(k, "MoveToFinished")) { LuaApi::pushSignal(L, SignalKind::MoveToFinished, humRoot(L)); return 1; }
    if (is(k, "Jump"))       { Npc* n = npcOf(L); lua_pushboolean(L, n && n->jump); return 1; }
    if (is(k, "MoveDirection")) { Npc* n = npcOf(L); LuaApi::pushVector3(L, n ? n->moveDir : glm::vec3(0.0f)); return 1; }
    if (is(k, "WalkToPoint")) { Npc* n = npcOf(L); LuaApi::pushVector3(L, n && n->hasTarget ? n->target : glm::vec3(0.0f)); return 1; }
    if (is(k, "RootPart")) {
        SceneNode* r = E(L)->scene()->findById(humRoot(L));
        SceneNode* hrp = r ? r->findChild("HumanoidRootPart") : nullptr;
        LuaApi::pushInstance(L, hrp ? hrp->id : 0);
        return 1;
    }
    if (is(k, "GetPlayingAnimationTracks")) { lua_pushcfunction(L, hum_playing); return 1; }
    if (is(k, "Animator"))   { pushAnimator(L, humRoot(L)); return 1; }
    if (is(k, "FindFirstChild") || is(k, "FindFirstChildOfClass") || is(k, "WaitForChild"))
        { lua_pushcfunction(L, hum_findChild); return 1; }
    return luaL_error(L, "'%s' is not a valid member of Humanoid", k);
}

int hum_newindex(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Humanoid& h = hum(L);
    if (is(k, "Health"))     h.health = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, h.maxHealth);
    else if (is(k, "MaxHealth")) { h.maxHealth = std::max(1.0f, (float)luaL_checknumber(L, 3));
                                   h.health = std::min(h.health, h.maxHealth); }
    else if (is(k, "WalkSpeed"))  h.walkSpeed = std::max(0.0f, (float)luaL_checknumber(L, 3));
    else if (is(k, "JumpPower"))  { h.jumpPower = std::max(0.0f, (float)luaL_checknumber(L, 3)); h.useJumpPower = true; }
    else if (is(k, "JumpHeight")) { h.jumpHeight = std::max(0.0f, (float)luaL_checknumber(L, 3)); h.useJumpPower = false; }
    else if (is(k, "UseJumpPower")) h.useJumpPower = lua_toboolean(L, 3);
    else if (is(k, "Sit")) {   // true: sit down right here (no seat needed); false: get up
        Player* p = E(L)->scene()->player();
        if (p && p->rootId() == humRoot(L)) {
            if (lua_toboolean(L, 3)) p->sitDown();
            else p->standUp();
        }
    }
    else if (is(k, "AutoRotate")) h.autoRotate = lua_toboolean(L, 3);
    else if (is(k, "PlatformStand")) h.platformStand = lua_toboolean(L, 3);
    else if (is(k, "Jump")) {
        if (Npc* n = npcOf(L)) n->jump = lua_toboolean(L, 3);
        else if (Player* p = E(L)->scene()->player(); p && p->rootId() == humRoot(L) && lua_toboolean(L, 3)) p->scriptJump();
        return 0;
    }
    else return luaL_error(L, "'%s' can't be set on Humanoid", k);
    touched(L);
    return 0;
}

int hum_eq(lua_State* L) {
    auto* a = static_cast<HumRef*>(luaL_testudata(L, 1, kHum));
    auto* b = static_cast<HumRef*>(luaL_testudata(L, 2, kHum));
    lua_pushboolean(L, a && b && a->rootId == b->rootId);
    return 1;
}

// ===========================================================================
// Camera (workspace.CurrentCamera)
// ===========================================================================
//
// One per computer, like Roblox's: the play camera (game/PlayCamera.cpp) follows
// what scripts set here, and writes back where it really is every frame.

ScriptCamera& cam(lua_State* L) { return E(L)->scene()->camera(); }

// Enum.CameraType.Scriptable -> "Scriptable" (also takes "Enum.CameraType.Scriptable").
std::string enumName(lua_State* L, int idx) {
    std::string s = luaL_checkstring(L, idx);
    const size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(dot + 1);
}

// The camera's view and lens, for turning points into screen pixels and back.
struct Lens {
    glm::mat4 world;   // camera -> world (no scale)
    glm::vec2 size;    // pixels
    float     tanHalf, aspect, orthoHalf;
};
Lens lensOf(const ScriptCamera& c) {
    Lens l;
    l.world = c.cframe;
    for (int i = 0; i < 3; ++i) {
        glm::vec3 a(l.world[i]);
        l.world[i] = glm::vec4(glm::length(a) > 1e-6f ? glm::normalize(a) : glm::vec3(i == 0, i == 1, i == 2), 0.0f);
    }
    l.size = glm::max(c.viewport, glm::vec2(1.0f));
    l.tanHalf = std::tan(glm::radians(c.fov) * 0.5f);
    l.aspect = l.size.x / l.size.y;
    l.orthoHalf = c.orthoHeight * 0.5f;
    return l;
}

// camera:WorldToViewportPoint(pos) -> Vector3(x, y pixels, depth), onScreen
int cam_worldToViewport(lua_State* L) {
    const Lens l = lensOf(cam(L));
    const glm::vec3 p = glm::vec3(glm::inverse(l.world) * glm::vec4(LuaApi::checkVector3(L, 2), 1.0f));
    const float depth = -p.z;
    glm::vec2 ndc;
    if (l.orthoHalf > 0.0f) ndc = glm::vec2(p.x / (l.orthoHalf * l.aspect), p.y / l.orthoHalf);
    else {
        const float d = std::abs(depth) > 1e-6f ? std::abs(depth) : 1e-6f;   // (behind: mirrored, like Roblox)
        ndc = glm::vec2(p.x / (d * l.tanHalf * l.aspect), p.y / (d * l.tanHalf));
    }
    const glm::vec2 px((ndc.x * 0.5f + 0.5f) * l.size.x, (0.5f - ndc.y * 0.5f) * l.size.y);
    LuaApi::pushVector3(L, glm::vec3(px, depth));
    lua_pushboolean(L, depth > 0.0f && px.x >= 0.0f && px.y >= 0.0f && px.x <= l.size.x && px.y <= l.size.y);
    return 2;
}

// camera:ViewportPointToRay(x, y [, depth]) -> a unit Ray from the camera through that pixel
int cam_viewportToRay(lua_State* L) {
    const Lens l = lensOf(cam(L));
    const float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    const float depth = (float)luaL_optnumber(L, 4, 0.0);
    const glm::vec2 ndc(x / l.size.x * 2.0f - 1.0f, 1.0f - y / l.size.y * 2.0f);
    glm::vec3 origin, dir;
    if (l.orthoHalf > 0.0f) {
        origin = glm::vec3(ndc.x * l.orthoHalf * l.aspect, ndc.y * l.orthoHalf, 0.0f);
        dir = glm::vec3(0, 0, -1);
    } else {
        origin = glm::vec3(0.0f);
        dir = glm::normalize(glm::vec3(ndc.x * l.tanHalf * l.aspect, ndc.y * l.tanHalf, -1.0f));
    }
    origin = glm::vec3(l.world * glm::vec4(origin, 1.0f));
    dir = glm::vec3(l.world * glm::vec4(dir, 0.0f));
    origin += dir * depth;
    lua_getglobal(L, "Ray");
    lua_getfield(L, -1, "new");
    LuaApi::pushVector3(L, origin);
    LuaApi::pushVector3(L, dir);
    lua_call(L, 2, 1);
    return 1;
}

// Lighting and the Camera hold things like any object (screen effects, the Sky): the
// child-finding methods and `.ChildName` work on them through the object that holds
// their children (methods get Lighting / the Camera as self; checkNode turns that into it).
bool proxyIndex(lua_State* L, SceneNode* holder, const char* k) {
    static const char* const kAllowed[] = {"FindFirstChild", "FindFirstChildOfClass", "FindFirstChildWhichIsA", "WaitForChild",
                                           "GetChildren", "GetDescendants", "ClearAllChildren", "IsDescendantOf",
                                           "GetAttribute", "GetAttributes", "SetAttribute"};
    for (const char* m : kAllowed) {
        if (!is(k, m)) continue;
        lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
        if (lua_getfield(L, -1, k) != LUA_TNIL) { lua_remove(L, -2); return true; }
        lua_pop(L, 2);
        return false;
    }
    if (holder)
        if (SceneNode* c = holder->findChild(k)) { LuaApi::pushInstance(L, c->id); return true; }
    return false;
}

int cam_isA(lua_State* L) {
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, is(c, "Camera") || is(c, "Instance"));
    return 1;
}

int cam_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    const ScriptCamera& c = cam(L);
    if (is(k, "CFrame"))      { LuaApi::pushCFrame(L, c.cframe); return 1; }
    if (is(k, "Focus"))       { LuaApi::pushCFrame(L, glm::translate(glm::mat4(1.0f), c.focus)); return 1; }
    if (is(k, "FieldOfView")) { lua_pushnumber(L, c.fov); return 1; }
    if (is(k, "CameraType"))  { lua_pushstring(L, c.typeName.c_str()); return 1; }
    if (is(k, "CameraSubject")) {
        if (c.subject) {
            if (c.subjectIsHumanoid) LuaApi::pushHumanoid(L, c.subject);
            else LuaApi::pushInstance(L, c.subject);
        } else if (Player* p = E(L)->scene()->player(); p && !E(L)->noLocalPlayer() && p->rootId()) {
            LuaApi::pushHumanoid(L, p->rootId());   // your own character's Humanoid
        } else {
            lua_pushnil(L);
        }
        return 1;
    }
    if (is(k, "ViewportSize")) { LuaApi::pushVector2(L, c.viewport); return 1; }
    if (is(k, "NearPlaneZ"))   { lua_pushnumber(L, -0.1); return 1; }
    if (is(k, "Name") || is(k, "ClassName")) { lua_pushstring(L, "Camera"); return 1; }
    if (is(k, "Parent")) { LuaApi::pushInstance(L, E(L)->scene()->root()->id); return 1; }
    if (is(k, "IsA")) { lua_pushcfunction(L, cam_isA); return 1; }
    if (is(k, "WorldToViewportPoint") || is(k, "WorldToScreenPoint")) { lua_pushcfunction(L, cam_worldToViewport); return 1; }
    if (is(k, "ViewportPointToRay") || is(k, "ScreenPointToRay")) { lua_pushcfunction(L, cam_viewportToRay); return 1; }
    // Screen effects put in the camera: camera:GetChildren(), camera.Blur...
    if (proxyIndex(L, E(L)->scene()->cameraNode(), k)) return 1;
    return luaL_error(L, "'%s' is not a valid member of Camera", k);
}

int cam_newindex(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    ScriptCamera& c = cam(L);
    if (is(k, "CFrame")) {
        c.cframe = LuaApi::checkCFrame(L, 3);
        c.cframeSet = true;   // (the play camera takes it on its next frame)
        if (c.type == CameraType::Fixed) c.fixedAt = glm::vec3(c.cframe[3]);
        return 0;
    }
    if (is(k, "Focus")) {
        if (glm::vec3* v = LuaApi::toVector3(L, 3)) c.focus = *v;
        else c.focus = glm::vec3(LuaApi::checkCFrame(L, 3)[3]);
        return 0;
    }
    if (is(k, "FieldOfView")) { c.fov = std::clamp((float)luaL_checknumber(L, 3), 1.0f, 120.0f); return 0; }
    if (is(k, "CameraType")) {
        const std::string t = enumName(L, 3);
        CameraType type;
        if (t == "Scriptable") type = CameraType::Scriptable;
        else if (t == "Fixed") type = CameraType::Fixed;
        else if (t == "Custom" || t == "Follow" || t == "Track" || t == "Watch" || t == "Attach" || t == "Orbital") type = CameraType::Custom;
        else return luaL_error(L, "'%s' is not a CameraType (use Custom, Scriptable or Fixed)", t.c_str());
        if (type == CameraType::Fixed && c.type != CameraType::Fixed) c.fixedAt = glm::vec3(c.cframe[3]);   // stays right here
        c.type = type;
        c.typeName = t;
        return 0;
    }
    if (is(k, "CameraSubject")) {
        Player* p = E(L)->scene()->player();
        const uint64_t mine = p && !E(L)->noLocalPlayer() ? p->rootId() : 0;
        if (lua_isnil(L, 3)) { c.subject = 0; c.subjectIsHumanoid = true; return 0; }
        if (auto* h = static_cast<HumRef*>(luaL_testudata(L, 3, kHum))) {
            c.subject = h->rootId == mine ? 0 : h->rootId;
            c.subjectIsHumanoid = true;
            return 0;
        }
        SceneNode* n = LuaApi::checkNode(L, 3);
        if (hasHumanoid(L, n)) {   // a character model: follow its head, like its Humanoid
            c.subject = n->id == mine ? 0 : n->id;
            c.subjectIsHumanoid = true;
        } else {
            c.subject = n->id;
            c.subjectIsHumanoid = false;
        }
        return 0;
    }
    if (is(k, "Name") || is(k, "Parent")) return 0;   // (there's only ever the one camera)
    return luaL_error(L, "'%s' can't be set on Camera", k);
}

// ===========================================================================
// Lighting
// ===========================================================================

int light_isA(lua_State* L) {
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, is(c, "Lighting") || is(c, "Instance"));
    return 1;
}

int light_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Environment& e = E(L)->scene()->environment();
    if (is(k, "ClockTime"))     { lua_pushnumber(L, E(L)->clockTime()); return 1; }
    if (is(k, "Brightness"))    { lua_pushnumber(L, e.sunIntensity);    return 1; }
    if (is(k, "GlobalShadows")) { lua_pushboolean(L, e.shadows);        return 1; }
    if (is(k, "FogEnabled"))    { lua_pushboolean(L, e.fogEnabled);     return 1; }
    if (is(k, "FogDensity"))    { lua_pushnumber(L, e.fogDensity);      return 1; }
    if (is(k, "FogColor"))      { LuaApi::pushColor3(L, e.fogColor);    return 1; }
    if (is(k, "Ambient"))       { LuaApi::pushColor3(L, e.ambientColor); return 1; }
    if (is(k, "SunColor"))      { LuaApi::pushColor3(L, e.sunColor);    return 1; }
    if (is(k, "Name") || is(k, "ClassName")) { lua_pushstring(L, "Lighting"); return 1; }
    if (is(k, "Parent")) { lua_pushnil(L); return 1; }
    if (is(k, "IsA")) { lua_pushcfunction(L, light_isA); return 1; }
    // Its screen effects and Sky: Lighting:GetChildren(), Lighting.Sky...
    if (proxyIndex(L, E(L)->scene()->lightingFolder(true), k)) return 1;
    return luaL_error(L, "'%s' is not a valid member of Lighting", k);
}

int light_newindex(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Environment& e = E(L)->scene()->environment();
    if (is(k, "ClockTime"))     { E(L)->clockTime() = (float)luaL_checknumber(L, 3); E(L)->applyClockTime(); return 0; }
    if (is(k, "Brightness"))    { e.sunIntensity = (float)luaL_checknumber(L, 3); return 0; }
    if (is(k, "GlobalShadows")) { e.shadows = lua_toboolean(L, 3);    return 0; }
    if (is(k, "FogEnabled"))    { e.fogEnabled = lua_toboolean(L, 3); return 0; }
    if (is(k, "FogDensity"))    { e.fogDensity = (float)luaL_checknumber(L, 3); return 0; }
    if (is(k, "FogColor"))      { e.fogColor = LuaApi::checkColor3(L, 3);     return 0; }
    if (is(k, "Ambient"))       { e.ambientColor = LuaApi::checkColor3(L, 3); return 0; }
    if (is(k, "SunColor"))      { e.sunColor = LuaApi::checkColor3(L, 3);     return 0; }
    return luaL_error(L, "'%s' can't be set on Lighting", k);
}

void makeMeta(lua_State* L, const char* name, const luaL_Reg* fns) {
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, fns, 0);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);
}

} // namespace

namespace LuaApi {

void pushInstance(lua_State* L, uint64_t id) {
    if (!id) { lua_pushnil(L); return; }
    // Reuse the same Lua object for the same Instance so it works as a table key.
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceCache");
    if (lua_rawgeti(L, -1, (lua_Integer)id) == LUA_TUSERDATA) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    auto* ref = static_cast<InstRef*>(lua_newuserdatauv(L, sizeof(InstRef), 0));
    ref->id = id;
    luaL_setmetatable(L, kInst);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, (lua_Integer)id);
    lua_remove(L, -2);
}

SceneNode* checkNode(lua_State* L, int idx) {
    // Lighting and the Camera stand for what holds their children (effect.Parent = Lighting).
    if (luaL_testudata(L, idx, kLight)) return engine(L)->scene()->lightingFolder(true);
    if (luaL_testudata(L, idx, kCam)) return engine(L)->scene()->cameraNode();
    // A player (a Lua table) stands for its object: `folder.Parent = player`.
    if (lua_type(L, idx) == LUA_TTABLE) {
        idx = lua_absindex(L, idx);
        lua_pushstring(L, "__node");
        lua_rawget(L, idx);
        auto* r = static_cast<InstRef*>(luaL_testudata(L, -1, kInst));
        uint64_t id = r ? r->id : 0;
        lua_pop(L, 1);
        if (!id) luaL_argerror(L, idx, "expected an object");
        SceneNode* n = engine(L)->resolve(id);
        if (!n) luaL_error(L, "This object has been destroyed");
        return n;
    }
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, idx, kInst));
    SceneNode* n = engine(L)->resolve(ref->id);
    if (!n) luaL_error(L, "This object has been destroyed");
    return n;
}

uint64_t toInstanceId(lua_State* L, int idx) {
    auto* ref = static_cast<InstRef*>(luaL_testudata(L, idx, kInst));
    return ref ? ref->id : 0;
}

void pushSignal(lua_State* L, SignalKind kind, uint64_t id) {
    auto* s = static_cast<SignalRef*>(lua_newuserdatauv(L, sizeof(SignalRef), 0));
    s->kind = kind;
    s->id   = id;
    luaL_setmetatable(L, kSig);
}

void pushValue(lua_State* L, const SceneNode& n) {
    const Attribute& v = n.value;
    switch (v.type) {
        case Attribute::Bool:    lua_pushboolean(L, v.b); break;
        case Attribute::String:  lua_pushstring(L, v.s.c_str()); break;
        case Attribute::Vector3: pushVector3(L, v.v); break;
        case Attribute::Color3:  pushColor3(L, v.v); break;
        default:
            if (n.intValue) lua_pushinteger(L, (lua_Integer)v.n);
            else lua_pushnumber(L, v.n);
    }
}

void pushHumanoid(lua_State* L, uint64_t rootId) {
    auto* h = static_cast<HumRef*>(lua_newuserdatauv(L, sizeof(HumRef), 0));
    h->rootId = rootId;
    luaL_setmetatable(L, kHum);
}

void pushLighting(lua_State* L) {
    // The same object every time, so `effect.Parent == Lighting` holds.
    if (lua_getfield(L, LUA_REGISTRYINDEX, "GB.Lighting") == LUA_TUSERDATA) return;
    lua_pop(L, 1);
    lua_newuserdatauv(L, 1, 0);
    luaL_setmetatable(L, kLight);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.Lighting");
}

void pushCamera(lua_State* L) {
    // The same object every time, so `workspace.CurrentCamera == camera` holds.
    if (lua_getfield(L, LUA_REGISTRYINDEX, "GB.Camera") == LUA_TUSERDATA) return;
    lua_pop(L, 1);
    lua_newuserdatauv(L, 1, 0);
    luaL_setmetatable(L, kCam);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.Camera");
}

void registerInstance(lua_State* L) {
    static const luaL_Reg instMeta[] = {
        {"__index", inst_index}, {"__newindex", inst_newindex}, {"__eq", inst_eq},
        {"__tostring", inst_tostring}, {nullptr, nullptr}};
    static const luaL_Reg sigMeta[]   = {{"__index", sig_index},   {nullptr, nullptr}};
    static const luaL_Reg connMeta[]  = {{"__index", conn_index},  {nullptr, nullptr}};
    static const luaL_Reg humMeta[]   = {{"__index", hum_index}, {"__newindex", hum_newindex}, {"__eq", hum_eq}, {nullptr, nullptr}};
    static const luaL_Reg lightMeta[] = {{"__index", light_index}, {"__newindex", light_newindex}, {nullptr, nullptr}};
    static const luaL_Reg camMeta[]   = {{"__index", cam_index}, {"__newindex", cam_newindex}, {nullptr, nullptr}};
    static const luaL_Reg trackMeta[] = {{"__index", track_index}, {"__newindex", track_newindex}, {"__eq", track_eq}, {nullptr, nullptr}};
    static const luaL_Reg animrMeta[] = {{"__index", animr_index}, {nullptr, nullptr}};
    makeMeta(L, kInst, instMeta);
    makeMeta(L, kSig, sigMeta);
    makeMeta(L, kConn, connMeta);
    makeMeta(L, kHum, humMeta);
    makeMeta(L, kLight, lightMeta);
    makeMeta(L, kCam, camMeta);
    makeMeta(L, kTrack, trackMeta);
    makeMeta(L, kAnimr, animrMeta);

    luaL_newlib(L, kMethods);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    luaL_newlib(L, kTerrainMethods);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.TerrainMethods");

    // Weak-valued cache: id -> Instance userdata.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.InstanceCache");

    lua_newtable(L);
    lua_pushcfunction(L, inst_new);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Instance");
}

} // namespace LuaApi
