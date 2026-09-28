#include "drawings.hpp"
#include <chrono>
#include <stdexcept>
namespace cr {
double drawing_index(const std::vector<Bar> &bars, Time t, Time step) {
    if (bars.empty())
        return 0;
    step = std::max<Time>(1, step);
    if (t <= bars.front().time)
        return double(t - bars.front().time) / step;
    if (t >= bars.back().time)
        return double(bars.size() - 1) + double(t - bars.back().time) / step;
    auto it = std::upper_bound(bars.begin(), bars.end(), t,
                               [](Time value, const Bar &b) { return value < b.time; });
    auto prev = it - 1;
    return double(prev - bars.begin()) + double(t - prev->time) / double(it->time - prev->time);
}
Time drawing_time(const std::vector<Bar> &bars, double index, Time step) {
    if (bars.empty())
        return 0;
    step = std::max<Time>(1, step);
    if (index <= 0)
        return bars.front().time + Time(std::llround(index * step));
    if (index >= double(bars.size() - 1))
        return bars.back().time + Time(std::llround((index - double(bars.size() - 1)) * step));
    size_t i = size_t(std::floor(index));
    return bars[i].time + Time(std::llround((index - double(i)) * double(bars[i + 1].time - bars[i].time)));
}
std::vector<TimeTick> time_grid(const std::vector<Bar> &bars, const View &view, Time step, double pixels,
                                bool local) {
    using namespace std::chrono;
    if (bars.empty() || view.count <= 0 || pixels < 1 || !std::isfinite(view.first) ||
        !std::isfinite(view.count) || std::abs(view.first) > 1e8 || view.count > 1e8)
        return {};
    auto start = drawing_time(bars, view.first - .5, step);
    auto end = drawing_time(bars, view.first + view.count - .5, step);
    if (local) {
        start = local_wall(start);
        end = local_wall(end);
    }
    // Stay within the calendar and timestamp ranges supported by all platforms.
    if (start < -11676096000LL || end > 253402214400LL)
        return {};
    struct Interval {
        int unit, count;
    }; // hours, days, weeks, months, years
    const Interval choices[] = {{0, 1},  {0, 3},  {0, 6},  {0, 12},  {1, 1},   {2, 1},
                                {3, 1},  {3, 3},  {3, 6},  {4, 1},   {4, 2},   {4, 5},
                                {4, 10}, {4, 20}, {4, 50}, {4, 100}, {4, 500}, {4, 1000}};
    for (auto choice : choices) {
        if (choice.unit == 0 && step >= 86400)
            continue;
        auto day = floor<days>(sys_seconds{seconds{start}});
        year_month_day ymd{day};
        sys_seconds tick;
        if (choice.unit == 0) {
            auto hours_since = duration_cast<hours>(sys_seconds{seconds{start}} - sys_seconds{day}).count();
            tick = sys_seconds{day} + hours{hours_since / choice.count * choice.count};
        } else if (choice.unit == 1) {
            tick = day;
        } else if (choice.unit == 2) {
            tick = day - days{weekday{day}.iso_encoding() - 1}; // Monday
        } else if (choice.unit == 3) {
            tick = sys_days{ymd.year() /
                            month{(unsigned(ymd.month()) - 1) / choice.count * choice.count + 1} / 1};
        } else {
            tick = sys_days{year{int(ymd.year()) / choice.count * choice.count} / January / 1};
        }
        auto advance = [&] {
            if (choice.unit == 0)
                tick += hours{choice.count};
            else if (choice.unit == 1)
                tick += days{choice.count};
            else if (choice.unit == 2)
                tick += days{7 * choice.count};
            else {
                year_month_day d{floor<days>(tick)};
                if (choice.unit == 3)
                    d += months{choice.count};
                else
                    d += years{choice.count};
                tick = sys_days{d};
            }
        };
        while (tick.time_since_epoch().count() < start)
            advance();
        std::vector<TimeTick> ticks;
        bool crowded = false;
        double last_pixel = -1e9;
        size_t budget = size_t(std::max(2., pixels / 80 + 2));
        // Bound work even at extreme zoom; coarser intervals are tried next.
        for (int attempts = 0; tick.time_since_epoch().count() <= end; advance()) {
            if (++attempts > 2000) {
                crowded = true;
                break;
            }
            Time wall = tick.time_since_epoch().count();
            Time t = local ? local_instant(wall) : wall;
            if (local && local_wall(t) != wall)
                continue; // Spring-forward clock gap.
            if (!ticks.empty() && t <= ticks.back().time)
                continue;
            double index = drawing_index(bars, t, step);
            // A session/calendar boundary belongs to the first available bar after it.
            // Missing sessions never produce multiple ticks at that bar.
            if (t >= bars.front().time && t <= bars.back().time) {
                auto it = std::lower_bound(bars.begin(), bars.end(), t,
                                           [](const Bar &b, Time value) { return b.time < value; });
                index = double(it - bars.begin());
            }
            double pixel = (index - view.first + .5) / view.count * pixels;
            if (pixel < 0 || pixel > pixels || (!ticks.empty() && index == ticks.back().index))
                continue;
            year_month_day d{floor<days>(tick)};
            bool year_start = d.month() == January && d.day() == std::chrono::day{1};
            bool midnight = tick == floor<days>(tick);
            const char *format = choice.unit == 4 || (year_start && midnight) ? "%Y"
                                 : choice.unit == 3                           ? "%B"
                                 : choice.unit == 0 && !midnight              ? "%H:%M"
                                                                              : "%b %d";
            bool major = (year_start && midnight) || (d.day() == std::chrono::day{1} && midnight);
            ticks.push_back({t, index, date(wall, format), major});
            if (pixel - last_pixel < 90 || ticks.size() > budget) {
                crowded = true;
                break;
            }
            last_pixel = pixel;
        }
        if (!crowded)
            return ticks;
    }
    return {};
}
Measurement measure(const Anchor &a, const Anchor &b, const std::vector<Bar> &bars, Time step) {
    return {b.price - a.price, a.price == 0 ? missing : (b.price - a.price) / std::abs(a.price) * 100,
            drawing_index(bars, b.time, step) - drawing_index(bars, a.time, step), b.time - a.time};
}
double fib_price(const Drawing &d, double ratio) {
    double zero = d.fib.reverse ? d.a.price : d.b.price;
    double one = d.fib.reverse ? d.b.price : d.a.price;
    return zero + (one - zero) * ratio;
}
Json encode_drawing(const Drawing &d) {
    Json result = {
        {"id", d.id},       {"kind", d.kind}, {"a", {d.a.time, d.a.price}}, {"b", {d.b.time, d.b.price}},
        {"color", d.color}, {"text", d.text}, {"locked", d.locked},         {"timeframe", d.timeframe}};
    if (d.kind == "fib") {
        Json levels = Json::array();
        for (auto &l : d.fib.levels)
            levels.push_back({{"ratio", l.ratio}, {"color", l.color}, {"enabled", l.enabled}});
        auto &f = d.fib;
        result["fib"] = {{"levels", levels},
                         {"reverse", f.reverse},
                         {"extend_left", f.extend_left},
                         {"extend_right", f.extend_right},
                         {"prices", f.prices},
                         {"percentages", f.percentages},
                         {"labels", f.labels},
                         {"labels_right", f.labels_right},
                         {"background", f.background},
                         {"trend_line", f.trend_line},
                         {"opacity", f.opacity}};
    }
    return result;
}
Drawing decode_drawing(const Json &j) {
    Drawing d;
    d.id = j.at("id").get<uint64_t>();
    d.kind = j.at("kind");
    d.a = {j.at("a").at(0).get<Time>(), j.at("a").at(1).get<double>()};
    d.b = {j.at("b").at(0).get<Time>(), j.at("b").at(1).get<double>()};
    d.color = j.value("color", gold);
    d.text = j.value("text", std::string("Note"));
    d.locked = j.value("locked", false);
    d.timeframe = j.value("timeframe", -1);
    if (d.kind == "fib" && j.contains("fib")) {
        auto &f = j.at("fib");
        d.fib.reverse = f.value("reverse", false);
        d.fib.extend_left = f.value("extend_left", false);
        d.fib.extend_right = f.value("extend_right", false);
        d.fib.prices = f.value("prices", true);
        d.fib.percentages = f.value("percentages", false);
        d.fib.labels = f.value("labels", true);
        d.fib.labels_right = f.value("labels_right", true);
        d.fib.background = f.value("background", true);
        d.fib.trend_line = f.value("trend_line", true);
        d.fib.opacity = f.value("opacity", .08);
        if (!std::isfinite(d.fib.opacity) || d.fib.opacity < 0 || d.fib.opacity > 1)
            throw std::runtime_error("Invalid Fibonacci opacity");
        if (f.contains("levels")) {
            if (!f["levels"].is_array() || f["levels"].empty() || f["levels"].size() > 24)
                throw std::runtime_error("Use 1-24 Fibonacci levels");
            d.fib.levels.clear();
            for (auto &l : f["levels"]) {
                FibLevel level{l.at("ratio").get<double>(), l.value("color", gold), l.value("enabled", true)};
                if (!std::isfinite(level.ratio) || std::abs(level.ratio) > 100)
                    throw std::runtime_error("Invalid Fibonacci ratio");
                d.fib.levels.push_back(level);
            }
        }
    }
    if (!d.id || d.id >= uint64_t(INT64_MAX) ||
        (d.kind != "level" && d.kind != "trend" && d.kind != "ray" && d.kind != "rectangle" &&
         d.kind != "text" && d.kind != "fib") ||
        !std::isfinite(d.a.price) || !std::isfinite(d.b.price) || std::abs(double(d.a.time)) > 1e12 ||
        std::abs(double(d.b.time)) > 1e12 || d.text.size() > 1024 || d.timeframe < -1 || d.timeframe > 3)
        throw std::runtime_error("Invalid drawing");
    return d;
}
const std::vector<Drawing> &DrawingBook::for_symbol(const std::string &symbol) const {
    static const std::vector<Drawing> empty;
    auto it = drawings.find(symbol);
    return it == drawings.end() ? empty : it->second;
}
const Drawing *DrawingBook::find(const std::string &symbol, uint64_t id) const {
    for (auto &d : for_symbol(symbol))
        if (d.id == id)
            return &d;
    return nullptr;
}
void DrawingBook::apply(const Edit &e, bool forward) {
    auto &list = drawings[e.symbol];
    auto value = forward ? e.after : e.before;
    uint64_t id = e.after ? e.after->id : e.before->id;
    auto it = std::find_if(list.begin(), list.end(), [&](const Drawing &d) { return d.id == id; });
    if (value) {
        if (it != list.end())
            *it = *value;
        else
            list.insert(list.begin() + std::min(e.position, list.size()), *value);
    } else if (it != list.end())
        list.erase(it);
    if (list.empty())
        drawings.erase(e.symbol);
}
void DrawingBook::commit(Edit edit) {
    apply(edit, true);
    past.push_back(std::move(edit));
    if (past.size() > 200)
        past.erase(past.begin());
    future.clear();
}
uint64_t DrawingBook::add(const std::string &symbol, Drawing d) {
    d.id = next_id++;
    d = decode_drawing(encode_drawing(d));
    commit({symbol, std::nullopt, d, for_symbol(symbol).size()});
    return d.id;
}
bool DrawingBook::replace(const std::string &symbol, Drawing d) {
    auto old = find(symbol, d.id);
    if (!old || *old == d)
        return false;
    d = decode_drawing(encode_drawing(d));
    commit({symbol, *old, d, 0});
    return true;
}
bool DrawingBook::erase(const std::string &symbol, uint64_t id) {
    auto &list = for_symbol(symbol);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) {
            commit({symbol, list[i], std::nullopt, i});
            return true;
        }
    return false;
}
bool DrawingBook::undo() {
    if (past.empty())
        return false;
    auto e = past.back();
    past.pop_back();
    apply(e, false);
    future.push_back(std::move(e));
    return true;
}
bool DrawingBook::redo() {
    if (future.empty())
        return false;
    auto e = future.back();
    future.pop_back();
    apply(e, true);
    past.push_back(std::move(e));
    return true;
}
Json DrawingBook::document() const {
    Json result = Json::object();
    for (auto &[symbol, list] : drawings) {
        auto &items = result[symbol] = Json::array();
        for (auto &d : list)
            items.push_back(encode_drawing(d));
    }
    return result;
}
void DrawingBook::restore(const Json &j) {
    if (!j.is_object())
        throw std::runtime_error("Invalid drawings");
    DrawingBook loaded;
    std::set<uint64_t> ids;
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto symbol = normalize_symbol(it.key());
        if (!it.value().is_array())
            throw std::runtime_error("Invalid symbol drawings");
        for (auto &item : it.value()) {
            auto d = decode_drawing(item);
            if (!ids.insert(d.id).second)
                throw std::runtime_error("Duplicate drawing ID");
            loaded.next_id = std::max(loaded.next_id, d.id + 1);
            loaded.drawings[symbol].push_back(std::move(d));
        }
    }
    *this = std::move(loaded);
}
} // namespace cr
