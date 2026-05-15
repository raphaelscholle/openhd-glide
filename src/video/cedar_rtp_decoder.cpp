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
#include <iterator>
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
    bool got_frame {};
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
        if (access_unit_submitted_this_poll_) {
            if (drain_picture(frame)) {
                got_frame = true;
            }
            access_unit_submitted_this_poll_ = false;
        }
    }

    if (got_frame) {
        return true;
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
#endif
}

std::string CedarRtpDecoder::stats() const
{
#if OPENHD_GLIDE_HAS_CEDAR
    return "rtp_packets=" + std::to_string(packets_)
        + " rtp_sequence_gaps=" + std::to_string(rtp_sequence_gaps_)
        + " late_or_duplicate_packets=" + std::to_string(late_or_duplicate_packets_)
        + " dropped_incomplete_fragments=" + std::to_string(dropped_incomplete_fragments_)
        + " dropped_waiting_for_idr=" + std::to_string(dropped_waiting_for_idr_)
        + " access_units=" + std::to_string(access_units_)
        + " dropped_access_units=" + std::to_string(dropped_access_units_)
        + " decoded_frames=" + std::to_string(decoded_frames_);
#else
    return {};
#endif
}

const std::string& CedarRtpDecoder::last_error() const
{
    return last_error_;
}

#if OPENHD_GLIDE_HAS_CEDAR
namespace {

constexpr std::uint8_t start_code[] { 0x00, 0x00, 0x00, 0x01 };

void append_start_code(std::vector<std::uint8_t>& output)
{
    output.insert(output.end(), std::begin(start_code), std::end(start_code));
}

void append_aud_if_needed(std::vector<std::uint8_t>& output)
{
    if (!output.empty()) {
        return;
    }
    constexpr std::uint8_t aud[] { 0x00, 0x00, 0x00, 0x01, 0x09, 0x10 };
    output.insert(output.end(), std::begin(aud), std::end(aud));
}

class RbspBitReader {
public:
    RbspBitReader(const std::uint8_t* data, std::size_t size)
        : data_(data)
        , size_(size)
    {
    }

    bool read_bit(std::uint32_t& bit)
    {
        while (byte_offset_ < size_) {
            if (byte_offset_ >= 2
                && data_[byte_offset_ - 2] == 0x00
                && data_[byte_offset_ - 1] == 0x00
                && data_[byte_offset_] == 0x03) {
                ++byte_offset_;
                bit_offset_ = 0;
                continue;
            }
            bit = (data_[byte_offset_] >> (7U - bit_offset_)) & 1U;
            ++bit_offset_;
            if (bit_offset_ == 8U) {
                bit_offset_ = 0;
                ++byte_offset_;
            }
            return true;
        }
        return false;
    }

    bool read_ue(std::uint32_t& value)
    {
        std::uint32_t leading_zero_bits {};
        std::uint32_t bit {};
        while (read_bit(bit)) {
            if (bit != 0) {
                break;
            }
            ++leading_zero_bits;
            if (leading_zero_bits > 31U) {
                return false;
            }
        }
        if (bit == 0) {
            return false;
        }

        std::uint32_t suffix {};
        for (std::uint32_t i = 0; i < leading_zero_bits; ++i) {
            if (!read_bit(bit)) {
                return false;
            }
            suffix = (suffix << 1U) | bit;
        }
        value = ((1U << leading_zero_bits) - 1U) + suffix;
        return true;
    }

private:
    const std::uint8_t* data_ {};
    std::size_t size_ {};
    std::size_t byte_offset_ {};
    std::uint32_t bit_offset_ {};
};

bool first_slice_starts_frame(const std::uint8_t* nal, std::size_t size)
{
    if (size <= 1) {
        return false;
    }
    RbspBitReader reader(nal + 1, size - 1U);
    std::uint32_t first_mb_in_slice {};
    return reader.read_ue(first_mb_in_slice) && first_mb_in_slice == 0;
}

bool nal_is_vcl(std::uint8_t nal_type)
{
    return nal_type >= 1 && nal_type <= 5;
}

} // namespace

bool CedarRtpDecoder::init_socket(std::uint16_t udp_port)
{
    socket_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0) {
        last_error_ = std::string("failed to create UDP socket: ") + std::strerror(errno);
        return false;
    }

    const int receive_buffer = 32 * 1024 * 1024;
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
    stream_info.bIsFramePackage = 0;

    VConfig config {};
    config.eOutputPixelFormat = PIXEL_FORMAT_NV21;
    config.nVbvBufferSize = 16 * 1024 * 1024;
    config.nFrameBufferNum = 10;
    config.nDisplayHoldingFrameBufferNum = 0;
    config.nDecodeSmoothFrameBufferNum = 0;
    config.bDisable3D = 1;
    config.bNoBFrames = 1;
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
    const auto sequence = static_cast<std::uint16_t>((static_cast<std::uint16_t>(packet[2]) << 8U) | packet[3]);
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
    return append_h264_payload(packet + offset, payload_size, marker, sequence, timestamp);
}

bool CedarRtpDecoder::append_h264_payload(const std::uint8_t* payload, std::size_t size, bool marker, std::uint16_t sequence, std::uint32_t timestamp)
{
    if (size == 0) {
        return true;
    }

    if (dropping_timestamp_) {
        if (timestamp == drop_timestamp_) {
            return true;
        }
        dropping_timestamp_ = false;
    }

    if (have_sequence_) {
        const auto sequence_delta = static_cast<std::int16_t>(sequence - expected_sequence_);
        if (sequence_delta < 0) {
            ++late_or_duplicate_packets_;
            return true;
        }
        if (sequence_delta > 0) {
            ++rtp_sequence_gaps_;
            reset_access_unit();
            require_idr_ = true;
        }
    }
    expected_sequence_ = static_cast<std::uint16_t>(sequence + 1U);
    have_sequence_ = true;

    if (have_timestamp_ && timestamp != current_timestamp_ && !access_unit_.empty()) {
        if (fu_started_) {
            reset_access_unit();
        } else if (!submit_access_unit()) {
            return false;
        }
    }
    current_timestamp_ = timestamp;
    have_timestamp_ = true;

    const auto nal_type = payload[0] & 0x1FU;
    if (nal_type >= 1 && nal_type <= 23) {
        fu_started_ = false;
        cache_parameter_set(payload, size);
        if (nal_is_vcl(nal_type)) {
            if (require_idr_ && nal_type != 5) {
                reset_access_unit();
                dropping_timestamp_ = true;
                drop_timestamp_ = timestamp;
                ++dropped_waiting_for_idr_;
                return true;
            }
            if (!access_unit_has_vcl_ && !first_slice_starts_frame(payload, size)) {
                reset_access_unit();
                dropping_timestamp_ = true;
                drop_timestamp_ = timestamp;
                ++dropped_incomplete_fragments_;
                return true;
            }
            if (nal_type == 5) {
                append_cached_parameter_sets();
                require_idr_ = false;
            }
            access_unit_has_vcl_ = true;
        }
        append_aud_if_needed(access_unit_);
        append_start_code(access_unit_);
        access_unit_.insert(access_unit_.end(), payload, payload + size);
    } else if (nal_type == 24) {
        fu_started_ = false;
        std::size_t offset = 1;
        while (offset + 2U <= size) {
            const auto nal_size = (static_cast<std::size_t>(payload[offset]) << 8U) | payload[offset + 1U];
            offset += 2U;
            if (offset + nal_size > size) {
                break;
            }
            const auto stap_nal_type = payload[offset] & 0x1FU;
            cache_parameter_set(payload + offset, nal_size);
            if (nal_is_vcl(stap_nal_type)) {
                if (require_idr_ && stap_nal_type != 5) {
                    reset_access_unit();
                    dropping_timestamp_ = true;
                    drop_timestamp_ = timestamp;
                    ++dropped_waiting_for_idr_;
                    return true;
                }
                if (!access_unit_has_vcl_ && !first_slice_starts_frame(payload + offset, nal_size)) {
                    reset_access_unit();
                    dropping_timestamp_ = true;
                    drop_timestamp_ = timestamp;
                    ++dropped_incomplete_fragments_;
                    return true;
                }
                if (stap_nal_type == 5) {
                    append_cached_parameter_sets();
                    require_idr_ = false;
                }
                access_unit_has_vcl_ = true;
            }
            append_aud_if_needed(access_unit_);
            append_start_code(access_unit_);
            access_unit_.insert(access_unit_.end(), payload + offset, payload + offset + nal_size);
            offset += nal_size;
        }
    } else if (nal_type == 28 && size >= 2) {
        const auto fu_indicator = payload[0];
        const auto fu_header = payload[1];
        const bool start = (fu_header & 0x80U) != 0;
        const bool end = (fu_header & 0x40U) != 0;
        const auto reconstructed = static_cast<std::uint8_t>((fu_indicator & 0xE0U) | (fu_header & 0x1FU));
        if (start) {
            const auto reconstructed_nal_type = reconstructed & 0x1FU;
            if (nal_is_vcl(reconstructed_nal_type)) {
                if (require_idr_ && reconstructed_nal_type != 5) {
                    reset_access_unit();
                    dropping_timestamp_ = true;
                    drop_timestamp_ = timestamp;
                    ++dropped_waiting_for_idr_;
                    return true;
                }
                std::vector<std::uint8_t> first_fragment_nal;
                first_fragment_nal.reserve(size - 1U);
                first_fragment_nal.push_back(reconstructed);
                first_fragment_nal.insert(first_fragment_nal.end(), payload + 2, payload + size);
                if (!access_unit_has_vcl_ && !first_slice_starts_frame(first_fragment_nal.data(), first_fragment_nal.size())) {
                    reset_access_unit();
                    dropping_timestamp_ = true;
                    drop_timestamp_ = timestamp;
                    ++dropped_incomplete_fragments_;
                    return true;
                }
                if (reconstructed_nal_type == 5) {
                    append_cached_parameter_sets();
                    require_idr_ = false;
                }
                access_unit_has_vcl_ = true;
            }
            fu_started_ = true;
            fu_timestamp_ = timestamp;
            append_aud_if_needed(access_unit_);
            append_start_code(access_unit_);
            access_unit_.push_back(reconstructed);
        } else if (!fu_started_ || fu_timestamp_ != timestamp) {
            ++dropped_incomplete_fragments_;
            return true;
        }
        access_unit_.insert(access_unit_.end(), payload + 2, payload + size);
        if (end) {
            fu_started_ = false;
        }
    }

    if (marker && !access_unit_.empty() && !fu_started_) {
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
        reset_access_unit();
        for (int i = 0; i < 4; ++i) {
            const auto result = DecodeVideoStream(decoder_, 0, 0, 0, 0);
            if (result == VDECODE_RESULT_NO_BITSTREAM || result == VDECODE_RESULT_NO_FRAME_BUFFER) {
                break;
            }
        }
        return true;
    }
    if (buffer_size + ring_buffer_size < requested) {
        reset_access_unit();
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
    have_timestamp_ = false;
    access_unit_has_vcl_ = false;

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

void CedarRtpDecoder::reset_access_unit()
{
    access_unit_.clear();
    have_timestamp_ = false;
    fu_started_ = false;
    access_unit_has_vcl_ = false;
    ++dropped_access_units_;
}

bool CedarRtpDecoder::drain_picture(glide::dev::DmabufVideoFrame& frame)
{
    bool got_picture {};
    while (auto* picture = RequestPicture(decoder_, 0)) {
        ready_pictures_.push_back(picture);
        got_picture = true;
        ++decoded_frames_;
    }
    while (ready_pictures_.size() > 6U) {
        auto* stale_picture = ready_pictures_.front();
        ready_pictures_.pop_front();
        return_picture(stale_picture);
    }
    if (!got_picture && ready_pictures_.empty()) {
        return false;
    }
    if (current_picture_ != nullptr || ready_pictures_.empty()) {
        return false;
    }

    current_picture_ = ready_pictures_.front();
    ready_pictures_.pop_front();
    if (!picture_to_frame(current_picture_, frame)) {
        return false;
    }
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

void CedarRtpDecoder::cache_parameter_set(const std::uint8_t* nal, std::size_t size)
{
    if (nal == nullptr || size == 0) {
        return;
    }
    const auto nal_type = nal[0] & 0x1FU;
    if (nal_type == 7) {
        sps_.assign(nal, nal + size);
    } else if (nal_type == 8) {
        pps_.assign(nal, nal + size);
    }
}

void CedarRtpDecoder::append_cached_parameter_sets()
{
    if (!sps_.empty()) {
        append_aud_if_needed(access_unit_);
        append_start_code(access_unit_);
        access_unit_.insert(access_unit_.end(), sps_.begin(), sps_.end());
    }
    if (!pps_.empty()) {
        append_aud_if_needed(access_unit_);
        append_start_code(access_unit_);
        access_unit_.insert(access_unit_.end(), pps_.begin(), pps_.end());
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
    while (!ready_pictures_.empty()) {
        auto* picture = ready_pictures_.front();
        ready_pictures_.pop_front();
        return_picture(picture);
    }
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
