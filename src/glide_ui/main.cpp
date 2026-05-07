#include "common/ipc.hpp"
#include "common/logging.hpp"
#include "common/preview_control.hpp"

#if OPENHD_GLIDE_HAS_LVGL_SDL
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#endif

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
    stop_requested = 1;
}

struct HeadlessOptions {
    std::string ipc_socket { glide::ipc::default_socket_path };
};

HeadlessOptions parse_headless_options(int argc, char** argv)
{
    HeadlessOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--ipc-socket" && i + 1 < argc) {
            options.ipc_socket = argv[++i];
        }
    }
    return options;
}

int run_headless_ui(const HeadlessOptions& options)
{
    glide::log(glide::LogLevel::info, "GlideUI", "headless control worker started");

    glide::ipc::Client ipc;
    if (ipc.connect_to(options.ipc_socket)) {
        ipc.send_line("hello glide-ui");
        ipc.send_line("status glide-ui ready headless");
        ipc.send_line("get fps");
    } else {
        glide::log(glide::LogLevel::warning, "GlideUI", "IPC controller unavailable");
    }

    auto next_heartbeat = std::chrono::steady_clock::now();
    while (stop_requested == 0) {
        if (ipc.connected()) {
            for (const auto& line : ipc.poll_lines()) {
                if (line == "state fps 0" || line == "state fps 1") {
                    glide::preview_control::set_fps_overlay_enabled(line.back() == '1');
                }
            }
            if (std::chrono::steady_clock::now() >= next_heartbeat) {
                ipc.send_line("heartbeat glide-ui");
                next_heartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}

} // namespace

#if OPENHD_GLIDE_HAS_LVGL_SDL
namespace {

struct Options {
    std::uint32_t width { 760 };
    std::uint32_t height { 720 };
    int x {};
    int y {};
    float opacity { 1.0F };
    bool preview {};
    bool positioned {};
    bool borderless {};
    bool always_on_top {};
    bool transparent_clear {};
    bool headless {};
    std::string ipc_socket { glide::ipc::default_socket_path };
};

enum class SidebarPanel {
    scan = 0,
    link = 1,
    video = 2,
    camera = 3,
    recording = 4,
    rc = 5,
    misc = 6,
    status = 7,
};

struct UiState {
    glide::ipc::Client ipc;
    bool fps_enabled { true };
    SidebarPanel active_panel { SidebarPanel::scan };
    lv_obj_t* panel_title {};
    lv_obj_t* panel_body {};
    lv_obj_t* fps_switch {};
    lv_obj_t* fps_label {};
    lv_obj_t* nav_buttons[9] {};
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--preview") {
            options.preview = true;
        } else if (argument == "--headless" || argument == "--device") {
            options.headless = true;
        } else if (argument == "--width" && i + 1 < argc) {
            options.width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--height" && i + 1 < argc) {
            options.height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--x" && i + 1 < argc) {
            options.x = std::stoi(argv[++i]);
            options.positioned = true;
        } else if (argument == "--y" && i + 1 < argc) {
            options.y = std::stoi(argv[++i]);
            options.positioned = true;
        } else if (argument == "--opacity" && i + 1 < argc) {
            options.opacity = std::clamp(std::stof(argv[++i]), 0.05F, 1.0F);
        } else if (argument == "--borderless") {
            options.borderless = true;
        } else if (argument == "--always-on-top") {
            options.always_on_top = true;
        } else if (argument == "--transparent-clear") {
            options.transparent_clear = true;
        } else if (argument == "--ipc-socket" && i + 1 < argc) {
            options.ipc_socket = argv[++i];
        }
    }
    return options;
}

lv_color_t color(std::uint32_t rgb)
{
    return lv_color_hex(rgb);
}

void set_panel_style(lv_obj_t* object, std::uint32_t bg, std::uint8_t opacity = LV_OPA_COVER)
{
    lv_obj_set_style_bg_color(object, color(bg), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, std::uint32_t text_color = 0xffffff)
{
    auto* object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, color(text_color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    return object;
}

const char* panel_title(SidebarPanel panel)
{
    switch (panel) {
    case SidebarPanel::scan:
        return "Find Air Unit";
    case SidebarPanel::link:
        return "Link";
    case SidebarPanel::video:
        return "Video";
    case SidebarPanel::camera:
        return "Camera";
    case SidebarPanel::recording:
        return "Recording";
    case SidebarPanel::rc:
        return "RC";
    case SidebarPanel::misc:
        return "Misc";
    case SidebarPanel::status:
        return "Status";
    }
    return "Misc";
}

const char* nav_symbol(int index)
{
    switch (index) {
    case 0:
        return LV_SYMBOL_LEFT;
    case 1:
        return LV_SYMBOL_EYE_OPEN;
    case 2:
        return LV_SYMBOL_WIFI;
    case 3:
        return LV_SYMBOL_VIDEO;
    case 4:
        return LV_SYMBOL_IMAGE;
    case 5:
        return LV_SYMBOL_SAVE;
    case 6:
        return LV_SYMBOL_KEYBOARD;
    case 7:
        return LV_SYMBOL_SETTINGS;
    case 8:
        return LV_SYMBOL_HOME;
    }
    return LV_SYMBOL_SETTINGS;
}

void send_fps_state(UiState& state)
{
    glide::preview_control::set_fps_overlay_enabled(state.fps_enabled);
    if (state.ipc.connected()) {
        state.ipc.send_line(std::string("set fps ") + (state.fps_enabled ? "1" : "0"));
    }
}

void sync_fps_controls(UiState& state)
{
    if (state.fps_switch == nullptr || state.fps_label == nullptr) {
        return;
    }

    if (state.fps_enabled) {
        lv_obj_add_state(state.fps_switch, LV_STATE_CHECKED);
        lv_label_set_text(state.fps_label, "FPS overlay enabled");
    } else {
        lv_obj_remove_state(state.fps_switch, LV_STATE_CHECKED);
        lv_label_set_text(state.fps_label, "FPS overlay disabled");
    }
}

void build_scan_panel(UiState& state)
{
    lv_obj_set_flex_flow(state.panel_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state.panel_body, 12, 0);

    auto* channels = lv_obj_create(state.panel_body);
    set_panel_style(channels, 0x162332);
    lv_obj_set_size(channels, LV_PCT(100), 138);
    auto* channels_text = label(channels, "Channels", &lv_font_montserrat_28);
    lv_obj_align(channels_text, LV_ALIGN_LEFT_MID, 84, 0);
    auto* channels_value = label(channels, "OpenHD [1-7]", &lv_font_montserrat_24);
    lv_obj_align(channels_value, LV_ALIGN_RIGHT_MID, -100, 0);

    auto* bandwidth = lv_obj_create(state.panel_body);
    set_panel_style(bandwidth, 0x162332);
    lv_obj_set_size(bandwidth, LV_PCT(100), 138);
    auto* bandwidth_text = label(bandwidth, "Bandwidth", &lv_font_montserrat_28);
    lv_obj_align(bandwidth_text, LV_ALIGN_LEFT_MID, 74, 0);
    auto* bandwidth_value = label(bandwidth, "20 MHz", &lv_font_montserrat_24);
    lv_obj_align(bandwidth_value, LV_ALIGN_RIGHT_MID, -130, 0);

    auto* scan = lv_button_create(state.panel_body);
    set_panel_style(scan, 0x101722);
    lv_obj_set_size(scan, 500, 60);
    lv_obj_set_style_bg_color(scan, color(0x101722), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(scan, color(0x1c3344), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(scan, 0, 0);
    lv_obj_align(scan, LV_ALIGN_TOP_MID, 0, 8);
    auto* scan_label = label(scan, "START SCAN", &lv_font_montserrat_26);
    lv_obj_center(scan_label);

    auto* progress_row = lv_obj_create(state.panel_body);
    set_panel_style(progress_row, 0x0b1722, LV_OPA_TRANSP);
    lv_obj_set_size(progress_row, LV_PCT(100), 80);
    auto* bar = lv_bar_create(progress_row);
    lv_obj_set_size(bar, 300, 8);
    lv_obj_align(bar, LV_ALIGN_CENTER, -50, 0);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, color(0x0b1722), 0);
    lv_obj_set_style_bg_color(bar, color(0x185577), LV_PART_INDICATOR);
    auto* percent = label(progress_row, "0%", &lv_font_montserrat_32);
    lv_obj_align(percent, LV_ALIGN_CENTER, 230, 0);
}

void build_misc_panel(UiState& state)
{
    lv_obj_set_flex_flow(state.panel_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(state.panel_body, 42, 0);
    lv_obj_set_style_pad_row(state.panel_body, 20, 0);

    auto* section = label(state.panel_body, "Flow Overlay", &lv_font_montserrat_28);
    lv_obj_set_width(section, LV_PCT(100));

    auto* row = lv_obj_create(state.panel_body);
    set_panel_style(row, 0x162332);
    lv_obj_set_size(row, LV_PCT(100), 82);
    lv_obj_set_style_pad_left(row, 28, 0);
    lv_obj_set_style_pad_right(row, 28, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    state.fps_label = label(row, "FPS overlay enabled", &lv_font_montserrat_22);
    state.fps_switch = lv_switch_create(row);
    lv_obj_set_size(state.fps_switch, 72, 36);
    lv_obj_add_event_cb(
        state.fps_switch,
        [](lv_event_t* event) {
            auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
            state->fps_enabled = lv_obj_has_state(state->fps_switch, LV_STATE_CHECKED);
            sync_fps_controls(*state);
            send_fps_state(*state);
        },
        LV_EVENT_VALUE_CHANGED,
        &state);
    sync_fps_controls(state);
}

void build_placeholder_panel(UiState& state)
{
    lv_obj_set_style_pad_all(state.panel_body, 42, 0);
    auto* text = label(state.panel_body, "Controls will be ported here.", &lv_font_montserrat_24, 0xb3c6d6);
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 0);
}

void clear_panel(UiState& state)
{
    lv_obj_clean(state.panel_body);
    state.fps_switch = nullptr;
    state.fps_label = nullptr;
}

void set_active_panel(UiState& state, SidebarPanel panel)
{
    state.active_panel = panel;
    lv_label_set_text(state.panel_title, panel_title(panel));
    clear_panel(state);

    for (int i = 1; i < 9; ++i) {
        if (state.nav_buttons[i] == nullptr) {
            continue;
        }
        if (static_cast<int>(panel) == i - 1) {
            lv_obj_add_state(state.nav_buttons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(state.nav_buttons[i], LV_STATE_CHECKED);
        }
    }

    if (panel == SidebarPanel::scan) {
        build_scan_panel(state);
    } else if (panel == SidebarPanel::misc) {
        build_misc_panel(state);
    } else {
        build_placeholder_panel(state);
    }
}

void nav_clicked(lv_event_t* event)
{
    auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
    auto index = static_cast<int>(reinterpret_cast<std::intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    if (index <= 0) {
        return;
    }
    set_active_panel(*state, static_cast<SidebarPanel>(index - 1));
}

void build_sidebar(UiState& state, std::uint32_t width, std::uint32_t height)
{
    auto* screen = lv_screen_active();
    set_panel_style(screen, 0x0b1118);

    auto* rail = lv_obj_create(screen);
    set_panel_style(rail, 0x0d1620);
    lv_obj_set_size(rail, 88, height);
    lv_obj_align(rail, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < 9; ++i) {
        auto* button = lv_button_create(rail);
        state.nav_buttons[i] = button;
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<std::intptr_t>(i)));
        lv_obj_set_size(button, 88, static_cast<int>(height / 9));
        lv_obj_set_style_radius(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, color(0x0d1620), 0);
        lv_obj_set_style_bg_color(button, color(0x172a3a), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, color(0x1d3344), LV_STATE_PRESSED);
        lv_obj_add_event_cb(button, nav_clicked, LV_EVENT_CLICKED, &state);

        auto* icon = label(button, nav_symbol(i), &lv_font_montserrat_36);
        lv_obj_center(icon);
    }

    auto* panel = lv_obj_create(screen);
    set_panel_style(panel, 0x0b1722);
    lv_obj_set_size(panel, static_cast<int>(width) - 88, height);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 88, 0);

    auto* header = lv_obj_create(panel);
    set_panel_style(header, 0x162332);
    lv_obj_set_size(header, LV_PCT(100), 96);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    state.panel_title = label(header, "Find Air Unit", &lv_font_montserrat_36);
    lv_obj_center(state.panel_title);

    state.panel_body = lv_obj_create(panel);
    set_panel_style(state.panel_body, 0x0b1722);
    lv_obj_set_size(state.panel_body, LV_PCT(100), static_cast<int>(height) - 96);
    lv_obj_align(state.panel_body, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    set_active_panel(state, SidebarPanel::scan);
}

void poll_ipc(UiState& state)
{
    if (!state.ipc.connected()) {
        state.fps_enabled = glide::preview_control::fps_overlay_enabled();
        sync_fps_controls(state);
        return;
    }

    for (const auto& line : state.ipc.poll_lines()) {
        if (line == "state fps 0" || line == "state fps 1") {
            state.fps_enabled = line.back() == '1';
            glide::preview_control::set_fps_overlay_enabled(state.fps_enabled);
            sync_fps_controls(state);
        }
    }
}

void set_sdl_position(const Options& options)
{
    if (!options.positioned) {
        return;
    }
    const auto position = std::to_string(options.x) + "," + std::to_string(options.y);
    setenv("SDL_VIDEO_WINDOW_POS", position.c_str(), 1);
}

} // namespace

int main(int argc, char** argv)
{
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    const auto options = parse_options(argc, argv);
    if (options.headless) {
        return run_headless_ui(HeadlessOptions {
            .ipc_socket = options.ipc_socket,
        });
    }

    glide::log(glide::LogLevel::info, "GlideUI", "LVGL UI started");

    if (!options.preview) {
        return 0;
    }

    set_sdl_position(options);
    lv_init();
    auto* display = lv_sdl_window_create(static_cast<int32_t>(options.width), static_cast<int32_t>(options.height));
    lv_sdl_window_set_title(display, "GlideUI LVGL Preview");
    lv_sdl_window_set_resizeable(display, false);
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();

    UiState state;
    state.fps_enabled = glide::preview_control::fps_overlay_enabled();
    if (state.ipc.connect_to(options.ipc_socket)) {
        state.ipc.send_line("hello glide-ui");
        state.ipc.send_line("status glide-ui ready lvgl-sdl");
        state.ipc.send_line("get fps");
    } else {
        glide::log(glide::LogLevel::warning, "GlideUI", "IPC controller unavailable; using preview fallback state");
    }

    build_sidebar(state, options.width, options.height);

    auto last_tick = std::chrono::steady_clock::now();
    while (stop_requested == 0) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick);
        if (elapsed.count() > 0) {
            lv_tick_inc(static_cast<uint32_t>(elapsed.count()));
            last_tick = now;
        }

        poll_ipc(state);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    lv_sdl_quit();
    return 0;
}
#else
int main(int argc, char** argv)
{
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    bool headless {};
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--headless" || argument == "--device") {
            headless = true;
        }
    }
    if (headless) {
        return run_headless_ui(parse_headless_options(argc, argv));
    }

    glide::log(glide::LogLevel::error, "GlideUI", "LVGL SDL preview requires SDL2 development files; use --headless for device IPC tests");
    return 1;
}
#endif
