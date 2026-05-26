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

struct CpuCoreInfo {
    unsigned int logical_id {};
    std::optional<unsigned int> physical_package_id;
    std::optional<unsigned int> core_id;
    std::optional<std::uint64_t> current_frequency_khz;
    std::optional<std::uint64_t> max_frequency_khz;
    bool online { true };
};

struct CpuTopology {
    std::vector<CpuCoreInfo> cores;
};

CpuTopology probe_cpu_topology();
std::string describe_cpu_topology(const CpuTopology& topology);

} // namespace glide::platform
