#include "dev/kms_gles_window.hpp"

#if OPENHD_GLIDE_HAS_KMS_GBM
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fcntl.h>
#include <gbm.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <array>
#include <cstring>
#endif

namespace glide::dev {

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

bool KmsGlesWindow::create(std::uint32_t requested_width, std::uint32_t requested_height)
{
#if OPENHD_GLIDE_HAS_KMS_GBM
    return open_card()
        && choose_connector_and_mode(requested_width, requested_height)
        && create_gbm()
        && create_egl();
#else
    (void)requested_width;
    (void)requested_height;
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

bool KmsGlesWindow::choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height)
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
    for (int i = 0; i < chosen_connector->count_modes; ++i) {
        const auto& candidate = chosen_connector->modes[i];
        if (candidate.hdisplay == requested_width && candidate.vdisplay == requested_height) {
            selected_mode = candidate;
            break;
        }
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

bool KmsGlesWindow::create_gbm()
{
    gbm_device_ = gbm_create_device(drm_fd_);
    if (gbm_device_ == nullptr) {
        last_error_ = "failed to create GBM device";
        return false;
    }

    gbm_surface_ = gbm_surface_create(
        static_cast<gbm_device*>(gbm_device_),
        surface_.width,
        surface_.height,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_surface_ == nullptr) {
        last_error_ = "failed to create GBM scanout surface";
        return false;
    }
    return true;
}

bool KmsGlesWindow::create_egl()
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
        last_error_ = "failed to initialize EGL";
        return false;
    }
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        last_error_ = "failed to bind OpenGL ES API";
        return false;
    }

    const std::array<EGLint, 13> config_attributes {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    EGLConfig config {};
    EGLint config_count {};
    if (eglChooseConfig(display, config_attributes.data(), &config, 1, &config_count) != EGL_TRUE || config_count == 0) {
        last_error_ = "failed to choose EGL config";
        return false;
    }

    const std::array<EGLint, 3> context_attributes {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes.data());
    if (context == EGL_NO_CONTEXT) {
        last_error_ = "failed to create OpenGL ES context";
        return false;
    }

    EGLSurface surface = eglCreateWindowSurface(
        display,
        config,
        reinterpret_cast<EGLNativeWindowType>(gbm_surface_),
        nullptr);
    if (surface == EGL_NO_SURFACE) {
        eglDestroyContext(display, context);
        last_error_ = "failed to create EGL window surface";
        return false;
    }

    if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        last_error_ = "failed to make EGL context current";
        return false;
    }

    eglSwapInterval(display, 1);
    egl_display_ = display;
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

    auto* mode = static_cast<drmModeModeInfo*>(mode_);
    if (drmModeSetCrtc(drm_fd_, crtc_id_, framebuffer_id, 0, 0, &connector_id_, 1, mode) != 0) {
        drmModeRmFB(drm_fd_, framebuffer_id);
        gbm_surface_release_buffer(static_cast<gbm_surface*>(gbm_surface_), bo);
        last_error_ = "failed to set DRM CRTC";
        return;
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
