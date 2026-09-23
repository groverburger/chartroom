#pragma once
#include "core.hpp"

namespace cr {
// Public-provider schemas, kept separate from transport and UI.
bool nasdaq_symbol(const std::string &);
std::string nasdaq_url(const std::string &, const std::string &endpoint);
std::string data_source(const History &);
Time eastern_time(const std::string &); // Nasdaq's explicit ET timestamp, including DST.
Quote nasdaq_quote(const std::string &, const std::string &symbol);
History nasdaq_history(const std::string &, const std::string &symbol);
History stooq_history(const std::string &, const std::string &symbol);
History binance_history(const std::string &, const std::string &symbol, const std::string &interval);
Quote binance_quote(const std::string &);
History append_session(History, const History &); // Latest daily OHLC, one candle per session.

struct OptionSide {
    double last = missing, bid = missing, ask = missing, volume = missing, interest = missing;
    bool itm = false;
};
struct OptionRow {
    std::string expiry;
    double strike = 0;
    OptionSide call, put;
};
struct OptionChain {
    std::vector<OptionRow> rows;
    std::string last_trade;
    double underlying = missing;
    int total = 0;
    bool truncated = false;
    Time fetched = 0;
};
struct OptionDates {
    std::vector<std::string> dates;
    Time fetched = 0;
    bool complete = false;
};
OptionDates parse_option_dates(const std::string &);
bool valid_expiry(const std::string &);
double atm_strike(const OptionChain &, const std::string &expiry);
const char *moneyness(double strike, double underlying, double atm, bool puts);
OptionChain parse_options(const std::string &);
void merge_options(OptionChain &, OptionChain);

struct ScreenQuery {
    int sort = 0, offset = 0;
    double min_price = 0, min_cap = 0, min_volume = 0;
    std::string sector;
};
struct ScreenRow {
    std::string symbol, name, sector, exchange;
    double price = missing, change = missing, cap = missing, volume = missing;
};
struct ScreenResult {
    std::vector<ScreenRow> rows;
    int total = 0;
    Time fetched = 0;
};
Json scanner_request(const ScreenQuery &);
ScreenResult parse_screen(const std::string &);
} // namespace cr
