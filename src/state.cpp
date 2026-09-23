#include "state.hpp"
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
namespace cr {
namespace fs = std::filesystem;
fs::path default_directory() {
#ifdef __EMSCRIPTEN__
    return "/data";
#elif defined(_WIN32)
    const char *base = std::getenv("LOCALAPPDATA");
    return fs::path(base ? base : ".") / "ChartroomCpp";
#elif defined(__APPLE__)
    const char *base = std::getenv("HOME");
    return fs::path(base ? base : ".") / "Library" / "Application Support" / "ChartroomCpp";
#else
    const char *xdg = std::getenv("XDG_DATA_HOME");
    const char *home = std::getenv("HOME");
    return (xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".local" / "share") / "chartroom-cpp";
#endif
}
Json read_json(const fs::path &p) {
    std::ifstream in(p);
    if (!in)
        throw std::runtime_error("Cannot read " + p.string());
    return Json::parse(in);
}
void atomic_json(const fs::path &p, const Json &j) {
    fs::create_directories(p.parent_path());
    auto temp = p;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << j.dump();
        out.flush();
        if (!out)
            throw std::runtime_error("Cannot save " + p.string());
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace " + p.string());
#else
    fs::rename(temp, p);
#endif
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module.chartroomSync)
            Module.chartroomSync();
    });
#endif
}
fs::path cache_path(const fs::path &dir, const std::string &s, const std::string &i) {
    static const char *hex = "0123456789abcdef";
    std::string name;
    for (unsigned char c : s) {
        name += hex[c >> 4];
        name += hex[c & 15];
    }
    return dir / "cache" / (name + "." + i + ".json");
}
State::State(fs::path dir, bool offline_, fs::path import) : directory(std::move(dir)), offline(offline_) {
    lists = {{"Big names", big_names}, {"Futures & crypto", {"ES=F", "NQ=F", "CL=F", "ETH-USD", "SOL-USD"}}};
    fs::create_directories(directory);
    auto path = directory / "workspace.json";
    if (fs::exists(path)) {
        try {
            restore(read_json(path));
        } catch (const std::exception &e) {
            notice = "Saved workspace could not be read: " + std::string(e.what());
            auto backup = directory / ("workspace.invalid." + std::to_string(now()) + ".json");
            std::error_code ec;
            fs::copy_file(path, backup, ec);
            if (ec)
                may_save = false;
            panels.clear();
        }
    } else if (!import.empty()) {
        try {
            import_julia(import);
        } catch (const std::exception &e) {
            notice = "Import failed: " + std::string(e.what());
        }
    }
    if (panels.empty())
        add("SPY");
    for (auto s : core_symbols)
        ensure(s, 2);
    std::set<std::string> cached_symbols(core_symbols.begin(), core_symbols.end());
    for (auto &p : panels)
        cached_symbols.insert(p->symbol);
    for (auto &list : lists)
        cached_symbols.insert(list.symbols.begin(), list.symbols.end());
    for (auto &s : cached_symbols) {
        try {
            auto j = read_json(cache_path(directory, s, "quote"));
            if (j.at("symbol") != s)
                continue;
            accept_quote(s,
                         {j.at("price"), j.at("change"), parse_time(j.at("asof")),
                          parse_time(j.at("fetched_at")), j.value("snapshot", false)},
                         false);
        } catch (...) {
        }
    }
}
State::~State() = default;
std::string State::key(const std::string &s, int tf) const {
    return s + "|" + intervals[tf];
}
Panel &State::current() {
    for (auto &p : panels)
        if (p->id == active)
            return *p;
    return *panels.front();
}
Panel &State::add(std::string symbol, bool duplicate) {
    if (panels.size() >= 64) {
        notice = "Close a chart before adding more than 64.";
        return current();
    }
    auto p = std::make_unique<Panel>();
    if (!panels.empty()) {
        auto &old = current();
        if (symbol.empty())
            symbol = old.symbol;
        if (duplicate) {
            p->tf = old.tf;
            p->indicators = old.indicators;
            p->candles = old.candles;
            p->ohlc = old.ohlc;
            p->logarithmic = old.logarithmic;
            p->volume = old.volume;
            p->show_drawings = old.show_drawings;
            p->pending = old.pending.is_null() ? encode_view(old.view, old.bars) : old.pending;
            p->pending_julia = old.pending_julia;
        }
    }
    if (symbol.empty())
        symbol = "SPY";
    p->symbol = normalize_symbol(symbol);
    p->id = next_id++;
    active = p->id;
    panels.push_back(std::move(p));
    relayout = true;
    return *panels.back();
}
void State::select(Panel &p, std::string s, int tf) {
    s = normalize_symbol(s);
    if (s == p.symbol && tf == p.tf)
        return;
    p.drawing = {};
    p.scroll = {};
    p.symbol = s;
    p.tf = tf;
    p.bars.clear();
    p.results.clear();
    p.signature.clear();
    p.pending = nullptr;
    p.revision = 0;
    p.dirty = true;
    p.view.fit(0);
}
Series &State::ensure(const std::string &s, int tf) {
    auto k = key(s, tf);
    auto [it, inserted] = series.try_emplace(k);
    auto &entry = it->second;
    if (inserted) {
        try {
            entry.history = decode_history(read_json(cache_path(directory, s, intervals[tf])));
            if (entry.history.symbol != s || entry.history.interval != intervals[tf])
                throw std::runtime_error("Mismatched cache");
            entry.loaded = true;
            entry.revision = 1;
            entry.next = 0;
            if (auto q = quote(entry.history))
                accept_quote(s, *q, false);
        } catch (...) {
            entry.loaded = false;
        }
    }
    return entry;
}
void State::repair(History h, std::function<void(History)> callback) {
    auto targets = recovery_targets(h, now());
    if (targets.empty()) {
        callback(std::move(h));
        return;
    }
    auto symbol = h.symbol;
    bool vix = symbol == "^VIX" && h.interval == "1d";
    auto url = history_url(symbol, vix ? "1h" : "1m", *targets.begin(), now());
    network.get(url, [h = std::move(h), targets, vix,
                      callback = std::move(callback)](std::string body, std::string error) mutable {
        try {
            if (!error.empty())
                throw std::runtime_error(error);
            auto intraday = parse_history(body, h.symbol, vix ? "1h" : "1m", true);
            h = vix ? recover_vix(std::move(h), intraday, targets, now())
                    : recover_crypto(std::move(h), intraday, targets, now());
        } catch (const std::exception &e) {
            h.meta["chartroomRecoveryError"] =
                std::string(vix ? "VIX recovery failed: " : "Crypto recovery failed: ") + e.what();
        }
        callback(std::move(h));
    });
}
void State::accept(const std::string &k, History h) {
    ++visual_revision;
    auto &entry = series.at(k);
    entry.history = entry.loaded ? merge_history(entry.history, h) : std::move(h);
    entry.loaded = !entry.history.bars.empty();
    entry.loading = false;
    entry.next = now() + 60;
    entry.error.clear();
    ++entry.revision;
    if (auto q = quote(entry.history))
        accept_quote(entry.history.symbol, *q);
    try {
        if (entry.loaded)
            atomic_json(cache_path(directory, entry.history.symbol, entry.history.interval),
                        encode_history(entry.history));
    } catch (const std::exception &e) {
        notice = e.what();
    }
}
void State::fetch(const std::string &symbol, int tf, bool full) {
    auto &entry = ensure(symbol, tf);
    if (offline || entry.loading)
        return;
    entry.loading = true;
    ++visual_revision;
    auto k = key(symbol, tf);
    Time clock = now(), start = 0;
    std::string interval = intervals[tf];
    if (entry.loaded && !full) {
        Time overlap = interval == "1wk" ? 14 * 86400 : 5 * 86400;
        start = std::min(clock - overlap, entry.history.bars.back().time - overlap);
    }
    if (interval == "1h")
        start = std::max(start, clock - 729 * 86400);
    fetch_quote(symbol);
    network.get(history_url(symbol, interval, start, clock),
                [this, k, symbol, interval](std::string body, std::string error) {
                    try {
                        if (!error.empty())
                            throw std::runtime_error(error);
                        auto h = parse_history(body, symbol, interval, series.at(k).loaded);
                        repair(std::move(h), [this, k](History repaired) { accept(k, std::move(repaired)); });
                    } catch (const std::exception &e) {
                        ++visual_revision;
                        auto &item = series.at(k);
                        item.error = e.what();
                        item.loading = false;
                        item.next = now() + 120;
                    }
                });
}
void State::accept_quote(const std::string &symbol, Quote q, bool persist) {
    if (!std::isfinite(q.price) || !std::isfinite(q.change) || q.asof <= 0 || q.fetched <= 0)
        return;
    auto old = quotes.find(symbol);
    if (old != quotes.end()) {
        auto &prior = old->second;
        if (q.asof < prior.asof ||
            (q.asof == prior.asof && ((prior.snapshot && !q.snapshot) ||
                                      (prior.snapshot == q.snapshot && q.fetched < prior.fetched))))
            return;
    }
    quotes[symbol] = q;
    quote_errors.erase(symbol);
    ++visual_revision;
    if (persist) {
        try {
            atomic_json(cache_path(directory, symbol, "quote"),
                        {{"version", 1},
                         {"symbol", symbol},
                         {"price", q.price},
                         {"change", q.change},
                         {"asof", date(q.asof, "%Y-%m-%dT%H:%M:%S")},
                         {"fetched_at", date(q.fetched, "%Y-%m-%dT%H:%M:%S")},
                         {"snapshot", q.snapshot}});
        } catch (const std::exception &e) {
            notice = e.what();
        }
    }
}
void State::fetch_quote(const std::string &symbol) {
    if (offline || quote_loading.count(symbol))
        return;
    quote_loading.insert(symbol);
    // Intraday metadata includes previousClose even when no bars traded in this five-minute window.
    // This avoids depending on complete daily OHLC rows (Yahoo sometimes omits yesterday's row).
    network.get(history_url(symbol, "1m", now() - 300, now()),
                [this, symbol](std::string body, std::string error) {
                    quote_loading.erase(symbol);
                    quote_next[symbol] = now() + 60;
                    ++visual_revision;
                    try {
                        if (!error.empty())
                            throw std::runtime_error(error);
                        accept_quote(symbol, parse_quote(body, symbol));
                    } catch (const std::exception &e) {
                        quote_errors[symbol] = e.what();
                    }
                });
}
void State::refresh(Panel &p) {
    fetch(p.symbol, p.tf, true);
}
void State::update(Panel &p) {
    auto &e = ensure(p.symbol, p.tf);
    if (!e.loaded)
        return;
    std::string sig = std::to_string(e.revision);
    for (auto &s : p.indicators) {
        sig += encode_indicator(s).dump();
        if (s.enabled && s.kind == "RIBBON" && s.timeframe > 0) {
            auto &dep = ensure(p.symbol, s.timeframe - 1);
            sig += std::to_string(dep.revision);
        }
    }
    if (sig == p.signature && !p.dirty)
        return;
    if (p.revision != e.revision || p.bars.empty()) {
        auto bars = p.tf == 1 ? aggregate(e.history.bars, 4) : e.history.bars;
        preserve_view(p.view, p.bars, bars);
        p.bars = std::move(bars);
        p.revision = e.revision;
        if (!p.pending.is_null()) {
            try {
                restore_view(p.view, p.bars, p.pending, p.pending_julia);
            } catch (const std::exception &error) {
                notice = error.what();
                p.view.fit(p.bars.size());
            }
            p.pending = nullptr;
            p.pending_julia = false;
        }
    }
    p.results.clear();
    for (auto &s : p.indicators)
        if (s.enabled) {
            const History *source = nullptr;
            if (s.kind == "RIBBON" && s.timeframe > 0) {
                auto &dep = ensure(p.symbol, s.timeframe - 1);
                if (dep.loaded)
                    source = &dep.history;
            }
            p.results.push_back(calculate(s, p.bars, p.tf, &e.history, source));
        }
    p.signature = std::move(sig);
    p.dirty = false;
}
std::set<std::pair<std::string, int>> State::needed_series() const {
    std::set<std::pair<std::string, int>> needed;
    for (auto &p : panels) {
        needed.insert({p->symbol, p->tf});
        for (auto &i : p->indicators)
            if (i.enabled && i.kind == "RIBBON" && i.timeframe > 0)
                needed.insert({p->symbol, i.timeframe - 1});
    }
    for (auto &symbol : core_symbols) {
        auto it = series.find(key(symbol, 2));
        if (it == series.end() || !it->second.loaded)
            needed.insert({symbol, 2});
    }
    return needed;
}
std::vector<std::string> State::visible_symbols() const {
    auto visible = core_symbols;
    for (auto &p : panels)
        visible.push_back(p->symbol);
    if (!lists.empty()) {
        auto &list = lists[size_t(selected_list)];
        visible.insert(visible.end(), list.symbols.begin(), list.symbols.end());
    }
    return visible;
}
bool State::tick() {
    auto before = visual_revision;
    network.poll();
    for (auto [symbol, tf] : needed_series()) {
        auto &e = ensure(symbol, tf);
        if (e.next <= now())
            fetch(symbol, tf);
    }
    for (auto &p : panels)
        update(*p);
    auto visible = visible_symbols();
    if (!offline && quote_tick <= now()) {
        quote_tick = now() + 1;
        for (auto &symbol : visible)
            if (!quote_loading.count(symbol) && quote_next[symbol] <= now()) {
                fetch_quote(symbol);
                break;
            }
    }
    for (auto &symbol : visible) {
        auto q = quotes.find(symbol);
        bool stale = q == quotes.end() || now() - q->second.fetched >= 600 || quote_errors.count(symbol);
        auto [it, inserted] = quote_stale.try_emplace(symbol, stale);
        if (inserted || it->second != stale) {
            it->second = stale;
            ++visual_revision;
        }
    }
    return before != visual_revision;
}
Time State::next_deadline() const {
    Time next = std::numeric_limits<Time>::max(), clock = now();
    if (save_pending && may_save)
        next = save_next;
    if (!offline) {
        for (auto &[symbol, tf] : needed_series()) {
            auto it = series.find(key(symbol, tf));
            if (it == series.end())
                next = std::min(next, clock);
            else if (!it->second.loading)
                next = std::min(next, it->second.next);
        }
        for (auto &symbol : visible_symbols()) {
            if (!quote_loading.count(symbol)) {
                auto it = quote_next.find(symbol);
                next = std::min(next, std::max(quote_tick, it == quote_next.end() ? clock : it->second));
            }
        }
    }
    for (auto &symbol : visible_symbols()) {
        auto it = quotes.find(symbol);
        if (it != quotes.end() && !quote_errors.count(symbol) && it->second.fetched + 600 > clock)
            next = std::min(next, it->second.fetched + 600);
    }
    return next;
}
Json State::document() const {
    Json charts = Json::array();
    for (auto &p : panels) {
        Json inds = Json::array();
        for (auto &i : p->indicators)
            inds.push_back(encode_indicator(i));
        charts.push_back({{"id", p->id},
                          {"symbol", p->symbol},
                          {"timeframe", p->tf},
                          {"view", p->pending.is_null() ? encode_view(p->view, p->bars) : p->pending},
                          {"view_julia", p->pending_julia},
                          {"candles", p->candles},
                          {"chart_style", !p->candles ? "line"
                                          : p->ohlc   ? "bars"
                                                      : "candles"},
                          {"volume", p->volume},
                          {"logarithmic", p->logarithmic},
                          {"show_drawings", p->show_drawings},
                          {"indicators", inds}});
    }
    Json ls = Json::array();
    for (auto &l : lists)
        ls.push_back({{"name", l.name}, {"symbols", l.symbols}});
    return {{"version", 1},
            {"format", "chartroom-cpp"},
            {"drawings", drawings.document()},
            {"charts", charts},
            {"active", active},
            {"layout", layout},
            {"ini", ini},
            {"lists", ls},
            {"selected_list", selected_list},
            {"width", width},
            {"height", height},
            {"x", x},
            {"y", y},
            {"maximized", maximized},
            {"drawing_tools_open", drawing_tools_open}};
}
void State::restore(const Json &j, bool julia) {
    if (j.at("version") != 1)
        throw std::runtime_error("Unsupported workspace version");
    if (!j.at("charts").is_array() || j["charts"].empty() || j["charts"].size() > 64)
        throw std::runtime_error("Invalid charts");
    if (!julia && j.value("format", std::string{}) != "chartroom-cpp")
        throw std::runtime_error("Use --import-julia for a Julia workspace");
    std::vector<std::unique_ptr<Panel>> loaded;
    std::set<int> ids;
    for (auto &c : j["charts"]) {
        auto p = std::make_unique<Panel>();
        p->id = c.at("id");
        if (p->id < 1 || !ids.insert(p->id).second)
            throw std::runtime_error("Invalid chart IDs");
        p->symbol = normalize_symbol(c.at("symbol"));
        p->tf = c.at("timeframe").get<int>() - (julia ? 1 : 0);
        if (p->tf < 0 || p->tf > 3)
            throw std::runtime_error("Invalid timeframe");
        p->pending = c.at("view");
        View check;
        restore_view(check, {}, p->pending, julia);
        p->pending_julia = julia || c.value("view_julia", false);
        auto style = c.value("chart_style", std::string(c.value("candles", true) ? "candles" : "line"));
        if (style != "candles" && style != "bars" && style != "line")
            throw std::runtime_error("Unknown chart style");
        p->candles = style != "line";
        p->ohlc = style == "bars";
        p->logarithmic = c.value("logarithmic", false);
        p->volume = c.value("volume", true);
        p->show_drawings = c.value("show_drawings", true);
        for (auto &i : c.value("indicators", Json::array()))
            p->indicators.push_back(decode_indicator(i));
        if (c.value("sma", false))
            p->indicators.push_back(indicator("SMA"));
        next_id = std::max(next_id, p->id + 1);
        loaded.push_back(std::move(p));
    }
    if (!julia && j.contains("lists")) {
        std::vector<List> ls;
        for (auto &l : j["lists"]) {
            List row{l.at("name"), {}};
            if (row.name.empty())
                throw std::runtime_error("Invalid list name");
            for (auto &s : l.at("symbols"))
                row.symbols.push_back(normalize_symbol(s));
            ls.push_back(std::move(row));
        }
        if (!ls.empty())
            lists = std::move(ls);
        selected_list = std::clamp(j.value("selected_list", 0), 0, int(lists.size() - 1));
    }
    if (!julia)
        drawings.restore(j.value("drawings", Json::object()));
    panels = std::move(loaded);
    active = j.value("active", panels.front()->id);
    layout = j.value("layout", std::string("Grid"));
    if (layout != "Grid" && layout != "Rows" && layout != "Columns" && layout != "Tabs")
        layout = "Grid";
    if (!julia) {
        ini = j.value("ini", std::string{});
        width = std::clamp(j.value("width", 1440), 640, 7680);
        height = std::clamp(j.value("height", 900), 400, 4320);
        x = j.value("x", -1);
        y = j.value("y", -1);
        maximized = j.value("maximized", false);
        drawing_tools_open = j.value("drawing_tools_open", false);
    }
    relayout = ini.empty();
}
void State::import_julia(const fs::path &dir) {
    auto source = dir / "workspace.json";
    if (fs::exists(source))
        restore(read_json(source), true);
    if (fs::exists(dir / "watchlists.json")) {
        auto j = read_json(dir / "watchlists.json");
        std::vector<List> ls;
        for (auto &row : j.at("lists")) {
            List l{row.at("name"), {}};
            for (auto &s : row.at("symbols"))
                l.symbols.push_back(normalize_symbol(s));
            ls.push_back(std::move(l));
        }
        if (!ls.empty()) {
            lists = std::move(ls);
            selected_list = std::clamp(j.value("selected", 1) - 1, 0, int(lists.size() - 1));
        }
    }
    if (fs::exists(dir / "cache")) {
        fs::create_directories(directory / "cache");
        for (auto &entry : fs::directory_iterator(dir / "cache"))
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                fs::copy_file(entry.path(), directory / "cache" / entry.path().filename(),
                              fs::copy_options::skip_existing);
    }
    notice = "Imported Julia workspace and cache into this app's own storage.";
}
void State::save(bool force) {
    if (!may_save)
        return;
    if (!force && (!save_pending || save_next > now()))
        return;
    save_next = now() + 1;
    save_pending = false;
    auto doc = document();
    auto text = doc.dump();
    if (!force && text == last_saved)
        return;
    try {
        atomic_json(directory / "workspace.json", doc);
        last_saved = std::move(text);
    } catch (const std::exception &e) {
        notice = e.what();
    }
}
void State::remove_closed() {
    if (panels.size() == 1 || std::none_of(panels.begin(), panels.end(), [](auto &p) { return p->open; }))
        panels.front()->open = true;
    auto n = panels.size();
    panels.erase(std::remove_if(panels.begin(), panels.end(), [](auto &p) { return !p->open; }),
                 panels.end());
    if (n != panels.size()) {
        active = current().id;
        relayout = true;
    }
}
} // namespace cr
