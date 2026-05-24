#include "common/preview_control.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace glide::preview_control {
namespace {

std::filesystem::path fps_control_path()
{
    return std::filesystem::temp_directory_path() / "openhd-glide-flow-fps.enabled";
}

std::filesystem::path coordinates_control_path()
{
    return std::filesystem::temp_directory_path() / "openhd-glide-flow-coordinates.enabled";
}

std::filesystem::path compact_readouts_control_path()
{
    return std::filesystem::temp_directory_path() / "openhd-glide-flow-compact-readouts.enabled";
}

std::filesystem::path osd_layout_control_path()
{
    return std::filesystem::temp_directory_path() / "openhd-glide-flow-osd.layout";
}

bool read_enabled_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return true;
    }

    char value {};
    file >> value;
    return value != '0';
}

void write_enabled_file(const std::filesystem::path& path, bool enabled)
{
    std::ofstream file(path, std::ios::trunc);
    file << (enabled ? '1' : '0') << '\n';
}

} // namespace

bool fps_overlay_enabled()
{
    return read_enabled_file(fps_control_path());
}

void set_fps_overlay_enabled(bool enabled)
{
    write_enabled_file(fps_control_path(), enabled);
}

bool coordinates_overlay_enabled()
{
    return read_enabled_file(coordinates_control_path());
}

void set_coordinates_overlay_enabled(bool enabled)
{
    write_enabled_file(coordinates_control_path(), enabled);
}

bool compact_readouts_enabled()
{
    std::ifstream file(compact_readouts_control_path());
    if (!file) {
        return false;
    }

    char value {};
    file >> value;
    return value != '0';
}

void set_compact_readouts_enabled(bool enabled)
{
    write_enabled_file(compact_readouts_control_path(), enabled);
}

std::string osd_layout()
{
    std::ifstream file(osd_layout_control_path());
    std::string value;
    file >> value;
    if (value == "rocket") {
        return value;
    }
    return "drone";
}

void set_osd_layout(const std::string& layout)
{
    std::ofstream file(osd_layout_control_path(), std::ios::trunc);
    file << (layout == "rocket" ? "rocket" : "drone") << '\n';
}

} // namespace glide::preview_control
