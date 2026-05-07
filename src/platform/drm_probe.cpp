#include "platform/drm_probe.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__linux__) && OPENHD_GLIDE_HAS_LIBDRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

namespace glide::platform {
namespace {

std::string format_fourcc(std::uint32_t format)
{
    std::string value;
    value.push_back(static_cast<char>(format & 0xffu));
    value.push_back(static_cast<char>((format >> 8u) & 0xffu));
    value.push_back(static_cast<char>((format >> 16u) & 0xffu));
    value.push_back(static_cast<char>((format >> 24u) & 0xffu));
    return value;
}

#if defined(__linux__) && OPENHD_GLIDE_HAS_LIBDRM
std::vector<std::string> find_drm_device_paths()
{
    const std::filesystem::path dri_root { "/dev/dri" };
    std::vector<std::string> paths;
    std::error_code error;

    if (!std::filesystem::exists(dri_root, error) || error) {
        return paths;
    }

    for (const auto& entry : std::filesystem::directory_iterator(
             dri_root,
             std::filesystem::directory_options::skip_permission_denied,
             error)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("card", 0) == 0) {
            paths.push_back(entry.path().string());
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

std::optional<std::uint64_t> get_plane_property(
    int fd,
    drmModeObjectPropertiesPtr properties,
    const char* name)
{
    if (properties == nullptr) {
        return std::nullopt;
    }

    for (std::uint32_t i = 0; i < properties->count_props; ++i) {
        drmModePropertyPtr property = drmModeGetProperty(fd, properties->props[i]);
        if (property == nullptr) {
            continue;
        }

        const std::string property_name = property->name;
        drmModeFreeProperty(property);

        if (property_name == name) {
            return properties->prop_values[i];
        }
    }

    return std::nullopt;
}

DrmPlaneInfo read_plane(int fd, std::uint32_t plane_id)
{
    DrmPlaneInfo info {};
    info.id = plane_id;

    drmModePlanePtr plane = drmModeGetPlane(fd, plane_id);
    if (plane == nullptr) {
        return info;
    }

    info.possible_crtcs = plane->possible_crtcs;
    if (plane->crtc_id != 0) {
        info.current_crtc_id = plane->crtc_id;
    }

    for (std::uint32_t i = 0; i < plane->count_formats; ++i) {
        info.formats.push_back(plane->formats[i]);
    }

    drmModeObjectPropertiesPtr properties = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (const auto value = get_plane_property(fd, properties, "CRTC_X")) {
        info.x = static_cast<std::int32_t>(*value);
    }
    if (const auto value = get_plane_property(fd, properties, "CRTC_Y")) {
        info.y = static_cast<std::int32_t>(*value);
    }
    if (const auto value = get_plane_property(fd, properties, "CRTC_W")) {
        info.width = static_cast<std::uint32_t>(*value);
    }
    if (const auto value = get_plane_property(fd, properties, "CRTC_H")) {
        info.height = static_cast<std::uint32_t>(*value);
    }

    if (properties != nullptr) {
        drmModeFreeObjectProperties(properties);
    }

    drmModeFreePlane(plane);
    return info;
}

DrmDeviceInfo probe_device(const char* path)
{
    DrmDeviceInfo device;
    device.path = path;

    const int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        device.status = "not accessible";
        return device;
    }

    if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        device.status = "failed to enable universal planes";
        close(fd);
        return device;
    }

    drmModePlaneResPtr resources = drmModeGetPlaneResources(fd);
    if (resources == nullptr) {
        device.status = "failed to read plane resources";
        close(fd);
        return device;
    }

    device.available = true;
    device.status = "ok";
    device.planes.reserve(resources->count_planes);

    for (std::uint32_t i = 0; i < resources->count_planes; ++i) {
        device.planes.push_back(read_plane(fd, resources->planes[i]));
    }

    drmModeFreePlaneResources(resources);
    close(fd);
    return device;
}
#endif

} // namespace

DrmProbeResult probe_drm_planes()
{
    DrmProbeResult result;

#if defined(__linux__) && OPENHD_GLIDE_HAS_LIBDRM
    const auto paths = find_drm_device_paths();
    result.devices.reserve(paths.size());

    if (paths.empty()) {
        DrmDeviceInfo device;
        device.path = "/dev/dri";
        device.available = false;
        device.status = "no DRM card devices found";
        result.devices.push_back(device);
        return result;
    }

    for (const auto& path : paths) {
        result.devices.push_back(probe_device(path.c_str()));
    }
#else
    DrmDeviceInfo device;
    device.path = "DRM/KMS";
    device.available = false;
#if defined(__linux__)
    device.status = "libdrm was not found at build time";
#else
    device.status = "DRM probing is only available on Linux";
#endif
    result.devices.push_back(device);
#endif

    return result;
}

std::string describe_drm_probe(const DrmProbeResult& result)
{
    std::ostringstream stream;

    for (const auto& device : result.devices) {
        stream << "DRM device " << device.path << ": " << device.status;
        if (device.available) {
            stream << " (" << device.planes.size() << " plane(s))";
        }
        stream << '\n';

        for (const auto& plane : device.planes) {
            stream << "  plane " << plane.id
                   << " possible_crtcs=0x" << std::hex << plane.possible_crtcs << std::dec
                   << " crtc=" << (plane.current_crtc_id ? std::to_string(*plane.current_crtc_id) : "none")
                   << " pos=(" << (plane.x ? std::to_string(*plane.x) : "?")
                   << "," << (plane.y ? std::to_string(*plane.y) : "?") << ")"
                   << " size=" << (plane.width ? std::to_string(*plane.width) : "?")
                   << "x" << (plane.height ? std::to_string(*plane.height) : "?");

            if (!plane.formats.empty()) {
                stream << " formats=";
                const auto limit = std::min<std::size_t>(plane.formats.size(), 6);
                for (std::size_t i = 0; i < limit; ++i) {
                    if (i != 0) {
                        stream << ",";
                    }
                    stream << format_fourcc(plane.formats[i]);
                }
                if (plane.formats.size() > limit) {
                    stream << ",...";
                }
            }

            stream << '\n';
        }
    }

    return stream.str();
}

} // namespace glide::platform
