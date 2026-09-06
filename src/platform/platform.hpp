#pragma once

#include <memory>
#include <utility>

namespace platform {

class Platform {
public:
    Platform();
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    [[nodiscard]] void* native_window() const;
    [[nodiscard]] bool poll_events();
    [[nodiscard]] bool minimized() const;
    void delay(unsigned milliseconds) const;

    void initialize_imgui_platform();
    void begin_imgui_frame();
    void shutdown_imgui_platform();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform

