#include "app.hpp"
#include "build_version.hpp"
#include "render_schedule.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "drawing_ui.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cctype>
#include <cstdio>
#include <functional>
#include <optional>
namespace cr {
void market_windows(State &);
static constexpr ImU32 bg = rgba(11, 16, 23), grid = rgba(24, 32, 41), muted = rgba(117, 140, 163),
                       ink = rgba(219, 230, 240), surface = rgba(14, 20, 28), chrome = rgba(10, 14, 20),
                       line_color = rgba(33, 43, 55), accent = rgba(80, 150, 240), faint = rgba(78, 96, 116);
static ImU32 with_alpha(ImU32 c, int a) { return (c & 0x00ffffffu) | (uint32_t(a) << 24); }
static void dashed(ImDrawList *draw, ImVec2 a, ImVec2 b, ImU32 c, float dash = 4, float gap = 4) {
    float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0))
        return;
    dx /= length;
    dy /= length;
    for (float t = 0; t < length; t += dash + gap) {
        float e = std::min(length, t + dash);
        draw->AddLine({a.x + dx * t, a.y + dy * t}, {a.x + dx * e, a.y + dy * e}, c);
    }
}
// Rounded value tag on a price or time axis.
static void axis_tag(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 fill, ImU32 text_color,
                     const std::string &value) {
    draw->AddRectFilled(min, max, fill, 3);
    auto size = ImGui::CalcTextSize(value.c_str());
    draw->AddText({min.x + 6, (min.y + max.y - size.y) / 2}, text_color, value.c_str());
}
static std::string fmt(double n, int digits = 2) {
    if (!std::isfinite(n))
        return "--";
    char b[80];
    std::snprintf(b, sizeof(b), "%.*f", digits, n);
    return b;
}
// Sub-dollar prices and ratios such as RSP/SPY need more than cents to show movement.
static std::string price_text(double n) {
    return fmt(n, std::abs(n) < 1 ? 4 : 2);
}
static std::string compact_number(double n) {
    const char *suffix = "";
    for (auto [scale, s] : {std::pair{1e12, "T"}, {1e9, "B"}, {1e6, "M"}, {1e3, "K"}})
        if (std::abs(n) >= scale) {
            n /= scale;
            suffix = s;
            break;
        }
    return fmt(n, *suffix ? 2 : 0) + suffix;
}
static void text(ImU32 color, const std::string &s) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
}
void theme() {
    ImGui::StyleColorsDark();
    auto &s = ImGui::GetStyle();
    s.WindowRounding = s.PopupRounding = 6;
    s.FrameRounding = s.GrabRounding = s.TabRounding = s.ChildRounding = 4;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.TabBarOverlineSize = 2;
    s.WindowPadding = {10, 8};
    s.FramePadding = {8, 4};
    s.ItemSpacing = {8, 6};
    s.ItemInnerSpacing = {6, 4};
    s.ScrollbarSize = 10;
    s.GrabMinSize = 8;
    s.SeparatorTextBorderSize = 1;
    s.DockingSeparatorSize = 1;
    // Tabs already carry close buttons and drag handles; the window-menu arrow is clutter.
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.WindowTitleAlign = {0, .5f};
    auto set = [&](ImGuiCol c, ImU32 v) { s.Colors[c] = ImGui::ColorConvertU32ToFloat4(v); };
    set(ImGuiCol_Text, ink);
    set(ImGuiCol_TextDisabled, muted);
    set(ImGuiCol_WindowBg, surface);
    set(ImGuiCol_ChildBg, rgba(0, 0, 0, 0));
    set(ImGuiCol_PopupBg, rgba(19, 26, 35, 250));
    set(ImGuiCol_Border, line_color);
    set(ImGuiCol_BorderShadow, rgba(0, 0, 0, 0));
    set(ImGuiCol_FrameBg, rgba(24, 32, 43));
    set(ImGuiCol_FrameBgHovered, rgba(32, 42, 56));
    set(ImGuiCol_FrameBgActive, rgba(38, 50, 66));
    set(ImGuiCol_TitleBg, chrome);
    set(ImGuiCol_TitleBgActive, rgba(17, 23, 31));
    set(ImGuiCol_TitleBgCollapsed, chrome);
    set(ImGuiCol_MenuBarBg, chrome);
    set(ImGuiCol_ScrollbarBg, rgba(0, 0, 0, 0));
    set(ImGuiCol_ScrollbarGrab, rgba(42, 54, 68));
    set(ImGuiCol_ScrollbarGrabHovered, rgba(58, 72, 90));
    set(ImGuiCol_ScrollbarGrabActive, rgba(72, 90, 112));
    set(ImGuiCol_CheckMark, accent);
    set(ImGuiCol_SliderGrab, accent);
    set(ImGuiCol_SliderGrabActive, blue);
    set(ImGuiCol_Button, rgba(26, 34, 45));
    set(ImGuiCol_ButtonHovered, rgba(36, 48, 63));
    set(ImGuiCol_ButtonActive, rgba(44, 60, 80));
    set(ImGuiCol_Header, rgba(30, 44, 62));
    set(ImGuiCol_HeaderHovered, rgba(28, 38, 51));
    set(ImGuiCol_HeaderActive, rgba(36, 54, 76));
    set(ImGuiCol_Separator, line_color);
    set(ImGuiCol_SeparatorHovered, rgba(60, 90, 130));
    set(ImGuiCol_SeparatorActive, accent);
    set(ImGuiCol_ResizeGrip, rgba(0, 0, 0, 0));
    set(ImGuiCol_ResizeGripHovered, rgba(60, 90, 130, 170));
    set(ImGuiCol_ResizeGripActive, accent);
    set(ImGuiCol_InputTextCursor, ink);
    set(ImGuiCol_Tab, chrome);
    set(ImGuiCol_TabHovered, rgba(30, 40, 53));
    set(ImGuiCol_TabSelected, surface);
    set(ImGuiCol_TabSelectedOverline, accent);
    set(ImGuiCol_TabDimmed, chrome);
    set(ImGuiCol_TabDimmedSelected, surface);
    set(ImGuiCol_TabDimmedSelectedOverline, rgba(0, 0, 0, 0));
    set(ImGuiCol_DockingPreview, rgba(80, 150, 240, 90));
    set(ImGuiCol_DockingEmptyBg, bg);
    set(ImGuiCol_TableHeaderBg, rgba(19, 26, 35));
    set(ImGuiCol_TableBorderStrong, line_color);
    set(ImGuiCol_TableBorderLight, rgba(27, 35, 45));
    set(ImGuiCol_TableRowBgAlt, rgba(255, 255, 255, 6));
    set(ImGuiCol_TextSelectedBg, rgba(80, 150, 240, 80));
    set(ImGuiCol_NavCursor, accent);
    set(ImGuiCol_ModalWindowDimBg, rgba(5, 8, 12, 150));
}
static std::string title(const Panel &p) {
    return display_symbol(p.symbol) + "  " + timeframes[p.tf] + "###chart_" + std::to_string(p.id);
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
        ImGui::DockBuilderDockWindow(title(*s.panels[i]).c_str(), slots[i]);
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
                if (int(i) != list_index && s.lists[i].source.empty() &&
                    ImGui::MenuItem(s.lists[i].name.c_str())) {
                    auto &symbols = s.lists[i].symbols;
                    if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end())
                        symbols.push_back(symbol);
                }
            ImGui::EndMenu();
        }
        if (it != list.symbols.end() && list.source.empty()) {
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
    auto row_min = ImGui::GetItemRectMin(), row_max = ImGui::GetItemRectMax();
    if (chosen)
        ImGui::GetWindowDrawList()->AddRectFilled({row_min.x - 4, row_min.y}, {row_min.x - 2, row_max.y}, accent);
    float symbol_end = ImGui::GetCursorPosX() + ImGui::CalcTextSize(display_symbol(symbol).c_str()).x;
    float change_x = std::max(60.f, right - ImGui::CalcTextSize(label.c_str()).x - 2);
    float change_column = ImGui::CalcTextSize("+00.00%").x;
    bool extended = it != s.quotes.end() && fresh_extended(it->second, now());
    if (extended)
        symbol_end += ImGui::CalcTextSize(" post").x;
    if (it != s.quotes.end() && std::isfinite(it->second.price)) {
        auto price = price_text(it->second.price);
        float price_x = right - change_column - 16 - ImGui::CalcTextSize(price.c_str()).x;
        if (price_x > symbol_end + 10) {
            ImGui::SameLine(price_x);
            text(ink, price);
            hovered |= ImGui::IsItemHovered();
        }
    }
    if (extended) {
        // Pre/post-market marker sits beside the ticker, away from the numeric columns.
        ImGui::SameLine(symbol_end - ImGui::CalcTextSize(" post").x + 6);
        text(gold, it->second.extended->session == "Pre" ? "pre" : "post");
        hovered |= ImGui::IsItemHovered();
    }
    ImGui::SameLine(change_x);
    text(color, label);
    if (hovered || ImGui::IsItemHovered()) {
        auto &profile = s.profile(symbol);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(symbol.c_str());
        if (profile.loaded) {
            ImGui::SameLine();
            text(ink, profile.data.name);
            // Stocks list a sector; funds, futures, indices and crypto show their instrument type.
            std::string detail = profile.data.sector.empty() ? profile.data.type : profile.data.sector;
            if (!profile.data.industry.empty())
                detail += " / " + profile.data.industry;
            if (!detail.empty())
                text(muted, detail);
        } else if (profile.loading)
            text(faint, "Loading name...");
        else if (!profile.error.empty())
            text(faint, profile.error);
        ImGui::Separator();
        if (it != s.quotes.end()) {
            text(ink,
                 price_text(it->second.price) + " / " + label +
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
    // Build status lives at the right edge of the menu bar rather than inside every watchlist.
    const bool newer = s.available_build > build::number;
    std::string version = std::string("v") + build::version;
    float width = ImGui::CalcTextSize(version.c_str()).x;
#ifdef __EMSCRIPTEN__
    const char *update = "Reload to update";
#else
    const char *update = "Restart to update";
#endif
    if (newer)
        width += ImGui::CalcTextSize(update).x + ImGui::GetStyle().ItemSpacing.x + 2 * ImGui::GetStyle().FramePadding.x;
    float x = ImGui::GetWindowContentRegionMax().x - width - 8;
    if (x > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(x);
    if (newer) {
#ifdef __EMSCRIPTEN__
        ImGui::PushStyleColor(ImGuiCol_Text, gold);
        bool reload = ImGui::MenuItem(update);
        ImGui::PopStyleColor();
        if (reload) {
            s.ini = ImGui::SaveIniSettingsToMemory();
            s.save(true);
            EM_ASM({ Module.chartroomReload(); });
        }
#else
        text(gold, update);
#endif
    }
    text(newer ? gold : faint, version);
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
    ImGui::EndMainMenuBar();
}
static void watchlist(State &s, WatchlistWindow &w) {
    ImGui::SetNextWindowSize({300, 520}, ImGuiCond_FirstUseEver);
    if (w.focus) {
        ImGui::SetNextWindowFocus();
        w.focus = false;
    }
    w.measured = true;
    w.shown.clear();
    if (!ImGui::Begin(watchlist_title(s, w).c_str(), &w.open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        s.selected_list = w.list;
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
        ImGui::TextUnformatted(s.lists[size_t(w.list)].source.empty()
                                   ? "Right-click a ticker to reorder or remove it."
                                   : "Live lists update from their source; copy tickers to edit them.");
        ImGui::EndPopup();
    }
    auto &input = w.input;
    auto &current = s.lists[size_t(w.list)];
    if (!current.source.empty()) {
        std::string status = std::to_string(current.symbols.size()) + " stocks";
        if (current.loading)
            status += " / updating";
        else if (!current.asof.empty())
            status += " / live, " + current.asof;
        text(faint, status);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", live_list_url(current.source).c_str());
        if (!current.error.empty()) {
            ImGui::PushTextWrapPos(0);
            text(gold, current.error);
            ImGui::PopTextWrapPos();
        }
    } else if (ImGui::SetNextItemWidth(-1),
               ImGui::InputTextWithHint("##symbol", "+ Add to list / Enter", input, sizeof(input),
                                        ImGuiInputTextFlags_EnterReturnsTrue)) {
        try {
            auto symbol = resolve_symbol(input);
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
    {
        float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        float change_column = ImGui::CalcTextSize("+00.00%").x;
        text(faint, "Symbol");
        float last_x = right - change_column - 16 - ImGui::CalcTextSize("Last").x;
        if (last_x > 80) {
            ImGui::SameLine(last_x);
            text(faint, "Last");
        }
        ImGui::SameLine(std::max(70.f, right - ImGui::CalcTextSize("Chg %").x - 2));
        text(faint, "Chg %");
    }
    // Context actions may mutate the list while rows are being drawn.
    auto symbols = s.lists[size_t(w.list)].symbols;
    ImGuiListClipper clipper;
    clipper.Begin(int(symbols.size()));
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            row(s, symbols[size_t(i)], w.list);
            w.shown.push_back(symbols[size_t(i)]);
        }
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
                    : s.kind == "AD"   ? "A/D " + std::to_string(s.period)
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
                    s.kind == "KC" || s.kind == "STOCH" || s.kind == "ROC" || s.kind == "OBV" || s.kind == "AD")
                    p.dirty |= edit_color("Line color", s.color);
                if (s.kind != "OBV")
                    p.dirty |= ImGui::InputInt(s.kind == "MACD"     ? "Fast period"
                                               : s.kind == "PIVOTS" ? "N bars each side"
                                               : s.kind == "AD"     ? "Half-life"
                                                                    : "Period",
                                               &s.period);
                if (s.kind == "AD")
                    ImGui::TextWrapped("Balance of accumulation and distribution evidence, -100 to +100. Up or "
                                       "down closes of at least 0.2%% on volume above the prior 10-bar average "
                                       "count, weighted by relative volume and the move in 20-bar ATR units. "
                                       "Daily charts with a 20-bar half-life show the A+ to E grade.");
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
// Change versus the previous close: the sidebar quote when it is fresh, otherwise the prior bar.
static std::optional<double> session_change(State &s, const Panel &p) {
    auto it = s.quotes.find(p.symbol);
    if (it != s.quotes.end() && now() - it->second.fetched < 600 && !s.quote_errors.count(p.symbol) &&
        std::isfinite(it->second.change))
        return it->second.change;
    if (p.bars.size() < 2 || !(p.bars[p.bars.size() - 2].close > 0))
        return std::nullopt;
    return (p.bars.back().close / p.bars[p.bars.size() - 2].close - 1) * 100;
}
static bool segment(const char *label, bool selected) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgba(36, 56, 82));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(42, 64, 92));
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, rgba(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(30, 40, 53));
        ImGui::PushStyleColor(ImGuiCol_Text, muted);
    }
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    return pressed;
}
static void pnf_latest(Panel &p) {
    double n = double(p.pnf_chart.columns.size());
    p.pnf_view.count = std::max(1., std::min(p.pnf_view.count, n / View::latest_position));
    p.pnf_view.first = n - .5 - p.pnf_view.count * View::latest_position;
}
static void toolbar(State &s, Panel &p) {
    auto &series = s.ensure(p.symbol, p.tf);
    const float base_size = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, 26);
    {
        // The symbol is an inline field: click it, type a ticker, press Enter.
        auto id = ImGui::GetID("##symbol");
        bool editing = ImGui::GetActiveID() == id;
        if (!editing)
            std::snprintf(p.jump, sizeof(p.jump), "%s", display_symbol(p.symbol).c_str());
        float width = std::max(ImGui::CalcTextSize(p.jump).x, ImGui::CalcTextSize(editing ? "WWWWWW" : "W").x) +
                      2 * ImGui::GetStyle().FramePadding.x;
        ImGui::SetNextItemWidth(width);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, editing ? rgba(24, 32, 43) : rgba(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, rgba(30, 40, 53));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 0});
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 6);
        if (ImGui::InputText("##symbol", p.jump, sizeof(p.jump),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsUppercase |
                                 ImGuiInputTextFlags_AutoSelectAll)) {
            try {
                s.select(p, resolve_symbol(p.jump), p.tf);
                s.notice.clear();
            } catch (const std::exception &e) {
                s.notice = e.what();
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered() && !editing) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            ImGui::PushFont(nullptr, base_size);
            ImGui::SetTooltip("Click to change the symbol, then press Enter.\nCombine tickers with + - * / and "
                              "parentheses, e.g. RSP/SPY or (AAPL+MSFT)/2.\nPut spaces around minus: SPY - QQQ.");
            ImGui::PopFont();
        }
    }
    if (!p.bars.empty()) {
        ImGui::SameLine(0, 4);
        text(ink, price_text(p.bars.back().close));
    }
    ImGui::PopFont();
    if (!p.bars.empty()) {
        if (auto change = session_change(s, p)) {
            double previous = p.bars.back().close / (1 + *change / 100);
            double delta = p.bars.back().close - previous;
            ImGui::SameLine(0, 10);
            float y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(y + 7);
            text(*change >= 0 ? up : down, (delta >= 0 ? "+" : "") + fmt(delta, std::abs(previous) < 1 ? 4 : 2) + "  (" +
                                                (*change >= 0 ? "+" : "") + fmt(*change) + "%)");
        }
    }
    std::string updated = "--";
    if (series.loaded && series.history.fetched > 0)
        updated = local_date(series.history.fetched, "%H:%M:%S");
    updated = (s.offline ? "Offline / " : series.loading ? "Refreshing / " : "Updated ") + updated;
    ImGui::SameLine();
    float updated_x =
        ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(updated.c_str()).x;
    float last_right = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    if (updated_x >= last_right + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine(updated_x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
    } else
        ImGui::NewLine();
    text(!series.error.empty() ? gold : faint, updated);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Last successful chart data refresh.");
        if (series.loaded && series.history.fetched > 0)
            ImGui::TextUnformatted(local_date(series.history.fetched, "%Y-%m-%d %H:%M:%S %Z").c_str());
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, ImGui::GetStyle().ItemSpacing.y});
    for (int tf = 0; tf < 4; ++tf) {
        if (tf)
            ImGui::SameLine();
        ImGui::PushID(tf);
        if (segment(timeframes[tf], tf == p.tf) && tf != p.tf)
            s.select(p, p.symbol, tf);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    auto divider = [] {
        ImGui::SameLine(0, 10);
        auto pos = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddLine({pos.x, pos.y + 4}, {pos.x, pos.y + h - 4}, line_color);
        ImGui::Dummy({1, h});
        ImGui::SameLine(0, 10);
    };
    divider();
    if (ImGui::Button("Indicators"))
        ImGui::OpenPopup("Indicators");
    indicators(p);
    ImGui::SameLine();
    if (ImGui::Button("View"))
        ImGui::OpenPopup("View");
    drawing_toolbar(s, p);
    ImGui::SameLine();
    if (ImGui::Button("Facts")) {
        s.active = p.id;
        s.fundamentals.open = s.fundamentals.focus = s.fundamentals.follow = true;
        s.fundamentals.symbol = p.symbol;
    }
    divider();
    ImGui::BeginDisabled(series.loading || s.offline);
    if (ImGui::Button("Refresh"))
        s.refresh(p);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Latest")) {
        if (p.point_figure)
            pnf_latest(p);
        else
            p.view.fit(p.bars.size());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Jump to the newest bar (double-click the chart or press End).");
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
    if (s.active == p.id && !s.notice.empty())
        text(gold, s.notice);
    if (ImGui::BeginPopup("View")) {
        int style = p.point_figure ? 3 : !p.candles ? 2 : p.ohlc ? 1 : 0;
        const char *styles[] = {"Candlesticks", "OHLC bars", "Line", "Point & figure"};
        if (ImGui::Combo("Chart style", &style, styles, 4)) {
            if (style == 3) {
                p.pnf_fit = !p.point_figure;
                p.point_figure = true;
            } else {
                p.point_figure = false;
                p.candles = style != 2;
                p.ohlc = style == 1;
            }
            p.dirty = true;
        }
        if (p.point_figure) {
            // Every change rebuilds the columns and returns to the latest one.
            auto &f = p.pnf;
            bool changed = false;
            int boxes = f.logarithmic ? 0 : 1;
            const char *box_kinds[] = {"Logarithmic (percent)", "Arithmetic (price)"};
            if (ImGui::Combo("Box scale", &boxes, box_kinds, 2)) {
                f.logarithmic = boxes == 0;
                changed = true;
            }
            ImGui::SetNextItemWidth(120);
            if (f.logarithmic) {
                changed |= ImGui::InputDouble("Box size (%)", &f.percent, .25, 1, "%.2f");
                f.percent = std::clamp(f.percent, .05, 50.);
                for (double preset : {.5, 1., 2., 3., 5.}) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton((fmt(preset, preset < 1 ? 1 : 0) + "%").c_str())) {
                        f.percent = preset;
                        changed = true;
                    }
                }
            } else {
                bool automatic = f.box <= 0;
                if (ImGui::Checkbox("Traditional box for the price", &automatic)) {
                    f.box = automatic ? 0 : pnf_traditional_box(p.bars.empty() ? 100 : p.bars.back().close);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Classic scale: $1 boxes from $20 to $100, $2 to $200, $4 to $500, ...");
                if (!automatic) {
                    ImGui::SetNextItemWidth(120);
                    changed |= ImGui::InputDouble("Box size", &f.box, 0, 0, "%.4g");
                    f.box = std::max(f.box, 1e-6);
                } else
                    ImGui::TextDisabled("Box size %s", price_text(p.pnf_chart.step).c_str());
            }
            ImGui::SetNextItemWidth(120);
            changed |= ImGui::InputInt("Reversal (boxes)", &f.reversal);
            f.reversal = std::clamp(f.reversal, 1, 10);
            int method = f.closes ? 1 : 0;
            const char *methods[] = {"High / low", "Close only"};
            if (ImGui::Combo("Prices", &method, methods, 2)) {
                f.closes = method == 1;
                changed = true;
            }
            if (changed) {
                p.pnf_fit = true;
                p.dirty = true;
            }
            ImGui::TextDisabled("X: rising boxes. O: falling. 1-9, A-C: first box of each month.");
            ImGui::TextDisabled("Shaded box: double-top buy or double-bottom sell breakout.");
            ImGui::TextDisabled("Indicators, drawings and volume are hidden in this style.");
        }
        int scale = p.logarithmic ? 1 : 0;
        const char *scales[] = {"Linear", "Logarithmic"};
        ImGui::BeginDisabled(p.point_figure);
        if (ImGui::Combo("Price scale", &scale, scales, 2))
            p.logarithmic = scale == 1;
        ImGui::EndDisabled();
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
    // Computed symbols such as RSP/SPY have no volume of their own.
    const bool volume = p.volume && !is_expression(p.symbol);
    int pane_count = volume ? 1 : 0;
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
    {
        // A faint symbol watermark identifies each chart in dense layouts.
        auto mark = display_symbol(p.symbol) + " / " + timeframes[p.tf];
        float font_size = std::clamp((right - left) * .07f, 28.f, 72.f);
        auto *font = ImGui::GetFont();
        auto extent = font->CalcTextSizeA(font_size, FLT_MAX, 0, mark.c_str());
        if (extent.x < (right - left) * .8f && bottom - top > font_size * 2)
            draw->AddText(font, font_size,
                          {(left + right - extent.x) / 2, (top + bottom - extent.y) / 2},
                          rgba(255, 255, 255, 9), mark.c_str());
    }
    auto time_ticks = time_grid(p.bars, p.view, p.tf == 1 ? 14400 : duration(intervals[p.tf]), pw, true);
    for (const auto &tick : time_ticks)
        line(px(tick.index), top, px(tick.index), foot - 8, tick.major ? rgba(33, 44, 56) : grid);
    auto g = log_scale ? log_price_grid(lo, hi, bottom - top) : price_grid(lo, hi, bottom - top);
    double latest = p.bars.back().close;
    float ly = latest >= lo && latest <= hi ? py(latest) : -1e6f;
    for (auto price : g.levels) {
        float y = py(price);
        bool major = log_scale ? std::abs(std::log10(price) - std::round(std::log10(price))) < 1e-9
                               : std::abs(price / g.major - std::round(price / g.major)) < 1e-9;
        line(left, y, right, y, major ? rgba(33, 44, 56) : grid);
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
    // Legend: each overlay in its own color, with the scale mode first.
    draw->PushClipRect(origin, {right, top}, true);
    {
        float x = left + 4;
        auto item = [&](const std::string &t, ImU32 c) {
            label(x, origin.y + 4, t, c);
            x += ImGui::CalcTextSize(t.c_str()).x + 14;
        };
        if (p.logarithmic)
            item(log_scale ? "LOG" : "Linear (log needs positive prices)", faint);
        for (auto &r : p.results)
            if (!r.pane)
                item(r.name, r.colored_bars || r.colors.empty() ? blue : r.colors[0]);
    }
    draw->PopClipRect();
    auto pane_rule = [&](float y) { line(left, y, right + 100, y, line_color); };
    if (volume) {
        pane_rule(bottom + 1);
        label(left + 4, bottom + 3, "Volume", faint);
        double maxvol = 0;
        bool gaps = false;
        for (int i = a; i < b; ++i) {
            if (std::isfinite(p.bars[i].volume))
                maxvol = std::max(maxvol, p.bars[i].volume);
            else
                gaps = true;
        }
        if (gaps)
            label(left + 64, bottom + 3, "/ gaps unavailable", faint);
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
    int pane = volume ? 1 : 0;
    for (auto &r : p.results)
        if (r.pane) {
            float pt = bottom + pane * ph + 18, pb = bottom + (pane + 1) * ph - 4;
            bool rsi = r.name.starts_with("RSI"), stochastic = r.name.starts_with("STOCH"),
                 ad = r.name.starts_with("A/D");
            bool bounded = rsi || stochastic || ad;
            // A/D guides sit at the E and A- grade boundaries of its -100..+100 balance.
            double lower = rsi ? 30. : stochastic ? 20. : -30.68, upper = rsi ? 70. : stochastic ? 80. : 37.04;
            double low = ad ? -100 : 0, high = bounded ? 100 : 0;
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
            pane_rule(pt - 17);
            label(left + 4, pt - 15, r.name, r.colors.empty() ? faint : r.colors[0]);
            if (bounded) {
                draw->AddRectFilled({left, y(upper)}, {right, y(lower)}, rgba(80, 150, 240, 10));
                if (ad)
                    line(left, y(0), right, y(0), grid);
            }
            for (double level : {bounded ? lower : low, bounded ? upper : high}) {
                if (bounded)
                    dashed(draw, {left, y(level)}, {right, y(level)}, rgba(46, 60, 76), 3, 3);
                else
                    line(left, y(level), right, y(level), grid);
                label(right + 10, y(level) - 8, ad ? (level > 0 ? "A-" : "E") : fmt(level), faint);
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
        auto change = session_change(s, p);
        ImU32 tone = !change ? accent : *change >= 0 ? up : down;
        dashed(draw, {left, ly}, {right, ly}, with_alpha(tone, 150), 2, 3);
        axis_tag(draw, {right + 2, ly - 11}, {right + 98, ly + 11}, tone, rgba(8, 12, 18), price_text(latest));
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
    const ImU32 cross = rgba(140, 160, 182, 150), tag = rgba(46, 60, 78);
    if (over) {
        hover = int(std::floor(p.view.first + (mx - left) / pw * p.view.count));
        bool on_bar = hover >= 0 && hover < int(p.bars.size());
        float cross_x = on_bar ? px(hover) : mx;
        if (cross_x >= left && cross_x <= right) {
            dashed(draw, {cross_x, top}, {cross_x, foot - 2}, cross);
            if (on_bar) {
                auto when = local_date(p.bars[hover].time, p.tf < 2 ? "%a %Y-%m-%d %H:%M" : "%a %Y-%m-%d");
                float w = ImGui::CalcTextSize(when.c_str()).x + 12;
                float x = std::clamp(cross_x - w / 2, left, right - w);
                axis_tag(draw, {x, foot}, {x + w, foot + 21}, tag, ink, when);
            }
        }
        if (my <= bottom) {
            dashed(draw, {left, my}, {right, my}, cross);
            double price = scale.price((bottom - my) / (bottom - top));
            axis_tag(draw, {right + 2, my - 11}, {right + 98, my + 11}, tag, ink, fmt(price));
        }
    }
    // Hovered bar readout sits inside the plot, under the legend, so it never shifts the layout.
    int readout = hover >= 0 && hover < int(p.bars.size()) ? hover : over ? -1 : int(p.bars.size()) - 1;
    if (readout >= 0) {
        auto &bar = p.bars[readout];
        double previous = readout > 0 ? p.bars[readout - 1].close : bar.open;
        ImU32 tone = bar.close >= previous ? up : down;
        float x = left + 4, y = top + 4;
        auto pair = [&](const char *k, const std::string &v, ImU32 c) {
            label(x, y, k, faint);
            x += ImGui::CalcTextSize(k).x + 4;
            label(x, y, v, c);
            x += ImGui::CalcTextSize(v.c_str()).x + 12;
        };
        draw->PushClipRect({left, top}, {right, bottom}, true);
        pair("O", price_text(bar.open), tone);
        pair("H", price_text(bar.high), tone);
        pair("L", price_text(bar.low), tone);
        pair("C", price_text(bar.close), tone);
        if (previous > 0) {
            double pct = (bar.close / previous - 1) * 100;
            pair("", (pct >= 0 ? "+" : "") + fmt(pct) + "%", tone);
        }
        if (std::isfinite(bar.volume))
            pair("V", compact_number(bar.volume), muted);
        draw->PopClipRect();
    }
    auto hint = drawing_hint(p);
    if (!hint.empty()) {
        text(gold, hint);
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
static std::string pnf_settings_label(const PnfChart &c, const PnfSettings &s) {
    std::string box = c.logarithmic ? fmt(c.step * 100, c.step * 100 < 1 ? 2 : 1) + "%" : price_text(c.step);
    return "P&F " + box + " x " + std::to_string(std::max(1, s.reversal)) + " / " +
           (c.logarithmic ? "log" : "arithmetic") + " / " + (s.closes ? "close" : "high-low");
}
// Point and figure: one column per run of X (rising) or O (falling) boxes, rows on the box grid.
static void pnf_chart(State &s, Panel &p) {
    auto size = ImGui::GetContentRegionAvail();
    size.y -= 28;
    if (size.x < 260 || size.y < 190) {
        ImGui::TextWrapped("Enlarge this panel to display the chart.");
        return;
    }
    const auto &pf = p.pnf_chart;
    const auto &cols = pf.columns;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    auto &io = ImGui::GetIO();
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride), active = ImGui::IsItemActive();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bg);
    float left = origin.x + 16, right = origin.x + size.x - 102, top = origin.y + 46, foot = origin.y + size.y - 28,
          bottom = foot - 14, pw = right - left;
    auto centered = [&](const std::string &value, float y, ImU32 color) {
        draw->AddText({(left + right - ImGui::CalcTextSize(value.c_str()).x) / 2, y}, color, value.c_str());
    };
    if (cols.empty()) {
        centered(pf.error.empty() ? "Not enough price movement for one column at this box size."
                                  : pf.error,
                 (top + bottom) / 2 - 8, pf.error.empty() ? muted : gold);
        centered("Change the box size or reversal under View.", (top + bottom) / 2 + 16, faint);
        ImGui::TextUnformatted("");
        return;
    }
    const size_t n = cols.size();
    auto &view = p.pnf_view;
    if (p.pnf_fit) {
        // About 16 px per column: readable marks, with the newest columns at the usual position.
        view.count = std::clamp(double(pw) / 16, std::min(15., double(n) / View::latest_position),
                                std::max(1., double(n) / View::latest_position));
        view.first = double(n) - .5 - view.count * View::latest_position;
        p.pnf_fit = false;
    }
    float mx = io.MousePos.x, my = io.MousePos.y;
    bool over = hovered && mx >= left && mx <= right && my >= top && my <= foot;
    ScrollMotion motion;
    if (over)
        motion = p.scroll.update(io.MouseWheelH, io.MouseWheel, io.KeyShift, ImGui::GetTime());
    bool zooming = motion.zoom != 0;
    if (over) {
        view.pan(-motion.pan * view.count * .06);
        if (zooming)
            view.zoom(n, motion.zoom, (mx - left) / pw);
        if (ImGui::IsMouseDoubleClicked(0))
            pnf_latest(p);
    }
    if (active) {
        s.active = p.id;
        if (over && !zooming && ImGui::IsMouseDragging(0, 0))
            view.pan(-io.MouseDelta.x / pw * view.count);
    }
    if ((hovered || ImGui::IsItemFocused()) && !io.WantTextInput && !io.KeyCtrl && !io.KeySuper) {
        for (auto key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End})
            ImGui::SetKeyOwner(key, ImGui::GetItemID());
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_A))
            view.pan(-std::max(1., view.count * .1));
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_D))
            view.pan(std::max(1., view.count * .1));
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            view.first = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            pnf_latest(p);
    }
    auto [a, b] = view.visible(n);
    if (a == b) { // Panned past either end: keep the nearest column in the scale.
        a = std::clamp(a, 0, int(n) - 1);
        b = a + 1;
    }
    // Rows fit the visible columns and the latest price, like the time chart's automatic price scale.
    double latest = p.bars.back().close;
    double latest_row = pf.logarithmic && latest <= 0 ? missing : pf.row(latest);
    int lo = cols[size_t(a)].low, hi = cols[size_t(a)].high;
    for (int c = a; c < b; ++c) {
        lo = std::min(lo, cols[size_t(c)].low);
        hi = std::max(hi, cols[size_t(c)].high);
    }
    if (b == int(n) && std::isfinite(latest_row)) {
        lo = std::min(lo, int(std::floor(latest_row)));
        hi = std::max(hi, int(std::ceil(latest_row)));
    }
    int pad = std::max(1, (hi - lo) / 12);
    double r0 = lo - pad - .5, r1 = hi + pad + .5;
    float rh = (bottom - top) / float(r1 - r0), cw = pw / float(view.count);
    auto x = [&](double col) { return left + float((col - view.first + .5) / view.count) * pw; };
    auto y = [&](double row) { return bottom - float((row - r0) / (r1 - r0)) * (bottom - top); };
    auto bar_time = [&](int bar) { return p.bars[size_t(std::clamp(bar, 0, int(p.bars.size()) - 1))].time; };
    auto label_price = [&](double v) {
        double m = std::abs(v);
        return fmt(v, m >= 10000 ? 0 : m >= 1000 ? 1 : m >= 10 ? 2 : m >= 1 ? 3 : 4);
    };
    // Grid: rows at a spacing that keeps price labels apart; columns where the year or month turns.
    int row_step = 1;
    for (int step : {1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000, 2000, 5000, 10000})
        if (step * rh >= 26) {
            row_step = step;
            break;
        }
    float ly = std::isfinite(latest_row) ? y(latest_row) : -1e6f;
    for (int r = int(std::ceil(r0)); r <= int(std::floor(r1)); ++r) {
        if (((r % row_step) + row_step) % row_step)
            continue;
        float yy = y(r);
        draw->AddLine({left, yy}, {right, yy}, grid);
        if (std::abs(yy - ly) > 22 && !(over && std::abs(yy - my) < 20))
            draw->AddText({right + 10, yy - 8}, muted, label_price(pf.price(r)).c_str());
    }
    {
        int last_label = -1000000;
        float min_gap = 74;
        for (int c = std::max(0, a); c < b; ++c) {
            Time t = bar_time(cols[size_t(c)].fills.front());
            Time prev = c ? bar_time(cols[size_t(c - 1)].fills.front()) : 0;
            bool new_year = !c || local_date(t, "%Y") != local_date(prev, "%Y");
            bool new_month = !c || local_date(t, "%Y%m") != local_date(prev, "%Y%m");
            float xx = x(c);
            if (new_year)
                draw->AddLine({xx, top}, {xx, bottom}, rgba(33, 44, 56));
            if ((new_month || new_year) && (xx - x(last_label)) >= min_gap) {
                auto text_value = local_date(t, new_year ? "%Y" : "%b '%y");
                draw->AddText({xx + 3, bottom + 4}, new_year ? ink : muted, text_value.c_str());
                last_label = c;
            }
        }
    }
    // Hover: the column, the box row, and the bar that filled that box.
    int hover_col = -1, hover_row = 0, hover_bar = -1;
    if (over && my <= bottom) {
        hover_col = int(std::floor(view.first + (mx - left) / pw * view.count));
        hover_row = int(std::lround(r0 + (bottom - my) / (bottom - top) * (r1 - r0)));
        if (hover_col >= 0 && hover_col < int(n)) {
            auto &c = cols[size_t(hover_col)];
            if (hover_row >= c.low && hover_row <= c.high)
                hover_bar = c.bar(hover_row);
        }
    }
    draw->PushClipRect({left, top}, {right, bottom}, true);
    // The live column is shaded so the current run stands out.
    draw->AddRectFilled({x(double(n - 1) - .5), top}, {x(double(n - 1) + .5), bottom}, rgba(255, 255, 255, 6));
    // Rows fit the price range, so cells are rarely square; marks fill the cell up to a 3:2 aspect.
    float box = std::min(cw, rh), thick = std::clamp(box * .1f, 1.f, 2.4f);
    float hw = cw * .34f, hh = rh * .34f;
    hw = std::min(hw, hh * 1.5f);
    hh = std::min(hh, hw * 1.5f);
    float digit_size = std::clamp(rh * 1.3f, 8.5f, ImGui::GetFontSize());
    bool marks = p.tf >= 2 && rh >= 6.5f && cw >= 7; // Month markers on daily and weekly charts.
    static const char *months = "123456789ABC";
    // The month of the box filled just before the first visible column, so markers start correctly.
    int previous_month = -1;
    if (a > 0)
        previous_month = std::stoi(local_date(bar_time(cols[size_t(a - 1)].fills.back()), "%m"));
    for (int c = a; c < b; ++c) {
        auto &col = cols[size_t(c)];
        ImU32 color = col.up ? up : down;
        float cx = x(c);
        if (col.signal) {
            // The breakout box: first X above the prior X column's top, or O below the prior O column's bottom.
            int row = col.up ? cols[size_t(c - 2)].high + 1 : cols[size_t(c - 2)].low - 1;
            draw->AddRectFilled({cx - cw / 2 + 1, y(row + .5) + 1}, {cx + cw / 2 - 1, y(row - .5) - 1},
                                with_alpha(color, 46), 2);
        }
        for (size_t k = 0; k < col.fills.size(); ++k) {
            int row = col.up ? col.low + int(k) : col.high - int(k);
            float cy = y(row);
            int bar = col.fills[k];
            if (bar == hover_bar && c == hover_col)
                draw->AddRectFilled({cx - cw / 2, cy - rh / 2}, {cx + cw / 2, cy + rh / 2}, with_alpha(color, 40));
            int month = -1;
            if (marks)
                month = std::stoi(local_date(bar_time(bar), "%m"));
            if (marks && previous_month >= 0 && month != previous_month) {
                char digit[2] = {months[month - 1], 0};
                auto extent = ImGui::GetFont()->CalcTextSizeA(digit_size, FLT_MAX, 0, digit);
                draw->AddText(ImGui::GetFont(), digit_size, {cx - extent.x / 2, cy - extent.y / 2}, color, digit);
            } else if (box < 3.5f) {
                draw->AddRectFilled({cx - std::max(.5f, cw * .3f), cy - std::max(.5f, rh * .4f)},
                                    {cx + std::max(.5f, cw * .3f), cy + std::max(.5f, rh * .4f)}, color);
            } else if (col.up) {
                draw->AddLine({cx - hw, cy - hh}, {cx + hw, cy + hh}, color, thick);
                draw->AddLine({cx + hw, cy - hh}, {cx - hw, cy + hh}, color, thick);
            } else
                draw->AddEllipse({cx, cy}, {hw, hh}, color, 0, 0, thick);
            if (marks)
                previous_month = month;
        }
    }
    if (std::isfinite(latest_row))
        dashed(draw, {left, ly}, {right, ly}, with_alpha(cols.back().up ? up : down, 150), 2, 3);
    if (over && my <= bottom && hover_col >= 0) {
        dashed(draw, {left, y(hover_row)}, {right, y(hover_row)}, rgba(70, 88, 108), 3, 3);
        dashed(draw, {x(hover_col), top}, {x(hover_col), bottom}, rgba(70, 88, 108), 3, 3);
    }
    draw->PopClipRect();
    if (std::isfinite(latest_row) && ly >= top && ly <= bottom)
        axis_tag(draw, {right + 2, ly - 11}, {right + 98, ly + 11}, cols.back().up ? up : down, rgba(8, 12, 18),
                 price_text(latest));
    if (over && my <= bottom) {
        float hy = y(hover_row);
        axis_tag(draw, {right + 2, hy - 11}, {right + 98, hy + 11}, rgba(46, 60, 76), ink,
                 label_price(pf.price(hover_row)));
    }
    // Legend: settings, size of the chart, and the most recent signal.
    {
        std::string summary = pnf_settings_label(pf, p.pnf) + " / " + std::to_string(n) + " columns";
        draw->AddText({left, origin.y + 6}, ink, summary.c_str());
        for (size_t c = n; c-- > 2;)
            if (cols[c].signal) {
                int row = cols[c].up ? cols[c - 2].high + 1 : cols[c - 2].low - 1;
                std::string signal = std::string(cols[c].up ? "Last signal: buy (double top) " : "Last signal: sell (double bottom) ") +
                                     local_date(bar_time(cols[c].bar(row)), "%b %d, %Y") + " at " +
                                     label_price(pf.price(row));
                draw->AddText({left, origin.y + 24}, cols[c].up ? up : down, signal.c_str());
                break;
            }
    }
    if (hover_col >= 0 && hover_col < int(n) && over && my <= bottom) {
        auto &c = cols[size_t(hover_col)];
        ImGui::BeginTooltip();
        text(c.up ? up : down, std::string(c.up ? "X column" : "O column") + " / " +
                                   std::to_string(c.high - c.low + 1) + " boxes / " + label_price(pf.price(c.low)) +
                                   " to " + label_price(pf.price(c.high)));
        text(muted, local_date(bar_time(c.fills.front()), "%b %d, %Y") + " to " +
                        local_date(bar_time(c.fills.back()), "%b %d, %Y"));
        if (hover_bar >= 0) {
            int same = int(std::count(c.fills.begin(), c.fills.end(), hover_bar));
            ImGui::Separator();
            text(ink, "Box " + label_price(pf.price(hover_row)) + " filled " +
                          local_date(bar_time(hover_bar), p.tf >= 2 ? "%b %d, %Y" : "%b %d, %Y %H:%M"));
            if (same > 1)
                text(muted, std::to_string(same) + " boxes filled by that bar (highlighted)");
        }
        ImGui::EndTooltip();
    }
    auto &e = s.ensure(p.symbol, p.tf);
    std::string status = s.offline ? "Offline cache" : e.loading ? "Refreshing..." : "Auto 60s";
    text(muted, data_source(e.history) + " / " + e.history.currency + " / " + status + " / " +
                    std::to_string(p.bars.size()) + " " + timeframes[p.tf] + " bars from " +
                    local_date(p.bars.front().time, "%Y-%m-%d"));
}
// Stale data is about to be replaced: after a manual refresh, a first load, or an old cache.
// Routine 60-second polls of fresh data stay quiet.
static bool loading_stale(const State &s, const Series &series) {
    return !s.offline && series.loading &&
           (series.manual || !series.loaded || now() - series.history.fetched > 90);
}
static void spinner(ImDrawList *draw, ImVec2 center, float radius, ImU32 color, float thickness = 4) {
    float start = float(ImGui::GetTime() * 5.5);
    draw->AddCircle(center, radius, with_alpha(color, 50), 48, thickness);
    draw->PathArcTo(center, radius, start, start + 4.2f, 36);
    draw->PathStroke(color, ImDrawFlags_None, thickness);
    request_frame(1. / 30.);
}
static void loading_overlay(Panel &p, const Series &series, ImVec2 min, ImVec2 max) {
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(min, max, true);
    draw->AddRectFilled(min, max, with_alpha(bg, 215));
    std::string title = "Refreshing " + display_symbol(p.symbol) + "  /  " + timeframes[p.tf];
    std::string detail = series.loaded && series.history.fetched > 0
                             ? "Chart below is from " + local_date(series.history.fetched, "%b %d %H:%M")
                             : "";
    float width = std::max(ImGui::CalcTextSize(title.c_str()).x, ImGui::CalcTextSize(detail.c_str()).x) + 56;
    ImVec2 center{(min.x + max.x) / 2, (min.y + max.y) / 2};
    ImVec2 card_min{center.x - width / 2, center.y - 78}, card_max{center.x + width / 2, center.y + 70};
    draw->AddRectFilled(card_min, card_max, surface, 8);
    draw->AddRect(card_min, card_max, with_alpha(accent, 140), 8, 0, 1.5f);
    spinner(draw, {center.x, center.y - 32}, 24, accent);
    auto centered = [&](const std::string &value, float y, ImU32 color) {
        draw->AddText({center.x - ImGui::CalcTextSize(value.c_str()).x / 2, y}, color, value.c_str());
    };
    centered(title, center.y + 10, ink);
    centered(detail, center.y + 36, muted);
    draw->PopClipRect();
}
// Background refresh of a cached chart: a corner badge, so the bars stay readable meanwhile.
static void refresh_badge(const Series &series, ImVec2 min, ImVec2 max) {
    auto *draw = ImGui::GetWindowDrawList();
    std::string label = "Refreshing";
    if (series.history.fetched > 0)
        label += " / data from " + local_date(series.history.fetched, "%b %d %H:%M");
    auto text_size = ImGui::CalcTextSize(label.c_str());
    // Sits left of the price axis, on the legend row.
    ImVec2 b_max{max.x - 110, min.y + 6 + text_size.y + 10}, b_min{b_max.x - text_size.x - 42, min.y + 6};
    if (b_min.x < min.x + 8)
        return;
    draw->PushClipRect(min, max, true);
    draw->AddRectFilled(b_min, b_max, surface, 12);
    draw->AddRect(b_min, b_max, with_alpha(accent, 140), 12, 0, 1.5f);
    spinner(draw, {b_min.x + 17, (b_min.y + b_max.y) / 2}, 7, accent, 2.5f);
    draw->AddText({b_min.x + 32, b_min.y + 5}, ink, label.c_str());
    draw->PopClipRect();
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
    ImU32 accent = failed || s.offline ? gold : rgba(0, 160, 210);
    if (loading_stale(s, series))
        spinner(draw, {center, top + 30}, 24, accent);
    else
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
        bool opened = ImGui::Begin(title(p).c_str(), s.panels.size() > 1 ? &p.open : nullptr,
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoCollapse);
        if (opened) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                s.active = p.id;
            if (s.panels.size() > 1 && s.active == p.id) {
                // Watchlist clicks load into the active chart, so mark it when there is a choice.
                auto inner = ImGui::GetCurrentWindow()->InnerRect;
                ImVec2 pos = inner.Min, end{inner.Max.x, inner.Min.y + 2};
                auto *draw = ImGui::GetWindowDrawList();
                draw->PushClipRect(pos, end, false);
                draw->AddRectFilled(pos, end, accent);
                draw->PopClipRect();
            }
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
            else {
                ImVec2 min = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
                if (p.point_figure)
                    pnf_chart(s, p);
                else
                    chart(s, p);
                // Covers the plot and axes, leaving the footer's source line readable.
                ImVec2 max{min.x + size.x, min.y + size.y - 28};
                if (loading_stale(s, e)) {
                    if (e.manual) // The user asked for this refresh: make it unmistakable.
                        loading_overlay(p, e, min, max);
                    else
                        refresh_badge(e, min, max);
                }
            }
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
