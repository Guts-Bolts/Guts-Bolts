#pragma once
#include <memory>
#include <string>

class AppWindow;
class Scene;
class Editor;

// Command-line options (mostly for testing and launching games directly).
struct LaunchOptions {
    std::string openFile;          // --open <file>
    bool        play = false;      // --play            start in Play mode
    std::string screenshot;        // --screenshot <out.ppm>  (then quit)
    int         frames = 120;      // --frames <n>      when to take it
    std::string holdKey;           // --hold <key>      hold a key during Play (W, A, S, D, Space)
    bool        teamHost = false;  // --team-host       start Team Create
    std::string teamJoin;          // --team-join <addr>
    std::string testAddPart;       // --test-add-part <name>  (tests: add + select a part)
    std::string testKeys;          // --test-keys "ctrl+a ctrl+g f2"  (tests: press these one by one)
    std::string testSelect;        // --test-select "Name1,Name2"  (tests)
    std::string exportRoblox;      // --export-roblox <file.rbxlx> (tests)
    std::string testSnapshot;      // --test-snapshot <file.png> (tests: the picture Publish sends)
    std::string testDrop;          // --test-drop "a.fbx;b.png" (tests: files dropped on the 3D view)
    std::string testMesh;          // --test-mesh "enter,face,top,extrude"  (tests: Modeling-mode steps)
    bool        testAnim = false;  // --test-anim (tests: the Animation Editor)
    bool        testCollide = false; // --test-collide (tests: Studio's Collisions)
    std::string testMouse;         // --test-mouse "click:x:y shift:x:y drag:x1:y1:x2:y2"  (tests, from frame 60)
    std::string testCommand;       // --test-command "<lua>"  (tests: run it in the Command Bar)
    std::string testInsert;        // --test-insert "ScreenGui TextButton" (tests: Insert Object, each into the last)
    std::string testPremades;      // --test-premades <comma list or "all">  (tests)
    bool        testFixPictures = false;  // --test-fix-pictures: File > Upload Missing Item Pictures (tests)
    bool        testAnalysis = false;  // --test-analysis: open Script Analysis and print what it finds (tests)
    std::string testTools;         // --test-tools "1 click wait print" while playing (tests: gear)
    // --test-multiplayer "N[:seconds]" (tests): the multiplayer_test AI tool starts a test with N
    // players, adds one more halfway, then prints its output, stops it and quits (default 30 s).
    std::string testMultiplayer;
    // --emote-pictures <dir>: render a picture of each starter emote and animation pack (the
    // character mid-move) into <dir>/<id>.png, then quit. tools/make_emotes.py --pictures packs them.
    std::string emotePictures;
};

// The editor application ("Guts and Bolts Studio").
class Application {
public:
    explicit Application(LaunchOptions opts = {});
    ~Application();
    void run();

private:
    void testMultiplayer(int frame);   // --test-multiplayer
    void makeEmotePictures();          // --emote-pictures
    double m_multiStart = 0.0;
    int    m_multiStep = 0;

    LaunchOptions m_opts;
    // Declared first so it is destroyed last (after everything using OpenGL).
    std::unique_ptr<AppWindow> m_window;
    std::unique_ptr<Scene>     m_scene;
    std::unique_ptr<Editor>    m_editor;
};
