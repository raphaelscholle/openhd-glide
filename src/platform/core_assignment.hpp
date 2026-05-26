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

#include "platform/cpu_topology.hpp"

#include <string>
#include <vector>

namespace glide::platform {

struct WorkerCoreAssignment {
    std::string worker_name;
    unsigned int logical_core {};
    bool shares_core {};
    std::string reason;
};

struct CoreAssignmentOptions {
    bool avoid_core0 {};
};

std::vector<WorkerCoreAssignment> assign_worker_cores(
    const CpuTopology& topology,
    CoreAssignmentOptions options = {});
std::string describe_worker_core_assignments(const std::vector<WorkerCoreAssignment>& assignments);

} // namespace glide::platform
