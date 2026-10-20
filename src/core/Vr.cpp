#include "Vr.h"

#ifndef GB_VR

// Built without VR (phones, Macs): nothing to connect to.
namespace Vr {
namespace { const std::string kNone = "This version of Guts&Bolts was built without VR."; Controls g_none; }
bool start(GLFWwindow*, std::string& error) { error = kNone; return false; }
void stop() {}
bool running() { return false; }
bool visible() { return false; }
const std::string& lastError() { static const std::string empty; return empty; }
bool beginFrame(View[2], float, float) { return false; }
void submitEye(int, unsigned) {}
void endFrame() {}
glm::mat4 head() { return glm::mat4(1.0f); }
bool hand(int, glm::mat4&) { return false; }
const Controls& controls() { return g_none; }
} // namespace Vr

#else

#include "Log.h"
#include <GL/glew.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>   // (OpenXR's Windows header needs IUnknown, which LEAN_AND_MEAN leaves out)
#define XR_USE_PLATFORM_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#else
#include <X11/Xlib.h>
#include <GL/glx.h>
#define XR_USE_PLATFORM_XLIB
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstring>
#include <vector>

namespace Vr {
namespace {

struct Eye {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int w = 0, h = 0;
    std::vector<XrSwapchainImageOpenGLKHR> images;
    bool submitted = false;
};

struct State {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning = false;
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    Eye eyes[2];
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    bool frameBegun = false, shouldRender = false;
    GLuint readFbo = 0, drawFbo = 0;
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction move = XR_NULL_HANDLE, turn = XR_NULL_HANDLE, jump = XR_NULL_HANDLE, menu = XR_NULL_HANDLE;
    XrAction use = XR_NULL_HANDLE, grip = XR_NULL_HANDLE;
    XrPath handPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace handSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    glm::mat4 hands[2] = {glm::mat4(1.0f), glm::mat4(1.0f)};
    bool handOk[2] = {false, false};
    Controls controls;
    glm::mat4 head{1.0f};
    std::string error;
};
State g;

glm::mat4 toMat(const XrPosef& p) {
    glm::mat4 m = glm::mat4_cast(glm::quat(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z));
    m[3] = glm::vec4(p.position.x, p.position.y, p.position.z, 1.0f);
    return m;
}

// OpenGL's lens shape from the headset's four angles (each eye's is lopsided:
// it sees further to the outside than across the nose).
glm::mat4 lens(const XrFovf& fov, float n, float f) {
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
    glm::mat4 m(0.0f);
    m[0][0] = 2.0f / (r - l);
    m[1][1] = 2.0f / (u - d);
    m[2][0] = (r + l) / (r - l);
    m[2][1] = (u + d) / (u - d);
    m[2][2] = -(f + n) / (f - n);
    m[2][3] = -1.0f;
    m[3][2] = -(2.0f * f * n) / (f - n);
    return m;
}

XrPath path(const char* s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(g.instance, s, &p);
    return p;
}

XrAction makeAction(const char* name, const char* shown, XrActionType type, bool perHand = false) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    if (perHand) {   // one action, read separately for each hand
        info.countSubactionPaths = 2;
        info.subactionPaths = g.handPath;
    }
    std::strncpy(info.actionName, name, sizeof(info.actionName) - 1);
    std::strncpy(info.localizedActionName, shown, sizeof(info.localizedActionName) - 1);
    info.actionType = type;
    XrAction a = XR_NULL_HANDLE;
    xrCreateAction(g.actionSet, &info, &a);
    return a;
}

// Which button does what, for each kind of controller. A headset's software
// only uses the ones for its own controllers (and can remap them for the player).
void suggest(const char* profile, std::initializer_list<std::pair<XrAction, const char*>> list) {
    std::vector<XrActionSuggestedBinding> b;
    for (auto& [a, p] : list) b.push_back({a, path(p)});
    XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    s.interactionProfile = path(profile);
    s.suggestedBindings = b.data();
    s.countSuggestedBindings = (uint32_t)b.size();
    xrSuggestInteractionProfileBindings(g.instance, &s);   // (an older runtime may not know a profile: fine)
}

bool makeActions() {
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(si.actionSetName, "gameplay");
    std::strcpy(si.localizedActionSetName, "Playing");
    if (XR_FAILED(xrCreateActionSet(g.instance, &si, &g.actionSet))) return false;
    g.move = makeAction("move", "Walk", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g.turn = makeAction("turn", "Turn", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g.jump = makeAction("jump", "Jump", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.menu = makeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.use = makeAction("use", "Use tool", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.handPath[0] = path("/user/hand/left");
    g.handPath[1] = path("/user/hand/right");
    g.grip = makeAction("hand", "Hand", XR_ACTION_TYPE_POSE_INPUT, true);
    const char* gripL = "/user/hand/left/input/grip/pose";
    const char* gripR = "/user/hand/right/input/grip/pose";
    suggest("/interaction_profiles/oculus/touch_controller", {
        {g.move, "/user/hand/left/input/thumbstick"}, {g.turn, "/user/hand/right/input/thumbstick"},
        {g.jump, "/user/hand/right/input/a/click"}, {g.menu, "/user/hand/left/input/menu/click"},
        {g.use, "/user/hand/right/input/trigger/value"}, {g.grip, gripL}, {g.grip, gripR}});
    suggest("/interaction_profiles/valve/index_controller", {
        {g.move, "/user/hand/left/input/thumbstick"}, {g.turn, "/user/hand/right/input/thumbstick"},
        {g.jump, "/user/hand/right/input/a/click"}, {g.menu, "/user/hand/left/input/b/click"},
        {g.use, "/user/hand/right/input/trigger/click"}, {g.grip, gripL}, {g.grip, gripR}});
    suggest("/interaction_profiles/htc/vive_controller", {
        {g.move, "/user/hand/left/input/trackpad"}, {g.turn, "/user/hand/right/input/trackpad"},
        {g.jump, "/user/hand/right/input/trackpad/click"}, {g.menu, "/user/hand/left/input/menu/click"},
        {g.use, "/user/hand/right/input/trigger/click"}, {g.grip, gripL}, {g.grip, gripR}});
    suggest("/interaction_profiles/microsoft/motion_controller", {
        {g.move, "/user/hand/left/input/thumbstick"}, {g.turn, "/user/hand/right/input/thumbstick"},
        {g.jump, "/user/hand/right/input/thumbstick/click"}, {g.menu, "/user/hand/left/input/menu/click"},
        {g.use, "/user/hand/right/input/trigger/value"}, {g.grip, gripL}, {g.grip, gripR}});
    suggest("/interaction_profiles/khr/simple_controller", {
        {g.use, "/user/hand/right/input/select/click"}, {g.menu, "/user/hand/left/input/menu/click"},
        {g.grip, gripL}, {g.grip, gripR}});
    for (int i = 0; i < 2; ++i) {   // where each controller is
        XrActionSpaceCreateInfo ai{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        ai.action = g.grip;
        ai.subactionPath = g.handPath[i];
        ai.poseInActionSpace.orientation.w = 1.0f;
        xrCreateActionSpace(g.session, &ai, &g.handSpace[i]);
    }
    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &g.actionSet;
    return XR_SUCCEEDED(xrAttachSessionActionSets(g.session, &at));
}

bool makeSwapchains() {
    uint32_t n = 0;
    xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr);
    if (n < 2) return false;
    std::vector<XrViewConfigurationView> cfg(n, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, cfg.data());

    // The picture format: 8-bit sRGB. The game's pictures are already brightness-
    // corrected (sRGB), and the copy into the headset's leaves the bytes as they are,
    // so the headset reads them right. (Plain RGBA8 would look washed out: headsets
    // take those as not corrected yet and correct them again.)
    uint32_t nf = 0;
    xrEnumerateSwapchainFormats(g.session, 0, &nf, nullptr);
    std::vector<int64_t> formats(nf);
    xrEnumerateSwapchainFormats(g.session, nf, &nf, formats.data());
    if (formats.empty()) return false;
    int64_t format = formats[0];
    for (int64_t want : {(int64_t)GL_SRGB8_ALPHA8, (int64_t)GL_RGBA8}) {
        bool has = false;
        for (int64_t f : formats) has = has || f == want;
        if (has) { format = want; break; }
    }

    for (int i = 0; i < 2; ++i) {
        Eye& e = g.eyes[i];
        e.w = (int)cfg[i].recommendedImageRectWidth;
        e.h = (int)cfg[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        sc.format = format;
        sc.sampleCount = 1;
        sc.width = (uint32_t)e.w;
        sc.height = (uint32_t)e.h;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        if (XR_FAILED(xrCreateSwapchain(g.session, &sc, &e.swapchain))) return false;
        uint32_t ni = 0;
        xrEnumerateSwapchainImages(e.swapchain, 0, &ni, nullptr);
        e.images.assign(ni, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        xrEnumerateSwapchainImages(e.swapchain, ni, &ni, (XrSwapchainImageBaseHeader*)e.images.data());
    }
    return true;
}

void closeAll() {
    for (Eye& e : g.eyes) {
        if (e.swapchain) xrDestroySwapchain(e.swapchain);
        e = Eye{};
    }
    if (g.readFbo) glDeleteFramebuffers(1, &g.readFbo);
    if (g.drawFbo) glDeleteFramebuffers(1, &g.drawFbo);
    g.readFbo = g.drawFbo = 0;
    for (XrSpace s : g.handSpace) if (s) xrDestroySpace(s);
    if (g.viewSpace) xrDestroySpace(g.viewSpace);
    if (g.local) xrDestroySpace(g.local);
    if (g.actionSet) xrDestroyActionSet(g.actionSet);   // (its actions go with it)
    if (g.session) xrDestroySession(g.session);
    if (g.instance) xrDestroyInstance(g.instance);
    std::string error = std::move(g.error);
    g = State{};
    g.error = std::move(error);
}

bool fail(std::string& error, const std::string& why) {
    error = why;
    Log::warn("VR: " + why);
    closeAll();
    return false;
}

// The headset's news: it's ready, it lost focus, it's closing...
void pollEvents() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (g.instance && xrPollEvent(g.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            g.state = ((XrEventDataSessionStateChanged*)&ev)->state;
            if (g.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                g.sessionRunning = XR_SUCCEEDED(xrBeginSession(g.session, &bi));
            } else if (g.state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(g.session);
                g.sessionRunning = false;
            } else if (g.state == XR_SESSION_STATE_EXITING || g.state == XR_SESSION_STATE_LOSS_PENDING) {
                g.error = g.state == XR_SESSION_STATE_EXITING ? "VR was closed." : "Lost the VR headset.";
                Log::system("VR: " + g.error);
                closeAll();
                return;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g.error = "The VR software closed.";
            Log::system("VR: " + g.error);
            closeAll();
            return;
        }
        ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void readControls() {
    Controls c;
    if (g.state == XR_SESSION_STATE_FOCUSED) {
        XrActiveActionSet active{g.actionSet, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        if (XR_SUCCEEDED(xrSyncActions(g.session, &sync))) {
            auto vec = [](XrAction a) {
                XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
                gi.action = a;
                XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
                xrGetActionStateVector2f(g.session, &gi, &s);
                glm::vec2 v = s.isActive ? glm::vec2(s.currentState.x, s.currentState.y) : glm::vec2(0.0f);
                return glm::length(v) < 0.15f ? glm::vec2(0.0f) : v;   // (sticks never sit quite at 0)
            };
            auto button = [](XrAction a, bool* pressed) {
                XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
                gi.action = a;
                XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
                xrGetActionStateBoolean(g.session, &gi, &s);
                if (pressed) *pressed = s.isActive && s.currentState && s.changedSinceLastSync;
                return s.isActive && s.currentState;
            };
            c.move = vec(g.move);
            c.turn = vec(g.turn);
            c.jump = button(g.jump, nullptr);
            button(g.menu, &c.menu);
            button(g.use, &c.use);
        }
    }
    g.controls = c;
}

} // namespace

bool start(GLFWwindow* window, std::string& error) {
    if (g.instance) return true;
    g.error.clear();

    // The VR software on this computer (SteamVR, the Oculus / Meta app...).
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr)))
        return fail(error, "No VR software found. Install SteamVR (or your headset's PC app), then try again.");
    std::vector<XrExtensionProperties> exts(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
    bool gl = false;
    for (auto& e : exts) gl = gl || !std::strcmp(e.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
    if (!gl) return fail(error, "Your VR software can't show OpenGL games. Try SteamVR as the OpenXR runtime.");

    const char* want[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName, "Guts&Bolts");
    ci.applicationInfo.applicationVersion = 1;
    std::strcpy(ci.applicationInfo.engineName, "Guts&Bolts");
    ci.applicationInfo.engineVersion = 1;
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);   // (works with every runtime, old ones too)
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = want;
    if (XR_FAILED(xrCreateInstance(&ci, &g.instance)))
        return fail(error, "Couldn't start the VR software. Is SteamVR (or your headset's app) running?");

    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(xrGetSystem(g.instance, &si, &g.system)))
        return fail(error, "No VR headset found. Plug it in and turn it on (or start Quest Link / Air Link), then try again.");

    // OpenXR asks every OpenGL game to check this first.
    PFN_xrGetOpenGLGraphicsRequirementsKHR getReqs = nullptr;
    xrGetInstanceProcAddr(g.instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReqs);
    XrGraphicsRequirementsOpenGLKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!getReqs || XR_FAILED(getReqs(g.instance, g.system, &reqs)))
        return fail(error, "The VR software didn't answer. Try restarting it.");

    // Share the window's OpenGL context with the headset.
#ifdef _WIN32
    XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    binding.hDC = GetDC(glfwGetWin32Window(window));
    binding.hGLRC = glfwGetWGLContext(window);
#else
    XrGraphicsBindingOpenGLXlibKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
    Display* dpy = glfwGetX11Display();
    GLXContext ctx = glfwGetGLXContext(window);
    int fbId = 0, count = 0;
    glXQueryContext(dpy, ctx, GLX_FBCONFIG_ID, &fbId);
    const int attrs[] = {GLX_FBCONFIG_ID, fbId, None};
    GLXFBConfig* configs = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrs, &count);
    if (!configs || count < 1) return fail(error, "Couldn't share the game's picture with the headset.");
    binding.xDisplay = dpy;
    binding.glxFBConfig = configs[0];
    if (XVisualInfo* vi = glXGetVisualFromFBConfig(dpy, configs[0])) { binding.visualid = (uint32_t)vi->visualid; XFree(vi); }
    XFree(configs);
    binding.glxDrawable = glfwGetGLXWindow(window);
    binding.glxContext = ctx;
#endif
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = g.system;
    if (XR_FAILED(xrCreateSession(g.instance, &sci, &g.session)))
        return fail(error, "The headset wouldn't start a game. Close other VR games and try again.");

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    xrCreateReferenceSpace(g.session, &rs, &g.local);
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(g.session, &rs, &g.viewSpace);

    uint32_t nb = 0;
    xrEnumerateEnvironmentBlendModes(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nb, nullptr);
    std::vector<XrEnvironmentBlendMode> blends(nb);
    xrEnumerateEnvironmentBlendModes(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nb, &nb, blends.data());
    if (!blends.empty()) g.blend = blends[0];

    if (!makeSwapchains()) return fail(error, "Couldn't make the pictures for the headset.");
    if (!makeActions()) return fail(error, "Couldn't set up the VR controllers.");
    glGenFramebuffers(1, &g.readFbo);
    glGenFramebuffers(1, &g.drawFbo);

    XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
    xrGetSystemProperties(g.instance, g.system, &props);
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(g.instance, &ip);
    Log::system(std::string("VR: started on ") + props.systemName + " (" + ip.runtimeName + "), " +
                std::to_string(g.eyes[0].w) + "x" + std::to_string(g.eyes[0].h) + " per eye");
    pollEvents();
    return true;
}

void stop() {
    if (!g.instance) return;
    if (g.frameBegun) endFrame();
    g.error.clear();
    closeAll();
    Log::system("VR: stopped");
}

bool running() { pollEvents(); return g.instance != XR_NULL_HANDLE; }
bool visible() { return g.state == XR_SESSION_STATE_VISIBLE || g.state == XR_SESSION_STATE_FOCUSED; }
const std::string& lastError() { return g.error; }

bool beginFrame(View eyes[2], float nearPlane, float farPlane) {
    pollEvents();
    g.shouldRender = false;
    g.controls = Controls{};
    if (!g.sessionRunning) return false;

    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    g.frame = XrFrameState{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(g.session, &wi, &g.frame))) return false;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(g.session, &bi))) return false;
    g.frameBegun = true;
    for (Eye& e : g.eyes) e.submitted = false;
    readControls();
    if (!g.frame.shouldRender) return false;

    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = g.frame.predictedDisplayTime;
    li.space = g.local;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t nv = 0;
    g.views[0] = g.views[1] = XrView{XR_TYPE_VIEW};
    if (XR_FAILED(xrLocateViews(g.session, &li, &vs, 2, &nv, g.views)) || nv < 2 ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return false;
    XrSpaceLocation hl{XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace(g.viewSpace, g.local, g.frame.predictedDisplayTime, &hl)) &&
        (hl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        g.head = toMat(hl.pose);
    for (int i = 0; i < 2; ++i) {   // the controllers
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        g.handOk[i] = g.handSpace[i] && XR_SUCCEEDED(xrLocateSpace(g.handSpace[i], g.local, g.frame.predictedDisplayTime, &loc)) &&
                      (loc.locationFlags & need) == need;
        if (g.handOk[i]) g.hands[i] = toMat(loc.pose);
    }
    for (int i = 0; i < 2; ++i) {
        eyes[i].pose = toMat(g.views[i].pose);
        eyes[i].projection = lens(g.views[i].fov, nearPlane, farPlane);
        eyes[i].width = g.eyes[i].w;
        eyes[i].height = g.eyes[i].h;
    }
    g.shouldRender = true;
    return true;
}

void submitEye(int eye, unsigned colorTexture) {
    if (!g.frameBegun || !g.shouldRender || eye < 0 || eye > 1) return;
    Eye& e = g.eyes[eye];
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(e.swapchain, &ai, &index))) return;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(e.swapchain, &wi))) return;

    // Copy the eye's picture into the headset's.
    GLint oldRead = 0, oldDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.drawFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.images[index].image, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);   // (copy the bytes as they are)
    glBlitFramebuffer(0, 0, e.w, e.h, 0, 0, e.w, e.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)oldRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)oldDraw);

    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(e.swapchain, &ri);
    e.submitted = true;
}

void endFrame() {
    if (!g.frameBegun) return;
    g.frameBegun = false;
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                              {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* layers[1] = {(XrCompositionLayerBaseHeader*)&layer};
    const bool both = g.shouldRender && g.eyes[0].submitted && g.eyes[1].submitted;
    if (both) {
        for (int i = 0; i < 2; ++i) {
            pv[i].pose = g.views[i].pose;
            pv[i].fov = g.views[i].fov;
            pv[i].subImage.swapchain = g.eyes[i].swapchain;
            pv[i].subImage.imageRect = {{0, 0}, {g.eyes[i].w, g.eyes[i].h}};
        }
        layer.space = g.local;
        layer.viewCount = 2;
        layer.views = pv;
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = g.frame.predictedDisplayTime;
    ei.environmentBlendMode = g.blend;
    ei.layerCount = both ? 1 : 0;
    ei.layers = layers;
    xrEndFrame(g.session, &ei);
}

glm::mat4 head() { return g.head; }
bool hand(int side, glm::mat4& pose) {
    if (side < 0 || side > 1 || !g.handOk[side]) return false;
    pose = g.hands[side];
    return true;
}
const Controls& controls() { return g.controls; }

} // namespace Vr

#endif
