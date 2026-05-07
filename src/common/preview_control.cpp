#include "common/preview_control.hpp"

#include <filesystem>
#include <fstream>

namespace glide::preview_control {
namespace {

std::filesystem::path control_path()
{
    return std::filesystem::temp_directory_path() / "openhd-glide-flow-fps.enabled";
}

} // namespace

bool fps_overlay_enabled()
{
    std::ifstream file(control_path());
    if (!file) {
        return true;
    }

    char value {};
    file >> value;
    return value != '0';
}

void set_fps_overlay_enabled(bool enabled)
{
    std::ofstream file(control_path(), std::ios::trunc);
    file << (enabled ? '1' : '0') << '\n';
}

} // namespace glide::preview_control
