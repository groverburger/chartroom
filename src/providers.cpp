#include "providers.hpp"
#include <cctype>
namespace cr {
void Providers::yahoo(std::string symbol, std::string interval, Time start, std::string previous_source,
                      HistoryCallback cb) {
    // A provider switch reloads its history: don't splice different price adjustments together.
    if (previous_source != "Yahoo")
        start = interval == "1h" ? now() - 729 * 86400 : 0;
    // A full daily or weekly load answers with two years first (~0.4 s) so the chart appears at once,
    // then backfills the complete history, which merges in behind it.
    bool backfill = start == 0;
    net.get(
        history_url(symbol, interval, backfill ? now() - 2 * 366 * 86400 : start, now()),
        [this, symbol, interval, backfill, cb](std::string body, std::string error) {
            try {
                if (!error.empty())
                    throw std::runtime_error(error);
                auto h = parse_history(body, symbol, interval);
                h.meta["chartroomSource"] = "Yahoo";
                cb(std::move(h), {});
            } catch (const std::exception &e) {
                auto problem = "Yahoo: " + std::string(e.what());
                if (interval != "1d" || !nasdaq_symbol(symbol))
                    cb({}, problem);
                else
                    nasdaq(symbol, problem, cb);
                return;
            }
            if (backfill)
                net.get(history_url(symbol, interval, 0, now()),
                        [symbol, interval, cb](std::string body, std::string error) {
                            // A failed backfill leaves the two years already shown.
                            try {
                                if (!error.empty())
                                    throw std::runtime_error(error);
                                auto h = parse_history(body, symbol, interval);
                                h.meta["chartroomSource"] = "Yahoo";
                                cb(std::move(h), {});
                            } catch (...) {
                            }
                        });
        },
        true);
}
// Fallbacks when Yahoo fails for a US stock or ETF: Nasdaq's daily chart, then Stooq end-of-day.
void Providers::nasdaq(std::string symbol, std::string problem, HistoryCallback cb) {
    auto url = nasdaq_url(symbol, "chart") + "&fromdate=1990-01-01&todate=" + date(now(), "%Y-%m-%d");
    net.get(
        url,
        [this, symbol, problem, cb](std::string body, std::string error) {
            try {
                if (!error.empty())
                    throw std::runtime_error(error);
                auto h = nasdaq_history(body, symbol);
                h.meta["chartroomProviderNotice"] = problem + "; showing Nasdaq completed sessions.";
                cb(std::move(h), {});
            } catch (const std::exception &e) {
                auto failures = problem + "; Nasdaq: " + e.what();
                std::string code = symbol;
                std::transform(code.begin(), code.end(), code.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                net.get(
                    "https://stooq.com/q/d/l/?s=" + url_encode(code + ".us") + "&i=d",
                    [symbol, cb, failures](std::string body, std::string error) {
                        try {
                            if (!error.empty())
                                throw std::runtime_error(error);
                            auto h = stooq_history(body, symbol);
                            h.meta["chartroomProviderNotice"] =
                                "Yahoo and Nasdaq unavailable; Stooq end-of-day history.";
                            cb(std::move(h), {});
                        } catch (const std::exception &e) {
                            cb({}, failures + "; Stooq: " + e.what());
                        }
                    },
                    true);
            }
        },
        true);
}
void Providers::history(std::string symbol, std::string interval, Time start, std::string previous_source,
                        HistoryCallback cb) {
    if (symbol.ends_with("USDT")) {
        // USDT is an explicit exchange pair, never silently substituted for a USD composite.
        auto i = interval == "1wk" ? "1w" : interval;
        auto url = "https://api.binance.com/api/v3/klines?symbol=" + url_encode(symbol) + "&interval=" + i +
                   "&limit=1000";
        net.get(
            url,
            [symbol, interval, cb](std::string body, std::string error) {
                try {
                    if (!error.empty())
                        throw std::runtime_error(error);
                    cb(binance_history(body, symbol, interval), {});
                } catch (const std::exception &e) {
                    cb({}, "Binance: " + std::string(e.what()) +
                               ". USD composite symbols such as BTC use Yahoo.");
                }
            },
            true);
        return;
    }
    yahoo(symbol, interval, start, previous_source, cb);
}
void Providers::quote(std::string symbol, QuoteCallback cb) {
    if (symbol.ends_with("USDT")) {
        net.get("https://api.binance.com/api/v3/ticker/24hr?symbol=" + url_encode(symbol),
                [cb](std::string body, std::string error) {
                    try {
                        if (!error.empty())
                            throw std::runtime_error(error);
                        cb(binance_quote(body), {});
                    } catch (const std::exception &e) {
                        cb({}, "Binance: " + std::string(e.what()));
                    }
                });
        return;
    }
    auto fallback = [this, symbol, cb] {
        auto url = history_url(symbol, "1m", now() - 300, now());
        url.replace(url.find("includePrePost=false"), std::string("includePrePost=false").size(),
                    "includePrePost=true");
        net.get(url, [symbol, cb](std::string body, std::string error) {
            try {
                if (!error.empty())
                    throw std::runtime_error(error);
                cb(parse_quote(body, symbol), {});
            } catch (const std::exception &e) {
                cb({}, "Yahoo: " + std::string(e.what()));
            }
        });
    };
    if (!nasdaq_symbol(symbol) || retry["quote:" + symbol] > now()) {
        fallback();
        return;
    }
    net.get(nasdaq_url(symbol, "info"), [this, symbol, cb, fallback](std::string body, std::string error) {
        try {
            if (!error.empty())
                throw std::runtime_error(error);
            cb(nasdaq_quote(body, symbol), {});
        } catch (const std::exception &) {
            retry["quote:" + symbol] = now() + 15 * 60;
            fallback();
        }
    });
}
void Providers::option_dates(std::string symbol, bool full,
                             std::function<void(OptionDates, std::string)> cb) {
    if (!nasdaq_symbol(symbol)) {
        cb({}, "Options are available for supported US stocks and ETFs.");
        return;
    }
    if (!full) {
        // The first row is an expiry header. No contract-chain download is needed to start.
        net.get(nasdaq_url(symbol, "option-chain") + "&limit=1&fromdate=all&money=all",
                [cb](std::string body, std::string error) {
                    try {
                        if (!error.empty())
                            throw std::runtime_error(error);
                        cb(parse_option_dates(body), {});
                    } catch (const std::exception &e) {
                        cb({}, e.what());
                    }
                });
        return;
    }
    // Nasdaq exposes month ranges, not a complete dates-only endpoint. Discover the remaining
    // exact dates from near-money rows only when the expiry picker opens; cache the dates daily.
    auto url = nasdaq_url(symbol, "option-chain") +
               "&limit=2000&fromdate=all&money=at&type=all&excode=oprac&callput=callput";
    options_page(url, 0, std::make_shared<OptionChain>(), [cb](OptionChain chain, std::string error) {
        if (!error.empty()) {
            cb({}, error);
            return;
        }
        std::set<std::string> dates;
        for (auto &r : chain.rows)
            dates.insert(r.expiry);
        cb({{dates.begin(), dates.end()}, now(), !chain.truncated},
           chain.truncated ? "Some expiry dates could not be loaded." : "");
    });
}
void Providers::options(std::string symbol, std::string expiry, OptionsCallback cb) {
    if (!nasdaq_symbol(symbol)) {
        cb({}, "Options are available for supported US stocks and ETFs.");
        return;
    }
    if (!valid_expiry(expiry)) {
        cb({}, "Select a valid expiry date.");
        return;
    }
    auto url = nasdaq_url(symbol, "option-chain") + "&limit=2000&fromdate=" + expiry + "&todate=" + expiry +
               "&money=all&type=all&excode=oprac&callput=callput";
    options_page(url, 0, std::make_shared<OptionChain>(), [expiry, cb](OptionChain chain, std::string error) {
        if (error.empty() && std::any_of(chain.rows.begin(), chain.rows.end(),
                                         [&](const OptionRow &r) { return r.expiry != expiry; }))
            error = "Nasdaq returned contracts for a different expiry.";
        cb(std::move(chain), std::move(error));
    });
}
void Providers::options_page(std::string url, int offset, std::shared_ptr<OptionChain> chain,
                             OptionsCallback cb) {
    net.get(url + "&offset=" + std::to_string(offset),
            [this, url, offset, chain, cb](std::string body, std::string error) {
                try {
                    if (!error.empty())
                        throw std::runtime_error(error);
                    auto page = parse_options(body);
                    int received = int(Json::parse(body).at("data").at("table").at("rows").size());
                    merge_options(*chain, std::move(page));
                    int next = offset + received;
                    if (next < chain->total && received > 0 && next < 20000) {
                        options_page(url, next, chain, cb);
                        return;
                    }
                    chain->truncated = next < chain->total;
                    if (chain->rows.empty())
                        throw std::runtime_error("No listed options in this date range");
                    cb(std::move(*chain), {});
                } catch (const std::exception &e) {
                    cb({}, "Nasdaq options: " + std::string(e.what()));
                }
            });
}
void Providers::screen(ScreenQuery q, std::function<void(ScreenResult, std::string)> cb) {
    net.post("https://scanner.tradingview.com/america/scan", scanner_request(q).dump(),
             [cb](std::string body, std::string error) {
                 try {
                     if (!error.empty())
                         throw std::runtime_error(error);
                     cb(parse_screen(body), {});
                 } catch (const std::exception &e) {
                     cb({}, "TradingView scanner: " + std::string(e.what()));
                 }
             });
}
void Providers::fundamentals(std::string symbol, std::function<void(Json, std::string)> cb) {
    struct Pending {
        Json parts = Json::object();
        int remaining = 4;
        std::string error;
    };
    auto pending = std::make_shared<Pending>();
    const std::pair<std::string, std::string> urls[] = {
        {"summary", nasdaq_url(symbol, "summary")},
        {"profile", "https://api.nasdaq.com/api/company/" + symbol + "/company-profile"},
        {"earnings", "https://api.nasdaq.com/api/company/" + symbol + "/earnings-surprise"},
        {"calendar", "https://api.nasdaq.com/api/analyst/" + symbol + "/earnings-date"}};
    for (auto [part, url] : urls)
        net.get(url, [pending, part, symbol, cb](std::string body, std::string error) {
            try {
                if (!error.empty())
                    throw std::runtime_error(error);
                auto parsed = Json::parse(body);
                if (!parsed.contains("data") || !parsed["data"].is_object())
                    throw std::runtime_error("unavailable for this symbol");
                // Validate identity/schema before accepting any component into its cache.
                parse_fundamentals(Json{{part, parsed}}, symbol);
                pending->parts[part] = std::move(parsed);
            } catch (const std::exception &e) {
                if (!pending->error.empty())
                    pending->error += "; ";
                pending->error += part + ": " + e.what();
            }
            if (--pending->remaining == 0)
                cb(std::move(pending->parts), pending->error);
        });
}
} // namespace cr
