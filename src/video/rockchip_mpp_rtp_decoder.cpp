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

#include "video/rockchip_mpp_rtp_decoder.hpp"

#if OPENHD_GLIDE_HAS_RKMPP
#include <rockchip/rk_mpi.h>

#include <drm_fourcc.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>
#endif

namespace glide::video {

#if OPENHD_GLIDE_HAS_RKMPP
namespace {
MppCtx as_ctx(void* ctx)
{
    return static_cast<MppCtx>(ctx);
}

MppApi* as_mpi(void* mpi)
{
    return static_cast<MppApi*>(mpi);
}

MppFrame as_frame(void* frame)
{
    return static_cast<MppFrame>(frame);
}
} // namespace
#endif

bool rockchip_mpp_decoder_available()
{
#if OPENHD_GLIDE_HAS_RKMPP
    return true;
#else
    return false;
#endif
}

RockchipMppRtpDecoder::~RockchipMppRtpDecoder()
{
    cleanup();
}

bool RockchipMppRtpDecoder::start(std::uint16_t udp_port, const std::string& codec)
{
#if OPENHD_GLIDE_HAS_RKMPP
    last_error_.clear();
    if (!init_mpp(codec)) {
        cleanup();
        return false;
    }
    if (!init_gstreamer(udp_port, codec)) {
        cleanup();
        return false;
    }
    running_.store(true, std::memory_order_release);
    feed_thread_ = std::thread(&RockchipMppRtpDecoder::feed_loop, this);
    frame_thread_ = std::thread(&RockchipMppRtpDecoder::frame_loop, this);
    return true;
#else
    (void)udp_port;
    (void)codec;
    last_error_ = "native Rockchip MPP decoder support was not found at build time";
    return false;
#endif
}

bool RockchipMppRtpDecoder::poll(glide::dev::DmabufVideoFrame& frame)
{
#if OPENHD_GLIDE_HAS_RKMPP
    void* selected {};
    {
        std::lock_guard lock(mutex_);
        if (ready_frames_.empty()) {
            return false;
        }
        while (ready_frames_.size() > 1) {
            auto dropped = ready_frames_.front();
            ready_frames_.pop_front();
            release_frame(dropped);
            ++dropped_decoded_frames_;
        }
        selected = ready_frames_.front();
        ready_frames_.pop_front();
    }

    if (pending_presented_frame_ != nullptr) {
        release_frame(pending_presented_frame_);
    }
    pending_presented_frame_ = selected;
    if (!frame_to_dmabuf(selected, frame)) {
        release_frame(pending_presented_frame_);
        return false;
    }
    return true;
#else
    (void)frame;
    return false;
#endif
}

void RockchipMppRtpDecoder::mark_presented()
{
#if OPENHD_GLIDE_HAS_RKMPP
    if (current_frame_ != nullptr) {
        release_frame(current_frame_);
    }
    current_frame_ = pending_presented_frame_;
    pending_presented_frame_ = nullptr;
#endif
}

std::string RockchipMppRtpDecoder::stats() const
{
#if OPENHD_GLIDE_HAS_RKMPP
    std::lock_guard lock(mutex_);
    std::ostringstream out;
    out << "parsed_units=" << parsed_units_
        << " submitted_packets=" << submitted_packets_
        << " decoded_frames=" << decoded_frames_
        << " queued_frames=" << ready_frames_.size()
        << " dropped_decoded_frames=" << dropped_decoded_frames_
        << " submit_stalls=" << submit_stalls_
        << " gst_pull_timeouts=" << gst_pull_timeouts_.load(std::memory_order_relaxed);
    return out.str();
#else
    return {};
#endif
}

const std::string& RockchipMppRtpDecoder::last_error() const
{
    return last_error_;
}

#if OPENHD_GLIDE_HAS_RKMPP
bool RockchipMppRtpDecoder::init_mpp(const std::string& codec)
{
    const bool h265 = codec == "h265" || codec == "hevc";
    const auto coding = h265 ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
    if (mpp_check_support_format(MPP_CTX_DEC, coding) != MPP_OK) {
        last_error_ = h265 ? "Rockchip MPP does not support HEVC decode" : "Rockchip MPP does not support AVC decode";
        return false;
    }
    MppCtx ctx {};
    MppApi* mpi {};
    if (mpp_create(&ctx, &mpi) != MPP_OK || ctx == nullptr || mpi == nullptr) {
        last_error_ = "mpp_create failed";
        return false;
    }
    ctx_ = ctx;
    mpi_ = mpi;
    configure_mpp();
    if (mpp_init(as_ctx(ctx_), MPP_CTX_DEC, coding) != MPP_OK) {
        last_error_ = "mpp_init decoder failed";
        return false;
    }
    configure_mpp();
    int output_block = MPP_POLL_BLOCK;
    as_mpi(mpi_)->control(as_ctx(ctx_), MPP_SET_OUTPUT_BLOCK, &output_block);
    return true;
}

bool RockchipMppRtpDecoder::configure_mpp()
{
    MppDecCfg cfg {};
    if (mpp_dec_cfg_init(&cfg) != MPP_OK) {
        return false;
    }
    if (mpi_ != nullptr && ctx_ != nullptr && as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_GET_CFG, cfg) == MPP_OK) {
        RK_U32 split_parse = 1;
        mpp_dec_cfg_set_u32(cfg, "base:split_parse", split_parse);
        as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_CFG, cfg);
    }
    mpp_dec_cfg_deinit(cfg);

    RK_U32 off {};
    RK_U32 on = 0xffff;
    if (mpi_ != nullptr && ctx_ != nullptr) {
        as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_PARSER_SPLIT_MODE, &off);
        as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_DISABLE_ERROR, &on);
        as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_IMMEDIATE_OUT, &on);
        as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_ENABLE_FAST_PLAY, &on);
        as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_PARSER_FAST_MODE, &off);
    }
    return true;
}

bool RockchipMppRtpDecoder::init_gstreamer(std::uint16_t udp_port, const std::string& codec)
{
    gst_init(nullptr, nullptr);
    const bool h265 = codec == "h265" || codec == "hevc";
    std::ostringstream text;
    text << "udpsrc port=" << udp_port
         << " buffer-size=33554432 caps=\"application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)"
         << (h265 ? "H265" : "H264")
         << ",payload=(int)96\" ! "
         << (h265 ? "rtph265depay ! h265parse config-interval=-1 ! video/x-h265,stream-format=byte-stream,alignment=nal ! "
                  : "rtph264depay ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=nal ! ")
         << "appsink name=encoded-sink sync=false async=false drop=true max-buffers=32 emit-signals=false";

    GError* error {};
    auto* pipeline = gst_parse_launch(text.str().c_str(), &error);
    if (pipeline == nullptr || error != nullptr) {
        last_error_ = std::string("failed to create Rockchip MPP RTP parser pipeline: ")
            + (error != nullptr ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        if (pipeline != nullptr) {
            gst_object_unref(pipeline);
        }
        return false;
    }
    auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "encoded-sink");
    if (sink == nullptr) {
        last_error_ = "failed to access Rockchip MPP encoded appsink";
        gst_object_unref(pipeline);
        return false;
    }
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        last_error_ = "failed to start Rockchip MPP RTP parser pipeline";
        gst_object_unref(sink);
        gst_object_unref(pipeline);
        return false;
    }
    gst_pipeline_ = pipeline;
    gst_sink_ = sink;
    return true;
}

void RockchipMppRtpDecoder::feed_loop()
{
    auto* sink = static_cast<GstElement*>(gst_sink_);
    while (running_.load(std::memory_order_acquire)) {
        auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 100 * GST_MSECOND);
        if (sample == nullptr) {
            gst_pull_timeouts_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        auto* buffer = gst_sample_get_buffer(sample);
        if (buffer != nullptr) {
            GstMapInfo map {};
            if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                const auto pts = GST_BUFFER_PTS_IS_VALID(buffer) ? static_cast<std::int64_t>(GST_BUFFER_PTS(buffer) / GST_MSECOND) : 0;
                submit_packet(map.data, map.size, pts);
                gst_buffer_unmap(buffer, &map);
            }
        }
        gst_sample_unref(sample);
    }
}

bool RockchipMppRtpDecoder::submit_packet(const std::uint8_t* data, std::size_t size, std::int64_t pts)
{
    if (data == nullptr || size == 0 || mpi_ == nullptr || ctx_ == nullptr) {
        return false;
    }
    MppPacket packet {};
    if (mpp_packet_init(&packet, const_cast<std::uint8_t*>(data), size) != MPP_OK) {
        return false;
    }
    mpp_packet_set_pos(packet, const_cast<std::uint8_t*>(data));
    mpp_packet_set_length(packet, size);
    mpp_packet_set_pts(packet, static_cast<RK_S64>(pts));

    const auto start = std::chrono::steady_clock::now();
    MPP_RET ret {};
    while (running_.load(std::memory_order_acquire)) {
        ret = as_mpi(mpi_)->decode_put_packet(as_ctx(ctx_), packet);
        if (ret == MPP_OK) {
            {
                std::lock_guard lock(mutex_);
                ++parsed_units_;
                ++submitted_packets_;
            }
            mpp_packet_deinit(&packet);
            return true;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::milliseconds(100)) {
            std::lock_guard lock(mutex_);
            ++submit_stalls_;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    mpp_packet_deinit(&packet);
    return false;
}

void RockchipMppRtpDecoder::frame_loop()
{
    while (running_.load(std::memory_order_acquire)) {
        MppFrame frame {};
        const auto ret = mpi_ != nullptr ? as_mpi(mpi_)->decode_get_frame(as_ctx(ctx_), &frame) : MPP_NOK;
        if (!running_.load(std::memory_order_acquire)) {
            release_frame(frame);
            break;
        }
        if (ret != MPP_OK || frame == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (mpp_frame_get_info_change(frame)) {
            as_mpi(mpi_)->control(as_ctx(ctx_), MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
            release_frame(frame);
            continue;
        }
        if (mpp_frame_get_errinfo(frame) || mpp_frame_get_discard(frame)) {
            release_frame(frame);
            continue;
        }
        if (mpp_frame_get_buffer(frame) == nullptr) {
            release_frame(frame);
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            while (ready_frames_.size() >= 4) {
                auto dropped = ready_frames_.front();
                ready_frames_.pop_front();
                release_frame(dropped);
                ++dropped_decoded_frames_;
            }
                ready_frames_.push_back(frame);
            ++decoded_frames_;
        }
    }
}

bool RockchipMppRtpDecoder::frame_to_dmabuf(void* frame_ptr, glide::dev::DmabufVideoFrame& out)
{
    auto frame = as_frame(frame_ptr);
    auto* buffer = mpp_frame_get_buffer(frame);
    if (buffer == nullptr) {
        last_error_ = "MPP decoded frame is missing a buffer";
        return false;
    }
    MppBufferInfo info {};
    if (mpp_buffer_info_get(buffer, &info) != MPP_OK || info.fd < 0) {
        last_error_ = "MPP decoded frame buffer is not fd-backed";
        return false;
    }
    const auto width = static_cast<std::uint32_t>(mpp_frame_get_width(frame));
    const auto height = static_cast<std::uint32_t>(mpp_frame_get_height(frame));
    const auto hstride = static_cast<std::uint32_t>(mpp_frame_get_hor_stride(frame));
    const auto vstride = static_cast<std::uint32_t>(mpp_frame_get_ver_stride(frame));
    const auto fmt = mpp_frame_get_fmt(frame);
    if (width == 0 || height == 0 || hstride == 0 || vstride == 0) {
        last_error_ = "MPP decoded frame has invalid dimensions";
        return false;
    }
    if (fmt != MPP_FMT_YUV420SP) {
        last_error_ = "MPP decoded frame format is not NV12";
        return false;
    }
    out = {};
    out.width = width;
    out.height = height;
    out.drm_format = DRM_FORMAT_NV12;
    out.plane_count = 2;
    out.fds[0] = info.fd;
    out.fds[1] = info.fd;
    out.strides[0] = hstride;
    out.strides[1] = hstride;
    out.offsets[0] = 0;
    out.offsets[1] = hstride * vstride;
    return true;
}

void RockchipMppRtpDecoder::release_frame(void*& frame)
{
    if (frame != nullptr) {
        auto native_frame = as_frame(frame);
        mpp_frame_deinit(&native_frame);
        frame = nullptr;
    }
}
#endif

void RockchipMppRtpDecoder::cleanup()
{
#if OPENHD_GLIDE_HAS_RKMPP
    running_.store(false, std::memory_order_release);
    if (gst_pipeline_ != nullptr) {
        gst_element_send_event(static_cast<GstElement*>(gst_pipeline_), gst_event_new_eos());
    }
    if (feed_thread_.joinable()) {
        feed_thread_.join();
    }
    if (frame_thread_.joinable()) {
        frame_thread_.join();
    }
    {
        std::lock_guard lock(mutex_);
        for (auto& frame : ready_frames_) {
            release_frame(frame);
        }
        ready_frames_.clear();
    }
    release_frame(pending_presented_frame_);
    release_frame(current_frame_);
    if (gst_pipeline_ != nullptr) {
        gst_element_set_state(static_cast<GstElement*>(gst_pipeline_), GST_STATE_NULL);
    }
    if (gst_sink_ != nullptr) {
        gst_object_unref(static_cast<GstElement*>(gst_sink_));
        gst_sink_ = nullptr;
    }
    if (gst_pipeline_ != nullptr) {
        gst_object_unref(static_cast<GstElement*>(gst_pipeline_));
        gst_pipeline_ = nullptr;
    }
    if (mpi_ != nullptr && ctx_ != nullptr) {
        as_mpi(mpi_)->reset(as_ctx(ctx_));
    }
    if (ctx_ != nullptr) {
        mpp_destroy(as_ctx(ctx_));
        ctx_ = nullptr;
        mpi_ = nullptr;
    }
#endif
}

} // namespace glide::video
