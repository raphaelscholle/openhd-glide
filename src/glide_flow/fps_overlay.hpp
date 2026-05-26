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

#include <cstdint>
#include <string>

namespace glide::flow {

struct SurfaceSize {
    std::uint32_t width {};
    std::uint32_t height {};
};

struct TextPlacement {
    std::string text;
    float x {};
    float y {};
    float scale {};
};

class FpsOverlay {
public:
    TextPlacement layout(double fps, SurfaceSize surface) const;

private:
    static constexpr float margin_ = 18.0F;
    static constexpr float scale_ = 22.0F;
};

} // namespace glide::flow
