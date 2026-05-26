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

namespace glide::preview_control {

bool fps_overlay_enabled();
void set_fps_overlay_enabled(bool enabled);
bool coordinates_overlay_enabled();
void set_coordinates_overlay_enabled(bool enabled);
bool compact_readouts_enabled();
void set_compact_readouts_enabled(bool enabled);
std::string osd_layout();
void set_osd_layout(const std::string& layout);
std::uint32_t theme_color(const std::string& key);
void set_theme_color(const std::string& key, std::uint32_t rgb);

} // namespace glide::preview_control
