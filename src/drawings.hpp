#pragma once
#include "core.hpp"
namespace cr {
struct Anchor {
    Time time{};
    double price{};
    bool operator==(const Anchor &) const = default;
};
struct FibLevel {
    double ratio{};
    uint32_t color = gold;
    bool enabled = true;
    bool operator==(const FibLevel &) const = default;
};
struct FibSettings {
    std::vector<FibLevel> levels = {{0, rgba(155, 165, 180)},    {.236, rgba(240, 108, 108)},
                                    {.382, rgba(120, 190, 110)}, {.5, rgba(60, 190, 160)},
                                    {.618, rgba(70, 165, 220)},  {.786, rgba(155, 125, 215)},
                                    {1, rgba(155, 165, 180)},    {1.272, gold, false},
                                    {1.618, blue, false},        {2.618, down, false}};
    bool reverse = false, extend_left = false, extend_right = false;
    bool prices = true, percentages = false, labels = true, labels_right = true;
    bool background = true, trend_line = true;
    double opacity = .08;
    bool operator==(const FibSettings &) const = default;
};
struct Drawing {
    uint64_t id{};
    std::string kind = "trend", text = "Note";
    Anchor a, b;
    uint32_t color = gold;
    bool locked = false;
    int timeframe = -1; // -1: all timeframes
    FibSettings fib;
    bool operator==(const Drawing &) const = default;
};
// Candle centers are integral indices. Interpolation uses actual timestamps inside
// history and the nominal interval outside it, without inventing exchange sessions.
double drawing_index(const std::vector<Bar> &, Time, Time step);
Time drawing_time(const std::vector<Bar> &, double index, Time step);
struct TimeTick {
    Time time{};
    double index{};
    std::string label;
    bool major = false;
};
// Calendar boundaries over the full visible range, including extrapolated space.
std::vector<TimeTick> time_grid(const std::vector<Bar> &, const View &, Time step, double pixels,
                                bool local = false);
struct Measurement {
    double change{}, percent{}, bars{};
    Time seconds{};
};
Measurement measure(const Anchor &, const Anchor &, const std::vector<Bar> &, Time step);
double fib_price(const Drawing &, double ratio);
Json encode_drawing(const Drawing &);
Drawing decode_drawing(const Json &);
class DrawingBook {
  public:
    const std::vector<Drawing> &for_symbol(const std::string &) const;
    const Drawing *find(const std::string &, uint64_t) const;
    uint64_t add(const std::string &, Drawing);
    bool replace(const std::string &, Drawing);
    bool erase(const std::string &, uint64_t);
    bool undo();
    bool redo();
    bool can_undo() const {
        return !past.empty();
    }
    bool can_redo() const {
        return !future.empty();
    }
    Json document() const;
    void restore(const Json &);

  private:
    struct Edit {
        std::string symbol;
        std::optional<Drawing> before, after;
        size_t position{};
    };
    std::map<std::string, std::vector<Drawing>> drawings;
    std::vector<Edit> past, future;
    uint64_t next_id = 1;
    void commit(Edit);
    void apply(const Edit &, bool forward);
};
enum class DrawTool { Select, Level, Trend, Ray, Rectangle, Text, Measure, Fibonacci };
// Ephemeral interaction state; only committed drawings enter the saved workspace.
struct DrawingInteraction {
    DrawTool tool = DrawTool::Select;
    uint64_t selected{};
    bool gesture = false, creating = false, measuring = false, measured = false, wait_release = false;
    bool awaiting_second = false, logarithmic = false;
    int handle = 0;
    Drawing draft, original, settings;
    Anchor start;
    double start_index{}, low{}, high{};
    View view;
};
} // namespace cr
