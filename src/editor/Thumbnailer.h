#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Scene;
class SceneNode;
class SceneRenderer;
class Framebuffer;

// Pictures of objects on their own, like the tiles in Roblox's Toolbox: the
// object is copied into an empty little world with a plain dark background,
// and the camera frames it from a three-quarter view.
class Thumbnailer {
public:
    Thumbnailer();
    ~Thumbnailer();

    // A PNG of these objects (copies of them; the originals aren't touched).
    std::string png(const std::vector<const SceneNode*>& nodes, int size = 256);
    // A PNG of whatever `build` puts into an empty scene.
    std::string png(const std::function<void(Scene&)>& build, int size = 256);

    // A texture for the Toolbox, made once per key. Pictures take a moment to
    // make, so only a couple are made each frame; until then this returns 0.
    unsigned texture(const std::string& key, const std::function<void(Scene&)>& build, int size = 128);

    // A picture that changes (a live preview): one texture per slot, made again when
    // the key changes. 0 until the first one is made.
    unsigned live(const std::string& slot, const std::string& key, const std::function<void(Scene&)>& build, int size = 160);

    void newFrame() { m_madeThisFrame = 0; }

private:
    // Renders `scene` framed on everything in it into `fb`.
    void shoot(Scene& scene, Framebuffer& fb, int size);
    std::string toPng(Framebuffer& fb);

    std::unique_ptr<SceneRenderer> m_renderer;
    std::map<std::string, std::unique_ptr<Framebuffer>> m_textures;
    struct Live { std::string key; std::unique_ptr<Framebuffer> fb; };
    std::map<std::string, Live> m_live;
    int m_madeThisFrame = 0;
};
