// iOS only: talks to UIKit (Apple's app toolkit) for the things SDL2 can't ask
// about by itself. Built with ARC, so no retain / release here.
#import <UIKit/UIKit.h>
#include "IosPlatform.h"

#include <SDL.h>
#include <SDL_syswm.h>

namespace {

// The UIKit window behind SDL's window, or nil.
UIWindow* uiWindow(SDL_Window* window) {
    if (!window) return nil;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT) return nil;
    return info.info.uikit.window;
}

} // namespace

namespace IosPlatform {

SafeArea safeArea(SDL_Window* window) {
    SafeArea out;
    UIWindow* w = uiWindow(window);
    if (!w) return out;
    const UIEdgeInsets in = w.safeAreaInsets;   // points, already turned to match the screen
    out.left = (float)in.left;
    out.top = (float)in.top;
    out.right = (float)in.right;
    out.bottom = (float)in.bottom;
    return out;
}

void setLandscape(SDL_Window* window, bool on) {
    // SDL's view controller reads this hint every time iOS asks which ways it may turn.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, on ? "LandscapeLeft LandscapeRight" : "LandscapeLeft LandscapeRight Portrait");
    UIWindow* w = uiWindow(window);
    if (!w) return;
    if (@available(iOS 16.0, *)) {
        [w.rootViewController setNeedsUpdateOfSupportedInterfaceOrientations];
        // Turn right away (iOS 16+), instead of waiting for the phone to be tipped over.
        if (UIWindowScene* scene = w.windowScene) {
            UIInterfaceOrientationMask mask = on ? UIInterfaceOrientationMaskLandscape
                                                 : UIInterfaceOrientationMaskAllButUpsideDown;
            UIWindowSceneGeometryPreferencesIOS* prefs =
                [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:mask];
            [scene requestGeometryUpdateWithPreferences:prefs errorHandler:nil];
        }
    } else {
        // iOS 15: turns once the phone is held sideways.
        [UIViewController attemptRotationToDeviceOrientation];
    }
}

} // namespace IosPlatform
