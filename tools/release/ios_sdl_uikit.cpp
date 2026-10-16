
// SDL device-idiom queries (package_ios.py appends this to the exported dummy.cpp).
// Godot 4.7's iOS template links SDL's joystick code, whose SDL_IsTablet/SDL_IsTV
// call these, but upstream defines them in src/video/uikit/SDL_uikitvideo.m, which
// Godot does not build. Same logic: compare UIDevice.currentDevice.userInterfaceIdiom.
#include <objc/message.h>
#include <objc/runtime.h>

static long openbfme_ui_idiom() {
    id device = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(objc_getClass("UIDevice"), sel_registerName("currentDevice"));
    return reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(device, sel_registerName("userInterfaceIdiom"));
}

extern "C" bool SDL_IsIPad(void) {
    return openbfme_ui_idiom() == 1; // UIUserInterfaceIdiomPad
}

extern "C" bool SDL_IsAppleTV(void) {
    return openbfme_ui_idiom() == 2; // UIUserInterfaceIdiomTV
}
