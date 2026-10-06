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
// S&P 500 constituents as of 2026-09-21; the live list refreshes from live_list_url("sp500").
static const std::vector<std::string> sp500_snapshot = {
        "A", "AAPL", "ABBV", "ABNB", "ABT", "ACGL", "ACN", "ADBE", "ADI", "ADM", "ADP", "ADSK", "AEE", "AEP",
        "AES", "AFL", "AIG", "AIZ", "AJG", "AKAM", "ALB", "ALGN", "ALL", "ALLE", "AMAT", "AMCR", "AMD", "AME",
        "AMGN", "AMP", "AMT", "AMZN", "ANET", "AON", "AOS", "APA", "APD", "APH", "APO", "APP", "APTV", "ARE",
        "ARES", "ATO", "AVGO", "AVY", "AWK", "AXON", "AXP", "AZO", "BA", "BAC", "BALL", "BAX", "BBY", "BDX",
        "BE", "BEN", "BF-B", "BG", "BIIB", "BKNG", "BKR", "BLK", "BMY", "BNY", "BR", "BRK-B", "BRO", "BSX",
        "BX", "BXP", "C", "CAH", "CARR", "CASY", "CAT", "CB", "CBOE", "CBRE", "CCI", "CCL", "CDNS", "CDW",
        "CEG", "CF", "CFG", "CHD", "CHRW", "CHTR", "CI", "CIEN", "CINF", "CL", "CLX", "CMCSA", "CME", "CMG",
        "CMI", "CMS", "CNC", "CNP", "COF", "COHR", "COIN", "COO", "COP", "COR", "COST", "CPAY", "CPRT", "CPT",
        "CRH", "CRL", "CRM", "CRWD", "CSCO", "CSGP", "CSX", "CTAS", "CTSH", "CTVA", "CVNA", "CVS", "CVX", "D",
        "DAL", "DASH", "DD", "DDOG", "DE", "DECK", "DELL", "DG", "DGX", "DHI", "DHR", "DIS", "DLR", "DLTR",
        "DOC", "DOV", "DOW", "DPZ", "DRI", "DTE", "DUK", "DVA", "DVN", "DXCM", "EBAY", "ECHO", "ECL", "ED",
        "EFX", "EG", "EIX", "EL", "ELV", "EME", "EMR", "EOG", "EQIX", "EQT", "ERIE", "ES", "ESS", "ETN", "ETR",
        "EVRG", "EW", "EXC", "EXE", "EXPD", "EXPE", "EXR", "F", "FANG", "FAST", "FCX", "FDS", "FDX", "FDXF",
        "FE", "FERG", "FFIV", "FICO", "FIS", "FISV", "FITB", "FIX", "FLEX", "FOX", "FOXA", "FRT", "FSLR",
        "FTNT", "FTV", "GD", "GDDY", "GE", "GEHC", "GEN", "GEV", "GILD", "GIS", "GL", "GLW", "GM", "GNRC",
        "GOOG", "GOOGL", "GPC", "GPN", "GRMN", "GS", "GWW", "HAL", "HAS", "HBAN", "HCA", "HD", "HIG", "HII",
        "HLT", "HON", "HONA", "HOOD", "HPE", "HPQ", "HRL", "HSIC", "HST", "HSY", "HUBB", "HUM", "HWM", "IBKR",
        "IBM", "ICE", "IDXX", "IEX", "IFF", "ILMN", "INCY", "INTC", "INTU", "INVH", "IP", "IQV", "IR", "IRM",
        "ISRG", "IT", "ITW", "IVZ", "J", "JBHT", "JBL", "JCI", "JKHY", "JNJ", "JPM", "KDP", "KEY", "KEYS",
        "KHC", "KIM", "KKR", "KLAC", "KMB", "KMI", "KO", "KR", "KVUE", "L", "LDOS", "LEN", "LH", "LHX", "LII",
        "LIN", "LITE", "LLY", "LMT", "LNT", "LOW", "LRCX", "LULU", "LUV", "LVS", "LYB", "LYV", "MA", "MAA",
        "MAR", "MAS", "MCD", "MCHP", "MCK", "MCO", "MDLZ", "MDT", "MET", "META", "MGM", "MKC", "MLM", "MMM",
        "MNST", "MO", "MOS", "MPC", "MPWR", "MRK", "MRNA", "MRSH", "MRVL", "MS", "MSCI", "MSFT", "MSI", "MTB",
        "MTD", "MU", "NCLH", "NDAQ", "NDSN", "NEE", "NEM", "NFLX", "NI", "NKE", "NOC", "NOW", "NRG", "NSC",
        "NTAP", "NTRS", "NUE", "NVDA", "NVR", "NWS", "NWSA", "NXPI", "O", "ODFL", "OKE", "OMC", "ON", "ORCL",
        "ORLY", "OTIS", "OXY", "P", "PANW", "PAYX", "PCAR", "PCG", "PEG", "PEP", "PFE", "PFG", "PG", "PGR",
        "PH", "PHM", "PKG", "PLD", "PLTR", "PM", "PNC", "PNR", "PNW", "PODD", "PPG", "PPL", "PRU", "PSA",
        "PSKY", "PSX", "PTC", "PWR", "PYPL", "Q", "QCOM", "RCL", "RDDT", "REG", "REGN", "RF", "RJF", "RL",
        "RMD", "ROK", "ROL", "ROP", "ROST", "RSG", "RTX", "RVTY", "SBAC", "SBUX", "SCHW", "SHW", "SJM", "SLB",
        "SMCI", "SNA", "SNDK", "SNPS", "SO", "SOLV", "SPG", "SPGI", "SRE", "STE", "STLD", "STT", "STX", "STZ",
        "SW", "SWK", "SWKS", "SYF", "SYK", "SYY", "T", "TDG", "TDY", "TECH", "TEL", "TER", "TFC", "TGT", "TJX",
        "TKO", "TMO", "TMUS", "TPL", "TPR", "TRGP", "TRMB", "TROW", "TRV", "TSCO", "TSLA", "TSN", "TT", "TTWO",
        "TXN", "TXT", "TYL", "UAL", "UBER", "UDR", "UHS", "ULTA", "UNH", "UNP", "UPS", "URI", "USB", "V",
        "VEEV", "VICI", "VLO", "VLTO", "VMC", "VMRK", "VRSK", "VRSN", "VRT", "VRTX", "VST", "VTR", "VTRS",
        "VZ", "WAB", "WAT", "WBD", "WDAY", "WDC", "WEC", "WELL", "WFC", "WM", "WMB", "WMT", "WRB", "WSM",
        "WST", "WTW", "WY", "WYNN", "XEL", "XOM", "XYL", "XYZ", "YUM", "ZBH", "ZBRA", "ZTS"
};
static std::vector<List> starter_lists() {
    return {
        {"At a glance", core_symbols},
        {"Big names", big_names},
        {"S&P 500", sp500_snapshot, "sp500"},
        {"Open 85-85", {}, "open8585"},
        {"Indices & broad markets",
         {"SPY", "QQQ", "RSP", "DIA", "IWM", "VTI", "VT", "^GSPC", "^DJI", "^IXIC", "^VIX"}},
        {"Crypto",
         {"BTC-USD", "ETH-USD", "SOL-USD", "XRP-USD", "BNB-USD", "ADA-USD", "DOGE-USD", "AVAX-USD",
          "LINK-USD", "LTC-USD", "BCH-USD"}},
        {"Metals & miners",
         {"GLD", "IAU", "SLV", "PPLT", "PALL", "CPER", "GDX", "GDXJ", "SIL", "COPX", "GC=F", "SI=F", "HG=F"}},
        {"US sectors", {"XLK", "XLF", "XLV", "XLY", "XLP", "XLE", "XLI", "XLB", "XLU", "XLRE", "XLC"}},
        {"Futures",
         {"ES=F", "NQ=F", "YM=F", "RTY=F", "CL=F", "NG=F", "GC=F", "SI=F", "HG=F", "ZC=F", "ZW=F", "ZS=F",
          "ZB=F", "ZN=F"}},
        {"Macro & economic proxies",
         {"^VIX", "^IRX", "^FVX", "^TNX", "^TYX", "DX-Y.NYB", "TIP", "HYG", "LQD", "TLT", "GLD", "USO", "DBA",
          "DBC"}},
        {"Bonds & credit",
         {"SGOV", "SHY", "IEF", "TLT", "TIP", "BND", "AGG", "LQD", "HYG", "JNK", "BKLN", "EMB", "MUB"}},
        {"Currencies",
         {"EURUSD=X", "GBPUSD=X", "JPY=X", "CHF=X", "CAD=X", "AUDUSD=X", "NZDUSD=X", "CNY=X", "DX-Y.NYB"}},
        {"Global markets",
         {"VEA", "VWO", "EFA", "EEM", "EWJ", "EWU", "EWG", "EWQ", "EWC", "EWA", "INDA", "FXI", "EWZ", "EWT",
          "EWY"}},
        {"Semiconductors",
         {"SMH", "SOXX", "NVDA", "AMD", "AVGO", "TSM", "ASML", "AMAT", "LRCX", "KLAC", "MU", "QCOM", "INTC",
          "TXN", "ADI"}},
        {"Software & cybersecurity",
         {"IGV", "CIBR", "MSFT", "ORCL", "CRM", "ADBE", "NOW", "PLTR", "SNOW", "DDOG", "CRWD", "PANW", "FTNT",
          "ZS", "NET"}},
        {"Internet & communication",
         {"GOOGL", "META", "AMZN", "NFLX", "SPOT", "DASH", "UBER", "ABNB", "BKNG", "EBAY", "T", "VZ", "TMUS",
          "DIS"}},
        {"Banks & financials",
         {"KRE", "KBE", "JPM", "BAC", "WFC", "C", "GS", "MS", "SCHW", "BLK", "BRK-B", "V", "MA", "AXP", "CME",
          "ICE"}},
        {"Healthcare & biotech",
         {"IBB", "XBI", "LLY", "JNJ", "ABBV", "MRK", "PFE", "AMGN", "GILD", "REGN", "VRTX", "UNH", "TMO",
          "ISRG"}},
        {"Consumer & retail",
         {"XRT", "WMT", "COST", "TGT", "HD", "LOW", "AMZN", "NKE", "LULU", "MCD", "SBUX", "KO", "PEP", "PG",
          "TSLA", "GM", "F"}},
        {"Energy",
         {"XLE", "XOP", "OIH", "XOM", "CVX", "COP", "EOG", "SLB", "HAL", "MPC", "VLO", "PSX", "LNG", "KMI",
          "WMB"}},
        {"Industrials & defense",
         {"ITA", "XAR", "CAT", "DE", "GE", "HON", "ETN", "UNP", "UPS", "FDX", "BA", "RTX", "LMT", "NOC",
          "GD"}},
        {"Real estate & utilities",
         {"VNQ", "XLRE", "XLU", "AMT", "PLD", "EQIX", "O", "SPG", "WELL", "NEE", "DUK", "SO", "AEP", "CEG",
          "VST"}},
        {"Styles & factors",
         {"VUG", "VTV", "IWF", "IWD", "MTUM", "QUAL", "USMV", "VLUE", "SIZE", "SCHD", "VIG", "IWM", "IWO",
          "IWN", "RSP"}}};
}
WatchlistWindow &State::add_watchlist(int list) {
    auto w = std::make_unique<WatchlistWindow>();
    w->id = next_watchlist_id++;
    w->list = std::clamp(list < 0 ? selected_list : list, 0, int(lists.size()) - 1);
    w->focus = true;
    watchlists.push_back(std::move(w));
    request_save();
    return *watchlists.back();
}
void State::delete_list(int index) {
    if (lists.size() <= 1 || index < 0 || index >= int(lists.size()))
        return;
    lists.erase(lists.begin() + index);
    auto remap = [&](int &i) { i = std::clamp(i > index ? i - 1 : i, 0, int(lists.size()) - 1); };
    remap(selected_list);
    for (auto &w : watchlists)
        remap(w->list);
    request_save();
}
State::State(fs::path dir, bool offline_, fs::path import) : directory(std::move(dir)), offline(offline_) {
    lists = starter_lists();
    selected_list = 1;
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
    if (watchlist_catalog < 2) {
        // Catalog 2 added the live lists; earlier catalogs keep any starters the user deleted.
        for (auto &list : starter_lists())
            if ((watchlist_catalog < 1 || !list.source.empty()) &&
                std::none_of(lists.begin(), lists.end(), [&](const List &old) {
                    return old.name == list.name || (!list.source.empty() && old.source == list.source);
                }))
                lists.push_back(std::move(list));
        if (watchlist_catalog < 1) {
            int favorites = int(std::find_if(lists.begin(), lists.end(),
                                             [](const List &l) { return l.name == "At a glance"; }) -
                                lists.begin());
            add_watchlist(favorites);
            add_watchlist(selected_list);
            relayout = true;
        }
        watchlist_catalog = 2;
    }
    std::set<std::string> cached_symbols;
    for (auto &p : panels)
        cached_symbols.insert(p->symbol);
    for (auto &list : lists)
        cached_symbols.insert(list.symbols.begin(), list.symbols.end());
    for (auto &s : cached_symbols) {
        try {
            auto j = read_json(cache_path(directory, s, "quote"));
            if (j.at("symbol") != s)
                continue;
            // Old Nasdaq snapshots could contain an extended-hours percentage.
            if (j.value("source", std::string{}) == "Nasdaq" && j.value("version", 1) < 2)
                continue;
            Quote q{j.at("price"),
                    j.at("change"),
                    parse_time(j.at("asof")),
                    parse_time(j.at("fetched_at")),
                    j.value("snapshot", false),
                    j.value("source", std::string("Yahoo")),
                    j.value("rolling", false)};
            if (j.contains("extended") && j["extended"].is_object()) {
                auto &e = j["extended"];
                double price = e.at("price");
                std::string session = e.at("session");
                if (std::isfinite(price) && price > 0 && (session == "Pre" || session == "Post"))
                    q.extended = ExtendedQuote{price, e.at("asof"), e.at("fetched"), session,
                                               e.value("source", q.source)};
            }
            accept_quote(s, q, false);
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
            p->point_figure = old.point_figure;
            p->pnf = old.pnf;
            p->logarithmic = old.logarithmic;
            p->volume = old.volume;
            p->earnings = old.earnings;
            p->eps = old.eps;
            p->eps_ttm = old.eps_ttm;
            p->show_drawings = old.show_drawings;
            if (normalize_symbol(symbol) == old.symbol)
                p->option_marker = old.option_marker;
            p->option_ladder = old.option_ladder;
            p->ladder_volume = old.ladder_volume;
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
    if (s != p.symbol)
        p.option_marker.reset();
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
    if (is_expression(s)) {
        derive(s, tf, entry);
        return entry;
    }
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
// Expressions own no network state: their legs load, cache and refresh as ordinary charts.
void State::derive(const std::string &s, int tf, Series &entry) {
    Expression e;
    try {
        e = parse_expression(s);
    } catch (const std::exception &error) {
        entry.error = error.what();
        entry.next = std::numeric_limits<Time>::max();
        return;
    }
    std::vector<const History *> legs;
    std::string signature, error;
    bool loading = false, loaded = true;
    Time next = std::numeric_limits<Time>::max();
    for (auto &symbol : e.symbols) {
        auto &leg = ensure(symbol, tf);
        legs.push_back(&leg.history);
        signature += std::to_string(leg.revision) + (leg.loaded ? "+" : "-") + "|";
        loading |= leg.loading;
        loaded &= leg.loaded;
        next = std::min(next, leg.next);
        if (error.empty() && !leg.error.empty())
            error = display_symbol(symbol) + ": " + leg.error;
    }
    entry.loading = loading;
    entry.manual &= loading;
    entry.next = next;
    entry.error = error;
    if (!loaded || signature == entry.derived)
        return;
    entry.derived = signature;
    entry.history = combine_expression(s, legs);
    entry.loaded = !entry.history.bars.empty();
    if (!entry.loaded && entry.error.empty())
        entry.error = "These tickers have no overlapping bars.";
    ++entry.revision;
    ++visual_revision;
}
void State::derive_quotes(const std::vector<std::string> &visible) {
    for (auto &symbol : visible) {
        if (!is_expression(symbol))
            continue;
        Expression e;
        try {
            e = parse_expression(symbol);
        } catch (...) {
            continue;
        }
        std::map<std::string, double> price, previous;
        Quote q;
        q.asof = q.fetched = std::numeric_limits<Time>::max();
        q.source = "Computed";
        bool complete = true;
        for (auto &leg : e.symbols) {
            auto it = quotes.find(leg);
            if (it == quotes.end() || quote_errors.count(leg)) {
                complete = false;
                break;
            }
            auto &lq = it->second;
            price[leg] = lq.price;
            previous[leg] = lq.price / (1 + lq.change / 100);
            q.asof = std::min(q.asof, lq.asof);
            q.fetched = std::min(q.fetched, lq.fetched);
            q.snapshot |= lq.snapshot;
            q.rolling |= lq.rolling;
        }
        if (!complete)
            continue;
        q.price = e.evaluate(price);
        double before = e.evaluate(previous);
        q.change = before != 0 ? (q.price / before - 1) * 100 : missing;
        if (!std::isfinite(q.price) || !std::isfinite(q.change))
            continue;
        auto old = quotes.find(symbol);
        if (old != quotes.end() && old->second.price == q.price && old->second.change == q.change &&
            old->second.asof == q.asof && old->second.fetched == q.fetched)
            continue;
        quotes[symbol] = q;
        ++visual_revision;
    }
}
void State::repair(History h, std::function<void(History)> callback) {
    auto targets = data_source(h) == "Yahoo" ? recovery_targets(h, now()) : std::set<Time>{};
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
    auto family = [](const History &history) {
        auto source = data_source(history);
        return source.starts_with("Nasdaq") ? std::string("Nasdaq") : source;
    };
    entry.history =
        entry.loaded && family(entry.history) == family(h) ? merge_history(entry.history, h) : std::move(h);
    entry.loaded = !entry.history.bars.empty();
    entry.loading = false;
    entry.manual = false;
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
    if (is_expression(symbol)) {
        try {
            for (auto &leg : parse_expression(symbol).symbols)
                if (full || ensure(leg, tf).next <= now())
                    fetch(leg, tf, full);
        } catch (...) {
        }
        ensure(symbol, tf);
        return;
    }
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
    providers.history(symbol, interval, start, entry.loaded ? data_source(entry.history) : "",
                      [this, k](History h, std::string error) {
                          if (error.empty()) {
                              repair(std::move(h),
                                     [this, k](History repaired) { accept(k, std::move(repaired)); });
                          } else {
                              ++visual_revision;
                              auto &item = series.at(k);
                              item.error = std::move(error);
                              item.loading = item.manual = false;
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
                                      (prior.snapshot == q.snapshot && q.fetched < prior.fetched)))) {
            // Provider regular-session timestamps can differ; a newer extended quote
            // still updates independently without rolling the regular quote backward.
            if (!q.extended || q.extended->asof <= prior.asof ||
                (prior.extended && q.extended->asof <= prior.extended->asof))
                return;
            auto extended = q.extended;
            q = prior;
            q.extended = extended;
        }
        // History refreshes must not erase a newer, separately timestamped live quote.
        if (prior.extended && prior.extended->asof > q.asof &&
            (!q.extended || q.extended->asof < prior.extended->asof))
            q.extended = prior.extended;
    }
    quotes[symbol] = q;
    quote_errors.erase(symbol);
    ++visual_revision;
    if (persist) {
        try {
            atomic_json(cache_path(directory, symbol, "quote"),
                        {{"version", 2},
                         {"symbol", symbol},
                         {"price", q.price},
                         {"change", q.change},
                         {"asof", date(q.asof, "%Y-%m-%dT%H:%M:%S")},
                         {"fetched_at", date(q.fetched, "%Y-%m-%dT%H:%M:%S")},
                         {"snapshot", q.snapshot},
                         {"source", q.source},
                         {"rolling", q.rolling},
                         {"extended", q.extended ? Json{{"price", q.extended->price},
                                                        {"asof", q.extended->asof},
                                                        {"fetched", q.extended->fetched},
                                                        {"session", q.extended->session},
                                                        {"source", q.extended->source}}
                                                 : Json{}}});
        } catch (const std::exception &e) {
            notice = e.what();
        }
    }
}
void State::refresh_live_lists() {
    if (offline)
        return;
    for (auto &w : watchlists) {
        if (!w->open)
            continue;
        auto &l = lists[size_t(w->list)];
        if (l.source.empty() || l.loading || l.next > now())
            continue;
        l.loading = true;
        // Lists may be deleted or reordered while the request is in flight; find it again by source.
        network.get(live_list_url(l.source), [this, source = l.source](std::string body, std::string error) {
            auto it = std::find_if(lists.begin(), lists.end(), [&](const List &l) { return l.source == source; });
            if (it == lists.end())
                return;
            it->loading = false;
            it->next = now() + (error.empty() ? 3600 : 900);
            ++visual_revision;
            try {
                if (!error.empty())
                    throw std::runtime_error(error);
                auto live = parse_live_list(source, body);
                if (live.symbols != it->symbols)
                    request_save();
                it->symbols = std::move(live.symbols);
                it->asof = live.asof.empty() ? local_date(now(), "%Y-%m-%d") : live.asof;
                it->error.clear();
            } catch (const std::exception &e) {
                it->error = e.what();
            }
        });
    }
}
void State::fetch_quote(const std::string &symbol) {
    if (offline || quote_loading.count(symbol))
        return;
    quote_loading.insert(symbol);
    providers.quote(symbol, [this, symbol](Quote q, std::string error) {
        quote_loading.erase(symbol);
        quote_next[symbol] = now() + 60;
        ++visual_revision;
        if (error.empty())
            accept_quote(symbol, std::move(q));
        else
            quote_errors[symbol] = std::move(error);
    });
}
static Json options_cache(const OptionChain &chain) {
    Json rows = Json::array();
    for (auto &r : chain.rows) {
        Json row = {{"expirygroup", date(parse_time(r.expiry + "T00:00:00"), "%B %d, %Y")},
                    {"strike", r.strike},
                    {"drillDownURL", ""}};
        for (auto [prefix, side] : {std::pair{"c_", r.call}, {"p_", r.put}}) {
            row[std::string(prefix) + "Last"] = side.last;
            row[std::string(prefix) + "Bid"] = side.bid;
            row[std::string(prefix) + "Ask"] = side.ask;
            row[std::string(prefix) + "Volume"] = side.volume;
            row[std::string(prefix) + "Openinterest"] = side.interest;
            row[std::string(prefix) + "colour"] = side.itm;
        }
        rows.push_back(std::move(row));
    }
    return {{"data",
             {{"totalRecord", chain.total}, {"lastTrade", chain.last_trade}, {"table", {{"rows", rows}}}}},
            {"fetched", chain.fetched},
            {"truncated", chain.truncated}};
}
void State::refresh_option_dates(bool full) {
    auto symbol = options.symbol;
    auto path = cache_path(directory, symbol, "option-dates");
    if (options.dates_symbol != symbol) {
        ++options.dates_generation;
        options.dates_symbol = symbol;
        options.dates = {};
        options.dates_error.clear();
        options.dates_loading = false;
        options.dates_retry = 0;
        try {
            auto j = read_json(path);
            for (auto &d : j.at("dates")) {
                auto value = d.get<std::string>();
                if (valid_expiry(value) && value >= date(now(), "%Y-%m-%d"))
                    options.dates.dates.push_back(value);
            }
            options.dates.fetched = j.at("fetched");
            options.dates.complete = j.value("complete", false);
        } catch (...) {
            options.dates = {};
        }
    }
    if (offline || options.dates_loading || options.dates_retry > now())
        return;
    if (!options.dates.dates.empty() && options.dates.fetched + 86400 > now() &&
        (!full || options.dates.complete))
        return;
    options.dates_loading = true;
    auto generation = ++options.dates_generation;
    providers.option_dates(symbol, full,
                           [this, generation, symbol, path](OptionDates dates, std::string error) {
                               if (options.dates_generation != generation || options.symbol != symbol)
                                   return;
                               options.dates_loading = false;
                               options.dates_error = std::move(error);
                               options.dates_retry = now() + (options.dates_error.empty() ? 0 : 120);
                               ++visual_revision;
                               if (!dates.dates.empty()) {
                                   options.dates = std::move(dates);
                                   try {
                                       atomic_json(path, {{"dates", options.dates.dates},
                                                          {"fetched", options.dates.fetched},
                                                          {"complete", options.dates.complete}});
                                   } catch (const std::exception &e) {
                                       notice = e.what();
                                   }
                                   if (options_visible())
                                       refresh_options();
                               }
                           });
}
void State::refresh_options(bool force) {
    refresh_option_dates();
    if (!valid_expiry(options.expiry) || options.expiry < date(now(), "%Y-%m-%d")) {
        options.expiry.clear();
        if (!options.dates.dates.empty())
            options.expiry = options.dates.dates.front();
    }
    auto key = options.symbol + "|" + options.expiry;
    if (options.loading && options.key == key)
        return;
    auto path = cache_path(directory, options.symbol, "options-" + options.expiry);
    if (options.key != key) {
        ++options.generation;
        options.data = {};
        options.error.clear();
        options.loading = false;
        try {
            auto j = read_json(path);
            options.data = parse_options(j.dump());
            options.data.fetched = j.at("fetched");
            options.data.truncated = j.value("truncated", false);
        } catch (...) {
        }
    }
    options.key = key;
    if (options.expiry.empty()) {
        options.next = now() + 120;
        return;
    }
    if (offline)
        return;
    if (!force && options.data.fetched > now() - 60) {
        options.next = options.data.fetched + 60;
        return;
    }
    auto generation = ++options.generation;
    options.loading = true;
    ++visual_revision;
    auto symbol = options.symbol, expiry = options.expiry;
    providers.options(
        symbol, expiry, [this, generation, symbol, expiry, path](OptionChain data, std::string error) {
            // A slower previous expiry/symbol must never overwrite the user's latest selection.
            if (generation != options.generation || options.symbol != symbol || options.expiry != expiry)
                return;
            options.loading = false;
            options.next = now() + (error.empty() ? 60 : 120);
            options.error = std::move(error);
            ++visual_revision;
            if (!options.error.empty())
                return;
            options.data = std::move(data);
            try {
                atomic_json(path, options_cache(options.data));
            } catch (const std::exception &e) {
                notice = e.what();
            }
        });
}
void State::refresh_screener() {
    auto key = scanner_request(screener.query).dump();
    if (screener.loading && screener.key == key)
        return;
    auto path = directory / "cache" / "screener.json";
    if (screener.key != key) {
        screener.data = {};
        screener.error.clear();
        try {
            auto j = read_json(path);
            if (j.at("query") == key) {
                screener.data = parse_screen(j.dump());
                screener.data.fetched = j.at("fetched");
            }
        } catch (...) {
        }
    }
    screener.key = key;
    auto generation = ++screener.generation;
    screener.loading = !offline;
    if (offline)
        return;
    ++visual_revision;
    providers.screen(screener.query, [this, generation, path, key](ScreenResult data, std::string error) {
        if (generation != screener.generation)
            return;
        screener.loading = false;
        screener.next = now() + (error.empty() ? 60 : 120);
        screener.error = std::move(error);
        ++visual_revision;
        if (!screener.error.empty())
            return;
        screener.data = std::move(data);
        Json rows = Json::array();
        for (auto &r : screener.data.rows)
            rows.push_back({{"s", r.exchange + ":" + r.symbol},
                            {"d", {r.name, r.price, r.change, r.cap, r.sector, r.volume, r.exchange}}});
        try {
            atomic_json(path, {{"query", key},
                               {"data", rows},
                               {"totalCount", screener.data.total},
                               {"fetched", screener.data.fetched}});
        } catch (const std::exception &e) {
            notice = e.what();
        }
    });
}
void State::refresh(Panel &p) {
    fetch(p.symbol, p.tf, true);
    auto &entry = ensure(p.symbol, p.tf);
    entry.manual = entry.loading;
}
const ProfileEntry &State::profile(const std::string &symbol) {
    auto [it, inserted] = profiles.try_emplace(symbol);
    auto &entry = it->second;
    if (is_expression(symbol)) {
        if (inserted) {
            entry.loaded = true;
            entry.data.type = "Computed from tickers";
            entry.data.name = display_symbol(symbol);
        }
        return entry;
    }
    auto path = cache_path(directory, symbol, "profile");
    if (inserted) {
        try {
            auto j = read_json(path);
            if (j.at("symbol") == symbol) {
                entry.data = {j.at("name").get<std::string>(), j.value("type", std::string{}),
                              j.value("sector", std::string{}), j.value("industry", std::string{})};
                entry.fetched = j.at("fetched");
                entry.loaded = true;
                entry.next = entry.fetched + 30 * 86400; // Names and sectors rarely change.
            }
        } catch (...) {
        }
    }
    if (offline || entry.loading || entry.next > now())
        return entry;
    entry.loading = true;
    network.get(profile_url(symbol), [this, symbol, path](std::string body, std::string error) {
        auto &entry = profiles[symbol];
        entry.loading = false;
        ++visual_revision;
        try {
            if (!error.empty())
                throw std::runtime_error(error);
            entry.data = parse_profile(body, symbol);
            entry.fetched = now();
            entry.next = entry.fetched + 30 * 86400;
            entry.loaded = true;
            entry.error.clear();
            atomic_json(path, {{"symbol", symbol},
                               {"name", entry.data.name},
                               {"type", entry.data.type},
                               {"sector", entry.data.sector},
                               {"industry", entry.data.industry},
                               {"fetched", entry.fetched}});
        } catch (const std::exception &e) {
            entry.error = e.what();
            entry.next = now() + 3600;
        }
    });
    return entry;
}
std::set<std::string> State::needed_companies() const {
    std::set<std::string> symbols;
    for (auto &p : panels)
        if (p->open && (p->earnings || p->eps)) {
            // Earnings and fundamentals wait until the chart has bars, so they never delay its history.
            auto it = series.find(key(p->symbol, p->tf));
            if (it != series.end() && it->second.loaded)
                symbols.insert(p->symbol);
        }
    if (fundamentals.open) {
        std::string symbol = fundamentals.symbol;
        if (fundamentals.follow)
            for (auto &p : panels)
                if (p->id == active)
                    symbol = p->symbol;
        symbols.insert(symbol);
    }
    return symbols;
}
CompanySnapshot &State::company(const std::string &symbol, bool force) {
    auto &c = companies[symbol];
    auto path = cache_path(directory, symbol, "fundamentals");
    if (!c.initialized) {
        c.initialized = true;
        c.data.symbol = symbol;
        try {
            auto saved = read_json(path);
            if (saved.value("symbol", std::string{}) == symbol) {
                c.parts = saved.at("parts");
                c.part_times = saved.value("part_times", Json::object());
                c.error = saved.value("error", std::string{});
                c.data = parse_fundamentals(c.parts, symbol);
                c.data.fetched = saved.value("fetched", Time{});
                c.next = c.data.fetched + (c.error.empty() ? 21600 : 900);
                ++c.revision;
            }
        } catch (...) {
        }
    }
    if (!nasdaq_symbol(symbol)) {
        c.error = "Company fundamentals are available for US stocks and ETFs.";
        c.next = std::numeric_limits<Time>::max();
        return c;
    }
    if (offline || c.loading || (!force && c.next > now()))
        return c;
    c.loading = true;
    providers.fundamentals(symbol, [this, symbol, path](Json parts, std::string error) {
        auto &c = companies[symbol];
        c.loading = false;
        c.error = std::move(error);
        c.next = now() + (c.error.empty() ? 21600 : 900);
        ++visual_revision;
        if (parts.empty())
            return;
        // Preserve older reports as the provider's short rolling window advances.
        if (parts.contains("earnings") && c.parts.contains("earnings")) {
            try {
                auto &rows = parts["earnings"]["data"]["earningsSurpriseTable"]["rows"];
                std::map<std::string, Json> merged;
                for (auto &row : c.parts["earnings"]["data"]["earningsSurpriseTable"]["rows"])
                    merged[row.at("dateReported").get<std::string>()] = row;
                for (auto &row : rows)
                    merged[row.at("dateReported").get<std::string>()] = row;
                rows = Json::array();
                for (auto &[day, row] : merged)
                    rows.push_back(row);
            } catch (...) {
            }
        }
        for (auto &[name, part] : parts.items()) {
            c.parts[name] = std::move(part);
            c.part_times[name] = now();
        }
        try {
            auto data = parse_fundamentals(c.parts, symbol);
            data.fetched = now();
            c.data = std::move(data);
            ++c.revision;
            atomic_json(path, {{"symbol", symbol},
                               {"parts", c.parts},
                               {"fetched", c.data.fetched},
                               {"part_times", c.part_times},
                               {"error", c.error}});
        } catch (const std::exception &e) {
            c.error = e.what();
        }
    });
    return c;
}
void State::update(Panel &p) {
    auto &e = ensure(p.symbol, p.tf);
    if (!e.loaded)
        return;
    std::string sig = std::to_string(e.revision);
    if (p.point_figure)
        sig += "pnf" + encode_pnf(p.pnf).dump();
    if (p.eps)
        sig += "eps" + std::to_string(company(p.symbol).revision) + std::to_string(p.eps_ttm);
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
    if (p.point_figure) {
        size_t before = p.pnf_chart.columns.size();
        p.pnf_chart = point_and_figure(p.bars, p.pnf);
        // Keep the reader's place as new columns arrive; follow the latest column if it was in view.
        // A fresh chart (pnf_fit) is sized by the renderer, which knows the plot's shape.
        auto n = p.pnf_chart.columns.size();
        if (!p.pnf_fit && p.pnf_view.first + p.pnf_view.count * View::latest_position >= double(before) - 1.5)
            p.pnf_view.first = double(n) - .5 - p.pnf_view.count * View::latest_position;
    } else
        p.pnf_chart = {};
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
    if (p.eps)
        p.results.push_back(eps_series(company(p.symbol).data, p.bars, p.eps_ttm));
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
    return needed;
}
std::vector<std::string> State::visible_symbols() const {
    std::vector<std::string> visible;
    std::set<std::string> seen;
    auto append = [&](const std::string &symbol) {
        if (seen.insert(symbol).second)
            visible.push_back(symbol);
    };
    // Expressions are listed with their legs, whose quotes they are computed from.
    auto add = [&](const std::string &symbol) {
        append(symbol);
        if (is_expression(symbol))
            try {
                for (auto &leg : parse_expression(symbol).symbols)
                    append(leg);
            } catch (...) {
            }
    };
    for (auto &p : panels)
        add(p->symbol);
    for (auto &w : watchlists)
        if (w->open)
            for (auto &symbol : w->measured ? w->shown : lists[size_t(w->list)].symbols)
                add(symbol);
    return visible;
}
bool State::tick() {
    auto before = visual_revision;
    network.poll();
    if (options_visible() && (options.key != options.symbol + "|" + options.expiry ||
                              (!offline && !options.loading && options.next <= now())))
        refresh_options();
    if (screener.open && (screener.key != scanner_request(screener.query).dump() ||
                          (!offline && !screener.loading && screener.next <= now())))
        refresh_screener();
    for (auto [symbol, tf] : needed_series()) {
        auto &e = ensure(symbol, tf);
        if (e.next <= now())
            fetch(symbol, tf);
    }
    for (auto &symbol : needed_companies())
        company(symbol);
    refresh_live_lists();
    for (auto &p : panels)
        update(*p);
    auto visible = visible_symbols();
    derive_quotes(visible);
    if (!offline && quote_tick <= now()) {
        quote_tick = now() + 1;
        // Longest-waiting symbol first, so lists longer than the refresh cycle do not starve.
        const std::string *due = nullptr;
        for (auto &symbol : visible)
            if (!is_expression(symbol) && !quote_loading.count(symbol) && quote_next[symbol] <= now() &&
                (!due || quote_next[symbol] < quote_next[*due]))
                due = &symbol;
        if (due)
            fetch_quote(*due);
    }
    for (auto &symbol : visible) {
        auto q = quotes.find(symbol);
        bool stale = q == quotes.end() || now() - q->second.fetched >= 600 || quote_errors.count(symbol);
        int display = (stale ? 1 : 0) | (q != quotes.end() && fresh_extended(q->second, now()) ? 2 : 0);
        auto [it, inserted] = quote_display_state.try_emplace(symbol, display);
        if (inserted || it->second != display) {
            it->second = display;
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
        for (auto &symbol : needed_companies()) {
            auto it = companies.find(symbol);
            if (it == companies.end())
                next = std::min(next, clock);
            else if (!it->second.loading)
                next = std::min(next, it->second.next);
        }
        if (options_visible() && !options.loading && !options.dates_loading)
            next = std::min(next, options.next);
        if (screener.open && !screener.loading)
            next = std::min(next, screener.next);
        for (auto &w : watchlists) {
            auto &l = lists[size_t(w->list)];
            if (w->open && !l.source.empty() && !l.loading)
                next = std::min(next, l.next);
        }
        for (auto &[symbol, tf] : needed_series()) {
            auto it = series.find(key(symbol, tf));
            if (it == series.end())
                next = std::min(next, clock);
            else if (!it->second.loading)
                next = std::min(next, it->second.next);
        }
        for (auto &symbol : visible_symbols()) {
            if (!is_expression(symbol) && !quote_loading.count(symbol)) {
                auto it = quote_next.find(symbol);
                next = std::min(next, std::max(quote_tick, it == quote_next.end() ? clock : it->second));
            }
        }
    }
    for (auto &symbol : visible_symbols()) {
        auto it = quotes.find(symbol);
        if (it != quotes.end()) {
            if (!quote_errors.count(symbol) && it->second.fetched + 600 > clock)
                next = std::min(next, it->second.fetched + 600);
            if (fresh_extended(it->second, clock)) {
                auto &e = *it->second.extended;
                next = std::min({next, e.fetched + 600, e.asof + 1800});
            }
        }
    }
    return next;
}
static Json encode_panel(const Panel *p) {
    Json inds = Json::array();
    for (auto &i : p->indicators)
        inds.push_back(encode_indicator(i));
    return {{"id", p->id},
            {"symbol", p->symbol},
            {"timeframe", p->tf},
            {"view", p->pending.is_null() ? encode_view(p->view, p->bars) : p->pending},
            {"view_julia", p->pending_julia},
            {"candles", p->candles},
            {"chart_style", !p->candles ? "line"
                            : p->ohlc   ? "bars"
                                        : "candles"},
            {"volume", p->volume},
            {"earnings", p->earnings},
            {"eps", p->eps},
            {"eps_ttm", p->eps_ttm},
            {"logarithmic", p->logarithmic},
            {"point_figure", p->point_figure},
            {"pnf", encode_pnf(p->pnf)},
            {"show_drawings", p->show_drawings},
            {"indicators", inds},
            {"option_ladder", p->option_ladder},
            {"ladder_volume", p->ladder_volume},
            {"option_marker", p->option_marker ? Json{{"expiry", p->option_marker->expiry},
                                                      {"strike", p->option_marker->strike},
                                                      {"puts", p->option_marker->puts}}
                                               : Json{}}};
}
Json State::document() const {
    Json charts = Json::array();
    for (auto &p : panels)
        charts.push_back(encode_panel(p.get()));
    Json ls = Json::array();
    for (auto &l : lists) {
        Json row = {{"name", l.name}, {"symbols", l.symbols}};
        if (!l.source.empty()) {
            row["source"] = l.source;
            row["asof"] = l.asof;
        }
        ls.push_back(std::move(row));
    }
    Json windows = Json::array();
    for (auto &w : watchlists)
        if (w->open)
            windows.push_back({{"id", w->id}, {"list", w->list}});
    return {{"version", 1},
            {"watchlist_catalog", watchlist_catalog},
            {"watchlist_windows", windows},
            {"next_watchlist_id", next_watchlist_id},
            {"format", "chartroom-cpp"},
            {"drawings", drawings.document()},
            {"charts", charts},
            {"recently_closed", closed_charts},
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
            {"drawing_tools_open", drawing_tools_open},
            {"fundamentals",
             {{"open", fundamentals.open}, {"symbol", fundamentals.symbol}, {"follow", fundamentals.follow}}},
            {"options",
             {{"open", options.open},
              {"symbol", options.symbol},
              {"expiry", options.expiry},
              {"puts", options.puts},
              {"target_chart", options.target_chart}}},
            {"screener",
             {{"open", screener.open},
              {"sort", screener.query.sort},
              {"offset", screener.query.offset},
              {"min_price", screener.query.min_price},
              {"min_cap", screener.query.min_cap},
              {"min_volume", screener.query.min_volume},
              {"sector", screener.query.sector},
              {"search", screener.search}}}};
}
static std::unique_ptr<Panel> decode_panel(const Json &c, bool julia = false) {
    auto p = std::make_unique<Panel>();
    p->id = c.at("id");
    if (p->id < 1)
        throw std::runtime_error("Invalid chart IDs");
    p->symbol = julia ? resolve_symbol(c.at("symbol")) : normalize_symbol(c.at("symbol"));
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
    p->point_figure = c.value("point_figure", false);
    p->pnf = decode_pnf(c.value("pnf", Json::object()));
    p->volume = c.value("volume", true);
    p->earnings = c.value("earnings", true);
    p->eps = c.value("eps", false);
    p->eps_ttm = c.value("eps_ttm", false);
    p->show_drawings = c.value("show_drawings", true);
    for (auto &i : c.value("indicators", Json::array()))
        p->indicators.push_back(decode_indicator(i));
    if (c.value("sma", false))
        p->indicators.push_back(indicator("SMA"));
    p->option_ladder = c.value("option_ladder", false);
    p->ladder_volume = c.value("ladder_volume", false);
    if (c.contains("option_marker") && c["option_marker"].is_object()) {
        const auto &m = c["option_marker"];
        OptionMarker marker{m.at("expiry"), m.at("strike"), m.value("puts", false)};
        if (!valid_expiry(marker.expiry) || !std::isfinite(marker.strike) || marker.strike <= 0)
            throw std::runtime_error("Invalid option marker");
        p->option_marker = marker;
    }
    return p;
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
        auto p = decode_panel(c, julia);
        if (!ids.insert(p->id).second)
            throw std::runtime_error("Duplicate chart IDs");
        next_id = std::max(next_id, p->id + 1);
        loaded.push_back(std::move(p));
    }
    if (!julia) {
        closed_charts.clear();
        for (auto &c : j.value("recently_closed", Json::array())) {
            auto p = decode_panel(c);
            if (!ids.insert(p->id).second)
                throw std::runtime_error("Duplicate closed chart ID");
            next_id = std::max(next_id, p->id + 1);
            closed_charts.push_back(c);
            if (closed_charts.size() > 20)
                closed_charts.erase(closed_charts.begin());
        }
    }
    if (!julia && j.contains("lists")) {
        std::vector<List> ls;
        for (auto &l : j["lists"]) {
            List row{l.at("name"), {}};
            if (row.name.empty())
                throw std::runtime_error("Invalid list name");
            for (auto &s : l.at("symbols"))
                row.symbols.push_back(normalize_symbol(s));
            row.source = l.value("source", std::string{});
            if (live_list_url(row.source).empty())
                row.source.clear();
            else
                row.asof = l.value("asof", std::string{});
            ls.push_back(std::move(row));
        }
        if (!ls.empty())
            lists = std::move(ls);
        selected_list = std::clamp(j.value("selected_list", 0), 0, int(lists.size() - 1));
    }
    if (!julia) {
        watchlist_catalog = j.value("watchlist_catalog", 0);
        watchlists.clear();
        std::set<int> window_ids;
        for (auto &item : j.value("watchlist_windows", Json::array())) {
            auto w = std::make_unique<WatchlistWindow>();
            w->id = item.at("id");
            if (w->id < 1 || w->id > 1000000 || !window_ids.insert(w->id).second)
                throw std::runtime_error("Invalid watchlist window ID");
            w->list = std::clamp(item.value("list", 0), 0, int(lists.size()) - 1);
            next_watchlist_id = std::max(next_watchlist_id, w->id + 1);
            watchlists.push_back(std::move(w));
        }
        next_watchlist_id = std::max(next_watchlist_id, j.value("next_watchlist_id", 1));
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
        auto o = j.value("options", Json::object());
        options.open = o.value("open", false);
        options.symbol = normalize_symbol(o.value("symbol", std::string("SPY")));
        options.expiry = o.value("expiry", std::string{});
        options.puts = o.value("puts", false);
        options.target_chart = o.value("target_chart", 0);
        auto fund = j.value("fundamentals", Json::object());
        fundamentals.open = fund.value("open", false);
        fundamentals.follow = fund.value("follow", true);
        fundamentals.symbol = normalize_symbol(fund.value("symbol", std::string("AAPL")));
        auto sc = j.value("screener", Json::object());
        screener.open = sc.value("open", false);
        screener.query.sort = std::clamp(sc.value("sort", 0), 0, 3);
        screener.query.offset = std::clamp(sc.value("offset", 0), 0, 100000);
        screener.query.min_price = std::max(0., sc.value("min_price", 0.));
        screener.query.min_cap = std::max(0., sc.value("min_cap", 0.));
        screener.query.min_volume = std::max(0., sc.value("min_volume", 0.));
        screener.query.sector = sc.value("sector", std::string{});
        screener.search = sc.value("search", std::string{});
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
                l.symbols.push_back(resolve_symbol(s));
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
bool State::reopen_chart(size_t index) {
    if (index >= closed_charts.size())
        return false;
    if (panels.size() >= 64) {
        notice = "Close a chart before adding more than 64.";
        return false;
    }
    auto p = decode_panel(closed_charts[index]);
    active = focus_chart = p->id;
    panels.push_back(std::move(p));
    closed_charts.erase(closed_charts.begin() + index);
    relayout = true;
    request_save();
    return true;
}
bool State::options_visible() const {
    auto p = option_target();
    return options.open || (p && p->option_ladder);
}
Panel *State::option_target() const {
    for (auto &p : panels)
        if (p->open && p->id == options.target_chart && p->symbol == options.symbol)
            return p.get();
    for (auto &p : panels)
        if (p->open && p->id == active && p->symbol == options.symbol)
            return p.get();
    for (auto &p : panels)
        if (p->open && p->symbol == options.symbol)
            return p.get();
    return nullptr;
}
void State::remove_closed() {
    if (panels.size() == 1 || std::none_of(panels.begin(), panels.end(), [](auto &p) { return p->open; }))
        panels.front()->open = true;
    auto n = panels.size();
    for (auto &p : panels)
        if (!p->open) {
            closed_charts.push_back(encode_panel(p.get()));
            if (closed_charts.size() > 20)
                closed_charts.erase(closed_charts.begin());
        }
    panels.erase(std::remove_if(panels.begin(), panels.end(), [](auto &p) { return !p->open; }),
                 panels.end());
    if (n != panels.size()) {
        active = current().id;
        relayout = true;
    }
}
} // namespace cr
