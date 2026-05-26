/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * © OpenHD, All Rights Reserved.
 ******************************************************************************/

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

    bool create(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz = 0);
    bool create_overlay(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz = 0);
    void swap();
    flow::SurfaceSize surface_size() const;
    const std::string& last_error() const;

private:
    bool open_card();
    bool choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz);
    bool choose_overlay_plane(std::uint32_t format);
    bool configure_overlay_plane();
    bool create_gbm_device();
    bool create_gbm_surface(std::uint32_t format);
    bool create_egl(bool alpha_surface);
    bool add_framebuffer(void* bo, std::uint32_t& framebuffer_id);
    void cleanup_scanout();
    void cleanup_egl();
    void cleanup_gbm();
    void cleanup_drm();

    int drm_fd_ { -1 };
    std::uint32_t connector_id_ {};
    std::uint32_t crtc_id_ {};
    int crtc_index_ { -1 };
    std::uint32_t overlay_plane_id_ {};
    std::uint32_t overlay_format_ {};
    bool overlay_mode_ {};
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
