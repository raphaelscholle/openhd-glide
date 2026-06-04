/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * © OpenHD, All Rights Reserved.
 ******************************************************************************/

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
    int channel_width_mhz {};
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
    void draw_bottom(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample, const OsdTheme& theme) const;
};

} // namespace glide::flow
