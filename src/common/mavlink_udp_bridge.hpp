#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace glide::mavlink {

struct UdpBridgeOptions {
    std::uint16_t listen_port { 14550 };
    std::uint8_t system_id { 255 };
    std::uint8_t component_id { 190 };
};

class UdpBridge {
public:
    UdpBridge() = default;
    ~UdpBridge();

    UdpBridge(const UdpBridge&) = delete;
    UdpBridge& operator=(const UdpBridge&) = delete;

    bool start(UdpBridgeOptions options = {});
    std::vector<std::string> poll();
    bool handle_action_line(const std::string& line);
    void close();
    bool running() const;
    const std::string& last_error() const;

private:
    bool send_param_set(const std::string& target, const std::string& name, const std::string& value);
    bool send_param_ext_set(const std::string& target, const std::string& name, const std::string& value);
    bool send_command_long(const std::string& command, const std::string& arguments);
    bool send_packet(std::uint32_t message_id, std::uint8_t crc_extra, const std::vector<std::uint8_t>& payload);

    int fd_ { -1 };
    UdpBridgeOptions options_ {};
    std::uint8_t sequence_ {};
    std::uint8_t flight_controller_system_id_ { 1 };
    std::uint8_t flight_controller_component_id_ { 1 };
    std::uint8_t air_system_id_ { 1 };
    std::uint8_t air_component_id_ { 1 };
    std::uint8_t ground_system_id_ { 100 };
    std::uint8_t ground_component_id_ { 1 };
    std::string last_error_;

    struct PeerStorage;
    PeerStorage* peer_ {};
};

} // namespace glide::mavlink
