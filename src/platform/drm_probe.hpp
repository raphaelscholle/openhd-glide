#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace glide::platform {

struct DrmPlaneInfo {
    std::uint32_t id {};
    std::uint32_t possible_crtcs {};
    std::optional<std::uint32_t> current_crtc_id;
    std::optional<std::int32_t> x;
    std::optional<std::int32_t> y;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    std::vector<std::uint32_t> formats;
};

struct DrmDeviceInfo {
    std::string path;
    bool available { false };
    std::string status;
    std::vector<DrmPlaneInfo> planes;
};

struct DrmProbeResult {
    std::vector<DrmDeviceInfo> devices;
};

DrmProbeResult probe_drm_planes();
std::string describe_drm_probe(const DrmProbeResult& result);

} // namespace glide::platform

