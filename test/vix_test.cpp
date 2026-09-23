#include "core.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                   \
    } while (0)
using namespace cr;
int main(int argc, char **argv) {
    try {
        auto dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/fixtures");
        auto read = [&](const char *file) {
            std::ifstream input(dir / file);
            return Json::parse(input);
        };
        auto raw = read("vix-zero-1d.json");
        auto hourly_raw = read("vix-zero-1h.json");
        const Time start = 1790060400, clock = 1790108161;
        auto daily = parse_history(raw.dump(), "^VIX", "1d");
        CHECK(daily.bars.size() == 2 && daily.bars.back().time < start);
        CHECK(daily.meta["chartroomRecoveredTimes"].empty());
        auto targets = recovery_targets(daily, clock);
        CHECK(targets == std::set<Time>{start});
        auto hours = parse_history(hourly_raw.dump(), "^VIX", "1h");
        auto fixed = recover_vix(daily, hours, targets, clock);
        CHECK(fixed.bars.size() == 3 && fixed.bars.back().time == start);
        auto b = fixed.bars.back();
        CHECK(std::abs(b.open - 14.64) < 1e-5);
        CHECK(std::abs(b.high - 14.95) < 1e-5);
        CHECK(std::abs(b.low - 14.19) < 1e-5);
        CHECK(std::abs(b.close - 14.21) < 1e-5);
        CHECK(b.volume == 0); // Index volume is legitimately zero.
        CHECK(fixed.meta["chartroomHourlyRecoveredTimes"] == Json::array({start}));
        CHECK(fixed.meta["chartroomRecoveryError"] == "");
        CHECK(fixed.meta["chartroomIncompleteTimes"].empty());
        CHECK(encode_history(decode_history(encode_history(fixed))) == encode_history(fixed));
        CHECK(quote(fixed) && std::abs(quote(fixed)->change - (14.21 / 14.87 - 1) * 100) < 1e-4);
        // Old releases cached the quote-expanded zero placeholder; salvage the rest of the cache.
        auto legacy = daily;
        legacy.bars.push_back({start, 0, 14.21, 0, 14.21, 0});
        legacy.meta["chartroomRecoveredTimes"] = Json::array({start});
        legacy.meta.erase("chartroomInvalidPriceTimes");
        auto cleaned = decode_history(encode_history(legacy));
        CHECK(cleaned.bars.size() == 2);
        CHECK(recovery_targets(cleaned, clock) == targets);
        CHECK(merge_history(cleaned, fixed).bars.back().low == b.low);
        // Reject missing session calendars and holes, without manufacturing any prices.
        auto partial = hours;
        partial.bars.erase(partial.bars.begin() + 4);
        auto failed = recover_vix(daily, partial, targets, clock);
        CHECK(failed.bars.size() == 2 && !failed.meta["chartroomRecoveryError"].get<std::string>().empty());
        auto retained = merge_history(fixed, failed);
        CHECK(retained.bars.back().low == b.low && retained.bars.back().time == start);
        CHECK(retained.meta["chartroomHourlyRecoveredTimes"] == Json::array({start}));
        partial = hours;
        partial.meta.erase("tradingPeriods");
        CHECK(recover_vix(daily, partial, targets, clock).bars.size() == 2);
        partial = hours;
        partial.bars.erase(partial.bars.begin());
        CHECK(recover_vix(daily, partial, targets, clock).bars.size() == 2);
        // Live sessions require only the buckets through the provider's current quote time.
        partial = hours;
        partial.bars.resize(3);
        partial.meta["regularMarketTime"] = start + 2 * 3600 + 30;
        CHECK(recover_vix(daily, partial, targets, start + 2 * 3600 + 30).bars.size() == 3);
        // Corrected daily data supersedes hourly recovery and its provenance marker.
        auto &q = raw["chart"]["result"][0]["indicators"]["quote"][0];
        q["open"].back() = 14.65;
        q["high"].back() = 14.96;
        q["low"].back() = 14.18;
        q["close"].back() = 14.22;
        auto official = parse_history(raw.dump(), "^VIX", "1d");
        auto replaced = merge_history(fixed, official);
        CHECK(replaced.bars.back().open == 14.65 && replaced.bars.back().low == 14.18);
        CHECK(replaced.meta["chartroomHourlyRecoveredTimes"].empty());
        // A missing close and empty zero range cannot be synthesized from a quote for any symbol.
        q["open"].back() = q["high"].back() = q["low"].back() = 0;
        q["close"].back() = nullptr;
        CHECK(parse_history(raw.dump(), "SPY", "1d").bars.size() == 2);
        // Negative futures prices stay valid through both parsing and cache loading.
        q["open"].back() = -5;
        q["high"].back() = 1;
        q["low"].back() = -40;
        q["close"].back() = -30;
        auto future = parse_history(raw.dump(), "CL=F", "1d");
        CHECK(future.bars.size() == 3 && future.bars.back().low == -40);
        CHECK(decode_history(encode_history(future)).bars.back().close == -30);
        std::cout << "VIX placeholder rejection, legacy cache cleanup, hourly recovery and failure handling "
                     "passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
