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
void market_windows(State &);
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
static std::string watchlist_title(const State &s, const WatchlistWindow &w) {
    return s.lists[size_t(w.list)].name + " / Watchlist###watchlist_" + std::to_string(w.id);
}
static void layout(State &s, ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID main = dock, left = 0;
    if (!s.watchlists.empty()) {
        ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, .18f, &left, &main);
        for (size_t i = 0; i < s.watchlists.size(); ++i) {
            ImGuiID slot = left;
            if (i + 1 < s.watchlists.size())
                ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 1.f / float(s.watchlists.size() - i), &slot,
                                            &left);
            ImGui::DockBuilderDockWindow(watchlist_title(s, *s.watchlists[i]).c_str(), slot);
        }
    }
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
static void row(State &s, const std::string &symbol, int list_index) {
    ImGui::PushID(symbol.c_str());
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
        if (ImGui::MenuItem("Options")) {
            s.options.symbol = symbol;
            s.options.open = s.options.focus = true;
            s.refresh_options();
        }
        auto &list = s.lists[size_t(list_index)];
        auto it = std::find(list.symbols.begin(), list.symbols.end(), symbol);
        if (ImGui::BeginMenu("Copy to list")) {
            for (size_t i = 0; i < s.lists.size(); ++i)
                if (int(i) != list_index && ImGui::MenuItem(s.lists[i].name.c_str())) {
                    auto &symbols = s.lists[i].symbols;
                    if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end())
                        symbols.push_back(symbol);
                }
            ImGui::EndMenu();
        }
        if (it != list.symbols.end()) {
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
    float change_x = std::max(60.f, right - ImGui::CalcTextSize(label.c_str()).x - 2);
    if (it != s.quotes.end() && fresh_extended(it->second, now()) && change_x > 115) {
        ImGui::SameLine(change_x - 40);
        text(gold, it->second.extended->session);
        hovered |= ImGui::IsItemHovered();
    }
    ImGui::SameLine(change_x);
    text(color, label);
    if (hovered || ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(symbol.c_str());
        if (it != s.quotes.end()) {
            text(ink,
                 fmt(it->second.price) + " / " + label +
                     (it->second.rolling ? " over 24 hours" : " regular session / versus previous close"));
            text(muted, it->second.source + " / As of " + local_date(it->second.asof));
            if (it->second.extended) {
                auto &e = *it->second.extended;
                auto change = (e.price / it->second.price - 1) * 100;
                text(gold, e.session + " " + fmt(e.price) + " / " + (change >= 0 ? "+" : "") + fmt(change) +
                               "% versus regular close");
                text(muted, e.source + " / " + local_date(e.asof) +
                                (fresh_extended(it->second, now()) ? "" : " / stale"));
            }
        }
        if (s.quote_errors.count(symbol))
            text(gold, s.quote_errors[symbol]);
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}
static void add_panel_menu(State &s) {
    if (ImGui::MenuItem("Chart")) {
        auto &p = s.add();
        s.focus_chart = p.id;
    }
    if (ImGui::MenuItem("Options chain")) {
        s.options.open = s.options.focus = true;
        s.options.symbol = s.current().symbol;
        s.options.target_chart = s.current().id;
        s.refresh_options();
    }
    if (ImGui::MenuItem("Screener")) {
        s.screener.open = s.screener.focus = true;
        s.refresh_screener();
    }
    if (ImGui::BeginMenu("Watchlist")) {
        for (size_t i = 0; i < s.lists.size(); ++i)
            if (ImGui::MenuItem(s.lists[i].name.c_str()))
                s.add_watchlist(int(i));
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Fundamentals")) {
        s.fundamentals.open = s.fundamentals.focus = true;
        s.fundamentals.follow = true;
    }
}
static void main_menu(State &s) {
    auto &io = ImGui::GetIO();
    if (!io.WantTextInput && (io.KeyCtrl || io.KeySuper) && io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_T, false) && !s.closed_charts.empty())
        s.reopen_chart(s.closed_charts.size() - 1);
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("Workspace")) {
        if (ImGui::MenuItem("Save workspace")) {
            s.ini = ImGui::SaveIniSettingsToMemory();
            s.save(true);
        }
        ImGui::Separator();
        ImGui::TextDisabled("Changes are saved automatically");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Panels")) {
        if (ImGui::BeginMenu("Add panel")) {
            add_panel_menu(s);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Duplicate active chart")) {
            auto &p = s.add("", true);
            s.focus_chart = p.id;
        }
        if (ImGui::MenuItem("Close active chart", nullptr, false, s.panels.size() > 1))
            s.current().open = false;
        if (ImGui::MenuItem("Reopen closed chart", "Ctrl/Cmd+Shift+T", false, !s.closed_charts.empty()))
            s.reopen_chart(s.closed_charts.size() - 1);
        if (ImGui::BeginMenu("Recently closed", !s.closed_charts.empty())) {
            for (size_t i = s.closed_charts.size(); i-- > 0;) {
                auto &c = s.closed_charts[i];
                auto label = display_symbol(c.at("symbol")) + " " + timeframes[c.at("timeframe").get<int>()] +
                             " / Chart " + std::to_string(c.at("id").get<int>());
                if (ImGui::MenuItem(label.c_str())) {
                    s.reopen_chart(i);
                    break;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        for (auto &p : s.panels) {
            auto label =
                display_symbol(p->symbol) + " " + timeframes[p->tf] + " / Chart " + std::to_string(p->id);
            if (ImGui::MenuItem(label.c_str(), nullptr, p->id == s.active))
                s.active = s.focus_chart = p->id;
        }
        for (auto &w : s.watchlists) {
            auto label = s.lists[size_t(w->list)].name + " / Watchlist " + std::to_string(w->id);
            if (ImGui::MenuItem(label.c_str()))
                w->focus = true;
        }
        if (ImGui::MenuItem("Options", nullptr, s.options.open))
            s.options.open = s.options.focus = true;
        if (ImGui::MenuItem("Screener", nullptr, s.screener.open))
            s.screener.open = s.screener.focus = true;
        if (ImGui::MenuItem("Drawing tools", nullptr, s.drawing_tools_open))
            s.drawing_tools_open = s.drawing_tools_focus = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layout")) {
        for (auto name : {"Grid", "Columns", "Rows", "Tabs"})
            if (ImGui::MenuItem(name, nullptr, s.layout == name)) {
                s.layout = name;
                s.relayout = true;
            }
        if (ImGui::MenuItem("Reset chart layout"))
            s.relayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::TextUnformatted("Scroll: zoom. Drag or horizontal swipe: pan.");
        ImGui::TextUnformatted("Options: hover a strike to preview, click to pin.");
        ImGui::TextUnformatted("Panels lists windows and the last 20 closed charts.");
        ImGui::Text("Chartroom %s", build::version);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("+ Add panel")) {
        add_panel_menu(s);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}
static void watchlist(State &s, WatchlistWindow &w) {
    ImGui::SetNextWindowSize({300, 520}, ImGuiCond_FirstUseEver);
    if (w.focus) {
        ImGui::SetNextWindowFocus();
        w.focus = false;
    }
    if (!ImGui::Begin(watchlist_title(s, w).c_str(), &w.open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        s.selected_list = w.list;
    const bool newer = s.available_build > build::number;
    text(newer ? gold : muted, std::string("v") + build::version);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Built %s", local_date(parse_time(build::timestamp)).c_str());
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
    auto &jump = w.jump;
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
    ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x - 55));
    if (ImGui::BeginCombo("##list", s.lists[size_t(w.list)].name.c_str())) {
        for (size_t i = 0; i < s.lists.size(); ++i)
            if (ImGui::Selectable(s.lists[i].name.c_str(), int(i) == w.list))
                s.selected_list = w.list = int(i);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Lists"))
        ImGui::OpenPopup("Lists");
    if (ImGui::BeginPopup("Lists")) {
        auto &name = w.name;
        auto &error = w.error;
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
                    if (old == lower && (!rename || int(i) != w.list))
                        throw std::runtime_error("List name already exists.");
                }
                if (rename)
                    s.lists[size_t(w.list)].name = n;
                else {
                    s.lists.push_back({n, {}});
                    w.list = int(s.lists.size() - 1);
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
        if (ImGui::Button("Open list in another window"))
            s.add_watchlist(w.list);
        ImGui::BeginDisabled(s.lists.size() <= 1);
        if (ImGui::Button("Delete current list")) {
            s.delete_list(w.list);
        }
        ImGui::EndDisabled();
        if (!error.empty())
            text(gold, error);
        ImGui::TextUnformatted("Right-click a ticker to reorder or remove it.");
        ImGui::EndPopup();
    }
    auto &input = w.input;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##symbol", "+ Add to list / Enter", input, sizeof(input),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        try {
            auto symbol = normalize_symbol(input);
            auto &symbols = s.lists[size_t(w.list)].symbols;
            if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end())
                symbols.push_back(symbol);
            s.select(s.current(), symbol, s.current().tf);
            input[0] = 0;
            s.notice.clear();
        } catch (const std::exception &e) {
            s.notice = e.what();
        }
    }
    if (s.lists[size_t(w.list)].name == "Macro & economic proxies") {
        ImGui::TextDisabled("Market proxies");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Prices for rates, inflation protection, credit, commodities and volatility.\nThese are "
                "market proxies, not economic releases such as CPI or jobs data.");
    }
    ImGui::Separator();
    text(muted, "Symbol");
    ImGui::SameLine(std::max(70.f, ImGui::GetContentRegionAvail().x - 65));
    text(muted, "Chg %");
    // Context actions may mutate the list while rows are being drawn.
    auto symbols = s.lists[size_t(w.list)].symbols;
    for (auto &symbol : symbols)
        row(s, symbol, w.list);
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
    ImGui::BeginChild("catalog", {0, 190}, ImGuiChildFlags_Borders);
    for (size_t i = 0; i < std::size(kinds); ++i) {
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
    if (std::string("eps earnings fundamentals").find(query) != std::string::npos)
        p.dirty |= ImGui::Checkbox("EPS line (reported earnings)", &p.eps);
    ImGui::EndChild();
    if (p.eps)
        p.dirty |= ImGui::Checkbox("EPS: trailing four quarters", &p.eps_ttm);
    ImGui::Checkbox("Option strike ladder", &p.option_ladder);
    if (p.option_ladder)
        ImGui::Checkbox("Use option volume (otherwise open interest)", &p.ladder_volume);
    ImGui::Separator();
    text(muted, "On this chart / expand to edit");
    int remove = -1;
    for (size_t i = 0; i < p.indicators.size(); ++i) {
        auto &s = p.indicators[i];
        ImGui::PushID(int(i));
        p.dirty |= ImGui::Checkbox("##enabled", &s.enabled);
        ImGui::SameLine();
        auto name = s.kind == "RIBBON" ? "MA ribbon colored bars"
                    : s.kind == "OBV"  ? "OBV"
                                       : s.kind + " " + std::to_string(s.period);
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
                if (s.kind == "SMA" || s.kind == "EMA" || s.kind == "VWMA" || s.kind == "DONCHIAN" ||
                    s.kind == "KC" || s.kind == "STOCH" || s.kind == "ROC" || s.kind == "OBV")
                    p.dirty |= edit_color("Line color", s.color);
                if (s.kind != "OBV")
                    p.dirty |= ImGui::InputInt(s.kind == "MACD"     ? "Fast period"
                                               : s.kind == "PIVOTS" ? "N bars each side"
                                                                    : "Period",
                                               &s.period);
                if (s.kind == "PIVOTS") {
                    p.dirty |= ImGui::Checkbox("Swing highs", &s.show_highs);
                    ImGui::SameLine();
                    p.dirty |= edit_color("High color", s.color);
                    p.dirty |= ImGui::Checkbox("Swing lows", &s.show_lows);
                    ImGui::SameLine();
                    p.dirty |= edit_color("Low color", s.low_color);
                    ImGui::TextWrapped("Marks a high above (or low below) N bars on both sides. Equal prices "
                                       "are not pivots. Markers appear on the pivot bar only after the "
                                       "following N bars close; the newest N bars remain unconfirmed.");
                }
                if (s.kind == "DONCHIAN") {
                    p.dirty |= ImGui::Checkbox("Show midpoint", &s.midline);
                    p.dirty |= ImGui::Checkbox("Exclude current bar", &s.exclude_current);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Use the previous N bars, useful for comparing the current bar "
                                          "with a breakout level.");
                }
                if (s.kind == "OBV")
                    ImGui::TextWrapped("Cumulative signed volume, starting at zero. Missing volume leaves a "
                                       "gap rather than implying zero trading.");
                s.period = std::clamp(s.period, 1, 500);
                if (s.kind == "MACD") {
                    p.dirty |= ImGui::InputInt("Slow period", &s.slow);
                    p.dirty |= ImGui::InputInt("Signal", &s.signal);
                    s.slow = std::clamp(s.slow, 1, 500);
                    s.signal = std::clamp(s.signal, 1, 500);
                }
                if (s.kind == "STOCH" || s.kind == "KC") {
                    p.dirty |= ImGui::InputInt(s.kind == "KC" ? "ATR period" : "%K smoothing", &s.slow);
                    s.slow = std::clamp(s.slow, 1, 500);
                    if (s.kind == "STOCH") {
                        p.dirty |= ImGui::InputInt("%D smoothing", &s.signal);
                        s.signal = std::clamp(s.signal, 1, 500);
                    }
                }
                if (s.kind == "BB" || s.kind == "KC") {
                    p.dirty |= ImGui::InputDouble(s.kind == "KC" ? "ATR multiplier" : "Deviation",
                                                  &s.deviation, .1, .5, "%.1f");
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
        updated = "Updated " + local_date(series.history.fetched, "%Y-%m-%d %H:%M:%S %Z");
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
                ImGui::Text("Price as of %s", local_date(time->get<Time>(), "%Y-%m-%d %H:%M:%S %Z").c_str());
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
    ImGui::SameLine();
    if (ImGui::Button("Facts")) {
        s.active = p.id;
        s.fundamentals.open = s.fundamentals.focus = s.fundamentals.follow = true;
        s.fundamentals.symbol = p.symbol;
    }
    if (p.option_marker) {
        auto &m = *p.option_marker;
        text(muted, std::string(m.puts ? "Put " : "Call ") + fmt(m.strike) + " / " + m.expiry);
        ImGui::SameLine();
        if (ImGui::SmallButton("Show expiry") && !p.bars.empty()) {
            auto step = p.tf == 1 ? 14400 : duration(intervals[p.tf]);
            // Expiration dates are anchored at the day's start, not an assumed settlement hour.
            double index = drawing_index(p.bars, parse_time(m.expiry + "T00:00:00"), step);
            p.view.first = index - p.view.count * .7;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear option"))
            p.option_marker.reset();
    }
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
        ImGui::Checkbox("Earnings markers", &p.earnings);
        bool linked = s.option_target() == &p;
        ImGui::BeginDisabled(!linked);
        ImGui::Checkbox("Option strike ladder", &p.option_ladder);
        ImGui::EndDisabled();
        if (!linked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Choose this chart as the linked chart in Options first.");
        if (p.option_ladder)
            ImGui::Checkbox("Use option volume (otherwise open interest)", &p.ladder_volume);
        ImGui::Separator();
        ImGui::TextUnformatted("Drag, horizontal scroll or Shift-scroll: pan.");
        ImGui::TextUnformatted("Scroll: zoom time around the cursor.");
        ImGui::TextUnformatted("Prices always fit visible candles and overlays.");
        ImGui::TextUnformatted("Arrows / A, D: pan. Home / End: oldest / latest.");
        ImGui::TextUnformatted("Double-click: latest. Pan beyond either end of history.");
        ImGui::Separator();
        ImGui::TextUnformatted("Provider snapshots / active series poll every 60 seconds.");
        ImGui::TextUnformatted("Sidebar changes: previous close; Binance pairs: rolling 24h.");
        ImGui::TextUnformatted("4h candles use UTC buckets. Displayed times follow your local timezone. "
                               "Futures use Yahoo =F series.");
        ImGui::TextUnformatted("Crypto minute-rebuilt candles have unavailable volume.");
        if (series.loaded)
            ImGui::Text("Fetched %s", local_date(series.history.fetched).c_str());
        ImGui::EndPopup();
    }
}
static void option_overlay(State &s, Panel &p, const DrawingPlot &plot) {
    auto draw = ImGui::GetWindowDrawList();
    auto scale = plot.scale();
    auto py = [&](double price) {
        return plot.bottom - float(scale.fraction(price)) * (plot.bottom - plot.top);
    };
    if (p.option_ladder && s.option_target() == &p && s.options.symbol == p.symbol) {
        auto &o = s.options;
        float x = plot.right + 110, width = 74;
        draw->AddRectFilled({x, plot.top}, {x + width, plot.bottom}, rgba(17, 25, 34));
        const char *metric = p.ladder_volume ? "Volume" : "OI";
        draw->AddText({x, plot.top - 20}, muted, (std::string(o.puts ? "Put " : "Call ") + metric).c_str());
        double maximum = 0;
        for (auto &r : o.data.rows)
            if (r.expiry == o.expiry) {
                auto &side = o.puts ? r.put : r.call;
                double v = p.ladder_volume ? side.volume : side.interest;
                if (std::isfinite(v))
                    maximum = std::max(maximum, v);
            }
        draw->PushClipRect({x, plot.top}, {x + width, plot.bottom}, true);
        for (auto &r : o.data.rows)
            if (r.expiry == o.expiry && r.strike >= plot.low && r.strike <= plot.high) {
                auto &side = o.puts ? r.put : r.call;
                double v = p.ladder_volume ? side.volume : side.interest;
                if (!std::isfinite(v) || v <= 0 || maximum <= 0)
                    continue;
                float y = py(r.strike), w = std::max(1.f, float(v / maximum) * width);
                auto color = o.puts ? rgba(200, 128, 200, 135) : rgba(0, 160, 210, 135);
                draw->AddRectFilled({x, y - 2}, {x + w, y + 2}, color);
                if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect({x, y - 3}, {x + width, y + 3}))
                    ImGui::SetTooltip("%s %s / strike %.2f\n%s: %.0f contracts\nSnapshot %s",
                                      o.expiry.c_str(), o.puts ? "Put" : "Call", r.strike, metric, v,
                                      local_date(o.data.fetched).c_str());
            }
        if (maximum == 0)
            draw->AddText({x + 6, plot.top + 6}, muted, "No data");
        draw->PopClipRect();
    }
    auto marker = [&](const OptionMarker &m, bool preview) {
        Time step = p.tf == 1 ? 14400 : duration(intervals[p.tf]);
        double index = drawing_index(p.bars, parse_time(m.expiry + "T00:00:00"), step);
        float x = plot.left + float((index - p.view.first + .5) / p.view.count) * (plot.right - plot.left);
        float y = py(m.strike);
        if (!std::isfinite(x) || !std::isfinite(y))
            return;
        bool outside_x = x < plot.left || x > plot.right, outside_y = y < plot.top || y > plot.bottom;
        float cx = std::clamp(x, plot.left + 6, plot.right - 6),
              cy = std::clamp(y, plot.top + 6, plot.bottom - 6);
        ImU32 color =
            m.puts ? rgba(200, 128, 200, preview ? 155 : 240) : rgba(0, 160, 210, preview ? 155 : 240);
        ImU32 faint = (color & 0xffffff) | (uint32_t(preview ? 45 : 85) << 24);
        draw->PushClipRect({plot.left, plot.top}, {plot.right, plot.bottom}, true);
        if (!outside_x)
            draw->AddLine({x, plot.top}, {x, plot.bottom}, faint);
        if (!outside_y)
            draw->AddLine({plot.left, y}, {plot.right, y}, faint);
        if (outside_x) {
            float dx = x > plot.right ? -7.f : 7.f;
            draw->AddTriangleFilled({cx, cy}, {cx + dx, cy - 5}, {cx + dx, cy + 5}, color);
        } else if (outside_y) {
            float dy = y > plot.bottom ? -7.f : 7.f;
            draw->AddTriangleFilled({cx, cy}, {cx - 5, cy + dy}, {cx + 5, cy + dy}, color);
        } else {
            draw->AddQuadFilled({cx, cy - 5}, {cx + 5, cy}, {cx, cy + 5}, {cx - 5, cy}, color);
        }
        auto label = std::string(preview ? "Preview " : "") + (m.puts ? "P " : "C ") + fmt(m.strike) + " / " +
                     m.expiry + (outside_x || outside_y ? " (off-screen)" : "");
        float tw = ImGui::CalcTextSize(label.c_str()).x;
        float tx = std::clamp(cx + 9, plot.left + 3, std::max(plot.left + 3, plot.right - tw - 3));
        float ty = cy > plot.bottom - 28 ? cy - 23 : cy + 8;
        draw->AddRectFilled({tx - 2, ty - 1}, {tx + tw + 2, ty + 17}, rgba(11, 16, 23, 230));
        draw->AddText({tx, ty}, color, label.c_str());
        draw->PopClipRect();
    };
    if (p.option_marker)
        marker(*p.option_marker, false);
    if (s.option_preview && s.option_preview->chart == p.id) {
        auto &m = s.option_preview->marker;
        if (!p.option_marker || p.option_marker->expiry != m.expiry || p.option_marker->strike != m.strike ||
            p.option_marker->puts != m.puts)
            marker(m, true);
    }
}
static void chart(State &s, Panel &p) {
    auto size = ImGui::GetContentRegionAvail();
    size.y -= 28;
    bool ladder = p.option_ladder && s.option_target() == &p && s.options.symbol == p.symbol;
    if (size.x < (ladder ? 350 : 260) || size.y < 190) {
        ImGui::TextWrapped("Enlarge this panel to display the chart.");
        return;
    }
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    auto &io = ImGui::GetIO();
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride), active = ImGui::IsItemActive();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bg);
    float left = origin.x + 16, right = origin.x + size.x - 102 - (ladder ? 90 : 0), top = origin.y + 26,
          foot = origin.y + size.y - 28;
    int pane_count = p.volume ? 1 : 0;
    for (auto &r : p.results)
        pane_count += r.pane;
    float ph = pane_count ? std::min(100.f, (size.y - 68) * .48f / pane_count) : 0;
    float event_height = p.earnings && nasdaq_symbol(p.symbol) ? 24.f : 0.f;
    float bottom = std::max(top + 50, foot - 14 - event_height - pane_count * ph), pw = right - left;
    float mx = io.MousePos.x, my = io.MousePos.y;
    bool over = hovered && mx >= left && mx <= right && my >= top && my <= foot - event_height;
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
        if (over && !drawing_claimed && !zooming && ImGui::IsMouseDragging(0, 0))
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
    auto px = [&](double i) { return left + float((i - p.view.first + .5) / p.view.count) * pw; };
    auto py = [&](double v) { return bottom - float(scale.fraction(v)) * (bottom - top); };
    auto label = [&](float x, float y, const std::string &t, ImU32 color = muted) {
        draw->AddText({x, y}, color, t.c_str());
    };
    auto line = [&](float x, float y, float x2, float y2, ImU32 c, float w = 1) {
        if (std::isfinite(y) && std::isfinite(y2))
            draw->AddLine({x, y}, {x2, y2}, c, w);
    };
    auto time_ticks = time_grid(p.bars, p.view, p.tf == 1 ? 14400 : duration(intervals[p.tf]), pw, true);
    for (const auto &tick : time_ticks)
        line(px(tick.index), top, px(tick.index), foot - 8, tick.major ? rgba(46, 59, 71) : grid);
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
        if (!r.pane) {
            if (r.pivots) {
                for (int i = a; i < b; ++i)
                    for (size_t j = 0; j < r.lines.size(); ++j)
                        if (std::isfinite(r.lines[j][i])) {
                            float x = px(i), y = py(r.lines[j][i]), direction = j == 0 ? -1.f : 1.f;
                            draw->AddTriangleFilled({x, y + direction * 4}, {x - 4, y + direction * 11},
                                                    {x + 4, y + direction * 11}, r.colors[j]);
                        }
            } else
                for (size_t j = 0; j < r.lines.size(); ++j)
                    curve(r.lines[j], py, r.colors[j]);
        }
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
            bool rsi = r.name.starts_with("RSI"), stochastic = r.name.starts_with("STOCH");
            bool bounded = rsi || stochastic;
            double low = 0, high = bounded ? 100 : 0;
            if (!bounded) {
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
            for (double level : {rsi ? 30. : stochastic ? 20. : low, rsi ? 70. : stochastic ? 80. : high}) {
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
    if (event_height) {
        auto &f = s.company(p.symbol).data;
        struct Badge {
            float x;
            std::vector<const EarningsEvent *> events;
        };
        std::vector<Badge> badges;
        for (auto &e : f.earnings) {
            Time t = parse_time(e.day + "T00:00:00");
            auto bar = std::lower_bound(p.bars.begin(), p.bars.end(), t,
                                        [](const Bar &b, Time t) { return b.time < t; });
            double index = drawing_index(p.bars, t, p.tf == 1 ? 14400 : duration(intervals[p.tf]));
            if (p.tf == 2 && bar != p.bars.end() && date(bar->time, "%Y-%m-%d") == e.day)
                index = double(bar - p.bars.begin());
            float x = px(index);
            if (x < left + 10 || x > right - 10)
                continue;
            if (!badges.empty() && x - badges.back().x < 24)
                badges.back().events.push_back(&e);
            else
                badges.push_back({x, {&e}});
        }
        for (auto &badge : badges) {
            float x = badge.x, y = foot - 13;
            auto &e = *badge.events.back();
            ImU32 color = e.upcoming                   ? gold
                          : !std::isfinite(e.surprise) ? muted
                          : e.surprise >= 0            ? up
                                                       : down;
            draw->AddRectFilled({x - 9, y - 9}, {x + 9, y + 9}, bg);
            draw->AddRect({x - 9, y - 9}, {x + 9, y + 9}, color);
            label(x - 4, y - 8, badge.events.size() > 1 ? "+" : "E", color);
            if (hovered && std::abs(mx - x) <= 10 && std::abs(my - y) <= 10) {
                ImGui::BeginTooltip();
                for (auto event : badge.events) {
                    ImGui::Text("%s / %s", event->day.c_str(),
                                event->upcoming ? "Estimated earnings date" : "Earnings report");
                    if (!event->fiscal.empty())
                        ImGui::Text("Quarter ended %s", event->fiscal.c_str());
                    if (std::isfinite(event->actual))
                        ImGui::Text("EPS %.2f", event->actual);
                    if (std::isfinite(event->estimate))
                        ImGui::Text("Consensus %.2f", event->estimate);
                    if (std::isfinite(event->surprise))
                        ImGui::Text("Surprise %+.2f%%", event->surprise);
                    ImGui::Separator();
                }
                ImGui::TextUnformatted("Report time unavailable. Click for company facts.");
                ImGui::EndTooltip();
                if (ImGui::IsMouseClicked(0)) {
                    s.active = p.id;
                    s.fundamentals.symbol = p.symbol;
                    s.fundamentals.open = s.fundamentals.focus = s.fundamentals.follow = true;
                }
            }
        }
    }
    // Clip labels at the plot edges instead of shifting several onto the same position.
    draw->PushClipRect({left, foot}, {right, foot + 24}, true);
    for (const auto &tick : time_ticks) {
        float width = ImGui::CalcTextSize(tick.label.c_str()).x;
        float x = px(tick.index) - width / 2;
        if (x >= left && x + width <= right)
            label(x, foot + 3, tick.label, tick.major ? ink : muted);
    }
    draw->PopClipRect();
    auto quote_it = s.quotes.find(p.symbol);
    if (quote_it != s.quotes.end() && fresh_extended(quote_it->second, now())) {
        auto &e = *quote_it->second.extended;
        float actual_y = py(e.price);
        if (std::isfinite(actual_y)) {
            float y = std::clamp(actual_y, top + 12, bottom - 12);
            bool outside = actual_y < top || actual_y > bottom;
            if (!outside)
                for (float x = left; x < right; x += 7)
                    line(x, actual_y, std::min(x + 2, right), actual_y, gold);
            std::string caption =
                e.session + " " + fmt(e.price) + (outside ? (actual_y < top ? " ^" : " v") : "");
            // A distinct tag avoids overlapping the regular close marker on a quiet session.
            float width = ImGui::CalcTextSize(caption.c_str()).x;
            draw->AddRectFilled({right - width - 12, y - 11}, {right, y + 12}, rgba(113, 70, 21));
            label(right - width - 6, y - 9, caption, ink);
            if (hovered && ImGui::IsMouseHoveringRect({right - width - 12, y - 11}, {right, y + 12}))
                ImGui::SetTooltip("%s-market / %s\n%s snapshot; may be delayed. Excluded from candles "
                                  "and sidebar change.%s",
                                  e.session.c_str(), local_date(e.asof).c_str(), e.source.c_str(),
                                  outside ? " Price is outside the visible scale." : "");
        }
    }
    render_drawings(s, p, drawing_plot);
    option_overlay(s, p, drawing_plot);
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
        text(muted, local_date(bar.time) + "   O " + fmt(bar.open) + "   H " + fmt(bar.high) + "   L " +
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
        text(muted, data_source(e.history) + " / " + e.history.currency + " / " + status + " / Bar opened " +
                        local_date(p.bars.back().time));
    }
}
static void chart_placeholder(State &s, Panel &p, const Series &series) {
    ImVec2 origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    size.x = std::max(1.f, size.x);
    size.y = std::max(1.f, size.y);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bg);
    for (float x = origin.x + 48; x < origin.x + size.x; x += 96)
        draw->AddLine({x, origin.y}, {x, origin.y + size.y}, rgba(20, 28, 37));
    for (float y = origin.y + 48; y < origin.y + size.y; y += 64)
        draw->AddLine({origin.x, y}, {origin.x + size.x, y}, rgba(20, 28, 37));
    const bool failed = !series.loading && !series.error.empty();
    const std::string heading = s.offline ? "No cached history"
                                : failed  ? "History unavailable"
                                          : "Loading market history";
    const std::string symbol = display_symbol(p.symbol) + "  /  " + timeframes[p.tf];
    float center = origin.x + size.x / 2;
    float top = origin.y + std::max(12.f, (size.y - 200) / 2);
    // A static chart icon keeps loading legible without waking the idle render loop.
    ImU32 accent = failed || s.offline ? gold : rgba(0, 160, 210);
    for (int i = 0; i < 3; ++i) {
        float x = center - 22 + i * 22, y = top + (i == 1 ? 6 : 15);
        draw->AddLine({x, y}, {x, y + 42}, accent, 1.5f);
        draw->AddRectFilled({x - 5, y + 10}, {x + 5, y + 31}, accent);
    }
    auto centered = [&](const std::string &value, float y, ImU32 color) {
        draw->AddText({center - ImGui::CalcTextSize(value.c_str()).x / 2, y}, color, value.c_str());
    };
    centered(symbol, top + 72, ink);
    centered(heading, top + 100, failed ? gold : ink);
    const char *detail = s.offline ? "Start online to fetch this chart."
                         : failed  ? "Check your connection, then try again."
                                   : "Fetching prices and preparing your indicators.";
    centered(detail, top + 126, muted);
    if (failed && !s.offline) {
        ImGui::SetCursorScreenPos({center - 35, top + 154});
        if (ImGui::Button("Retry", {70, 0}))
            s.refresh(p);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", series.error.c_str());
    }
    draw->PopClipRect();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
}
void frame(State &s, bool update) {
    if (update)
        s.tick();
    main_menu(s);
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (s.relayout)
        layout(s, dock);
    const size_t watchlist_count = s.watchlists.size();
    for (size_t i = 0; i < watchlist_count; ++i)
        if (s.watchlists[i]->open)
            watchlist(s, *s.watchlists[i]);
    std::erase_if(s.watchlists, [](const auto &w) { return !w->open; });
    s.option_preview.reset();
    market_windows(s);
    for (auto &ptr : s.panels) {
        auto &p = *ptr;
        if (s.focus_chart == p.id) {
            ImGui::SetNextWindowFocus();
            s.focus_chart = 0;
        }
        bool opened = ImGui::Begin(title(p, s).c_str(), s.panels.size() > 1 ? &p.open : nullptr,
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoCollapse);
        if (opened) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                s.active = p.id;
            toolbar(s, p);
            s.update(p);
            auto &e = s.ensure(p.symbol, p.tf);
            if (!e.error.empty() && !p.bars.empty()) {
                text(gold, p.bars.empty() ? "Data unavailable / " + e.error
                                          : "Refresh failed / showing cached history");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", e.error.c_str());
            }
            if (e.loaded) {
                auto provider_notice = e.history.meta.value("chartroomProviderNotice", std::string{});
                if (!provider_notice.empty()) {
                    text(gold, "Current session unavailable / showing available history");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", provider_notice.c_str());
                }
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
                chart_placeholder(s, p, e);
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
