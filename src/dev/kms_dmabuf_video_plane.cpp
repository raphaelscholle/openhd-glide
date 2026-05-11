#include "dev/kms_dmabuf_video_plane.hpp"

#if OPENHD_GLIDE_HAS_KMS_GBM
#include <drm.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#endif

namespace glide::dev {

#if OPENHD_GLIDE_HAS_KMS_GBM
namespace {

std::string errno_suffix()
{
    return std::string(": ") + std::strerror(errno);
}

bool get_plane_property_value(int drm_fd, std::uint32_t plane_id, const char* name, std::uint64_t& value)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
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

bool set_plane_property_to_range_edge(int drm_fd, std::uint32_t plane_id, const char* name, bool maximum)
{
    auto* properties = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
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
            set = drmModeObjectSetProperty(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, property->prop_id, value) == 0;
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

} // namespace
#endif

bool kms_dmabuf_video_plane_available()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    return true;
#else
    return false;
#endif
}

KmsDmabufVideoPlane::~KmsDmabufVideoPlane()
{
    cleanup();
}

bool KmsDmabufVideoPlane::create(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz, int preferred_plane_id)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    preferred_plane_id_ = preferred_plane_id;
    if (!open_card() || !choose_connector_and_mode(requested_width, requested_height, requested_refresh_hz) || !create_primary_buffer()) {
        return false;
    }
    return true;
#else
    (void)requested_width;
    (void)requested_height;
    last_error_ = "KMS DMABUF video plane support was not found at build time";
    return false;
#endif
}

bool KmsDmabufVideoPlane::present(const DmabufVideoFrame& frame)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    if (video_plane_id_ == 0 || video_plane_format_ != frame.drm_format) {
        if (video_plane_id_ != 0 && drm_fd_ >= 0) {
            drmModeSetPlane(drm_fd_, video_plane_id_, crtc_id_, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
        destroy_framebuffer_cache();
        current_framebuffer_ = 0;
        if (!choose_video_plane(frame.drm_format, preferred_plane_id_)) {
            return false;
        }
    }

    auto* imported = find_or_import_cached_framebuffer(frame);
    if (imported == nullptr) {
        return false;
    }

    if (drmModeSetPlane(
            drm_fd_,
            video_plane_id_,
            crtc_id_,
            imported->framebuffer,
            0,
            0,
            0,
            display_width_,
            display_height_,
            0,
            0,
            frame.width << 16U,
            frame.height << 16U)
        != 0) {
        last_error_ = "failed to set KMS video plane from DMABUF framebuffer" + errno_suffix();
        return false;
    }

    current_framebuffer_ = imported->framebuffer;
    return true;
#else
    (void)frame;
    return false;
#endif
}

const std::string& KmsDmabufVideoPlane::last_error() const
{
    return last_error_;
}

#if OPENHD_GLIDE_HAS_KMS_GBM
bool KmsDmabufVideoPlane::open_card()
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
        if (has_connected_connector) {
            drm_fd_ = fd;
            card_path_ = path;
            drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
            return true;
        }
        close(fd);
    }

    last_error_ = "no DRM card with a connected connector was found";
    return false;
}

bool KmsDmabufVideoPlane::choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz)
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
    display_width_ = selected_mode.hdisplay;
    display_height_ = selected_mode.vdisplay;
    mode_ = new drmModeModeInfo(selected_mode);
    original_crtc_ = drmModeGetCrtc(drm_fd_, crtc_id_);

    drmModeFreeEncoder(selected_encoder);
    drmModeFreeConnector(chosen_connector);
    drmModeFreeResources(resources);
    return true;
}

bool KmsDmabufVideoPlane::create_primary_buffer()
{
    drm_mode_create_dumb create {};
    create.width = display_width_;
    create.height = display_height_;
    create.bpp = 32;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        last_error_ = "failed to create primary DRM buffer";
        return false;
    }
    primary_.handle = create.handle;
    primary_.pitch = create.pitch;
    primary_.size = create.size;

    std::uint32_t handles[4] { primary_.handle, 0, 0, 0 };
    std::uint32_t strides[4] { primary_.pitch, 0, 0, 0 };
    std::uint32_t offsets[4] {};
    if (drmModeAddFB2(drm_fd_, display_width_, display_height_, DRM_FORMAT_XRGB8888, handles, strides, offsets, &primary_.framebuffer, 0) != 0) {
        last_error_ = "failed to create primary DRM framebuffer";
        destroy_primary_buffer();
        return false;
    }

    drm_mode_map_dumb map {};
    map.handle = primary_.handle;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        last_error_ = "failed to map primary DRM buffer";
        destroy_primary_buffer();
        return false;
    }
    primary_.map = mmap(nullptr, primary_.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd_, static_cast<off_t>(map.offset));
    if (primary_.map == MAP_FAILED) {
        primary_.map = nullptr;
        last_error_ = "failed to mmap primary DRM buffer";
        destroy_primary_buffer();
        return false;
    }
    std::memset(primary_.map, 0, static_cast<std::size_t>(primary_.size));

    auto* mode = static_cast<drmModeModeInfo*>(mode_);
    if (drmModeSetCrtc(drm_fd_, crtc_id_, primary_.framebuffer, 0, 0, &connector_id_, 1, mode) != 0) {
        last_error_ = "failed to set primary KMS framebuffer";
        destroy_primary_buffer();
        return false;
    }
    return true;
}

bool KmsDmabufVideoPlane::choose_video_plane(std::uint32_t drm_format, int preferred_plane_id)
{
    auto* planes = drmModeGetPlaneResources(drm_fd_);
    if (planes == nullptr) {
        last_error_ = "failed to read KMS plane resources";
        return false;
    }

    std::uint32_t selected {};
    std::uint32_t selected_type {};
    constexpr std::uint64_t overlay_plane = DRM_PLANE_TYPE_OVERLAY;
    constexpr std::uint64_t primary_plane = DRM_PLANE_TYPE_PRIMARY;
    for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
        auto* plane = drmModeGetPlane(drm_fd_, planes->planes[i]);
        if (plane == nullptr) {
            continue;
        }
        const bool requested = preferred_plane_id < 0 || plane->plane_id == static_cast<std::uint32_t>(preferred_plane_id);
        const bool usable_crtc = (plane->possible_crtcs & (1 << crtc_index_)) != 0;
        const bool supports_format = std::find(plane->formats, plane->formats + plane->count_formats, drm_format) != plane->formats + plane->count_formats;
        if (requested && usable_crtc && supports_format) {
            std::uint64_t plane_type {};
            get_plane_property_value(drm_fd_, plane->plane_id, "type", plane_type);
            if (selected == 0 || (preferred_plane_id < 0 && selected_type == primary_plane && plane_type == overlay_plane)) {
                selected = plane->plane_id;
                selected_type = static_cast<std::uint32_t>(plane_type);
            }
            if (preferred_plane_id >= 0 || plane_type == overlay_plane) {
                drmModeFreePlane(plane);
                break;
            }
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planes);
    if (selected == 0) {
        last_error_ = "no KMS plane supports decoded DMABUF video format";
        return false;
    }

    video_plane_id_ = selected;
    video_plane_format_ = drm_format;
    configure_video_plane();
    return true;
}

bool KmsDmabufVideoPlane::configure_video_plane()
{
    if (video_plane_id_ == 0 || drm_fd_ < 0) {
        return false;
    }

    set_plane_property_to_range_edge(drm_fd_, video_plane_id_, "zpos", true);
    set_plane_property_to_range_edge(drm_fd_, video_plane_id_, "ZPOS", true);
    set_plane_property_to_range_edge(drm_fd_, video_plane_id_, "alpha", true);
    return true;
}

bool KmsDmabufVideoPlane::import_frame(const DmabufVideoFrame& frame, ImportedFramebuffer& imported)
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

bool KmsDmabufVideoPlane::make_frame_key(const DmabufVideoFrame& frame, FrameKey& key)
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

KmsDmabufVideoPlane::ImportedFramebuffer* KmsDmabufVideoPlane::find_or_import_cached_framebuffer(const DmabufVideoFrame& frame)
{
    FrameKey key;
    if (!make_frame_key(frame, key)) {
        return nullptr;
    }

    ++frame_serial_;
    for (auto& cached : framebuffer_cache_) {
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

    evict_cached_framebuffer_if_needed();

    CachedFramebuffer cached;
    cached.key = key;
    cached.last_used = frame_serial_;
    if (!import_frame(frame, cached.imported)) {
        return nullptr;
    }
    framebuffer_cache_.push_back(cached);
    return &framebuffer_cache_.back().imported;
}

void KmsDmabufVideoPlane::evict_cached_framebuffer_if_needed()
{
    constexpr std::size_t max_cached_framebuffers = 12;
    if (framebuffer_cache_.size() < max_cached_framebuffers) {
        return;
    }

    auto victim = framebuffer_cache_.end();
    for (auto it = framebuffer_cache_.begin(); it != framebuffer_cache_.end(); ++it) {
        if (it->imported.framebuffer == current_framebuffer_) {
            continue;
        }
        if (victim == framebuffer_cache_.end() || it->last_used < victim->last_used) {
            victim = it;
        }
    }
    if (victim == framebuffer_cache_.end() && !framebuffer_cache_.empty()) {
        victim = framebuffer_cache_.begin();
    }
    if (victim != framebuffer_cache_.end()) {
        destroy_imported(victim->imported);
        framebuffer_cache_.erase(victim);
    }
}

void KmsDmabufVideoPlane::destroy_imported(ImportedFramebuffer& imported)
{
    if (imported.framebuffer != 0 && drm_fd_ >= 0) {
        drmModeRmFB(drm_fd_, imported.framebuffer);
        imported.framebuffer = 0;
    }

    std::set<std::uint32_t> closed;
    for (auto& handle : imported.handles) {
        if (handle == 0 || closed.count(handle) != 0 || drm_fd_ < 0) {
            handle = 0;
            continue;
        }
        drm_gem_close close_handle {};
        close_handle.handle = handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &close_handle);
        closed.insert(handle);
        handle = 0;
    }
}

void KmsDmabufVideoPlane::destroy_framebuffer_cache()
{
    for (auto& cached : framebuffer_cache_) {
        destroy_imported(cached.imported);
    }
    framebuffer_cache_.clear();
}

void KmsDmabufVideoPlane::destroy_primary_buffer()
{
    if (primary_.map != nullptr) {
        munmap(primary_.map, primary_.size);
        primary_.map = nullptr;
    }
    if (primary_.framebuffer != 0 && drm_fd_ >= 0) {
        drmModeRmFB(drm_fd_, primary_.framebuffer);
        primary_.framebuffer = 0;
    }
    if (primary_.handle != 0 && drm_fd_ >= 0) {
        drm_mode_destroy_dumb destroy {};
        destroy.handle = primary_.handle;
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        primary_.handle = 0;
    }
}
#endif

void KmsDmabufVideoPlane::cleanup()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    if (drm_fd_ >= 0 && video_plane_id_ != 0) {
        drmModeSetPlane(drm_fd_, video_plane_id_, crtc_id_, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    if (drm_fd_ >= 0 && original_crtc_ != nullptr) {
        auto* crtc = static_cast<drmModeCrtc*>(original_crtc_);
        drmModeSetCrtc(drm_fd_, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y, &connector_id_, 1, &crtc->mode);
    }
    current_framebuffer_ = 0;
    destroy_framebuffer_cache();
    destroy_primary_buffer();
    if (original_crtc_ != nullptr) {
        drmModeFreeCrtc(static_cast<drmModeCrtc*>(original_crtc_));
        original_crtc_ = nullptr;
    }
    delete static_cast<drmModeModeInfo*>(mode_);
    mode_ = nullptr;
    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
#endif
}

} // namespace glide::dev
