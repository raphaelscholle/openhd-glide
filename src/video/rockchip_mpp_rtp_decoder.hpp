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

#include "dev/kms_dmabuf_video_plane.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace glide::video {

class RockchipMppRtpDecoder {
public:
    RockchipMppRtpDecoder() = default;
    ~RockchipMppRtpDecoder();

    RockchipMppRtpDecoder(const RockchipMppRtpDecoder&) = delete;
    RockchipMppRtpDecoder& operator=(const RockchipMppRtpDecoder&) = delete;

    bool start(std::uint16_t udp_port, const std::string& codec);
    bool poll(glide::dev::DmabufVideoFrame& frame);
    void mark_presented();
    std::string stats() const;
    const std::string& last_error() const;

private:
    bool init_mpp(const std::string& codec);
    bool init_gstreamer(std::uint16_t udp_port, const std::string& codec);
    bool configure_mpp();
    void feed_loop();
    void frame_loop();
    bool submit_packet(const std::uint8_t* data, std::size_t size, std::int64_t pts);
    bool frame_to_dmabuf(void* frame, glide::dev::DmabufVideoFrame& out);
    void release_frame(void*& frame);
    void cleanup();

    void* ctx_ {};
    void* mpi_ {};
    void* gst_pipeline_ {};
    void* gst_sink_ {};
    std::thread feed_thread_;
    std::thread frame_thread_;
    std::atomic<bool> running_ {};
    mutable std::mutex mutex_;
    std::deque<void*> ready_frames_;
    void* current_frame_ {};
    void* pending_presented_frame_ {};
    std::uint64_t parsed_units_ {};
    std::uint64_t decoded_frames_ {};
    std::uint64_t dropped_decoded_frames_ {};
    std::uint64_t submitted_packets_ {};
    std::uint64_t submit_stalls_ {};
    std::atomic<std::uint64_t> gst_pull_timeouts_ {};
    std::string last_error_;
};

bool rockchip_mpp_decoder_available();

} // namespace glide::video
