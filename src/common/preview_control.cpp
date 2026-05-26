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

#include "common/preview_control.hpp"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
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

std::filesystem::path theme_color_path(const std::string& key)
{
    return std::filesystem::temp_directory_path() / ("openhd-glide-theme-" + key + ".rgb");
}

std::uint32_t default_theme_color(const std::string& key)
{
    if (key == "bar_text") {
        return 0xebf5ff;
    }
    if (key == "bar_background") {
        return 0x0e1318;
    }
    if (key == "primary") {
        return 0x99ffb8;
    }
    if (key == "secondary") {
        return 0x55a8ff;
    }
    return 0xffffff;
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
    if (value == "rocket" || value == "rover" || value == "ship") {
        return value;
    }
    return "drone";
}

void set_osd_layout(const std::string& layout)
{
    std::ofstream file(osd_layout_control_path(), std::ios::trunc);
    if (layout == "rocket" || layout == "rover" || layout == "ship") {
        file << layout << '\n';
    } else {
        file << "drone\n";
    }
}

std::uint32_t theme_color(const std::string& key)
{
    std::ifstream file(theme_color_path(key));
    std::string value;
    file >> value;
    if (value.size() == 6 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        })) {
        return static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
    }
    return default_theme_color(key);
}

void set_theme_color(const std::string& key, std::uint32_t rgb)
{
    std::ofstream file(theme_color_path(key), std::ios::trunc);
    file << std::hex << std::setw(6) << std::setfill('0') << (rgb & 0xffffffU) << '\n';
}

} // namespace glide::preview_control
