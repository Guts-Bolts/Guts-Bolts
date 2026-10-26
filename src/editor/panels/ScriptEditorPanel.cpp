#include "ScriptEditorPanel.h"
#include "../../core/I18n.h"
#include "../Premades.h"
#include "../Theme.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scripting/ScriptEngine.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <set>

namespace {

// Words autocomplete knows: Lua itself plus the Guts&Bolts / Roblox-style API.
const char* const kWords[] = {
    // Lua
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not",
    "or", "repeat", "return", "then", "true", "until", "while", "continue",
    "print", "warn", "error", "pairs", "ipairs", "tostring", "tonumber", "type", "pcall", "select", "unpack",
    "require", "math", "string", "table", "os", "coroutine",
    "math.random", "math.floor", "math.ceil", "math.abs", "math.sin", "math.cos", "math.sqrt", "math.clamp",
    "math.min", "math.max", "math.huge", "math.pi", "math.round", "math.lerp",
    "string.format", "string.sub", "string.len", "string.upper", "string.lower", "string.rep", "string.split",
    "table.insert", "table.remove", "table.find", "table.sort", "table.concat",
    // Globals
    "workspace", "game", "script", "Instance.new", "Vector3.new", "Color3.new", "Color3.fromRGB", "Color3.fromHSV",
    "CFrame.new", "CFrame.Angles", "CFrame.lookAt", "task.wait", "task.spawn", "task.delay", "wait", "spawn", "delay",
    "time", "tick", "Explode", "Effects", "Sounds", "Gui", "Lighting", "Enum",
    "Players", "RunService", "UserInputService", "VRService", "GetUserCFrame", "VREnabled", "CollectionService", "PathfindingService", "TweenService", "TweenInfo", "HttpService", "JSONEncode", "JSONDecode", "Raycast", "RaycastParams", "FindPartOnRay", "GetPartBoundsInRadius", "Terrain", "FillBlock", "FillBall", "GetHeight", "Sculpt", "BindableEvent", "BindableFunction", "ReplicatedStorage", "ServerStorage", "NextInteger", "NextNumber",
    "PromptProductPurchase", "ProcessReceipt", "PromptProductPurchaseFinished", "ProductPurchaseDecision", "PurchaseGranted",
    "NotProcessedYet", "UserOwnsGamePassAsync", "PromptGamePassPurchase",
    "TeleportService", "TeleportOptions", "Teleport", "TeleportAsync", "TeleportPartyAsync", "SetTeleportData", "GetJoinData",
    "GetLocalPlayerTeleportData",
    "PhysicsService", "RegisterCollisionGroup", "UnregisterCollisionGroup", "RenameCollisionGroup", "IsCollisionGroupRegistered",
    "GetRegisteredCollisionGroups", "CollisionGroupSetCollidable", "CollisionGroupsAreCollidable", "GetMaxCollisionGroups",
    "game:GetService", "Players.LocalPlayer", "RunService.Heartbeat", "UserInputService.InputBegan",
    "ContextActionService", "BindAction", "BindActionAtPriority", "UnbindAction", "UnbindAllActions",
    "GetBoundActionInfo", "GetAllBoundActionInfo", "SetTitle", "SetImage", "SetDescription", "SetPosition", "GetButton",
    "ContextActionResult", "ContextActionPriority", "UserInputState", "KeyCode", "UserInputType",
    // Members
    "Parent", "Name", "ClassName", "Position", "Orientation", "Size", "CFrame", "Color", "Transparency", "Material",
    "Anchored", "CanCollide", "CollisionGroup", "RespectCanCollide", "Velocity", "AssemblyLinearVelocity", "AssemblyAngularVelocity", "Shape",
    "Touched", "Clicked", "Heartbeat", "Died", "Humanoid", "Health", "MaxHealth", "WalkSpeed", "JumpPower", "Character",
    "Connect", "Once", "Wait", "Disconnect", "FindFirstChild", "FindFirstChildOfClass", "WaitForChild", "GetChildren",
    "GetDescendants", "Destroy", "Clone", "IsA", "IsDescendantOf", "GetFullName", "GetPivot", "PivotTo",
    "ApplyImpulse", "ApplyAngularImpulse", "TakeDamage", "BreakJoints", "Play", "Stop",
    "MoveTo", "MoveToFinished", "Move", "Jump", "RootPart", "GetState", "CreatePath", "ComputeAsync", "GetWaypoints",
    "PathfindTo", "PathfindStart", "PathfindStatus", "StopPathfinding", "CheckOcclusionAsync", "FindPathAsync",
    "IsWalkable", "FindClosestPoint", "GetRandomPoint", "CanWalkStraight", "Bake", "SetBakeSettings",
    "AgentRadius", "AgentHeight", "AgentCanJump", "WaypointSpacing", "Costs", "Blocked", "Unblocked",
    "GetAttribute", "SetAttribute", "GetAttributes", "GetAttributeChangedSignal", "AttributeChanged",
    "AddTag", "RemoveTag", "HasTag", "GetTags", "GetTagged", "GetInstanceAddedSignal",
    "ClockTime", "Brightness", "FogEnabled", "FogColor", "Ambient",
    "Gui.Label", "Gui.Message", "Effects.Blood", "Effects.Oil", "Effects.Gibs", "Effects.Sparks", "Sounds.Play",
    // Game UI
    "UDim2.new", "UDim2.fromScale", "UDim2.fromOffset", "UDim.new", "Vector2.new", "StarterGui", "PlayerGui",
    "ScreenGui", "Frame", "TextLabel", "TextButton", "ImageLabel", "ImageButton", "UICorner", "UIStroke", "UIShadow", "UIBlur", "TextBox", "ScrollingFrame", "UIListLayout", "UIGridLayout", "UIPadding", "BillboardGui", "SurfaceGui", "UIGradient", "UIScale", "UIAspectRatioConstraint", "ViewportFrame", "WorldModel", "CurrentCamera", "AspectRatio", "AspectType", "DominantAxis", "LightDirection", "LightColor", "BodyVelocity", "BodyPosition", "BodyGyro", "BodyAngularVelocity", "BodyThrust", "BodyForce", "LinearVelocity", "AlignPosition", "AlignOrientation", "VectorForce", "Torque",
    "AnchorPoint", "BackgroundColor3", "BackgroundTransparency", "BorderSizePixel", "BorderColor3", "ZIndex",
    "Visible", "Text", "TextColor3", "TextSize", "TextScaled", "TextWrapped", "TextXAlignment", "TextYAlignment",
    "TextTransparency", "TextStrokeTransparency", "TextStrokeColor3", "Image", "ImageColor3", "ImageTransparency",
    "AutoButtonColor", "ClipsDescendants", "CornerRadius", "Thickness", "AbsoluteSize", "AbsolutePosition",
    "MouseButton1Click", "Activated", "MouseEnter", "MouseLeave", "DisplayOrder", "Enabled",
    // Talking between LocalScripts and Scripts
    "LocalScript", "RemoteEvent", "RemoteFunction", "ReplicatedStorage", "FireServer", "FireClient", "FireAllClients",
    "OnServerEvent", "OnClientEvent", "InvokeServer", "OnServerInvoke", "LocalPlayer",
    "ProximityPrompt", "ProximityPromptService", "Triggered", "TriggerEnded", "ActionText", "ObjectText",
    "KeyboardKeyCode", "HoldDuration", "MaxActivationDistance", "RequiresLineOfSight", "PromptButtonHoldBegan",
    "PromptButtonHoldEnded", "PromptShown", "PromptHidden", "ClickablePrompt",
    "ClickDetector", "MouseClick", "RightMouseClick", "MouseHoverEnter", "MouseHoverLeave", "CursorIcon",
    "Highlight", "FillColor", "OutlineColor", "FillTransparency", "OutlineTransparency", "DepthMode",
    "HighlightDepthMode", "AlwaysOnTop", "Occluded", "Adornee",
    "Trail", "Beam", "Attachment0", "Attachment1", "ColorSequence", "ColorSequenceKeypoint", "NumberSequence",
    "NumberSequenceKeypoint", "Keypoints", "Lifetime", "MinLength", "MaxLength", "WidthScale", "LightEmission",
    "FaceCamera", "Width0", "Width1", "CurveSize0", "CurveSize1", "Segments", "TextureSpeed", "TextureLength", "TextureMode",
    // Messages between the game's servers
    "MessagingService", "PublishAsync", "SubscribeAsync", "Data", "Sent",
    // Sound groups and sound effects
    "SoundService", "SoundGroup", "Volume", "PlaybackSpeed", "Looped", "Playing", "SoundId", "Priority",
    "ReverbSoundEffect", "EchoSoundEffect", "EqualizerSoundEffect", "DistortionSoundEffect", "PitchShiftSoundEffect",
    "ChorusSoundEffect", "CompressorSoundEffect", "FlangeSoundEffect", "TremoloSoundEffect",
    "DecayTime", "Density", "Diffusion", "DryLevel", "WetLevel", "Delay", "Feedback", "HighGain", "LowGain", "MidGain",
    "Level", "Octave", "Depth", "Mix", "Rate", "Attack", "GainMakeup", "Ratio", "Release", "Threshold", "Duty", "Frequency",
    // Sky and screen effects (in Lighting, or the Camera)
    "Sky", "MaterialVariant", "MaterialService", "BaseMaterial", "ColorMap", "NormalMap", "RoughnessMap", "MetalnessMap", "StudsPerTile", "SkyboxBk", "SkyboxDn", "SkyboxFt", "SkyboxLf", "SkyboxRt", "SkyboxUp", "SunTextureId", "MoonTextureId",
    "StarCount", "CelestialBodiesShown", "SunAngularSize", "MoonAngularSize",
    "ColorCorrectionEffect", "SunRaysEffect", "DepthOfFieldEffect", "BloomEffect", "BlurEffect", "PostEffect",
    "Contrast", "Saturation", "TintColor", "Intensity", "Spread", "Threshold", "FarIntensity", "NearIntensity",
    "FocusDistance", "InFocusRadius", "CurrentCamera",
    // Joints
    "Attachment", "RopeConstraint", "RodConstraint", "SpringConstraint", "WeldConstraint", "HingeConstraint",
    "BallSocketConstraint", "PrismaticConstraint", "CylindricalConstraint", "LimitsEnabled", "UpperAngle", "LowerAngle",
    "TwistLimitsEnabled", "TwistLowerAngle", "TwistUpperAngle", "Restitution", "LowerLimit", "UpperLimit",
    "ActuatorType", "Enum.ActuatorType.Motor", "Enum.ActuatorType.Servo", "MotorMaxForce", "ServoMaxForce",
    "TargetPosition", "CurrentPosition", "AngularActuatorType", "MotorMaxTorque", "ServoMaxTorque", "AngularSpeed",
    "TargetAngle", "CurrentAngle", "AngularLimitsEnabled", "InclinationAngle",
    "ParticleEmitter", "Fire", "Smoke", "Sparkles", "Emit", "NumberRange", "Rate", "Speed", "SpreadAngle", "EmissionDirection",
    "Acceleration", "Drag", "RotSpeed", "LockedToPart", "SecondaryColor", "Heat", "Opacity", "RiseVelocity", "SparkleColor",
    // Emotes and the emote menu
    "PlayEmote", "SetCoreGuiEnabled", "GetCoreGuiEnabled", "CoreGuiType", "EmotesMenu", "GuiService",
    "SetEmotesMenuOpen", "GetEmotesMenuOpen", "GetPlayingAnimationTracks",
};

bool wordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

size_t findText(const std::string& hay, const std::string& needle, size_t from, bool matchCase) {
    if (needle.empty()) return std::string::npos;
    if (matchCase) return hay.find(needle, from);
    return lower(hay).find(lower(needle), from);
}
size_t findTextBack(const std::string& hay, const std::string& needle, size_t before, bool matchCase) {
    if (needle.empty() || before == 0) return std::string::npos;
    return matchCase ? hay.rfind(needle, before - 1) : lower(hay).rfind(lower(needle), before - 1);
}

} // namespace

ScriptEditorPanel::ScriptEditorPanel(Scene* scene) : m_scene(scene) {}

void ScriptEditorPanel::open(uint64_t scriptId) {
    m_id = scriptId;
    m_focus = true;
}

void ScriptEditorPanel::openAtLine(uint64_t scriptId, int line) {
    SceneNode* n = m_scene->findById(scriptId);
    if (!n) return;
    open(scriptId);
    int pos = 0, at = 1;
    for (; pos < (int)n->source.size() && at < line; ++pos) if (n->source[pos] == '\n') ++at;
    m_selectStart = m_selectEnd = pos;
    m_focusCode = true;
}

// Called by the text box every frame while it's active (and when Tab completes a word).
// Roblox Studio's dark script colours.
void ScriptEditorPanel::drawColoredCode(ImDrawList* dl, const std::string& src, ImVec2 origin, float lineH, int first, int last) {
    I18n::Pause keep;   // code and comments stay as they are
    static const std::set<std::string> kKeywords = {"and", "break", "do", "else", "elseif", "end", "for", "function", "goto", "if",
        "in", "local", "not", "or", "repeat", "return", "then", "until", "while", "continue", "self"};
    static const std::set<std::string> kLiterals = {"true", "false", "nil"};
    static const std::set<std::string> kBuiltins = {"game", "workspace", "script", "print", "warn", "error", "wait", "task", "math",
        "string", "table", "Instance", "Vector3", "Vector2", "CFrame", "Color3", "BrickColor", "UDim2", "UDim", "Enum", "pairs",
        "ipairs", "tostring", "tonumber", "typeof", "type", "require", "spawn", "delay", "tick", "time", "os", "coroutine", "pcall",
        "Ray", "TweenInfo", "RaycastParams", "Sounds", "Explode", "next", "select", "unpack", "setmetatable", "getmetatable"};
    const ImU32 cText = IM_COL32(204, 204, 204, 255), cKey = IM_COL32(248, 109, 124, 255), cStr = IM_COL32(173, 241, 149, 255),
                cNum = IM_COL32(255, 198, 0, 255), cCom = IM_COL32(102, 102, 102, 255), cBuilt = IM_COL32(132, 214, 247, 255),
                cCall = IM_COL32(253, 251, 172, 255), cProp = IM_COL32(97, 161, 241, 255);
    int line = 0;
    float x = origin.x;
    size_t i = 0;
    const size_t n = src.size();
    // Draw [a, b) in a colour, moving along (and down at new lines).
    auto paint = [&](size_t a, size_t b, ImU32 col) {
        while (a < b) {
            size_t nl = src.find('\n', a);
            size_t end = nl == std::string::npos || nl >= b ? b : nl;
            if (line >= first && line <= last && end > a) {
                dl->AddText(ImVec2(x, origin.y + line * lineH), col, src.data() + a, src.data() + end);
                x += ImGui::CalcTextSize(src.data() + a, src.data() + end).x;
            } else if (end > a) {
                x += 0.0f;   // off screen: only the line count matters
            }
            if (end < b) { ++line; x = origin.x; a = end + 1; } else a = end;
        }
    };
    auto longClose = [&](size_t at) -> size_t {   // after "[[", "[==[": where it ends
        size_t j = at + 1;
        int level = 0;
        while (j < n && src[j] == '=') { ++level; ++j; }
        std::string close = "]" + std::string((size_t)level, '=') + "]";
        size_t e = src.find(close, j + 1);
        return e == std::string::npos ? n : e + close.size();
    };
    auto isLong = [&](size_t at) {
        if (at >= n || src[at] != '[') return false;
        size_t j = at + 1;
        while (j < n && src[j] == '=') ++j;
        return j < n && src[j] == '[';
    };
    std::string prevWord;
    char prevSig = 0;
    while (i < n && line <= last) {
        const char c = src[i];
        if (c == '-' && i + 1 < n && src[i + 1] == '-') {
            size_t e = isLong(i + 2) ? longClose(i + 2) : src.find('\n', i);
            if (e == std::string::npos) e = n;
            paint(i, e, cCom); i = e; continue;
        }
        if (c == '"' || c == '\'') {
            size_t e = i + 1;
            while (e < n && src[e] != c && src[e] != '\n') { if (src[e] == '\\') ++e; ++e; }
            e = std::min(n, e + 1);
            paint(i, e, cStr); i = e; prevSig = '"'; continue;
        }
        if (isLong(i)) { size_t e = longClose(i); paint(i, e, cStr); i = e; prevSig = '"'; continue; }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i + 1]))) {
            size_t e = i;
            while (e < n && (std::isalnum((unsigned char)src[e]) || src[e] == '.' || src[e] == '_')) ++e;
            paint(i, e, cNum); i = e; prevSig = '0'; continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t e = i;
            while (e < n && (std::isalnum((unsigned char)src[e]) || src[e] == '_')) ++e;
            const std::string w = src.substr(i, e - i);
            size_t k = e;
            while (k < n && (src[k] == ' ' || src[k] == '\t')) ++k;
            const bool call = k < n && (src[k] == '(' || src[k] == '"' || src[k] == '{');
            ImU32 col = cText;
            if (kKeywords.count(w)) col = cKey;
            else if (kLiterals.count(w)) col = cNum;
            else if (prevSig == '.' || prevSig == ':') col = call ? cCall : cProp;
            else if (kBuiltins.count(w)) col = cBuilt;
            else if (call || prevWord == "function") col = cCall;
            paint(i, e, col);
            prevWord = w; prevSig = 'a'; i = e; continue;
        }
        size_t e = i + 1;
        paint(i, e, cText);
        if (!std::isspace((unsigned char)c)) { prevSig = c; prevWord.clear(); }
        i = e;
    }
}

int ScriptEditorPanel::onEdit(ImGuiInputTextCallbackData* d) {
    auto* self = static_cast<ScriptEditorPanel*>(d->UserData);
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Tab: finish the word with the picked suggestion.
        if (!self->m_suggest.empty()) {
            const std::string& word = self->m_suggest[std::clamp(self->m_pick, 0, (int)self->m_suggest.size() - 1)];
            int start = d->CursorPos;
            while (start > 0 && (wordChar(d->Buf[start - 1]) || d->Buf[start - 1] == '.')) --start;
            d->DeleteChars(start, d->CursorPos - start);
            d->InsertChars(start, word.c_str());
            self->m_suggest.clear();
        }
        return 0;
    }
    // Somebody asked us to select something (Find).
    if (self->m_selectStart >= 0) {
        int len = d->BufTextLen;
        d->SelectionStart = std::clamp(self->m_selectStart, 0, len);
        d->SelectionEnd = std::clamp(self->m_selectEnd, 0, len);
        d->CursorPos = d->SelectionEnd;
        self->m_selectStart = self->m_selectEnd = -1;
    }
    self->m_cursor = d->CursorPos;
    self->m_cursorLine = 1 + (int)std::count(d->Buf, d->Buf + d->CursorPos, '\n');
    // Where the cursor is on screen (the text box's own scrolling window is the current one here).
    int lineStart = d->CursorPos;
    while (lineStart > 0 && d->Buf[lineStart - 1] != '\n') --lineStart;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    float x = ImGui::CalcTextSize(d->Buf + lineStart, d->Buf + d->CursorPos).x;
    float y = (self->m_cursorLine - 1) * ImGui::GetTextLineHeight();
    self->m_caret = ImVec2(w->Pos.x + ImGui::GetStyle().FramePadding.x + x - w->Scroll.x,
                           w->Pos.y + ImGui::GetStyle().FramePadding.y + y - w->Scroll.y);
    // The word being typed.
    int start = d->CursorPos;
    while (start > 0 && (wordChar(d->Buf[start - 1]) || d->Buf[start - 1] == '.')) --start;
    self->m_prefix.assign(d->Buf + start, d->Buf + d->CursorPos);   // e.g. "math.fl" or "part.Pos"
    return 0;
}

void ScriptEditorPanel::updateSuggestions(const std::string& source) {
    m_suggest.clear();
    std::string p = m_prefix;
    // "part.Pos" -> suggest members for "Pos"; "math.fl" -> "math.floor".
    std::string member = p;
    if (auto dot = p.find_last_of('.'); dot != std::string::npos) member = p.substr(dot + 1);
    if (member.size() < 2 && p.size() < 3) return;
    std::set<std::string> seen;
    auto offer = [&](const std::string& word) {
        if (m_suggest.size() >= 8 || seen.count(word)) return;
        std::string lw = lower(word);
        // A dotted word ("math.floor") matches the whole prefix; a plain word matches the part after the dot.
        bool ok = word.find('.') != std::string::npos ? lw.rfind(lower(p), 0) == 0 && lw != lower(p)
                                                      : lw.rfind(lower(member), 0) == 0 && lw != lower(member);
        if (!ok) return;
        seen.insert(word);
        // Complete the part after the dot for members.
        m_suggest.push_back(word.find('.') == std::string::npos && p.find('.') != std::string::npos
                                ? p.substr(0, p.find_last_of('.') + 1) + word : word);
    };
    for (const char* w : kWords) offer(w);
    // Names used in this script (locals, functions...).
    std::string word;
    for (size_t i = 0; i <= source.size() && m_suggest.size() < 8; ++i) {
        char c = i < source.size() ? source[i] : ' ';
        if (wordChar(c)) { word += c; continue; }
        if (word.size() >= 3 && !std::isdigit((unsigned char)word[0])) offer(word);
        word.clear();
    }
    m_pick = std::clamp(m_pick, 0, std::max(0, (int)m_suggest.size() - 1));
}

void ScriptEditorPanel::findNext(const std::string& source, bool backwards) {
    if (m_find.empty()) return;
    size_t at;
    if (backwards) {
        size_t from = m_matchAt >= 0 ? (size_t)m_matchAt : (size_t)m_cursor;
        at = findTextBack(source, m_find, from, m_matchCase);
        if (at == std::string::npos) at = findTextBack(source, m_find, source.size() + 1, m_matchCase);   // wrap
    } else {
        size_t from = m_matchAt >= 0 ? (size_t)m_matchAt + 1 : (size_t)m_cursor;
        at = findText(source, m_find, from, m_matchCase);
        if (at == std::string::npos) at = findText(source, m_find, 0, m_matchCase);   // wrap
    }
    if (at == std::string::npos) { m_findStatus = "Not found"; m_matchAt = -1; return; }
    m_matchAt = (int)at;
    m_selectStart = (int)at;
    m_selectEnd = (int)(at + m_find.size());
    m_focusCode = true;
    int line = 1 + (int)std::count(source.begin(), source.begin() + (long)at, '\n');
    m_findStatus = "Line " + std::to_string(line);
}

void ScriptEditorPanel::render() {
    if (m_focus) { ImGui::SetNextWindowFocus(); m_focus = false; }
    ImGui::Begin("Script Editor");

    SceneNode* s = m_id ? m_scene->findById(m_id) : nullptr;
    if (s && !s->isScript()) s = nullptr;
    // Follow the selection when a different script is picked.
    if (SceneNode* sel = m_scene->selected(); sel && sel->isScript() && sel != s) {
        s = sel;
        m_id = sel->id;
    }

    // Ctrl+Shift+F works from anywhere in Studio.
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) showFindAll();

    if (!s) {
        ImGui::Spacing();
        ImGui::TextWrapped("No script open.");
        ImGui::Spacing();
        ImGui::TextDisabled("To write code:");
        ImGui::BulletText("Select a part, then click  + Script  in the Toolbox\n(or Add > Script in the menu).");
        ImGui::BulletText("Double-click any Script in the Explorer to open it here.");
        ImGui::BulletText("Press Play (F5) to run your scripts.");
        ImGui::BulletText("Ctrl+Shift+F searches every script.");
        ImGui::End();
        return;
    }

    // Find / replace keys while this panel has focus.
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        m_showFind = true; m_showReplace = false; m_focusFind = true;
    }
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        m_showFind = true; m_showReplace = true; m_focusFind = true;
    }
    if (focused && m_showFind && ImGui::IsKeyPressed(ImGuiKey_F3, false)) findNext(s->source, io.KeyShift);

    // --- Header: which script, enabled toggle, snippets ---
    ImGui::TextDisabled("Editing");
    ImGui::SameLine();
    ImGui::TextUnformatted(s->fullName().c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &s->enabled);
    ImGui::SameLine();
    if (ImGui::Button("Insert code..."))
        ImGui::OpenPopup("##snippets");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a ready-made piece of code to the end of this script");
    if (ImGui::BeginPopup("##snippets")) {
        ImGui::TextDisabled("Adds to the end of the script");
        ImGui::Separator();
        for (const Snippet& sn : snippetList()) {
            if (ImGui::MenuItem(sn.name)) {
                if (!s->source.empty() && s->source.back() != '\n') s->source += '\n';
                if (!s->source.empty()) s->source += '\n';
                s->source += sn.code;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Find")) { m_showFind = !m_showFind; m_focusFind = m_showFind; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Find (Ctrl+F) and replace (Ctrl+H) in this script");
    ImGui::SameLine();
    if (ImGui::Button("Find in all")) showFindAll();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search every script in the game (Ctrl+Shift+F)");

    // --- Find / replace bar ---
    if (m_showFind) {
        ImGui::SetNextItemWidth(220);
        if (m_focusFind) { ImGui::SetKeyboardFocusHere(); m_focusFind = false; }
        if (ImGui::InputTextWithHint("##find", "Find", &m_find, ImGuiInputTextFlags_EnterReturnsTrue))
            findNext(s->source, io.KeyShift);
        if (ImGui::IsItemEdited()) { m_matchAt = -1; m_findStatus.clear(); }
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_showFind = false;
        ImGui::SameLine();
        if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) findNext(s->source, true);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Down)) findNext(s->source, false);
        ImGui::SameLine();
        ImGui::Checkbox("Aa", &m_matchCase);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Match capital letters exactly");
        ImGui::SameLine();
        if (!m_find.empty()) {
            int count = 0;
            for (size_t at = findText(s->source, m_find, 0, m_matchCase); at != std::string::npos;
                 at = findText(s->source, m_find, at + m_find.size(), m_matchCase))
                ++count;
            ImGui::TextDisabled("%d found%s%s", count, m_findStatus.empty() ? "" : "  -  ", m_findStatus.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(m_showReplace ? "Hide replace" : "Replace...")) m_showReplace = !m_showReplace;
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) m_showFind = false;
        if (m_showReplace) {
            ImGui::SetNextItemWidth(220);
            ImGui::InputTextWithHint("##replace", "Replace with", &m_replace);
            ImGui::SameLine();
            if (ImGui::Button("Replace")) {
                // Replace the match we're on (if the cursor is still on it), then find the next one.
                if (m_matchAt >= 0 && findText(s->source, m_find, (size_t)m_matchAt, m_matchCase) == (size_t)m_matchAt) {
                    s->source.replace((size_t)m_matchAt, m_find.size(), m_replace);
                    m_cursor = m_matchAt + (int)m_replace.size();
                    m_matchAt = -1;
                }
                findNext(s->source, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Replace all")) {
                int n = 0;
                for (size_t at = findText(s->source, m_find, 0, m_matchCase); at != std::string::npos;
                     at = findText(s->source, m_find, at + m_replace.size(), m_matchCase)) {
                    s->source.replace(at, m_find.size(), m_replace);
                    if (++n > 100000) break;
                }
                m_findStatus = "Replaced " + std::to_string(n);
                m_matchAt = -1;
            }
        }
    }

    // --- Code area ---
    float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    ImGui::PushFont(EditorTheme::codeFont());
    // While suggestions show, Tab picks one; otherwise it types a tab.
    const bool suggesting = m_codeActive && !m_suggest.empty();
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackAlways |
                                (suggesting ? ImGuiInputTextFlags_CallbackCompletion : ImGuiInputTextFlags_AllowTabInput);
    // Line numbers down the left, like Roblox Studio.
    const int lines = 1 + (int)std::count(s->source.begin(), s->source.end(), '\n');
    const float gutter = ImGui::CalcTextSize(std::to_string(std::max(lines, 99)).c_str()).x + 14.0f;
    const ImVec2 gutterPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(gutter, 1));
    ImGui::SameLine(0, 0);
    if (m_focusCode) { ImGui::SetKeyboardFocusHere(); m_focusCode = false; }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
    ImGui::InputTextMultiline("##code", &s->source, ImVec2(-1, -footer), flags, &ScriptEditorPanel::onEdit, this);
    ImGui::PopStyleColor();
    m_codeActive = ImGui::IsItemActive();
    const float lineH = ImGui::GetTextLineHeight();
    {
        const ImVec2 boxMin = ImGui::GetItemRectMin(), boxMax = ImGui::GetItemRectMax();
        // The text box scrolls inside its own little window (the last child made just now).
        ImVec2 scroll(0, 0);
        ImGuiWindow* parent = ImGui::GetCurrentWindow();
        ImGuiWindow* box = parent->DC.ChildWindows.empty() ? nullptr : parent->DC.ChildWindows.back();
        if (box) scroll = box->Scroll;
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        const ImVec2 origin(boxMin.x + pad.x - scroll.x, boxMin.y + pad.y - scroll.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Gutter: numbers, with the mistake's line in red.
        dl->AddRectFilled(ImVec2(gutterPos.x, boxMin.y), ImVec2(gutterPos.x + gutter - 2, boxMax.y), IM_COL32(37, 37, 38, 255));
        dl->PushClipRect(ImVec2(gutterPos.x, boxMin.y), ImVec2(gutterPos.x + gutter, boxMax.y), true);
        const int first = std::max(0, (int)(scroll.y / lineH) - 1), last = std::min(lines, first + (int)((boxMax.y - boxMin.y) / lineH) + 3);
        for (int i = first; i < last; ++i) {
            const std::string n = std::to_string(i + 1);
            const float y = origin.y + i * lineH;
            const bool bad = !m_error.empty() && m_errorLine == i + 1;
            const bool here = m_cursorLine == i + 1;
            dl->AddText(ImVec2(gutterPos.x + gutter - 8 - ImGui::CalcTextSize(n.c_str()).x, y),
                        bad ? IM_COL32(255, 90, 80, 255) : here ? IM_COL32(220, 220, 220, 255) : IM_COL32(120, 120, 120, 255), n.c_str());
        }
        dl->PopClipRect();
        // The code itself, coloured like Roblox Studio's dark theme (drawn over the plain text,
        // into the text box's own window so it lands on top).
        if (box) dl = box->DrawList;
        dl->PushClipRect(ImVec2(boxMin.x + 1, boxMin.y + 1), ImVec2(boxMax.x - 1, boxMax.y - 1), true);
        drawColoredCode(dl, s->source, origin, lineH, first, last);
        // A red wavy line under the line with the mistake.
        if (!m_error.empty() && m_errorLine >= 1 && m_errorLine <= lines) {
            size_t ls = 0;
            for (int k = 1; k < m_errorLine; ++k) ls = s->source.find('\n', ls) + 1;
            size_t le = s->source.find('\n', ls);
            if (le == std::string::npos) le = s->source.size();
            const float w = std::max(20.0f, ImGui::CalcTextSize(s->source.data() + ls, s->source.data() + le).x);
            const float y = origin.y + m_errorLine * lineH - 1.0f;
            for (float x = 0; x < w; x += 4.0f)
                dl->AddLine(ImVec2(origin.x + x, y + ((int)(x / 4) % 2 ? 2.0f : 0.0f)),
                            ImVec2(origin.x + x + 4, y + ((int)(x / 4) % 2 ? 0.0f : 2.0f)), IM_COL32(255, 70, 60, 255), 1.2f);
        }
        dl->PopClipRect();
    }
    ImGui::PopFont();

    // --- Autocomplete popup under the cursor ---
    if (m_codeActive) updateSuggestions(s->source);
    else m_suggest.clear();
    if (m_codeActive && !m_suggest.empty()) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_pick = (m_pick + 1) % (int)m_suggest.size();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_pick = (m_pick + (int)m_suggest.size() - 1) % (int)m_suggest.size();
        ImGui::SetNextWindowPos(ImVec2(m_caret.x, m_caret.y + lineH + 2));
        ImGui::BeginTooltip();
        ImGui::PushFont(EditorTheme::codeFont());
        for (int i = 0; i < (int)m_suggest.size(); ++i) {
            if (i == m_pick) ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1), "> %s", m_suggest[i].c_str());
            else ImGui::Text("  %s", m_suggest[i].c_str());
        }
        ImGui::PopFont();
        ImGui::TextDisabled("Tab: use it   Ctrl+Up/Down: pick");
        ImGui::EndTooltip();
    }

    // --- Live syntax check (only re-run when the text changes) ---
    if (s->source != m_checked) {
        m_checked = s->source;
        m_error.clear();
        ScriptEngine::checkSyntax(s->source, m_error, m_errorLine);
    }
    if (m_error.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "No mistakes found");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.4f, 1), "Line %d: %s", m_errorLine, m_error.c_str());
    }
    ImGui::SameLine();
    char pos[32];
    std::snprintf(pos, sizeof(pos), "Ln %d", m_cursorLine);
    float w = ImGui::CalcTextSize(pos).x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
    ImGui::TextDisabled("%s", pos);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Find in all scripts
// ---------------------------------------------------------------------------

void ScriptEditorPanel::runFindAll() {
    m_hits.clear();
    m_findAllRan = m_findAll;
    if (m_findAll.empty()) return;
    m_scene->forEach([&](SceneNode* n) {
        if (!n->isScript() || m_hits.size() >= 500) return;
        const std::string& src = n->source;
        for (size_t at = findText(src, m_findAll, 0, false); at != std::string::npos && m_hits.size() < 500;
             at = findText(src, m_findAll, at + m_findAll.size(), false)) {
            size_t ls = src.rfind('\n', at);
            ls = ls == std::string::npos ? 0 : ls + 1;
            size_t le = src.find('\n', at);
            std::string text = src.substr(ls, (le == std::string::npos ? src.size() : le) - ls);
            text.erase(0, text.find_first_not_of(" \t"));
            if (text.size() > 90) text = text.substr(0, 90) + "...";
            int line = 1 + (int)std::count(src.begin(), src.begin() + (long)at, '\n');
            m_hits.push_back({n->id, n->fullName(), line, (int)at, text});
        }
    });
}

void ScriptEditorPanel::renderFindAll() {
    if (!m_showFindAll) return;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Find in All Scripts", &m_showFindAll)) { ImGui::End(); return; }
    ImGui::SetNextItemWidth(-1);
    if (m_focusFindAll) { ImGui::SetKeyboardFocusHere(); m_focusFindAll = false; }
    ImGui::InputTextWithHint("##findall", "Search every script (not case sensitive)", &m_findAll);
    if (m_findAll != m_findAllRan) runFindAll();
    if (!m_findAll.empty()) ImGui::TextDisabled("%d match%s", (int)m_hits.size(), m_hits.size() == 1 ? "" : "es");
    ImGui::Separator();
    ImGui::BeginChild("##hits");
    for (size_t i = 0; i < m_hits.size(); ++i) {
        const Hit& h = m_hits[i];
        ImGui::PushID((int)i);
        char label[256];
        std::snprintf(label, sizeof(label), "%s  (line %d)", h.name.c_str(), h.line);
        if (ImGui::Selectable(label)) {
            if (SceneNode* n = m_scene->findById(h.script)) {
                m_scene->select(n);
                open(h.script);
                m_selectStart = h.pos;
                m_selectEnd = h.pos + (int)m_findAll.size();
                m_focusCode = true;
            }
        }
        ImGui::PushFont(EditorTheme::codeFont());
        ImGui::TextDisabled("    %s", h.text.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
