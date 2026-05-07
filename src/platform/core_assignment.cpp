#include "platform/core_assignment.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>

namespace glide::platform {
namespace {

std::uint64_t ranking_frequency(const CpuCoreInfo& core)
{
    if (core.max_frequency_khz) {
        return *core.max_frequency_khz;
    }

    if (core.current_frequency_khz) {
        return *core.current_frequency_khz;
    }

    return 0;
}

std::vector<CpuCoreInfo> online_cores(const CpuTopology& topology, CoreAssignmentOptions options)
{
    std::vector<CpuCoreInfo> cores;

    for (const auto& core : topology.cores) {
        if (core.online && !(options.avoid_core0 && core.logical_id == 0)) {
            cores.push_back(core);
        }
    }

    std::sort(cores.begin(), cores.end(), [](const auto& left, const auto& right) {
        const auto left_frequency = ranking_frequency(left);
        const auto right_frequency = ranking_frequency(right);
        if (left_frequency != right_frequency) {
            return left_frequency > right_frequency;
        }

        return left.logical_id < right.logical_id;
    });

    return cores;
}

} // namespace

std::vector<WorkerCoreAssignment> assign_worker_cores(const CpuTopology& topology)
{
    return assign_worker_cores(topology, CoreAssignmentOptions {});
}

std::vector<WorkerCoreAssignment> assign_worker_cores(
    const CpuTopology& topology,
    CoreAssignmentOptions options)
{
    auto cores = online_cores(topology, options);
    if (cores.empty() && options.avoid_core0) {
        cores = online_cores(topology, CoreAssignmentOptions {});
    }

    std::vector<WorkerCoreAssignment> assignments;

    if (cores.empty()) {
        return assignments;
    }

    const auto view_core = cores.front().logical_id;
    assignments.push_back(WorkerCoreAssignment {
        .worker_name = "glide-view",
        .logical_core = view_core,
        .shares_core = false,
        .reason = options.avoid_core0 && view_core != 0
            ? "highest priority worker on fastest online non-core0 because OpenHD is running"
            : "highest priority worker on fastest online core",
    });

    if (cores.size() == 1) {
        assignments.push_back(WorkerCoreAssignment {
            .worker_name = "glide-flow",
            .logical_core = view_core,
            .shares_core = true,
            .reason = "no non-view core available; single-core systems are not a target",
        });
        assignments.push_back(WorkerCoreAssignment {
            .worker_name = "glide-ui",
            .logical_core = view_core,
            .shares_core = true,
            .reason = "no non-view core available; single-core systems are not a target",
        });
        return assignments;
    }

    const auto flow_core = cores[1].logical_id;
    assignments.push_back(WorkerCoreAssignment {
        .worker_name = "glide-flow",
        .logical_core = flow_core,
        .shares_core = false,
        .reason = "second priority worker on next fastest online core",
    });

    if (cores.size() >= 3) {
        const auto ui_core = cores[2].logical_id;
        assignments.push_back(WorkerCoreAssignment {
            .worker_name = "glide-ui",
            .logical_core = ui_core,
            .shares_core = false,
            .reason = "lowest priority worker, but enough cores are available for a fast isolated core",
        });
    } else {
        assignments.push_back(WorkerCoreAssignment {
            .worker_name = "glide-ui",
            .logical_core = flow_core,
            .shares_core = true,
            .reason = "only one non-view core available; sharing with glide-flow keeps glide-view isolated",
        });
    }

    return assignments;
}

std::string describe_worker_core_assignments(const std::vector<WorkerCoreAssignment>& assignments)
{
    std::ostringstream stream;
    stream << "Worker CPU assignments: " << assignments.size() << " worker(s)\n";

    for (const auto& assignment : assignments) {
        stream << "  " << assignment.worker_name
               << " -> cpu" << assignment.logical_core
               << " shared=" << (assignment.shares_core ? "yes" : "no")
               << " reason=\"" << assignment.reason << "\"\n";
    }

    return stream.str();
}

} // namespace glide::platform
