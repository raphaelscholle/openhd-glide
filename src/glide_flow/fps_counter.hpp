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

#include <chrono>
#include <optional>

namespace glide::flow {

class FpsCounter {
public:
    using Clock = std::chrono::steady_clock;

    std::optional<double> frame(Clock::time_point now = Clock::now());
    double latest_fps() const;

private:
    Clock::time_point window_start_ { Clock::now() };
    unsigned int frames_in_window_ {};
    double latest_fps_ {};
};

} // namespace glide::flow
