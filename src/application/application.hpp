#pragma once
#include "application/settings.hpp"
#include <string_view>

namespace application {
// Optional event-poll hook may throw to interrupt execution, including while minimized.
void run(const Settings&, void (*poll_hook)() = nullptr);
int run(std::string_view input_filename = {});
}
