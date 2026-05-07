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
