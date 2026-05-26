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

namespace glide::flow {

struct AttitudeSample {
    float roll_degrees {};
    float pitch_degrees {};
};

struct WindSample {
    float direction_degrees {};
    float speed_mps {};
    bool valid {};
};

class PerformanceHorizon {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, AttitudeSample attitude, WindSample wind = {}, const OsdTheme& theme = OsdTheme {}) const;

private:
    static constexpr float widget_width_ = 250.0F;
    static constexpr float widget_height_ = 48.0F;
    static constexpr float horizon_thickness_ = 4.0F;
    static constexpr float horizon_gap_ = 20.0F;
    static constexpr float pitch_pixels_per_degree_ = 4.0F;
};

} // namespace glide::flow
