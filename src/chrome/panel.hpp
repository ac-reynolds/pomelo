#pragma once
#include "chrome/types.hpp"

namespace chrome {
// Shared panel shell; returns whether the content is expanded. Always call end_panel.
bool begin_panel(const char* name, Bounds bounds, PanelState& state, bool right);
void end_panel();
} // namespace chrome
