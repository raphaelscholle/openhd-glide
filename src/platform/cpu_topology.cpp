#include "platform/cpu_topology.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

namespace glide::platform {
namespace {

std::optional<std::uint64_t> read_u64_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::uint64_t value {};
    if (!(file >> value)) {
        return std::nullopt;
    }

    return value;
}

std::optional<unsigned int> read_uint_file(const std::filesystem::path& path)
{
    const auto value = read_u64_file(path);
    if (!value) {
        return std::nullopt;
    }

    return static_cast<unsigned int>(*value);
}

bool is_cpu_directory(const std::filesystem::directory_entry& entry)
{
    std::error_code error;
    if (!entry.is_directory(error) || error) {
        return false;
    }

    const auto name = entry.path().filename().string();
    if (name.size() <= 3 || name.rfind("cpu", 0) != 0) {
        return false;
    }

    return std::all_of(name.begin() + 3, name.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

CpuCoreInfo probe_linux_cpu(const std::filesystem::path& cpu_path)
{
    const auto name = cpu_path.filename().string();
    CpuCoreInfo info {};
    info.logical_id = static_cast<unsigned int>(std::stoul(name.substr(3)));
    info.physical_package_id = read_uint_file(cpu_path / "topology" / "physical_package_id");
    info.core_id = read_uint_file(cpu_path / "topology" / "core_id");
    info.current_frequency_khz = read_u64_file(cpu_path / "cpufreq" / "scaling_cur_freq");
    info.max_frequency_khz = read_u64_file(cpu_path / "cpufreq" / "cpuinfo_max_freq");

    if (const auto online = read_u64_file(cpu_path / "online")) {
        info.online = *online != 0;
    }

    return info;
}

CpuTopology fallback_topology()
{
    CpuTopology topology;
    const auto count = std::max(1u, std::thread::hardware_concurrency());
    topology.cores.reserve(count);

    for (unsigned int i = 0; i < count; ++i) {
        topology.cores.push_back(CpuCoreInfo {
            .logical_id = i,
            .physical_package_id = std::nullopt,
            .core_id = std::nullopt,
            .current_frequency_khz = std::nullopt,
            .max_frequency_khz = std::nullopt,
            .online = true,
        });
    }

    return topology;
}

std::string format_frequency(std::optional<std::uint64_t> khz)
{
    if (!khz) {
        return "unknown";
    }

    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(2);
    stream << static_cast<double>(*khz) / 1000000.0 << " GHz";
    return stream.str();
}

} // namespace

CpuTopology probe_cpu_topology()
{
#ifdef __linux__
    const std::filesystem::path cpu_root { "/sys/devices/system/cpu" };
    CpuTopology topology;
    std::error_code error;

    if (std::filesystem::exists(cpu_root, error) && !error) {
        for (const auto& entry : std::filesystem::directory_iterator(
                 cpu_root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error)) {
            if (is_cpu_directory(entry)) {
                topology.cores.push_back(probe_linux_cpu(entry.path()));
            }
        }
    }

    std::sort(topology.cores.begin(), topology.cores.end(), [](const auto& left, const auto& right) {
        return left.logical_id < right.logical_id;
    });

    if (!topology.cores.empty()) {
        return topology;
    }
#endif

    return fallback_topology();
}

std::string describe_cpu_topology(const CpuTopology& topology)
{
    std::ostringstream stream;
    stream << "CPU topology: " << topology.cores.size() << " logical core(s)\n";

    for (const auto& core : topology.cores) {
        stream << "  cpu" << core.logical_id
               << " online=" << (core.online ? "yes" : "no")
               << " package=" << (core.physical_package_id ? std::to_string(*core.physical_package_id) : "unknown")
               << " core=" << (core.core_id ? std::to_string(*core.core_id) : "unknown")
               << " current=" << format_frequency(core.current_frequency_khz)
               << " max=" << format_frequency(core.max_frequency_khz)
               << '\n';
    }

    return stream.str();
}

} // namespace glide::platform
