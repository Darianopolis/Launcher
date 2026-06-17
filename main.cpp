#include <print>
#include <vector>
#include <algorithm>
#include <chrono>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <flat_set>

auto format_time(std::chrono::system_clock::time_point time) -> std::string
{
    time_t tt = std::chrono::system_clock::to_time_t(time);

    tm tm = {};
    localtime_r(&tt, &tm);

    auto year  = tm.tm_year + 1900;
    auto month = tm.tm_mon + 1;
    auto msec  = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

    static constexpr std::string_view weekdays[] = {
        "Sunday",
        "Monday",
        "Tuesday",
        "Wednesday",
        "Thursday",
        "Friday",
        "Saturday",
    };

    static constexpr auto day_suffix = [&](auto i) {
        switch (i) {
            break;case 1: case 21: case 31: return "st";
            break;case 2: case 22:          return "nd";
            break;case 3: case 23:          return "rd";
            break;default:                  return "th";
        }
    };

    return std::format("{} {}{} {:02}:{:02}",
        weekdays[tm.tm_wday].substr(0, 3), tm.tm_mday, day_suffix(tm.tm_mday),
        tm.tm_hour, tm.tm_min, tm.tm_sec);
};

template<typename Fn>
struct DeferGuard
{
    Fn fn;

    DeferGuard(Fn&& fn): fn(std::move(fn)) {}
    ~DeferGuard() { fn(); };
};

#define defer DeferGuard _ = [&]

static const std::flat_set<std::string_view> xwayland_blocklist = {
    "Spotify (Launcher)",
};

struct WmLauncherApp
{
    GAppInfo* info;
    std::string display_name;
    std::string filter_string;

    bool shown;
    bool block_xwayland;
};

static
struct {
    std::vector<struct WmLauncherApp> apps;
    std::string filter;
    const WmLauncherApp* selected = 0;
} launcher;

static
void clear_apps()
{
    for (auto& entry : launcher.apps) g_object_unref(entry.info);
    launcher.apps.clear();
}

static
auto match_string(std::string haystack, std::string needle) -> bool
{
    if (needle.empty()) return true;

    auto it = std::ranges::search(haystack, needle, [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
    });

    return !it.empty();
}

static
void filter(bool up, bool down)
{
    const WmLauncherApp* first_matched = nullptr;
    const WmLauncherApp* last_matched = nullptr;
    for (auto& app : launcher.apps) {
        app.shown = match_string(app.filter_string, launcher.filter);

        if (!app.shown) continue;

        if (up && last_matched && &app == launcher.selected) {
            launcher.selected = last_matched;
            up = false;
        }

        if (down && last_matched && last_matched == launcher.selected) {
            launcher.selected = &app;
            down = false;
        }

        if (!first_matched) first_matched = &app;
        last_matched = &app;
    }

    if (!launcher.selected || !launcher.selected->shown) {
        launcher.selected = first_matched;
    }
}

static
void scan_apps()
{
    clear_apps();

    auto* apps = g_app_info_get_all();
    defer { g_list_free(apps); };

    for (auto* l = apps; l; l = l->next) {
        GAppInfo* app = G_APP_INFO(l->data);
        defer {
            if (app) g_object_unref(app);
        };

        if (!G_IS_DESKTOP_APP_INFO(app)) continue;
        if (!g_app_info_should_show(app)) continue;
        if (!g_app_info_get_executable(app)) continue;

        auto* desktop = G_DESKTOP_APP_INFO(app);

        auto& entry = launcher.apps.emplace_back();
        entry.info = app;
        entry.display_name = g_app_info_get_display_name(app) ?: g_app_info_get_name(app);

        entry.filter_string += g_app_info_get_display_name(app) ?: "";
        entry.filter_string += '\0';
        entry.filter_string += g_app_info_get_executable(app);

        entry.block_xwayland = xwayland_blocklist.contains(g_app_info_get_name(app));

        // TODO: Categories
        // std::println("Categories: {}", g_desktop_app_info_get_categories(desktop) ?: "");

        for (auto* keyword = g_desktop_app_info_get_keywords(desktop); keyword && *keyword; ++keyword) {
            entry.filter_string += '\0';
            entry.filter_string += *keyword;
        }

        std::string_view id = g_app_info_get_id(app) ?: "";
        if (id.ends_with(".desktop")) {
            id.remove_suffix(strlen(".desktop"));
        }
        entry.filter_string += '\0';
        entry.filter_string += id;

        app = nullptr;
    }

    std::ranges::sort(launcher.apps, std::less{}, &WmLauncherApp::display_name);

    filter(false, false);
}

static
void frame();

static
void hide()
{
    std::exit(0);
}

static
void launch(WmLauncherApp& app)
{
    std::println("Running: {}", app.display_name);
    std::println("  name: {}", g_app_info_get_name(app.info));
    std::println("  command line: {}", g_app_info_get_commandline(app.info) ?: "");

    auto* ctx = g_app_launch_context_new();
    defer { g_object_unref(ctx); };

    if (app.block_xwayland) {
        std::println("  blocking XWayland socket");
        g_app_launch_context_unsetenv(ctx, "DISPLAY");
    }

    GError* err = nullptr;
    if (!g_app_info_launch(app.info, nullptr, ctx, &err)) {
        std::println("Error launching {}: {}", app.display_name, err->message);
    }
    hide();
}

static
void frame()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    defer {
        ImGui::End();
        ImGui::PopStyleVar();
    };
    bool dont_close = true;
    if (!ImGui::Begin("Launch", &dont_close, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize)) return;

    if (!dont_close || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        launcher.filter = {};
        hide();
    }

    auto& io = ImGui::GetIO();

    // Time

    {
        // TODO: Refresh display on minute changes

        auto now = std::chrono::system_clock::now();
        ImGui::TextUnformatted(format_time(now).c_str());
        ImGui::Separator();
    }

    // Search bar

    ImGui::SetKeyboardFocusHere(0);

    bool check_scroll = false;
    bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    if (up && down) { up = down = false; }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##filter", "Search applications...", &launcher.filter) || up || down) {
        filter(up, down);
        check_scroll = true;
    }
    ImGui::PopItemWidth();

    // Results

    ImGui::BeginChild("results", ImVec2(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    defer { ImGui::EndChild(); };

    bool mouse_moving = io.MouseDelta.x || io.MouseDelta.y || io.MouseWheel;

    for (auto& app : launcher.apps) {
        if (!app.shown) continue;

        bool highlight = &app == launcher.selected;

        int select_flags = 0;
        select_flags |= ImGuiSelectableFlags_AllowDoubleClick;
        if (highlight) select_flags |= ImGuiSelectableFlags_Highlight;
        if (!highlight) ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));

        bool selected = highlight && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        ImGui::Selectable(app.display_name.c_str(), selected, select_flags);
        if (selected || (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))) {
            launch(app);
        }

        if (!highlight) ImGui::PopStyleColor();

        // Only update highlight from hover if mouse is moving/scrolling
        if (ImGui::IsItemHovered() && mouse_moving) {
            highlight = true;
            launcher.selected = &app;
        }

        if (highlight && check_scroll) {
            ImGui::SetScrollHereY();
        }
    }
}

int main()
{
    SDL_SetHint("SDL_VIDEO_DRIVER", "wayland");

    SDL_Init(SDL_INIT_VIDEO);

    auto window = SDL_CreateWindow("Launcher", 800, 600, SDL_WINDOW_RESIZABLE);
    auto renderer = SDL_CreateRenderer(window, nullptr);

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    scan_apps();
    filter(false, false);

    SDL_Event event;
    for (;;) {
        bool first = true;
        while (first ? SDL_WaitEvent(&event) : SDL_PollEvent(&event)) {
            first = false;
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                break;case SDL_EVENT_QUIT:
                      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                      case SDL_EVENT_WINDOW_FOCUS_LOST:
                    goto CLOSE;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();

        frame();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }
CLOSE:
}
