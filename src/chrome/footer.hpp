#pragma once
#include "chrome/types.hpp"
#include <string_view>

namespace chrome {
float footer_height();
void draw_footer(Bounds bounds, std::string_view message);
} // namespace chrome
