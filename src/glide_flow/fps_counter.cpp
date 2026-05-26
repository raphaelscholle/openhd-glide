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

#include "glide_flow/fps_counter.hpp"

namespace glide::flow {

std::optional<double> FpsCounter::frame(Clock::time_point now)
{
    ++frames_in_window_;

    const auto elapsed = now - window_start_;
    if (elapsed < std::chrono::seconds(1)) {
        return std::nullopt;
    }

    const auto seconds = std::chrono::duration<double>(elapsed).count();
    latest_fps_ = static_cast<double>(frames_in_window_) / seconds;
    frames_in_window_ = 0;
    window_start_ = now;

    return latest_fps_;
}

double FpsCounter::latest_fps() const
{
    return latest_fps_;
}

} // namespace glide::flow
