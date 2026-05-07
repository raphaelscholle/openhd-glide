#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace glide::dev {

struct DmabufVideoFrame {
    std::uint32_t width {};
    std::uint32_t height {};
    std::uint32_t drm_format {};
    std::uint32_t plane_count {};
    std::array<int, 4> fds { -1, -1, -1, -1 };
    std::array<std::uint32_t, 4> strides {};
    std::array<std::uint32_t, 4> offsets {};
};

class KmsDmabufVideoPlane {
public:
    KmsDmabufVideoPlane() = default;
    ~KmsDmabufVideoPlane();

    KmsDmabufVideoPlane(const KmsDmabufVideoPlane&) = delete;
    KmsDmabufVideoPlane& operator=(const KmsDmabufVideoPlane&) = delete;

    bool create(std::uint32_t requested_width, std::uint32_t requested_height, int preferred_plane_id = -1);
    bool present(const DmabufVideoFrame& frame);
    const std::string& last_error() const;

private:
    struct ImportedFramebuffer {
        std::uint32_t framebuffer {};
        std::array<std::uint32_t, 4> handles {};
    };

    struct DumbBuffer {
        std::uint32_t handle {};
        std::uint32_t framebuffer {};
        std::uint32_t pitch {};
        std::uint64_t size {};
        void* map {};
    };

    bool open_card();
    bool choose_connector_and_mode(std::uint32_t requested_width, std::uint32_t requested_height);
    bool create_primary_buffer();
    bool choose_video_plane(std::uint32_t drm_format, int preferred_plane_id);
    bool import_frame(const DmabufVideoFrame& frame, ImportedFramebuffer& imported);
    void destroy_imported(ImportedFramebuffer& imported);
    void destroy_primary_buffer();
    void cleanup();

    int drm_fd_ { -1 };
    std::uint32_t connector_id_ {};
    std::uint32_t crtc_id_ {};
    int crtc_index_ { -1 };
    std::uint32_t video_plane_id_ {};
    std::uint32_t video_plane_format_ {};
    int preferred_plane_id_ { -1 };
    std::uint32_t display_width_ {};
    std::uint32_t display_height_ {};
    void* original_crtc_ {};
    void* mode_ {};
    DumbBuffer primary_ {};
    ImportedFramebuffer current_ {};
    std::string card_path_;
    std::string last_error_;
};

bool kms_dmabuf_video_plane_available();

} // namespace glide::dev
