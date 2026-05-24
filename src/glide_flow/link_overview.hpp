#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"
#include "glide_flow/osd_theme.hpp"

#include <chrono>

namespace glide::flow {

struct LinkOverviewSample {
    int rssi_dbm {};
    int txc_temp_c {};
    int mcs {};
    int downlink_quality {};
    int frequency_mhz {};
    float bitrate_mbit {};
    bool recording {};
    bool uplink_ok {};
    int rc_quality {};
    float ground_voltage_v {};
    int ground_mah {};
    float air_voltage_v {};
    float air_current_a {};
    float air_speed_mps {};
    float height_m {};
    float home_distance_m {};
    float total_distance_m {};
    float wind_speed_mps {};
    float wind_direction_deg {};
    double latitude_deg {};
    double longitude_deg {};
    float mah_per_km {};
    int satellites {};
    bool show_coordinates { true };
    std::chrono::seconds flight_time {};
    bool armed {};
    const char* flight_mode {};
};

class SimulatedLinkOverview {
public:
    LinkOverviewSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class LinkOverviewRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample, const OsdTheme& theme) const;
    void draw_top(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample, const OsdTheme& theme) const;
};

} // namespace glide::flow
