#include "core.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("Failed: ") + #x + " at line " + std::to_string(__LINE__)); \
    } while (0)
static int checks = 0;
static bool near(double a, double b) {
    return std::abs(a - b) < 1e-8 * std::max({1., std::abs(a), std::abs(b)});
}
int main(int argc, char **argv) {
    try {
        using namespace cr;
        std::vector<Bar> bars;
        for (int i = 0; i < 400; ++i)
            bars.push_back(
                {1736121600 + i * 3600, 100 + i * .1, 102 + i * .1, 99 + i * .1, 101 + i * .1, 1000. + i});
        {
            ScrollGesture scroll;
            auto horizontal = scroll.update(2, .15, false, 1);
            CHECK(horizontal.pan == 2 && horizontal.zoom == 0);
            auto tail = scroll.update(.01, .04, false, 1.05);
            CHECK(tail.pan == .01 && tail.zoom == 0);
            auto vertical = scroll.update(.15, 2, false, 1.5);
            CHECK(vertical.pan == 0 && vertical.zoom == 2);
            auto vertical_tail = scroll.update(.04, .01, false, 1.55);
            CHECK(vertical_tail.pan == 0 && vertical_tail.zoom == .01);
            auto shifted = scroll.update(0, -1, true, 1.56);
            CHECK(shifted.pan == -1 && shifted.zoom == 0);
        }
        CHECK(resolve_symbol(" btc ") == "BTC-USD");
        CHECK(resolve_symbol("es") == "ES=F");
        CHECK(normalize_symbol("es") == "ES" && normalize_symbol("CL") == "CL"); // Eversource, Colgate.
        // Symbol arithmetic: canonical spelling, shorthands per leg, dashes inside tickers.
        CHECK(normalize_symbol(" rsp / spy ") == "RSP/SPY" && is_expression("RSP/SPY"));
        CHECK(normalize_symbol("( aapl + msft ) / 2") == "(AAPL+MSFT)/2");
        CHECK(normalize_symbol("spy - qqq") == "SPY - QQQ" && normalize_symbol("SPY - QQQ") == "SPY - QQQ");
        CHECK(normalize_symbol("BTC-USD") == "BTC-USD" && !is_expression("BTC-USD"));
        CHECK(resolve_symbol("btc/eth") == "BTC-USD/ETH-USD" && display_symbol("BTC-USD/^VIX") == "BTC/VIX");
        for (auto bad : {"SPY/", "(SPY/QQQ", "SPY QQQ", "2*3", "-SPY", "SPY/QQQ)", "A/B/C/D/E/F/G/H/I"}) {
            bool threw = false;
            try {
                normalize_symbol(bad);
            } catch (const std::exception &) {
                threw = true;
            }
            CHECK(threw);
        }
        {
            auto e = parse_expression("(AAPL+MSFT)/2 - SPY*0.5");
            CHECK(e.symbols == std::vector<std::string>({"AAPL", "MSFT", "SPY"}));
            CHECK(near(e.evaluate({{"AAPL", 10}, {"MSFT", 30}, {"SPY", 4}}), 18));
            CHECK(std::isnan(parse_expression("SPY/QQQ").evaluate({{"SPY", 1}, {"QQQ", 0}})));
            // Stocks open at 13:30 UTC and crypto at 00:00; both match on the UTC day. A day missing
            // from either leg is skipped, and high/low widen to contain the computed open and close.
            History stock{"SPY", "1d"}, coin{"BTC-USD", "1d"};
            for (int day = 0; day < 4; ++day) {
                stock.bars.push_back({day * 86400 + 48600, 100, 110, 90, 105, 1000});
                if (day != 2)
                    coin.bars.push_back({day * 86400, 50, 60, 40, 50, 10});
            }
            auto ratio = combine_expression("SPY/BTC-USD", {&stock, &coin});
            CHECK(ratio.bars.size() == 3 && ratio.bars[2].time == 3 * 86400 + 48600);
            CHECK(near(ratio.bars[0].open, 2) && near(ratio.bars[0].close, 2.1));
            CHECK(near(ratio.bars[0].high, 90. / 40) && near(ratio.bars[0].low, 110. / 60));
            CHECK(std::isnan(ratio.bars[0].volume) && ratio.symbol == "SPY/BTC-USD");
        }
        CHECK(!valid({0, 10, 9, 8, 11, 0}));
        auto ribbon = calculate(indicator("RIBBON"), bars);
        CHECK(ribbon.lines.size() == 5);
        CHECK(ribbon.scores.back() == 4);
        CHECK(ribbon_color(4) == rgba(0, 160, 210));
        CHECK(ribbon_color(-4) == rgba(136, 0, 136));
        std::vector<Indicator> averages;
        for (int i = 0; i < 20; ++i) {
            auto added = next_indicator(i % 2 ? "SMA" : "EMA", averages);
            if (!averages.empty())
                CHECK(added.color != averages.back().color);
            averages.push_back(added);
        }
        averages.back().color = rgba(12, 34, 56);
        CHECK(next_indicator("EMA", averages).color != averages.back().color);
        auto custom = indicator("RIBBON");
        custom.background = false;
        custom.ma_colors[2] = rgba(12, 34, 56);
        auto custom_result = calculate(custom, bars);
        CHECK(!custom_result.background);
        CHECK(custom_result.scores == ribbon.scores);
        CHECK(custom_result.colors[2] == rgba(12, 34, 56));
        CHECK(encode_indicator(decode_indicator(encode_indicator(custom))) == encode_indicator(custom));
        custom.show_emas = false;
        CHECK(calculate(custom, bars).lines.empty());
        CHECK(calculate(custom, bars).colored_bars);
        auto average = indicator("EMA");
        average.color = rgba(23, 45, 67);
        CHECK(calculate(average, bars).colors[0] == average.color);
        CHECK(decode_indicator(Json{{"kind", "RIBBON"}}).background);
        auto rsi = calculate(indicator("RSI"), bars);
        CHECK(rsi.lines[0].back() == 100);
        // A/D parity with open8585/ratings.py _ad_raw_history on the same synthetic bars.
        {
            std::vector<Bar> ad_bars;
            for (int i = 0; i < 160; ++i) {
                double close = 100 + (i * 37 % 23) - 11 + i * .25;
                ad_bars.push_back({Time(i) * 86400, close, close + 1 + (i % 5) * .5, close - 1 - (i % 3) * .5,
                                   close, i == 90 ? missing : 1000. + (i * 7919 % 997)});
            }
            auto ad = ad_balance(ad_bars);
            CHECK(std::isnan(ad[61]));
            for (auto [i, expected] : {std::pair{62, -7.152176751632532},
                                       {90, -25.566541328440934},
                                       {100, -25.566541328440923},
                                       {159, -4.837748858072912}})
                CHECK(near(ad[size_t(i)], expected));
            CHECK(std::string(ad_grade(ad[159])) == "C-" && std::string(ad_grade(-18.249625)) == "D");
            CHECK(std::string(ad_grade(80)) == "A+" && std::string(ad_grade(-80)) == "E");
            CHECK(calculate(indicator("AD"), ad_bars, 2).name == "A/D 20 / C-");
            CHECK(calculate(indicator("AD"), ad_bars, 3).name == "A/D 20");
        }
        // Point and figure, worked by hand: $1 boxes, 3-box reversal, closes only.
        {
            auto closes = [](std::vector<double> prices) {
                std::vector<Bar> out;
                for (size_t i = 0; i < prices.size(); ++i)
                    out.push_back({Time(i) * 86400, prices[i], prices[i], prices[i], prices[i], 1});
                return out;
            };
            PnfSettings s{false, 1, 1, 3, true};
            // 100 starts; rises to 104; 102 is no reversal; 100.9 reaches 101 = 104 - 3; 99.5 extends; 103 reverses.
            auto c = point_and_figure(closes({100, 101.2, 103.5, 104, 102, 100.9, 99.5, 103}), s).columns;
            CHECK(c.size() == 3);
            CHECK(c[0].up && c[0].low == 100 && c[0].high == 104);
            CHECK(!c[1].up && c[1].high == 103 && c[1].low == 100);
            CHECK(c[2].up && c[2].low == 101 && c[2].high == 103 && !c[2].signal);
            CHECK(c[0].bar(100) == 0 && c[0].bar(101) == 1 && c[0].bar(104) == 3);
            CHECK(c[1].bar(103) == 5 && c[1].bar(100) == 6 && c[2].bar(103) == 7);
            // A later X column above the prior X top is a double-top breakout.
            c = point_and_figure(closes({100, 101.2, 103.5, 104, 100.9, 99.5, 103, 105.2}), s).columns;
            CHECK(c.size() == 3 && c[2].high == 105 && c[2].signal == 1);
            // High/low method: the high extends the X column before the low is considered.
            std::vector<Bar> hl = {{0, 100, 100.5, 99.5, 100, 1}, {86400, 100, 102.4, 100, 102, 1},
                                   {172800, 102, 102.2, 99.1, 99.5, 1}};
            c = point_and_figure(hl, {false, 1, 1, 3, false}).columns;
            CHECK(c.size() == 1 && c[0].high == 102); // A low of 99.1 reaches only row 100, not 102 - 3.
            // Log rows pass through 100; touching a level fills its box despite floating-point error.
            auto log_chart = point_and_figure(closes({100, 101, 102.01, 103.0301}), {true, 1, 0, 3, true});
            CHECK(log_chart.columns.size() == 1 && log_chart.columns[0].high == 3);
            CHECK(near(log_chart.price(3), 103.0301) && near(log_chart.row(100), 0));
            CHECK(!point_and_figure(closes({10, -5, 12}), {true, 1, 0, 3, true}).error.empty());
            CHECK(point_and_figure(closes({10, -5, 12}), {false, 1, 1, 3, true}).error.empty());
            CHECK(point_and_figure(closes({150}), {false, 1, 0, 3, true}).step == 2); // Traditional box.
            CHECK(pnf_traditional_box(50) == 1 && pnf_traditional_box(3) == .25 && pnf_traditional_box(30000) == 500);
            CHECK(encode_pnf(decode_pnf(encode_pnf(s))) == encode_pnf(s) && decode_pnf(Json()).reversal == 3);
        }
        auto sma = calculate(indicator("SMA"), bars);
        CHECK(std::isnan(sma.lines[0][18]));
        CHECK(near(sma.lines[0][19], 101.95));
        for (auto k : kinds) {
            auto s = indicator(k);
            CHECK(encode_indicator(decode_indicator(encode_indicator(s))) == encode_indicator(s));
            auto r = calculate(s, bars);
            for (auto &line : r.lines)
                CHECK(line.size() == bars.size());
        }
        for (double first : {-30.5, 289.25, 430.75})
            for (double count : {15., 110.5, 400.})
                for (double anchor : {0., .31, 1.})
                    for (double wheel : {-2., 2.}) {
                        View v{first, count};
                        double world = first + anchor * count;
                        v.zoom(bars.size(), wheel, anchor);
                        CHECK(near(v.first + anchor * v.count, world));
                        auto [lo, hi] = price_limits(bars, v, {ribbon});
                        auto [a, b] = v.visible(bars.size());
                        for (int i = a; i < b; ++i) {
                            CHECK(lo <= bars[i].low && bars[i].high <= hi);
                            for (auto &line : ribbon.lines)
                                CHECK(lo <= line[i] && line[i] <= hi);
                        }
                    }
        PriceScale log_axis{10, 1000, true};
        CHECK(near(log_axis.fraction(100), .5));
        CHECK(near(log_axis.fraction(20) - log_axis.fraction(10),
                   log_axis.fraction(200) - log_axis.fraction(100)));
        for (double price : {1., 10., 20., 100., 1000., 2000.})
            CHECK(near(log_axis.price(log_axis.fraction(price)), price));
        CHECK(!std::isfinite(log_axis.fraction(0)) && !std::isfinite(log_axis.fraction(-10)));
        for (View log_view : {View{0, 400}, View{320, 20}, View{-500, 100}}) {
            auto [lo, hi] = price_limits(bars, log_view, {ribbon}, true);
            CHECK(lo > 0 && hi > lo);
            auto [first, end] = log_view.visible(bars.size());
            for (int i = first; i < end; ++i) {
                CHECK(lo <= bars[i].low && hi >= bars[i].high);
                for (auto &line : ribbon.lines)
                    if (std::isfinite(line[i]))
                        CHECK(lo <= line[i] && hi >= line[i]);
            }
        }
        std::vector<Bar> flat = {{0, .00001, .00001, .00001, .00001, 0}};
        auto flat_limits = price_limits(flat, View{0, 1}, {}, true);
        CHECK(flat_limits.first > 0 && flat_limits.first < .00001 && flat_limits.second > .00001);
        flat[0] = {0, 1, 2, -1, 1, 0};
        CHECK(price_limits(flat, View{0, 1}, {}, true).first < 0); // Linear fallback preserves negative data.
        auto log_grid = log_price_grid(9, 110000, 600);
        for (double level : {10., 100., 1000., 10000., 100000.})
            CHECK(std::find(log_grid.levels.begin(), log_grid.levels.end(), level) != log_grid.levels.end());
        PriceScale grid_scale{9, 110000, true};
        for (size_t i = 1; i < log_grid.levels.size(); ++i)
            CHECK((grid_scale.fraction(log_grid.levels[i]) - grid_scale.fraction(log_grid.levels[i - 1])) *
                      600 >=
                  39.99);
        View partial{9.75, 15.5};
        CHECK((partial.visible(400) == std::pair<int, int>(9, 26)));
        View v;
        v.fit(bars.size());
        auto latest_position = [](const View &view, size_t n) {
            return (double(n) - .5 - view.first) / view.count;
        };
        CHECK(near(latest_position(v, bars.size()), .75));
        CHECK(near(v.count * .75, View::default_count));
        for (size_t n : {1u, 5u, 200u}) {
            View short_view;
            short_view.fit(n);
            CHECK(near(latest_position(short_view, n), .75));
            auto fitted = short_view;
            short_view.zoom(n, -1, .75);
            CHECK(near(short_view.count, fitted.count));
            CHECK(near(short_view.first, fitted.first));
        }
        auto original = v;
        for (int i = 0; i < 100; ++i) {
            v.zoom(bars.size(), .25, .31);
            v.zoom(bars.size(), -.25, .31);
        }
        CHECK(near(v.first, original.first) && near(v.count, original.count));
        auto old = price_limits(bars, v);
        auto shifted = bars;
        for (auto &b : shifted) {
            b.low += 20;
            b.high += 20;
            b.open += 20;
            b.close += 20;
        }
        auto next = price_limits(shifted, v);
        CHECK(near(next.first, old.first + 20) && near(next.second, old.second + 20));
        auto document = encode_view(v, bars);
        CHECK(document["follow"] == true);
        View restored;
        restore_view(restored, bars, document);
        CHECK(near(v.first, restored.first) && near(v.count, restored.count));
        document["prices"] = {1, 2};
        restore_view(restored, bars, document);
        CHECK(price_limits(bars, restored) == price_limits(bars, v));
        auto extra = bars;
        extra.push_back({bars.back().time + 3600, 140, 145, 139, 144, 100});
        preserve_view(v, bars, extra);
        CHECK(near(v.first, original.first + 1));
        CHECK(near(latest_position(v, extra.size()), .75));
        // Older saved latest views adopt the margin without changing their zoom.
        auto legacy = document;
        legacy["first"] = double(bars.size()) - 100;
        legacy["count"] = 100;
        restore_view(restored, bars, legacy);
        CHECK(near(latest_position(restored, bars.size()), .75) && restored.count == 100);
        // Deliberate historical pans remain where the user left them on refresh/restart.
        View historical{50.25, 100};
        auto historical_document = encode_view(historical, bars);
        CHECK(historical_document["follow"] == false);
        restore_view(restored, extra, historical_document);
        CHECK(restored.first == historical.first && restored.count == historical.count);
        auto grouped = aggregate(bars, 4);
        CHECK(grouped.size() == 100);
        CHECK(near(grouped[0].open, bars[0].open));
        CHECK(near(grouped[0].close, bars[3].close));
        CHECK(near(grouped[0].volume, 4006));
        auto grid = price_grid(51000, 109000, 600);
        CHECK(grid.step == 10000);
        CHECK(std::find(grid.levels.begin(), grid.levels.end(), 100000) != grid.levels.end());
        CHECK(price_label(12500, 2500) == "12.5k");
        Json fixture = {
            {"chart",
             {{"error", nullptr},
              {"result", Json::array({{{"meta",
                                        {{"symbol", "SPY"},
                                         {"dataGranularity", "1d"},
                                         {"regularMarketTime", 1736208200},
                                         {"regularMarketPrice", 14}}},
                                       {"timestamp", {1736121600, 1736208000}},
                                       {"indicators",
                                        {{"quote", Json::array({{{"open", {10, 11}},
                                                                 {"high", {12, 13}},
                                                                 {"low", {9, 10}},
                                                                 {"close", {11, nullptr}},
                                                                 {"volume", {100, 200}}}})}}}}})}}}};
        auto h = parse_history(fixture.dump(), "SPY", "1d");
        CHECK(h.bars.size() == 2);
        CHECK(h.bars.back().close == 14 && h.bars.back().high == 14);
        CHECK(quote(h).has_value());
        CHECK(near(quote(h)->change, (14. / 11 - 1) * 100));
        CHECK(encode_history(decode_history(encode_history(h))) == encode_history(h));
        fixture["chart"]["result"][0]["meta"]["regularMarketTime"] = 1736294500;
        auto tail = parse_history(fixture.dump(), "SPY", "1d");
        CHECK(tail.bars.size() == 1);
        auto merged = merge_history(h, tail);
        CHECK(merged.bars.size() == 2 && merged.bars.back().close == 14);
        // Sidebar quotes do not depend on complete OHLC arrays or a present yesterday candle.
        Json snapshot = {
            {"chart",
             {{"error", nullptr},
              {"result", Json::array({{{"meta",
                                        {{"symbol", "SPY"},
                                         {"regularMarketPrice", 105},
                                         {"regularMarketTime", 1736294500},
                                         {"previousClose", 100},
                                         {"chartPreviousClose", 80}}},
                                       {"timestamp", nullptr},
                                       {"indicators", {{"quote", Json::array({Json::object()})}}}}})}}}};
        auto fresh_quote = parse_quote(snapshot.dump(), "SPY");
        CHECK(fresh_quote.price == 105 && near(fresh_quote.change, 5) && fresh_quote.snapshot);
        CHECK(fresh_quote.asof == 1736294500);
        auto extended_snapshot = snapshot;
        auto &extended_result = extended_snapshot["chart"]["result"][0];
        extended_result["meta"]["hasPrePostMarketData"] = true;
        extended_result["meta"]["regularMarketChangePercent"] = -0.08;
        extended_result["meta"]["currentTradingPeriod"] = {
            {"pre", {{"start", 1736323200}, {"end", 1736346600}}},
            {"post", {{"start", 1736370000}, {"end", 1736384400}}}};
        extended_result["timestamp"] = {1736323200, 1736323260, 1736323320};
        extended_result["indicators"]["quote"][0]["close"] = {106, 107, nullptr};
        auto eq = parse_quote(extended_snapshot.dump(), "SPY");
        CHECK(eq.change == -.08 && eq.price == 105);
        CHECK(eq.extended && eq.extended->price == 107 && eq.extended->session == "Pre");
        extended_result["timestamp"] = {1736370000, 1736370060, 1736370120};
        CHECK(parse_quote(extended_snapshot.dump(), "SPY").extended->session == "Post");
        extended_result["meta"]["regularMarketTime"] = 1736371000;
        CHECK(!parse_quote(extended_snapshot.dump(), "SPY").extended);
        History intraday;
        intraday.interval = "1h";
        intraday.meta = snapshot["chart"]["result"][0]["meta"];
        intraday.fetched = now();
        CHECK(quote(intraday) && near(quote(intraday)->change, 5));
        auto invalid_quote = [&](Json j, const std::string &symbol = "SPY") {
            bool failed = false;
            try {
                parse_quote(j.dump(), symbol);
            } catch (...) {
                failed = true;
            }
            CHECK(failed);
        };
        invalid_quote(snapshot, "QQQ");
        auto bad_snapshot = snapshot;
        bad_snapshot["chart"]["result"][0]["meta"].erase("previousClose");
        invalid_quote(bad_snapshot); // Never substitute chartPreviousClose from the requested range.
        bad_snapshot = snapshot;
        bad_snapshot["chart"]["result"][0]["meta"]["previousClose"] = 0;
        invalid_quote(bad_snapshot);
        bad_snapshot = snapshot;
        bad_snapshot["chart"]["result"][0]["meta"]["regularMarketPrice"] = nullptr;
        invalid_quote(bad_snapshot);
        bad_snapshot = snapshot;
        bad_snapshot["chart"]["error"] = {{"description", "Unavailable"}};
        invalid_quote(bad_snapshot);
        // A daily chart's latest quote price can be ahead of the OHLC close.
        h.meta["regularMarketTime"] = h.bars.back().time + 100;
        h.meta["regularMarketPrice"] = 15;
        CHECK(quote(h)->price == 15 && near(quote(h)->change, (15. / 11 - 1) * 100));
        History crypto;
        crypto.symbol = "BTC-USD";
        crypto.interval = "1h";
        crypto.meta = {{"instrumentType", "CRYPTOCURRENCY"}, {"chartroomIncompleteTimes", {1736121600}}};
        History minutes;
        minutes.symbol = crypto.symbol;
        minutes.interval = "1m";
        for (int i = 0; i < 60; ++i)
            minutes.bars.push_back({1736121600 + i * 60, 100., 110. + i, 90., 105., 1.});
        auto recovered = recover_crypto(crypto, minutes, {1736121600}, 1736125200);
        CHECK(recovered.bars.size() == 1);
        CHECK(std::isnan(recovered.bars[0].volume));
        CHECK(recovered.bars[0].high == 169);
        minutes.bars.erase(minutes.bars.begin() + 20);
        CHECK(recover_crypto(crypto, minutes, {1736121600}, 1736125200).bars.empty());
        // A quote snapshot must merge into its candle, never appear as a second bar.
        crypto.interval = "1d";
        crypto.bars = {{1736121600, 100, 110, 90, 105, 100}, {1736121700, 108, 108, 108, 108, 0}};
        auto normalized = normalize_crypto(crypto);
        CHECK(normalized.bars.size() == 1 && normalized.bars[0].close == 108 &&
              normalized.bars[0].volume == 100);
        // Hand-calculated channels, oscillators and volume studies, including warmup and gaps.
        std::vector<Bar> study = {{1, 10, 12, 8, 10, 100},
                                  {2, 11, 14, 9, 12, 200},
                                  {3, 12, 13, 10, 11, 300},
                                  {4, 11, 16, 7, 15, 400},
                                  {5, 15, 17, 13, 14, 500}};
        auto dc = indicator("DONCHIAN");
        dc.period = 3;
        auto channel = calculate(dc, study);
        CHECK(!channel.pane && std::isnan(channel.lines[0][1]));
        CHECK(channel.lines[0][2] == 14 && channel.lines[1][2] == 8 && channel.lines[2][2] == 11);
        CHECK(channel.lines[0][4] == 17 && channel.lines[1][4] == 7);
        dc.exclude_current = true;
        dc.midline = false;
        auto prior = calculate(dc, study);
        CHECK(prior.lines.size() == 2 && std::isnan(prior.lines[0][2]));
        CHECK(prior.lines[0][3] == 14 && prior.lines[1][3] == 8);
        CHECK(encode_indicator(decode_indicator(encode_indicator(dc))) == encode_indicator(dc));
        auto stoch = indicator("STOCH");
        stoch.period = 3;
        stoch.slow = 1;
        stoch.signal = 2;
        auto stochastic = calculate(stoch, study);
        CHECK(stochastic.pane && near(stochastic.lines[0][2], 50));
        CHECK(near(stochastic.lines[0][3], 800. / 9));
        CHECK(std::isnan(stochastic.lines[1][2]));
        CHECK(near(stochastic.lines[1][3], (50 + 800. / 9) / 2));
        auto roc = indicator("ROC");
        roc.period = 2;
        CHECK(near(calculate(roc, study).lines[0][2], 10));
        CHECK(near(calculate(roc, study).lines[0][3], 25));
        auto vwma = indicator("VWMA");
        vwma.period = 2;
        CHECK(near(calculate(vwma, study).lines[0][1], 3400. / 300));
        auto no_volume = study;
        no_volume[2].volume = missing;
        auto weighted = calculate(vwma, no_volume);
        CHECK(std::isnan(weighted.lines[0][2]) && std::isnan(weighted.lines[0][3]));
        CHECK(near(weighted.lines[0][4], 13000. / 900));
        no_volume[0].volume = no_volume[1].volume = 0;
        CHECK(std::isnan(calculate(vwma, no_volume).lines[0][1]));
        CHECK(calculate(indicator("OBV"), study).lines[0] == std::vector<double>({0, 200, -100, 300, -200}));
        CHECK(std::isnan(calculate(indicator("OBV"), no_volume).lines[0][4]));
        auto kc = indicator("KC");
        kc.period = 2;
        kc.slow = 2;
        kc.deviation = 2;
        auto keltner = calculate(kc, study);
        CHECK(near(keltner.lines[0][1], 34. / 3));
        CHECK(near(keltner.lines[1][1], 34. / 3 + 9));
        CHECK(near(keltner.lines[2][1], 34. / 3 - 9));
        // Appending future bars never changes an existing historical indicator value.
        for (auto kind : kinds) {
            if (std::string(kind) == "RIBBON" || std::string(kind) == "PIVOTS")
                continue;
            auto spec = indicator(kind);
            spec.period = spec.slow = spec.signal = 2;
            auto short_bars = study;
            short_bars.pop_back();
            auto before = calculate(spec, short_bars), after = calculate(spec, study);
            for (size_t line = 0; line < before.lines.size(); ++line)
                for (size_t i = 0; i < short_bars.size(); ++i)
                    CHECK(std::isnan(before.lines[line][i])
                              ? std::isnan(after.lines[line][i])
                              : near(before.lines[line][i], after.lines[line][i]));
            CHECK(encode_indicator(decode_indicator(encode_indicator(spec))) == encode_indicator(spec));
        }
        // Swing pivots intentionally use right-hand bars, but only after those bars close.
        auto pivots = indicator("PIVOTS");
        pivots.period = 2;
        std::vector<Bar> swings;
        const std::vector<double> highs = {10, 12, 18, 14, 13, 12, 14, 13, 11};
        const std::vector<double> lows = {8, 9, 11, 10, 7, 4, 8, 9, 8};
        for (size_t i = 0; i < highs.size(); ++i)
            swings.push_back({1736121600 + Time(i) * 3600, 10, highs[i], lows[i], 10, 100});
        Time close4 = swings[4].time + 3600;
        auto before_close = calculate(pivots, swings, 0, nullptr, nullptr, close4 - 1);
        CHECK(std::isnan(before_close.lines[0][2]));
        auto after_close = calculate(pivots, swings, 0, nullptr, nullptr, close4);
        CHECK(after_close.pivots && after_close.lines[0][2] == 18);
        CHECK(std::isnan(after_close.lines[1][5]));
        auto all_closed = calculate(pivots, swings, 0, nullptr, nullptr, swings.back().time + 3600);
        CHECK(all_closed.lines[1][5] == 4 && all_closed.lines[0][6] == 14);
        for (int i : {0, 1, 7, 8})
            CHECK(std::isnan(all_closed.lines[0][i]) && std::isnan(all_closed.lines[1][i]));
        auto tied = swings;
        tied[3].high = 18;
        tied[6].low = 4;
        CHECK(std::isnan(calculate(pivots, tied, 0).lines[0][2]));
        CHECK(std::isnan(calculate(pivots, tied, 0).lines[1][5]));
        auto only_five = swings;
        only_five.resize(5);
        CHECK(calculate(pivots, only_five, 0, nullptr, nullptr, close4).lines[0][2] == 18);
        only_five.pop_back();
        CHECK(std::isnan(calculate(pivots, only_five, 0).lines[0][2]));
        History cached;
        cached.bars = swings;
        cached.bars.resize(5);
        cached.fetched = close4 - 1;
        // An old snapshot of an unfinished bar must not become confirmed just as time passes.
        CHECK(std::isnan(calculate(pivots, cached.bars, 0, &cached, nullptr, close4 + 3600).lines[0][2]));
        cached.fetched = close4;
        CHECK(calculate(pivots, cached.bars, 0, &cached, nullptr, close4).lines[0][2] == 18);
        // A shortened last session bar closes at the provider's actual session boundary.
        cached.meta["currentTradingPeriod"]["regular"] = {{"start", swings[4].time}, {"end", close4 - 1800}};
        cached.fetched = close4 - 1800;
        CHECK(calculate(pivots, cached.bars, 0, &cached, nullptr, close4 - 1800).lines[0][2] == 18);
        pivots.show_highs = false;
        pivots.low_color = rgba(1, 2, 3);
        auto lows_only = calculate(pivots, swings, 0);
        CHECK(std::isnan(lows_only.lines[0][2]) && lows_only.lines[1][5] == 4);
        CHECK(encode_indicator(decode_indicator(encode_indicator(pivots))) == encode_indicator(pivots));
        if (argc > 1) {
            std::ifstream f(argv[1]);
            auto vectors = Json::parse(f);
            auto source = decode_history(vectors["history"]);
            for (auto &test : vectors["indicators"]) {
                auto spec = decode_indicator(test["spec"]);
                auto result = calculate(spec, source.bars);
                CHECK(result.lines.size() == test["lines"].size());
                for (size_t j = 0; j < result.lines.size(); ++j)
                    for (size_t i = 0; i < source.bars.size(); ++i) {
                        auto expected = test["lines"][j][i];
                        CHECK(expected.is_null() ? std::isnan(result.lines[j][i])
                                                 : near(result.lines[j][i], expected.get<double>()));
                    }
                for (size_t i = 0; i < result.scores.size(); ++i)
                    CHECK(near(result.scores[i], test["scores"][i].get<double>()));
            }
        }
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
