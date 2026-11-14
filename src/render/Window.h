// Fullscreen, an OpenDF amenity (the original games set a display mode;
// OpenDF's window and its size are its own): desktop fullscreen, toggled with
// Alt+Enter (and Cmd+Ctrl+F or Cmd+F on macOS) in the launcher, both front
// ends, missions and the viewers. One setting for the whole run, so every
// window OpenDF opens (launcher -> front end -> mission -> front end) follows
// it; the launcher keeps it in opendf.ini. F11 is not used: it is Delta
// Force's contour map key. docs/LAUNCHER.md "Fullscreen".
#pragma once

#include <SDL.h>

#include <functional>

namespace df {

bool fullscreen();
// Changes the setting only (windows created from now on); no window changes.
void setFullscreen(bool on);
// `flags` for SDL_CreateWindow plus SDL_WINDOW_FULLSCREEN_DESKTOP when the
// setting is on and the window is shown (screenshot runs stay hidden).
Uint32 windowFlags(Uint32 flags);
// Called with the new setting after each toggle (main stores it in opendf.ini).
void setFullscreenSaver(std::function<void(bool)> save);
// For each event before the window's own handling: true when the event is a
// fullscreen toggle key (it has then toggled `win`) or that key's release,
// which the caller must ignore.
bool fullscreenEvent(SDL_Window* win, const SDL_Event& e);
// Clears the toggle's key in a copy of SDL_GetKeyboardState while it is
// still held, so a mission does not see Enter or F held.
void maskFullscreenKey(Uint8* keys);

} // namespace df
