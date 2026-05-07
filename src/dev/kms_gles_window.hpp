#pragma once

#include "glide_flow/fps_overlay.hpp"

#include <cstdint>
#include <string>

namespace glide::dev {

class KmsGlesWindow {
public:
    KmsGlesWindow() = default;
    ~KmsGlesWindow();

    KmsGlesWindow(const KmsGlesWindow&) = delete;
    KmsGlesWindow& operator=(const KmsGlesWindow&) = delete;

    bool create(std::uint32_t requested_width, std::uint32_t requested_height);
    void swap();
    flow::SurfaceSize surface_size() const;
    const std::string& last_error() const;

private:
    bool open_card();
    bool choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height);
    bool create_gbm_device();
    bool create_gbm_surface(std::uint32_t format);
    bool create_egl();
    bool add_framebuffer(void* bo, std::uint32_t& framebuffer_id);
    void cleanup_scanout();
    void cleanup_egl();
    void cleanup_gbm();
    void cleanup_drm();

    int drm_fd_ { -1 };
    std::uint32_t connector_id_ {};
    std::uint32_t crtc_id_ {};
    int crtc_index_ { -1 };
    void* original_crtc_ {};
    void* mode_ {};
    void* gbm_device_ {};
    void* gbm_surface_ {};
    void* egl_display_ {};
    void* egl_config_ {};
    void* egl_context_ {};
    void* egl_surface_ {};
    void* current_bo_ {};
    std::uint32_t current_framebuffer_id_ {};
    flow::SurfaceSize surface_ {};
    std::string card_path_;
    std::string last_error_;
};

bool kms_gles_available();

} // namespace glide::dev
