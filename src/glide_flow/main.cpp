#include "common/logging.hpp"
#include "common/ipc.hpp"
#include "common/mavlink_state.hpp"
#include "common/preview_control.hpp"
#include "dev/kms_gles_window.hpp"
#include "dev/sdl_gles_window.hpp"
#include "glide_flow/altitude_widget.hpp"
#include "glide_flow/fps_counter.hpp"
#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"
#include "glide_flow/link_overview.hpp"
#include "glide_flow/performance_horizon.hpp"
#include "glide_flow/rocket_osd.hpp"
#include "glide_flow/simulated_attitude.hpp"
#include "glide_flow/speed_widget.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
    stop_requested = 1;
}

struct Options {
    glide::flow::SurfaceSize surface {
        .width = 1920,
        .height = 1080,
    };
    int x {};
    int y {};
    bool render_gles {};
    bool preview {};
    bool kms {};
    bool stay_alive {};
    bool positioned {};
    bool borderless {};
    std::uint32_t display_refresh_hz {};
    std::string ipc_socket { glide::ipc::default_socket_path };
};

glide::flow::SurfaceSize parse_surface_size(int argc, char** argv)
{
    glide::flow::SurfaceSize surface {
        .width = 1920,
        .height = 1080,
    };

    for (int i = 1; i + 1 < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--width") {
            surface.width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--height") {
            surface.height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        }
    }

    return surface;
}

Options parse_options(int argc, char** argv)
{
    Options options;
    options.surface = parse_surface_size(argc, argv);

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--render-gles") {
            options.render_gles = true;
        } else if (argument == "--preview") {
            options.preview = true;
            options.render_gles = true;
        } else if (argument == "--kms" || argument == "--kmd") {
            options.kms = true;
        } else if (argument == "--stay-alive") {
            options.stay_alive = true;
        } else if (argument == "--x" && i + 1 < argc) {
            options.x = std::stoi(argv[++i]);
            options.positioned = true;
        } else if (argument == "--y" && i + 1 < argc) {
            options.y = std::stoi(argv[++i]);
            options.positioned = true;
        } else if (argument == "--borderless") {
            options.borderless = true;
        } else if (argument == "--display-refresh-hz" && i + 1 < argc) {
            options.display_refresh_hz = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--ipc-socket" && i + 1 < argc) {
            options.ipc_socket = argv[++i];
        }
    }

    return options;
}

std::string describe_placement(const glide::flow::TextPlacement& placement)
{
    std::ostringstream stream;
    stream << "FPS overlay '" << placement.text << "' at x=" << placement.x
           << " y=" << placement.y << " scale=" << placement.scale;
    return stream.str();
}

} // namespace

int main(int argc, char** argv)
{
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    auto options = parse_options(argc, argv);
    glide::flow::FpsCounter fps_counter;
    glide::flow::FpsOverlay fps_overlay;
    glide::flow::GlesTextRenderer renderer;
    glide::flow::AltitudeWidgetRenderer altitude_widget;
    glide::flow::SimulatedAltitude simulated_altitude;
    glide::flow::SpeedWidgetRenderer speed_widget;
    glide::flow::SimulatedSpeed simulated_speed;
    glide::flow::LinkOverviewRenderer link_overview;
    glide::flow::SimulatedLinkOverview simulated_link;
    glide::flow::PerformanceHorizon performance_horizon;
    glide::flow::SimulatedAttitude simulated_attitude;
    glide::flow::RocketOsdRenderer rocket_osd;
    glide::flow::SimulatedRocketOsd simulated_rocket;
    glide::dev::KmsGlesWindow kms_window;
    glide::dev::SdlGlesWindow preview_window;
    glide::ipc::Client ipc;
    glide::mavlink::Snapshot mavlink;
    bool coordinates_enabled = glide::preview_control::coordinates_overlay_enabled();
    bool compact_readouts = glide::preview_control::compact_readouts_enabled();
    std::string osd_layout = glide::preview_control::osd_layout();
    constexpr bool fps_overlay_enabled = false;

    if (options.preview) {
        if (!preview_window.create("GlideFlow Preview", glide::dev::WindowPlacement {
                .width = options.surface.width,
                .height = options.surface.height,
                .x = options.x,
                .y = options.y,
                .positioned = options.positioned,
                .borderless = options.borderless,
            })) {
            glide::log(glide::LogLevel::error, "GlideFlow", preview_window.last_error());
            return 1;
        }
        options.surface = preview_window.surface_size();
    }
    if (options.kms) {
#if OPENHD_GLIDE_DEVICE_KMS
        glide::log(glide::LogLevel::info, "GlideFlow", "DRM/KMS mode requested");
        if (!kms_window.create(options.surface.width, options.surface.height, options.display_refresh_hz)) {
            glide::log(glide::LogLevel::error, "GlideFlow", kms_window.last_error());
            return 1;
        }
        options.surface = kms_window.surface_size();
        options.render_gles = true;
        glide::log(
            glide::LogLevel::info,
            "GlideFlow",
            "DRM/KMS surface ready " + std::to_string(options.surface.width) + "x" + std::to_string(options.surface.height));
        glide::log(glide::LogLevel::info, "GlideFlow", renderer.runtime_description());
        if (renderer.likely_software_renderer()) {
            glide::log(glide::LogLevel::warning, "GlideFlow", "OpenGL ES renderer looks like a software fallback");
        } else {
            glide::log(glide::LogLevel::info, "GlideFlow", "OpenGL ES renderer appears hardware accelerated");
        }
#else
        glide::log(glide::LogLevel::error, "GlideFlow", "DRM/KMS mode is disabled in this build");
        return 1;
#endif
    }

    glide::log(glide::LogLevel::info, "GlideFlow", "OSD renderer started");
    if (ipc.connect_to(options.ipc_socket)) {
        ipc.send_line("hello glide-flow");
        ipc.send_line("status glide-flow ready");
    } else {
        glide::log(glide::LogLevel::warning, "GlideFlow", "IPC controller unavailable; using preview fallback state");
    }
    if (options.render_gles && !renderer.available()) {
        glide::log(glide::LogLevel::warning, "GlideFlow", "OpenGL ES renderer unavailable; running layout path only");
    } else if (options.preview && options.render_gles) {
        glide::log(glide::LogLevel::info, "GlideFlow", renderer.runtime_description());
        if (renderer.likely_software_renderer()) {
            glide::log(glide::LogLevel::warning, "GlideFlow", "OpenGL ES renderer looks like a software fallback");
        } else {
            glide::log(glide::LogLevel::info, "GlideFlow", "OpenGL ES renderer appears hardware accelerated");
        }
    }

    glide::flow::TextPlacement placement = fps_overlay.layout(0.0, options.surface);

    constexpr auto preview_frame_time = std::chrono::microseconds(16667);

    for (unsigned int frame = 0; stop_requested == 0 && (options.preview || options.stay_alive || frame < 180); ++frame) {
        const auto frame_start = std::chrono::steady_clock::now();

        if (options.preview && !preview_window.poll()) {
            break;
        }
        if (ipc.connected()) {
            for (const auto& line : ipc.poll_lines()) {
                if (line == "state coords 0" || line == "state coords 1") {
                    coordinates_enabled = line.back() == '1';
                    glide::preview_control::set_coordinates_overlay_enabled(coordinates_enabled);
                } else if (line == "state compact 0" || line == "state compact 1") {
                    compact_readouts = line.back() == '1';
                    glide::preview_control::set_compact_readouts_enabled(compact_readouts);
                } else if (line == "state osd drone" || line == "state osd rocket") {
                    osd_layout = line.substr(10);
                    glide::preview_control::set_osd_layout(osd_layout);
                } else {
                    glide::mavlink::apply_ipc_line(mavlink, line);
                }
            }
        } else {
            coordinates_enabled = glide::preview_control::coordinates_overlay_enabled();
            compact_readouts = glide::preview_control::compact_readouts_enabled();
            osd_layout = glide::preview_control::osd_layout();
        }

        options.surface = options.preview ? preview_window.surface_size() : options.surface;
        const auto fps = fps_counter.frame();
        if (fps) {
            placement = fps_overlay.layout(*fps, options.surface);
            std::cout << describe_placement(placement) << '\n';
            if (ipc.connected()) {
                ipc.send_line("status glide-flow fps " + std::to_string(*fps));
            }
        }

        if (options.render_gles && renderer.available()) {
            renderer.clear(0.02F, 0.02F, 0.025F, 1.0F, options.surface);
            if (osd_layout == "rocket") {
                rocket_osd.draw(renderer, options.surface, simulated_rocket.sample());
            } else {
                auto link_sample = simulated_link.sample();
                link_sample.show_coordinates = coordinates_enabled;
                link_sample.armed = mavlink.armed;
                if (mavlink.flight_mode != "N/A") {
                    link_sample.flight_mode = mavlink.flight_mode.c_str();
                }
                if (mavlink.position_valid) {
                    link_sample.latitude_deg = mavlink.latitude_deg;
                    link_sample.longitude_deg = mavlink.longitude_deg;
                    link_sample.height_m = mavlink.altitude_m;
                }
                if (mavlink.speed_valid) {
                    link_sample.air_speed_mps = mavlink.airspeed_mps > 0.0F ? mavlink.airspeed_mps : mavlink.ground_speed_mps;
                }
                if (mavlink.battery_valid) {
                    link_sample.air_voltage_v = mavlink.voltage_v;
                }
                if (mavlink.satellites > 0) {
                    link_sample.satellites = mavlink.satellites;
                }
                link_overview.draw(renderer, options.surface, link_sample);
                const auto attitude_sample = mavlink.attitude_valid
                    ? glide::flow::AttitudeSample { .roll_degrees = mavlink.roll_degrees, .pitch_degrees = mavlink.pitch_degrees }
                    : simulated_attitude.sample();
                performance_horizon.draw(
                    renderer,
                    options.surface,
                    attitude_sample,
                    glide::flow::WindSample {
                        .direction_degrees = link_sample.wind_direction_deg,
                        .speed_mps = link_sample.wind_speed_mps,
                        .valid = true,
                    });
                const auto speed_sample = mavlink.speed_valid
                    ? glide::flow::SpeedSample { .speed_mps = mavlink.ground_speed_mps }
                    : simulated_speed.sample();
                const auto altitude_sample = mavlink.altitude_valid
                    ? glide::flow::AltitudeSample { .altitude_m = mavlink.altitude_m }
                    : simulated_altitude.sample();
                speed_widget.draw(renderer, options.surface, speed_sample, compact_readouts);
                altitude_widget.draw(renderer, options.surface, altitude_sample, compact_readouts);
            }
            if (fps_overlay_enabled) {
                renderer.draw(placement, options.surface);
            }
            if (mavlink.message_count > 0) {
                const auto& latest = mavlink.messages[mavlink.message_count - 1U];
                renderer.draw(
                    glide::flow::TextPlacement {
                        .text = latest,
                        .x = 28.0F,
                        .y = static_cast<float>(options.surface.height) - 92.0F,
                        .scale = 20.0F,
                    },
                    options.surface);
            }
        }

        if (options.preview) {
            preview_window.swap();
        }
        if (options.kms) {
            kms_window.swap();
        }

        if (options.preview) {
            std::this_thread::sleep_until(frame_start + preview_frame_time);
        } else if (!options.kms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    return 0;
}
