#include "market.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
    } while (0)
using namespace cr;
static std::string fixture(const std::string &dir, const char *name) {
    std::ifstream file(dir + "/" + name);
    return {std::istreambuf_iterator<char>(file), {}};
}
template <class F> static bool fails(F f) {
    try {
        f();
        return false;
    } catch (...) {
        return true;
    }
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        std::string dir = argv[1];
        // Ticker names: the exact symbol only, with sector for stocks and the instrument type for funds.
        auto apple = parse_profile(fixture(dir, "yahoo-search-aapl.json"), "AAPL");
        CHECK(apple.name == "Apple Inc." && apple.sector == "Technology" && apple.industry == "Consumer Electronics");
        auto fund = parse_profile(fixture(dir, "yahoo-search-xlk.json"), "XLK");
        CHECK(fund.type == "ETF" && fund.sector.empty() && fund.name.find("Technology") != std::string::npos);
        CHECK(fails([&] { parse_profile(fixture(dir, "yahoo-search-aapl.json"), "AAP"); }));
        CHECK(profile_url("^VIX").find("q=%5EVIX") != std::string::npos);
        // Live watchlists: dashed class shares, sorted S&P members, and the 85-85 list in published order.
        auto sp500 = parse_live_list("sp500", fixture(dir, "sp500-constituents.csv"));
        CHECK(sp500.symbols.size() == 503 && std::is_sorted(sp500.symbols.begin(), sp500.symbols.end()));
        for (auto symbol : {"BRK-B", "BF-B", "ES", "CL", "AAPL"})
            CHECK(std::count(sp500.symbols.begin(), sp500.symbols.end(), symbol) == 1);
        CHECK(fails([] { parse_live_list("sp500", "Symbol,Security\nAAPL,Apple\n"); }));
        auto open8585 = parse_live_list("open8585", fixture(dir, "open8585-list.json"));
        CHECK(open8585.symbols == std::vector<std::string>({"MPC", "PSX", "VLO"}));
        CHECK(open8585.asof == "2026-09-25");
        CHECK(fails([] { parse_live_list("open8585", "{}"); }));
        CHECK(live_list_url("open8585").ends_with("/api/list.json") && live_list_url("custom").empty());
        Json parts = {{"earnings", Json::parse(fixture(dir, "nasdaq-earnings.json"))},
                      {"profile", Json::parse(fixture(dir, "nasdaq-profile.json"))},
                      {"summary", Json::parse(fixture(dir, "nasdaq-summary.json"))}};
        auto fundamentals = parse_fundamentals(parts, "AAPL");
        CHECK(fundamentals.name == "Apple Inc.");
        CHECK(!fundamentals.facts.empty() && fundamentals.sector == "Technology");
        CHECK(fundamentals.earnings.size() == 4);
        CHECK(fundamentals.earnings.front().day == "2025-10-30");
        CHECK(fundamentals.earnings.back().day == "2026-07-30");
        CHECK(std::abs(trailing_eps(fundamentals.earnings, 3) - 8.61) < 1e-8);
        CHECK(!std::isfinite(trailing_eps(fundamentals.earnings, 2)));
        CHECK(fails([&] { parse_fundamentals(parts, "MSFT"); }));
        // No EPS backpainting to the fiscal quarter end or unknown report time.
        std::vector<Bar> eps_bars;
        for (auto day : {"2025-10-30", "2025-10-31", "2026-07-30", "2026-07-31"})
            eps_bars.push_back({parse_time(std::string(day) + "T13:30:00"), 1, 2, 1, 2, 100});
        auto eps = eps_series(fundamentals, eps_bars, false);
        CHECK(!std::isfinite(eps.lines[0][0]));
        CHECK(eps.lines[0][1] == 1.85 && eps.lines[0][2] == 2.01 && eps.lines[0][3] == 1.91);
        auto ttm = eps_series(fundamentals, eps_bars, true);
        CHECK(!std::isfinite(ttm.lines[0][2]) && std::abs(ttm.lines[0][3] - 8.61) < 1e-8);
        fundamentals.earnings[1].fiscal = "Sep 2025";
        CHECK(!std::isfinite(trailing_eps(fundamentals.earnings, 3)));
        parts["calendar"] = Json::parse(fixture(dir, "nasdaq-earnings-date.json"));
        parts["calendar"]["data"]["reportText"] =
            "Estimated 10/29/2099; consensus EPS forecast for the quarter is $1.98.";
        auto future = parse_fundamentals(parts, "AAPL");
        CHECK(future.earnings.size() == 5 && future.earnings.back().upcoming);
        CHECK(future.earnings.back().estimate == 1.98 && !std::isfinite(future.earnings.back().actual));
        parts["earnings"]["data"]["earningsSurpriseTable"]["rows"][0]["eps"] = -1.5;
        CHECK(parse_fundamentals(parts, "AAPL").earnings[3].actual == -1.5);
        auto q = nasdaq_quote(fixture(dir, "nasdaq-info.json"), "SPY");
        CHECK(q.source == "Nasdaq" && q.price == 768.43 && q.change == -.64);
        CHECK(date(q.asof) == "2026-09-23 18:10");
        CHECK(date(eastern_time("Jan 15, 2026 9:35 AM ET")) == "2026-01-15 14:35");
        CHECK(date(eastern_time("Mar 09, 2026 9:35 AM ET")) == "2026-03-09 13:35");
        CHECK(fails([] { eastern_time("Sep 23, 2026"); }));
        auto pre = nasdaq_quote(fixture(dir, "nasdaq-info-premarket.json"), "SPY");
        CHECK(pre.price == 767.18 && pre.change == -.08);
        CHECK(date(pre.asof) == "2026-09-24 20:00");
        CHECK(pre.extended && pre.extended->price == 770.0705 && pre.extended->session == "Pre");
        CHECK(date(pre.extended->asof) == "2026-09-25 09:11");
        CHECK(fresh_extended(pre, pre.extended->asof + 60));
        CHECK(!fresh_extended(pre, pre.extended->fetched + 601));
        auto after = Json::parse(fixture(dir, "nasdaq-info-premarket.json"));
        after["data"]["marketStatus"] = "After-Hours";
        after["data"]["primaryData"]["lastTradeTimestamp"] = "Sep 24, 2026 5:00 PM ET";
        auto post = nasdaq_quote(after.dump(), "SPY");
        CHECK(post.extended && post.extended->session == "Post" && post.change == -.08);
        after["data"]["primaryData"]["lastSalePrice"] = "N/A";
        CHECK(!nasdaq_quote(after.dump(), "SPY").extended);
        after["data"]["secondaryData"] = nullptr;
        CHECK(fails([&] { nasdaq_quote(after.dump(), "SPY"); }));
        auto h = nasdaq_history(fixture(dir, "nasdaq-chart.json"), "SPY");
        CHECK(h.bars.size() == 4 && h.bars.back().close == 773.38);
        CHECK(quote(h)->source == "Nasdaq");
        CHECK(date(h.bars.back().time) == "2026-09-22 13:30");
        CHECK(fails([&] { nasdaq_history(fixture(dir, "nasdaq-chart.json"), "AAPL"); }));
        auto line = Json::parse(fixture(dir, "nasdaq-chart.json"));
        for (auto &p : line["data"]["chart"])
            p["z"].erase("open");
        CHECK(fails([&] { nasdaq_history(line.dump(), "SPY"); }));
        auto tail = h;
        tail.bars = {h.bars.back(), {parse_time("2026-09-23T13:30:00"), 770, 775, 760, 768, 10000}};
        tail.bars[0].close = 774; // Do not rewrite the completed historical series.
        auto merged = append_session(h, tail);
        CHECK(merged.bars.size() == 5 && merged.bars[3].close == 773.38 && merged.bars[4].close == 768);
        CHECK(data_source(merged) == "Nasdaq + Yahoo latest session");
        auto chain = parse_options(fixture(dir, "nasdaq-options.json"));
        CHECK(chain.rows.size() == 5 && chain.rows[0].expiry == "2026-09-23");
        CHECK(chain.rows.back().expiry == "2026-10-16");
        CHECK(std::isnan(chain.rows[0].call.interest) && chain.rows[0].call.itm);
        CHECK(chain.underlying == 768.4);
        OptionChain sample;
        sample.underlying = 100;
        sample.rows = {{"2026-10-16", 90, {}, {}},
                       {"2026-10-16", 100, {}, {}},
                       {"2026-10-16", 110, {}, {}},
                       {"2027-01-15", 101, {}, {}}};
        CHECK(atm_strike(sample, "2026-10-16") == 100);
        CHECK(std::string(moneyness(100, 100, 100, false)) == "ATM");
        CHECK(std::string(moneyness(90, 100, 100, false)) == "ITM");
        CHECK(std::string(moneyness(110, 100, 100, false)) == "OTM");
        CHECK(std::string(moneyness(90, 100, 100, true)) == "OTM");
        CHECK(std::string(moneyness(110, 100, 100, true)) == "ITM");
        CHECK(std::string(moneyness(90, missing, missing, true)) == "--");
        sample.underlying = 105;
        CHECK(atm_strike(sample, "2026-10-16") == 100);
        CHECK(!valid_expiry("2026-02-30") && valid_expiry("2028-02-29"));
        auto dates = parse_option_dates(fixture(dir, "nasdaq-expiries.json"));
        CHECK(dates.dates.front() == "2026-09-23" && !dates.complete);
        CHECK(dates.dates.back() == "2029-01-19");
        auto page = Json::parse(fixture(dir, "nasdaq-options.json"));
        page["data"]["table"]["rows"] = Json::array({page["data"]["table"]["rows"].back()});
        auto page2 = parse_options(page.dump()); // Pagination can start mid-expiry without a header.
        CHECK(page2.rows[0].expiry == "2026-10-16");
        auto size = chain.rows.size();
        merge_options(chain, std::move(page2));
        CHECK(chain.rows.size() == size);
        auto scanner = parse_screen(fixture(dir, "scanner.json"));
        CHECK(scanner.total > 100 && scanner.rows[0].symbol == "NVDA" && scanner.rows[0].cap > 1e12);
        ScreenQuery query;
        query.sort = 2;
        query.offset = 100;
        query.min_price = 10;
        query.min_cap = 1;
        query.sector = "Finance";
        auto request = scanner_request(query);
        CHECK(request["sort"]["sortOrder"] == "asc" && request["range"][0] == 100 &&
              request["range"][1] == 200);
        CHECK(request["filter"].size() == 6);
        auto nulls = Json::parse(fixture(dir, "scanner.json"));
        nulls["data"][0]["d"][1] = nullptr;
        CHECK(std::isnan(parse_screen(nulls.dump()).rows[0].price));
        auto crypto =
            binance_history("[[1700000000000,\"100\",\"110\",\"90\",\"105\",\"10\"]]", "BTCUSDT", "1d");
        CHECK(crypto.currency == "USDT" && crypto.bars[0].close == 105);
        auto cq = binance_quote(R"({"lastPrice":"105","priceChangePercent":"5","closeTime":1700000000000})");
        CHECK(cq.rolling && cq.source == "Binance");
        CHECK(fails([] { stooq_history("<html>challenge</html>", "SPY"); }));
        auto csv = stooq_history("Date,Open,High,Low,Close,Volume\n2026-09-22,10,12,9,11,1000\n", "SPY");
        CHECK(csv.bars.size() == 1 && csv.bars[0].close == 11);
        CHECK(nasdaq_symbol("SPY") && !nasdaq_symbol("BTC-USD") && !nasdaq_symbol("ES=F") &&
              !nasdaq_symbol("^VIX"));
        std::cout << "Provider schemas, timestamps, incomplete OHLC, options pagination and scanner filters "
                     "passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
