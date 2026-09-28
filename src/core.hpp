#pragma once
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace cr {
using Json = nlohmann::json;
using Time = int64_t;
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
struct Bar {
    Time time{};
    double open{}, high{}, low{}, close{}, volume{};
};
struct History {
    std::string symbol, interval, currency, exchange;
    std::vector<Bar> bars;
    Time fetched{};
    int skipped{};
    Json meta = Json::object();
};
struct View {
    static constexpr int default_count = 220;
    static constexpr double latest_position = .75;
    double first = 0, count = default_count;
    void fit(size_t n);
    void zoom(size_t n, double wheel, double anchor);
    void pan(double bars) {
        first += bars;
    }
    std::pair<int, int> visible(size_t n) const; // half-open, includes partial candles
};
struct PriceScale {
    double low, high;
    bool logarithmic = false;
    double fraction(double price) const;
    double price(double fraction) const;
};
struct ScrollMotion {
    double pan{}, zoom{};
};
struct ScrollGesture {
    // Keep one axis through a trackpad gesture, including its momentum tail.
    double last_event = -1;
    int axis = 0;
    bool shifted = false;
    ScrollMotion update(double horizontal, double vertical, bool shift, double seconds);
};
constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}
constexpr uint32_t blue = rgba(102, 171, 255), gold = rgba(240, 184, 92), up = rgba(51, 201, 166),
                   down = rgba(245, 97, 110);
inline constexpr std::array<uint32_t, 5> ema_colors = {
    rgba(68, 136, 255), rgba(76, 176, 79), rgba(255, 235, 59), rgba(255, 153, 0), rgba(255, 82, 82)};
inline constexpr std::array<uint32_t, 8> ma_palette = {blue,
                                                       gold,
                                                       up,
                                                       rgba(200, 128, 200),
                                                       rgba(255, 110, 110),
                                                       rgba(90, 210, 230),
                                                       rgba(180, 210, 90),
                                                       rgba(170, 160, 255)};
struct Indicator {
    std::string kind = "EMA";
    bool enabled = true, colored_bars = true, show_emas = true, background = true;
    bool exclude_current = false, midline = true, show_highs = true, show_lows = true;
    uint32_t low_color = rgba(0, 160, 210);
    uint32_t color = blue;
    std::array<uint32_t, 5> ma_colors = ema_colors;
    int period = 20, slow = 26, signal = 9, timeframe = 0;
    double deviation = 2;
};
struct Result {
    std::string name;
    bool pane = false, colored_bars = false, background = true, pivots = false;
    std::vector<std::vector<double>> lines;
    std::vector<uint32_t> colors;
    std::vector<double> histogram, scores;
};
struct ExtendedQuote {
    double price{};
    Time asof{}, fetched{};
    std::string session; // Pre or Post; distinct from the regular-session quote.
    std::string source = "Yahoo";
};
struct Quote {
    double price{}, change{};
    Time asof{}, fetched{};
    bool snapshot = false;
    std::string source = "Yahoo";
    bool rolling = false;
    std::optional<ExtendedQuote> extended = {};
};
bool fresh_extended(const Quote &, Time clock);
struct Grid {
    double step{}, major{};
    std::vector<double> levels;
};
inline const std::vector<std::string> core_symbols = {"SPY", "BTC-USD", "GLD", "^VIX", "QQQ", "RSP"};
inline const std::vector<std::string> big_names = {
    "AAPL", "MSFT", "NVDA", "AMZN", "GOOGL", "META", "TSLA", "AVGO", "BRK-B", "JPM", "V",
    "MA",   "WMT",  "COST", "NFLX", "AMD",   "ORCL", "PLTR", "XOM",  "LLY",   "UNH", "GS"};
inline const char *timeframes[] = {"1h", "4h", "1D", "1W"};
inline const char *intervals[] = {"1h", "1h", "1d", "1wk"};
inline const char *kinds[] = {"SMA",      "EMA", "BB",    "RSI", "MACD", "ATR", "RIBBON",
                              "DONCHIAN", "KC",  "STOCH", "ROC", "VWMA", "OBV", "PIVOTS"};
inline const char *names[] = {"Simple moving average",
                              "Exponential moving average",
                              "Bollinger Bands",
                              "Relative strength index",
                              "MACD",
                              "Average true range",
                              "MA ribbon colored bars",
                              "Donchian channel",
                              "Keltner channels",
                              "Stochastic oscillator",
                              "Rate of change",
                              "Volume-weighted moving average",
                              "On-balance volume",
                              "Swing highs / lows (pivot points)"};
Time now();
Time parse_time(const std::string &);
std::string date(Time, const char *format = "%Y-%m-%d %H:%M"); // UTC for storage and provider requests.
std::string local_date(Time, const char *format = "%Y-%m-%d %H:%M %Z");
Time local_wall(Time);    // Local civil fields encoded on a UTC-shaped calendar for grid arithmetic.
Time local_instant(Time); // Convert those civil fields back using the system/browser DST rules.
std::string normalize_symbol(std::string);
std::string display_symbol(const std::string &);
std::string url_encode(const std::string &);
std::string history_url(const std::string &, const std::string &, Time start, Time end);
Time duration(const std::string &);
Time bucket(Time, const std::string &);
bool valid(const Bar &);
History parse_history(const std::string &, const std::string &, const std::string &,
                      bool allow_empty = false);
History decode_history(const Json &);
Json encode_history(const History &);
History merge_history(const History &, const History &);
History normalize_crypto(History);
std::set<Time> recovery_targets(const History &, Time clock);
History recover_crypto(History, const History &, const std::set<Time> &, Time clock);
History recover_vix(History, const History &, const std::set<Time> &, Time clock);
std::optional<Quote> quote(const History &);
Quote parse_quote(const std::string &body, const std::string &symbol);
std::vector<Bar> aggregate(const std::vector<Bar> &, int hours);
std::vector<double> mean(const std::vector<double> &, int);
std::vector<double> ema(const std::vector<double> &, int, bool wilder = false);
Indicator indicator(const std::string &);
Indicator next_indicator(const std::string &, const std::vector<Indicator> &);
Json encode_indicator(const Indicator &);
Indicator decode_indicator(const Json &);
Result calculate(const Indicator &, const std::vector<Bar> &, int chart_tf = 2,
                 const History *chart = nullptr, const History *source = nullptr, Time clock = now());
std::pair<double, double> price_limits(const std::vector<Bar> &, const View &,
                                       const std::vector<Result> & = {}, bool logarithmic = false);
Grid price_grid(double low, double high, double pixels);
Grid log_price_grid(double low, double high, double pixels);
std::string price_label(double, double step);
uint32_t ribbon_color(double);
Json encode_view(const View &, const std::vector<Bar> &);
void restore_view(View &, const std::vector<Bar> &, const Json &, bool julia = false);
void preserve_view(View &, const std::vector<Bar> &old, const std::vector<Bar> &next);
} // namespace cr
