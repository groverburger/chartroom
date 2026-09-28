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
        auto cached_quote = read_json(cache_path(directory, "AAPL", "quote"));
        cached_quote["extended"] = {{"price", 153},
                                    {"asof", quote_time + 3600},
                                    {"fetched", now()},
                                    {"session", "Post"},
                                    {"source", "Nasdaq"}};
        atomic_json(cache_path(directory, "AAPL", "quote"), cached_quote);
        auto obsolete = cached_quote;
        obsolete["symbol"] = "QQQ";
        obsolete["source"] = "Nasdaq";
        obsolete["version"] = 1;
        atomic_json(cache_path(directory, "QQQ", "quote"), obsolete);
        {
            State s(directory, true);
            CHECK(!s.quotes.count("QQQ"));
            CHECK(s.quotes.at("AAPL").extended->price == 153);
            CHECK(s.quotes.at("AAPL").extended->source == "Nasdaq");
            CHECK(s.quotes.at("AAPL").price == 150);
            s.ensure("AAPL", 2); // Loading older chart history must not roll the sidebar back.
            CHECK(s.quotes.at("AAPL").price == 150 && s.quotes.at("AAPL").snapshot);
        }
        // Live markers expire with one scheduled redraw even when working offline.
        {
            State s(directory, true);
            s.tick();
            s.quotes.clear();
            Quote q{150, 3.5, now() - 86400, now(), true};
            q.extended = ExtendedQuote{153, now() - 1700, now(), "Pre"};
            s.quotes["SPY"] = q;
            s.tick();
            s.save(true);
            CHECK(s.next_deadline() == q.extended->asof + 1800);
            CHECK(!s.tick());
            s.quotes["SPY"].extended->fetched = now() - 601;
            CHECK(s.tick());
            CHECK(!fresh_extended(s.quotes["SPY"], now()));
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
            p.earnings = false;
            p.eps = true;
            p.eps_ttm = true;
            s.fundamentals.open = true;
            s.fundamentals.follow = false;
            s.fundamentals.symbol = "AAPL";
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
            CHECK(!duplicate.earnings && duplicate.eps && duplicate.eps_ttm);
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
            s.options.open = true;
            s.options.symbol = "AAPL";
            s.options.puts = true;
            s.options.expiry = "2026-12-18";
            s.screener.open = true;
            s.screener.query.sort = 2;
            s.screener.query.offset = 100;
            s.screener.query.min_price = 5;
            s.screener.query.min_cap = 3;
            s.screener.query.min_volume = 100000;
            s.screener.query.sector = "Finance";
            s.screener.search = "Bank";
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
        // Closed charts preserve their identity and complete settings across restarts.
        auto archive_dir = directory / "archive";
        atomic_json(cache_path(archive_dir, "SPY", "1d"), encode_history(h));
        Json archived;
        {
            State s(archive_dir, true);
            s.tick();
            auto &p = s.current();
            p.view.zoom(400, 2, .31);
            p.indicators.push_back(indicator("RIBBON"));
            p.indicators.back().background = false;
            p.volume = false;
            p.earnings = false;
            p.eps = true;
            p.eps_ttm = true;
            s.fundamentals.open = true;
            s.fundamentals.follow = false;
            s.fundamentals.symbol = "AAPL";
            p.logarithmic = true;
            p.option_marker = OptionMarker{"2026-10-16", 135, true};
            p.option_ladder = p.ladder_volume = true;
            s.options.target_chart = p.id;
            archived = s.document()["charts"][0];
            s.add("AAPL");
            p.open = false;
            s.remove_closed();
            CHECK(s.panels.size() == 1 && s.closed_charts.size() == 1);
            CHECK(s.closed_charts[0] == archived);
            CHECK(!s.option_target()); // Never attach SPY options to AAPL.
            s.save(true);
        }
        {
            State s(archive_dir, true);
            CHECK(s.closed_charts.size() == 1);
            CHECK(s.reopen_chart(0));
            s.tick();
            CHECK(s.current().id == archived["id"]);
            CHECK(s.document()["charts"][1] == archived);
            CHECK(s.option_target() == &s.current());
            CHECK(s.options_visible()); // A ladder keeps its snapshot scheduled with Options closed.
            CHECK(s.closed_charts.empty());
            CHECK(!s.reopen_chart(0));
            int highest = 0;
            for (auto &p : s.panels)
                highest = std::max(highest, p->id);
            CHECK(s.add().id > highest);
            for (int i = 0; i < 25; ++i) {
                s.add().open = false;
                s.remove_closed();
            }
            CHECK(s.closed_charts.size() == 20);
            for (auto &p : s.panels)
                p->open = false;
            s.remove_closed();
            CHECK(s.panels.size() == 1 && s.current().open);
            s.current().option_marker = OptionMarker{"2026-10-16", 135, true};
            s.select(s.current(), "BTC", 2);
            CHECK(!s.current().option_marker);
        }
        // Migrate a legacy singleton sidebar, preserving user lists and edits.
        auto watch_dir = directory / "watchlists";
        auto legacy = expected;
        legacy.erase("watchlist_windows");
        legacy.erase("watchlist_catalog");
        legacy.erase("next_watchlist_id");
        legacy["lists"] = Json::array({{{"name", "My ideas"}, {"symbols", {"SPY", "AAPL"}}}});
        legacy["selected_list"] = 0;
        atomic_json(watch_dir / "workspace.json", legacy);
        int favorite_index = 0, deleted_size = 0;
        {
            State s(watch_dir, true);
            CHECK(s.lists.front().name == "My ideas" && s.lists.front().symbols.size() == 2);
            CHECK(s.lists.size() >= 20 && s.watchlists.size() == 2);
            favorite_index = s.watchlists[0]->list;
            CHECK(s.lists[size_t(favorite_index)].name == "At a glance");
            CHECK(s.watchlists[1]->list == 0);
            s.lists[size_t(favorite_index)].symbols = {"GLD", "QQQ"};
            auto &third = s.add_watchlist(favorite_index);
            CHECK(third.id != s.watchlists.front()->id);
            s.delete_list(0);
            --favorite_index;
            CHECK(s.watchlists[0]->list == favorite_index && third.list == favorite_index);
            CHECK(s.lists[size_t(third.list)].symbols == std::vector<std::string>({"GLD", "QQQ"}));
            s.delete_list(int(s.lists.size()) - 1);
            deleted_size = int(s.lists.size());
            s.save(true);
        }
        {
            State s(watch_dir, true);
            CHECK(s.watchlists.size() == 3 && int(s.lists.size()) == deleted_size);
            CHECK(s.lists[size_t(favorite_index)].symbols == std::vector<std::string>({"GLD", "QQQ"}));
            CHECK(s.watchlists[2]->list == favorite_index);
            s.watchlists.clear();
            s.save(true);
        }
        {
            State s(watch_dir, true);
            CHECK(s.watchlists.empty()); // Closing all windows is persistent, not a special case.
            CHECK(int(s.lists.size()) == deleted_size); // Deleted starters must not reappear.
            CHECK(s.add_watchlist(favorite_index).id > 3);
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
