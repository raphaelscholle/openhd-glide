#pragma once

#include "dev/kms_dmabuf_video_plane.hpp"
#include "glide_flow/fps_overlay.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace glide::dev {

class KmsAtomicCompositor {
public:
    KmsAtomicCompositor() = default;
    ~KmsAtomicCompositor();

    KmsAtomicCompositor(const KmsAtomicCompositor&) = delete;
    KmsAtomicCompositor& operator=(const KmsAtomicCompositor&) = delete;

    bool create(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz = 0);
    bool present(const DmabufVideoFrame& video_frame, bool update_flow_frame);
    bool make_flow_context_current();
    bool release_flow_context();
    bool publish_rendered_flow_frame();
    bool publish_solid_flow_frame(std::uint32_t argb);
    flow::SurfaceSize surface_size() const;
    const std::string& last_error() const;

private:
    struct ImportedFramebuffer {
        std::uint32_t framebuffer {};
        std::array<std::uint32_t, 4> handles {};
    };

    struct FrameKey {
        std::uint32_t width {};
        std::uint32_t height {};
        std::uint32_t drm_format {};
        std::uint32_t plane_count {};
        std::array<std::uint64_t, 4> device_ids {};
        std::array<std::uint64_t, 4> inodes {};
        std::array<std::uint32_t, 4> strides {};
        std::array<std::uint32_t, 4> offsets {};
    };

    struct CachedFramebuffer {
        FrameKey key {};
        ImportedFramebuffer imported {};
        std::uint64_t last_used {};
    };

    struct DumbBuffer {
        std::uint32_t handle {};
        std::uint32_t framebuffer {};
        std::uint32_t pitch {};
        std::uint64_t size {};
        void* map {};
    };

    bool open_card();
    bool choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height, std::uint32_t requested_refresh_hz);
    bool create_primary_buffer();
    bool choose_video_plane(std::uint32_t drm_format);
    bool choose_flow_plane();
    bool create_gbm_device();
    bool create_flow_surface();
    bool create_egl();
    bool create_solid_flow_buffer();
    bool add_gbm_framebuffer(void* bo, std::uint32_t& framebuffer_id);
    bool lock_flow_framebuffer(std::uint32_t& framebuffer_id, void*& bo);
    bool import_video_frame(const DmabufVideoFrame& frame, ImportedFramebuffer& imported);
    bool make_frame_key(const DmabufVideoFrame& frame, FrameKey& key);
    ImportedFramebuffer* find_or_import_video_framebuffer(const DmabufVideoFrame& frame);
    void evict_video_framebuffer_if_needed();
    bool commit(const DmabufVideoFrame& video_frame, std::uint32_t video_framebuffer, std::uint32_t flow_framebuffer);
    void destroy_imported(ImportedFramebuffer& imported);
    void destroy_video_framebuffer_cache();
    void destroy_primary_buffer();
    void destroy_solid_flow_buffer();
    void cleanup();

    int drm_fd_ { -1 };
    std::uint32_t connector_id_ {};
    std::uint32_t crtc_id_ {};
    int crtc_index_ { -1 };
    std::uint32_t primary_plane_id_ {};
    std::uint32_t video_plane_id_ {};
    std::uint32_t video_plane_format_ {};
    std::uint32_t flow_plane_id_ {};
    std::uint32_t mode_blob_id_ {};
    bool modeset_committed_ {};
    flow::SurfaceSize surface_ {};
    void* original_crtc_ {};
    void* mode_ {};
    DumbBuffer primary_ {};
    void* gbm_device_ {};
    void* gbm_surface_ {};
    void* egl_display_ {};
    void* egl_config_ {};
    void* egl_context_ {};
    void* egl_surface_ {};
    void* current_flow_bo_ {};
    std::uint32_t current_flow_framebuffer_ {};
    bool current_flow_is_solid_dumb_ {};
    void* pending_flow_bo_ {};
    std::uint32_t pending_flow_framebuffer_ {};
    bool pending_flow_is_solid_dumb_ {};
    DumbBuffer solid_flow_ {};
    std::mutex flow_framebuffer_mutex_;
    std::vector<CachedFramebuffer> video_framebuffer_cache_;
    std::uint64_t frame_serial_ {};
    std::string card_path_;
    std::string last_error_;
};

bool kms_atomic_compositor_available();

} // namespace glide::dev
