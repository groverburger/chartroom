#pragma once
namespace cr {
// Seconds until ImGui next needs a frame, after Render(). Infinity means idle.
double next_ui_frame();
// Ask for another frame within `seconds` (e.g. an animation drawn this frame). Cleared each frame.
void request_frame(double seconds);
} // namespace cr
