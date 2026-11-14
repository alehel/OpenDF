#include "Window.h"

#include <cstdio>

namespace df {

namespace {
bool g_on = false;
std::function<void(bool)> g_save;
SDL_Scancode g_held = SDL_SCANCODE_UNKNOWN;   // the toggle's key until it is released

bool isToggle(const SDL_Keysym& k) {
    const bool alt = (k.mod & KMOD_ALT) != 0;
    if (alt && (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER)) return true;
#ifdef __APPLE__
    // macOS: Cmd+Ctrl+F (the system's own shortcut) and Cmd+F.
    if ((k.mod & KMOD_GUI) != 0 && k.sym == SDLK_f) return true;
#endif
    return false;
}
} // namespace

bool fullscreen() { return g_on; }

void setFullscreen(bool on) { g_on = on; }

Uint32 windowFlags(Uint32 flags) {
    if (g_on && !(flags & SDL_WINDOW_HIDDEN)) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    return flags;
}

void setFullscreenSaver(std::function<void(bool)> save) { g_save = std::move(save); }

bool fullscreenEvent(SDL_Window* win, const SDL_Event& e) {
    if (e.type == SDL_KEYUP && g_held != SDL_SCANCODE_UNKNOWN && e.key.keysym.scancode == g_held) {
        g_held = SDL_SCANCODE_UNKNOWN;
        return true;
    }
    if (e.type != SDL_KEYDOWN || !isToggle(e.key.keysym)) return false;
    g_held = e.key.keysym.scancode;
    if (e.key.repeat || !win) return true;
    // A hidden window (screenshot runs) never goes fullscreen.
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_HIDDEN) return true;
    const bool on = !g_on;
    if (SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "fullscreen: %s\n", SDL_GetError());
        return true;
    }
    g_on = on;
    if (g_save) g_save(on);
    return true;
}

void maskFullscreenKey(Uint8* keys) {
    if (g_held != SDL_SCANCODE_UNKNOWN) keys[g_held] = 0;
}

} // namespace df
