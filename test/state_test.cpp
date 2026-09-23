#include "state.hpp"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
    } while (0)
int main() {
    using namespace cr;
    auto directory = std::filesystem::temp_directory_path() /
                     ("chartroom-state-test-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        // Worker completion wakes a sleeping event loop; callbacks stay on the caller thread.
        {
            Network network;
            std::mutex mutex;
            std::condition_variable ready;
            bool woke = false, completed = false;
            auto main_thread = std::this_thread::get_id();
            network.set_wakeup([&] {
                std::lock_guard lock(mutex);
                woke = true;
                ready.notify_one();
            });
            network.get("chartroom-test://no-network", [&](std::string, std::string error) {
                CHECK(std::this_thread::get_id() == main_thread);
                CHECK(!error.empty());
                completed = true;
            });
            std::unique_lock lock(mutex);
            CHECK(ready.wait_for(lock, std::chrono::seconds(3), [&] { return woke; }));
            lock.unlock();
            CHECK(!completed);
            network.poll();
            CHECK(completed);
        }
        History h;
        h.symbol = "SPY";
        h.interval = "1d";
        h.currency = "USD";
        h.fetched = now();
        for (int i = 0; i < 400; ++i)
            h.bars.push_back(
                {1736121600 + i * 86400, 100 + i * .1, 103 + i * .1, 99 + i * .1, 102 + i * .1, 1000.});
        atomic_json(cache_path(directory, "SPY", "1d"), encode_history(h));
        // Quote snapshots and history caches load through the same freshness rules.
        auto aapl = h;
        aapl.symbol = "AAPL";
        atomic_json(cache_path(directory, "AAPL", "1d"), encode_history(aapl));
        auto quote_time = aapl.bars.back().time + 120;
        atomic_json(cache_path(directory, "AAPL", "quote"), {{"symbol", "AAPL"},
                                                             {"price", 150},
                                                             {"change", 3.5},
                                                             {"asof", date(quote_time, "%Y-%m-%dT%H:%M:%S")},
                                                             {"fetched_at", date(now(), "%Y-%m-%dT%H:%M:%S")},
                                                             {"snapshot", true}});
        {
            State s(directory, true);
            CHECK(s.quotes.at("AAPL").price == 150);
            s.ensure("AAPL", 2); // Loading older chart history must not roll the sidebar back.
            CHECK(s.quotes.at("AAPL").price == 150 && s.quotes.at("AAPL").snapshot);
        }
        Json expected;
        {
            State s(directory, true);
            s.tick();
            auto &p = s.current();
            CHECK(p.bars.size() == 400);
            p.view.zoom(400, 2, .31);
            p.indicators.push_back(indicator("RIBBON"));
            p.indicators.back().timeframe = 4;
            p.volume = false;
            p.ohlc = true;
            p.logarithmic = true;
            p.indicators.back().background = false;
            p.indicators.back().ma_colors[0] = rgba(12, 34, 56);
            p.indicators.push_back(next_indicator("EMA", p.indicators));
            p.indicators.back().color = rgba(98, 76, 54);
            s.update(p);
            auto &duplicate = s.add("", true);
            s.tick();
            CHECK(duplicate.view.first == p.view.first);
            CHECK(duplicate.indicators[0].timeframe == 4);
            CHECK(!duplicate.volume);
            CHECK(duplicate.ohlc);
            CHECK(duplicate.logarithmic);
            CHECK(!duplicate.indicators[0].background);
            CHECK(duplicate.indicators[0].ma_colors[0] == rgba(12, 34, 56));
            CHECK(duplicate.indicators[1].color == rgba(98, 76, 54));
            s.lists.push_back({"Custom", {"SPY", "BTC-USD"}});
            s.selected_list = 2;
            s.layout = "Rows";
            Drawing note;
            note.kind = "text";
            note.a = note.b = {h.bars[310].time, 131.5};
            note.text = "Support / earnings";
            note.color = rgba(40, 120, 220);
            note.locked = true;
            note.timeframe = 2;
            s.drawings.add("SPY", note);
            Drawing fib;
            fib.kind = "fib";
            fib.a = note.a;
            fib.b = {h.bars[350].time, 139.5};
            fib.fib.reverse = true;
            fib.fib.extend_right = true;
            fib.fib.levels[4].color = rgba(12, 34, 56);
            s.drawings.add("SPY", fib);
            s.drawing_tools_open = true;
            p.show_drawings = false;
            s.save(true);
            expected = s.document();
            s.quotes.clear();
            CHECK(s.next_deadline() == std::numeric_limits<Time>::max());
            s.request_save();
            CHECK(s.next_deadline() <= now() + 1);
            s.save(true);
            CHECK(s.next_deadline() == std::numeric_limits<Time>::max());
        }
        {
            State s(directory, true);
            s.tick();
            CHECK(s.document() == expected);
            CHECK(s.panels.size() == 2);
            CHECK(s.panels[0]->logarithmic && s.panels[1]->logarithmic);
            CHECK(!s.panels.front()->show_drawings);
            CHECK(s.drawings.for_symbol("SPY").size() == 2);
            CHECK(s.drawings.for_symbol("SPY")[0].locked);
            CHECK(s.drawings.for_symbol("SPY")[0].text == "Support / earnings");
            CHECK(s.drawings.for_symbol("SPY")[0].timeframe == 2);
            CHECK(s.drawing_tools_open);
            CHECK(s.drawings.for_symbol("SPY")[1].fib.reverse);
            CHECK(s.drawings.for_symbol("SPY")[1].fib.extend_right);
            CHECK(s.drawings.for_symbol("SPY")[1].fib.levels[4].color == rgba(12, 34, 56));
            CHECK(!s.drawings.can_undo());
            CHECK(s.current().indicators[0].timeframe == 4);
            auto &p = s.current();
            s.select(p, "BTC", 2);
            s.tick();
            CHECK(p.bars.empty());
            CHECK(p.symbol == "BTC-USD");
            CHECK(s.drawings.for_symbol("BTC-USD").empty());
            CHECK(s.drawings.for_symbol("SPY").size() == 2);
            CHECK(s.panels.front()->bars.size() == 400);
        }
        auto julia = directory / "julia";
        auto imported = directory / "imported";
        atomic_json(cache_path(julia, "SPY", "1d"), encode_history(h));
        Json doc = {{"version", 1},
                    {"active", 7},
                    {"charts", Json::array({{{"id", 7},
                                             {"symbol", "SPY"},
                                             {"timeframe", 3},
                                             {"view",
                                              {{"first", 201.5},
                                               {"count", 70.25},
                                               {"length", 400},
                                               {"anchor", h.bars[200].time},
                                               {"follow", false},
                                               {"prices", {1, 2}}}},
                                             {"indicators", Json::array()}}})}};
        atomic_json(julia / "workspace.json", doc);
        {
            State s(imported, true, julia);
            s.tick();
            CHECK(s.current().view.first == 200.5);
            CHECK(s.current().view.count == 70.25);
            CHECK(s.current().bars.size() == 400);
            s.save(true);
            CHECK(read_json(julia / "workspace.json") == doc);
        }
        std::filesystem::remove_all(directory);
        std::cout << "Workspace, duplication, independent symbols, Julia import and persistence passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        std::filesystem::remove_all(directory);
        return 1;
    }
}
