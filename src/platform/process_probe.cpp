#include "platform/process_probe.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace glide::platform {
namespace {

bool is_numeric_name(const std::string& name)
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

std::string read_first_line(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

bool is_relevant_openhd_name(const std::string& name)
{
    if (name == "openhd-glide") {
        return false;
    }

    return name == "openhd" || name.rfind("openhd-", 0) == 0 || name.rfind("OpenHD", 0) == 0;
}

std::string read_cmdline_argv0(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::string value;
    std::getline(file, value, '\0');

    const auto slash = value.find_last_of('/');
    if (slash == std::string::npos) {
        return value;
    }

    return value.substr(slash + 1);
}

} // namespace

bool is_openhd_process_running()
{
#ifdef __linux__
    const std::filesystem::path proc_root { "/proc" };
    std::error_code error;

    if (!std::filesystem::exists(proc_root, error) || error) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(
             proc_root,
             std::filesystem::directory_options::skip_permission_denied,
             error)) {
        const auto pid = entry.path().filename().string();
        if (!is_numeric_name(pid)) {
            continue;
        }

        const auto command_name = read_first_line(entry.path() / "comm");
        if (is_relevant_openhd_name(command_name)) {
            return true;
        }

        const auto argv0 = read_cmdline_argv0(entry.path() / "cmdline");
        if (is_relevant_openhd_name(argv0)) {
            return true;
        }
    }
#endif

    return false;
}

} // namespace glide::platform
