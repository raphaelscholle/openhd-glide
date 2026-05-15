#include "dev/kms_atomic_compositor.hpp"
#include "common/logging.hpp"

#if OPENHD_GLIDE_HAS_KMS_GBM
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#endif

namespace glide::dev {

bool kms_atomic_compositor_available()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    return true;
#else
    return false;
#endif
}

KmsAtomicCompositor::~KmsAtomicCompositor()
{
    cleanup();
}

#if OPENHD_GLIDE_HAS_KMS_GBM
namespace {

std::string errno_suffix()
{
    return std::string(": ") + std::strerror(errno);
}

std::string egl_error_message(const char* prefix)
{
    std::ostringstream stream;
    stream << prefix << " EGL error=0x" << std::hex << eglGetError();
    return stream.str();
}

std::uint32_t property_id(int drm_fd, std::uint32_t object_id, std::uint32_t object_type, const char* name)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, object_id, object_type);
    if (properties == nullptr) {
        return 0;
    }

    std::uint32_t id {};
    for (std::uint32_t i = 0; i < properties->count_props; ++i) {
        auto* property = drmModeGetProperty(drm_fd, properties->props[i]);
        if (property != nullptr && std::strcmp(property->name, name) == 0) {
            id = property->prop_id;
        }
        if (property != nullptr) {
            drmModeFreeProperty(property);
        }
        if (id != 0) {
            break;
        }
    }

    drmModeFreeObjectProperties(properties);
    return id;
}

bool add_property(
    int drm_fd,
    drmModeAtomicReq* request,
    std::uint32_t object_id,
    std::uint32_t object_type,
    const char* name,
    std::uint64_t value,
    std::string& error)
{
    const auto id = property_id(drm_fd, object_id, object_type, name);
    if (id == 0) {
        error = std::string("missing atomic KMS property ") + name;
        return false;
    }
    if (drmModeAtomicAddProperty(request, object_id, id, value) < 0) {
        error = std::string("failed to add atomic KMS property ") + name + errno_suffix();
        return false;
    }
    return true;
}

bool add_optional_property(
    int drm_fd,
    drmModeAtomicReq* request,
    std::uint32_t object_id,
    std::uint32_t object_type,
    const char* name,
    std::uint64_t value,
    std::string& error)
{
    const auto id = property_id(drm_fd, object_id, object_type, name);
    if (id == 0) {
        return true;
    }
    if (drmModeAtomicAddProperty(request, object_id, id, value) < 0) {
        error = std::string("failed to add atomic KMS property ") + name + errno_suffix();
        return false;
    }
    return true;
}

bool add_optional_range_edge_property(
    int drm_fd,
    drmModeAtomicReq* request,
    std::uint32_t object_id,
    std::uint32_t object_type,
    const char* name,
    bool maximum,
    std::string& error)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, object_id, object_type);
    if (properties == nullptr) {
        return true;
    }

    bool ok { true };
    for (std::uint32_t i = 0; i < properties->count_props; ++i) {
        auto* property = drmModeGetProperty(drm_fd, properties->props[i]);
        if (property != nullptr
            && std::strcmp(property->name, name) == 0
            && (property->flags & DRM_MODE_PROP_RANGE) != 0
            && property->count_values >= 2) {
            const auto value = maximum ? property->values[1] : property->values[0];
            ok = add_optional_property(drm_fd, request, object_id, object_type, name, value, error);
        }
        if (property != nullptr) {
            drmModeFreeProperty(property);
        }
        if (!ok) {
            break;
        }
    }

    drmModeFreeObjectProperties(properties);
    return ok;
}

bool add_optional_enum_property(
    int drm_fd,
    drmModeAtomicReq* request,
    std::uint32_t object_id,
    std::uint32_t object_type,
    const char* name,
    const char* enum_name,
    std::string& error)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, object_id, object_type);
    if (properties == nullptr) {
        return true;
    }

    bool ok { true };
    for (std::uint32_t i = 0; i < properties->count_props; ++i) {
        auto* property = drmModeGetProperty(drm_fd, properties->props[i]);
        if (property != nullptr && std::strcmp(property->name, name) == 0 && (property->flags & DRM_MODE_PROP_ENUM) != 0) {
            for (int j = 0; j < property->count_enums; ++j) {
                if (std::strcmp(property->enums[j].name, enum_name) == 0) {
                    ok = add_optional_property(drm_fd, request, object_id, object_type, name, property->enums[j].value, error);
                    break;
                }
            }
        }
        if (property != nullptr) {
            drmModeFreeProperty(property);
        }
        if (!ok) {
            break;
        }
    }

    drmModeFreeObjectProperties(properties);
    return ok;
}

bool get_property_value(int drm_fd, std::uint32_t object_id, std::uint32_t object_type, const char* name, std::uint64_t& value)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, object_id, object_type);
    if (properties == nullptr) {
        return false;
    }

    bool found {};
    for (std::uint32_t i = 0; i < properties->count_props; ++i) {
        auto* property = drmModeGetProperty(drm_fd, properties->props[i]);
        if (property != nullptr && std::strcmp(property->name, name) == 0) {
            value = properties->prop_values[i];
            found = true;
        }
        if (property != nullptr) {
            drmModeFreeProperty(property);
        }
        if (found) {
            break;
        }
    }

    drmModeFreeObjectProperties(properties);
    return found;
}

bool set_range_edge(int drm_fd, std::uint32_t object_id, std::uint32_t object_type, const char* name, bool maximum)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, object_id, object_type);
    if (properties == nullptr) {
        return false;
    }

    bool set {};
    for (std::uint32_t i = 0; i < properties->count_props; ++i) {
        auto* property = drmModeGetProperty(drm_fd, properties->props[i]);
        if (property != nullptr
            && std::strcmp(property->name, name) == 0
            && (property->flags & DRM_MODE_PROP_RANGE) != 0
            && property->count_values >= 2) {
            const auto value = maximum ? property->values[1] : property->values[0];
            set = drmModeObjectSetProperty(drm_fd, object_id, object_type, property->prop_id, value) == 0;
        }
        if (property != nullptr) {
            drmModeFreeProperty(property);
        }
        if (set) {
            break;
        }
    }

    drmModeFreeObjectProperties(properties);
    return set;
}

std::uint32_t plane_type(int drm_fd, std::uint32_t plane_id)
{
    std::uint64_t value {};
    get_property_value(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "type", value);
    return static_cast<std::uint32_t>(value);
}

bool plane_supports_format(drmModePlane* plane, std::uint32_t format)
{
    return std::find(plane->formats, plane->formats + plane->count_formats, format) != plane->formats + plane->count_formats;
}

void configure_mesa_runtime_for_board()
{
    if (access("/usr/lib/aarch64-linux-gnu/dri/sun4i-drm_dri.so", R_OK) != 0) {
        return;
    }
    if (std::getenv("LIBGL_DRIVERS_PATH") == nullptr) {
        setenv("LIBGL_DRIVERS_PATH", "/usr/lib/aarch64-linux-gnu/dri", 0);
    }
    if (std::getenv("MESA_LOADER_DRIVER_OVERRIDE") == nullptr) {
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "sun4i-drm", 0);
    }
}

} // namespace
#endif

bool KmsAtomicCompositor::create(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    return open_card()
        && choose_connector_and_mode(requested_width, requested_height, requested_refresh_hz)
        && create_primary_buffer()
        && create_gbm_device()
        && create_flow_surface()
        && create_egl();
#else
    (void)requested_width;
    (void)requested_height;
    last_error_ = "atomic KMS compositor support was not found at build time";
    return false;
#endif
}

bool KmsAtomicCompositor::present(const DmabufVideoFrame& video_frame, bool update_flow_frame)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    if (video_plane_id_ == 0 || video_plane_format_ != video_frame.drm_format) {
        video_plane_id_ = 0;
        video_plane_format_ = 0;
        destroy_video_framebuffer_cache();
        if (!choose_video_plane(video_frame.drm_format)) {
            return false;
        }
    }
    if (flow_plane_id_ == 0 && !choose_flow_plane()) {
        return false;
    }

    auto* video = find_or_import_video_framebuffer(video_frame);
    if (video == nullptr) {
        return false;
    }

    if (current_flow_framebuffer_ == 0) {
        update_flow_frame = true;
    }

    std::uint32_t flow_framebuffer { current_flow_framebuffer_ };
    void* flow_bo {};
    if (update_flow_frame && !lock_flow_framebuffer(flow_framebuffer, flow_bo)) {
        return false;
    }

    if (!commit(video_frame, video->framebuffer, flow_framebuffer)) {
        if (flow_bo != nullptr) {
            drmModeRmFB(drm_fd_, flow_framebuffer);
            gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), static_cast<gbm_bo*>(flow_bo));
        }
        return false;
    }

    if (flow_bo != nullptr && current_flow_framebuffer_ != 0) {
        drmModeRmFB(drm_fd_, current_flow_framebuffer_);
    }
    if (flow_bo != nullptr && current_flow_bo_ != nullptr) {
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), static_cast<gbm_bo*>(current_flow_bo_));
    }
    if (flow_bo != nullptr) {
        current_flow_framebuffer_ = flow_framebuffer;
        current_flow_bo_ = flow_bo;
    }
    return true;
#else
    (void)video_frame;
    return false;
#endif
}

flow::SurfaceSize KmsAtomicCompositor::surface_size() const
{
    return surface_;
}

const std::string& KmsAtomicCompositor::last_error() const
{
    return last_error_;
}

#if OPENHD_GLIDE_HAS_KMS_GBM
bool KmsAtomicCompositor::open_card()
{
    for (const auto* path : { "/dev/dri/card1", "/dev/dri/card0", "/dev/dri/card2" }) {
        const int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        auto* resources = drmModeGetResources(fd);
        if (resources == nullptr) {
            close(fd);
            continue;
        }

        bool has_connected_connector {};
        for (int i = 0; i < resources->count_connectors; ++i) {
            auto* connector = drmModeGetConnector(fd, resources->connectors[i]);
            if (connector != nullptr
                && connector->connection == DRM_MODE_CONNECTED
                && connector->count_modes > 0) {
                has_connected_connector = true;
            }
            if (connector != nullptr) {
                drmModeFreeConnector(connector);
            }
            if (has_connected_connector) {
                break;
            }
        }

        drmModeFreeResources(resources);
        if (!has_connected_connector) {
            close(fd);
            continue;
        }

        drm_fd_ = fd;
        card_path_ = path;
        drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        if (drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
            last_error_ = "failed to enable DRM atomic client capability" + errno_suffix();
            close(drm_fd_);
            drm_fd_ = -1;
            return false;
        }
        return true;
    }

    last_error_ = "no DRM card with a connected connector was found";
    return false;
}

bool KmsAtomicCompositor::choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz)
{
    auto* resources = drmModeGetResources(drm_fd_);
    if (resources == nullptr) {
        last_error_ = "failed to read DRM resources";
        return false;
    }

    drmModeConnector* chosen_connector {};
    for (int i = 0; i < resources->count_connectors; ++i) {
        auto* connector = drmModeGetConnector(drm_fd_, resources->connectors[i]);
        if (connector != nullptr
            && connector->connection == DRM_MODE_CONNECTED
            && connector->count_modes > 0) {
            chosen_connector = connector;
            break;
        }
        if (connector != nullptr) {
            drmModeFreeConnector(connector);
        }
    }

    if (chosen_connector == nullptr) {
        drmModeFreeResources(resources);
        last_error_ = "no connected DRM connector with modes was found";
        return false;
    }

    drmModeModeInfo selected_mode = chosen_connector->modes[0];
    bool found_resolution = false;
    bool found_exact_refresh = false;
    for (int i = 0; i < chosen_connector->count_modes; ++i) {
        const auto& candidate = chosen_connector->modes[i];
        if (candidate.hdisplay != requested_width || candidate.vdisplay != requested_height) {
            continue;
        }
        if (!found_resolution) {
            selected_mode = candidate;
            found_resolution = true;
        }
        if (requested_refresh_hz != 0 && static_cast<std::uint32_t>(candidate.vrefresh) == requested_refresh_hz) {
            selected_mode = candidate;
            found_exact_refresh = true;
            break;
        }
        if (requested_refresh_hz == 0 && candidate.vrefresh > selected_mode.vrefresh) {
            selected_mode = candidate;
        }
    }
    if (found_resolution && requested_refresh_hz != 0 && !found_exact_refresh) {
        last_error_ = "requested refresh rate is unavailable for the selected resolution";
        drmModeFreeConnector(chosen_connector);
        drmModeFreeResources(resources);
        return false;
    }
    if (found_resolution) {
        glide::log(
            glide::LogLevel::info,
            "OpenHD-Glide",
            "atomic KMS mode selected "
                + std::to_string(selected_mode.hdisplay) + "x" + std::to_string(selected_mode.vdisplay)
                + "@" + std::to_string(selected_mode.vrefresh)
                + "Hz on connector " + std::to_string(chosen_connector->connector_id)
                + (requested_refresh_hz != 0 ? (" (requested " + std::to_string(requested_refresh_hz) + "Hz)") : " (highest refresh auto-selection)"));
    } else {
        glide::log(
            glide::LogLevel::warning,
            "OpenHD-Glide",
            "no exact resolution match for requested "
                + std::to_string(requested_width) + "x" + std::to_string(requested_height)
                + "; using connector default mode "
                + std::to_string(selected_mode.hdisplay) + "x" + std::to_string(selected_mode.vdisplay)
                + "@" + std::to_string(selected_mode.vrefresh) + "Hz");
    }

    drmModeEncoder* selected_encoder {};
    if (chosen_connector->encoder_id != 0) {
        selected_encoder = drmModeGetEncoder(drm_fd_, chosen_connector->encoder_id);
    }
    if (selected_encoder == nullptr) {
        for (int i = 0; i < chosen_connector->count_encoders; ++i) {
            auto* encoder = drmModeGetEncoder(drm_fd_, chosen_connector->encoders[i]);
            if (encoder != nullptr) {
                selected_encoder = encoder;
                break;
            }
        }
    }

    if (selected_encoder == nullptr) {
        drmModeFreeConnector(chosen_connector);
        drmModeFreeResources(resources);
        last_error_ = "connected DRM connector has no usable encoder";
        return false;
    }

    std::uint32_t selected_crtc {};
    int selected_crtc_index = -1;
    if (selected_encoder->crtc_id != 0) {
        selected_crtc = selected_encoder->crtc_id;
        for (int i = 0; i < resources->count_crtcs; ++i) {
            if (resources->crtcs[i] == selected_crtc) {
                selected_crtc_index = i;
                break;
            }
        }
    }
    if (selected_crtc == 0) {
        for (int i = 0; i < resources->count_crtcs; ++i) {
            if ((selected_encoder->possible_crtcs & (1 << i)) != 0) {
                selected_crtc = resources->crtcs[i];
                selected_crtc_index = i;
                break;
            }
        }
    }

    if (selected_crtc == 0 || selected_crtc_index < 0) {
        drmModeFreeEncoder(selected_encoder);
        drmModeFreeConnector(chosen_connector);
        drmModeFreeResources(resources);
        last_error_ = "failed to select a DRM CRTC for the connector";
        return false;
    }

    connector_id_ = chosen_connector->connector_id;
    crtc_id_ = selected_crtc;
    crtc_index_ = selected_crtc_index;
    surface_ = flow::SurfaceSize {
        .width = selected_mode.hdisplay,
        .height = selected_mode.vdisplay,
    };
    mode_ = new drmModeModeInfo(selected_mode);
    original_crtc_ = drmModeGetCrtc(drm_fd_, crtc_id_);
    if (drmModeCreatePropertyBlob(drm_fd_, mode_, sizeof(drmModeModeInfo), &mode_blob_id_) != 0) {
        drmModeFreeEncoder(selected_encoder);
        drmModeFreeConnector(chosen_connector);
        drmModeFreeResources(resources);
        last_error_ = "failed to create DRM mode property blob" + errno_suffix();
        return false;
    }

    drmModeFreeEncoder(selected_encoder);
    drmModeFreeConnector(chosen_connector);
    drmModeFreeResources(resources);
    return true;
}

bool KmsAtomicCompositor::create_primary_buffer()
{
    drm_mode_create_dumb create {};
    create.width = surface_.width;
    create.height = surface_.height;
    create.bpp = 32;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        last_error_ = "failed to create primary DRM buffer" + errno_suffix();
        return false;
    }
    primary_.handle = create.handle;
    primary_.pitch = create.pitch;
    primary_.size = create.size;

    std::uint32_t handles[4] { primary_.handle, 0, 0, 0 };
    std::uint32_t strides[4] { primary_.pitch, 0, 0, 0 };
    std::uint32_t offsets[4] {};
    if (drmModeAddFB2(drm_fd_, surface_.width, surface_.height, DRM_FORMAT_XRGB8888, handles, strides, offsets, &primary_.framebuffer, 0) != 0) {
        last_error_ = "failed to create primary DRM framebuffer" + errno_suffix();
        destroy_primary_buffer();
        return false;
    }

    drm_mode_map_dumb map {};
    map.handle = primary_.handle;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        last_error_ = "failed to map primary DRM buffer" + errno_suffix();
        destroy_primary_buffer();
        return false;
    }
    primary_.map = mmap(nullptr, primary_.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd_, static_cast<off_t>(map.offset));
    if (primary_.map == MAP_FAILED) {
        primary_.map = nullptr;
        last_error_ = "failed to mmap primary DRM buffer" + errno_suffix();
        destroy_primary_buffer();
        return false;
    }
    std::memset(primary_.map, 0, static_cast<std::size_t>(primary_.size));

    auto* planes = drmModeGetPlaneResources(drm_fd_);
    if (planes == nullptr) {
        last_error_ = "failed to read KMS plane resources";
        return false;
    }
    for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
        auto* plane = drmModeGetPlane(drm_fd_, planes->planes[i]);
        if (plane == nullptr) {
            continue;
        }
        const bool usable_crtc = (plane->possible_crtcs & (1 << crtc_index_)) != 0;
        if (usable_crtc && plane_supports_format(plane, DRM_FORMAT_XRGB8888) && plane_type(drm_fd_, plane->plane_id) == DRM_PLANE_TYPE_PRIMARY) {
            primary_plane_id_ = plane->plane_id;
            drmModeFreePlane(plane);
            break;
        }
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(planes);
    if (primary_plane_id_ == 0) {
        last_error_ = "no primary KMS plane supports XRGB8888";
        return false;
    }
    return true;
}

bool KmsAtomicCompositor::choose_video_plane(std::uint32_t drm_format)
{
    auto* planes = drmModeGetPlaneResources(drm_fd_);
    if (planes == nullptr) {
        last_error_ = "failed to read KMS plane resources";
        return false;
    }

    for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
        auto* plane = drmModeGetPlane(drm_fd_, planes->planes[i]);
        if (plane == nullptr) {
            continue;
        }
        const auto type = plane_type(drm_fd_, plane->plane_id);
        const bool usable_crtc = (plane->possible_crtcs & (1 << crtc_index_)) != 0;
        const bool available = plane->plane_id != primary_plane_id_ && plane->plane_id != flow_plane_id_;
        if (usable_crtc && available && plane_supports_format(plane, drm_format) && type == DRM_PLANE_TYPE_OVERLAY) {
            video_plane_id_ = plane->plane_id;
            video_plane_format_ = drm_format;
            drmModeFreePlane(plane);
            break;
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planes);
    if (video_plane_id_ == 0) {
        last_error_ = "no KMS overlay plane supports decoded video format";
        return false;
    }
    set_range_edge(drm_fd_, video_plane_id_, DRM_MODE_OBJECT_PLANE, "zpos", false);
    set_range_edge(drm_fd_, video_plane_id_, DRM_MODE_OBJECT_PLANE, "ZPOS", false);
    set_range_edge(drm_fd_, video_plane_id_, DRM_MODE_OBJECT_PLANE, "alpha", true);
    return true;
}

bool KmsAtomicCompositor::choose_flow_plane()
{
    auto* planes = drmModeGetPlaneResources(drm_fd_);
    if (planes == nullptr) {
        last_error_ = "failed to read KMS plane resources";
        return false;
    }

    for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
        auto* plane = drmModeGetPlane(drm_fd_, planes->planes[i]);
        if (plane == nullptr) {
            continue;
        }
        const auto type = plane_type(drm_fd_, plane->plane_id);
        const bool usable_crtc = (plane->possible_crtcs & (1 << crtc_index_)) != 0;
        const bool available = plane->plane_id != primary_plane_id_ && plane->plane_id != video_plane_id_;
        if (usable_crtc && available && plane_supports_format(plane, DRM_FORMAT_ARGB8888) && type == DRM_PLANE_TYPE_OVERLAY) {
            flow_plane_id_ = plane->plane_id;
            drmModeFreePlane(plane);
            break;
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planes);
    if (flow_plane_id_ == 0) {
        last_error_ = "no KMS overlay plane supports ARGB8888 Flow scanout";
        return false;
    }
    set_range_edge(drm_fd_, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "zpos", true);
    set_range_edge(drm_fd_, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "ZPOS", true);
    set_range_edge(drm_fd_, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "alpha", true);
    return true;
}

bool KmsAtomicCompositor::create_gbm_device()
{
    configure_mesa_runtime_for_board();

    gbm_device_ = gbm_create_device(drm_fd_);
    if (gbm_device_ == nullptr) {
        last_error_ = "failed to create GBM device";
        return false;
    }
    return true;
}

bool KmsAtomicCompositor::create_flow_surface()
{
    gbm_surface_ = gbm_surface_create(
        static_cast<gbm_device*>(gbm_device_),
        surface_.width,
        surface_.height,
        GBM_FORMAT_ARGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_surface_ == nullptr) {
        last_error_ = "failed to create Flow GBM overlay surface";
        return false;
    }
    return true;
}

bool KmsAtomicCompositor::create_egl()
{
    configure_mesa_runtime_for_board();

    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = EGL_NO_DISPLAY;
    if (get_platform_display != nullptr) {
        display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm_device_, nullptr);
    }
    if (display == EGL_NO_DISPLAY) {
        display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(gbm_device_));
    }
    if (display == EGL_NO_DISPLAY) {
        last_error_ = "failed to get EGL display for GBM";
        return false;
    }
    if (eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
        last_error_ = egl_error_message("failed to initialize EGL");
        return false;
    }
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        last_error_ = egl_error_message("failed to bind OpenGL ES API");
        return false;
    }

    const std::array<EGLint, 13> config_attributes {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config {};
    EGLint config_count {};
    if (eglChooseConfig(display, config_attributes.data(), &config, 1, &config_count) != EGL_TRUE || config_count == 0) {
        last_error_ = egl_error_message("failed to choose EGL config");
        return false;
    }

    const std::array<EGLint, 3> context_attributes {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes.data());
    if (context == EGL_NO_CONTEXT) {
        last_error_ = egl_error_message("failed to create OpenGL ES context");
        return false;
    }
    EGLSurface surface = eglCreateWindowSurface(
        display,
        config,
        reinterpret_cast<EGLNativeWindowType>(gbm_surface_),
        nullptr);
    if (surface == EGL_NO_SURFACE) {
        eglDestroyContext(display, context);
        last_error_ = egl_error_message("failed to create EGL window surface");
        return false;
    }
    if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        last_error_ = egl_error_message("failed to make EGL context current");
        return false;
    }

    eglSwapInterval(display, 0);
    egl_display_ = display;
    egl_config_ = config;
    egl_context_ = context;
    egl_surface_ = surface;
    return true;
}

bool KmsAtomicCompositor::add_gbm_framebuffer(void* bo, std::uint32_t& framebuffer_id)
{
    auto* buffer = static_cast<gbm_bo*>(bo);
    const std::uint32_t width = gbm_bo_get_width(buffer);
    const std::uint32_t height = gbm_bo_get_height(buffer);
    const std::uint32_t format = gbm_bo_get_format(buffer);
    std::uint32_t handles[4] {};
    std::uint32_t strides[4] {};
    std::uint32_t offsets[4] {};
    handles[0] = gbm_bo_get_handle(buffer).u32;
    strides[0] = gbm_bo_get_stride(buffer);
    if (drmModeAddFB2(drm_fd_, width, height, format, handles, strides, offsets, &framebuffer_id, 0) != 0) {
        last_error_ = "failed to add Flow GBM framebuffer" + errno_suffix();
        return false;
    }
    return true;
}

bool KmsAtomicCompositor::lock_flow_framebuffer(std::uint32_t& framebuffer_id, void*& bo)
{
    auto display = static_cast<EGLDisplay>(egl_display_);
    auto surface = static_cast<EGLSurface>(egl_surface_);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
        last_error_ = "Flow EGL surface is unavailable";
        return false;
    }
    if (eglSwapBuffers(display, surface) != EGL_TRUE) {
        last_error_ = egl_error_message("failed to swap Flow EGL overlay surface");
        return false;
    }

    bo = gbm_surface_lock_front_buffer(static_cast<gbm_surface*>(gbm_surface_));
    if (bo == nullptr) {
        last_error_ = "failed to lock Flow GBM front buffer";
        return false;
    }
    if (!add_gbm_framebuffer(bo, framebuffer_id)) {
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), static_cast<gbm_bo*>(bo));
        bo = nullptr;
        return false;
    }
    return true;
}

bool KmsAtomicCompositor::import_video_frame(const DmabufVideoFrame& frame, ImportedFramebuffer& imported)
{
    if (frame.plane_count == 0 || frame.plane_count > 4) {
        last_error_ = "invalid DMABUF plane count";
        return false;
    }

    std::uint32_t handles[4] {};
    std::map<int, std::uint32_t> imported_fds;
    for (std::uint32_t i = 0; i < frame.plane_count; ++i) {
        if (frame.fds[i] < 0) {
            last_error_ = "decoded DMABUF frame has an invalid fd";
            return false;
        }
        const auto existing = imported_fds.find(frame.fds[i]);
        if (existing != imported_fds.end()) {
            handles[i] = existing->second;
            imported.handles[i] = handles[i];
            continue;
        }
        if (drmPrimeFDToHandle(drm_fd_, frame.fds[i], &handles[i]) != 0) {
            last_error_ = "failed to import decoded DMABUF fd into DRM" + errno_suffix();
            imported.handles = { handles[0], handles[1], handles[2], handles[3] };
            destroy_imported(imported);
            return false;
        }
        imported_fds.emplace(frame.fds[i], handles[i]);
        imported.handles[i] = handles[i];
    }

    std::uint32_t strides[4] { frame.strides[0], frame.strides[1], frame.strides[2], frame.strides[3] };
    std::uint32_t offsets[4] { frame.offsets[0], frame.offsets[1], frame.offsets[2], frame.offsets[3] };
    if (drmModeAddFB2(drm_fd_, frame.width, frame.height, frame.drm_format, handles, strides, offsets, &imported.framebuffer, 0) != 0) {
        last_error_ = "failed to create DRM framebuffer from decoded DMABUF" + errno_suffix();
        destroy_imported(imported);
        return false;
    }
    return true;
}

bool KmsAtomicCompositor::make_frame_key(const DmabufVideoFrame& frame, FrameKey& key)
{
    if (frame.plane_count == 0 || frame.plane_count > 4) {
        last_error_ = "invalid DMABUF plane count";
        return false;
    }

    key = {};
    key.width = frame.width;
    key.height = frame.height;
    key.drm_format = frame.drm_format;
    key.plane_count = frame.plane_count;
    key.strides = frame.strides;
    key.offsets = frame.offsets;
    for (std::uint32_t i = 0; i < frame.plane_count; ++i) {
        if (frame.fds[i] < 0) {
            last_error_ = "decoded DMABUF frame has an invalid fd";
            return false;
        }
        struct stat info {};
        if (fstat(frame.fds[i], &info) != 0) {
            last_error_ = "failed to identify decoded DMABUF fd";
            return false;
        }
        key.device_ids[i] = static_cast<std::uint64_t>(info.st_dev);
        key.inodes[i] = static_cast<std::uint64_t>(info.st_ino);
    }
    return true;
}

KmsAtomicCompositor::ImportedFramebuffer* KmsAtomicCompositor::find_or_import_video_framebuffer(const DmabufVideoFrame& frame)
{
    FrameKey key;
    if (!make_frame_key(frame, key)) {
        return nullptr;
    }

    ++frame_serial_;
    for (auto& cached : video_framebuffer_cache_) {
        if (cached.key.width == key.width
            && cached.key.height == key.height
            && cached.key.drm_format == key.drm_format
            && cached.key.plane_count == key.plane_count
            && cached.key.device_ids == key.device_ids
            && cached.key.inodes == key.inodes
            && cached.key.strides == key.strides
            && cached.key.offsets == key.offsets) {
            cached.last_used = frame_serial_;
            return &cached.imported;
        }
    }

    evict_video_framebuffer_if_needed();
    CachedFramebuffer cached;
    cached.key = key;
    cached.last_used = frame_serial_;
    if (!import_video_frame(frame, cached.imported)) {
        return nullptr;
    }
    video_framebuffer_cache_.push_back(cached);
    return &video_framebuffer_cache_.back().imported;
}

void KmsAtomicCompositor::evict_video_framebuffer_if_needed()
{
    constexpr std::size_t max_cached_framebuffers = 12;
    if (video_framebuffer_cache_.size() < max_cached_framebuffers) {
        return;
    }

    auto victim = video_framebuffer_cache_.begin();
    for (auto it = video_framebuffer_cache_.begin(); it != video_framebuffer_cache_.end(); ++it) {
        if (it->last_used < victim->last_used) {
            victim = it;
        }
    }
    destroy_imported(victim->imported);
    video_framebuffer_cache_.erase(victim);
}

bool KmsAtomicCompositor::commit(const DmabufVideoFrame& video_frame, std::uint32_t video_framebuffer, std::uint32_t flow_framebuffer)
{
    auto* request = drmModeAtomicAlloc();
    if (request == nullptr) {
        last_error_ = "failed to allocate DRM atomic request";
        return false;
    }

    bool ok = true;
    if (!modeset_committed_) {
        ok = ok && add_property(drm_fd_, request, connector_id_, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", crtc_id_, last_error_);
        ok = ok && add_property(drm_fd_, request, crtc_id_, DRM_MODE_OBJECT_CRTC, "MODE_ID", mode_blob_id_, last_error_);
        ok = ok && add_property(drm_fd_, request, crtc_id_, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1, last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "zpos", false, last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "ZPOS", false, last_error_);
        ok = ok && add_optional_enum_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "pixel blend mode", "None", last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "zpos", false, last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "ZPOS", false, last_error_);
        ok = ok && add_optional_enum_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "pixel blend mode", "None", last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "zpos", true, last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "ZPOS", true, last_error_);
        ok = ok && add_optional_range_edge_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "alpha", true, last_error_);
        ok = ok && add_optional_enum_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "pixel blend mode", "Pre-multiplied", last_error_);
    }

    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "FB_ID", primary_.framebuffer, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id_, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_X", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_W", surface_.width << 16U, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_H", surface_.height << 16U, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_W", surface_.width, last_error_);
    ok = ok && add_property(drm_fd_, request, primary_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_H", surface_.height, last_error_);

    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "FB_ID", video_framebuffer, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id_, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_X", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_W", video_frame.width << 16U, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_H", video_frame.height << 16U, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_W", surface_.width, last_error_);
    ok = ok && add_property(drm_fd_, request, video_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_H", surface_.height, last_error_);

    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "FB_ID", flow_framebuffer, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id_, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_X", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_W", surface_.width << 16U, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "SRC_H", surface_.height << 16U, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_W", surface_.width, last_error_);
    ok = ok && add_property(drm_fd_, request, flow_plane_id_, DRM_MODE_OBJECT_PLANE, "CRTC_H", surface_.height, last_error_);

    if (!ok) {
        drmModeAtomicFree(request);
        return false;
    }

    const std::uint32_t flags = modeset_committed_ ? 0 : DRM_MODE_ATOMIC_ALLOW_MODESET;
    if (drmModeAtomicCommit(drm_fd_, request, flags, nullptr) != 0) {
        last_error_ = "failed to commit atomic KMS video+Flow frame" + errno_suffix();
        drmModeAtomicFree(request);
        return false;
    }

    modeset_committed_ = true;
    drmModeAtomicFree(request);
    return true;
}

void KmsAtomicCompositor::destroy_imported(ImportedFramebuffer& imported)
{
    if (drm_fd_ >= 0 && imported.framebuffer != 0) {
        drmModeRmFB(drm_fd_, imported.framebuffer);
        imported.framebuffer = 0;
    }
    if (drm_fd_ >= 0) {
        for (auto& handle : imported.handles) {
            if (handle == 0) {
                continue;
            }
            drm_gem_close close_args {};
            close_args.handle = handle;
            drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &close_args);
            handle = 0;
        }
    }
}

void KmsAtomicCompositor::destroy_video_framebuffer_cache()
{
    for (auto& cached : video_framebuffer_cache_) {
        destroy_imported(cached.imported);
    }
    video_framebuffer_cache_.clear();
}

void KmsAtomicCompositor::destroy_primary_buffer()
{
    if (primary_.map != nullptr) {
        munmap(primary_.map, static_cast<std::size_t>(primary_.size));
        primary_.map = nullptr;
    }
    if (drm_fd_ >= 0 && primary_.framebuffer != 0) {
        drmModeRmFB(drm_fd_, primary_.framebuffer);
        primary_.framebuffer = 0;
    }
    if (drm_fd_ >= 0 && primary_.handle != 0) {
        drm_mode_destroy_dumb destroy {};
        destroy.handle = primary_.handle;
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        primary_.handle = 0;
    }
}

void KmsAtomicCompositor::cleanup()
{
    if (drm_fd_ >= 0 && original_crtc_ != nullptr) {
        auto* crtc = static_cast<drmModeCrtc*>(original_crtc_);
        drmModeSetCrtc(
            drm_fd_,
            crtc->crtc_id,
            crtc->buffer_id,
            crtc->x,
            crtc->y,
            &connector_id_,
            1,
            &crtc->mode);
    }
    if (original_crtc_ != nullptr) {
        drmModeFreeCrtc(static_cast<drmModeCrtc*>(original_crtc_));
        original_crtc_ = nullptr;
    }
    if (current_flow_framebuffer_ != 0 && drm_fd_ >= 0) {
        drmModeRmFB(drm_fd_, current_flow_framebuffer_);
        current_flow_framebuffer_ = 0;
    }
    if (current_flow_bo_ != nullptr && gbm_surface_ != nullptr) {
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), static_cast<gbm_bo*>(current_flow_bo_));
        current_flow_bo_ = nullptr;
    }

    auto display = static_cast<EGLDisplay>(egl_display_);
    if (display != EGL_NO_DISPLAY && display != nullptr) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface_ != nullptr && static_cast<EGLSurface>(egl_surface_) != EGL_NO_SURFACE) {
            eglDestroySurface(display, static_cast<EGLSurface>(egl_surface_));
        }
        if (egl_context_ != nullptr && static_cast<EGLContext>(egl_context_) != EGL_NO_CONTEXT) {
            eglDestroyContext(display, static_cast<EGLContext>(egl_context_));
        }
        eglTerminate(display);
    }
    egl_display_ = nullptr;
    egl_context_ = nullptr;
    egl_surface_ = nullptr;

    if (gbm_surface_ != nullptr) {
        gbm_surface_destroy(static_cast<gbm_surface*>(gbm_surface_));
        gbm_surface_ = nullptr;
    }
    if (gbm_device_ != nullptr) {
        gbm_device_destroy(static_cast<gbm_device*>(gbm_device_));
        gbm_device_ = nullptr;
    }

    destroy_video_framebuffer_cache();
    destroy_primary_buffer();

    if (drm_fd_ >= 0 && mode_blob_id_ != 0) {
        drmModeDestroyPropertyBlob(drm_fd_, mode_blob_id_);
        mode_blob_id_ = 0;
    }
    if (mode_ != nullptr) {
        delete static_cast<drmModeModeInfo*>(mode_);
        mode_ = nullptr;
    }
    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
}
#endif

} // namespace glide::dev
