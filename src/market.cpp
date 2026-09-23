#include "market.hpp"
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>
namespace cr {
static double num(const Json &j) {
    if (j.is_number()) {
        auto n = j.get<double>();
        return std::isfinite(n) ? n : missing;
    }
    if (!j.is_string())
        return missing;
    auto s = j.get<std::string>();
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '$' || c == ',' || c == '%'; }),
            s.end());
    try {
        size_t end;
        auto n = std::stod(s, &end);
        return end == s.size() && std::isfinite(n) ? n : missing;
    } catch (...) {
        return missing;
    }
}
static double field(const Json &j, const char *key) {
    return num(j.value(key, Json{}));
}
static Json nasdaq_data(const std::string &body) {
    auto j = Json::parse(body);
    if (!j.contains("data") || !j["data"].is_object())
        throw std::runtime_error("Nasdaq: no data for this symbol");
    return j["data"];
}
bool nasdaq_symbol(const std::string &s) {
    return !s.empty() && s.size() <= 10 &&
           s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-") == std::string::npos && !s.ends_with("-USD") &&
           !s.ends_with("USDT");
}
std::string nasdaq_url(const std::string &s, const std::string &endpoint) {
    static const std::set<std::string> etfs = {
        "SPY", "QQQ", "GLD", "RSP", "DIA", "USO", "UUP", "IWM", "VTI",  "TLT",  "FEZ",  "IEUR", "EWG",
        "EWU", "EWQ", "EWI", "VOO", "SLV", "XLF", "XLK", "XLE", "ARKK", "TQQQ", "SQQQ", "HYG",  "EEM"};
    return "https://api.nasdaq.com/api/quote/" + url_encode(s) + "/" + endpoint +
           "?assetclass=" + (etfs.count(s) ? "etf" : "stocks");
}
std::string data_source(const History &h) {
    return h.meta.value("chartroomSource", std::string("Yahoo"));
}
static Time et_offset(Time local) {
    using namespace std::chrono;
    auto day = floor<days>(sys_seconds{seconds{local}});
    auto y = year_month_day{day}.year();
    // US DST rule in effect since 2007. Older historical daily opens use the old rule.
    auto start =
        sys_days{year_month_weekday{y, int(y) >= 2007 ? March : April, Sunday[int(y) >= 2007 ? 2u : 1u]}};
    auto end = int(y) >= 2007 ? sys_days{year_month_weekday{y, November, Sunday[1]}}
                              : sys_days{year_month_weekday_last{y, October, Sunday[last]}};
    return local >= duration_cast<seconds>(start.time_since_epoch()).count() + 2 * 3600 &&
                   local < duration_cast<seconds>(end.time_since_epoch()).count() + 2 * 3600
               ? 4 * 3600
               : 5 * 3600;
}
Time eastern_time(const std::string &s) {
    std::tm t{};
    std::istringstream in(s);
    in >> std::get_time(&t, "%b %d, %Y %I:%M %p");
    if (in.fail() || !s.ends_with(" ET"))
        throw std::runtime_error("Nasdaq: missing trade timestamp");
    char iso[32];
    std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &t);
    auto local = parse_time(iso);
    return local + et_offset(local);
}
Quote nasdaq_quote(const std::string &body, const std::string &symbol) {
    auto d = nasdaq_data(body);
    if (d.value("symbol", "") != symbol)
        throw std::runtime_error("Nasdaq: mismatched symbol");
    auto p = d.at("primaryData");
    Quote q{field(p, "lastSalePrice"), field(p, "percentageChange"), eastern_time(p.at("lastTradeTimestamp")),
            now(), true};
    if (!(q.price > 0) || !std::isfinite(q.change))
        throw std::runtime_error("Nasdaq: incomplete quote");
    q.source = "Nasdaq";
    return q;
}
History nasdaq_history(const std::string &body, const std::string &symbol) {
    auto d = nasdaq_data(body);
    if (d.value("symbol", "") != symbol)
        throw std::runtime_error("Nasdaq: mismatched symbol");
    History h;
    h.symbol = symbol;
    h.interval = "1d";
    h.currency = "USD";
    h.fetched = now();
    h.meta["chartroomSource"] = "Nasdaq";
    h.meta["chartroomProviderNotice"] = "";
    h.meta["regularMarketTime"] = 0;
    std::map<Time, Bar> bars;
    for (auto &p : d.at("chart")) {
        auto z = p.value("z", Json::object());
        double timestamp = field(p, "x");
        if (!std::isfinite(timestamp) || timestamp <= 0) {
            ++h.skipped;
            continue;
        }
        Time local = bucket(Time(timestamp / 1000), "1d") + 9 * 3600 + 30 * 60;
        Bar b{local + et_offset(local), field(z, "open"),  field(z, "high"),
              field(z, "low"),          field(z, "close"), field(z, "volume")};
        if (!valid(b) || b.low <= 0) {
            ++h.skipped;
            continue;
        }
        bars[b.time] = b;
    }
    for (auto &[t, b] : bars)
        h.bars.push_back(b);
    if (h.bars.empty())
        throw std::runtime_error("Nasdaq: no daily OHLC candles");
    return h;
}
History append_session(History h, const History &tail) {
    if (h.symbol != tail.symbol || h.interval != "1d" || tail.interval != "1d")
        throw std::runtime_error("Incompatible session data");
    if (h.bars.empty())
        return tail;
    auto last_day = bucket(h.bars.back().time, "1d");
    std::map<Time, Bar> sessions;
    for (auto b : h.bars)
        sessions[bucket(b.time, "1d")] = b;
    bool appended = false;
    for (auto b : tail.bars)
        if (bucket(b.time, "1d") > last_day) {
            sessions[bucket(b.time, "1d")] = b;
            appended = true;
        }
    if (appended) {
        h.bars.clear();
        for (auto &[t, b] : sessions)
            h.bars.push_back(b);
        h.meta["chartroomSource"] = "Nasdaq + Yahoo latest session";
        if (tail.meta.contains("regularMarketTime"))
            h.meta["regularMarketTime"] = tail.meta["regularMarketTime"];
    }
    return h;
}
History stooq_history(const std::string &body, const std::string &symbol) {
    std::istringstream input(body);
    std::string line;
    std::getline(input, line);
    if (!line.starts_with("Date,Open,High,Low,Close,Volume"))
        throw std::runtime_error("Stooq: daily CSV unavailable");
    History h;
    h.symbol = symbol;
    h.interval = "1d";
    h.currency = "USD";
    h.fetched = now();
    h.meta["chartroomSource"] = "Stooq";
    std::map<Time, Bar> sorted;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream row(line);
        std::vector<std::string> v;
        std::string s;
        while (std::getline(row, s, ','))
            v.push_back(s);
        if (v.size() != 6)
            continue;
        auto local = parse_time(v[0] + "T09:30:00");
        Bar b{local + et_offset(local), num(v[1]), num(v[2]), num(v[3]), num(v[4]), num(v[5])};
        if (local > 0 && valid(b) && b.low > 0)
            sorted[b.time] = b;
    }
    for (auto &[t, b] : sorted)
        h.bars.push_back(b);
    if (h.bars.empty())
        throw std::runtime_error("Stooq: no daily candles");
    return h;
}
History binance_history(const std::string &body, const std::string &symbol, const std::string &interval) {
    auto rows = Json::parse(body);
    if (!rows.is_array())
        throw std::runtime_error("Binance: no candles");
    History h;
    h.symbol = symbol;
    h.interval = interval;
    h.currency = "USDT";
    h.exchange = "Binance";
    h.fetched = now();
    h.meta["chartroomSource"] = "Binance";
    for (auto &r : rows) {
        if (!r.is_array() || r.size() < 6 || !r[0].is_number_integer())
            continue;
        Bar b{r[0].get<Time>() / 1000, num(r[1]), num(r[2]), num(r[3]), num(r[4]), num(r[5])};
        if (!valid(b) || b.low <= 0 || (!h.bars.empty() && b.time <= h.bars.back().time))
            throw std::runtime_error("Binance: invalid candle");
        h.bars.push_back(b);
    }
    if (h.bars.empty())
        throw std::runtime_error("Binance: empty history");
    return h;
}
Quote binance_quote(const std::string &body) {
    auto d = Json::parse(body);
    Quote q{field(d, "lastPrice"), field(d, "priceChangePercent"), d.at("closeTime").get<Time>() / 1000,
            now(), true};
    if (!(q.price > 0) || !std::isfinite(q.change))
        throw std::runtime_error("Binance: incomplete quote");
    q.source = "Binance";
    q.rolling = true;
    return q;
}
static std::string expiry_group(const std::string &s) {
    std::tm t{};
    std::istringstream in(s);
    in >> std::get_time(&t, "%B %d, %Y");
    if (in.fail())
        return {};
    char b[16];
    std::strftime(b, sizeof(b), "%Y-%m-%d", &t);
    return b;
}
bool valid_expiry(const std::string &s) {
    return s.size() == 10 && s[4] == '-' && s[7] == '-' && parse_time(s + "T00:00:00") > 0 &&
           date(parse_time(s + "T00:00:00"), "%Y-%m-%d") == s;
}
OptionDates parse_option_dates(const std::string &body) {
    auto d = nasdaq_data(body);
    std::set<std::string> dates;
    for (auto &r : d.at("table").at("rows")) {
        auto group = r.value("expirygroup", std::string{});
        auto day = expiry_group(group);
        if (valid_expiry(day))
            dates.insert(day);
    }
    if (d.contains("filterlist") && d["filterlist"].contains("fromdate"))
        for (auto &f : d["filterlist"]["fromdate"].at("filter")) {
            auto range = f.value("value", std::string{});
            auto split = range.find('|');
            if (split != std::string::npos) {
                auto first = range.substr(0, split), last = range.substr(split + 1);
                if (valid_expiry(first))
                    dates.insert(first);
                if (valid_expiry(last))
                    dates.insert(last);
            }
        }
    if (dates.empty())
        throw std::runtime_error("No listed expiries for this symbol");
    return {{dates.begin(), dates.end()}, now(), false};
}
double atm_strike(const OptionChain &chain, const std::string &expiry) {
    double best = missing, distance = std::numeric_limits<double>::infinity();
    if (!std::isfinite(chain.underlying))
        return best;
    for (auto &r : chain.rows)
        if (r.expiry == expiry) {
            double diff = std::abs(r.strike - chain.underlying);
            if (diff < distance || (diff == distance && r.strike < best)) {
                best = r.strike;
                distance = diff;
            }
        }
    return best;
}
const char *moneyness(double strike, double underlying, double atm, bool puts) {
    if (!std::isfinite(underlying))
        return "--";
    if (strike == atm)
        return "ATM";
    return (puts ? strike > underlying : strike < underlying) ? "ITM" : "OTM";
}
OptionChain parse_options(const std::string &body) {
    auto d = nasdaq_data(body);
    OptionChain out;
    out.fetched = now();
    out.total = d.value("totalRecord", 0);
    out.last_trade = d.value("lastTrade", std::string{});
    static const std::regex price(R"(\$([0-9][0-9,.]*))");
    std::smatch price_match;
    if (std::regex_search(out.last_trade, price_match, price))
        out.underlying = num(price_match[1].str());
    std::string expiry;
    static const std::regex contract("-([0-9]{6})[cp][0-9]{8}$", std::regex::icase);
    for (auto &r : d.at("table").at("rows")) {
        if (r.contains("expirygroup") && r["expirygroup"].is_string() &&
            !r["expirygroup"].get<std::string>().empty())
            expiry = expiry_group(r["expirygroup"]);
        double strike = field(r, "strike");
        if (!std::isfinite(strike) || strike <= 0)
            continue;
        std::string row_expiry = expiry;
        auto url = r.value("drillDownURL", std::string{});
        std::smatch match;
        if (std::regex_search(url, match, contract)) {
            auto digits = match[1].str();
            row_expiry = "20" + digits.substr(0, 2) + "-" + digits.substr(2, 2) + "-" + digits.substr(4, 2);
        }
        if (row_expiry.empty())
            throw std::runtime_error("Nasdaq: option expiry missing");
        auto side = [&](const char *prefix) {
            auto get = [&](const char *name) { return field(r, (std::string(prefix) + name).c_str()); };
            return OptionSide{get("Last"),         get("Bid"),
                              get("Ask"),          get("Volume"),
                              get("Openinterest"), r.value(std::string(prefix) + "colour", false)};
        };
        out.rows.push_back({row_expiry, strike, side("c_"), side("p_")});
    }
    return out;
}
void merge_options(OptionChain &out, OptionChain page) {
    std::map<std::pair<std::string, double>, OptionRow> rows;
    for (auto &r : out.rows)
        rows[{r.expiry, r.strike}] = r;
    for (auto &r : page.rows)
        rows[{r.expiry, r.strike}] = r;
    out.rows.clear();
    for (auto &[k, r] : rows)
        out.rows.push_back(std::move(r));
    out.underlying = page.underlying;
    out.total = page.total;
    out.fetched = page.fetched;
    out.last_trade = std::move(page.last_trade);
}
Json scanner_request(const ScreenQuery &q) {
    const char *sorts[] = {"market_cap_basic", "change", "change", "volume"};
    int sort = std::clamp(q.sort, 0, 3);
    Json filters = Json::array(
        {{{"left", "type"}, {"operation", "equal"}, {"right", "stock"}},
         {{"left", "typespecs"}, {"operation", "has"}, {"right", {"common"}}},
         {{"left", "exchange"}, {"operation", "in_range"}, {"right", {"NASDAQ", "NYSE", "AMEX"}}}});
    for (auto [key, value] :
         {std::pair{"close", q.min_price}, {"market_cap_basic", q.min_cap * 1e9}, {"volume", q.min_volume}})
        if (std::isfinite(value) && value > 0)
            filters.push_back({{"left", key}, {"operation", "egreater"}, {"right", value}});
    if (!q.sector.empty())
        filters.push_back({{"left", "sector"}, {"operation", "equal"}, {"right", q.sector}});
    int offset = std::clamp(q.offset, 0, 100000);
    return {
        {"columns", {"description", "close", "change", "market_cap_basic", "sector", "volume", "exchange"}},
        {"filter", filters},
        {"sort", {{"sortBy", sorts[sort]}, {"sortOrder", sort == 2 ? "asc" : "desc"}}},
        {"range", {offset, offset + 100}}};
}
ScreenResult parse_screen(const std::string &body) {
    auto d = Json::parse(body);
    ScreenResult out;
    out.fetched = now();
    out.total = d.at("totalCount");
    for (auto &r : d.at("data")) {
        auto id = r.at("s").get<std::string>();
        auto split = id.find(':');
        if (split == std::string::npos)
            continue;
        auto v = r.at("d");
        if (v.size() != 7)
            throw std::runtime_error("Scanner: unexpected columns");
        auto str = [&](size_t i) { return v[i].is_string() ? v[i].get<std::string>() : std::string{}; };
        auto symbol = id.substr(split + 1);
        std::replace(symbol.begin(), symbol.end(), '.', '-');
        out.rows.push_back(
            {normalize_symbol(symbol), str(0), str(4), str(6), num(v[1]), num(v[2]), num(v[3]), num(v[5])});
    }
    return out;
}
} // namespace cr
