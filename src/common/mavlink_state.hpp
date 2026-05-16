#pragma once

#include <array>
#include <string>

namespace glide::mavlink {

struct Snapshot {
    bool air_alive {};
    bool ground_alive {};
    bool fc_alive {};
    bool armed {};
    int frequency_mhz { 5745 };
    int channel_width_mhz { 20 };
    int mcs_index { 2 };
    int tx_power_mw { 1200 };
    int scan_progress { 0 };
    int rc_channels[4] { 1500, 1500, 1000, 1500 };
    std::string resolution_fps { "1920x1080@120" };
    std::string rotation { "0 deg" };
    std::string recording { "Disabled" };
    std::string recording_status { "Idle" };
    std::string air_wifi_mode { "Hotspot" };
    std::string ground_wifi_mode { "Monitor" };
    std::string air_hotspot { "Off" };
    std::string ground_hotspot { "On" };
    std::string openhd_version { "N/A" };
    std::string ground_chipset { "A733 / sunxi-drm" };
    std::string air_chipset { "N/A" };
    std::string camera { "N/A" };
    std::array<std::string, 5> messages {};
    std::size_t message_count {};
};

bool apply_ipc_line(Snapshot& snapshot, const std::string& line);
std::string format_action_set_param(const std::string& target, const std::string& param, const std::string& value);
std::string format_action_command(const std::string& command, const std::string& arguments = {});

} // namespace glide::mavlink
