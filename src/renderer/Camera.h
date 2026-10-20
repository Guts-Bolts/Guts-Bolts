#pragma once
#include <glm/glm.hpp>

class Camera {
public:
    Camera();

    // Blender-style orbit: MMB drag = orbit, Shift+MMB = pan, scroll = zoom
    void orbit(float dx, float dy);
    void pan  (float dx, float dy);
    void zoom (float delta);
    // Roblox Studio style: turn your head (the eye stays put) and fly around.
    void look (float dx, float dy);
    void fly  (float forward, float right, float up);   // world units
    void resize(int w, int h);

    glm::mat4 view()       const;
    glm::mat4 projection() const;
    glm::vec3 position()   const;
    glm::vec3 forward()    const;   // the way the camera looks

    float yaw      = 45.0f;
    float pitch    = 25.0f;
    float distance = 8.0f;
    // Playing: a wall between the character and the camera pulls it in to here
    // (< 0 = nothing in the way). `distance` stays what you zoomed to.
    float clip = -1.0f;
    float shownDistance() const { return clip >= 0.0f && clip < distance ? clip : distance; }
    glm::vec3 pivot = {0, 0, 0};
    float fov = 60.0f;
    // Orthographic: no perspective, things don't get smaller further away (2D,
    // isometric and puzzle games). orthoSize = how many studs tall the view is;
    // 0 = as much as the perspective view shows at `distance` (so zooming still works).
    bool  orthographic = false;
    float orthoSize = 0.0f;
    float viewHeight() const;   // orthographic: studs from the bottom of the view to the top
    // Playing, with a script in charge (Camera.CameraType Scriptable / Fixed): the view
    // is exactly `cframe` (where it is, looking along its -Z) instead of the orbit.
    // `noInput`: the mouse, wheel and sticks leave it alone (Scriptable).
    bool      useCFrame = false;
    glm::mat4 cframe{1.0f};
    bool      noInput = false;
    // A VR headset's eye: its own lopsided lens shape instead of fov / orthographic.
    bool      useProjection = false;
    glm::mat4 projectionOverride{1.0f};
    // Back to the plain orbit camera (Studio's Stop, a new game in the Player).
    void      clearScripted() { useCFrame = false; noInput = false; }
    int width()  const { return m_w; }
    int height() const { return m_h; }

private:
    int m_w = 1, m_h = 1;
};
