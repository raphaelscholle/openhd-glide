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

#include <array>
#include <chrono>

namespace glide::flow {

struct NavalRadarContact {
    float bearing_degrees {};
    float range_normalized {};
    bool ship {};
    bool selected {};
};

struct NavalOsdSample {
    float heading_degrees {};
    std::array<NavalRadarContact, 7> contacts {};
};

class SimulatedNavalOsd {
public:
    NavalOsdSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class NavalOsdRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const NavalOsdSample& sample, const OsdTheme& theme) const;
};

} // namespace glide::flow
