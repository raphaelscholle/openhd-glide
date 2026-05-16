#include "common/mavlink_state.hpp"

#include <algorithm>
#include <sstream>

namespace glide::mavlink {
namespace {

std::string rest_after(std::istringstream& stream)
{
    std::string value;
    std::getline(stream, value);
    if (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    return value;
}

int clamp_percent(int value)
{
    return std::clamp(value, 0, 100);
}

void push_message(Snapshot& snapshot, std::string message)
{
    if (message.empty()) {
        return;
    }
    if (snapshot.message_count < snapshot.messages.size()) {
        snapshot.messages[snapshot.message_count++] = std::move(message);
        return;
    }
    for (std::size_t i = 1; i < snapshot.messages.size(); ++i) {
        snapshot.messages[i - 1] = std::move(snapshot.messages[i]);
    }
    snapshot.messages.back() = std::move(message);
}

} // namespace

bool apply_ipc_line(Snapshot& snapshot, const std::string& line)
{
    std::istringstream stream(line);
    std::string prefix;
    std::string key;
    stream >> prefix >> key;
    if (prefix != "mav") {
        return false;
    }

    if (key == "alive") {
        std::string target;
        int alive {};
        stream >> target >> alive;
        if (target == "air") {
            snapshot.air_alive = alive != 0;
        } else if (target == "ground") {
            snapshot.ground_alive = alive != 0;
        } else if (target == "fc") {
            snapshot.fc_alive = alive != 0;
        }
        return true;
    }
    if (key == "armed") {
        int armed {};
        stream >> armed;
        snapshot.armed = armed != 0;
        return true;
    }
    if (key == "link") {
        stream >> snapshot.frequency_mhz >> snapshot.channel_width_mhz >> snapshot.mcs_index >> snapshot.tx_power_mw;
        return true;
    }
    if (key == "scan") {
        stream >> snapshot.scan_progress;
        snapshot.scan_progress = clamp_percent(snapshot.scan_progress);
        return true;
    }
    if (key == "rc") {
        for (auto& channel : snapshot.rc_channels) {
            stream >> channel;
        }
        return true;
    }
    if (key == "param") {
        std::string target;
        std::string param;
        stream >> target >> param;
        const auto value = rest_after(stream);
        if (param == "RESOLUTION_FPS") {
            snapshot.resolution_fps = value;
        } else if (param == "ROTATION_FLIP" || param == "ROTATION_DEG") {
            snapshot.rotation = value;
        } else if (param == "AIR_RECORDING_E") {
            snapshot.recording = value;
        } else if (param == "WIFI_MODE" && target == "air") {
            snapshot.air_wifi_mode = value;
        } else if (param == "WIFI_MODE" && target == "ground") {
            snapshot.ground_wifi_mode = value;
        } else if (param == "WIFI_HOTSPOT_E" && target == "air") {
            snapshot.air_hotspot = value;
        } else if (param == "WIFI_HOTSPOT_E" && target == "ground") {
            snapshot.ground_hotspot = value;
        }
        return true;
    }
    if (key == "status") {
        std::string field;
        stream >> field;
        const auto value = rest_after(stream);
        if (field == "openhd_version") {
            snapshot.openhd_version = value;
        } else if (field == "ground_chipset") {
            snapshot.ground_chipset = value;
        } else if (field == "air_chipset") {
            snapshot.air_chipset = value;
        } else if (field == "camera") {
            snapshot.camera = value;
        } else if (field == "recording") {
            snapshot.recording_status = value;
        }
        return true;
    }
    if (key == "message") {
        push_message(snapshot, rest_after(stream));
        return true;
    }
    return false;
}

std::string format_action_set_param(const std::string& target, const std::string& param, const std::string& value)
{
    return "mav set " + target + " " + param + " " + value;
}

std::string format_action_command(const std::string& command, const std::string& arguments)
{
    if (arguments.empty()) {
        return "mav command " + command;
    }
    return "mav command " + command + " " + arguments;
}

} // namespace glide::mavlink
