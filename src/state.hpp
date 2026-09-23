#pragma once
#include "core.hpp"
#include "drawings.hpp"
#include "providers.hpp"
#include <filesystem>
#include <memory>
namespace cr {
struct Series {
    History history;
    uint64_t revision = 0;
    bool loaded = false, loading = false;
    Time next = 0;
    std::string error;
};
struct List {
    std::string name;
    std::vector<std::string> symbols;
};
struct Panel {
    int id = 1, tf = 2;
    std::string symbol = "SPY";
    View view;
    ScrollGesture scroll;
    bool candles = true, ohlc = false, volume = true, open = true, logarithmic = false;
    std::vector<Indicator> indicators;
    std::vector<Bar> bars;
    std::vector<Result> results;
    Json pending;
    bool pending_julia = false;
    std::string signature;
    uint64_t revision = 0;
    bool dirty = true;
    char search[128]{};
    bool show_drawings = true;
    DrawingInteraction drawing;
};
struct OptionsWindow {
    bool open = false, loading = false, focus = false;
    std::string symbol = "SPY", expiry, key, error, centered_key;
    bool puts = false, dates_loading = false;
    std::string dates_symbol, dates_error;
    OptionDates dates;
    Time dates_retry = 0;
    uint64_t dates_generation = 0;
    uint64_t generation = 0;
    Time next = 0;
    OptionChain data;
};
struct ScreenerWindow {
    bool open = false, loading = false, focus = false;
    ScreenQuery query;
    std::string key, error, search;
    uint64_t generation = 0;
    Time next = 0;
    ScreenResult data;
};
class State {
  public:
    explicit State(std::filesystem::path directory, bool offline = false, std::filesystem::path import = {});
    ~State();
    std::filesystem::path directory;
    bool offline = false, relayout = true;
    std::string notice, layout = "Grid", ini;
    int active = 1, next_id = 1, selected_list = 0, width = 1440, height = 900, x = -1, y = -1;
    bool maximized = false, drawing_tools_open = false, drawing_tools_focus = false;
    uint64_t available_build = 0;
    OptionsWindow options;
    ScreenerWindow screener;
    void refresh_options(bool force = false);
    void refresh_option_dates(bool full = false);
    void refresh_screener();
    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<List> lists;
    DrawingBook drawings;
    std::map<std::string, Quote> quotes;
    std::map<std::string, std::string> quote_errors;
    std::map<std::string, Series> series;
    Panel &current();
    Panel &add(std::string symbol = "", bool duplicate = false);
    void select(Panel &, std::string, int tf);
    Series &ensure(const std::string &, int tf);
    void refresh(Panel &);
    bool tick(); // True when background work changed something visible.
    Time next_deadline() const;
    void set_wakeup(std::function<void()> wakeup) {
        network.set_wakeup(std::move(wakeup));
    }
    void request_save() {
        save_pending = true;
    }
    void update(Panel &);
    Json document() const;
    void save(bool force = false);
    void remove_closed();
    void import_julia(const std::filesystem::path &);

  private:
    Network network;
    Providers providers{network};
    std::map<std::string, Time> quote_next;
    std::set<std::string> quote_loading;
    Time quote_tick = 0, save_next = 0;
    std::string last_saved;
    bool may_save = true, save_pending = true;
    uint64_t visual_revision = 0;
    std::map<std::string, bool> quote_stale;
    std::set<std::pair<std::string, int>> needed_series() const;
    std::vector<std::string> visible_symbols() const;
    std::string key(const std::string &, int tf) const;
    void fetch(const std::string &, int tf, bool full = false);
    void fetch_quote(const std::string &);
    void accept_quote(const std::string &, Quote, bool persist = true);
    void repair(History, std::function<void(History)>);
    void accept(const std::string &, History);
    void restore(const Json &, bool julia = false);
};
std::filesystem::path default_directory();
std::filesystem::path cache_path(const std::filesystem::path &, const std::string &, const std::string &);
void atomic_json(const std::filesystem::path &, const Json &);
Json read_json(const std::filesystem::path &);
} // namespace cr
