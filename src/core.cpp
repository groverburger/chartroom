#include "core.hpp"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace cr {
ScrollMotion ScrollGesture::update(double horizontal, double vertical, bool shift, double seconds) {
    if (horizontal == 0 && vertical == 0)
        return {};
    if (last_event < 0 || seconds - last_event > .20 || shift != shifted)
        axis = shift || std::abs(horizontal) > std::abs(vertical) ? 1 : 2;
    last_event = seconds;
    shifted = shift;
    if (shift)
        return {horizontal != 0 ? horizontal : vertical, 0};
    return axis == 1 ? ScrollMotion{horizontal, 0} : ScrollMotion{0, vertical};
}

Time now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
static std::tm utc(Time t) {
    std::tm out{};
    std::time_t value = t;
#ifdef _WIN32
    gmtime_s(&out, &value);
#else
    gmtime_r(&value, &out);
#endif
    return out;
}
Time parse_time(const std::string &s) {
    std::tm tm{};
    std::istringstream in(s);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail())
        return 0;
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}
std::string date(Time t, const char *format) {
    char b[80];
    auto tm = utc(t);
    std::strftime(b, sizeof(b), format, &tm);
    return b;
}
static std::tm local_tm(Time t) {
    std::tm out{};
    std::time_t value = t;
#ifdef _WIN32
    localtime_s(&out, &value);
#else
    localtime_r(&value, &out);
#endif
    return out;
}
std::string local_date(Time t, const char *format) {
    char b[128]{};
    auto tm = local_tm(t);
    std::strftime(b, sizeof(b), format, &tm);
    return b;
}
Time local_wall(Time t) {
    auto tm = local_tm(t);
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}
Time local_instant(Time wall) {
    auto tm = utc(wall);
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}
static std::string ticker(std::string s, bool aliases) {
    for (auto &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    if (s.size() > 32 ||
        s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.^=_-") != std::string::npos)
        throw std::runtime_error("Invalid ticker.");
    static const std::map<std::string, std::string> shorthands = {
        {"BTC", "BTC-USD"}, {"ETH", "ETH-USD"}, {"SOL", "SOL-USD"}, {"VIX", "^VIX"},
        {"ES", "ES=F"},     {"NQ", "NQ=F"},     {"CL", "CL=F"}};
    auto it = shorthands.find(s);
    return aliases && it != shorthands.end() ? it->second : s;
}
static bool numeric(const std::string &s) {
    return s.find_first_not_of("0123456789.") == std::string::npos && std::count(s.begin(), s.end(), '.') <= 1 &&
           s != ".";
}
// Splits on + * / ( ) and on a dash that starts a token; a dash inside a ticker stays (BRK-B).
static std::vector<Expression::Token> tokens(const std::string &s, bool aliases) {
    std::vector<Expression::Token> out;
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c)))
            ++i;
        else if (std::string("+-*/()").find(c) != std::string::npos) {
            out.push_back({c});
            ++i;
        } else {
            size_t end = s.find_first_of(" \t\r\n+*/()", i);
            auto word = s.substr(i, end == std::string::npos ? std::string::npos : end - i);
            i += word.size();
            if (numeric(word))
                out.push_back({0, word, std::stod(word)});
            else
                out.push_back({0, ticker(word, aliases)});
        }
    }
    return out;
}
static Expression parse_tokens(const std::vector<Expression::Token> &);
static std::string canonical(std::string s, bool aliases) {
    auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos)
        throw std::runtime_error("Enter a ticker such as SPY or BTC.");
    s = s.substr(a, b - a + 1);
    auto parts = tokens(s, aliases);
    if (parts.size() == 1 && !parts[0].op && parts[0].symbol.size() && !numeric(parts[0].symbol))
        return parts[0].symbol;
    parse_tokens(parts); // Grammar and ticker count, before joining could merge "SPY QQQ".
    std::string out;
    for (auto &t : parts)
        out += t.op == '-' ? std::string(" - ") : t.op ? std::string(1, t.op) : t.symbol;
    parse_expression(out);
    return out;
}
std::string normalize_symbol(std::string s) {
    return canonical(std::move(s), false);
}
std::string resolve_symbol(std::string s) {
    return canonical(std::move(s), true);
}
bool is_expression(const std::string &s) {
    return s.find_first_of("+*/() ") != std::string::npos;
}
Expression parse_expression(const std::string &s) {
    if (s.size() > 60)
        throw std::runtime_error("Expression is too long.");
    return parse_tokens(tokens(s, false));
}
static Expression parse_tokens(const std::vector<Expression::Token> &list) {
    Expression e;
    size_t i = 0;
    auto fail = [](const std::string &why) -> void { throw std::runtime_error("Invalid expression: " + why); };
    std::function<void()> sum, product, operand;
    operand = [&] {
        if (i >= list.size())
            fail("it ends with an operator.");
        auto &t = list[i++];
        if (t.op == '(') {
            sum();
            if (i >= list.size() || list[i].op != ')')
                fail("missing ).");
            ++i;
        } else if (t.op)
            fail(std::string("unexpected ") + t.op + ". Use spaces around minus: SPY - QQQ.");
        else {
            if (t.symbol.size() && !numeric(t.symbol) &&
                std::find(e.symbols.begin(), e.symbols.end(), t.symbol) == e.symbols.end())
                e.symbols.push_back(t.symbol);
            e.rpn.push_back(t);
        }
    };
    product = [&] {
        operand();
        while (i < list.size() && (list[i].op == '*' || list[i].op == '/')) {
            auto op = list[i++];
            operand();
            e.rpn.push_back(op);
        }
    };
    sum = [&] {
        product();
        while (i < list.size() && (list[i].op == '+' || list[i].op == '-')) {
            auto op = list[i++];
            product();
            e.rpn.push_back(op);
        }
    };
    sum();
    if (i < list.size())
        fail(list[i].op == ')' ? "unmatched )." : "missing operator between terms.");
    if (e.symbols.empty())
        fail("include at least one ticker.");
    if (e.symbols.size() > 8)
        fail("use at most 8 tickers.");
    return e;
}
double Expression::evaluate(const std::map<std::string, double> &values) const {
    std::vector<double> stack;
    for (auto &t : rpn) {
        if (!t.op) {
            if (t.symbol.empty() || numeric(t.symbol))
                stack.push_back(t.number);
            else {
                auto it = values.find(t.symbol);
                stack.push_back(it == values.end() ? missing : it->second);
            }
            continue;
        }
        double b = stack.back();
        stack.pop_back();
        double &a = stack.back();
        a = t.op == '+' ? a + b : t.op == '-' ? a - b : t.op == '*' ? a * b : b != 0 ? a / b : missing;
    }
    return stack.empty() ? missing : stack.back();
}
std::string display_symbol(const std::string &s) {
    if (is_expression(s)) {
        std::string out;
        for (auto &t : tokens(s, false))
            out += t.op == '-' ? std::string(" - ") : t.op ? std::string(1, t.op) : display_symbol(t.symbol);
        return out;
    }
    if (s == "^VIX")
        return "VIX";
    if (s == "BTC-USD" || s == "ETH-USD" || s == "SOL-USD")
        return s.substr(0, 3);
    if (s.size() > 2 && s.substr(s.size() - 2) == "=F")
        return s.substr(0, s.size() - 2);
    return s;
}
std::string url_encode(const std::string &s) {
    std::string out;
    char b[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
            out += char(c);
        else {
            std::snprintf(b, 4, "%%%02X", c);
            out += b;
        }
    }
    return out;
}
std::string history_url(const std::string &s, const std::string &interval, Time start, Time end) {
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + url_encode(normalize_symbol(s)) +
           "?interval=" + interval + "&period1=" + std::to_string(std::max<Time>(0, start)) +
           "&period2=" + std::to_string(end) + "&includePrePost=false";
}
Time duration(const std::string &i) {
    return i == "1m" ? 60 : i == "1h" ? 3600 : i == "1wk" ? 604800 : 86400;
}
static Time floor_div(Time x, Time y) {
    return x / y - (x % y < 0);
}
Time bucket(Time t, const std::string &i) {
    if (i == "1wk")
        return floor_div(t - 345600, 604800) * 604800 + 345600;
    return floor_div(t, duration(i)) * duration(i);
}
bool valid(const Bar &b) {
    return std::isfinite(b.open) && std::isfinite(b.high) && std::isfinite(b.low) && std::isfinite(b.close) &&
           b.low <= std::min(b.open, b.close) && std::max(b.open, b.close) <= b.high &&
           (std::isnan(b.volume) || (std::isfinite(b.volume) && b.volume >= 0));
}
// VIX is a volatility index: nonpositive OHLC values are missing-data sentinels.
// Keep this symbol-specific: futures (and some other indices) can trade below zero.
static bool invalid_vix_price(const Bar &b, const std::string &symbol) {
    return symbol == "^VIX" && (b.open <= 0 || b.high <= 0 || b.low <= 0 || b.close <= 0);
}
static double number(const Json &j) {
    return j.is_number() ? j.get<double>() : missing;
}
static std::set<Time> times(const Json &j, const char *key) {
    std::set<Time> out;
    if (j.contains(key) && j[key].is_array())
        for (auto &t : j[key])
            if (t.is_number())
                out.insert(t.get<Time>());
    return out;
}
static bool crypto(const History &h) {
    return h.meta.value("instrumentType", std::string{}) == "CRYPTOCURRENCY";
}
History normalize_crypto(History h) {
    if (!crypto(h))
        return h;
    std::vector<Bar> bars;
    auto bad = times(h.meta, "chartroomIncompleteTimes");
    for (auto b : h.bars) {
        auto t = bucket(b.time, h.interval);
        if (t != b.time && b.volume == 0 && b.open == b.high && b.high == b.low && b.low == b.close) {
            if (!bars.empty() && bars.back().time == t) {
                auto &old = bars.back();
                old.high = std::max(old.high, b.close);
                old.low = std::min(old.low, b.close);
                old.close = b.close;
            } else
                bad.insert(t);
        } else
            bars.push_back(b);
    }
    h.bars = std::move(bars);
    h.meta["chartroomIncompleteTimes"] = bad;
    return h;
}
History parse_history(const std::string &body, const std::string &symbol, const std::string &interval,
                      bool allow_empty) {
    auto root = Json::parse(body);
    auto &chart = root.at("chart");
    if (!chart.value("error", Json()).is_null())
        throw std::runtime_error("Yahoo: " +
                                 chart["error"].value("description", std::string("Data unavailable")));
    if (!chart.contains("result") || !chart["result"].is_array() || chart["result"].empty())
        throw std::runtime_error("No history for " + symbol);
    auto &r = chart["result"][0];
    History h;
    h.symbol = symbol;
    h.interval = interval;
    h.fetched = now();
    h.meta = r.value("meta", Json::object());
    if (h.meta.value("dataGranularity", interval) != interval)
        throw std::runtime_error("Unexpected Yahoo interval");
    h.currency = h.meta.value("currency", std::string{});
    h.exchange = h.meta.value("exchangeName", std::string{});
    auto ts = r.value("timestamp", Json::array());
    if (ts.is_null())
        ts = Json::array();
    auto q = r.at("indicators").at("quote").at(0);
    for (auto key : {"open", "high", "low", "close", "volume"})
        if (!q.contains(key) || !q[key].is_array() || q[key].size() != ts.size())
            throw std::runtime_error("Inconsistent Yahoo OHLC arrays");
    std::map<Time, Bar> bars;
    std::set<Time> incomplete, recovered, invalid_prices;
    for (size_t i = 0; i < ts.size(); ++i) {
        if (!ts[i].is_number()) {
            ++h.skipped;
            continue;
        }
        Time t = ts[i].get<Time>();
        Bar b{t,
              number(q["open"][i]),
              number(q["high"][i]),
              number(q["low"][i]),
              number(q["close"][i]),
              number(q["volume"][i])};
        if (invalid_vix_price(b, symbol)) {
            ++h.skipped;
            incomplete.insert(t);
            invalid_prices.insert(t);
            continue;
        }
        double qp = number(h.meta.value("regularMarketPrice", Json())),
               qt = number(h.meta.value("regularMarketTime", Json()));
        // A quote supplies a close, not a missing session's open or range.
        bool range_present = !(b.open == 0 && b.high == 0 && b.low == 0);
        if (i + 1 == ts.size() && range_present && std::isfinite(qp) && (symbol != "^VIX" || qp > 0) &&
            qt >= t && qt < t + duration(interval)) {
            if (!std::isfinite(b.close) && std::isfinite(b.open) && std::isfinite(b.high) &&
                std::isfinite(b.low) && std::isfinite(b.volume)) {
                b.close = qp;
                recovered.insert(t);
            }
            if (b.low <= b.open && b.open <= b.high && (b.close < b.low || b.close > b.high) &&
                std::abs(b.close - qp) <= .01 + std::abs(qp) * 1e-6) {
                b.high = std::max(b.high, b.close);
                b.low = std::min(b.low, b.close);
                recovered.insert(t);
            }
        }
        if (valid(b) && std::isfinite(b.volume))
            bars[t] = b;
        else {
            ++h.skipped;
            incomplete.insert(t);
        }
    }
    for (auto &[t, b] : bars)
        h.bars.push_back(b);
    if (h.bars.empty() && !allow_empty)
        throw std::runtime_error("No complete OHLC candles for " + symbol);
    h.meta["chartroomIncompleteTimes"] = incomplete;
    h.meta["chartroomRecoveredTimes"] = recovered;
    h.meta["chartroomInvalidPriceTimes"] = invalid_prices;
    return normalize_crypto(std::move(h));
}
Json encode_history(const History &h) {
    Json bars = Json::array();
    for (auto &b : h.bars)
        bars.push_back(
            {b.time, b.open, b.high, b.low, b.close, std::isfinite(b.volume) ? Json(b.volume) : Json()});
    return {{"version", 1},
            {"symbol", h.symbol},
            {"interval", h.interval},
            {"currency", h.currency},
            {"exchange", h.exchange},
            {"fetched_at", date(h.fetched, "%Y-%m-%dT%H:%M:%S")},
            {"skipped", h.skipped},
            {"meta", h.meta},
            {"bars", bars}};
}
History decode_history(const Json &j) {
    if (j.at("version") != 1)
        throw std::runtime_error("Unsupported cache");
    History h;
    h.symbol = normalize_symbol(j.at("symbol"));
    h.interval = j.at("interval");
    h.currency = j.value("currency", std::string{});
    h.exchange = j.value("exchange", std::string{});
    h.fetched = parse_time(j.at("fetched_at"));
    h.skipped = j.value("skipped", 0);
    h.meta = j.value("meta", Json::object());
    auto invalid_prices = times(h.meta, "chartroomInvalidPriceTimes");
    auto incomplete = times(h.meta, "chartroomIncompleteTimes");
    Time previous = std::numeric_limits<Time>::min();
    for (auto &row : j.at("bars")) {
        Bar b{row.at(0).get<Time>(), row.at(1), row.at(2), row.at(3), row.at(4), number(row.at(5))};
        if (b.time <= previous)
            throw std::runtime_error("Unsorted cached candles");
        previous = b.time;
        if (invalid_vix_price(b, h.symbol)) {
            invalid_prices.insert(b.time);
            incomplete.insert(b.time);
            ++h.skipped;
            continue;
        }
        if (!valid(b))
            throw std::runtime_error("Invalid cached candle");
        h.bars.push_back(b);
    }
    if (h.bars.empty())
        throw std::runtime_error("Empty cached history");
    if (!invalid_prices.empty()) {
        h.meta["chartroomInvalidPriceTimes"] = invalid_prices;
        h.meta["chartroomIncompleteTimes"] = incomplete;
    }
    return normalize_crypto(std::move(h));
}
static void collect_sessions(const Json &j, std::map<Time, Time> &out) {
    if (j.is_object() && j.contains("start") && j.contains("end")) {
        out[j["start"].get<Time>()] = j["end"].get<Time>();
    } else if (j.is_array() || j.is_object())
        for (auto &c : j)
            collect_sessions(c, out);
}
static std::map<Time, Time> sessions(const Json &meta) {
    std::map<Time, Time> out;
    collect_sessions(meta.value("tradingPeriods", Json::array()), out);
    return out;
}
History merge_history(const History &old, const History &tail) {
    if (old.symbol != tail.symbol || old.interval != tail.interval)
        throw std::runtime_error("Cannot merge different series");
    History out = old;
    out.fetched = tail.fetched;
    out.skipped = tail.skipped;
    out.meta.update(tail.meta);
    auto bad = times(tail.meta, "chartroomIncompleteTimes");
    std::map<Time, Bar> bars;
    for (auto b : old.bars) {
        bool keep = tail.bars.empty();
        if (!keep) {
            auto first = tail.bars.front().time, last = tail.bars.back().time;
            bool provisional = (old.interval == "1d" || old.interval == "1wk") && b.time > last &&
                               bucket(b.time, old.interval) == bucket(last, old.interval) &&
                               !bad.count(b.time);
            keep = b.time < first || (b.time > last && !provisional) || bad.count(b.time);
        }
        if (keep)
            bars[b.time] = b;
    }
    for (auto b : tail.bars)
        bars[b.time] = b;
    auto missing_times = times(old.meta, "chartroomIncompleteTimes");
    missing_times.insert(bad.begin(), bad.end());
    for (auto &[t, b] : bars)
        missing_times.erase(t);
    out.meta["chartroomIncompleteTimes"] = missing_times;
    for (auto key : {"chartroomMinuteRecoveredTimes", "chartroomHourlyRecoveredTimes"}) {
        auto repaired = times(old.meta, key);
        for (auto b : tail.bars)
            repaired.erase(b.time);
        auto fresh = times(tail.meta, key);
        repaired.insert(fresh.begin(), fresh.end());
        for (auto it = repaired.begin(); it != repaired.end();)
            if (!bars.count(*it))
                it = repaired.erase(it);
            else
                ++it;
        out.meta[key] = repaired;
    }
    auto calendar = sessions(old.meta);
    for (auto [a, b] : sessions(tail.meta))
        calendar[a] = b;
    if (!calendar.empty()) {
        Json days = Json::array();
        for (auto [a, b] : calendar)
            days.push_back({{"start", a}, {"end", b}});
        out.meta["tradingPeriods"] = days;
    }
    out.bars.clear();
    for (auto &[t, b] : bars)
        out.bars.push_back(b);
    return out;
}
std::set<Time> recovery_targets(const History &h, Time clock) {
    std::set<Time> out;
    if (h.symbol == "^VIX" && h.interval == "1d") {
        for (auto t : times(h.meta, "chartroomInvalidPriceTimes"))
            if (t >= clock - 6 * 86400 && t <= clock)
                out.insert(t);
        return out;
    }
    if (!crypto(h) || h.interval == "1m")
        return out;
    auto bad = times(h.meta, "chartroomIncompleteTimes");
    auto quote_bad = times(h.meta, "chartroomRecoveredTimes");
    bad.insert(quote_bad.begin(), quote_bad.end());
    for (auto t : bad)
        if (t >= clock - 6 * 86400 && t <= clock) {
            auto start = bucket(t, h.interval);
            if (start >= clock - 7 * 86400)
                out.insert(start);
        }
    if (!out.empty()) {
        if (h.interval == "1h") {
            auto copy = out;
            for (auto t : copy)
                if (t - 3600 >= clock - 7 * 86400)
                    out.insert(t - 3600);
        }
        out.insert(bucket(clock, h.interval));
    }
    return out;
}
History recover_crypto(History h, const History &minutes, const std::set<Time> &targets, Time clock) {
    if (h.symbol != minutes.symbol || minutes.interval != "1m")
        throw std::runtime_error("Mismatched minute history");
    std::map<Time, Bar> source, output;
    for (auto b : minutes.bars)
        source[b.time] = b;
    for (auto b : h.bars)
        output[b.time] = b;
    auto recovered = times(h.meta, "chartroomMinuteRecoveredTimes"),
         bad = times(h.meta, "chartroomIncompleteTimes"),
         quote_bad = times(h.meta, "chartroomRecoveredTimes");
    Time asof = clock;
    if (minutes.meta.contains("regularMarketTime") && minutes.meta["regularMarketTime"].is_number())
        asof = std::min(clock, minutes.meta["regularMarketTime"].get<Time>());
    int failed = 0;
    for (auto start : targets) {
        auto end = start + duration(h.interval), complete = std::min(end, bucket(asof, "1m"));
        bool ok = start <= asof;
        for (auto t = start; t < complete && ok; t += 60)
            ok = source.count(t);
        auto first = source.lower_bound(start), last = source.lower_bound(std::min(end, asof + 1));
        if (!ok || first == last || first == source.end() || first->first != start) {
            ++failed;
            continue;
        }
        Bar b = first->second;
        for (auto it = first; it != last; ++it) {
            b.high = std::max(b.high, it->second.high);
            b.low = std::min(b.low, it->second.low);
            b.close = it->second.close;
        }
        b.volume = missing;
        output[start] = b;
        recovered.insert(start);
        bad.erase(start);
        quote_bad.erase(start);
    }
    h.bars.clear();
    for (auto [t, b] : output)
        h.bars.push_back(b);
    h.meta["chartroomMinuteRecoveredTimes"] = recovered;
    h.meta["chartroomIncompleteTimes"] = bad;
    h.meta["chartroomRecoveredTimes"] = quote_bad;
    h.meta["chartroomRecoveryError"] =
        failed ? "Minute coverage incomplete; affected candles were not reconstructed." : "";
    for (auto key : {"regularMarketTime", "regularMarketPrice"})
        if (minutes.meta.contains(key))
            h.meta[key] = minutes.meta[key];
    return h;
}
History recover_vix(History h, const History &hours, const std::set<Time> &targets, Time clock) {
    if (h.symbol != "^VIX" || h.interval != "1d" || hours.symbol != h.symbol || hours.interval != "1h")
        throw std::runtime_error("Mismatched VIX recovery history");
    auto calendar = sessions(hours.meta);
    std::map<Time, Bar> source, output;
    for (auto b : hours.bars)
        if (valid(b) && !invalid_vix_price(b, h.symbol))
            source[b.time] = b;
    for (auto b : h.bars)
        output[b.time] = b;
    auto recovered = times(h.meta, "chartroomHourlyRecoveredTimes"),
         bad = times(h.meta, "chartroomIncompleteTimes"),
         invalid = times(h.meta, "chartroomInvalidPriceTimes"),
         quote_bad = times(h.meta, "chartroomRecoveredTimes");
    Time asof = clock;
    if (hours.meta.contains("regularMarketTime") && hours.meta["regularMarketTime"].is_number())
        asof = std::min(clock, hours.meta["regularMarketTime"].get<Time>());
    int failed = 0;
    for (auto start : targets) {
        auto session = calendar.find(start);
        bool ok = session != calendar.end() && session->second > start && session->second <= start + 86400 &&
                  asof >= start;
        std::optional<Bar> daily;
        // Require every expected hourly bucket, including the unfinished current hour.
        // The provider's session times handle DST and VIX's extended calculation hours.
        if (ok)
            for (Time t = start; t < session->second && t <= asof; t += 3600) {
                auto it = source.find(t);
                if (it == source.end()) {
                    ok = false;
                    break;
                }
                auto b = it->second;
                if (!daily)
                    daily = b;
                else {
                    daily->high = std::max(daily->high, b.high);
                    daily->low = std::min(daily->low, b.low);
                    daily->close = b.close;
                    daily->volume += b.volume;
                }
            }
        if (!ok || !daily) {
            ++failed;
            continue;
        }
        output[start] = *daily;
        recovered.insert(start);
        bad.erase(start);
        invalid.erase(start);
        quote_bad.erase(start);
    }
    h.bars.clear();
    for (auto [t, b] : output)
        h.bars.push_back(b);
    h.meta["chartroomHourlyRecoveredTimes"] = recovered;
    h.meta["chartroomIncompleteTimes"] = bad;
    h.meta["chartroomInvalidPriceTimes"] = invalid;
    h.meta["chartroomRecoveredTimes"] = quote_bad;
    h.meta["chartroomRecoveryError"] =
        failed ? "Hourly session coverage incomplete; invalid VIX candles were not reconstructed." : "";
    return h;
}
static std::optional<Quote> metadata_quote(const Json &meta, Time fetched) {
    // chartPreviousClose is the close BEFORE THE REQUESTED RANGE, not necessarily yesterday.
    double price = number(meta.value("regularMarketPrice", Json())),
           prior = number(meta.value("previousClose", Json())),
           time = number(meta.value("regularMarketTime", Json()));
    double change = number(meta.value("regularMarketChangePercent", Json()));
    if (!std::isfinite(price) || !std::isfinite(time) || time <= 0 || time > 1e12 ||
        (meta.value("symbol", std::string{}) == "^VIX" && price <= 0))
        return {};
    if (!std::isfinite(change)) {
        if (!std::isfinite(prior) || prior == 0 ||
            (meta.value("symbol", std::string{}) == "^VIX" && prior <= 0))
            return {};
        change = (price / prior - 1) * 100;
    }
    if (!std::isfinite(change))
        return {};
    return Quote{price, change, Time(time), fetched, true};
}
bool fresh_extended(const Quote &q, Time clock) {
    return q.extended && q.extended->asof > q.asof && clock - q.extended->fetched < 600 &&
           clock - q.extended->asof < 1800 && q.extended->asof <= clock + 60;
}
Quote parse_quote(const std::string &body, const std::string &symbol) {
    auto root = Json::parse(body);
    auto &chart = root.at("chart");
    if (!chart.value("error", Json()).is_null())
        throw std::runtime_error("Yahoo: " +
                                 chart["error"].value("description", std::string("Quote unavailable")));
    auto &meta = chart.at("result").at(0).at("meta");
    if (meta.value("symbol", std::string{}) != symbol)
        throw std::runtime_error("Mismatched quote symbol");
    if (auto q = metadata_quote(meta, now())) {
        const auto &result = chart.at("result").at(0);
        // Only the quote request includes extended minutes; chart history remains regular-only.
        // A timestamp and explicit provider session bounds are required for a live marker.
        try {
            const auto &periods = meta.at("currentTradingPeriod");
            const auto &ts = result.at("timestamp");
            const auto &closes = result.at("indicators").at("quote").at(0).at("close");
            if (meta.value("hasPrePostMarketData", false) && ts.is_array() && closes.is_array())
                for (size_t i = std::min(ts.size(), closes.size()); i-- > 0;) {
                    double price = number(closes[i]), timestamp = number(ts[i]);
                    if (!(price > 0) || !std::isfinite(timestamp) || timestamp <= q->asof ||
                        timestamp > q->fetched + 60)
                        continue;
                    for (auto session : {"pre", "post"}) {
                        const auto &period = periods.at(session);
                        if (timestamp >= period.at("start").get<Time>() &&
                            timestamp < period.at("end").get<Time>())
                            q->extended = ExtendedQuote{price, Time(timestamp), q->fetched,
                                                        std::string(session) == "pre" ? "Pre" : "Post"};
                    }
                    if (q->extended)
                        break;
                }
        } catch (...) {
            // Regular-session prices remain usable when optional extended data is absent.
        }
        return *q;
    }
    throw std::runtime_error("Current price or previous session close unavailable");
}
History combine_expression(const std::string &symbol, const std::vector<const History *> &legs) {
    auto e = parse_expression(symbol);
    History out;
    if (legs.empty() || legs.size() != e.symbols.size())
        throw std::runtime_error("Expression legs do not match");
    auto &first = *legs.front();
    out.symbol = symbol;
    out.interval = first.interval;
    out.fetched = first.fetched;
    out.currency = first.currency;
    // Keep the first leg's session calendar; drop its provider and recovery annotations.
    for (auto it = first.meta.begin(); it != first.meta.end(); ++it)
        if (!it.key().starts_with("chartroom"))
            out.meta[it.key()] = it.value();
    std::set<std::string> sources;
    for (auto *leg : legs) {
        out.fetched = std::min(out.fetched, leg->fetched);
        if (leg->currency != out.currency)
            out.currency.clear();
        sources.insert(leg->meta.value("chartroomSource", std::string("Yahoo")));
    }
    std::string source;
    for (auto &s : sources)
        source += (source.empty() ? "" : " + ") + s;
    out.meta["chartroomSource"] = source;
    // Daily bars open at different UTC times by venue (stocks 13:30, crypto 00:00), so match on the bar's
    // UTC day; weekly on the week bucket; intraday on the hour.
    auto slot = [&](Time t) {
        return out.interval == "1d" ? floor_div(t, 86400) : out.interval == "1wk" ? bucket(t, "1wk") : floor_div(t, 3600);
    };
    std::vector<std::map<Time, const Bar *>> index(legs.size());
    for (size_t j = 1; j < legs.size(); ++j)
        for (auto &b : legs[j]->bars)
            index[j][slot(b.time)] = &b;
    std::map<std::string, double> open, high, low, close;
    for (auto &b : first.bars) {
        Time key = slot(b.time);
        bool complete = true;
        for (size_t j = 0; j < legs.size() && complete; ++j) {
            const Bar *leg = &b;
            if (j) {
                auto it = index[j].find(key);
                if (it == index[j].end()) {
                    complete = false;
                    break;
                }
                leg = it->second;
            }
            open[e.symbols[j]] = leg->open;
            high[e.symbols[j]] = leg->high;
            low[e.symbols[j]] = leg->low;
            close[e.symbols[j]] = leg->close;
        }
        if (!complete)
            continue;
        // As in TradingView spreads: evaluate each price field, then widen high/low to contain the bar.
        double o = e.evaluate(open), h = e.evaluate(high), l = e.evaluate(low), c = e.evaluate(close);
        Bar bar{b.time, o, std::max({o, h, l, c}), std::min({o, h, l, c}), c, missing};
        if (valid(bar))
            out.bars.push_back(bar);
    }
    return out;
}
std::optional<Quote> quote(const History &h) {
    if (auto q = metadata_quote(h.meta, h.fetched)) {
        q->source = h.meta.value("chartroomSource", std::string("Yahoo"));
        return q;
    }
    if (h.interval != "1d" || h.bars.size() < 2)
        return {};
    auto &last = h.bars.back();
    auto &prior = h.bars[h.bars.size() - 2];
    for (auto t : times(h.meta, "chartroomIncompleteTimes"))
        if (t > prior.time)
            return {};
    if (prior.close == 0)
        return {};
    Time asof = last.time;
    double price = last.close;
    if (h.meta.contains("regularMarketTime") && h.meta["regularMarketTime"].is_number()) {
        Time t = h.meta["regularMarketTime"];
        double current = number(h.meta.value("regularMarketPrice", Json()));
        if (last.time <= t && t < last.time + 86400 && std::isfinite(current) &&
            (h.symbol != "^VIX" || current > 0)) {
            asof = t;
            price = current;
        }
    }
    return Quote{price, (price / prior.close - 1) * 100,
                 asof,  h.fetched,
                 false, h.meta.value("chartroomSource", std::string("Yahoo"))};
}
void View::fit(size_t n) {
    // Retain the usual amount of history and add room for future dates.
    count = double(std::min<size_t>(default_count, std::max<size_t>(1, n))) / latest_position;
    first = n ? double(n) - .5 - count * latest_position : 0;
}
void View::zoom(size_t n, double wheel, double anchor) {
    anchor = std::clamp(anchor, 0., 1.);
    double world = first + anchor * count;
    count = std::clamp(count * std::exp(-std::clamp(wheel, -10., 10.) * .16),
                       double(std::min<size_t>(15, std::max<size_t>(1, n))),
                       double(std::max<size_t>(1, n)) / latest_position);
    first = world - anchor * count;
}
std::pair<int, int> View::visible(size_t n) const {
    double a = std::clamp(std::floor(first), 0., double(n)),
           b = std::clamp(std::ceil(first + count), 0., double(n));
    return {int(a), int(std::max(a, b))};
}
std::vector<Bar> aggregate(const std::vector<Bar> &bars, int hours) {
    std::vector<Bar> out;
    for (auto b : bars) {
        auto t = floor_div(b.time, Time(hours) * 3600) * hours * 3600;
        if (out.empty() || out.back().time != t) {
            b.time = t;
            out.push_back(b);
        } else {
            auto &a = out.back();
            a.high = std::max(a.high, b.high);
            a.low = std::min(a.low, b.low);
            a.close = b.close;
            a.volume += b.volume;
        }
    }
    return out;
}
std::vector<double> mean(const std::vector<double> &a, int p) {
    std::vector<double> out(a.size(), missing);
    double sum = 0;
    int finite = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::isfinite(a[i])) {
            sum += a[i];
            ++finite;
        }
        if (i >= size_t(p) && std::isfinite(a[i - p])) {
            sum -= a[i - p];
            --finite;
        }
        if (finite == p)
            out[i] = sum / p;
    }
    return out;
}
std::vector<double> ema(const std::vector<double> &a, int p, bool wilder) {
    std::vector<double> out(a.size(), missing);
    size_t seed = wilder ? size_t(p) : 1;
    if (a.size() < seed)
        return out;
    double sum = 0;
    for (size_t i = 0; i < seed; ++i)
        sum += a[i];
    out[seed - 1] = sum / seed;
    double alpha = wilder ? 1. / p : 2. / (p + 1);
    for (size_t i = seed; i < a.size(); ++i)
        out[i] = alpha * a[i] + (1 - alpha) * out[i - 1];
    return out;
}
Indicator indicator(const std::string &k) {
    Indicator s;
    s.kind = k;
    s.color = k == "SMA" ? gold : blue;
    s.period = (k == "RSI" || k == "ATR" || k == "STOCH") ? 14 : (k == "MACD" || k == "ROC") ? 12 : 20;
    if (k == "PIVOTS") {
        s.period = 5;
        s.color = rgba(200, 128, 200);
    }
    if (k == "STOCH")
        s.slow = s.signal = 3;
    if (k == "KC")
        s.slow = 10;
    return s;
}
Indicator next_indicator(const std::string &kind, const std::vector<Indicator> &existing) {
    auto spec = indicator(kind);
    if (kind != "SMA" && kind != "EMA" && kind != "VWMA")
        return spec;
    size_t count = 0;
    uint32_t last = 0;
    for (const auto &old : existing)
        if (old.kind == "SMA" || old.kind == "EMA" || old.kind == "VWMA") {
            ++count;
            last = old.color;
        }
    size_t index = count % ma_palette.size();
    auto found = std::find(ma_palette.begin(), ma_palette.end(), last);
    if (found != ma_palette.end())
        index = (size_t(found - ma_palette.begin()) + 1) % ma_palette.size();
    if (ma_palette[index] == last)
        index = (index + 1) % ma_palette.size();
    spec.color = ma_palette[index];
    return spec;
}
static Json encode_color(uint32_t c) {
    return Json::array(
        {double(c & 255) / 255, double((c >> 8) & 255) / 255, double((c >> 16) & 255) / 255, 1.0});
}
static uint32_t decode_color(const Json &j) {
    if (!j.is_array() || (j.size() != 3 && j.size() != 4))
        throw std::runtime_error("Invalid indicator color");
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
        double v = j.at(i).get<double>();
        if (!std::isfinite(v))
            throw std::runtime_error("Invalid indicator color");
        rgb[i] = int(std::lround(std::clamp(v, 0., 1.) * 255));
    }
    return rgba(rgb[0], rgb[1], rgb[2]);
}
Json encode_indicator(const Indicator &s) {
    Json colors = Json::array();
    for (auto c : s.ma_colors)
        colors.push_back(encode_color(c));
    return {{"kind", s.kind},
            {"background", s.background},
            {"exclude_current", s.exclude_current},
            {"midline", s.midline},
            {"show_highs", s.show_highs},
            {"show_lows", s.show_lows},
            {"low_color", encode_color(s.low_color)},
            {"color", encode_color(s.color)},
            {"ma_colors", colors},
            {"enabled", s.enabled},
            {"period", s.period},
            {"slow", s.slow},
            {"signal", s.signal},
            {"deviation", s.deviation},
            {"colored_bars", s.colored_bars},
            {"show_emas", s.show_emas},
            {"timeframe", s.timeframe}};
}
Indicator decode_indicator(const Json &j) {
    auto s = indicator(j.at("kind"));
    bool known = false;
    for (auto k : kinds)
        known |= s.kind == k;
    if (!known)
        throw std::runtime_error("Unknown indicator");
    s.enabled = j.value("enabled", true);
    s.period = std::clamp(j.value("period", s.period), 1, 500);
    s.slow = std::clamp(j.value("slow", s.slow), 1, 500);
    s.signal = std::clamp(j.value("signal", s.signal), 1, 500);
    s.deviation = std::clamp(j.value("deviation", 2.), .1, 10.);
    s.colored_bars = j.value("colored_bars", true);
    s.show_emas = j.value("show_emas", true);
    s.background = j.value("background", true);
    s.exclude_current = j.value("exclude_current", false);
    s.midline = j.value("midline", true);
    s.show_highs = j.value("show_highs", true);
    s.show_lows = j.value("show_lows", true);
    if (j.contains("low_color"))
        s.low_color = decode_color(j["low_color"]);
    if (j.contains("color"))
        s.color = decode_color(j["color"]);
    if (j.contains("ma_colors")) {
        if (j["ma_colors"].size() != 5)
            throw std::runtime_error("Expected five ribbon colors");
        for (size_t i = 0; i < 5; ++i)
            s.ma_colors[i] = decode_color(j["ma_colors"][i]);
    }
    s.timeframe = std::clamp(j.value("timeframe", 0), 0, 4);
    return s;
}
static std::vector<Time> bar_ends(const std::vector<Bar> &bars, int tf, const std::map<Time, Time> &cal) {
    std::vector<Time> out;
    for (auto b : bars) {
        Time end = b.time + (tf == 0 ? 3600 : tf == 1 ? 14400 : tf == 2 ? 86400 : 604800);
        if (tf < 2) {
            auto it = cal.upper_bound(b.time);
            if (it != cal.begin()) {
                --it;
                if (b.time < it->second)
                    end = std::min(end, it->second);
            }
        } else if (tf == 2) {
            auto start = bucket(b.time, "1d");
            auto it = cal.lower_bound(start);
            end = it != cal.end() && bucket(it->first, "1d") == start ? it->second : start + 86400;
        } else {
            Time start = bucket(b.time, "1wk");
            end = start + 604800;
            auto it = cal.lower_bound(end);
            if (it != cal.begin()) {
                --it;
                if (it->first >= start)
                    end = it->second;
            }
        }
        out.push_back(end);
    }
    return out;
}
static std::vector<double> align(const std::vector<Bar> &bars, int tf, const History *chart,
                                 const std::vector<Bar> &src, int stf, const History &source,
                                 const std::vector<double> &values, Time clock) {
    auto cal = chart ? sessions(chart->meta) : std::map<Time, Time>{};
    if (cal.empty())
        cal = sessions(source.meta);
    auto ends = bar_ends(bars, tf, cal), se = bar_ends(src, stf, cal);
    if (stf == 3 && tf != 3)
        for (size_t j = 0; j < src.size(); ++j) {
            Time start = bucket(src[j].time, "1wk"), end = start + 604800;
            auto it = std::lower_bound(bars.begin(), bars.end(), end,
                                       [](const Bar &b, Time t) { return b.time < t; });
            if (end <= clock) {
                if (it != bars.begin() && std::prev(it)->time >= start)
                    se[j] = ends[size_t(std::prev(it) - bars.begin())];
            } else {
                se[j] = end;
                if (!crypto(source)) {
                    if (utc(clock).tm_wday == 0 || utc(clock).tm_wday == 6) {
                        if (it != bars.begin() && std::prev(it)->time >= start)
                            se[j] = ends[size_t(std::prev(it) - bars.begin())];
                    }
                    for (auto [a, b] : cal)
                        if (a >= start && a < end && utc(a).tm_wday == 5)
                            se[j] = b;
                }
            }
        }
    std::vector<double> out(bars.size(), missing);
    for (size_t i = 0; i < bars.size(); ++i) {
        Time cutoff = std::min(ends[i], clock);
        int j = int(std::upper_bound(se.begin(), se.end(), cutoff) - se.begin()) - 1;
        if (i + 1 == bars.size() && bars[i].time <= clock && clock < ends[i]) {
            auto it = std::upper_bound(src.begin(), src.end(), cutoff,
                                       [](Time t, const Bar &b) { return t < b.time; });
            j = std::max(j, int(it - src.begin()) - 1);
        }
        if (j >= 0)
            out[i] = values[size_t(j)];
    }
    return out;
}
// Monotonic queues keep rolling extrema linear in the number of bars.
static std::pair<std::vector<double>, std::vector<double>> extremes(const std::vector<Bar> &bars,
                                                                    int period) {
    std::vector<double> high(bars.size(), missing), low(bars.size(), missing);
    std::deque<size_t> highs, lows;
    for (size_t i = 0; i < bars.size(); ++i) {
        while (!highs.empty() && highs.front() + size_t(period) <= i)
            highs.pop_front();
        while (!lows.empty() && lows.front() + size_t(period) <= i)
            lows.pop_front();
        while (!highs.empty() && bars[highs.back()].high <= bars[i].high)
            highs.pop_back();
        while (!lows.empty() && bars[lows.back()].low >= bars[i].low)
            lows.pop_back();
        highs.push_back(i);
        lows.push_back(i);
        if (i + 1 >= size_t(period)) {
            high[i] = bars[highs.front()].high;
            low[i] = bars[lows.front()].low;
        }
    }
    return {high, low};
}
static std::vector<double> true_ranges(const std::vector<Bar> &bars) {
    std::vector<double> tr;
    for (size_t i = 0; i < bars.size(); ++i) {
        auto &b = bars[i];
        tr.push_back(i == 0 ? b.high - b.low
                            : std::max({b.high - b.low, std::abs(b.high - bars[i - 1].close),
                                        std::abs(b.low - bars[i - 1].close)}));
    }
    return tr;
}
// Mirrors open8585/ratings.py (ad_ema_conviction_balance_v1), including its pandas NaN handling.
std::vector<double> ad_balance(const std::vector<Bar> &bars, int half_life) {
    constexpr size_t volume_lookback = 10, atr_lookback = 20, warmup = 63;
    const size_t n = bars.size();
    std::vector<double> out(n, missing);
    double alpha = 1 - std::pow(.5, 1. / half_life);
    // ewm(adjust=False): a missing observation still decays the prior state's weight.
    struct Ema {
        double value = 0, weight = 1;
    } up, down;
    auto add = [&](Ema &e, double x) {
        e.weight *= 1 - alpha;
        if (std::isfinite(x)) {
            e.value = (e.weight * e.value + alpha * x) / (e.weight + alpha);
            e.weight = 1;
        }
    };
    std::vector<double> tr(n, missing);
    for (size_t i = 1; i < n; ++i) {
        auto &b = bars[i];
        double prev = bars[i - 1].close;
        tr[i] = std::max({b.high - b.low, std::abs(b.high - prev), std::abs(b.low - prev)});
    }
    for (size_t i = 1; i < n; ++i) {
        double prev = bars[i - 1].close, change = bars[i].close - prev;
        double ret = prev > 0 ? change / prev : missing;
        double prior = missing, atr = missing;
        if (i >= volume_lookback) {
            prior = 0;
            for (size_t j = i - volume_lookback; j < i; ++j)
                prior += bars[j].volume;
            prior /= volume_lookback;
        }
        if (i >= atr_lookback) {
            atr = 0;
            for (size_t j = i + 1 - atr_lookback; j <= i; ++j)
                atr += tr[j];
            atr /= atr_lookback;
        }
        double relative = prior > 0 ? bars[i].volume / prior : missing;
        double move = atr > 0 ? std::clamp(change / atr, -1., 1.) : missing;
        // NaN comparisons are false, so missing volume or returns never qualify.
        bool qualifies = std::abs(ret) >= .002 && relative > 1;
        double evidence = qualifies ? std::log2(1 + relative) * (1 + std::abs(move)) : 0;
        add(up, ret > 0 ? evidence : 0);
        add(down, ret < 0 ? evidence : 0);
        double total = up.value + down.value;
        if (i + 1 >= warmup)
            out[i] = total > 0 ? 100 * (up.value - down.value) / total : 0;
    }
    return out;
}
const char *ad_grade(double balance) {
    static constexpr double thresholds[] = {-30.684010209862702, -22.778246196649842, -15.058281620379942,
                                            -11.87880266107041,  -3.8535151919915176, 2.2851098463169595,
                                            5.111067544378887,   20.532192692683413,  24.094060332509027,
                                            37.042540790342954,  46.39769728076267,   59.53363136770642};
    static constexpr const char *grades[] = {"E",  "D-", "D",  "D+", "C-", "C", "C+",
                                             "B-", "B",  "B+", "A-", "A",  "A+"};
    if (!std::isfinite(balance))
        return "";
    return grades[std::upper_bound(std::begin(thresholds), std::end(thresholds), balance) -
                  std::begin(thresholds)];
}
Result calculate(const Indicator &s, const std::vector<Bar> &bars, int tf, const History *chart,
                 const History *source, Time clock) {
    Result r;
    r.name = s.kind == "RIBBON" ? "MA ribbon" : s.kind + " " + std::to_string(s.period);
    r.pane = s.kind == "RSI" || s.kind == "MACD" || s.kind == "ATR" || s.kind == "STOCH" || s.kind == "ROC" ||
             s.kind == "OBV" || s.kind == "AD";
    r.colored_bars = s.kind == "RIBBON" && s.colored_bars;
    r.background = s.background;
    std::vector<double> prices;
    for (auto b : bars)
        prices.push_back(b.close);
    auto p = s.period;
    if (s.kind == "SMA" || s.kind == "EMA") {
        r.lines.push_back(s.kind == "SMA" ? mean(prices, p) : ema(prices, p));
        r.colors.push_back(s.color);
    } else if (s.kind == "PIVOTS") {
        r.name = "Swing pivots " + std::to_string(p) + " + " + std::to_string(p);
        r.pivots = true;
        r.lines.assign(2, std::vector<double>(bars.size(), missing));
        r.colors = {s.color, s.low_color};
        auto calendar = chart ? sessions(chart->meta) : std::map<Time, Time>{};
        if (chart && chart->meta.contains("currentTradingPeriod"))
            collect_sessions(chart->meta["currentTradingPeriod"].value("regular", Json::object()), calendar);
        // Weekly session calendars can be partial (e.g. only Monday), so require the
        // full interval or the next weekly bar before confirming that week's close.
        auto ends = bar_ends(bars, tf, tf == 3 ? std::map<Time, Time>{} : calendar);
        Time observed = chart && chart->fetched > 0 ? std::min(clock, chart->fetched) : clock;
        for (size_t i = size_t(p); i + size_t(p) < bars.size(); ++i) {
            const size_t right = i + size_t(p);
            bool next_started = right + 1 < bars.size() && bars[right + 1].time <= observed;
            if (!next_started && ends[right] > observed)
                continue;
            bool high = s.show_highs, low = s.show_lows;
            for (size_t j = i - p; j <= right && (high || low); ++j) {
                if (j == i)
                    continue;
                // Strict extrema: equal highs/lows do not create duplicate plateau markers.
                high &= bars[i].high > bars[j].high;
                low &= bars[i].low < bars[j].low;
            }
            if (high)
                r.lines[0][i] = bars[i].high;
            if (low)
                r.lines[1][i] = bars[i].low;
        }
    } else if (s.kind == "DONCHIAN") {
        auto [high, low] = extremes(bars, p);
        if (s.exclude_current) {
            for (size_t i = bars.size(); i-- > 0;) {
                high[i] = i ? high[i - 1] : missing;
                low[i] = i ? low[i - 1] : missing;
            }
        }
        r.lines = {high, low};
        r.colors = {s.color, s.color};
        if (s.midline) {
            auto mid = high;
            for (size_t i = 0; i < mid.size(); ++i)
                mid[i] = (high[i] + low[i]) / 2;
            r.lines.push_back(std::move(mid));
            r.colors.push_back(gold);
        }
        if (s.exclude_current)
            r.name += " / prior bars";
    } else if (s.kind == "KC") {
        auto mid = ema(prices, p), high = mid, low = mid;
        auto atr = ema(true_ranges(bars), s.slow, true);
        for (size_t i = 0; i < bars.size(); ++i) {
            high[i] += s.deviation * atr[i];
            low[i] -= s.deviation * atr[i];
        }
        r.lines = {mid, high, low};
        r.colors = {gold, s.color, s.color};
    } else if (s.kind == "STOCH") {
        auto [high, low] = extremes(bars, p);
        auto k = high;
        for (size_t i = size_t(p - 1); i < bars.size(); ++i)
            k[i] = high[i] == low[i] ? 50 : 100 * (prices[i] - low[i]) / (high[i] - low[i]);
        k = mean(k, s.slow);
        r.lines = {k, mean(k, s.signal)};
        r.colors = {s.color, gold};
    } else if (s.kind == "ROC") {
        std::vector<double> values(prices.size(), missing);
        for (size_t i = size_t(p); i < prices.size(); ++i)
            if (prices[i - p] != 0)
                values[i] = (prices[i] / prices[i - p] - 1) * 100;
        r.lines = {values};
        r.colors = {s.color};
        r.name += " (%)";
    } else if (s.kind == "VWMA") {
        std::vector<double> pv, volume;
        for (auto &b : bars) {
            bool valid = std::isfinite(b.volume) && b.volume >= 0;
            volume.push_back(valid ? b.volume : missing);
            pv.push_back(valid ? b.close * b.volume : missing);
        }
        auto weights = mean(volume, p), values = mean(pv, p);
        for (size_t i = 0; i < values.size(); ++i)
            values[i] = weights[i] > 0 ? values[i] / weights[i] : missing;
        r.lines = {values};
        r.colors = {s.color};
    } else if (s.kind == "OBV") {
        std::vector<double> values(bars.size(), missing);
        double total = 0;
        bool valid = true;
        for (size_t i = 0; i < bars.size(); ++i) {
            valid &= std::isfinite(bars[i].volume) && bars[i].volume >= 0;
            if (i && valid)
                total += prices[i] > prices[i - 1]   ? bars[i].volume
                         : prices[i] < prices[i - 1] ? -bars[i].volume
                                                     : 0;
            if (valid)
                values[i] = total;
        }
        r.lines = {values};
        r.colors = {s.color};
        r.name = valid ? "OBV" : "OBV / volume gaps unavailable";
    } else if (s.kind == "BB") {
        auto m = mean(prices, p), hi = m, lo = m;
        for (size_t i = size_t(p - 1); i < prices.size(); ++i) {
            double sum = 0;
            for (size_t j = i + 1 - p; j <= i; ++j)
                sum += (prices[j] - m[i]) * (prices[j] - m[i]);
            double d = s.deviation * std::sqrt(sum / p);
            hi[i] += d;
            lo[i] -= d;
        }
        r.lines = {m, hi, lo};
        r.colors = {gold, blue, blue};
    } else if (s.kind == "RSI") {
        std::vector<double> gains, losses;
        for (size_t i = 1; i < prices.size(); ++i) {
            double d = prices[i] - prices[i - 1];
            gains.push_back(std::max(d, 0.));
            losses.push_back(std::max(-d, 0.));
        }
        gains = ema(gains, p, true);
        losses = ema(losses, p, true);
        std::vector<double> values(prices.size(), missing);
        for (size_t i = 0; i < gains.size(); ++i)
            if (std::isfinite(gains[i]))
                values[i + 1] =
                    losses[i] == 0 ? (gains[i] == 0 ? 50 : 100) : 100 - 100 / (1 + gains[i] / losses[i]);
        r.lines = {values};
        r.colors = {blue};
    } else if (s.kind == "MACD") {
        auto fast = ema(prices, p), slow = ema(prices, s.slow);
        for (size_t i = 0; i < fast.size(); ++i)
            fast[i] -= slow[i];
        auto signal = ema(fast, s.signal);
        r.histogram = fast;
        for (size_t i = 0; i < fast.size(); ++i)
            r.histogram[i] -= signal[i];
        r.lines = {fast, signal};
        r.colors = {blue, gold};
        r.name = "MACD " + std::to_string(p) + "/" + std::to_string(s.slow) + "/" + std::to_string(s.signal);
    } else if (s.kind == "AD") {
        r.lines = {ad_balance(bars, p)};
        r.colors = {s.color};
        r.name = "A/D " + std::to_string(p);
        double last = r.lines[0].empty() ? missing : r.lines[0].back();
        // The letter boundaries were calibrated on daily bars with a 20-session half-life.
        if (tf == 2 && p == 20 && std::isfinite(last))
            r.name += std::string(" / ") + ad_grade(last);
    } else if (s.kind == "ATR") {
        r.lines = {ema(true_ranges(bars), p, true)};
        r.colors = {gold};
    } else if (s.kind == "RIBBON") {
        bool custom = s.timeframe > 0 && s.timeframe - 1 != tf;
        std::vector<Bar> sb =
            custom && source ? (s.timeframe == 2 ? aggregate(source->bars, 4) : source->bars) : bars;
        std::vector<double> sp;
        for (auto b : sb)
            sp.push_back(b.close);
        std::vector<std::vector<double>> es;
        for (int period : {20, 50, 100, 150, 200}) {
            auto e = ema(sp, period);
            if (custom) {
                if (source)
                    e = align(bars, tf, chart, sb, s.timeframe - 1, *source, e, clock);
                else
                    e.assign(bars.size(), missing);
            }
            es.push_back(std::move(e));
        }
        r.scores.assign(bars.size(), missing);
        for (size_t i = 0; i < bars.size(); ++i) {
            bool ok = true;
            double score = 0;
            for (int j = 0; j < 5; ++j)
                ok &= std::isfinite(es[j][i]);
            if (ok) {
                for (int j = 0; j < 4; ++j)
                    score += es[j][i] > es[j + 1][i] ? 1 : -1;
                r.scores[i] = score;
            }
        }
        if (s.show_emas) {
            r.lines = std::move(es);
            r.colors.assign(s.ma_colors.begin(), s.ma_colors.end());
        }
        if (custom)
            r.name += " / " + std::string(timeframes[s.timeframe - 1]);
    }
    return r;
}
uint32_t ribbon_color(double score) {
    static uint32_t c[] = {rgba(136, 0, 136), rgba(200, 128, 200), rgba(200, 115, 30), rgba(106, 213, 177),
                           rgba(0, 160, 210)};
    return c[std::clamp(int(std::lround((score + 4) / 2)), 0, 4)];
}
std::pair<double, double> price_limits(const std::vector<Bar> &bars, const View &v,
                                       const std::vector<Result> &results, bool logarithmic) {
    if (bars.empty())
        return {0, 1};
    auto [a, b] = v.visible(bars.size());
    if (a == b) {
        a = std::max(0, int(bars.size()) - View::default_count);
        b = int(bars.size());
    }
    double lo = bars[a].low, hi = bars[a].high;
    for (int i = a; i < b; ++i) {
        lo = std::min(lo, bars[i].low);
        hi = std::max(hi, bars[i].high);
    }
    for (auto &r : results)
        if (!r.pane)
            for (auto &line : r.lines)
                for (int i = a; i < b && i < int(line.size()); ++i)
                    if (std::isfinite(line[i])) {
                        lo = std::min(lo, line[i]);
                        hi = std::max(hi, line[i]);
                    }
    if (logarithmic && lo > 0) {
        double low = std::log(lo), high = std::log(hi);
        double pad = std::max((high - low) * .08, .0001);
        return {std::max(std::numeric_limits<double>::denorm_min(), std::exp(low - pad)),
                std::exp(std::min(high + pad, std::log(std::numeric_limits<double>::max())))};
    }
    double pad = std::max({(hi - lo) * .08, std::abs(hi) * .0001, .01});
    return {lo - pad, hi + pad};
}
double PriceScale::fraction(double value) const {
    if (logarithmic)
        return value > 0 ? (std::log(value) - std::log(low)) / (std::log(high) - std::log(low)) : missing;
    return (value - low) / (high - low);
}
double PriceScale::price(double fraction) const {
    return logarithmic ? std::exp(std::log(low) + fraction * (std::log(high) - std::log(low)))
                       : low + fraction * (high - low);
}
Grid log_price_grid(double low, double high, double pixels) {
    Grid out;
    if (!(low > 0 && high > low) || !std::isfinite(high) || !std::isfinite(pixels) || pixels <= 0)
        return out;
    PriceScale scale{low, high, true};
    double decades = std::log10(high) - std::log10(low);
    if (decades < 1) {
        out = price_grid(low, high, pixels);
        std::vector<double> levels;
        for (double v : out.levels)
            if (v > 0 &&
                (levels.empty() || (scale.fraction(v) - scale.fraction(levels.back())) * pixels >= 40))
                levels.push_back(v);
        out.levels = std::move(levels);
        return out;
    }
    int first = int(std::floor(std::log10(low))), last = int(std::ceil(std::log10(high)));
    int stride = std::max(1, int(std::ceil(decades / std::max(1., pixels / 65))));
    out.step = std::pow(10., std::max(-307, first - 1));
    out.major = std::pow(10., first);
    auto add = [&](double v) {
        if (!std::isfinite(v) || v < low || v > high)
            return;
        for (double prior : out.levels)
            if (std::abs(scale.fraction(v) - scale.fraction(prior)) * pixels < 40)
                return;
        out.levels.push_back(v);
    };
    for (int e = first; e <= last; ++e)
        if (e % stride == 0)
            add(std::pow(10., e));
    if (stride == 1)
        for (int e = first; e <= last; ++e)
            for (double m : {2., 5.})
                add(m * std::pow(10., e));
    std::sort(out.levels.begin(), out.levels.end());
    return out;
}
Grid price_grid(double lo, double hi, double pixels) {
    Grid g;
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi <= lo || pixels <= 0)
        return g;
    double target = (hi - lo) / std::clamp(pixels / 80., 1., 16.),
           mag = std::pow(10., std::floor(std::log10(target))), m = target / mag;
    g.step = (m <= 1 ? 1 : m <= 2 ? 2 : m <= 2.5 ? 2.5 : m <= 5 ? 5 : 10) * mag;
    g.major = std::pow(10., std::floor(std::log10(g.step)) + 1);
    double first = std::ceil(lo / g.step);
    for (int i = 0; i < 100; ++i) {
        double p = (first + i) * g.step;
        if (p > hi)
            break;
        g.levels.push_back(p);
    }
    return g;
}
std::string price_label(double value, double step) {
    double a = std::abs(value), unit = a >= 1e9 ? 1e9 : a >= 1e6 ? 1e6 : a >= 1e3 ? 1e3 : 1.;
    const char *suffix = a >= 1e9 ? "B" : a >= 1e6 ? "M" : a >= 1e3 ? "k" : "";
    int digits = std::max(0, int(std::ceil(-std::log10(step / unit))));
    double scaled = step / unit * std::pow(10., digits);
    if (std::abs(scaled - std::round(scaled)) > 1e-9)
        ++digits;
    char s[80];
    if (digits > 8)
        std::snprintf(s, sizeof(s), "%.8g", value);
    else
        std::snprintf(s, sizeof(s), "%.*f%s", digits, value / unit, suffix);
    return s;
}
Json encode_view(const View &v, const std::vector<Bar> &bars) {
    Time anchor =
        bars.empty() ? 0 : bars[size_t(std::clamp(std::floor(v.first), 0., double(bars.size() - 1)))].time;
    return {{"first", v.first},
            {"count", v.count},
            {"length", bars.size()},
            {"anchor", bars.empty() ? Json() : Json(anchor)},
            {"follow", !bars.empty() && std::abs(v.first + v.count * View::latest_position -
                                                 (double(bars.size()) - .5)) < .01}};
}
void restore_view(View &v, const std::vector<Bar> &bars, const Json &d, bool julia) {
    double first = d.at("first").get<double>() - (julia ? 1 : 0), count = d.at("count");
    if (!std::isfinite(first) || !std::isfinite(count) || count <= 0 || std::abs(first) > 1e12)
        throw std::runtime_error("Invalid saved view");
    double old = d.value("length", 0.);
    v.count = std::min(count, 1e8);
    if (d.value("follow", false))
        v.first = double(bars.size()) - .5 - v.count * View::latest_position;
    else if (first >= old && old > 0)
        v.first = double(bars.size()) + first - old;
    else if (first >= 0 && !d.value("anchor", Json()).is_null() && !bars.empty()) {
        Time anchor = d["anchor"].get<Time>();
        auto it = std::lower_bound(bars.begin(), bars.end(), anchor,
                                   [](const Bar &b, Time t) { return b.time < t; });
        v.first = double(it - bars.begin()) + first - std::floor(first);
    } else
        v.first = first;
}
void preserve_view(View &v, const std::vector<Bar> &old, const std::vector<Bar> &next) {
    if (old.empty())
        v.fit(next.size());
    else
        restore_view(v, next, encode_view(v, old));
}
double PnfChart::price(int r) const {
    // Log rows pass through 100, as in Columnist, so levels are stable across symbols and reloads.
    return logarithmic ? 100 * std::pow(1 + step, r) : r * step;
}
double PnfChart::row(double p) const {
    return logarithmic ? std::log(p / 100) / std::log1p(step) : p / step;
}
double pnf_traditional_box(double price) {
    // The classic Dorsey/Cohen scale, by price level.
    double a = std::abs(price);
    for (auto [limit, box] : {std::pair{0.25, 0.01}, {1., 0.0625}, {5., 0.25}, {20., 0.5}, {100., 1.},
                              {200., 2.}, {500., 4.}, {1000., 5.}, {25000., 50.}})
        if (a < limit)
            return box;
    return 500;
}
PnfChart point_and_figure(const std::vector<Bar> &bars, const PnfSettings &s) {
    PnfChart out;
    out.logarithmic = s.logarithmic;
    out.step = s.logarithmic ? s.percent / 100 : s.box > 0 ? s.box : 0;
    if (bars.empty())
        return out;
    if (!out.step)
        out.step = pnf_traditional_box(bars.back().close);
    if (!(out.step > 0) || !std::isfinite(out.step)) {
        out.error = "Choose a positive box size.";
        return out;
    }
    if (s.logarithmic)
        for (auto &b : bars)
            if (!(b.low > 0)) {
                out.error = "Logarithmic boxes need positive prices; use arithmetic boxes for this symbol.";
                return out;
            }
    int reversal = std::max(1, s.reversal);
    // A level counts as reached when the price touches it; the epsilon absorbs floating-point noise.
    auto floor_row = [&](double p) { return int(std::floor(out.row(p) + 1e-9)); };
    auto ceil_row = [&](double p) { return int(std::ceil(out.row(p) - 1e-9)); };
    {
        double low = bars[0].low, high = bars[0].high;
        for (auto &b : bars) {
            low = std::min(low, b.low);
            high = std::max(high, b.high);
        }
        if (out.row(high) - out.row(low) > 200000) {
            out.error = "Box size is too small for this price range.";
            return out;
        }
    }
    auto &cols = out.columns;
    int anchor = floor_row(bars[0].close);
    for (size_t i = 0; i < bars.size(); ++i) {
        double high = s.closes ? bars[i].close : bars[i].high, low = s.closes ? bars[i].close : bars[i].low;
        int hi = floor_row(high), lo = ceil_row(low);
        if (cols.empty()) {
            // The first column starts once price leaves the starting box; its first box is the start.
            bool rise = hi > anchor, fall = lo < anchor;
            if (!rise && !fall)
                continue;
            if (rise && (!fall || hi - anchor >= anchor - lo)) {
                PnfColumn c{true, anchor, hi};
                c.fills.push_back(0);
                for (int r = anchor + 1; r <= hi; ++r)
                    c.fills.push_back(int(i));
                cols.push_back(std::move(c));
            } else {
                PnfColumn c{false, lo, anchor};
                c.fills.push_back(0);
                for (int r = anchor - 1; r >= lo; --r)
                    c.fills.push_back(int(i));
                cols.push_back(std::move(c));
            }
            continue;
        }
        auto &c = cols.back();
        // Continuation takes precedence over reversal within a bar, as in the high/low method.
        if (c.up) {
            if (hi > c.high) {
                for (int r = c.high + 1; r <= hi; ++r)
                    c.fills.push_back(int(i));
                c.high = hi;
            } else if (lo <= c.high - reversal) {
                PnfColumn next{false, lo, c.high - 1};
                for (int r = c.high - 1; r >= lo; --r)
                    next.fills.push_back(int(i));
                cols.push_back(std::move(next));
            }
        } else {
            if (lo < c.low) {
                for (int r = c.low - 1; r >= lo; --r)
                    c.fills.push_back(int(i));
                c.low = lo;
            } else if (hi >= c.low + reversal) {
                PnfColumn next{true, c.low + 1, hi};
                for (int r = c.low + 1; r <= hi; ++r)
                    next.fills.push_back(int(i));
                cols.push_back(std::move(next));
            }
        }
    }
    // Double-top breakouts and double-bottom breakdowns versus the prior column of the same kind.
    for (size_t i = 2; i < cols.size(); ++i) {
        if (cols[i].up && cols[i].high > cols[i - 2].high)
            cols[i].signal = 1;
        if (!cols[i].up && cols[i].low < cols[i - 2].low)
            cols[i].signal = -1;
    }
    return out;
}
Json encode_pnf(const PnfSettings &s) {
    return {{"logarithmic", s.logarithmic},
            {"percent", s.percent},
            {"box", s.box},
            {"reversal", s.reversal},
            {"closes", s.closes}};
}
PnfSettings decode_pnf(const Json &j) {
    PnfSettings s;
    if (!j.is_object())
        return s;
    s.logarithmic = j.value("logarithmic", s.logarithmic);
    s.percent = std::clamp(j.value("percent", s.percent), .05, 50.);
    s.box = std::max(0., j.value("box", s.box));
    s.reversal = std::clamp(j.value("reversal", s.reversal), 1, 10);
    s.closes = j.value("closes", s.closes);
    return s;
}
} // namespace cr
