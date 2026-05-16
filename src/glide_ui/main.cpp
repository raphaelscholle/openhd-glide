#include "common/ipc.hpp"
#include "common/logging.hpp"
#include "common/mavlink_state.hpp"
#include "common/preview_control.hpp"

#if OPENHD_GLIDE_HAS_LVGL
#include "lvgl.h"
#if OPENHD_GLIDE_HAS_LVGL_SDL
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#endif
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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
    glide::mavlink::Snapshot mavlink;
    while (stop_requested == 0) {
        if (ipc.connected()) {
            for (const auto& line : ipc.poll_lines()) {
                if (line == "state fps 0" || line == "state fps 1") {
                    glide::preview_control::set_fps_overlay_enabled(line.back() == '1');
                } else {
                    glide::mavlink::apply_ipc_line(mavlink, line);
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

#if OPENHD_GLIDE_HAS_LVGL
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
    bool buffer {};
    std::string buffer_path { "/tmp/openhd-glide-ui.argb" };
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
    glide::mavlink::Snapshot mavlink;
    bool fps_enabled { true };
    bool advanced_visible {};
    bool expanded { true };
    bool focus_panel {};
    int selected_row {};
    int row_count {};
    lv_obj_t* root {};
    SidebarPanel active_panel { SidebarPanel::scan };
    lv_obj_t* panel_title {};
    lv_obj_t* panel_body {};
    lv_obj_t* fps_switch {};
    lv_obj_t* fps_label {};
    lv_obj_t* scan_bar {};
    lv_obj_t* scan_percent {};
    lv_obj_t* nav_buttons[9] {};
};

struct BufferDisplay {
    int fd { -1 };
    std::uint32_t width {};
    std::uint32_t height {};
    std::size_t size {};
    void* map {};
    std::vector<std::uint32_t> draw_buffer;
};

void close_buffer_display(BufferDisplay& display)
{
#if defined(__linux__)
    if (display.map != nullptr && display.map != MAP_FAILED) {
        msync(display.map, display.size, MS_SYNC);
        munmap(display.map, display.size);
    }
    if (display.fd >= 0) {
        close(display.fd);
    }
#endif
    display = {};
}

bool create_buffer_display(BufferDisplay& target, const Options& options)
{
#if defined(__linux__)
    close_buffer_display(target);
    target.width = options.width;
    target.height = options.height;
    target.size = static_cast<std::size_t>(target.width) * target.height * sizeof(std::uint32_t);
    target.fd = open(options.buffer_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (target.fd < 0) {
        glide::log(glide::LogLevel::error, "GlideUI", "failed to open UI buffer " + options.buffer_path);
        return false;
    }
    if (ftruncate(target.fd, static_cast<off_t>(target.size)) != 0) {
        glide::log(glide::LogLevel::error, "GlideUI", "failed to resize UI buffer " + options.buffer_path);
        close_buffer_display(target);
        return false;
    }
    target.map = mmap(nullptr, target.size, PROT_READ | PROT_WRITE, MAP_SHARED, target.fd, 0);
    if (target.map == MAP_FAILED) {
        target.map = nullptr;
        glide::log(glide::LogLevel::error, "GlideUI", "failed to mmap UI buffer " + options.buffer_path);
        close_buffer_display(target);
        return false;
    }
    std::fill_n(static_cast<std::uint32_t*>(target.map), target.width * target.height, 0x00000000U);
    target.draw_buffer.resize(static_cast<std::size_t>(target.width) * target.height);
    return true;
#else
    (void)target;
    (void)options;
    glide::log(glide::LogLevel::error, "GlideUI", "buffer display requires Linux mmap support");
    return false;
#endif
}

void buffer_flush(lv_display_t* display, const lv_area_t* area, unsigned char* pixels)
{
    auto* target = static_cast<BufferDisplay*>(lv_display_get_user_data(display));
    if (target == nullptr || target->map == nullptr) {
        lv_display_flush_ready(display);
        return;
    }

    const int32_t x1 = std::max<int32_t>(0, area->x1);
    const int32_t y1 = std::max<int32_t>(0, area->y1);
    const int32_t x2 = std::min<int32_t>(static_cast<int32_t>(target->width) - 1, area->x2);
    const int32_t y2 = std::min<int32_t>(static_cast<int32_t>(target->height) - 1, area->y2);
    if (x2 >= x1 && y2 >= y1) {
        auto* destination = static_cast<std::uint32_t*>(target->map);
        auto* source = reinterpret_cast<const std::uint32_t*>(pixels);
        const auto area_width = static_cast<std::size_t>(area->x2 - area->x1 + 1);
        const auto copy_width = static_cast<std::size_t>(x2 - x1 + 1);
        for (int32_t y = y1; y <= y2; ++y) {
            const auto source_y = static_cast<std::size_t>(y - area->y1);
            const auto source_x = static_cast<std::size_t>(x1 - area->x1);
            std::memcpy(
                destination + static_cast<std::size_t>(y) * target->width + static_cast<std::size_t>(x1),
                source + source_y * area_width + source_x,
                copy_width * sizeof(std::uint32_t));
        }
        msync(target->map, target->size, MS_ASYNC);
    }
    lv_display_flush_ready(display);
}

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
        } else if (argument == "--buffer") {
            options.buffer = true;
        } else if (argument == "--buffer-path" && i + 1 < argc) {
            options.buffer_path = argv[++i];
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

const char* nav_text(int index)
{
    switch (index) {
    case 0:
        return "Back";
    case 1:
        return "Scan";
    case 2:
        return "Link";
    case 3:
        return "Video";
    case 4:
        return "Camera";
    case 5:
        return "Recording";
    case 6:
        return "RC";
    case 7:
        return "Misc";
    case 8:
        return "Status";
    }
    return "Menu";
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
        lv_label_set_text(state.fps_label, "KMS video plane FPS enabled");
    } else {
        lv_obj_remove_state(state.fps_switch, LV_STATE_CHECKED);
        lv_label_set_text(state.fps_label, "KMS video plane FPS disabled");
    }
}

void send_mavlink_action(UiState& state, const std::string& line)
{
    if (state.ipc.connected()) {
        state.ipc.send_line(line);
    }
}

lv_obj_t* value_row(UiState& state, const char* title, const std::string& value, std::uint32_t value_color = 0xffffff)
{
    const int row_index = state.row_count++;
    auto* row = lv_obj_create(state.panel_body);
    set_panel_style(row, state.focus_panel && state.selected_row == row_index ? 0x1b3141 : 0x111d28);
    lv_obj_set_size(row, LV_PCT(100), 46);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_border_width(row, state.focus_panel && state.selected_row == row_index ? 1 : 0, 0);
    lv_obj_set_style_border_color(row, color(0x2f7fa5), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* left = label(row, title, &lv_font_montserrat_16, 0xd7e3ec);
    lv_obj_set_width(left, LV_PCT(45));
    auto* right = label(row, value.c_str(), &lv_font_montserrat_16, value_color);
    lv_obj_set_width(right, LV_PCT(48));
    lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(right, LV_LABEL_LONG_DOT);
    return row;
}

lv_obj_t* action_button(UiState& state, const char* text)
{
    const int row_index = state.row_count++;
    auto* button = lv_button_create(state.panel_body);
    set_panel_style(button, state.focus_panel && state.selected_row == row_index ? 0x1b3141 : 0x101722);
    lv_obj_set_size(button, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(button, color(0x1d3344), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, color(0x20394d), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color(0x35566d), 0);

    auto* button_label = label(button, text, &lv_font_montserrat_16);
    lv_obj_center(button_label);
    return button;
}

void setup_panel_column(lv_obj_t* body, int pad = 14)
{
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, pad, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
}

void set_active_panel(UiState& state, SidebarPanel panel);
void build_sidebar(UiState& state, std::uint32_t width, std::uint32_t height);

void rebuild_ui(UiState& state)
{
    if (state.root == nullptr) {
        return;
    }
    const auto width = lv_obj_get_width(state.root);
    const auto height = lv_obj_get_height(state.root);
    lv_obj_clean(state.root);
    build_sidebar(state, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
}

void apply_terminal_key(UiState& state, const std::string& line)
{
    if (line.rfind("ui key ", 0) != 0) {
        return;
    }
    const auto key = line.substr(7);
    auto index = static_cast<int>(state.active_panel);
    if (!state.expanded) {
        if (key == "right" || key == "enter") {
            state.expanded = true;
            rebuild_ui(state);
        }
        return;
    }
    if (key == "up") {
        if (state.focus_panel) {
            state.selected_row = std::max(0, state.selected_row - 1);
            rebuild_ui(state);
        } else {
            index = std::max(0, index - 1);
            set_active_panel(state, static_cast<SidebarPanel>(index));
        }
    } else if (key == "down") {
        if (state.focus_panel) {
            state.selected_row = std::min(std::max(0, state.row_count - 1), state.selected_row + 1);
            rebuild_ui(state);
        } else {
            index = std::min(7, index + 1);
            set_active_panel(state, static_cast<SidebarPanel>(index));
        }
    } else if (key == "left" || key == "back") {
        if (state.focus_panel) {
            state.focus_panel = false;
            rebuild_ui(state);
        } else {
            state.expanded = false;
            rebuild_ui(state);
        }
    } else if (key == "right") {
        state.focus_panel = true;
        state.selected_row = 0;
        rebuild_ui(state);
    } else if (key == "enter") {
        if (!state.focus_panel) {
            state.focus_panel = true;
            state.selected_row = 0;
            rebuild_ui(state);
        } else if (state.active_panel == SidebarPanel::scan && state.selected_row == 2) {
            send_mavlink_action(state, glide::mavlink::format_action_command("scan", "bands=openhd width=" + std::to_string(state.mavlink.channel_width_mhz)));
        } else if (state.active_panel == SidebarPanel::status && state.selected_row == 5) {
            state.advanced_visible = !state.advanced_visible;
            rebuild_ui(state);
        }
    }
}

void build_scan_panel(UiState& state)
{
    setup_panel_column(state.panel_body);
    value_row(state, "Channels", "OpenHD [1-7]");
    value_row(state, "Bandwidth", std::to_string(state.mavlink.channel_width_mhz) + " MHz");

    auto* scan = action_button(state, "START SCAN");
    lv_obj_add_event_cb(
        scan,
        [](lv_event_t* event) {
            auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
            send_mavlink_action(*state, glide::mavlink::format_action_command("scan", "bands=openhd width=" + std::to_string(state->mavlink.channel_width_mhz)));
        },
        LV_EVENT_CLICKED,
        &state);

    auto* progress_row = lv_obj_create(state.panel_body);
    set_panel_style(progress_row, 0x0b1722, LV_OPA_TRANSP);
    lv_obj_set_size(progress_row, LV_PCT(100), 70);
    state.scan_bar = lv_bar_create(progress_row);
    lv_obj_set_size(state.scan_bar, 300, 8);
    lv_obj_align(state.scan_bar, LV_ALIGN_CENTER, -42, 0);
    lv_bar_set_value(state.scan_bar, state.mavlink.scan_progress, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(state.scan_bar, color(0x203040), 0);
    lv_obj_set_style_bg_color(state.scan_bar, color(0x20b383), LV_PART_INDICATOR);
    state.scan_percent = label(progress_row, (std::to_string(state.mavlink.scan_progress) + "%").c_str(), &lv_font_montserrat_28);
    lv_obj_align(state.scan_percent, LV_ALIGN_CENTER, 210, 0);
}

void build_misc_panel(UiState& state)
{
    setup_panel_column(state.panel_body);

    value_row(state, "Air WiFi Mode", state.mavlink.air_wifi_mode);
    value_row(state, "Ground WiFi Mode", state.mavlink.ground_wifi_mode);
    value_row(state, "Air Hotspot", state.mavlink.air_hotspot);
    value_row(state, "Ground Hotspot", state.mavlink.ground_hotspot);

    auto* section = label(state.panel_body, "Video Plane FPS", &lv_font_montserrat_28);
    lv_obj_set_width(section, LV_PCT(100));

    auto* row = lv_obj_create(state.panel_body);
    set_panel_style(row, 0x162332);
    lv_obj_set_size(row, LV_PCT(100), 82);
    lv_obj_set_style_pad_left(row, 28, 0);
    lv_obj_set_style_pad_right(row, 28, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    state.fps_label = label(row, "KMS video plane FPS enabled", &lv_font_montserrat_22);
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

void build_link_panel(UiState& state)
{
    setup_panel_column(state.panel_body);
    value_row(state, "Frequency", std::to_string(state.mavlink.frequency_mhz) + " MHz");
    value_row(state, "Channel Width", std::to_string(state.mavlink.channel_width_mhz) + " MHz");
    value_row(state, "Modulation", "MCS " + std::to_string(state.mavlink.mcs_index));
    value_row(state, "TX Power", std::to_string(state.mavlink.tx_power_mw) + " mW");
    auto* pit = action_button(state, "TOGGLE PIT MODE");
    lv_obj_add_event_cb(
        pit,
        [](lv_event_t* event) {
            auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
            send_mavlink_action(*state, glide::mavlink::format_action_set_param("air", "PIT_MODE", "toggle"));
        },
        LV_EVENT_CLICKED,
        &state);
}

void build_video_panel(UiState& state)
{
    setup_panel_column(state.panel_body);
    value_row(state, "Resolution", state.mavlink.resolution_fps);
    value_row(state, "Rotation", state.mavlink.rotation);
    value_row(state, "Codec", "H.264 RTP");
    value_row(state, "KMS Plane", "DMABUF scanout");
}

void build_camera_panel(UiState& state)
{
    setup_panel_column(state.panel_body);
    value_row(state, "Brightness", "0");
    value_row(state, "Saturation", "0");
    value_row(state, "Contrast", "0");
    value_row(state, "Sharpness", "0");
}

void build_recording_panel(UiState& state)
{
    setup_panel_column(state.panel_body);
    value_row(state, "Recording", state.mavlink.recording);
    value_row(state, "Status", state.mavlink.recording_status);
    auto* button = action_button(state, "TOGGLE AIR RECORDING");
    lv_obj_add_event_cb(
        button,
        [](lv_event_t* event) {
            auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
            send_mavlink_action(*state, glide::mavlink::format_action_set_param("camera1", "AIR_RECORDING_E", "toggle"));
        },
        LV_EVENT_CLICKED,
        &state);
}

void build_rc_panel(UiState& state)
{
    setup_panel_column(state.panel_body);
    value_row(state, "Joystick", "Disabled");
    for (int i = 0; i < 4; ++i) {
        value_row(state, ("CH" + std::to_string(i + 1)).c_str(), std::to_string(state.mavlink.rc_channels[i]) + " us");
    }
}

void build_status_panel(UiState& state)
{
    setup_panel_column(state.panel_body, 12);
    const auto connection = state.mavlink.air_alive && state.mavlink.ground_alive
        ? std::string("Connected")
        : (state.mavlink.air_alive ? std::string("AIR only") : (state.mavlink.ground_alive ? std::string("GND only") : std::string("Not connected")));
    const auto connection_color = state.mavlink.air_alive && state.mavlink.ground_alive ? 0x20b383 : 0xdf4c7c;
    value_row(state, "Connection", connection, connection_color);
    value_row(state, "OpenHD Version", state.mavlink.openhd_version, state.mavlink.openhd_version == "N/A" ? 0xdf4c7c : 0x20b383);
    value_row(state, "Chipset GND", state.mavlink.ground_chipset, state.mavlink.ground_alive ? 0x20b383 : 0xb3c6d6);
    value_row(state, "Chipset AIR", state.mavlink.air_chipset, state.mavlink.air_alive ? 0x20b383 : 0xdf4c7c);
    value_row(state, "Camera", state.mavlink.camera, state.mavlink.air_alive ? 0x20b383 : 0xdf4c7c);

    auto* advanced = action_button(state, state.advanced_visible ? "HIDE MAVLINK MESSAGES" : "SHOW MAVLINK MESSAGES");
    lv_obj_add_event_cb(
        advanced,
        [](lv_event_t* event) {
            auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
            state->advanced_visible = !state->advanced_visible;
            set_active_panel(*state, SidebarPanel::status);
        },
        LV_EVENT_CLICKED,
        &state);

    if (state.advanced_visible) {
        for (std::size_t i = 0; i < state.mavlink.message_count; ++i) {
            value_row(state, "MAVLink", state.mavlink.messages[i], 0xd7e3ec);
        }
    }
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
    state.scan_bar = nullptr;
    state.scan_percent = nullptr;
    state.row_count = 0;
    state.selected_row = std::max(0, state.selected_row);
}

void set_active_panel(UiState& state, SidebarPanel panel)
{
    state.active_panel = panel;
    state.selected_row = 0;
    state.focus_panel = false;
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
    } else if (panel == SidebarPanel::link) {
        build_link_panel(state);
    } else if (panel == SidebarPanel::video) {
        build_video_panel(state);
    } else if (panel == SidebarPanel::camera) {
        build_camera_panel(state);
    } else if (panel == SidebarPanel::recording) {
        build_recording_panel(state);
    } else if (panel == SidebarPanel::rc) {
        build_rc_panel(state);
    } else if (panel == SidebarPanel::misc) {
        build_misc_panel(state);
    } else if (panel == SidebarPanel::status) {
        build_status_panel(state);
    } else {
        build_placeholder_panel(state);
    }
}

void nav_clicked(lv_event_t* event)
{
    auto* state = static_cast<UiState*>(lv_event_get_user_data(event));
    auto index = static_cast<int>(reinterpret_cast<std::intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    if (index <= 0) {
        state->expanded = false;
        rebuild_ui(*state);
        return;
    }
    set_active_panel(*state, static_cast<SidebarPanel>(index - 1));
}

void build_sidebar(UiState& state, std::uint32_t width, std::uint32_t height)
{
    auto* screen = lv_screen_active();
    state.root = screen;
    set_panel_style(screen, 0x081018);

    auto* rail = lv_obj_create(screen);
    const int rail_width = state.expanded ? 188 : 56;
    set_panel_style(rail, 0x07111a);
    lv_obj_set_size(rail, rail_width, height);
    lv_obj_align(rail, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(rail, 10, 0);
    lv_obj_set_style_pad_row(rail, 4, 0);

    if (state.expanded) {
        auto* usage = label(rail, "USAGE EXAMPLE", &lv_font_montserrat_12, 0xb9c7d3);
        lv_obj_set_style_text_letter_space(usage, 0, 0);
        lv_obj_set_height(usage, 26);
        auto* brand = label(rail, "OPENHD", &lv_font_montserrat_12, 0xffffff);
        lv_obj_set_height(brand, 22);
    }

    for (int i = 0; i < 9; ++i) {
        auto* button = lv_button_create(rail);
        state.nav_buttons[i] = button;
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<std::intptr_t>(i)));
        lv_obj_set_size(button, LV_PCT(100), 34);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        const bool checked = state.expanded && i > 0 && static_cast<int>(state.active_panel) == i - 1 && !state.focus_panel;
        lv_obj_set_style_bg_color(button, color(checked ? 0x172839 : 0x07111a), 0);
        lv_obj_set_style_bg_color(button, color(0x1c3447), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, color(0x243d52), LV_STATE_PRESSED);
        lv_obj_add_event_cb(button, nav_clicked, LV_EVENT_CLICKED, &state);

        auto* row = lv_obj_create(button);
        set_panel_style(row, 0x000000, LV_OPA_TRANSP);
        lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(row, state.expanded ? 8 : 0, 0);
        lv_obj_set_style_pad_column(row, 8, 0);

        auto* icon = label(row, nav_symbol(i), &lv_font_montserrat_16, i == 0 ? 0x8da2b2 : 0xd5e0e8);
        lv_obj_set_width(icon, state.expanded ? 20 : LV_PCT(100));
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        if (state.expanded) {
            auto* text = label(row, nav_text(i), &lv_font_montserrat_12, checked ? 0xffffff : 0x9fb0bd);
            lv_obj_set_width(text, LV_PCT(70));
        }
    }

    if (!state.expanded) {
        return;
    }

    auto* panel = lv_obj_create(screen);
    set_panel_style(panel, 0x0c1924);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, color(state.focus_panel ? 0x2f7fa5 : 0x152837), 0);
    lv_obj_set_size(panel, static_cast<int>(width) - rail_width - 18, static_cast<int>(height) - 20);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, rail_width + 10, 10);

    auto* header = lv_obj_create(panel);
    set_panel_style(header, 0x0c1924, LV_OPA_TRANSP);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    state.panel_title = label(header, "Find Air Unit", &lv_font_montserrat_20);
    lv_obj_align(state.panel_title, LV_ALIGN_LEFT_MID, 16, 0);

    state.panel_body = lv_obj_create(panel);
    set_panel_style(state.panel_body, 0x0c1924, LV_OPA_TRANSP);
    lv_obj_set_size(state.panel_body, LV_PCT(100), static_cast<int>(height) - 82);
    lv_obj_align(state.panel_body, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    const auto active = state.active_panel;
    lv_label_set_text(state.panel_title, panel_title(active));
    clear_panel(state);
    if (active == SidebarPanel::scan) {
        build_scan_panel(state);
    } else if (active == SidebarPanel::link) {
        build_link_panel(state);
    } else if (active == SidebarPanel::video) {
        build_video_panel(state);
    } else if (active == SidebarPanel::camera) {
        build_camera_panel(state);
    } else if (active == SidebarPanel::recording) {
        build_recording_panel(state);
    } else if (active == SidebarPanel::rc) {
        build_rc_panel(state);
    } else if (active == SidebarPanel::misc) {
        build_misc_panel(state);
    } else if (active == SidebarPanel::status) {
        build_status_panel(state);
    }
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
        } else if (glide::mavlink::apply_ipc_line(state.mavlink, line)) {
            set_active_panel(state, state.active_panel);
        } else {
            apply_terminal_key(state, line);
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

    if (!options.preview && !options.buffer) {
        return 0;
    }

    set_sdl_position(options);
    lv_init();
    BufferDisplay buffer_display;
#if OPENHD_GLIDE_HAS_LVGL_SDL
    if (options.preview) {
        auto* display = lv_sdl_window_create(static_cast<int32_t>(options.width), static_cast<int32_t>(options.height));
        lv_sdl_window_set_title(display, "GlideUI LVGL Preview");
        lv_sdl_window_set_resizeable(display, false);
        lv_sdl_mouse_create();
        lv_sdl_keyboard_create();
    }
#else
    if (options.preview) {
        glide::log(glide::LogLevel::error, "GlideUI", "LVGL SDL preview requires SDL2 development files; use --buffer on device builds");
        return 1;
    }
#endif
    if (options.buffer) {
        if (!create_buffer_display(buffer_display, options)) {
            return 1;
        }
        auto* display = lv_display_create(static_cast<int32_t>(options.width), static_cast<int32_t>(options.height));
        lv_display_set_user_data(display, &buffer_display);
        lv_display_set_flush_cb(display, buffer_flush);
        lv_display_set_buffers(
            display,
            buffer_display.draw_buffer.data(),
            nullptr,
            buffer_display.draw_buffer.size() * sizeof(std::uint32_t),
            LV_DISPLAY_RENDER_MODE_PARTIAL);
        glide::log(glide::LogLevel::info, "GlideUI", "LVGL buffer backend writing " + options.buffer_path);
    }

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

#if OPENHD_GLIDE_HAS_LVGL_SDL
    if (options.preview) {
        lv_sdl_quit();
    }
#endif
    close_buffer_display(buffer_display);
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

