#pragma once

struct SDL_Window;

namespace application {

// Owns the process-wide SDL video lifetime and its sole application window. The
// class guarantees SDL outlives the window.
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] SDL_Window* native_window() const;
    [[nodiscard]] bool poll_events(float& orbit_dx, float& orbit_dy);
    [[nodiscard]] bool minimized() const;
    void delay(unsigned milliseconds) const;

private:
    SDL_Window* window_{};
};

} // namespace application
