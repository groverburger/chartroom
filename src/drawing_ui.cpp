#include "drawing_ui.hpp"
#include <cstdio>
namespace cr {
static Time step(const Panel &p) {
    return p.tf == 1 ? 14400 : duration(intervals[p.tf]);
}
static const char *tool_name(DrawTool tool) {
    switch (tool) {
    case DrawTool::Fibonacci:
        return "Fibonacci retracement";
    case DrawTool::Level:
        return "Horizontal level";
    case DrawTool::Trend:
        return "Trend line";
    case DrawTool::Ray:
        return "Ray";
    case DrawTool::Rectangle:
        return "Rectangle";
    case DrawTool::Text:
        return "Text note";
    case DrawTool::Measure:
        return "Measure";
    default:
        return "Select / pan";
    }
}
static ImVec2 position(const Panel &p, const DrawingPlot &plot, const Anchor &a) {
    return {plot.left + float((drawing_index(p.bars, a.time, step(p)) - p.view.first + .5) / p.view.count) *
                            (plot.right - plot.left),
            plot.bottom - float(plot.scale().fraction(a.price)) * (plot.bottom - plot.top)};
}
static Anchor anchor(const Panel &p, const DrawingPlot &plot, ImVec2 mouse, const View &view) {
    double index =
        std::round(view.first + (mouse.x - plot.left) / (plot.right - plot.left) * view.count - .5);
    double price = plot.scale().price((plot.bottom - mouse.y) / (plot.bottom - plot.top));
    return {drawing_time(p.bars, index, step(p)), price};
}
static float distance(ImVec2 a, ImVec2 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
static float line_distance(ImVec2 m, ImVec2 a, ImVec2 b, bool ray) {
    double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    if (length < .001)
        return distance(m, a);
    double t = ((m.x - a.x) * dx + (m.y - a.y) * dy) / length;
    t = std::max(0., t);
    if (!ray)
        t = std::min(1., t);
    return distance(m, {float(a.x + t * dx), float(a.y + t * dy)});
}
static bool visible(const Drawing &d, const Panel &p) {
    return d.timeframe < 0 || d.timeframe == p.tf;
}
// Endpoint handles take priority over bodies, including where objects overlap.
static int hit(const Drawing &d, const Panel &p, const DrawingPlot &plot, ImVec2 m, bool handles) {
    auto a = position(p, plot, d.a), b = position(p, plot, d.b);
    if (!std::isfinite(a.y) || ((d.kind != "level" && d.kind != "text") && !std::isfinite(b.y)))
        return -1;
    if (handles && !d.locked) {
        if (distance(m, a) < 8)
            return 1;
        if (d.kind != "level" && d.kind != "text" && distance(m, b) < 8)
            return 2;
    }
    if (d.kind == "fib") {
        float left = d.fib.extend_left ? plot.left : std::min(a.x, b.x);
        float right = d.fib.extend_right ? plot.right : std::max(a.x, b.x);
        for (auto &level : d.fib.levels)
            if (level.enabled) {
                float y = position(p, plot, {d.a.time, fib_price(d, level.ratio)}).y;
                if (m.x >= left - 5 && m.x <= right + 5 && std::abs(m.y - y) < 7)
                    return 0;
            }
        return d.fib.trend_line && line_distance(m, a, b, false) < 6 ? 0 : -1;
    }
    if (d.kind == "level")
        return std::abs(m.y - a.y) < 6 ? 0 : -1;
    if (d.kind == "text") {
        auto size = ImGui::CalcTextSize(d.text.c_str());
        return m.x >= a.x - 5 && m.x <= a.x + size.x + 8 && m.y >= a.y - 5 && m.y <= a.y + size.y + 8 ? 0
                                                                                                      : -1;
    }
    if (d.kind == "rectangle") {
        ImVec2 c{a.x, b.y}, e{b.x, a.y};
        return std::min({line_distance(m, a, c, false), line_distance(m, c, b, false),
                         line_distance(m, b, e, false), line_distance(m, e, a, false)}) < 6
                   ? 0
                   : -1;
    }
    return line_distance(m, a, b, d.kind == "ray") < 6 ? 0 : -1;
}
static void cancel(Panel &p) {
    p.drawing.awaiting_second = false;
    p.drawing.gesture = false;
    p.drawing.measuring = false;
    p.drawing.measured = false;
    p.drawing.tool = DrawTool::Select;
}
static void open_settings(Panel &p, const Drawing &d) {
    p.drawing.settings = d;
    ImGui::OpenPopup("Drawing settings");
}
static void fib_settings(FibSettings &f) {
    ImGui::SeparatorText("Fibonacci");
    ImGui::Checkbox("Reverse", &f.reverse);
    ImGui::SameLine();
    ImGui::Checkbox("Trend line", &f.trend_line);
    ImGui::Checkbox("Extend left", &f.extend_left);
    ImGui::SameLine();
    ImGui::Checkbox("Extend right", &f.extend_right);
    ImGui::Checkbox("Levels", &f.labels);
    ImGui::SameLine();
    ImGui::Checkbox("Percentages", &f.percentages);
    ImGui::SameLine();
    ImGui::Checkbox("Prices", &f.prices);
    ImGui::Checkbox("Labels on right", &f.labels_right);
    ImGui::Checkbox("Background", &f.background);
    ImGui::SameLine();
    float opacity = float(f.opacity);
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("Opacity", &opacity, 0, 1, "%.2f"))
        f.opacity = opacity;
    ImGui::TextDisabled("Ratio / color / visibility (up to 24 levels)");
    ImGui::BeginChild("Fib levels", {0, 170}, ImGuiChildFlags_Borders);
    int remove = -1;
    for (size_t i = 0; i < f.levels.size(); ++i) {
        ImGui::PushID(int(i));
        auto &l = f.levels[i];
        ImGui::Checkbox("##enabled", &l.enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputDouble("##ratio", &l.ratio, 0, 0, "%.3f");
        ImGui::SameLine();
        auto c = ImGui::ColorConvertU32ToFloat4(l.color);
        float rgb[] = {c.x, c.y, c.z};
        if (ImGui::ColorEdit3("##color", rgb, ImGuiColorEditFlags_NoInputs))
            l.color = ImGui::ColorConvertFloat4ToU32({rgb[0], rgb[1], rgb[2], 1});
        ImGui::SameLine();
        ImGui::BeginDisabled(f.levels.size() == 1);
        if (ImGui::SmallButton("Remove"))
            remove = int(i);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (remove >= 0)
        f.levels.erase(f.levels.begin() + remove);
    ImGui::EndChild();
    ImGui::BeginDisabled(f.levels.size() >= 24);
    if (ImGui::Button("Add level"))
        f.levels.push_back({1.618, blue, true});
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset levels"))
        f.levels = FibSettings{}.levels;
}
static void drawing_settings(State &s, Panel &p) {
    auto &u = p.drawing;
    // Settings use an explicit Apply, so a color drag or text edit is one undo step.
    ImGui::SetNextWindowSize({u.settings.kind == "fib" ? 450.f : 340.f, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopup("Drawing settings")) {
        auto &d = u.settings;
        ImGui::TextUnformatted("Drawing settings");
        ImGui::Checkbox("Locked", &d.locked);
        ImGui::BeginDisabled(d.locked);
        auto color = ImGui::ColorConvertU32ToFloat4(d.color);
        float rgb[] = {color.x, color.y, color.z};
        if (ImGui::ColorEdit3("Color", rgb, ImGuiColorEditFlags_NoInputs))
            d.color = ImGui::ColorConvertFloat4ToU32({rgb[0], rgb[1], rgb[2], 1});
        int tf = d.timeframe + 1;
        const char *choices[] = {"All timeframes", "1h", "4h", "1D", "1W"};
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("Visible on", &tf, choices, 5))
            d.timeframe = tf - 1;
        if (d.kind == "fib")
            fib_settings(d.fib);
        if (d.kind == "text") {
            char value[1025];
            std::snprintf(value, sizeof(value), "%s", d.text.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextMultiline("##note", value, sizeof(value), {0, 80}))
                d.text = value;
        }
        ImGui::SetNextItemWidth(170);
        ImGui::InputDouble(d.kind == "level" ? "Price" : "Start price", &d.a.price, 0, 0, "%.6f");
        if (d.kind != "level" && d.kind != "text") {
            ImGui::SetNextItemWidth(170);
            ImGui::InputDouble("End price", &d.b.price, 0, 0, "%.6f");
        }
        ImGui::EndDisabled();
        bool invalid = !std::isfinite(d.a.price) || !std::isfinite(d.b.price);
        for (auto &level : d.fib.levels)
            invalid |= !std::isfinite(level.ratio) || std::abs(level.ratio) > 100;
        ImGui::BeginDisabled(invalid);
        if (ImGui::Button("Apply")) {
            s.drawings.replace(p.symbol, d);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(d.locked);
        if (ImGui::Button("Delete")) {
            s.drawings.erase(p.symbol, d.id);
            u.selected = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}
void drawing_toolbar(State &s, Panel &p) {
    auto &u = p.drawing;
    if (u.selected && !s.drawings.find(p.symbol, u.selected))
        u.selected = 0;
    auto &io = ImGui::GetIO();
    if (s.active == p.id && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !u.gesture) {
        if (io.KeyCtrl || io.KeySuper) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (io.KeyShift)
                    s.drawings.redo();
                else
                    s.drawings.undo();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                s.drawings.redo();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                   ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            auto d = s.drawings.find(p.symbol, u.selected);
            if (d && !d->locked) {
                s.drawings.erase(p.symbol, u.selected);
                u.selected = 0;
            }
        }
    }
    ImGui::SameLine();
    std::string button = u.tool == DrawTool::Select ? "Draw" : "Draw *";
    if (ImGui::Button((button + "###Draw").c_str())) {
        s.active = p.id;
        s.drawing_tools_open = true;
        s.drawing_tools_focus = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open the detachable drawing tools window. Shift-drag to measure.");
    drawing_settings(s, p);
}
void drawing_tools_window(State &s) {
    if (!s.drawing_tools_open)
        return;
    auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({310, 490}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - 340, viewport->WorkPos.y + 110},
                            ImGuiCond_FirstUseEver);
    if (s.drawing_tools_focus) {
        ImGui::SetNextWindowFocus();
        s.drawing_tools_focus = false;
    }
    if (ImGui::Begin("Drawing tools", &s.drawing_tools_open, ImGuiWindowFlags_NoCollapse)) {
        auto label = [&](const Panel &p) {
            return display_symbol(p.symbol) + " " + timeframes[p.tf] + " / Chart " + std::to_string(p.id);
        };
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##target", label(s.current()).c_str())) {
            for (auto &p : s.panels)
                if (ImGui::Selectable(label(*p).c_str(), p->id == s.active))
                    s.active = p->id;
            ImGui::EndCombo();
        }
        auto &p = s.current();
        auto &u = p.drawing;
        for (auto tool : {DrawTool::Select, DrawTool::Level, DrawTool::Trend, DrawTool::Ray,
                          DrawTool::Rectangle, DrawTool::Text, DrawTool::Measure, DrawTool::Fibonacci})
            if (ImGui::Selectable(tool_name(tool), u.tool == tool)) {
                cancel(p);
                u.tool = tool;
                p.show_drawings = true;
            }
        ImGui::Separator();
        ImGui::BeginDisabled(!s.drawings.can_undo() || u.gesture);
        if (ImGui::Button("Undo"))
            s.drawings.undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!s.drawings.can_redo() || u.gesture);
        if (ImGui::Button("Redo"))
            s.drawings.redo();
        ImGui::EndDisabled();
        ImGui::Checkbox("Show drawings", &p.show_drawings);
        if (auto selected = s.drawings.find(p.symbol, u.selected)) {
            auto d = *selected;
            if (ImGui::Button("Edit selected"))
                open_settings(p, d);
            ImGui::SameLine();
            if (ImGui::Button(d.locked ? "Unlock" : "Lock")) {
                d.locked = !d.locked;
                s.drawings.replace(p.symbol, d);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(d.locked);
            if (ImGui::Button("Delete")) {
                s.drawings.erase(p.symbol, d.id);
                u.selected = 0;
            }
            ImGui::EndDisabled();
        }
        ImGui::TextWrapped("Drag this window's title to move or dock it. Tools apply to the selected chart.");
        ImGui::TextDisabled("Shift-drag measures. Esc cancels.");
        drawing_settings(s, p);
    }
    ImGui::End();
}

bool drawing_input(State &s, Panel &p, const DrawingPlot &plot, bool hovered) {
    auto &u = p.drawing;
    auto &io = ImGui::GetIO();
    bool claimed = u.gesture;
    if (u.wait_release) {
        u.wait_release = ImGui::IsMouseDown(0);
        return true;
    }
    if (s.active == p.id && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancel(p);
        u.wait_release = ImGui::IsMouseDown(0);
        u.selected = 0;
        return true;
    }
    const Drawing *target = nullptr;
    int handle = -1;
    if (hovered && p.show_drawings && !u.gesture && u.tool == DrawTool::Select) {
        if (auto selected = s.drawings.find(p.symbol, u.selected); selected && visible(*selected, p)) {
            handle = hit(*selected, p, plot, io.MousePos, true);
            if (handle >= 0)
                target = selected;
        }
        if (!target)
            for (auto it = s.drawings.for_symbol(p.symbol).rbegin();
                 it != s.drawings.for_symbol(p.symbol).rend(); ++it) {
                if (!visible(*it, p))
                    continue;
                handle = hit(*it, p, plot, io.MousePos, false);
                if (handle >= 0) {
                    target = &*it;
                    break;
                }
            }
        if (target)
            ImGui::SetMouseCursor(target->locked ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_ResizeAll);
    }
    if (hovered && ImGui::IsMouseClicked(1)) {
        if (target) {
            u.selected = target->id;
            open_settings(p, *target);
        } else
            cancel(p);
        return true;
    }
    if (hovered && ImGui::IsMouseClicked(0) && !u.gesture) {
        s.active = p.id;
        u.measured = false;
        bool measuring = io.KeyShift || u.tool == DrawTool::Measure;
        if (!measuring && u.tool == DrawTool::Select && !target) {
            u.selected = 0;
            return false;
        }
        if (!measuring && target && ImGui::IsMouseDoubleClicked(0)) {
            u.selected = target->id;
            open_settings(p, *target);
            u.wait_release = true;
            return true;
        }
        u.gesture = true;
        u.awaiting_second = false;
        claimed = true;
        u.measuring = measuring;
        u.creating = measuring || u.tool != DrawTool::Select;
        u.view = p.view;
        u.low = plot.low;
        u.high = plot.high;
        u.logarithmic = plot.logarithmic;
        u.start = anchor(p, plot, io.MousePos, p.view);
        u.start_index = drawing_index(p.bars, u.start.time, step(p));
        if (!u.creating) {
            u.selected = target->id;
            u.draft = u.original = *target;
            u.handle = handle;
        } else {
            u.draft = Drawing{};
            u.draft.a = u.draft.b = u.start;
            u.draft.kind = u.tool == DrawTool::Fibonacci   ? "fib"
                           : u.tool == DrawTool::Level     ? "level"
                           : u.tool == DrawTool::Ray       ? "ray"
                           : u.tool == DrawTool::Rectangle ? "rectangle"
                           : u.tool == DrawTool::Text      ? "text"
                                                           : "trend";
            if (!measuring && (u.tool == DrawTool::Level || u.tool == DrawTool::Text)) {
                u.selected = s.drawings.add(p.symbol, u.draft);
                u.gesture = false;
                u.wait_release = true;
                if (u.tool == DrawTool::Text)
                    open_settings(p, *s.drawings.find(p.symbol, u.selected));
                u.tool = DrawTool::Select;
            }
        }
    }
    if (u.gesture) {
        DrawingPlot frozen = plot;
        frozen.low = u.low;
        frozen.high = u.high;
        frozen.logarithmic = u.logarithmic;
        ImVec2 mouse{std::clamp(io.MousePos.x, plot.left, plot.right),
                     std::clamp(io.MousePos.y, plot.top, plot.bottom)};
        auto at = anchor(p, frozen, mouse, u.view);
        if (u.creating)
            u.draft.b = at;
        else if (!u.original.locked && ImGui::IsMouseDragging(0)) {
            if (u.handle == 1)
                u.draft.a = at;
            else if (u.handle == 2)
                u.draft.b = at;
            else {
                double dx = drawing_index(p.bars, at.time, step(p)) - u.start_index;
                double dy = at.price - u.start.price;
                auto moved = [&](Anchor a) {
                    return Anchor{drawing_time(p.bars, drawing_index(p.bars, a.time, step(p)) + dx, step(p)),
                                  u.logarithmic ? a.price * (at.price / u.start.price) : a.price + dy};
                };
                u.draft.a = moved(u.original.a);
                u.draft.b = moved(u.original.b);
            }
        }
        bool finish = !ImGui::IsMouseDown(0);
        if (u.creating && u.draft.kind == "fib" && !u.measuring) {
            if (u.awaiting_second) {
                finish = hovered && ImGui::IsMouseClicked(0);
                if (finish)
                    u.wait_release = true;
            } else if (finish && distance(position(p, plot, u.draft.a), position(p, plot, u.draft.b)) < 3) {
                u.awaiting_second = true;
                finish = false;
            }
        }
        if (finish) {
            if (u.measuring)
                u.measured = true;
            else if (u.creating) {
                if (distance(position(p, plot, u.draft.a), position(p, plot, u.draft.b)) >= 3)
                    u.selected = s.drawings.add(p.symbol, u.draft);
            } else if (!u.original.locked)
                s.drawings.replace(p.symbol, u.draft);
            u.gesture = false;
            u.awaiting_second = false;
            u.measuring = false;
            u.tool = DrawTool::Select;
        }
    }
    return claimed || (hovered && u.tool != DrawTool::Select);
}
// Clip before submitting lines: saved drawings can lie far outside the view.
static void clipped_line(ImDrawList *draw, ImVec2 a, ImVec2 b, const DrawingPlot &p, ImU32 color,
                         bool ray = false) {
    if (!std::isfinite(a.y) || !std::isfinite(b.y))
        return;
    double dx = double(b.x) - a.x, dy = double(b.y) - a.y, low = 0, high = ray ? 1e30 : 1;
    auto clip = [&](double v, double q) {
        if (std::abs(v) < 1e-12)
            return q >= 0;
        double r = q / v;
        if (v < 0)
            low = std::max(low, r);
        else
            high = std::min(high, r);
        return low <= high;
    };
    if (clip(-dx, a.x - p.left) && clip(dx, p.right - a.x) && clip(-dy, a.y - p.top) &&
        clip(dy, p.bottom - a.y))
        draw->AddLine({float(a.x + low * dx), float(a.y + low * dy)},
                      {float(a.x + high * dx), float(a.y + high * dy)}, color, 1.5f);
}
void render_drawings(State &s, Panel &p, const DrawingPlot &plot) {
    auto *draw = ImGui::GetWindowDrawList();
    auto &u = p.drawing;
    draw->PushClipRect({plot.left, plot.top}, {plot.right, plot.bottom}, true);
    auto render = [&](const Drawing &d, bool selected) {
        if (!visible(d, p))
            return;
        auto a = position(p, plot, d.a), b = position(p, plot, d.b);
        if (!std::isfinite(a.y) || ((d.kind != "level" && d.kind != "text") && !std::isfinite(b.y)))
            return;
        if (d.kind == "level") {
            clipped_line(draw, {plot.left, a.y}, {plot.right, a.y}, plot, d.color);
            char value[80];
            std::snprintf(value, sizeof(value), "%.6g%s", d.a.price, d.locked ? " [locked]" : "");
            draw->AddText({plot.left + 5, a.y - 19}, d.color, value);
        } else if (d.kind == "text") {
            auto size = ImGui::CalcTextSize(d.text.c_str());
            draw->AddRectFilled({a.x - 3, a.y - 2}, {a.x + size.x + 4, a.y + size.y + 2},
                                rgba(11, 16, 23, 220));
            draw->AddText(a, d.color, d.text.c_str());
            if (selected)
                draw->AddRect({a.x - 3, a.y - 2}, {a.x + size.x + 4, a.y + size.y + 2}, d.color);
        } else if (d.kind == "fib") {
            float left = d.fib.extend_left ? plot.left : std::min(a.x, b.x);
            float right = d.fib.extend_right ? plot.right : std::max(a.x, b.x);
            std::vector<std::pair<float, uint32_t>> bands;
            for (auto &level : d.fib.levels)
                if (level.enabled && (!plot.logarithmic || fib_price(d, level.ratio) > 0))
                    bands.push_back(
                        {position(p, plot, {d.a.time, fib_price(d, level.ratio)}).y, level.color});
            std::sort(bands.begin(), bands.end());
            if (d.fib.background && right > plot.left && left < plot.right)
                for (size_t i = 1; i < bands.size(); ++i) {
                    float top = std::max(plot.top, bands[i - 1].first),
                          bottom = std::min(plot.bottom, bands[i].first);
                    if (top < bottom)
                        draw->AddRectFilled(
                            {std::max(left, plot.left), top}, {std::min(right, plot.right), bottom},
                            (bands[i - 1].second & 0xffffffu) | (uint32_t(d.fib.opacity * 255) << 24));
                }
            if (d.fib.trend_line)
                clipped_line(draw, a, b, plot, d.color);
            for (auto &level : d.fib.levels)
                if (level.enabled) {
                    double price = fib_price(d, level.ratio);
                    float y = position(p, plot, {d.a.time, price}).y;
                    clipped_line(draw, {left, y}, {right, y}, plot, level.color);
                    if (!std::isfinite(y) || y < plot.top || y > plot.bottom || right < plot.left ||
                        left > plot.right)
                        continue;
                    char label[96], value[48];
                    std::string text;
                    if (d.fib.labels) {
                        std::snprintf(label, sizeof(label), d.fib.percentages ? "%.1f%%" : "%.3g",
                                      level.ratio * (d.fib.percentages ? 100 : 1));
                        text = label;
                    }
                    if (d.fib.prices) {
                        std::snprintf(value, sizeof(value), "%.6g", price);
                        text += (text.empty() ? "" : "  ") + std::string(value);
                    }
                    auto size = ImGui::CalcTextSize(text.c_str());
                    float x = d.fib.labels_right ? std::min(right, plot.right) - size.x - 4
                                                 : std::max(left, plot.left) + 4;
                    draw->AddText({std::max(plot.left + 2, x), y - ImGui::GetFontSize() - 2}, level.color,
                                  text.c_str());
                }
        } else if (d.kind == "rectangle") {
            ImVec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
            if (lo.x < plot.right && hi.x > plot.left && lo.y < plot.bottom && hi.y > plot.top)
                draw->AddRectFilled({std::max(lo.x, plot.left), std::max(lo.y, plot.top)},
                                    {std::min(hi.x, plot.right), std::min(hi.y, plot.bottom)},
                                    (d.color & 0x00ffffffu) | 0x18000000u);
            clipped_line(draw, a, {a.x, b.y}, plot, d.color);
            clipped_line(draw, {a.x, b.y}, b, plot, d.color);
            clipped_line(draw, b, {b.x, a.y}, plot, d.color);
            clipped_line(draw, {b.x, a.y}, a, plot, d.color);
        } else
            clipped_line(draw, a, b, plot, d.color, d.kind == "ray");
        if (selected && !d.locked) {
            draw->AddCircleFilled(a, 4, d.color);
            if (d.kind != "level" && d.kind != "text")
                draw->AddCircleFilled(b, 4, d.color);
        }
    };
    if (p.show_drawings)
        for (auto &d : s.drawings.for_symbol(p.symbol)) {
            if (u.gesture && !u.creating && u.draft.id == d.id)
                render(u.draft, true);
            else
                render(d, d.id == u.selected);
        }
    if (u.gesture && u.creating && !u.measuring)
        render(u.draft, true);
    if (u.measuring || u.measured) {
        auto a = position(p, plot, u.draft.a), b = position(p, plot, u.draft.b);
        if (!std::isfinite(a.y) || !std::isfinite(b.y)) {
            draw->PopClipRect();
            return;
        }
        auto value = measure(u.draft.a, u.draft.b, p.bars, step(p));
        auto color = value.change >= 0 ? up : down;
        clipped_line(draw, a, {b.x, a.y}, plot, color);
        clipped_line(draw, {b.x, a.y}, b, plot, color);
        clipped_line(draw, a, b, plot, color);
        auto seconds = std::abs(value.seconds);
        char label[200], percent[40];
        if (std::isfinite(value.percent))
            std::snprintf(percent, sizeof(percent), "%+.2f%%", value.percent);
        else
            std::snprintf(percent, sizeof(percent), "n/a");
        std::snprintf(label, sizeof(label), "%+.4g (%s) | %.0f bars\n%lldd %lldh %lldm elapsed", value.change,
                      percent, std::abs(value.bars), static_cast<long long>(seconds / 86400),
                      static_cast<long long>(seconds % 86400 / 3600),
                      static_cast<long long>(seconds % 3600 / 60));
        auto size = ImGui::CalcTextSize(label);
        ImVec2 pos{std::clamp(b.x + 12, plot.left + 4, std::max(plot.left + 4, plot.right - size.x - 10)),
                   std::clamp(b.y + 12, plot.top + 4, std::max(plot.top + 4, plot.bottom - size.y - 10))};
        draw->AddRectFilled({pos.x - 4, pos.y - 3}, {pos.x + size.x + 4, pos.y + size.y + 3},
                            rgba(20, 30, 40, 245));
        draw->AddText(pos, color, label);
    }
    draw->PopClipRect();
}
std::string drawing_hint(const Panel &p) {
    auto &u = p.drawing;
    if (u.measuring || u.measured)
        return "Measure / Shift-drag again / Esc to clear";
    if (u.tool == DrawTool::Fibonacci)
        return u.awaiting_second ? "Fibonacci / Click the second anchor / Esc to cancel"
                                 : "Fibonacci / Click two anchors or drag / Esc to cancel";
    if (u.tool == DrawTool::Level || u.tool == DrawTool::Text)
        return std::string(tool_name(u.tool)) + " / Click to place / Esc to cancel";
    if (u.tool != DrawTool::Select)
        return std::string(tool_name(u.tool)) + " / Drag between two points / Esc to cancel";
    if (u.selected)
        return "Selected drawing / Drag to move / Right-click to edit / Delete / Esc to deselect";
    return {};
}
} // namespace cr
