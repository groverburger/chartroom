#include "drawings.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                   \
    } while (0)
using namespace cr;
int main() {
    try {
        // Friday -> Monday -> Tuesday: closed sessions occupy no extra chart slots.
        Time friday = 1736467200, day = 86400;
        std::vector<Bar> bars = {{friday, 100, 110, 90, 105, 1000},
                                 {friday + 3 * day, 105, 115, 100, 110, 2000},
                                 {friday + 4 * day, 110, 120, 105, 115, 3000}};
        CHECK(drawing_index(bars, friday + 3 * day, day) == 1);
        CHECK(drawing_time(bars, 1, day) == friday + 3 * day);
        CHECK(drawing_time(bars, -2, day) == friday - 2 * day);
        CHECK(drawing_time(bars, 4, day) == friday + 6 * day);
        for (double i : {-2., 0., .25, .5, 1., 1.5, 2., 5.})
            CHECK(std::abs(drawing_index(bars, drawing_time(bars, i, day), day) - i) < 1e-8);
        Drawing d;
        d.a = {bars[0].time, 100};
        d.b = {bars[1].time, 110};
        auto m = measure(d.a, d.b, bars, day);
        CHECK(m.change == 10 && m.percent == 10 && m.bars == 1 && m.seconds == 3 * day);
        CHECK(measure(d.b, d.a, bars, day).seconds == -3 * day);
        CHECK(!std::isfinite(measure({friday, 0}, d.b, bars, day).percent));
        auto original = d;
        bars.insert(bars.begin(), {friday - day, 95, 105, 90, 100, 1000});
        CHECK(drawing_index(bars, d.a.time, day) == 1);
        CHECK(d == original); // Loading earlier history never rewrites anchors.
        auto hourly = bars;
        hourly.insert(hourly.begin() + 2, {friday + 3600, 100, 110, 90, 105, 100});
        CHECK(drawing_index(hourly, d.b.time, 3600) == 3);
        DrawingBook book;
        auto a = book.add("SPY", d);
        d.kind = "rectangle";
        auto b = book.add("SPY", d);
        d.kind = "ray";
        auto c = book.add("BTC-USD", d);
        CHECK(a != b && b != c);
        auto edit = *book.find("SPY", a);
        edit.a.price = 123.45;
        edit.locked = true;
        edit.timeframe = 1;
        CHECK(book.replace("SPY", edit));
        CHECK(book.undo());
        CHECK(book.find("SPY", a)->a.price == 100);
        CHECK(book.redo());
        CHECK(*book.find("SPY", a) == edit);
        CHECK(book.erase("SPY", a));
        CHECK(book.find("SPY", a) == nullptr);
        CHECK(book.undo());
        CHECK(book.for_symbol("SPY").front().id == a);
        CHECK(book.for_symbol("SPY")[1].id == b); // Undo preserves stacking order.
        CHECK(!book.replace("SPY", edit));
        CHECK(book.can_redo()); // No-op edits don't clear redo.
        edit.text = "Notes";
        CHECK(book.replace("SPY", edit));
        CHECK(!book.can_redo());
        auto document = book.document();
        DrawingBook restored;
        restored.restore(document);
        CHECK(restored.document() == document && !restored.can_undo() && !restored.can_redo());
        CHECK(restored.add("SPY", d) > c);
        for (auto kind : {"level", "trend", "ray", "rectangle", "text", "fib"}) {
            d.id = 1;
            d.kind = kind;
            d.text = "First line\nSecond line";
            CHECK(decode_drawing(encode_drawing(d)) == d);
        }
        // Retracement zero is the second anchor; one is the first, in either direction.
        d.kind = "fib";
        d.a.price = 100;
        d.b.price = 200;
        CHECK(fib_price(d, 0) == 200 && fib_price(d, 1) == 100);
        CHECK(std::abs(fib_price(d, .618) - 138.2) < 1e-10);
        CHECK(fib_price(d, -.5) == 250 && fib_price(d, 1.5) == 50);
        d.fib.reverse = true;
        CHECK(fib_price(d, 0) == 100 && fib_price(d, 1) == 200);
        CHECK(std::abs(fib_price(d, .618) - 161.8) < 1e-10);
        std::swap(d.a, d.b);
        d.fib.reverse = false;
        CHECK(std::abs(fib_price(d, .618) - 161.8) < 1e-10);
        d.fib.extend_right = true;
        d.fib.background = false;
        d.fib.percentages = true;
        d.fib.levels = {{0, blue}, {.618, gold}, {1.618, down, false}};
        CHECK(decode_drawing(encode_drawing(d)) == d);
        DrawingBook fib_book;
        auto fib_id = fib_book.add("SPY", d);
        auto revised = *fib_book.find("SPY", fib_id);
        revised.fib.reverse = true;
        fib_book.replace("SPY", revised);
        CHECK(fib_book.undo() && !fib_book.find("SPY", fib_id)->fib.reverse);
        CHECK(fib_book.redo() && fib_book.find("SPY", fib_id)->fib.reverse);
        for (auto bad_fib : {Json{{"levels", Json::array()}}, Json{{"opacity", 1.1}},
                             Json{{"levels", Json::array({{{"ratio", 101}}})}}}) {
            auto invalid = encode_drawing(d);
            invalid["fib"] = bad_fib;
            bool rejected = false;
            try {
                decode_drawing(invalid);
            } catch (...) {
                rejected = true;
            }
            CHECK(rejected);
        }
        auto bad = document;
        bad["SPY"].push_back(bad["SPY"][0]);
        bool rejected = false;
        try {
            restored.restore(bad);
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(restored.for_symbol("BTC-USD").size() == 1); // Failed load is transactional.
        auto invalid = encode_drawing(d);
        invalid["timeframe"] = 4;
        rejected = false;
        try {
            decode_drawing(invalid);
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
        DrawingBook bounded;
        for (int i = 0; i < 250; ++i)
            bounded.add("SPY", d);
        int undone = 0;
        while (bounded.undo())
            ++undone;
        CHECK(undone == 200 && bounded.for_symbol("SPY").size() == 50);
        while (bounded.redo()) {
        }
        CHECK(bounded.for_symbol("SPY").size() == 250);
        std::cout << "Drawing anchors, measurement, edit history, validation and serialization passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
