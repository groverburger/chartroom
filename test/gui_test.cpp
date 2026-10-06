#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <functional>
#include <iostream>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
    } while (0)
int main() {
    using namespace cr;
    auto dir =
        std::filesystem::temp_directory_path() /
        ("chartroom-gui-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    GLFWwindow *window = nullptr;
    try {
        for (auto symbol : {"SPY", "BTC-USD"}) {
            History h;
            h.symbol = symbol;
            h.interval = "1d";
            h.fetched = now();
            for (int i = 0; i < 400; ++i)
                h.bars.push_back(
                    {1736121600 + i * 86400, 100 + i * .1, 104 + i * .1, 99 + i * .1, 102 + i * .1, 1000.});
            atomic_json(cache_path(dir, symbol, "1d"), encode_history(h));
        }
        CHECK(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        const char *glsl = "#version 150";
#else
        const char *glsl = "#version 130";
#endif
        window = glfwCreateWindow(1440, 900, "Chartroom input tests", nullptr, nullptr);
        CHECK(window);
        glfwMakeContextCurrent(window);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr;
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init(glsl);
        theme();
        State state(dir, true);
        auto &a = state.current();
        a.indicators = {indicator("RIBBON"), indicator("RSI")};
        a.ohlc = true;
        a.indicators[0].background = false;
        a.indicators[0].ma_colors[0] = rgba(12, 34, 56);
        auto &b = state.add(resolve_symbol("BTC"));
        b.indicators = {indicator("BB"), indicator("MACD")};
        auto frame = [&](
                         std::function<void()> input = []() {}, std::function<void()> extra = []() {}) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            io.DeltaTime = 1.f / 60.f;
            input();
            ImGui::NewFrame();
            cr::frame(state);
            extra();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        };
        for (int i = 0; i < 4; ++i)
            frame();
        auto *aw = ImGui::FindWindowByName("###chart_1");
        auto *bw = ImGui::FindWindowByName("###chart_2");
        CHECK(aw && bw);
        CHECK(aw->Pos.x + aw->Size.x <= bw->Pos.x + 2);
        double first = a.view.first, count = a.view.count, other = b.view.first;
        float mx = aw->Pos.x + 200, my = aw->Pos.y + 200;
        auto mouse = [&]() { io.AddMousePosEvent(mx, my); };
        frame(mouse);
        frame(mouse);
        frame([&]() {
            mouse();
            io.AddMouseWheelEvent(.15f, 2.f);
        });
        frame(mouse);
        CHECK(a.view.count < count);
        CHECK(b.view.first == other);
        float left = aw->Pos.x + aw->WindowPadding.x + 16,
              right = aw->Pos.x + aw->Size.x - aw->WindowPadding.x - 102;
        double anchor = (mx - left) / (right - left);
        CHECK(std::abs((a.view.first + anchor * a.view.count) - (first + anchor * count)) < 1e-3);
        auto [lo, hi] = price_limits(a.bars, a.view, a.results);
        auto [begin, end] = a.view.visible(a.bars.size());
        for (int i = begin; i < end; ++i)
            CHECK(lo <= a.bars[i].low && a.bars[i].high <= hi);
        // Check the actual vertical crosshair geometry, not just the hover index.
        int hovered_index = int(std::floor(a.view.first + anchor * a.view.count));
        float snapped = left + float((hovered_index - a.view.first + .5) / a.view.count) * (right - left);
        auto &vertices = aw->DrawList->VtxBuffer;
        float min_x = 1e9f, max_x = -1e9f;
        for (const auto &v : vertices) {
            if (v.col == rgba(140, 160, 182, 150) && std::abs(v.pos.x - snapped) < 3 &&
                v.pos.y > aw->Pos.y + 100 && v.pos.y < aw->Pos.y + 135) {
                min_x = std::min(min_x, v.pos.x);
                max_x = std::max(max_x, v.pos.x);
            }
        }
        // ImGui offsets lines by half a pixel and emits antialiased edge vertices.
        CHECK(min_x < max_x);
        CHECK(std::abs((min_x + max_x) / 2 - .5f - snapped) < 1e-3f);
        // Feed the native GLFW scroll callback: horizontal swipes pan despite vertical noise.
        for (int i = 0; i < 16; ++i)
            frame(mouse);
        auto pre_swipe = a.view;
        frame([&] {
            mouse();
            ImGui_ImplGlfw_ScrollCallback(window, 2, .15);
        });
        CHECK(a.view.first < pre_swipe.first && a.view.count == pre_swipe.count);
        frame([&] {
            mouse();
            ImGui_ImplGlfw_ScrollCallback(window, .01, .04);
        });
        CHECK(a.view.count == pre_swipe.count);
        for (int i = 0; i < 16; ++i)
            frame(mouse);
        double before_reverse = a.view.first;
        frame([&] {
            mouse();
            ImGui_ImplGlfw_ScrollCallback(window, -2, -.1);
        });
        CHECK(a.view.first > before_reverse && a.view.count == pre_swipe.count);
        CHECK(b.view.first == other);
        a.view = pre_swipe;
        // Exercise drawing gestures through real ImGui input, including navigation conflicts.
        io.MouseDoubleClickTime = .01f;
        state.active = a.id;
        auto move = [&](float x, float y) { frame([&] { io.AddMousePosEvent(x, y); }); };
        auto press = [&](float x, float y) {
            move(x, y);
            frame([&] {
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, true);
            });
        };
        auto release = [&] {
            frame([&] { io.AddMouseButtonEvent(0, false); });
            frame();
        };
        auto key = [&](ImGuiKey k) {
            frame([&] { io.AddKeyEvent(k, true); });
            frame([&] { io.AddKeyEvent(k, false); });
        };
        float x1 = left + 55, x2 = right - 50, y1 = my + 40, y2 = my + 130;
        auto unchanged = a.view;
        a.drawing.tool = DrawTool::Trend;
        press(x1, y1);
        move(x2, y2);
        release();
        CHECK(state.drawings.for_symbol("SPY").size() == 1);
        CHECK(state.drawings.for_symbol("BTC-USD").empty());
        CHECK(a.view.first == unchanged.first && a.view.count == unchanged.count);
        auto trend = state.drawings.for_symbol("SPY")[0];
        CHECK(trend.kind == "trend" && trend.a.time < trend.b.time && trend.a.price > trend.b.price);
        // Undo and redo use the platform-independent Ctrl shortcut (Cmd is also supported).
        frame([&] { io.AddKeyEvent(ImGuiMod_Ctrl, true); });
        key(ImGuiKey_Z);
        CHECK(state.drawings.for_symbol("SPY").empty());
        frame([&] { io.AddKeyEvent(ImGuiMod_Shift, true); });
        key(ImGuiKey_Z);
        CHECK(state.drawings.for_symbol("SPY").size() == 1);
        frame([&] {
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
        });
        // Drag one endpoint and verify one undo restores the whole gesture.
        a.drawing.selected = trend.id;
        float snapped_x1 =
            left + float((drawing_index(a.bars, trend.a.time, 86400) - a.view.first + .5) / a.view.count) *
                       (right - left);
        press(snapped_x1 + 2, y1 + 2);
        release();
        CHECK(*state.drawings.find("SPY", trend.id) == trend); // Selecting a handle must not move it.
        press(snapped_x1, y1);
        move(snapped_x1 + 20, y1 - 25);
        release();
        CHECK(*state.drawings.find("SPY", trend.id) != trend);
        CHECK(state.drawings.undo());
        CHECK(*state.drawings.find("SPY", trend.id) == trend);
        // A locked object can be selected but neither dragged nor deleted.
        auto locked = trend;
        locked.locked = true;
        state.drawings.replace("SPY", locked);
        press(snapped_x1, y1);
        move(snapped_x1 + 30, y1 - 30);
        release();
        CHECK(*state.drawings.find("SPY", trend.id) == locked);
        key(ImGuiKey_Delete);
        CHECK(state.drawings.find("SPY", trend.id));
        state.drawings.replace("SPY", trend);
        // Escape cancels an in-flight creation without panning or changing the book.
        a.drawing.tool = DrawTool::Rectangle;
        press(x1, y2 + 20);
        move(x2, y1);
        key(ImGuiKey_Escape);
        move(x2 - 10, y1);
        release();
        CHECK(state.drawings.for_symbol("SPY").size() == 1);
        CHECK(a.view.first == unchanged.first);
        // Measurement has no saved object or undo entry and doesn't pan the chart.
        auto before_measure = state.drawings.document();
        frame([&] { io.AddKeyEvent(ImGuiMod_Shift, true); });
        press(x1, y1 + 15);
        move(x2, y2 + 15);
        release();
        frame([&] { io.AddKeyEvent(ImGuiMod_Shift, false); });
        CHECK(a.drawing.measured);
        CHECK(state.drawings.document() == before_measure);
        CHECK(a.view.first == unchanged.first);
        auto measured = measure(a.drawing.draft.a, a.drawing.draft.b, a.bars, 86400);
        CHECK(measured.bars > 0 && measured.change < 0 && measured.seconds > 0);
        key(ImGuiKey_Escape);
        CHECK(!a.drawing.measured);
        // Every tool produces the requested geometry; levels are single-click tools.
        for (auto tool : {DrawTool::Level, DrawTool::Ray, DrawTool::Rectangle}) {
            a.drawing.tool = tool;
            press(x1, y2 + 30);
            move(x2, y1 + 10);
            release();
        }
        CHECK(state.drawings.for_symbol("SPY").size() == 4);
        CHECK(state.drawings.for_symbol("SPY")[1].kind == "level");
        CHECK(state.drawings.for_symbol("SPY")[2].kind == "ray");
        CHECK(state.drawings.for_symbol("SPY")[3].kind == "rectangle");
        // Fibonacci accepts two clicks or a drag. A pending second click stays ephemeral.
        a.drawing.tool = DrawTool::Fibonacci;
        press(x1, y2 + 30);
        release();
        CHECK(a.drawing.awaiting_second && a.drawing.gesture);
        CHECK(state.drawings.for_symbol("SPY").size() == 4);
        move(x2, y1);
        press(x2, y1);
        release();
        CHECK(state.drawings.for_symbol("SPY").size() == 5);
        auto fib = state.drawings.for_symbol("SPY").back();
        CHECK(fib.kind == "fib" && fib.a.price < fib.b.price);
        CHECK(!a.drawing.gesture && a.drawing.tool == DrawTool::Select);
        CHECK(a.view.first == unchanged.first && a.view.count == unchanged.count);
        CHECK(state.drawings.undo() && state.drawings.redo());
        a.drawing.tool = DrawTool::Fibonacci;
        press(x1, y1);
        move(x2, y2);
        release();
        CHECK(state.drawings.for_symbol("SPY").size() == 6);
        CHECK(state.drawings.for_symbol("SPY").back().a.price >
              state.drawings.for_symbol("SPY").back().b.price);
        a.drawing.tool = DrawTool::Fibonacci;
        press(x1, y1);
        release();
        key(ImGuiKey_Escape);
        CHECK(!a.drawing.gesture && state.drawings.for_symbol("SPY").size() == 6);
        // Drawing anchors use the logarithmic inverse; dragging a body preserves its price ratio.
        a.logarithmic = true;
        frame(mouse);
        a.drawing.tool = DrawTool::Trend;
        press(x1, y2);
        move(x2, y1);
        release();
        auto log_trend = state.drawings.for_symbol("SPY").back();
        CHECK(log_trend.kind == "trend" && log_trend.b.price > log_trend.a.price);
        float center_x = (x1 + x2) / 2, center_y = (y1 + y2) / 2;
        press(center_x, center_y);
        move(center_x + 20, center_y - 20);
        release();
        auto log_moved = *state.drawings.find("SPY", log_trend.id);
        CHECK(log_moved.a.price > log_trend.a.price);
        CHECK(std::abs(log_moved.b.price / log_moved.a.price - log_trend.b.price / log_trend.a.price) <
              1e-10);
        for (const auto &vertex : aw->DrawList->VtxBuffer)
            CHECK(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
        CHECK(state.drawings.undo());
        a.logarithmic = false;
        frame(mouse);
        auto drawing_doc = state.drawings.document();
        // Switching intervals cancels ephemeral interactions but retains symbol drawings.
        state.select(a, "SPY", 3);
        CHECK(state.drawings.document() == drawing_doc);
        state.select(a, "SPY", 2);
        state.tick();
        a.view = unchanged;
        for (int i = 0; i < 3; ++i)
            frame(mouse);
        auto popup_title = "SPY 1D###chart_1";
        frame(mouse, [&]() {
            ImGui::Begin(popup_title);
            ImGui::OpenPopup("Indicators");
            ImGui::End();
        });
        frame(mouse);
        frame(mouse, [&]() {
            ImGui::Begin(popup_title);
            if (ImGui::BeginPopup("Indicators")) {
                ImGui::PushID(0);
                ImGui::GetStateStorage()->SetBool(ImGui::GetID("###settings"), true);
                ImGui::PushID("###settings");
                ImGui::GetStateStorage()->SetBool(ImGui::GetID("Moving average colors"), true);
                ImGui::PopID();
                ImGui::PopID();
                ImGui::EndPopup();
            }
            ImGui::End();
        });
        float width = 0;
        for (int i = 0; i < 100; ++i) {
            frame([&]() { io.AddMousePosEvent(mx + i % 20, my + i % 13); },
                  [&]() {
                      ImGui::Begin(popup_title);
                      auto id = ImGui::GetID("Indicators");
                      for (auto &pop : ImGui::GetCurrentContext()->OpenPopupStack)
                          if (pop.PopupId == id && pop.Window) {
                              if (width == 0)
                                  width = pop.Window->Size.x;
                              CHECK(pop.Window->Size.x == width);
                          }
                      ImGui::End();
                  });
        }
        CHECK(width > 300);
        // Floating panels stay in the main OS window, including old detached layouts.
        frame([] {}, [] { ImGui::ClosePopupsOverWindow(nullptr, true); });
        CHECK(!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable));
        // The header symbol is an inline field: click, type, Enter loads it in that chart.
        {
            auto field = aw->ContentRegionRect.Min;
            ImVec2 at{field.x + 12, field.y + 12};
            press(at.x, at.y);
            release();
            CHECK(ImGui::GetActiveID() == aw->GetID("##symbol"));
            frame([&] {
                for (char c : std::string("QQQ"))
                    io.AddInputCharacter(c);
            });
            key(ImGuiKey_Enter);
            CHECK(a.symbol == "QQQ" && b.symbol != "QQQ");
            state.select(a, "SPY", 2);
            state.tick();
            a.view = unchanged;
            for (int i = 0; i < 3; ++i)
                frame();
        }
        state.drawing_tools_open = true;
        frame();
        auto *tools = ImGui::FindWindowByName("Drawing tools");
        CHECK(tools);
        ImGui::LoadIniSettingsFromMemory(
            "[Window][Drawing tools]\nViewportPos=3000,2000\nViewportId=0x12345678\n"
            "Pos=0,0\nSize=320,560\nCollapsed=0\n\n");
        for (int i = 0; i < 4; ++i)
            frame();
        CHECK(tools->Viewport == ImGui::GetMainViewport());
        CHECK(!tools->ViewportOwned);
        CHECK(ImGui::GetPlatformIO().Viewports.Size == 1);
        auto *main_viewport = ImGui::GetMainViewport();
        CHECK(tools->Pos.x < main_viewport->Pos.x + main_viewport->Size.x);
        CHECK(tools->Pos.y < main_viewport->Pos.y + main_viewport->Size.y);
        CHECK(tools->Pos.x + tools->Size.x > main_viewport->Pos.x);
        CHECK(tools->Pos.y + tools->TitleBarHeight > main_viewport->Pos.y);
        state.ini = ImGui::SaveIniSettingsToMemory();
        state.save(true);
        {
            State restored(dir, true);
            restored.tick();
            CHECK(restored.panels.size() == 2);
            CHECK(restored.ini == state.ini);
            CHECK(restored.drawing_tools_open);
            CHECK(restored.drawings.document() == state.drawings.document());
            CHECK(restored.panels[0]->view.count == a.view.count);
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::filesystem::remove_all(dir);
        std::cout << "Native chart input, swipe pan, drawings, measurement, undo/redo, auto-fit and restart "
                     "passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        std::filesystem::remove_all(dir);
        return 1;
    }
}
