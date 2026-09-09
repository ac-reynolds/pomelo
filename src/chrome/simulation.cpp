#include "chrome/simulation.hpp"
#include "chrome/panel.hpp"

void chrome::draw_simulation(Bounds bounds, PanelState& state) {
    if (begin_panel("simulation", bounds, state, false)) {
        // Simulation controls belong here.
    }
    end_panel();
}
