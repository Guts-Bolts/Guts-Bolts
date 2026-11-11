#pragma once
#include <cstdint>
#include <map>
#include <string>
#include "../../scene/Animation.h"
#include "../EditorState.h"

class Scene;
class SceneNode;

// Studio's Animation Editor, like Roblox's: pick a rig (a Model of parts, like
// the Rig from the MODEL tab or your character), make or pick an Animation
// inside it, then pose its parts at points on the timeline. Turning or moving
// a part with the Rotate / Move tools adds a keyframe where the playhead is;
// Play shows it moving between them.
//
// While it's open the rig is shown posed, but every part remembers where it
// really is (SceneNode::restPose), so saving, undo and Play see the real rig.
class AnimationEditor {
public:
    AnimationEditor(Scene* scene, EditorState* state);

    void render(bool* open);              // the window
    // Once a frame (before the Viewport draws): turn edits of the rig's
    // parts into keyframes, and show the pose at the playhead.
    void update(float dt, bool gizmoInUse);
    void close();                         // stop editing: the rig goes back how it was
    bool editing() const { return m_rig != 0; }
    // Start on this rig (the Model around `node`).
    void editRig(SceneNode* node);
    // Tests.
    void  testSetTime(float t) { m_time = t; m_playing = false; }
    void  testPlay() { m_time = 0.0f; m_playing = true; }
    float time() const { return m_time; }

private:
    SceneNode* rig();
    SceneNode* animation();
    Anim::Clip clip();                    // the Animation's keyframes (parsed)
    void       save(const Anim::Clip& c); // write them back (undoable)
    void       ensureRest();
    void       setTime(float t);
    float      snap(float t) const;
    void       keyPart(Anim::Clip& c, SceneNode* part);   // key the part as it's posed now
    void       drawTimeline(Anim::Clip& c);
    void       drawToolbar(Anim::Clip& c);
    void       drawPosePanel(Anim::Clip& c);
    // The "..." menu (like Roblox's): Load, Save, Save As, Import, Export, Create New, Priority.
    void       drawFileMenu(Anim::Clip& c);
    void       drawDialogs();             // Save As, Publish and Import from the Library
    SceneNode* createAnimation(const std::string& name, const std::string& text);
    std::string fileText(const std::string& name);   // this animation as a .gbanim file
    void       importText(const std::string& text, const std::string& fallbackName);
    void       importRoblox(const std::string& path);   // a Roblox KeyframeSequence, fitted to this rig
public:
    // "wave twice, then bow" -> a new Animation in the rig (AnimFromText.h). "" = done, else what went wrong.
    std::string makeFromWords(const std::string& text);
    void       testImportRoblox(const std::string& path) { importRoblox(path); }
    const std::string& message() const { return m_message; }
private:

    Scene*       m_scene;
    EditorState* m_state;
    uint64_t     m_rig = 0;
    uint64_t     m_anim = 0;
    Anim::RestPose m_rest;
    std::map<uint64_t, Transform> m_applied;   // what we posed each part to last frame

    float m_time = 0.0f;
    float m_view = 2.0f;                  // seconds shown on the timeline
    bool  m_playing = false;
    int   m_fps = 60;                     // keyframes snap to 1/60 s

    // The picked keyframe (part "" = the whole keyframe).
    bool        m_hasSel = false;
    float       m_selTime = 0.0f;
    std::string m_selPart;
    // Dragging a keyframe along the timeline.
    bool        m_dragging = false;
    float       m_dragFrom = 0.0f;
    // Copy / paste of a keyframe's poses.
    std::map<std::string, Anim::Pose> m_clipboard;
    std::string m_newName;

    // The "..." menu's windows.
    bool        m_openSaveAs = false, m_openPublish = false, m_openImport = false, m_openWords = false;
    std::string m_words, m_wordsNote;   // "Make from words..." (AnimFromText.h)
    std::string m_saveAsName, m_pubName, m_pubDesc, m_importId, m_message;
    bool        m_pubPublic = false, m_busy = false, m_doneRequested = false;
    double      m_messageAt = -100.0;
    std::string m_importQuery, m_importLoaded;
    std::vector<std::pair<std::string, std::string>> m_importList;   // id, "name - by creator"
};
