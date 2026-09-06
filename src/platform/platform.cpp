#include "platform/platform.hpp"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>

#include <stdexcept>

struct platform::Platform::Impl {
    SDL_Window* window{};
    bool imgui_initialized{};
};

platform::Platform::Platform() : impl_(std::make_unique<Impl>()) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    impl_->window = SDL_CreateWindow("Pomelo", 1440, 900,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!impl_->window) {
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
}

platform::Platform::~Platform() {
    shutdown_imgui_platform();
    if (impl_->window) SDL_DestroyWindow(impl_->window);
    SDL_Quit();
}

void* platform::Platform::native_window() const { return impl_->window; }

bool platform::Platform::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (impl_->imgui_initialized) ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(impl_->window)))
            return false;
    }
    return true;
}

bool platform::Platform::minimized() const {
    return (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_MINIMIZED) != 0;
}
void platform::Platform::delay(unsigned milliseconds) const { SDL_Delay(milliseconds); }

void platform::Platform::initialize_imgui_platform() {
    if (!ImGui_ImplSDL3_InitForVulkan(impl_->window))
        throw std::runtime_error("Could not initialize ImGui SDL backend");
    impl_->imgui_initialized = true;
}
void platform::Platform::begin_imgui_frame() { ImGui_ImplSDL3_NewFrame(); }
void platform::Platform::shutdown_imgui_platform() {
    if (impl_ && impl_->imgui_initialized) {
        ImGui_ImplSDL3_Shutdown();
        impl_->imgui_initialized = false;
    }
}

