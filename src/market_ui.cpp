#include "imgui.h"
#include "state.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
namespace cr {
static void value(double n, int digits = 2) {
    if (std::isfinite(n))
        ImGui::Text("%.*f", digits, n);
    else
        ImGui::TextDisabled("--");
}
static void compact(double n) {
    if (!std::isfinite(n)) {
        ImGui::TextDisabled("--");
        return;
    }
    if (std::abs(n) >= 1e12)
        ImGui::Text("%.2fT", n / 1e12);
    else if (std::abs(n) >= 1e9)
        ImGui::Text("%.2fB", n / 1e9);
    else if (std::abs(n) >= 1e6)
        ImGui::Text("%.2fM", n / 1e6);
    else if (std::abs(n) >= 1e3)
        ImGui::Text("%.1fK", n / 1e3);
    else
        value(n, 0);
}
static void status(bool offline, bool loading, Time fetched, const std::string &error) {
    if (loading)
        ImGui::TextDisabled("Refreshing...");
    if (fetched)
        ImGui::TextDisabled("%s %s", offline ? "Offline snapshot" : "Updated",
                            local_date(fetched, "%Y-%m-%d %H:%M:%S %Z").c_str());
    else if (offline)
        ImGui::TextDisabled("No cached snapshot for these settings.");
    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(gold));
        ImGui::TextWrapped("%s%s", error.c_str(), fetched ? ". Showing last successful snapshot." : "");
        ImGui::PopStyleColor();
    }
}
static void open_symbol(State &s, const std::string &symbol) {
    auto &io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeySuper)
        s.add(symbol);
    else
        s.select(s.current(), symbol, s.current().tf);
}
static void options_window(State &s) {
    auto &o = s.options;
    if (!o.open)
        return;
    auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_FirstUseEver, {.5f, .5f});
    ImGui::SetNextWindowSize(
        {std::min(1000.f, viewport->WorkSize.x - 30), std::min(570.f, viewport->WorkSize.y - 30)},
        ImGuiCond_FirstUseEver);
    if (o.focus) {
        ImGui::SetNextWindowFocus();
        o.focus = false;
    }
    if (!ImGui::Begin("Options", &o.open)) {
        ImGui::End();
        return;
    }
    static char ticker[64]{};
    static std::string last_symbol;
    if (last_symbol != o.symbol) {
        std::snprintf(ticker, sizeof(ticker), "%s", o.symbol.c_str());
        last_symbol = o.symbol;
    }
    ImGui::SetNextItemWidth(110);
    bool load =
        ImGui::InputTextWithHint("##option-symbol", "Symbol", ticker, sizeof(ticker),
                                 ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    load |= ImGui::Button("Load");
    ImGui::SameLine();
    if (ImGui::Button("Use active chart")) {
        std::snprintf(ticker, sizeof(ticker), "%s", s.current().symbol.c_str());
        load = true;
    }
    if (load) {
        try {
            o.symbol = resolve_symbol(ticker);
            o.expiry.clear();
            s.refresh_options();
        } catch (const std::exception &e) {
            o.error = e.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open underlying chart"))
        open_symbol(s, o.symbol);
    ImGui::SameLine();
    ImGui::BeginDisabled(s.offline || o.loading);
    if (ImGui::Button("Refresh"))
        s.refresh_options(true);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("Expiry", o.expiry.empty() ? "Select expiry" : o.expiry.c_str())) {
        s.refresh_option_dates(true);
        for (auto &e : o.dates.dates) {
            if (ImGui::Selectable(e.c_str(), e == o.expiry)) {
                o.expiry = e;
                s.refresh_options();
            }
            if (e == o.expiry)
                ImGui::SetItemDefaultFocus();
        }
        if (o.dates_loading)
            ImGui::TextDisabled("Loading more expiries...");
        if (!o.dates_error.empty()) {
            ImGui::TextWrapped("%s", o.dates_error.c_str());
            if (ImGui::Button("Retry dates")) {
                o.dates_retry = 0;
                s.refresh_option_dates(true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Calls", !o.puts))
        o.puts = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Puts", o.puts))
        o.puts = true;
    auto target = s.option_target();
    std::string target_label = target ? display_symbol(target->symbol) + " " + timeframes[target->tf] +
                                            " / Chart " + std::to_string(target->id)
                                      : "Open an underlying chart";
    ImGui::SetNextItemWidth(230);
    if (ImGui::BeginCombo("Linked chart", target_label.c_str())) {
        for (auto &p : s.panels)
            if (p->symbol == o.symbol && p->open) {
                auto label =
                    display_symbol(p->symbol) + " " + timeframes[p->tf] + " / Chart " + std::to_string(p->id);
                if (ImGui::Selectable(label.c_str(), p.get() == target)) {
                    o.target_chart = p->id;
                    target = p.get();
                }
            }
        ImGui::EndCombo();
    }
    if (target) {
        ImGui::SameLine();
        ImGui::Checkbox("Strike ladder", &target->option_ladder);
        if (target->option_ladder) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            int metric = target->ladder_volume ? 1 : 0;
            if (ImGui::Combo("##ladder-metric", &metric, "Open interest\0Volume\0"))
                target->ladder_volume = metric == 1;
        }
    }
    if (o.dates_loading && o.expiry.empty())
        ImGui::TextDisabled("Loading expiries...");
    if (o.expiry.empty() && !o.dates_error.empty())
        ImGui::TextWrapped("%s", o.dates_error.c_str());
    status(s.offline, o.loading, o.data.fetched, o.error);
    if (std::isfinite(o.data.underlying))
        ImGui::Text("Underlying: %.2f", o.data.underlying);
    // Use the ribbon's hues as low-opacity washes; keep price text neutral and readable.
    auto wash = [](ImU32 color, unsigned alpha) { return (color & 0x00ffffffu) | (alpha << 24); };
    const ImU32 itm_background = wash(ribbon_color(4), 30);
    const ImU32 otm_background = wash(ribbon_color(-4), 40);
    const ImU32 atm_background = rgba(219, 230, 240, 24);
    ImGui::TextDisabled("Nasdaq / may be delayed / selected expiry refreshes every 60s while open");
    if (o.data.truncated)
        ImGui::TextWrapped("This expiry exceeds the provider row limit; some strikes are unavailable.");
    if (o.data.rows.empty() && !o.loading && o.error.empty() && !s.offline && !o.dates_loading)
        ImGui::TextDisabled("No contracts loaded for this expiry.");
    double atm = atm_strike(o.data, o.expiry);
    auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg |
                 ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    // A distinct table per selection resets scrolling when the expiry changes.
    ImGui::PushID(o.key.c_str());
    if (ImGui::BeginTable("chain", 6, flags, {0, 0})) {
        const char *headers[] = {"Strike", "Last", "Bid", "Ask", "Volume", "Open interest"};
        for (auto name : headers)
            ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        // Provider/cache rows are ascending; display the highest strikes at the top.
        for (auto it = o.data.rows.rbegin(); it != o.data.rows.rend(); ++it) {
            auto &r = *it;
            if (r.expiry != o.expiry)
                continue;
            auto label = moneyness(r.strike, o.data.underlying, atm, o.puts);
            bool at = std::strcmp(label, "ATM") == 0, in = std::strcmp(label, "ITM") == 0;
            bool known = std::strcmp(label, "--") != 0;
            ImGui::TableNextRow();
            if (known)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, at   ? atm_background
                                                                  : in ? itm_background
                                                                       : otm_background);
            ImGui::TableSetColumnIndex(0);
            bool pinned = target && target->option_marker && target->option_marker->expiry == r.expiry &&
                          target->option_marker->strike == r.strike && target->option_marker->puts == o.puts;
            char strike_label[80];
            std::snprintf(strike_label, sizeof(strike_label), "%s%.2f", pinned ? "* " : "", r.strike);
            if (ImGui::Selectable(strike_label, false, ImGuiSelectableFlags_SpanAllColumns)) {
                if (target)
                    target->option_marker = OptionMarker{r.expiry, r.strike, o.puts};
            }
            if (ImGui::IsItemHovered()) {
                if (target)
                    s.option_preview = OptionPreview{target->id, {r.expiry, r.strike, o.puts}};
                ImGui::SetTooltip(target
                                      ? "Hover previews on the linked chart. Click to pin strike and expiry."
                                      : "Open a chart of this underlying to preview or pin this option.");
            }
            if (at && o.centered_key != o.key) {
                ImGui::SetScrollHereY(.5f);
                o.centered_key = o.key;
            }
            auto &side = o.puts ? r.put : r.call;
            int column = 1;
            for (auto [n, digits] : {std::pair{side.last, 2},
                                     {side.bid, 2},
                                     {side.ask, 2},
                                     {side.volume, 0},
                                     {side.interest, 0}}) {
                ImGui::TableSetColumnIndex(column++);
                value(n, digits);
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::End();
}
static void screener_window(State &s) {
    auto &sc = s.screener;
    if (!sc.open)
        return;
    auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_FirstUseEver, {.5f, .5f});
    ImGui::SetNextWindowSize(
        {std::min(1050.f, viewport->WorkSize.x - 30), std::min(650.f, viewport->WorkSize.y - 30)},
        ImGuiCond_FirstUseEver);
    if (sc.focus) {
        ImGui::SetNextWindowFocus();
        sc.focus = false;
    }
    if (!ImGui::Begin("Screener", &sc.open)) {
        ImGui::End();
        return;
    }
    const char *presets[] = {"Largest companies", "Top gainers", "Top losers", "Most active"};
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("##preset", &sc.query.sort, presets, 4)) {
        sc.query.offset = 0;
        s.refresh_screener();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s.offline || sc.loading);
    if (ImGui::Button("Refresh"))
        s.refresh_screener();
    ImGui::EndDisabled();
    static double price = 0, cap = 0, volume = 0;
    static char sector[100]{};
    static bool initialized = false;
    if (!initialized) {
        price = sc.query.min_price;
        cap = sc.query.min_cap;
        volume = sc.query.min_volume;
        std::snprintf(sector, sizeof(sector), "%s", sc.query.sector.c_str());
        initialized = true;
    }
    if (ImGui::CollapsingHeader("Filters")) {
        ImGui::SetNextItemWidth(100);
        ImGui::InputDouble("Min price ($)", &price, 0, 0, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputDouble("Min cap ($B)", &cap, 0, 0, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputDouble("Min volume", &volume, 0, 0, "%.0f");
        ImGui::SetNextItemWidth(240);
        ImGui::InputTextWithHint("Sector", "All sectors (blank)", sector, sizeof(sector));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Exact provider sector, e.g. Technology Services, Finance or Energy Minerals.");
        if (ImGui::Button("Apply filters")) {
            sc.query.min_price = std::isfinite(price) ? std::max(0., price) : 0;
            sc.query.min_cap = std::isfinite(cap) ? std::max(0., cap) : 0;
            sc.query.min_volume = std::isfinite(volume) ? std::max(0., volume) : 0;
            sc.query.sector = sector;
            sc.query.offset = 0;
            s.refresh_screener();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear filters")) {
            price = cap = volume = 0;
            sector[0] = 0;
            int sort = sc.query.sort;
            sc.query = {};
            sc.query.sort = sort;
            s.refresh_screener();
        }
    }
    char search[128];
    std::snprintf(search, sizeof(search), "%s", sc.search.c_str());
    ImGui::SetNextItemWidth(230);
    if (ImGui::InputTextWithHint("##filter", "Find on this page", search, sizeof(search)))
        sc.search = search;
    ImGui::SameLine();
    ImGui::BeginDisabled(sc.query.offset == 0 || sc.loading);
    if (ImGui::Button("Previous")) {
        sc.query.offset = std::max(0, sc.query.offset - 100);
        s.refresh_screener();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sc.query.offset + 100 >= sc.data.total || sc.loading);
    if (ImGui::Button("Next")) {
        sc.query.offset += 100;
        s.refresh_screener();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%d-%d of %d", sc.data.rows.empty() ? 0 : sc.query.offset + 1,
                        sc.query.offset + int(sc.data.rows.size()), sc.data.total);
    status(s.offline, sc.loading, sc.data.fetched, sc.error);
    ImGui::TextDisabled("TradingView scanner / US common stocks / may be delayed / click to chart, "
                        "Ctrl/Cmd-click for a new chart");
    auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg |
                 ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("stocks", 8, flags, {0, 0})) {
        const char *headers[] = {"Symbol", "Name",       "Price",  "Change %",
                                 "Volume", "Market cap", "Sector", "Exchange"};
        for (int i = 0; i < 8; ++i)
            ImGui::TableSetupColumn(headers[i], ImGuiTableColumnFlags_WidthStretch,
                                    i == 1   ? 2.5f
                                    : i == 6 ? 2.f
                                             : 1.f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        };
        auto find = lower(sc.search);
        for (auto &r : sc.data.rows) {
            if (!find.empty() && lower(r.symbol + " " + r.name).find(find) == std::string::npos)
                continue;
            ImGui::PushID((r.exchange + ":" + r.symbol).c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(r.symbol.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                open_symbol(s, r.symbol);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Open in new chart"))
                    s.add(r.symbol);
                if (ImGui::MenuItem("Options")) {
                    s.options.symbol = r.symbol;
                    s.options.open = s.options.focus = true;
                    s.refresh_options();
                }
                if (ImGui::BeginMenu("Add to watchlist")) {
                    for (auto &list : s.lists)
                        if (list.source.empty() && ImGui::MenuItem(list.name.c_str()) &&
                            std::find(list.symbols.begin(), list.symbols.end(), r.symbol) ==
                                list.symbols.end())
                            list.symbols.push_back(r.symbol);
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableSetColumnIndex(2);
            value(r.price);
            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(r.change >= 0 ? up : down));
            value(r.change);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(4);
            compact(r.volume);
            ImGui::TableSetColumnIndex(5);
            compact(r.cap);
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(r.sector.c_str());
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(r.exchange.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
static void fundamentals_window(State &s) {
    auto &w = s.fundamentals;
    if (!w.open)
        return;
    if (w.follow)
        w.symbol = s.current().symbol;
    ImGui::SetNextWindowSize({530, 640}, ImGuiCond_FirstUseEver);
    if (w.focus) {
        ImGui::SetNextWindowFocus();
        w.focus = false;
    }
    if (!ImGui::Begin("Fundamentals", &w.open)) {
        ImGui::End();
        return;
    }
    ImGui::Checkbox("Follow selected chart", &w.follow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputTextWithHint("##company", w.symbol.c_str(), w.input, sizeof(w.input),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        try {
            w.symbol = resolve_symbol(w.input);
            w.follow = false;
            w.input[0] = 0;
        } catch (const std::exception &e) {
            s.notice = e.what();
        }
    }
    auto &c = s.company(w.symbol);
    ImGui::SameLine();
    ImGui::BeginDisabled(c.loading || s.offline);
    if (ImGui::Button("Refresh"))
        s.company(w.symbol, true);
    ImGui::EndDisabled();
    auto &f = c.data;
    ImGui::Separator();
    ImGui::TextWrapped("%s  %s", w.symbol.c_str(), f.name.c_str());
    if (!f.sector.empty())
        ImGui::TextWrapped("%s / %s", f.sector.c_str(), f.industry.c_str());
    status(s.offline, c.loading, f.fetched, "");
    if (!c.error.empty())
        ImGui::TextWrapped("Unavailable updates: %s. Previously cached components are retained.",
                           c.error.c_str());
    if (!c.part_times.empty() && ImGui::CollapsingHeader("Data timestamps"))
        for (auto &[name, time] : c.part_times.items())
            ImGui::Text("%s: %s", name.c_str(), local_date(time.get<Time>()).c_str());
    if (ImGui::BeginTable("facts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (auto &[name, value] : f.facts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        }
        double ttm = missing;
        for (size_t i = 0; i < f.earnings.size(); ++i)
            if (!f.earnings[i].upcoming)
                ttm = trailing_eps(f.earnings, i);
        if (std::isfinite(ttm)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("EPS / trailing 4 quarters");
            ImGui::TableNextColumn();
            value(ttm);
        }
        ImGui::EndTable();
    }
    if (!f.description.empty() && ImGui::CollapsingHeader("About this company"))
        ImGui::TextWrapped("%s", f.description.c_str());
    ImGui::SeparatorText("Earnings / EPS");
    auto &p = s.current();
    ImGui::BeginDisabled(p.symbol != w.symbol);
    p.dirty |= ImGui::Checkbox("EPS line on selected chart", &p.eps);
    if (p.eps)
        p.dirty |= ImGui::Checkbox("Trailing four quarters", &p.eps_ttm);
    ImGui::EndDisabled();
    if (f.earnings.empty())
        ImGui::TextWrapped("No earnings reports available for this symbol.");
    if (ImGui::BeginTable("reports", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        for (auto name : {"Report date", "Quarter", "Actual", "Estimate", "Surprise %"})
            ImGui::TableSetupColumn(name);
        ImGui::TableHeadersRow();
        for (auto it = f.earnings.rbegin(); it != f.earnings.rend(); ++it) {
            auto &e = *it;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", e.day.c_str(), e.upcoming ? "*" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.upcoming ? "Upcoming" : e.fiscal.c_str());
            ImGui::TableNextColumn();
            value(e.actual);
            ImGui::TableNextColumn();
            value(e.estimate);
            ImGui::TableNextColumn();
            value(e.surprise);
        }
        ImGui::EndTable();
    }
    if (!f.upcoming_note.empty() && ImGui::CollapsingHeader("Upcoming report / estimated date"))
        ImGui::TextWrapped("%s", f.upcoming_note.c_str());
    ImGui::TextWrapped("Nasdaq / recent quarters plus locally cached reports. * Upcoming dates are "
                       "estimates. Report times are unavailable; the EPS line changes from the following "
                       "day. EPS uses the provider's reported basis, which may differ from GAAP.");
    ImGui::End();
}
void market_windows(State &s) {
    fundamentals_window(s);
    options_window(s);
    screener_window(s);
}
} // namespace cr
