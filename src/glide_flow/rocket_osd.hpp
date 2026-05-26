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

struct RocketOsdSample {
    float velocity_mps {};
    float altitude_km {};
    float g_force {};
    float fuel_percent {};
    int stage {};
    std::chrono::seconds mission_time {};
    const char* status {};
};

class SimulatedRocketOsd {
public:
    RocketOsdSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class RocketOsdRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const RocketOsdSample& sample, const OsdTheme& theme) const;
};

} // namespace glide::flow
