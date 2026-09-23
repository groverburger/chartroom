#include "app.hpp"
#include "build_version.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "drawing_ui.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cctype>
#include <cstdio>
#include <functional>
namespace cr {
static constexpr ImU32 bg = rgba(11, 16, 23), grid = rgba(31, 41, 51), muted = rgba(117, 140, 163),
                       ink = rgba(219, 230, 240);
static std::string fmt(double n, int digits = 2) {
    if (!std::isfinite(n))
        return "--";
    char b[80];
    std::snprintf(b, sizeof(b), "%.*f", digits, n);
    return b;
}
static void text(ImU32 color, const std::string &s) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
}
void theme() {
    ImGui::StyleColorsDark();
    auto &s = ImGui::GetStyle();
    s.WindowRounding = s.FrameRounding = s.GrabRounding = s.TabRounding = s.PopupRounding = s.ChildRounding =
        s.ScrollbarRounding = 0;
    s.WindowBorderSize = 1;
    s.FramePadding = {6, 4};
    s.ItemSpacing = {8, 6};
    s.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(rgba(18, 23, 31));
    s.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(ink);
    for (auto c : {ImGuiCol_Button, ImGuiCol_FrameBg, ImGuiCol_Header, ImGuiCol_TabSelected})
        s.Colors[c] = ImGui::ColorConvertU32ToFloat4(grid);
    s.Colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(up);
}
static std::string title(const Panel &p, const State &s) {
    return display_symbol(p.symbol) + " " + timeframes[p.tf] + (p.id == s.active ? " *" : "") + "###chart_" +
           std::to_string(p.id);
}
static void layout(State &s, ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID main = dock, left = 0;
    ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, .18f, &left, &main);
    ImGui::DockBuilderDockWindow("Watchlist", left);
    std::vector<ImGuiID> slots;
    if (s.layout == "Tabs")
        slots.assign(s.panels.size(), main);
    else {
        std::function<void(ImGuiID, int, bool)> split = [&](ImGuiID id, int n, bool across) {
            if (n <= 1) {
                slots.push_back(id);
                return;
            }
            int first = n / 2;
            ImGuiID a = 0, b = 0;
            ImGui::DockBuilderSplitNode(id, across ? ImGuiDir_Left : ImGuiDir_Up, float(first) / float(n), &a,
                                        &b);
            split(a, first, s.layout == "Grid" ? !across : across);
            split(b, n - first, s.layout == "Grid" ? !across : across);
        };
        split(main, int(s.panels.size()), s.layout != "Rows");
    }
    for (size_t i = 0; i < s.panels.size(); ++i)
        ImGui::DockBuilderDockWindow(title(*s.panels[i], s).c_str(), slots[i]);
    ImGui::DockBuilderFinish(dock);
    s.relayout = false;
}
static void row(State &s, const std::string &symbol, bool pinned) {
    ImGui::PushID((std::string(pinned ? "core" : "list") + symbol).c_str());
    float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    bool chosen = s.current().symbol == symbol;
    if (ImGui::Selectable(display_symbol(symbol).c_str(), chosen)) {
        auto &io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeySuper)
            s.add(symbol);
        else
            s.select(s.current(), symbol, s.current().tf);
    }
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::BeginPopupContextItem("Ticker")) {
        if (ImGui::MenuItem("Open in new chart"))
            s.add(symbol);
        auto &list = s.lists[size_t(s.selected_list)];
        auto it = std::find(list.symbols.begin(), list.symbols.end(), symbol);
        if (pinned) {
            if (ImGui::MenuItem("Add to current list") && it == list.symbols.end())
                list.symbols.push_back(symbol);
        } else if (it != list.symbols.end()) {
            if (ImGui::MenuItem("Move up", nullptr, false, it != list.symbols.begin()))
                std::iter_swap(it, it - 1);
            if (ImGui::MenuItem("Move down", nullptr, false, it + 1 != list.symbols.end()))
                std::iter_swap(it, it + 1);
            if (ImGui::MenuItem("Remove from list"))
                list.symbols.erase(std::find(list.symbols.begin(), list.symbols.end(), symbol));
        }
        ImGui::EndPopup();
    }
    auto it = s.quotes.find(symbol);
    std::string label = "--";
    ImU32 color = muted;
    if (it != s.quotes.end()) {
        auto &q = it->second;
        label = (q.change >= 0 ? "+" : "") + fmt(q.change) + "%";
        if (now() - q.fetched < 600 && !s.quote_errors.count(symbol))
            color = q.change >= 0 ? up : down;
    }
    ImGui::SameLine(std::max(60.f, right - ImGui::CalcTextSize(label.c_str()).x - 2));
    text(color, label);
    if (hovered || ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(symbol.c_str());
        if (it != s.quotes.end()) {
            text(ink, fmt(it->second.price) + " / " + label + " versus previous daily close");
            text(muted, "As of " + date(it->second.asof) + " UTC");
        }
        if (s.quote_errors.count(symbol))
            text(gold, s.quote_errors[symbol]);
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}
static void watchlist(State &s) {
    if (!ImGui::Begin("Watchlist", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const bool newer = s.available_build > build::number;
    text(newer ? gold : muted, std::string("v") + build::version);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Built %s", build::timestamp);
        if (!s.available_build)
            ImGui::TextUnformatted("Available-build metadata has not been read yet.");
        else if (newer)
            ImGui::Text("Newer build #%llu is available.", (unsigned long long)s.available_build);
        else
            ImGui::TextUnformatted("Matches the available build for this installation.");
        ImGui::TextUnformatted("Checks for a newer build every minute.");
        ImGui::EndTooltip();
    }
    if (newer) {
        ImGui::SameLine();
#ifdef __EMSCRIPTEN__
        if (ImGui::SmallButton("Reload")) {
            s.ini = ImGui::SaveIniSettingsToMemory();
            s.save(true);
            EM_ASM({ Module.chartroomReload(); });
        }
#else
        text(gold, "Restart to update");
#endif
    }
    if (ImGui::Button("+ Chart"))
        s.add();
    ImGui::SameLine();
    if (ImGui::Button("Layout"))
        ImGui::OpenPopup("Layout");
    if (ImGui::BeginPopup("Layout")) {
        for (auto name : {"Grid", "Columns", "Rows", "Tabs"})
            if (ImGui::MenuItem(name, nullptr, s.layout == name)) {
                s.layout = name;
                s.relayout = true;
            }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate active chart"))
            s.add("", true);
        if (ImGui::MenuItem("Close active chart", nullptr, false, s.panels.size() > 1))
            s.current().open = false;
        ImGui::EndPopup();
    }
    static char jump[64]{};
    float go_width = ImGui::CalcTextSize("Go").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::SetNextItemWidth(
        std::max(60.f, ImGui::GetContentRegionAvail().x - go_width - ImGui::GetStyle().ItemSpacing.x));
    bool go =
        ImGui::InputTextWithHint("##jump", "Go to symbol", jump, sizeof(jump),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsUppercase);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Enter a symbol to load it in the active chart.");
    ImGui::SameLine();
    go |= ImGui::Button("Go");
    if (go) {
        try {
            auto symbol = normalize_symbol(jump);
            s.select(s.current(), symbol, s.current().tf);
            jump[0] = 0;
            s.notice.clear();
            ImGui::SetWindowFocus(title(s.current(), s).c_str());
        } catch (const std::exception &e) {
            s.notice = e.what();
        }
    }
    ImGui::Separator();
    for (auto &symbol : core_symbols)
        row(s, symbol, true);
    ImGui::Separator();
    ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x - 55));
    if (ImGui::BeginCombo("##list", s.lists[size_t(s.selected_list)].name.c_str())) {
        for (size_t i = 0; i < s.lists.size(); ++i)
            if (ImGui::Selectable(s.lists[i].name.c_str(), int(i) == s.selected_list))
                s.selected_list = int(i);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Lists"))
        ImGui::OpenPopup("Lists");
    if (ImGui::BeginPopup("Lists")) {
        static char name[128]{};
        static std::string error;
        ImGui::InputTextWithHint("##name", "List name", name, sizeof(name));
        auto valid_name = [&]() {
            std::string value = name;
            auto a = value.find_first_not_of(" \t\r\n"), b = value.find_last_not_of(" \t\r\n");
            if (a == std::string::npos || b - a + 1 > 40)
                throw std::runtime_error("Use 1-40 characters.");
            return value.substr(a, b - a + 1);
        };
        auto change = [&](bool rename) {
            try {
                auto n = valid_name();
                std::string lower = n;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                for (size_t i = 0; i < s.lists.size(); ++i) {
                    std::string old = s.lists[i].name;
                    std::transform(old.begin(), old.end(), old.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    if (old == lower && (!rename || int(i) != s.selected_list))
                        throw std::runtime_error("List name already exists.");
                }
                if (rename)
                    s.lists[size_t(s.selected_list)].name = n;
                else {
                    s.lists.push_back({n, {}});
                    s.selected_list = int(s.lists.size() - 1);
                }
                name[0] = 0;
                error.clear();
            } catch (const std::exception &e) {
                error = e.what();
            }
        };
        if (ImGui::Button("Create"))
            change(false);
        ImGui::SameLine();
        if (ImGui::Button("Rename current"))
            change(true);
        ImGui::BeginDisabled(s.lists.size() <= 1);
        if (ImGui::Button("Delete current list")) {
            s.lists.erase(s.lists.begin() + s.selected_list);
            s.selected_list = std::min(s.selected_list, int(s.lists.size() - 1));
        }
        ImGui::EndDisabled();
        if (!error.empty())
            text(gold, error);
        ImGui::TextUnformatted("Right-click a ticker to reorder or remove it.");
        ImGui::EndPopup();
    }
    static char input[64]{};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##symbol", "+ Add to list / Enter", input, sizeof(input),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        try {
            auto symbol = normalize_symbol(input);
            auto &symbols = s.lists[size_t(s.selected_list)].symbols;
            if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end())
                symbols.push_back(symbol);
            s.select(s.current(), symbol, s.current().tf);
            input[0] = 0;
            s.notice.clear();
        } catch (const std::exception &e) {
            s.notice = e.what();
        }
    }
    // Context actions may mutate the list while rows are being drawn.
    auto symbols = s.lists[size_t(s.selected_list)].symbols;
    for (auto &symbol : symbols)
        row(s, symbol, false);
    if (!s.notice.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(0);
        text(gold, s.notice);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
static bool edit_color(const char *label, uint32_t &color) {
    auto value = ImGui::ColorConvertU32ToFloat4(color);
    float rgb[] = {value.x, value.y, value.z};
    if (!ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs))
        return false;
    color = ImGui::ColorConvertFloat4ToU32({rgb[0], rgb[1], rgb[2], 1});
    return true;
}
static void indicators(Panel &p) {
    auto available = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize({std::min(440.f, available.x - 16), std::min(550.f, available.y - 16)},
                             ImGuiCond_Always);
    if (!ImGui::BeginPopup("Indicators"))
        return;
    ImGui::Text("Indicators / %s", display_symbol(p.symbol).c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search indicators", p.search, sizeof(p.search));
    std::string query = p.search;
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    for (int i = 0; i < 7; ++i) {
        std::string name = std::string(kinds[i]) + " " + names[i];
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (name.find(query) == std::string::npos)
            continue;
        if (ImGui::Selectable((std::string("+ ") + names[i]).c_str(), false,
                              ImGuiSelectableFlags_NoAutoClosePopups)) {
            p.indicators.push_back(next_indicator(kinds[i], p.indicators));
            p.dirty = true;
        }
    }
    if (std::string("volume").find(query) != std::string::npos)
        ImGui::Checkbox("Volume", &p.volume);
    ImGui::Separator();
    text(muted, "On this chart / expand to edit");
    int remove = -1;
    for (size_t i = 0; i < p.indicators.size(); ++i) {
        auto &s = p.indicators[i];
        ImGui::PushID(int(i));
        p.dirty |= ImGui::Checkbox("##enabled", &s.enabled);
        ImGui::SameLine();
        auto name = s.kind == "RIBBON" ? "MA ribbon colored bars" : s.kind + " " + std::to_string(s.period);
        bool open = ImGui::TreeNode((name + "###settings").c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            remove = int(i);
        if (open) {
            ImGui::PushItemWidth(140);
            if (s.kind == "RIBBON") {
                p.dirty |= ImGui::Checkbox("Colored bars", &s.colored_bars);
                p.dirty |= ImGui::Checkbox("Show moving averages", &s.show_emas);
                p.dirty |= ImGui::Checkbox("Background coloring", &s.background);
                if (ImGui::TreeNode("Moving average colors")) {
                    const char *labels[] = {"20 EMA", "50 EMA", "100 EMA", "150 EMA", "200 EMA"};
                    for (size_t c = 0; c < 5; ++c)
                        p.dirty |= edit_color(labels[c], s.ma_colors[c]);
                    ImGui::TreePop();
                }
                const char *options[] = {"Same as chart", "1h", "4h", "1D", "1W"};
                p.dirty |= ImGui::Combo("MA timeframe", &s.timeframe, options, 5);
                ImGui::TextWrapped("EMA 20 / 50 / 100 / 150 / 200. Historical higher-timeframe values appear "
                                   "only when their source candle completes.");
            } else {
                if (s.kind == "SMA" || s.kind == "EMA")
                    p.dirty |= edit_color("Line color", s.color);
                p.dirty |= ImGui::InputInt(s.kind == "MACD" ? "Fast period" : "Period", &s.period);
                s.period = std::clamp(s.period, 1, 500);
                if (s.kind == "MACD") {
                    p.dirty |= ImGui::InputInt("Slow period", &s.slow);
                    p.dirty |= ImGui::InputInt("Signal", &s.signal);
                    s.slow = std::clamp(s.slow, 1, 500);
                    s.signal = std::clamp(s.signal, 1, 500);
                }
                if (s.kind == "BB") {
                    p.dirty |= ImGui::InputDouble("Deviation", &s.deviation, .1, .5, "%.1f");
                    s.deviation = std::clamp(s.deviation, .1, 10.);
                }
            }
            ImGui::PopItemWidth();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        p.indicators.erase(p.indicators.begin() + remove);
        p.dirty = true;
    }
    ImGui::EndPopup();
}
static void toolbar(State &s, Panel &p) {
    text(ink, display_symbol(p.symbol) + (p.bars.empty() ? "" : "  " + fmt(p.bars.back().close)));
    ImGui::SameLine();
    int tf = p.tf;
    ImGui::SetNextItemWidth(64);
    if (ImGui::Combo("##tf", &tf, timeframes, 4))
        s.select(p, p.symbol, tf);
    ImGui::SameLine();
    if (ImGui::Button("Latest"))
        p.view.fit(p.bars.size());
    auto &series = s.ensure(p.symbol, p.tf);
    std::string updated = "Updated --";
    if (series.loaded && series.history.fetched > 0)
        updated = "Updated " + date(series.history.fetched, "%Y-%m-%d %H:%M:%S") + " UTC";
    float updated_x =
        ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(updated.c_str()).x;
    float last_right = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    if (updated_x >= last_right + ImGui::GetStyle().ItemSpacing.x)
        ImGui::SameLine(updated_x);
    text(muted, updated);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Last successful chart data refresh.");
        if (series.loaded) {
            auto time = series.history.meta.find("regularMarketTime");
            if (time != series.history.meta.end() && time->is_number_integer() && time->get<Time>() > 0)
                ImGui::Text("Yahoo price as of %s UTC", date(time->get<Time>(), "%Y-%m-%d %H:%M:%S").c_str());
        }
        if (s.offline)
            ImGui::TextUnformatted("Showing offline cached data.");
        else if (series.loading)
            ImGui::TextUnformatted("Refreshing...");
        else if (!series.error.empty())
            ImGui::TextUnformatted("Last refresh failed; showing cached data.");
        ImGui::EndTooltip();
    }
    if (ImGui::Button("Indicators"))
        ImGui::OpenPopup("Indicators");
    indicators(p);
    ImGui::SameLine();
    if (ImGui::Button("View"))
        ImGui::OpenPopup("View");
    ImGui::SameLine();
    ImGui::BeginDisabled(series.loading || s.offline);
    if (ImGui::Button("Refresh"))
        s.refresh(p);
    ImGui::EndDisabled();
    drawing_toolbar(s, p);
    if (ImGui::BeginPopup("View")) {
        int style = !p.candles ? 2 : p.ohlc ? 1 : 0;
        const char *styles[] = {"Candlesticks", "OHLC bars", "Line"};
        if (ImGui::Combo("Chart style", &style, styles, 3)) {
            p.candles = style != 2;
            p.ohlc = style == 1;
        }
        int scale = p.logarithmic ? 1 : 0;
        const char *scales[] = {"Linear", "Logarithmic"};
        if (ImGui::Combo("Price scale", &scale, scales, 2))
            p.logarithmic = scale == 1;
        ImGui::Checkbox("Volume", &p.volume);
        ImGui::Separator();
        ImGui::TextUnformatted("Drag, horizontal scroll or Shift-scroll: pan.");
        ImGui::TextUnformatted("Scroll: zoom time around the cursor.");
        ImGui::TextUnformatted("Prices always fit visible candles and overlays.");
        ImGui::TextUnformatted("Arrows / A, D: pan. Home / End: oldest / latest.");
        ImGui::TextUnformatted("Double-click: latest. Pan beyond either end of history.");
        ImGui::Separator();
        ImGui::TextUnformatted("Yahoo snapshots / active series poll every 60 seconds.");
        ImGui::TextUnformatted("Sidebar changes are versus the previous daily close.");
        ImGui::TextUnformatted("4h candles use UTC buckets. Futures use Yahoo =F series.");
        ImGui::TextUnformatted("Crypto minute-rebuilt candles have unavailable volume.");
        if (series.loaded)
            ImGui::Text("Fetched %s UTC", date(series.history.fetched).c_str());
        ImGui::EndPopup();
    }
}
static void chart(State &s, Panel &p) {
    auto size = ImGui::GetContentRegionAvail();
    size.y -= 28;
    if (size.x < 260 || size.y < 190) {
        ImGui::TextWrapped("Enlarge this panel to display the chart.");
        return;
    }
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    auto &io = ImGui::GetIO();
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride), active = ImGui::IsItemActive();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bg);
    float left = origin.x + 16, right = origin.x + size.x - 102, top = origin.y + 26,
          foot = origin.y + size.y - 28;
    int pane_count = p.volume ? 1 : 0;
    for (auto &r : p.results)
        pane_count += r.pane;
    float ph = pane_count ? std::min(100.f, (size.y - 68) * .48f / pane_count) : 0;
    float bottom = std::max(top + 50, foot - 14 - pane_count * ph), pw = right - left;
    float mx = io.MousePos.x, my = io.MousePos.y;
    bool over = hovered && mx >= left && mx <= right && my >= top && my <= foot;
    auto initial_limits = price_limits(p.bars, p.view, p.results, p.logarithmic);
    DrawingPlot drawing_plot{left,
                             right,
                             top,
                             bottom,
                             initial_limits.first,
                             initial_limits.second,
                             p.logarithmic && initial_limits.first > 0};
    bool drawing_claimed = drawing_input(s, p, drawing_plot, over && my <= bottom);
    ScrollMotion motion;
    if (over && !drawing_claimed)
        motion = p.scroll.update(io.MouseWheelH, io.MouseWheel, io.KeyShift, ImGui::GetTime());
    bool zooming = motion.zoom != 0;
    if (over && !drawing_claimed) {
        p.view.pan(-motion.pan * p.view.count * .06);
        if (zooming)
            p.view.zoom(p.bars.size(), motion.zoom, (mx - left) / pw);
        if (ImGui::IsMouseDoubleClicked(0))
            p.view.fit(p.bars.size());
    }
    if (active) {
        s.active = p.id;
        if (!drawing_claimed && !zooming && ImGui::IsMouseDragging(0, 0))
            p.view.pan(-io.MouseDelta.x / pw * p.view.count);
    }
    if ((hovered || ImGui::IsItemFocused()) && !io.WantTextInput && !drawing_claimed && !io.KeyCtrl &&
        !io.KeySuper) {
        for (auto key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End})
            ImGui::SetKeyOwner(key, ImGui::GetItemID());
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_A))
            p.view.pan(-std::max(1., p.view.count * .1));
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_D))
            p.view.pan(std::max(1., p.view.count * .1));
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            p.view.first = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            p.view.fit(p.bars.size());
    }
    // Fit AFTER applying all time navigation. No manual price range is persisted.
    auto [lo, hi] = price_limits(p.bars, p.view, p.results, p.logarithmic);
    bool log_scale = p.logarithmic && lo > 0;
    if (p.drawing.gesture) {
        log_scale = p.drawing.logarithmic;
        lo = p.drawing.low;
        hi = p.drawing.high;
    }
    drawing_plot.low = lo;
    drawing_plot.high = hi;
    drawing_plot.logarithmic = log_scale;
    PriceScale scale{lo, hi, log_scale};
    auto [a, b] = p.view.visible(p.bars.size());
    auto px = [&](int i) { return left + float((i - p.view.first + .5) / p.view.count) * pw; };
    auto py = [&](double v) { return bottom - float(scale.fraction(v)) * (bottom - top); };
    auto label = [&](float x, float y, const std::string &t, ImU32 color = muted) {
        draw->AddText({x, y}, color, t.c_str());
    };
    auto line = [&](float x, float y, float x2, float y2, ImU32 c, float w = 1) {
        if (std::isfinite(y) && std::isfinite(y2))
            draw->AddLine({x, y}, {x2, y2}, c, w);
    };
    auto g = log_scale ? log_price_grid(lo, hi, bottom - top) : price_grid(lo, hi, bottom - top);
    double latest = p.bars.back().close;
    float ly = latest >= lo && latest <= hi ? py(latest) : -1e6f;
    for (auto price : g.levels) {
        float y = py(price);
        bool major = log_scale ? std::abs(std::log10(price) - std::round(std::log10(price))) < 1e-9
                               : std::abs(price / g.major - std::round(price / g.major)) < 1e-9;
        line(left, y, right, y, major ? rgba(46, 59, 71) : grid);
        if (std::abs(y - ly) > 22 && !(over && my <= bottom && std::abs(y - my) < 22))
            label(right + 10, y - 8,
                  price_label(price, log_scale && std::log10(hi) - std::log10(lo) >= 1 ? price : g.step),
                  major ? ink : muted);
    }
    float cw = std::max(1.f, pw / float(p.view.count) * .68f);
    auto bar_color = [&](int i) {
        ImU32 c = p.bars[i].close >= p.bars[i].open ? up : down;
        for (auto &r : p.results)
            if (r.colored_bars && !r.scores.empty() && std::isfinite(r.scores[i]))
                c = ribbon_color(r.scores[i]);
        return c;
    };
    auto curve = [&](const std::vector<double> &values, auto y, ImU32 color, float width = 1.4f) {
        std::vector<ImVec2> points;
        points.reserve(size_t(b - a + 2));
        auto flush = [&]() {
            if (points.size() > 1)
                draw->AddPolyline(points.data(), int(points.size()), color, 0, width);
            points.clear();
        };
        for (int i = std::max(0, a - 1); i < std::min(int(values.size()), b + 1); ++i) {
            if (std::isfinite(values[i]) && std::isfinite(y(values[i])))
                points.push_back({px(i), y(values[i])});
            else
                flush();
        }
        flush();
    };
    draw->PushClipRect({left, top}, {right, bottom}, true);
    for (auto &r : p.results)
        if (r.background && !r.scores.empty())
            for (int i = a; i < b; ++i)
                if (std::abs(r.scores[i]) == 4) {
                    float w = pw / float(p.view.count);
                    draw->AddRectFilled({px(i) - w / 2, top}, {px(i) + w / 2, bottom},
                                        r.scores[i] > 0 ? rgba(0, 128, 0, 26) : rgba(255, 0, 0, 26));
                }
    for (int i = a; i < b; ++i) {
        auto &bar = p.bars[i];
        auto c = bar_color(i);
        if (p.candles) {
            line(px(i), py(bar.high), px(i), py(bar.low), c);
            if (p.ohlc) {
                line(px(i) - cw / 2, py(bar.open), px(i), py(bar.open), c, 1.5f);
                line(px(i), py(bar.close), px(i) + cw / 2, py(bar.close), c, 1.5f);
                continue;
            }
            float y = std::min(py(bar.open), py(bar.close)),
                  height = std::max(1.f, std::abs(py(bar.open) - py(bar.close)));
            draw->AddRectFilled({px(i) - cw / 2, y}, {px(i) + cw / 2, y + height}, c);
        } else if (i > 0)
            line(px(i - 1), py(p.bars[i - 1].close), px(i), py(bar.close), c, 1.5);
    }
    for (auto &r : p.results)
        if (!r.pane)
            for (size_t j = 0; j < r.lines.size(); ++j)
                curve(r.lines[j], py, r.colors[j]);
    draw->PopClipRect();
    std::string legend = p.logarithmic ? (log_scale ? "Log" : "Linear (log needs positive prices)") : "";
    for (auto &r : p.results)
        if (!r.pane) {
            if (!legend.empty())
                legend += " / ";
            legend += r.name;
        }
    draw->PushClipRect(origin, {right, top}, true);
    label(left + 4, origin.y + 3, legend, blue);
    draw->PopClipRect();
    if (p.volume) {
        label(left + 4, bottom + 2, "Volume");
        double maxvol = 0;
        bool gaps = false;
        for (int i = a; i < b; ++i) {
            if (std::isfinite(p.bars[i].volume))
                maxvol = std::max(maxvol, p.bars[i].volume);
            else
                gaps = true;
        }
        if (gaps)
            label(left + 70, bottom + 2, "/ gaps unavailable");
        draw->PushClipRect({left, bottom + 18}, {right, bottom + ph}, true);
        if (maxvol > 0)
            for (int i = a; i < b; ++i)
                if (std::isfinite(p.bars[i].volume)) {
                    float y = bottom + ph - float(p.bars[i].volume / maxvol) * (ph - 18);
                    draw->AddRectFilled({px(i) - cw / 2, y}, {px(i) + cw / 2, bottom + ph},
                                        (bar_color(i) & 0x00ffffffu) | 0x66000000u);
                }
        draw->PopClipRect();
    }
    int pane = p.volume ? 1 : 0;
    for (auto &r : p.results)
        if (r.pane) {
            float pt = bottom + pane * ph + 18, pb = bottom + (pane + 1) * ph - 4;
            bool rsi = r.name.starts_with("RSI");
            double low = 0, high = rsi ? 100 : 0;
            if (!rsi) {
                for (auto &values : r.lines)
                    for (int i = a; i < b; ++i)
                        if (std::isfinite(values[i])) {
                            low = std::min(low, values[i]);
                            high = std::max(high, values[i]);
                        }
                for (int i = a; i < b && !r.histogram.empty(); ++i)
                    if (std::isfinite(r.histogram[i])) {
                        low = std::min(low, r.histogram[i]);
                        high = std::max(high, r.histogram[i]);
                    }
                if (high <= low)
                    high = low + 1;
                double pad = (high - low) * .08;
                low -= pad;
                high += pad;
            }
            auto y = [&](double v) { return pb - float((v - low) / (high - low)) * std::max(1.f, pb - pt); };
            line(left, pt - 16, right, pt - 16, grid);
            label(left + 4, pt - 16, r.name);
            for (double level : {rsi ? 30. : low, rsi ? 70. : high}) {
                line(left, y(level), right, y(level), grid);
                label(right + 10, y(level) - 8, fmt(level));
            }
            draw->PushClipRect({left, pt}, {right, pb}, true);
            for (int i = a; i < b && !r.histogram.empty(); ++i) {
                auto v = r.histogram[i];
                if (!std::isfinite(v))
                    continue;
                float top_y = std::min(y(0), y(v));
                draw->AddRectFilled({px(i) - cw / 2, top_y},
                                    {px(i) + cw / 2, top_y + std::max(1.f, std::abs(y(v) - y(0)))},
                                    v >= 0 ? up : down);
            }
            for (size_t j = 0; j < r.lines.size(); ++j)
                curve(r.lines[j], y, r.colors[j]);
            draw->PopClipRect();
            ++pane;
        }
    if (ly >= top && ly <= bottom) {
        draw->AddRectFilled({right + 1, ly - 11}, {right + 100, ly + 12}, rgba(33, 64, 94));
        label(right + 7, ly - 9, fmt(latest), ink);
    }
    int tick_count = std::clamp(int(pw / 150), 2, 6), prior = -1;
    if (a < b)
        for (int t = 0; t < tick_count; ++t) {
            int i = a + int(std::round(double(b - a - 1) * t / (tick_count - 1)));
            if (i == prior)
                continue;
            prior = i;
            label(std::clamp(px(i) - 35, left, right - 95), foot + 3,
                  date(p.bars[i].time, p.tf < 2 ? "%m-%d %H:%M" : "%Y-%m-%d"));
        }
    render_drawings(s, p, drawing_plot);
    int hover = -1;
    if (over) {
        hover = int(std::floor(p.view.first + (mx - left) / pw * p.view.count));
        float cross_x = hover >= 0 && hover < int(p.bars.size()) ? px(hover) : mx;
        if (cross_x >= left && cross_x <= right)
            line(cross_x, top, cross_x, foot - 8, muted);
        if (my <= bottom) {
            line(left, my, right, my, muted);
            double price = scale.price((bottom - my) / (bottom - top));
            draw->AddRectFilled({right + 1, my - 11}, {right + 100, my + 12}, grid);
            label(right + 7, my - 9, fmt(price), ink);
        }
    }
    auto hint = drawing_hint(p);
    if (!hint.empty()) {
        text(gold, hint);
    } else if (hover >= 0 && hover < int(p.bars.size())) {
        auto bar = p.bars[hover];
        text(muted, date(bar.time) + " UTC   O " + fmt(bar.open) + "   H " + fmt(bar.high) + "   L " +
                        fmt(bar.low) + "   C " + fmt(bar.close) + "   V " + fmt(bar.volume, 0));
    } else {
        auto &e = s.ensure(p.symbol, p.tf);
        std::string status = s.offline ? "Offline cache" : e.loading ? "Refreshing..." : "Auto 60s";
        if (e.history.meta.contains("chartroomMinuteRecoveredTimes") &&
            !e.history.meta["chartroomMinuteRecoveredTimes"].empty())
            status += " / minute-rebuilt candles";
        if (e.history.meta.contains("chartroomHourlyRecoveredTimes") &&
            !e.history.meta["chartroomHourlyRecoveredTimes"].empty())
            status += " / hourly-rebuilt candles";
        text(muted, "Yahoo / " + e.history.currency + " / " + status + " / Bar opened " +
                        date(p.bars.back().time) + " UTC");
    }
}
void frame(State &s, bool update) {
    if (update)
        s.tick();
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (s.relayout)
        layout(s, dock);
    watchlist(s);
    for (auto &ptr : s.panels) {
        auto &p = *ptr;
        bool opened = ImGui::Begin(title(p, s).c_str(), s.panels.size() > 1 ? &p.open : nullptr,
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoCollapse);
        if (opened) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                s.active = p.id;
            toolbar(s, p);
            s.update(p);
            auto &e = s.ensure(p.symbol, p.tf);
            if (!e.error.empty()) {
                text(gold, p.bars.empty() ? "Data unavailable / " + e.error
                                          : "Refresh failed / showing cached history");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", e.error.c_str());
            }
            if (e.loaded) {
                auto warning = e.history.meta.value("chartroomRecoveryError", std::string{});
                if (!warning.empty()) {
                    text(gold, "Some recent candles could not be recovered");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", warning.c_str());
                }
            }
            for (auto &ind : p.indicators)
                if (ind.enabled && ind.kind == "RIBBON" && ind.timeframe > 0) {
                    auto &dep = s.ensure(p.symbol, ind.timeframe - 1);
                    if (!dep.loaded) {
                        text(muted, dep.error.empty() ? "Loading indicator timeframe..." : dep.error);
                        break;
                    }
                }
            if (p.bars.empty())
                ImGui::TextUnformatted(s.offline ? "No cached history. Start online to load Yahoo data."
                                                 : "Loading Yahoo Finance history...");
            else
                chart(s, p);
        }
        ImGui::End();
    }
    drawing_tools_window(s);
    s.remove_closed();
    if (ImGui::GetIO().WantSaveIniSettings) {
        s.ini = ImGui::SaveIniSettingsToMemory();
        ImGui::GetIO().WantSaveIniSettings = false;
    }
    s.request_save();
    s.save();
}
} // namespace cr
