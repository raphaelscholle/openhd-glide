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
#include <optional>
#include <string>
#include <vector>

namespace glide::platform {

struct DrmPlaneInfo {
    std::uint32_t id {};
    std::uint32_t possible_crtcs {};
    std::optional<std::uint32_t> current_crtc_id;
    std::optional<std::int32_t> x;
    std::optional<std::int32_t> y;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    std::vector<std::uint32_t> formats;
};

struct DrmDeviceInfo {
    std::string path;
    bool available { false };
    std::string status;
    std::vector<DrmPlaneInfo> planes;
};

struct DrmProbeResult {
    std::vector<DrmDeviceInfo> devices;
};

DrmProbeResult probe_drm_planes();
std::string describe_drm_probe(const DrmProbeResult& result);

} // namespace glide::platform
