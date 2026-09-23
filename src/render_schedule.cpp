#include "render_schedule.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cr {
double next_ui_frame() {
    auto &g = *ImGui::GetCurrentContext();
    auto &io = g.IO;
    double next = std::numeric_limits<double>::infinity();
    auto deadline = [&](double delay) { next = std::min(next, std::max(.001, delay)); };
    if (!g.InputEventsQueue.empty())
        deadline(1. / 60.);
    // Held buttons/keys need drag, auto-scroll and repeat processing. A focused
    // text box or an open popup alone must not keep a full-rate render loop alive.
    if (!io.AppFocusLost) {
        for (bool down : io.MouseDown)
            if (down)
                deadline(1. / 60.);
        for (auto &key : io.KeysData)
            if (key.Down)
                deadline(1. / 60.);
        if (g.ActiveId && g.InputTextState.ID == g.ActiveId && io.ConfigInputTextCursorBlink) {
            double phase = g.InputTextState.CursorAnim;
            if (phase < 0)
                deadline(.8 - phase + .002);
            else {
                phase = std::fmod(phase, 1.2);
                deadline((phase <= .8 ? .8 : 1.2) - phase + .002);
            }
        }
    }
    if (g.HoveredId) {
        double delay = std::max(g.Style.HoverDelayNormal, g.Style.HoverStationaryDelay);
        if (g.HoveredIdTimer <= delay)
            deadline(delay - g.HoveredIdTimer + .01);
    }
    if (g.HoverItemDelayId) {
        if (g.HoverItemDelayTimer <= g.Style.HoverDelayShort)
            deadline(g.Style.HoverDelayShort - g.HoverItemDelayTimer + .01);
        if (g.HoverItemDelayTimer <= g.Style.HoverDelayNormal)
            deadline(g.Style.HoverDelayNormal - g.HoverItemDelayTimer + .01);
        if (g.MouseStationaryTimer <= g.Style.HoverStationaryDelay)
            deadline(g.Style.HoverStationaryDelay - g.MouseStationaryTimer + .01);
    }
    if (g.SettingsDirtyTimer > 0)
        deadline(g.SettingsDirtyTimer + .01);
    if (g.NavWindowingTarget || g.NavWindowingTargetAnim || g.DragDropActive)
        deadline(1. / 60.);
    for (auto *window : g.Windows)
        if (window->Active && (window->AutoFitFramesX > 0 || window->AutoFitFramesY > 0 ||
                               window->HiddenFramesCannotSkipItems > 0))
            deadline(1. / 60.);
    return next;
}
} // namespace cr
