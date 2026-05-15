#include "dev/kms_gles_window.hpp"

#if OPENHD_GLIDE_HAS_KMS_GBM
#include "common/logging.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#endif

namespace glide::dev {

#if OPENHD_GLIDE_HAS_KMS_GBM
namespace {

std::string egl_error_message(const char* prefix)
{
    std::ostringstream stream;
    stream << prefix << " EGL error=0x" << std::hex << eglGetError();
    return stream.str();
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

bool kms_gles_available()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    return true;
#else
    return false;
#endif
}

KmsGlesWindow::~KmsGlesWindow()
{
    cleanup_scanout();
    cleanup_egl();
    cleanup_gbm();
    cleanup_drm();
}

bool KmsGlesWindow::create(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    overlay_mode_ = false;
    return open_card()
        && choose_connector_and_mode(requested_width, requested_height, requested_refresh_hz)
        && create_gbm_device()
        && create_egl(false);
#else
    (void)requested_width;
    (void)requested_height;
    (void)requested_refresh_hz;
    last_error_ = "KMS/GBM/EGL support was not found at build time";
    return false;
#endif
}

bool KmsGlesWindow::create_overlay(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    overlay_mode_ = true;
    overlay_format_ = DRM_FORMAT_ARGB8888;
    return open_card()
        && choose_connector_and_mode(requested_width, requested_height, requested_refresh_hz)
        && choose_overlay_plane(overlay_format_)
        && configure_overlay_plane()
        && create_gbm_device()
        && create_gbm_surface(GBM_FORMAT_ARGB8888)
        && create_egl(true);
#else
    (void)requested_width;
    (void)requested_height;
    (void)requested_refresh_hz;
    last_error_ = "KMS/GBM/EGL support was not found at build time";
    return false;
#endif
}

#if OPENHD_GLIDE_HAS_KMS_GBM
bool KmsGlesWindow::open_card()
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
            return true;
        }
        close(fd);
    }

    last_error_ = "no DRM card with a connected connector was found";
    return false;
}

bool KmsGlesWindow::choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz)
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
            "GlideFlow",
            "KMS mode selected "
                + std::to_string(selected_mode.hdisplay) + "x" + std::to_string(selected_mode.vdisplay)
                + "@" + std::to_string(selected_mode.vrefresh)
                + "Hz on connector " + std::to_string(chosen_connector->connector_id)
                + (requested_refresh_hz != 0 ? (" (requested " + std::to_string(requested_refresh_hz) + "Hz)") : " (highest refresh auto-selection)"));
    } else {
        glide::log(
            glide::LogLevel::warning,
            "GlideFlow",
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

    auto* stored_mode = new drmModeModeInfo(selected_mode);
    mode_ = stored_mode;
    original_crtc_ = drmModeGetCrtc(drm_fd_, crtc_id_);

    drmModeFreeEncoder(selected_encoder);
    drmModeFreeConnector(chosen_connector);
    drmModeFreeResources(resources);
    return true;
}

bool KmsGlesWindow::create_gbm_device()
{
    gbm_device_ = gbm_create_device(drm_fd_);
    if (gbm_device_ == nullptr) {
        last_error_ = "failed to create GBM device";
        return false;
    }
    return true;
}

bool KmsGlesWindow::choose_overlay_plane(std::uint32_t format)
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

        std::uint64_t plane_type {};
        get_plane_property_value(drm_fd_, plane->plane_id, "type", plane_type);
        const bool usable_crtc = (plane->possible_crtcs & (1 << crtc_index_)) != 0;
        const bool supports_format = std::find(plane->formats, plane->formats + plane->count_formats, format) != plane->formats + plane->count_formats;
        if (usable_crtc && supports_format && plane_type == DRM_PLANE_TYPE_OVERLAY) {
            overlay_plane_id_ = plane->plane_id;
            drmModeFreePlane(plane);
            break;
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planes);
    if (overlay_plane_id_ == 0) {
        last_error_ = "no KMS overlay plane supports ARGB8888 Flow scanout";
        return false;
    }
    return true;
}

bool KmsGlesWindow::configure_overlay_plane()
{
    if (overlay_plane_id_ == 0 || drm_fd_ < 0) {
        return false;
    }

    set_plane_property_to_range_edge(drm_fd_, overlay_plane_id_, "zpos", true);
    set_plane_property_to_range_edge(drm_fd_, overlay_plane_id_, "ZPOS", true);
    set_plane_property_to_range_edge(drm_fd_, overlay_plane_id_, "alpha", true);
    return true;
}

bool KmsGlesWindow::create_gbm_surface(std::uint32_t format)
{
    gbm_surface_ = gbm_surface_create(
        static_cast<gbm_device*>(gbm_device_),
        surface_.width,
        surface_.height,
        format,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_surface_ == nullptr) {
        last_error_ = "failed to create GBM scanout surface";
        return false;
    }
    return true;
}

bool KmsGlesWindow::create_egl(bool alpha_surface)
{
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
        EGL_ALPHA_SIZE, alpha_surface ? 8 : 0,
        EGL_NONE,
    };
    EGLConfig config {};
    EGLint config_count {};
    if (eglChooseConfig(display, config_attributes.data(), &config, 1, &config_count) != EGL_TRUE || config_count == 0) {
        last_error_ = egl_error_message("failed to choose EGL config");
        return false;
    }

    EGLint native_visual_id {};
    if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &native_visual_id) != EGL_TRUE || native_visual_id == 0) {
        native_visual_id = GBM_FORMAT_XRGB8888;
    }

    if (gbm_surface_ == nullptr && !create_gbm_surface(static_cast<std::uint32_t>(native_visual_id))) {
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

    eglSwapInterval(display, 1);
    egl_display_ = display;
    egl_config_ = config;
    egl_context_ = context;
    egl_surface_ = surface;
    return true;
}

bool KmsGlesWindow::add_framebuffer(void* bo, std::uint32_t& framebuffer_id)
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
        last_error_ = "failed to add DRM framebuffer";
        return false;
    }
    return true;
}
#endif

void KmsGlesWindow::swap()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    auto display = static_cast<EGLDisplay>(egl_display_);
    auto surface = static_cast<EGLSurface>(egl_surface_);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
        return;
    }

    eglSwapBuffers(display, surface);
    auto* bo = gbm_surface_lock_front_buffer(static_cast<gbm_surface*>(gbm_surface_));
    if (bo == nullptr) {
        last_error_ = "failed to lock GBM front buffer";
        return;
    }

    std::uint32_t framebuffer_id {};
    if (!add_framebuffer(bo, framebuffer_id)) {
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), bo);
        return;
    }

    if (overlay_mode_) {
        if (drmModeSetPlane(
                drm_fd_,
                overlay_plane_id_,
                crtc_id_,
                framebuffer_id,
                0,
                0,
                0,
                surface_.width,
                surface_.height,
                0,
                0,
                surface_.width << 16U,
                surface_.height << 16U)
            != 0) {
            drmModeRmFB(drm_fd_, framebuffer_id);
            gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), bo);
            last_error_ = "failed to set DRM overlay plane";
            return;
        }
    } else {
        auto* mode = static_cast<drmModeModeInfo*>(mode_);
        if (drmModeSetCrtc(drm_fd_, crtc_id_, framebuffer_id, 0, 0, &connector_id_, 1, mode) != 0) {
            drmModeRmFB(drm_fd_, framebuffer_id);
            gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), bo);
            last_error_ = "failed to set DRM CRTC";
            return;
        }
    }

    if (current_framebuffer_id_ != 0) {
        drmModeRmFB(drm_fd_, current_framebuffer_id_);
    }
    if (current_bo_ != nullptr) {
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), static_cast<gbm_bo*>(current_bo_));
    }

    current_framebuffer_id_ = framebuffer_id;
    current_bo_ = bo;
#endif
}

flow::SurfaceSize KmsGlesWindow::surface_size() const
{
    return surface_;
}

const std::string& KmsGlesWindow::last_error() const
{
    return last_error_;
}

void KmsGlesWindow::cleanup_scanout()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    if (drm_fd_ >= 0 && overlay_plane_id_ != 0) {
        drmModeSetPlane(drm_fd_, overlay_plane_id_, crtc_id_, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    if (!overlay_mode_ && drm_fd_ >= 0 && original_crtc_ != nullptr) {
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
    if (drm_fd_ >= 0 && current_framebuffer_id_ != 0) {
        drmModeRmFB(drm_fd_, current_framebuffer_id_);
        current_framebuffer_id_ = 0;
    }
    if (gbm_surface_ != nullptr && current_bo_ != nullptr) {
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), static_cast<gbm_bo*>(current_bo_));
        current_bo_ = nullptr;
    }
#endif
}

void KmsGlesWindow::cleanup_egl()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
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
    egl_surface_ = nullptr;
    egl_context_ = nullptr;
#endif
}

void KmsGlesWindow::cleanup_gbm()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    if (gbm_surface_ != nullptr) {
        gbm_surface_destroy(static_cast<gbm_surface*>(gbm_surface_));
        gbm_surface_ = nullptr;
    }
    if (gbm_device_ != nullptr) {
        gbm_device_destroy(static_cast<gbm_device*>(gbm_device_));
        gbm_device_ = nullptr;
    }
#endif
}

void KmsGlesWindow::cleanup_drm()
{
#if OPENHD_GLIDE_HAS_KMS_GBM
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
