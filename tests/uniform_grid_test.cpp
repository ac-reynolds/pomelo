#include "graphics/uniform_grid.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition) { if (!condition) throw std::runtime_error("Grid check failed"); }
void near(double a, double b) { require(std::abs(a - b) < 1e-6); }
template<typename F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    require(rejected);
}
int main() {
    using namespace geometry;
    try {
        near(sinusoidal_value(0,0,0), 0.5);
        near(sinusoidal_value(0.125,0,0), 1);
        near(sinusoidal_value(0.375,0,0), 0);
        near(sinusoidal_value(0.17,0.31,0.43), sinusoidal_value(0.67,0.31,0.43));
        const auto grid = generate_sinusoidal_grid({24,24,24}, {-1,-1,-1}, 2.0f/24);
        validate_grid(grid);
        require(grid.values == generate_sinusoidal_grid({24,24,24}, {-1,-1,-1}, 2.0f/24).values);
        require(cell_index(grid,1,2,3) == 1 + 24*(2+24*3));
        auto center = cell_center(grid,0,1,2);
        near(center[0], -1 + 1.0/24);
        near(center[1], -1 + 3.0/24);
        near(center[2], -1 + 5.0/24);
        for (int axis=0; axis<3; ++axis) for (int side : {0,23}) {
            float minimum=1, maximum=0;
            for (unsigned z=0; z<24; ++z) for (unsigned y=0; y<24; ++y) for (unsigned x=0; x<24; ++x) {
                unsigned cell[] = {x,y,z};
                if (cell[axis] != unsigned(side)) continue;
                float value=grid.values[cell_index(grid,x,y,z)];
                minimum=std::min(minimum,value); maximum=std::max(maximum,value);
            }
            require(maximum-minimum > 0.9f);
        }
        // A 3x finer grid includes the coarse cell centers at indices 3*i+1.
        auto coarse=generate_sinusoidal_grid({3,5,7}, {2,3,4}, 1);
        auto fine=generate_sinusoidal_grid({9,15,21}, {2,3,4}, 1.0f/3);
        for (unsigned z=0; z<7; ++z) for (unsigned y=0; y<5; ++y) for (unsigned x=0; x<3; ++x)
            near(coarse.values[cell_index(coarse,x,y,z)],fine.values[cell_index(fine,3*x+1,3*y+1,3*z+1)]);
        rejects([] { generate_sinusoidal_grid({0,1,1},{0,0,0},1); });
        rejects([] { generate_sinusoidal_grid({1,1,1},{0,0,0},0); });
        rejects([] { generate_sinusoidal_grid({1,1,1},{0,0,0},-1); });
        rejects([] { generate_sinusoidal_grid({1,1,1},{0,0,0},std::numeric_limits<float>::infinity()); });
        rejects([] { generate_sinusoidal_grid({1,1,1},{NAN,0,0},1); });
        rejects([] { checked_cell_count({UINT32_MAX,UINT32_MAX,UINT32_MAX}); });
        auto bad=coarse; bad.values.pop_back(); rejects([&] { validate_grid(bad); });
        bad=coarse; bad.values[0]=NAN; rejects([&] { validate_grid(bad); });
        std::cout << "Grid checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
