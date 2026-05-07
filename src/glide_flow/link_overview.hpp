#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"

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
    float home_distance_m {};
    int satellites {};
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
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample) const;
};

} // namespace glide::flow
