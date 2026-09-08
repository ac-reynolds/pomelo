#pragma once

struct SDL_Window;

namespace application {

// Owns the process-wide SDL video lifetime and its sole application window. The
// class guarantees SDL outlives the window and the SDL ImGui adapter.
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] SDL_Window* native_window() const;
    [[nodiscard]] bool poll_events();
    [[nodiscard]] bool minimized() const;
    void delay(unsigned milliseconds) const;

    void initialize_imgui_platform();
    void begin_imgui_frame();
    void shutdown_imgui_platform();

private:
    SDL_Window* window_{};
    bool imgui_initialized_{};
};

} // namespace application
