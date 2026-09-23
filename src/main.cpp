#include "app.hpp"
#include "build_version.hpp"
#include "font.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "render_schedule.hpp"
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#include <mach-o/dyld.h>
#else
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <GL/gl.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif
struct Application;
static Application *application_for_refresh = nullptr;
struct Application {
    GLFWwindow *window = nullptr;
    std::unique_ptr<cr::State> state;
    int frames = 0, max_frames = 0;
    std::string screenshot;
    bool invalidated = true;
    int settle = 3, pumps = 0;
    double ui_due = 0;
    cr::Time build_next = 0;
    std::filesystem::path build_manifest;
    void frame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
#ifdef __EMSCRIPTEN__
        // GLFW's embedded browser port reports CSS-sized framebuffers by default.
        // Keep input/layout in CSS pixels, but rasterize into device pixels. Check
        // every frame to handle browser zoom and moves between different screens.
        auto &io = ImGui::GetIO();
        double ratio = emscripten_get_device_pixel_ratio();
        if (!std::isfinite(ratio) || ratio <= 0)
            ratio = 1;
        int backing_width = std::max(1, int(std::lround(io.DisplaySize.x * ratio)));
        int backing_height = std::max(1, int(std::lround(io.DisplaySize.y * ratio)));
        int current_width = 0, current_height = 0;
        emscripten_get_canvas_element_size("#canvas", &current_width, &current_height);
        if (backing_width != current_width || backing_height != current_height)
            emscripten_set_canvas_element_size("#canvas", backing_width, backing_height);
        io.DisplayFramebufferScale = ImVec2(backing_width / std::max(1.f, io.DisplaySize.x),
                                            backing_height / std::max(1.f, io.DisplaySize.y));
#endif
        ImGui::NewFrame();
        cr::frame(*state, false);
        ImGui::Render();
        int w, h;
#ifdef __EMSCRIPTEN__
        emscripten_get_canvas_element_size("#canvas", &w, &h);
#else
        glfwGetFramebufferSize(window, &w, &h);
#endif
        glViewport(0, 0, w, h);
        glClearColor(.045f, .063f, .09f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!screenshot.empty() && frames == max_frames - 1) {
            std::vector<unsigned char> pixels(size_t(w) * h * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream file(screenshot, std::ios::binary);
            file << "P6\n" << w << " " << h << "\n255\n";
            for (int y = h - 1; y >= 0; --y)
                file.write(reinterpret_cast<char *>(pixels.data() + size_t(y) * w * 3), w * 3);
        }
        glfwSwapBuffers(window);
#ifndef __EMSCRIPTEN__
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            auto *context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            for (auto *viewport : ImGui::GetPlatformIO().Viewports)
                if (viewport->ID != ImGui::GetMainViewport()->ID && viewport->PlatformHandle)
                    glfwSetWindowRefreshCallback(
                        static_cast<GLFWwindow *>(viewport->PlatformHandle), +[](GLFWwindow *) {
                            if (application_for_refresh)
                                application_for_refresh->invalidated = true;
                        });
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(context);
        }
#endif
        ++frames;
        if (max_frames > 0 && frames >= max_frames)
            glfwSetWindowShouldClose(window, true);
    }
    bool visible() const {
#ifdef __EMSCRIPTEN__
        return !EM_ASM_INT({ return document.hidden; });
#else
        if (max_frames > 0 || !glfwGetWindowAttrib(window, GLFW_ICONIFIED))
            return true;
        for (auto *viewport : ImGui::GetPlatformIO().Viewports)
            if (viewport->ID != ImGui::GetMainViewport()->ID &&
                !(viewport->Flags & ImGuiViewportFlags_IsMinimized))
                return true;
        return false;
#endif
    }
    double pump(bool input = false) {
        ++pumps;
        glfwPollEvents();
        auto &g = *ImGui::GetCurrentContext();
        input |= !g.InputEventsQueue.empty();
        bool changed = state->tick();
#ifndef __EMSCRIPTEN__
        for (auto *viewport : ImGui::GetPlatformIO().Viewports)
            invalidated |= viewport->PlatformRequestClose || viewport->PlatformRequestMove ||
                           viewport->PlatformRequestResize;
        if (cr::now() >= build_next) {
            build_next = cr::now() + 60;
            try {
                auto info = cr::read_json(build_manifest);
                uint64_t available = info.at("build_number").get<uint64_t>();
                if (available > 0 && available < 1000000000000ULL) {
                    invalidated |=
                        available != state->available_build &&
                        (available > cr::build::number || state->available_build > cr::build::number);
                    state->available_build = available;
                }
            } catch (...) { /* A missing manifest means update status is unknown. */
            }
        }
#else
        uint64_t available = uint64_t(EM_ASM_DOUBLE({ return Module.chartroomAvailableBuild || 0; }));
        invalidated |= available != state->available_build &&
                       (available > cr::build::number || state->available_build > cr::build::number);
        state->available_build = available;
#endif
        if (!glfwGetWindowAttrib(window, GLFW_ICONIFIED) && !glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
            int width, height, x, y;
            glfwGetWindowSize(window, &width, &height);
            glfwGetWindowPos(window, &x, &y);
            if (width != state->width || height != state->height || x != state->x || y != state->y) {
                state->width = width;
                state->height = height;
                state->x = x;
                state->y = y;
                state->request_save();
            }
        }
        bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
        if (state->maximized != maximized) {
            state->maximized = maximized;
            state->request_save();
        }
        state->save();
        if (input || changed || invalidated)
            settle = 3;
        invalidated = false;
        double clock = glfwGetTime();
        if (visible() && (settle > 0 || clock >= ui_due)) {
            frame();
            if (settle > 0)
                --settle;
            ui_due = glfwGetTime() + cr::next_ui_frame();
            if (settle > 0)
                ui_due = std::min(ui_due, glfwGetTime() + 1. / 60.);
        }
        double delay =
            visible() ? std::max(.001, ui_due - glfwGetTime()) : std::numeric_limits<double>::infinity();
        auto deadline = state->next_deadline();
#ifndef __EMSCRIPTEN__
        deadline = std::min(deadline, build_next);
#endif
        if (deadline != std::numeric_limits<cr::Time>::max()) {
            double wall =
                std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            delay = std::min(delay, std::max(.001, double(deadline) - wall));
        }
#ifdef __EMSCRIPTEN__
        EM_ASM(
            {
                Module.chartroomStats = Module.chartroomStats || {};
                Module.chartroomStats.frames = $0;
                Module.chartroomStats.wakes = $1;
            },
            frames, pumps);
#endif
        return delay;
    }
};
#ifdef __EMSCRIPTEN__
static Application *browser_app = nullptr;
extern "C" EMSCRIPTEN_KEEPALIVE double chartroom_pump(int input) {
    double delay = browser_app->pump(input != 0);
    return std::isfinite(delay) ? delay * 1000 : -1;
}
#endif
int main(int argc, char **argv) {
    try {
        auto directory = cr::default_directory();
        std::filesystem::path import;
        bool offline = false, hidden = false;
        int smoke = 0;
        double idle_check = 0;
        std::string shot;
        bool fetch_only = false;
        std::string symbol = "SPY";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing value after " + arg);
                return std::string(argv[++i]);
            };
            if (arg == "--data-dir")
                directory = value();
            else if (arg == "--import-julia")
                import = value();
            else if (arg == "--offline")
                offline = true;
            else if (arg == "--hidden")
                hidden = true;
            else if (arg == "--idle-check") {
                idle_check = std::stod(value());
                if (!std::isfinite(idle_check) || idle_check <= 0)
                    throw std::runtime_error("--idle-check requires a positive number of seconds");
            } else if (arg == "--smoke")
                smoke = std::stoi(value());
            else if (arg == "--screenshot")
                shot = value();
            else if (arg == "--fetch") {
                fetch_only = true;
                symbol = cr::normalize_symbol(value());
            } else if (arg == "--version") {
                std::cout << "Chartroom " << cr::build::version << " (" << cr::build::timestamp << ")\n";
                return 0;
            } else if (arg == "--help") {
                std::cout
                    << "Chartroom C++\n  --data-dir PATH       Cache and workspace directory\n  "
                       "--import-julia PATH   Copy Julia .chartroom on first launch\n  --offline            "
                       "Use cached data only\n  --smoke FRAMES       Bounded rendering check\n  --hidden     "
                       "        Hidden smoke window\n  --screenshot FILE    Write final smoke frame as PPM\n "
                       " --fetch SYMBOL       Verify market data without a window\n"
                       "  --idle-check SECONDS  Run the event loop for a bounded idle check\n"
                       "  --version            Print build version and timestamp\n";
                return 0;
            } else
                throw std::runtime_error("Unknown option: " + arg);
        }
#ifndef __EMSCRIPTEN__
        if (fetch_only) {
            cr::State state(directory, offline, import);
            auto &p = state.current();
            state.select(p, symbol, 2);
            state.refresh(p);
            auto start = cr::now();
            while (cr::now() - start < 60) {
                state.tick();
                auto &e = state.ensure(symbol, 2);
                if (!e.loading && (e.loaded || !e.error.empty())) {
                    if (!e.error.empty())
                        throw std::runtime_error(e.error);
                    std::cout << symbol << " / " << cr::data_source(e.history) << " / "
                              << e.history.bars.size() << " bars / " << cr::date(e.history.bars.back().time)
                              << " / close " << e.history.bars.back().close << "\n";
                    return 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            throw std::runtime_error("Market data fetch timed out");
        }
#endif
        glfwSetErrorCallback(
            [](int code, const char *message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
        if (!glfwInit())
            throw std::runtime_error("Could not initialize the desktop display.");
#ifdef __EMSCRIPTEN__
        const char *glsl = "#version 300 es";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
        const char *glsl = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
        const char *glsl = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
        if (hidden)
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        auto app = std::make_unique<Application>();
        application_for_refresh = app.get();
#ifndef __EMSCRIPTEN__
        std::filesystem::path executable = std::filesystem::absolute(argv[0]);
#ifdef __APPLE__
        uint32_t length = 0;
        _NSGetExecutablePath(nullptr, &length);
        std::vector<char> path(length);
        if (_NSGetExecutablePath(path.data(), &length) == 0)
            executable = path.data();
        app->build_manifest = executable.parent_path().parent_path() / "Resources" / "build-info.json";
#elif defined(_WIN32)
        std::vector<wchar_t> path(32768);
        auto length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (length)
            executable = std::wstring(path.data(), length);
        app->build_manifest = executable.parent_path() / "build-info.json";
#else
        std::error_code ec;
        auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec)
            executable = path;
        app->build_manifest = executable.parent_path() / "build-info.json";
#endif
#endif
        app->state = std::make_unique<cr::State>(directory, offline, import);
        auto &state = *app->state;
        app->window = glfwCreateWindow(state.width, state.height, "Chartroom", nullptr, nullptr);
        if (!app->window) {
            glfwTerminate();
            throw std::runtime_error("Could not create an OpenGL window.");
        }
        if (state.x >= 0 && state.y >= 0)
            glfwSetWindowPos(app->window, state.x, state.y);
        if (state.maximized)
            glfwMaximizeWindow(app->window);
        glfwMakeContextCurrent(app->window);
        glfwSwapInterval(1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
#ifndef __EMSCRIPTEN__
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
        io.IniFilename = nullptr;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        ImFontConfig font;
        font.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(chartroom_font, sizeof(chartroom_font), 17, &font);
        cr::theme();
        if (!state.ini.empty())
            ImGui::LoadIniSettingsFromMemory(state.ini.c_str());
        glfwSetWindowUserPointer(app->window, app.get());
#ifndef __EMSCRIPTEN__
        auto invalidate = +[](GLFWwindow *window) {
            static_cast<Application *>(glfwGetWindowUserPointer(window))->invalidated = true;
        };
        glfwSetWindowRefreshCallback(app->window, invalidate);
        glfwSetFramebufferSizeCallback(
            app->window, +[](GLFWwindow *w, int, int) {
                static_cast<Application *>(glfwGetWindowUserPointer(w))->invalidated = true;
            });
        glfwSetWindowSizeCallback(
            app->window, +[](GLFWwindow *w, int, int) {
                static_cast<Application *>(glfwGetWindowUserPointer(w))->invalidated = true;
            });
        glfwSetWindowContentScaleCallback(
            app->window, +[](GLFWwindow *w, float, float) {
                static_cast<Application *>(glfwGetWindowUserPointer(w))->invalidated = true;
            });
        glfwSetWindowIconifyCallback(
            app->window, +[](GLFWwindow *w, int) {
                static_cast<Application *>(glfwGetWindowUserPointer(w))->invalidated = true;
            });
#endif
        ImGui_ImplGlfw_InitForOpenGL(app->window, true);
        ImGui_ImplOpenGL3_Init(glsl);
        app->max_frames = smoke;
        app->screenshot = shot;
#ifdef __EMSCRIPTEN__
        auto raw = app.release();
        browser_app = raw;
        EM_ASM(
            {
                Module.chartroomBuild = $0;
                Module.chartroomVersion = UTF8ToString($1);
            },
            double(cr::build::number), cr::build::version);
        state.set_wakeup([] {
            EM_ASM({
                if (Module.chartroomRequest)
                    Module.chartroomRequest(false);
            });
        });
        ImGui_ImplGlfw_InstallEmscriptenCallbacks(raw->window, "#canvas");
        emscripten_set_visibilitychange_callback(
            raw, false, [](int, const EmscriptenVisibilityChangeEvent *event, void *data) -> EM_BOOL {
                if (event->hidden) {
                    auto *app = static_cast<Application *>(data);
                    app->state->ini = ImGui::SaveIniSettingsToMemory();
                    app->state->save(true);
                }
                return false;
            });
        EM_ASM({ Module.chartroomInstallScheduler(); });
        emscripten_exit_with_live_runtime();
#else
        state.set_wakeup([] { glfwPostEmptyEvent(); });
        double stop_at =
            idle_check > 0 ? glfwGetTime() + idle_check : std::numeric_limits<double>::infinity();
        while (!glfwWindowShouldClose(app->window) && glfwGetTime() < stop_at) {
            if (smoke) {
                state.tick();
                app->frame();
                continue;
            }
            double delay = std::min(app->pump(), stop_at - glfwGetTime());
            if (glfwWindowShouldClose(app->window))
                break;
            if (std::isfinite(delay))
                glfwWaitEventsTimeout(std::max(.001, delay));
            else
                glfwWaitEvents();
        }
        if (idle_check > 0)
            std::cout << "Idle check: " << app->frames << " frames / " << app->pumps << " wakes in "
                      << idle_check << " seconds\n";
        state.ini = ImGui::SaveIniSettingsToMemory();
        state.save(true);
        size_t bars = 0;
        for (auto &p : state.panels)
            bars += p->bars.size();
        if (smoke)
            std::cout << "Rendered " << app->frames << " frames / " << state.panels.size() << " charts / "
                      << bars << " loaded bars\n";
        // Join workers before terminating GLFW: completion callbacks can post wake events.
        app->state.reset();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        application_for_refresh = nullptr;
        glfwDestroyWindow(app->window);
        glfwTerminate();
        if (smoke && bars == 0)
            return 2;
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Chartroom: " << e.what() << "\n";
        return 1;
    }
}
