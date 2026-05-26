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

#include "glide_flow/simulated_attitude.hpp"

#include <cmath>

namespace glide::flow {

AttitudeSample SimulatedAttitude::sample(std::chrono::steady_clock::time_point now) const
{
    constexpr float pi = 3.14159265358979323846F;
    const auto seconds = std::chrono::duration<float>(now - start_).count();

    return AttitudeSample {
        .roll_degrees = std::sin(seconds * 0.75F) * 28.0F,
        .pitch_degrees = std::sin((seconds * 0.52F) + (pi * 0.35F)) * 8.0F,
    };
}

} // namespace glide::flow
