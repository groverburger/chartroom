#pragma once
#include "imgui.h"
#include "state.hpp"
namespace cr {
struct DrawingPlot {
    float left, right, top, bottom;
    double low, high;
    bool logarithmic = false;
    PriceScale scale() const {
        return {low, high, logarithmic};
    }
};
void drawing_toolbar(State &, Panel &);
void drawing_tools_window(State &);
// Returns true when a drawing gesture owns the mouse instead of chart navigation.
bool drawing_input(State &, Panel &, const DrawingPlot &, bool hovered);
void render_drawings(State &, Panel &, const DrawingPlot &);
std::string drawing_hint(const Panel &);
} // namespace cr
