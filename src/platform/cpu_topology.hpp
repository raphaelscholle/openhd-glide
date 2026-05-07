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

