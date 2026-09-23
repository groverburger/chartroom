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
        CHECK(normalize_symbol(" btc ") == "BTC-USD");
        CHECK(normalize_symbol("es") == "ES=F");
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
