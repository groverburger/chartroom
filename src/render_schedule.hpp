#pragma once
namespace cr {
// Seconds until ImGui next needs a frame, after Render(). Infinity means idle.
double next_ui_frame();
} // namespace cr
