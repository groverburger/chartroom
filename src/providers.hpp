#pragma once
#include "market.hpp"
#include "net.hpp"
namespace cr {
class Providers {
  public:
    explicit Providers(Network &n) : net(n) {}
    using HistoryCallback = std::function<void(History, std::string)>;
    using QuoteCallback = std::function<void(Quote, std::string)>;
    using OptionsCallback = std::function<void(OptionChain, std::string)>;
    void history(std::string symbol, std::string interval, Time start, std::string previous_source,
                 HistoryCallback);
    void quote(std::string symbol, QuoteCallback);
    void options(std::string symbol, std::string expiry, OptionsCallback);
    void option_dates(std::string symbol, bool full, std::function<void(OptionDates, std::string)>);
    void screen(ScreenQuery, std::function<void(ScreenResult, std::string)>);

  private:
    Network &net;
    std::map<std::string, Time> retry;
    void yahoo(std::string symbol, std::string interval, Time start, std::string previous_source,
               HistoryCallback);
    void options_page(std::string url, int offset, std::shared_ptr<OptionChain>, OptionsCallback);
};
} // namespace cr
