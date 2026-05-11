#pragma once

#include "dev/kms_dmabuf_video_plane.hpp"

#include <cstdint>
#include <string>
#include <vector>

typedef void* VideoDecoder;
typedef struct VIDEOPICTURE VideoPicture;
struct ScMemOpsS;
struct VeOpsS;

namespace glide::video {

class CedarRtpDecoder {
public:
    CedarRtpDecoder() = default;
    ~CedarRtpDecoder();

    CedarRtpDecoder(const CedarRtpDecoder&) = delete;
    CedarRtpDecoder& operator=(const CedarRtpDecoder&) = delete;

    bool start(std::uint16_t udp_port, std::uint32_t width, std::uint32_t height, std::uint32_t fps);
    bool poll(glide::dev::DmabufVideoFrame& frame);
    void mark_presented();
    const std::string& last_error() const;

private:
    bool init_socket(std::uint16_t udp_port);
    bool init_decoder(std::uint32_t width, std::uint32_t height, std::uint32_t fps);
    bool handle_rtp_packet(const std::uint8_t* packet, std::size_t size);
    bool append_h264_payload(const std::uint8_t* payload, std::size_t size, bool marker, std::uint32_t timestamp);
    bool submit_access_unit();
    bool drain_picture(glide::dev::DmabufVideoFrame& frame);
    bool picture_to_frame(VideoPicture* picture, glide::dev::DmabufVideoFrame& frame);
    void return_picture(VideoPicture*& picture);
    void cleanup();

    int socket_fd_ { -1 };
    VideoDecoder* decoder_ {};
    ScMemOpsS* memops_ {};
    VeOpsS* veops_ {};
    std::vector<std::uint8_t> access_unit_;
    std::uint32_t current_timestamp_ {};
    bool have_timestamp_ {};
    VideoPicture* pending_picture_ {};
    VideoPicture* current_picture_ {};
    std::uint64_t packets_ {};
    std::uint64_t access_units_ {};
    std::uint64_t decoded_frames_ {};
    std::uint64_t dropped_access_units_ {};
    bool access_unit_submitted_this_poll_ {};
    std::string last_error_;
};

bool cedar_decoder_available();

} // namespace glide::video
