#include "application/window.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

application::Window::Window() {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    window_ = SDL_CreateWindow("Pomelo", 1440, 900,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        const std::runtime_error error(SDL_GetError());
        SDL_Quit();
        throw error;
    }
}

application::Window::~Window() {
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

SDL_Window* application::Window::native_window() const { return window_; }

bool application::Window::poll_events(float& orbit_dx, float& orbit_dy) {
    orbit_dx = orbit_dy = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_MOUSE_MOTION &&
            event.motion.windowID == SDL_GetWindowID(window_) &&
            (event.motion.state & SDL_BUTTON_LMASK)) {
            orbit_dx += event.motion.xrel;
            orbit_dy += event.motion.yrel;
        }
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(window_)))
            return false;
    }
    return true;
}

bool application::Window::minimized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}
void application::Window::delay(unsigned milliseconds) const { SDL_Delay(milliseconds); }

