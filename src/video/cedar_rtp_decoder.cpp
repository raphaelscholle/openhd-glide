#include "video/cedar_rtp_decoder.hpp"

#if OPENHD_GLIDE_HAS_CEDAR
extern "C" {
#include <memoryAdapter.h>
#include <sc_interface.h>
#include <vbasetype.h>
#include <vdecoder.h>
#include <veAdapter.h>
}

#include <arpa/inet.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#endif

namespace glide::video {

bool cedar_decoder_available()
{
#if OPENHD_GLIDE_HAS_CEDAR
    return true;
#else
    return false;
#endif
}

CedarRtpDecoder::~CedarRtpDecoder()
{
    cleanup();
}

bool CedarRtpDecoder::start(std::uint16_t udp_port, std::uint32_t width, std::uint32_t height, std::uint32_t fps)
{
#if OPENHD_GLIDE_HAS_CEDAR
    return init_socket(udp_port) && init_decoder(width, height, fps);
#else
    (void)udp_port;
    (void)width;
    (void)height;
    (void)fps;
    last_error_ = "native Cedar decoder support was not found at build time";
    return false;
#endif
}

bool CedarRtpDecoder::poll(glide::dev::DmabufVideoFrame& frame)
{
#if OPENHD_GLIDE_HAS_CEDAR
    last_error_.clear();
    access_unit_submitted_this_poll_ = false;
    std::uint8_t packet[65536];
    for (;;) {
        const auto received = recv(socket_fd_, packet, sizeof(packet), MSG_DONTWAIT);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            last_error_ = std::string("failed to receive RTP packet: ") + std::strerror(errno);
            return false;
        }
        if (received == 0) {
            break;
        }
        if (!handle_rtp_packet(packet, static_cast<std::size_t>(received))) {
            return false;
        }
        if (access_unit_submitted_this_poll_ && drain_picture(frame)) {
            return true;
        }
    }

    return drain_picture(frame);
#else
    (void)frame;
    return false;
#endif
}

void CedarRtpDecoder::mark_presented()
{
#if OPENHD_GLIDE_HAS_CEDAR
    return_picture(current_picture_);
    current_picture_ = pending_picture_;
    pending_picture_ = nullptr;
#endif
}

const std::string& CedarRtpDecoder::last_error() const
{
    return last_error_;
}

#if OPENHD_GLIDE_HAS_CEDAR
bool CedarRtpDecoder::init_socket(std::uint16_t udp_port)
{
    socket_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0) {
        last_error_ = std::string("failed to create UDP socket: ") + std::strerror(errno);
        return false;
    }

    const int receive_buffer = 1024 * 1024;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(udp_port);
    if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        last_error_ = std::string("failed to bind UDP RTP socket: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool CedarRtpDecoder::init_decoder(std::uint32_t width, std::uint32_t height, std::uint32_t fps)
{
    AddVDPlugin();

    memops_ = MemAdapterGetOpsS();
    if (memops_ == nullptr || CdcMemOpen(memops_) != 0) {
        last_error_ = "failed to open Cedar memory adapter";
        return false;
    }

    veops_ = GetVeOpsS(VE_OPS_TYPE_NORMAL);
    if (veops_ == nullptr) {
        last_error_ = "failed to get Cedar VE ops";
        return false;
    }

    decoder_ = CreateVideoDecoder();
    if (decoder_ == nullptr) {
        last_error_ = "failed to create Cedar video decoder";
        return false;
    }

    VideoStreamInfo stream_info {};
    stream_info.eCodecFormat = VIDEO_CODEC_FORMAT_H264;
    stream_info.nWidth = static_cast<int>(width);
    stream_info.nHeight = static_cast<int>(height);
    stream_info.nFrameRate = static_cast<int>(fps);
    stream_info.nFrameDuration = fps > 0 ? static_cast<int>(1000000U / fps) : 16667;
    stream_info.bIsFramePackage = 1;

    VConfig config {};
    config.eOutputPixelFormat = PIXEL_FORMAT_NV21;
    config.nVbvBufferSize = 16 * 1024 * 1024;
    config.nFrameBufferNum = 4;
    config.nDisplayHoldingFrameBufferNum = 1;
    config.nDecodeSmoothFrameBufferNum = 0;
    config.bDisable3D = 1;
    config.bNoBFrames = 0;
    config.memops = memops_;
    config.veOpsS = veops_;
    config.nVeFreq = 624;

    const auto result = InitializeVideoDecoder(decoder_, &stream_info, &config);
    if (result != 0) {
        last_error_ = "failed to initialize Cedar H264 decoder result=" + std::to_string(result);
        return false;
    }
    return true;
}

bool CedarRtpDecoder::handle_rtp_packet(const std::uint8_t* packet, std::size_t size)
{
    if (size < 12 || (packet[0] >> 6U) != 2) {
        return true;
    }

    const bool has_padding = (packet[0] & 0x20U) != 0;
    const bool has_extension = (packet[0] & 0x10U) != 0;
    const auto csrc_count = packet[0] & 0x0FU;
    const bool marker = (packet[1] & 0x80U) != 0;
    const auto timestamp = (static_cast<std::uint32_t>(packet[4]) << 24U)
        | (static_cast<std::uint32_t>(packet[5]) << 16U)
        | (static_cast<std::uint32_t>(packet[6]) << 8U)
        | static_cast<std::uint32_t>(packet[7]);

    std::size_t offset = 12U + static_cast<std::size_t>(csrc_count) * 4U;
    if (offset > size) {
        return true;
    }
    if (has_extension) {
        if (offset + 4U > size) {
            return true;
        }
        const auto extension_words = (static_cast<std::size_t>(packet[offset + 2U]) << 8U) | packet[offset + 3U];
        offset += 4U + extension_words * 4U;
        if (offset > size) {
            return true;
        }
    }

    std::size_t payload_size = size - offset;
    if (has_padding && payload_size > 0) {
        const auto padding = packet[size - 1U];
        if (padding <= payload_size) {
            payload_size -= padding;
        }
    }
    if (payload_size == 0) {
        return true;
    }

    ++packets_;
    return append_h264_payload(packet + offset, payload_size, marker, timestamp);
}

bool CedarRtpDecoder::append_h264_payload(const std::uint8_t* payload, std::size_t size, bool marker, std::uint32_t timestamp)
{
    if (size == 0) {
        return true;
    }

    if (have_timestamp_ && timestamp != current_timestamp_ && !access_unit_.empty()) {
        if (!submit_access_unit()) {
            return false;
        }
    }
    current_timestamp_ = timestamp;
    have_timestamp_ = true;

    constexpr std::uint8_t start_code[] { 0x00, 0x00, 0x00, 0x01 };
    const auto nal_type = payload[0] & 0x1FU;
    if (nal_type >= 1 && nal_type <= 23) {
        access_unit_.insert(access_unit_.end(), std::begin(start_code), std::end(start_code));
        access_unit_.insert(access_unit_.end(), payload, payload + size);
    } else if (nal_type == 24) {
        std::size_t offset = 1;
        while (offset + 2U <= size) {
            const auto nal_size = (static_cast<std::size_t>(payload[offset]) << 8U) | payload[offset + 1U];
            offset += 2U;
            if (offset + nal_size > size) {
                break;
            }
            access_unit_.insert(access_unit_.end(), std::begin(start_code), std::end(start_code));
            access_unit_.insert(access_unit_.end(), payload + offset, payload + offset + nal_size);
            offset += nal_size;
        }
    } else if (nal_type == 28 && size >= 2) {
        const auto fu_indicator = payload[0];
        const auto fu_header = payload[1];
        const bool start = (fu_header & 0x80U) != 0;
        const auto reconstructed = static_cast<std::uint8_t>((fu_indicator & 0xE0U) | (fu_header & 0x1FU));
        if (start) {
            access_unit_.insert(access_unit_.end(), std::begin(start_code), std::end(start_code));
            access_unit_.push_back(reconstructed);
        }
        access_unit_.insert(access_unit_.end(), payload + 2, payload + size);
    }

    if (marker && !access_unit_.empty()) {
        return submit_access_unit();
    }
    return true;
}

bool CedarRtpDecoder::submit_access_unit()
{
    if (access_unit_.empty()) {
        return true;
    }

    char* buffer {};
    char* ring_buffer {};
    int buffer_size {};
    int ring_buffer_size {};
    const auto requested = static_cast<int>(access_unit_.size());
    if (RequestVideoStreamBuffer(decoder_, requested, &buffer, &buffer_size, &ring_buffer, &ring_buffer_size, 0) != 0) {
        access_unit_.clear();
        ++dropped_access_units_;
        for (int i = 0; i < 4; ++i) {
            const auto result = DecodeVideoStream(decoder_, 0, 0, 0, 0);
            if (result == VDECODE_RESULT_NO_BITSTREAM || result == VDECODE_RESULT_NO_FRAME_BUFFER) {
                break;
            }
        }
        return true;
    }
    if (buffer_size + ring_buffer_size < requested) {
        access_unit_.clear();
        ++dropped_access_units_;
        return true;
    }

    const auto first_copy = std::min<std::size_t>(static_cast<std::size_t>(buffer_size), access_unit_.size());
    std::memcpy(buffer, access_unit_.data(), first_copy);
    if (first_copy < access_unit_.size()) {
        std::memcpy(ring_buffer, access_unit_.data() + first_copy, access_unit_.size() - first_copy);
    }

    VideoStreamDataInfo data_info {};
    data_info.pData = buffer;
    data_info.nLength = requested;
    data_info.nPts = static_cast<int64_t>(current_timestamp_);
    data_info.bIsFirstPart = 1;
    data_info.bIsLastPart = 1;
    data_info.bValid = 1;
    if (SubmitVideoStreamData(decoder_, &data_info, 0) != 0) {
        last_error_ = "failed to submit H264 access unit to Cedar decoder";
        return false;
    }

    ++access_units_;
    access_unit_submitted_this_poll_ = true;
    access_unit_.clear();

    for (int i = 0; i < 8; ++i) {
        const auto result = DecodeVideoStream(decoder_, 0, 0, 0, 0);
        if (result == VDECODE_RESULT_NO_BITSTREAM || result == VDECODE_RESULT_NO_FRAME_BUFFER) {
            break;
        }
        if (result == VDECODE_RESULT_UNSUPPORTED) {
            last_error_ = "Cedar decoder reported unsupported H264 stream";
            return false;
        }
        if (result == VDECODE_RESULT_RESOLUTION_CHANGE) {
            break;
        }
    }
    return true;
}

bool CedarRtpDecoder::drain_picture(glide::dev::DmabufVideoFrame& frame)
{
    bool got_picture {};
    while (auto* picture = RequestPicture(decoder_, 0)) {
        return_picture(pending_picture_);
        pending_picture_ = picture;
        got_picture = true;
    }
    if (!got_picture || pending_picture_ == nullptr) {
        return false;
    }
    if (!picture_to_frame(pending_picture_, frame)) {
        return false;
    }
    ++decoded_frames_;
    return true;
}

bool CedarRtpDecoder::picture_to_frame(VideoPicture* picture, glide::dev::DmabufVideoFrame& frame)
{
    if (picture == nullptr || picture->nBufFd < 0) {
        last_error_ = "Cedar decoded picture has no DMABUF fd";
        return false;
    }

    frame = {};
    frame.fds = { -1, -1, -1, -1 };
    frame.width = static_cast<std::uint32_t>(picture->nWidth);
    frame.height = static_cast<std::uint32_t>(picture->nHeight);
    frame.fds[0] = picture->nBufFd;
    frame.fds[1] = picture->nBufFd;
    frame.strides[0] = static_cast<std::uint32_t>(picture->nLineStride > 0 ? picture->nLineStride : picture->nWidth);
    frame.strides[1] = frame.strides[0];
    frame.offsets[0] = 0;
    frame.offsets[1] = frame.strides[0] * frame.height;
    frame.plane_count = 2;

    switch (picture->ePixelFormat) {
    case PIXEL_FORMAT_NV12:
        frame.drm_format = DRM_FORMAT_NV12;
        return true;
    case PIXEL_FORMAT_NV21:
        frame.drm_format = DRM_FORMAT_NV21;
        return true;
    case PIXEL_FORMAT_YV12:
        frame.drm_format = DRM_FORMAT_YVU420;
        frame.plane_count = 3;
        frame.fds[2] = picture->nBufFd;
        frame.strides[1] = frame.strides[0] / 2U;
        frame.strides[2] = frame.strides[1];
        frame.offsets[1] = frame.strides[0] * frame.height;
        frame.offsets[2] = frame.offsets[1] + frame.strides[1] * ((frame.height + 1U) / 2U);
        return true;
    default:
        last_error_ = "unsupported Cedar output pixel format " + std::to_string(picture->ePixelFormat);
        return false;
    }
}

void CedarRtpDecoder::return_picture(VideoPicture*& picture)
{
    if (decoder_ != nullptr && picture != nullptr) {
        ReturnPicture(decoder_, picture);
        picture = nullptr;
    }
}

void CedarRtpDecoder::cleanup()
{
    return_picture(pending_picture_);
    return_picture(current_picture_);
    if (decoder_ != nullptr) {
        DestroyVideoDecoder(decoder_);
        decoder_ = nullptr;
    }
    if (memops_ != nullptr) {
        CdcMemClose(memops_);
        memops_ = nullptr;
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}
#endif

} // namespace glide::video
