#include "common/logging.hpp"
#include "common/ipc.hpp"
#include "common/preview_control.hpp"
#include "dev/kms_dmabuf_video_plane.hpp"
#include "platform/core_assignment.hpp"
#include "platform/cpu_topology.hpp"
#include "platform/drm_probe.hpp"
#include "platform/process_probe.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#if OPENHD_GLIDE_HAS_GSTREAMER
#include <gst/allocators/gstdmabuf.h>
#include <gst/allocators/gstfdmemory.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#endif

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
    stop_requested = 1;
}

struct Options {
    bool preview_stack {};
    bool kms_stack {};
    bool kms_video_preview {};
    std::uint32_t preview_width { 1280 };
    std::uint32_t flow_height { 720 };
    std::uint32_t ui_width { 760 };
    std::uint32_t ui_height { 720 };
    std::uint16_t view_udp_port { 5600 };
    int view_plane_id { -1 };
    int view_connector_id { -1 };
    float ui_opacity { 0.35F };
    int preview_x { 80 };
    int preview_y { 40 };
    std::string ipc_socket { glide::ipc::default_socket_path };
    bool vertical_stack {};
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--preview-stack") {
            options.preview_stack = true;
        } else if (argument == "--kms-stack" || argument == "--kmd-stack") {
            options.kms_stack = true;
        } else if (argument == "--kms-video-preview") {
            options.kms_video_preview = true;
        } else if (argument == "--preview-width" && i + 1 < argc) {
            options.preview_width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--ui-height" && i + 1 < argc) {
            options.ui_height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--ui-width" && i + 1 < argc) {
            options.ui_width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--flow-height" && i + 1 < argc) {
            options.flow_height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--view-udp-port" && i + 1 < argc) {
            options.view_udp_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        } else if (argument == "--view-plane-id" && i + 1 < argc) {
            options.view_plane_id = std::stoi(argv[++i]);
        } else if (argument == "--view-connector-id" && i + 1 < argc) {
            options.view_connector_id = std::stoi(argv[++i]);
        } else if (argument == "--preview-x" && i + 1 < argc) {
            options.preview_x = std::stoi(argv[++i]);
        } else if (argument == "--preview-y" && i + 1 < argc) {
            options.preview_y = std::stoi(argv[++i]);
        } else if (argument == "--ui-opacity" && i + 1 < argc) {
            options.ui_opacity = std::clamp(std::stof(argv[++i]), 0.05F, 1.0F);
        } else if (argument == "--ipc-socket" && i + 1 < argc) {
            options.ipc_socket = argv[++i];
        } else if (argument == "--vertical-stack") {
            options.vertical_stack = true;
        }
    }
    return options;
}

std::string executable_dir(char* argv0)
{
    const std::string path = argv0 != nullptr ? argv0 : "";
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash + 1);
}

std::string sibling_executable(char* argv0, const char* name)
{
    const auto dir = executable_dir(argv0);
    if (dir.empty()) {
        return name;
    }
    return dir + name;
}

std::vector<std::string> preview_args(const char* executable, const Options& options, bool ui)
{
    const auto y = (ui || !options.vertical_stack) ? options.preview_y : options.preview_y + static_cast<int>(options.ui_height);
    const auto height = ui ? options.ui_height : options.flow_height;
    const auto width = ui ? options.ui_width : options.preview_width;
    auto args = std::vector<std::string> {
        executable,
        "--preview",
        "--width",
        std::to_string(width),
        "--height",
        std::to_string(height),
        "--x",
        std::to_string(options.preview_x),
        "--y",
        std::to_string(y),
        "--borderless",
        "--ipc-socket",
        options.ipc_socket,
    };
    if (ui) {
        args.emplace_back("--always-on-top");
        args.emplace_back("--transparent-clear");
        args.emplace_back("--opacity");
        args.emplace_back(std::to_string(options.ui_opacity));
    }
    return args;
}

std::vector<std::string> view_args(const char* executable, const Options& options)
{
    auto args = std::vector<std::string> {
        executable,
        "--stay-alive",
        "--ipc-socket",
        options.ipc_socket,
    };
    if (options.kms_stack) {
        args.emplace_back("--udp-video");
        args.emplace_back("--udp-port");
        args.emplace_back(std::to_string(options.view_udp_port));
    }
    return args;
}

std::vector<std::string> kms_flow_args(const char* executable, const Options& options)
{
    return {
        executable,
        "--kms",
        "--stay-alive",
        "--width",
        std::to_string(options.preview_width),
        "--height",
        std::to_string(options.flow_height),
        "--ipc-socket",
        options.ipc_socket,
    };
}

std::vector<std::string> headless_ui_args(const char* executable, const Options& options)
{
    return {
        executable,
        "--headless",
        "--ipc-socket",
        options.ipc_socket,
    };
}

#if OPENHD_GLIDE_HAS_GSTREAMER
constexpr std::uint32_t fourcc(char a, char b, char c, char d)
{
    return static_cast<std::uint32_t>(a)
        | (static_cast<std::uint32_t>(b) << 8U)
        | (static_cast<std::uint32_t>(c) << 16U)
        | (static_cast<std::uint32_t>(d) << 24U);
}

std::uint32_t drm_format_from_name(std::string name)
{
    const auto modifier_separator = name.find(':');
    if (modifier_separator != std::string::npos) {
        name = name.substr(0, modifier_separator);
    }
    if (name == "YU12" || name == "I420") {
        return fourcc('Y', 'U', '1', '2');
    }
    if (name == "NV12") {
        return fourcc('N', 'V', '1', '2');
    }
    if (name == "XR24" || name == "BGRx") {
        return fourcc('X', 'R', '2', '4');
    }
    return 0;
}

std::string caps_to_string(GstCaps* caps)
{
    if (caps == nullptr) {
        return "unknown-caps";
    }
    auto* text = gst_caps_to_string(caps);
    std::string result = text != nullptr ? text : "unknown-caps";
    if (text != nullptr) {
        g_free(text);
    }
    return result;
}

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment)
{
    return ((value + alignment - 1U) / alignment) * alignment;
}

std::string describe_memory(GstMemory* memory)
{
    if (memory == nullptr) {
        return "null-memory";
    }

    gsize offset {};
    const auto size = gst_memory_get_sizes(memory, &offset, nullptr);
    const auto* allocator = memory->allocator;
    const auto* memory_type = allocator != nullptr && allocator->mem_type != nullptr ? allocator->mem_type : "unknown";
    std::ostringstream description;
    description << "type=" << memory_type
                << " size=" << size
                << " offset=" << offset
                << " dmabuf=" << (gst_is_dmabuf_memory(memory) ? "yes" : "no")
                << " fd=" << (gst_is_fd_memory(memory) ? "yes" : "no");
    return description.str();
}

std::string describe_buffer_memories(GstBuffer* buffer)
{
    if (buffer == nullptr) {
        return "buffer=null";
    }
    std::ostringstream description;
    const auto memory_count = gst_buffer_n_memory(buffer);
    description << "memory_count=" << memory_count;
    for (guint i = 0; i < memory_count; ++i) {
        description << " mem[" << i << "]={" << describe_memory(gst_buffer_peek_memory(buffer, i)) << "}";
    }
    return description.str();
}

int fd_from_memory(GstMemory* memory)
{
    if (memory == nullptr) {
        return -1;
    }
    if (gst_is_dmabuf_memory(memory)) {
        return gst_dmabuf_memory_get_fd(memory);
    }
    if (gst_is_fd_memory(memory)) {
        return gst_fd_memory_get_fd(memory);
    }
    return -1;
}

std::uint32_t infer_plane_count(std::uint32_t drm_format)
{
    if (drm_format == fourcc('Y', 'U', '1', '2')) {
        return 3;
    }
    if (drm_format == fourcc('N', 'V', '1', '2')) {
        return 2;
    }
    if (drm_format == fourcc('X', 'R', '2', '4')) {
        return 1;
    }
    return 0;
}

bool infer_video_layout(
    std::uint32_t drm_format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t memory_count,
    std::array<std::uint32_t, 4>& strides,
    std::array<std::uint32_t, 4>& offsets)
{
    strides = {};
    offsets = {};

    if (drm_format == fourcc('Y', 'U', '1', '2')) {
        const auto y_stride = align_up(width, 32);
        const auto uv_stride = align_up(width / 2U, 16);
        const auto y_size = y_stride * height;
        const auto uv_size = uv_stride * ((height + 1U) / 2U);
        strides[0] = y_stride;
        strides[1] = uv_stride;
        strides[2] = uv_stride;
        if (memory_count == 1) {
            offsets[1] = y_size;
            offsets[2] = y_size + uv_size;
        }
        return true;
    }

    if (drm_format == fourcc('N', 'V', '1', '2')) {
        const auto y_stride = align_up(width, 32);
        strides[0] = y_stride;
        strides[1] = y_stride;
        if (memory_count == 1) {
            offsets[1] = y_stride * height;
        }
        return true;
    }

    if (drm_format == fourcc('X', 'R', '2', '4')) {
        strides[0] = align_up(width * 4U, 64);
        return true;
    }

    return false;
}

bool extract_dmabuf_frame(GstSample* sample, glide::dev::DmabufVideoFrame& frame, std::string& error)
{
    auto* caps = gst_sample_get_caps(sample);
    auto* structure = caps != nullptr ? gst_caps_get_structure(caps, 0) : nullptr;
    auto* buffer = gst_sample_get_buffer(sample);
    auto* meta = buffer != nullptr ? gst_buffer_get_video_meta(buffer) : nullptr;
    if (structure == nullptr || buffer == nullptr) {
        error = "decoded sample is missing caps or buffer";
        return false;
    }

    int width {};
    int height {};
    if (!gst_structure_get_int(structure, "width", &width) || !gst_structure_get_int(structure, "height", &height) || width <= 0 || height <= 0) {
        error = "decoded sample has invalid dimensions";
        return false;
    }

    const auto* drm_format_text = gst_structure_get_string(structure, "drm-format");
    if (drm_format_text == nullptr) {
        drm_format_text = gst_structure_get_string(structure, "format");
    }
    const auto drm_format = drm_format_text != nullptr ? drm_format_from_name(drm_format_text) : 0;
    if (drm_format == 0) {
        error = "unsupported decoded DRM format in caps: " + caps_to_string(caps);
        return false;
    }

    const auto plane_count = meta != nullptr ? meta->n_planes : infer_plane_count(drm_format);
    if (plane_count == 0 || plane_count > frame.fds.size()) {
        error = "decoded sample has invalid video plane metadata";
        return false;
    }

    const auto memory_count = gst_buffer_n_memory(buffer);
    if (memory_count == 0) {
        error = "decoded sample contains no memory";
        return false;
    }

    frame = {};
    frame.fds = { -1, -1, -1, -1 };
    frame.width = static_cast<std::uint32_t>(width);
    frame.height = static_cast<std::uint32_t>(height);
    frame.drm_format = drm_format;
    frame.plane_count = plane_count;

    std::array<std::uint32_t, 4> inferred_strides {};
    std::array<std::uint32_t, 4> inferred_offsets {};
    if (meta == nullptr && !infer_video_layout(
                               drm_format,
                               frame.width,
                               frame.height,
                               static_cast<std::uint32_t>(memory_count),
                               inferred_strides,
                               inferred_offsets)) {
        error = "decoded sample is missing video metadata and layout could not be inferred";
        return false;
    }

    for (std::uint32_t plane = 0; plane < plane_count; ++plane) {
        const auto memory_index = memory_count == 1 ? 0 : plane;
        auto* memory = gst_buffer_peek_memory(buffer, memory_index);
        const auto fd = fd_from_memory(memory);
        if (fd < 0) {
            error = "decoded sample memory is not DMABUF/fd-backed despite DMABUF caps; " + describe_buffer_memories(buffer);
            return false;
        }
        frame.fds[plane] = fd;
        frame.strides[plane] = meta != nullptr ? static_cast<std::uint32_t>(meta->stride[plane]) : inferred_strides[plane];
        frame.offsets[plane] = meta != nullptr ? static_cast<std::uint32_t>(meta->offset[plane]) : inferred_offsets[plane];
    }
    return true;
}
#endif

int run_kms_video_preview(const Options& options)
{
#if OPENHD_GLIDE_HAS_GSTREAMER && OPENHD_GLIDE_DEVICE_KMS
    stop_requested = 0;
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    glide::dev::KmsDmabufVideoPlane output;
    if (!output.create(options.preview_width, options.flow_height, options.view_plane_id)) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", output.last_error());
        return 1;
    }

    gst_init(nullptr, nullptr);
    const auto make_pipeline_text = [&](bool force_dmabuf_capture) {
        std::ostringstream text;
        text
            << "udpsrc port=" << options.view_udp_port
            << " caps=\"application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264,payload=(int)96\" "
            << "! rtph264depay "
            << "! h264parse config-interval=-1 "
            << "! video/x-h264,stream-format=byte-stream,alignment=au "
            << "! v4l2h264dec ";
        if (force_dmabuf_capture) {
            text << "capture-io-mode=dmabuf ";
        }
        text
            << "! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream "
            << "! video/x-raw(memory:DMABuf),format=DMA_DRM "
            << "! appsink name=video-sink sync=false async=false drop=true max-buffers=4 emit-signals=false";
        return text.str();
    };

    GError* error {};
    const auto forced_pipeline_text = make_pipeline_text(true);
    auto* pipeline = gst_parse_launch(forced_pipeline_text.c_str(), &error);
    if (pipeline == nullptr) {
        glide::log(glide::LogLevel::warning, "OpenHD-Glide", error != nullptr ? error->message : "failed to create forced-DMABUF video preview pipeline");
        if (error != nullptr) {
            g_error_free(error);
            error = nullptr;
        }

        const auto fallback_pipeline_text = make_pipeline_text(false);
        pipeline = gst_parse_launch(fallback_pipeline_text.c_str(), &error);
        if (pipeline == nullptr) {
            glide::log(glide::LogLevel::error, "OpenHD-Glide", error != nullptr ? error->message : "failed to create video preview pipeline");
            if (error != nullptr) {
                g_error_free(error);
            }
            return 1;
        }
        glide::log(glide::LogLevel::warning, "OpenHD-Glide", "v4l2h264dec forced DMABUF capture was unavailable; using decoder auto mode");
    }
    if (error != nullptr) {
        glide::log(glide::LogLevel::warning, "OpenHD-Glide", error->message);
        g_error_free(error);
    }

    auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "video-sink");
    auto* bus = gst_element_get_bus(pipeline);
    if (sink == nullptr || bus == nullptr) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to access video preview appsink");
        if (bus != nullptr) {
            gst_object_unref(bus);
        }
        if (sink != nullptr) {
            gst_object_unref(sink);
        }
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to start controller-owned KMS video preview");
        gst_object_unref(bus);
        gst_object_unref(sink);
        gst_object_unref(pipeline);
        return 1;
    }

    glide::log(glide::LogLevel::info, "OpenHD-Glide", "controller-owned KMS DMABUF video plane running");
    glide::log(glide::LogLevel::info, "OpenHD-Glide", "KMS video importer build: forced decoder DMABUF capture + fd-backed memory diagnostics");
    glide::log(glide::LogLevel::info, "OpenHD-Glide", "decoded DMABUF frames are imported directly into DRM and scanned out on a KMS plane");

    std::uint64_t frames {};
    std::uint64_t frames_since_log {};
    auto last_log = std::chrono::steady_clock::now();
    GstSample* scanout_sample {};
    bool logged_caps {};
    std::uint32_t consecutive_frame_errors {};
    while (stop_requested == 0) {
        while (auto* message = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError* gst_error {};
                gchar* debug {};
                gst_message_parse_error(message, &gst_error, &debug);
                glide::log(glide::LogLevel::error, "OpenHD-Glide", gst_error != nullptr ? gst_error->message : "GStreamer error");
                if (debug != nullptr) {
                    glide::log(glide::LogLevel::warning, "OpenHD-Glide", debug);
                    g_free(debug);
                }
                if (gst_error != nullptr) {
                    g_error_free(gst_error);
                }
                gst_message_unref(message);
                stop_requested = 1;
                break;
            }
            glide::log(glide::LogLevel::warning, "OpenHD-Glide", "video preview pipeline reached EOS");
            gst_message_unref(message);
            stop_requested = 1;
            break;
        }

        auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), GST_SECOND / 20);
        if (sample == nullptr) {
            continue;
        }

        if (!logged_caps) {
            glide::log(glide::LogLevel::info, "OpenHD-Glide", "first DMABUF video sample caps=" + caps_to_string(gst_sample_get_caps(sample)));
            logged_caps = true;
        }

        glide::dev::DmabufVideoFrame frame;
        std::string frame_error;
        if (!extract_dmabuf_frame(sample, frame, frame_error)) {
            ++consecutive_frame_errors;
            glide::log(glide::LogLevel::warning, "OpenHD-Glide", "dropping decoded sample: " + frame_error);
            gst_sample_unref(sample);
            if (consecutive_frame_errors >= 30) {
                glide::log(glide::LogLevel::error, "OpenHD-Glide", "too many consecutive decoded samples could not be imported");
                stop_requested = 1;
                break;
            }
            continue;
        }
        consecutive_frame_errors = 0;

        if (output.present(frame)) {
            if (scanout_sample != nullptr) {
                gst_sample_unref(scanout_sample);
            }
            scanout_sample = sample;
            ++frames;
            ++frames_since_log;
        } else {
            glide::log(glide::LogLevel::error, "OpenHD-Glide", output.last_error());
            gst_sample_unref(sample);
            stop_requested = 1;
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_log >= std::chrono::seconds(1)) {
            const auto elapsed = std::chrono::duration<double>(now - last_log).count();
            std::ostringstream status;
            status.setf(std::ios::fixed);
            status.precision(1);
            status << "KMS DMABUF video plane fps=" << (static_cast<double>(frames_since_log) / elapsed)
                   << " total_frames=" << frames;
            glide::log(glide::LogLevel::info, "OpenHD-Glide", status.str());
            frames_since_log = 0;
            last_log = now;
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (scanout_sample != nullptr) {
        gst_sample_unref(scanout_sample);
    }
    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
    return 0;
#else
    (void)options;
    glide::log(glide::LogLevel::error, "OpenHD-Glide", "--kms-video-preview requires GStreamer and device KMS support");
    return 1;
#endif
}

#if defined(__linux__)
pid_t launch_child(const std::vector<std::string>& args)
{
    const auto pid = fork();
    if (pid != 0) {
        return pid;
    }

    std::vector<char*> exec_args;
    exec_args.reserve(args.size() + 1U);
    for (const auto& arg : args) {
        exec_args.push_back(const_cast<char*>(arg.c_str()));
    }
    exec_args.push_back(nullptr);

    execv(exec_args.front(), exec_args.data());
    execvp(exec_args.front(), exec_args.data());
    _exit(127);
}

void terminate_child(pid_t pid)
{
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

int run_preview_stack(char* argv0, const Options& options)
{
    stop_requested = 0;
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    const auto ui_executable = sibling_executable(argv0, "glide-ui");
    const auto flow_executable = sibling_executable(argv0, "glide-flow");
    const auto view_executable = sibling_executable(argv0, "glide-view");
    const auto flow_args = preview_args(flow_executable.c_str(), options, false);
    const auto ui_args = preview_args(ui_executable.c_str(), options, true);
    const auto video_args = view_args(view_executable.c_str(), options);

    glide::preview_control::set_fps_overlay_enabled(true);
    glide::ipc::Server ipc_server;
    if (!ipc_server.listen_on(options.ipc_socket)) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to start IPC server: " + ipc_server.last_error());
        return 1;
    }

    glide::log(glide::LogLevel::info, "OpenHD-Glide", "starting WSL preview stack");
    const auto view_pid = launch_child(video_args);
    if (view_pid < 0) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to launch glide-view worker");
        return 1;
    }

    const auto flow_pid = launch_child(flow_args);
    if (flow_pid < 0) {
        terminate_child(view_pid);
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to launch glide-flow preview");
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto ui_pid = launch_child(ui_args);
    if (ui_pid < 0) {
        terminate_child(view_pid);
        terminate_child(flow_pid);
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to launch glide-ui preview");
        return 1;
    }

    const auto flow_y = options.vertical_stack ? options.preview_y + static_cast<int>(options.ui_height) : options.preview_y;
    std::cout << "preview stack:\n"
              << "  glide-ui   x=" << options.preview_x << " y=" << options.preview_y
              << " width=" << options.ui_width << " height=" << options.ui_height
              << " opacity=" << options.ui_opacity << '\n'
              << "  glide-flow x=" << options.preview_x << " y=" << flow_y
              << " width=" << options.preview_width << " height=" << options.flow_height << '\n'
              << "  ipc        " << options.ipc_socket << '\n';

    bool fps_enabled = true;

    while (stop_requested == 0) {
        for (const auto& event : ipc_server.poll()) {
            std::cout << "ipc[" << event.client_id << "] " << event.line << '\n';
            if (event.line.rfind("hello ", 0) == 0) {
                ipc_server.send_line(event.client_id, std::string("state fps ") + (fps_enabled ? "1" : "0"));
            } else if (event.line == "get fps") {
                ipc_server.send_line(event.client_id, std::string("state fps ") + (fps_enabled ? "1" : "0"));
            } else if (event.line == "set fps 0" || event.line == "set fps 1") {
                fps_enabled = event.line.back() == '1';
                glide::preview_control::set_fps_overlay_enabled(fps_enabled);
                ipc_server.broadcast_line(std::string("state fps ") + (fps_enabled ? "1" : "0"));
            } else if (event.line == "ping") {
                ipc_server.send_line(event.client_id, "pong");
            }
        }

        int status {};
        const auto exited = waitpid(-1, &status, WNOHANG);
        if (exited == ui_pid) {
            terminate_child(view_pid);
            terminate_child(flow_pid);
            break;
        }
        if (exited == flow_pid) {
            terminate_child(view_pid);
            terminate_child(ui_pid);
            break;
        }
        if (exited == view_pid) {
            terminate_child(flow_pid);
            terminate_child(ui_pid);
            break;
        }
        if (exited < 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    terminate_child(view_pid);
    terminate_child(ui_pid);
    terminate_child(flow_pid);
    waitpid(view_pid, nullptr, 0);
    waitpid(ui_pid, nullptr, 0);
    waitpid(flow_pid, nullptr, 0);
    glide::log(glide::LogLevel::info, "OpenHD-Glide", "preview stack stopped");
    return 0;
}

int run_kms_stack(char* argv0, const Options& options)
{
#if OPENHD_GLIDE_DEVICE_KMS
    stop_requested = 0;
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    const auto ui_executable = sibling_executable(argv0, "glide-ui");
    const auto flow_executable = sibling_executable(argv0, "glide-flow");
    const auto view_executable = sibling_executable(argv0, "glide-view");
    const auto flow_args = kms_flow_args(flow_executable.c_str(), options);
    const auto ui_args = headless_ui_args(ui_executable.c_str(), options);
    const auto video_args = view_args(view_executable.c_str(), options);

    glide::preview_control::set_fps_overlay_enabled(true);
    glide::ipc::Server ipc_server;
    if (!ipc_server.listen_on(options.ipc_socket)) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to start IPC server: " + ipc_server.last_error());
        return 1;
    }

    glide::log(glide::LogLevel::info, "OpenHD-Glide", "starting device DRM/KMS stack");
    const auto view_pid = launch_child(video_args);
    if (view_pid < 0) {
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to launch glide-view worker");
        return 1;
    }

    const auto flow_pid = launch_child(flow_args);
    if (flow_pid < 0) {
        terminate_child(view_pid);
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to launch glide-flow KMS worker");
        return 1;
    }

    const auto ui_pid = launch_child(ui_args);
    if (ui_pid < 0) {
        terminate_child(view_pid);
        terminate_child(flow_pid);
        glide::log(glide::LogLevel::error, "OpenHD-Glide", "failed to launch glide-ui headless worker");
        return 1;
    }

    std::cout << "device KMS stack:\n"
              << "  glide-view UDP RTP/H264 decode port=" << options.view_udp_port
              << " (no KMS ownership)\n";
    if (options.view_plane_id >= 0) {
        glide::log(glide::LogLevel::warning, "OpenHD-Glide", "--view-plane-id is ignored until openhd-glide owns video plane import");
    }
    if (options.view_connector_id >= 0) {
        glide::log(glide::LogLevel::warning, "OpenHD-Glide", "--view-connector-id is ignored until openhd-glide owns video plane import");
    }
    std::cout << "  glide-flow drm/kms mode width=" << options.preview_width
              << " height=" << options.flow_height << '\n'
              << "  glide-ui   headless LVGL control worker until shared-buffer UI backend exists\n"
              << "  ipc        " << options.ipc_socket << '\n';

    bool fps_enabled = true;

    while (stop_requested == 0) {
        for (const auto& event : ipc_server.poll()) {
            std::cout << "ipc[" << event.client_id << "] " << event.line << '\n';
            if (event.line.rfind("hello ", 0) == 0 || event.line == "get fps") {
                ipc_server.send_line(event.client_id, std::string("state fps ") + (fps_enabled ? "1" : "0"));
            } else if (event.line == "set fps 0" || event.line == "set fps 1") {
                fps_enabled = event.line.back() == '1';
                glide::preview_control::set_fps_overlay_enabled(fps_enabled);
                ipc_server.broadcast_line(std::string("state fps ") + (fps_enabled ? "1" : "0"));
            } else if (event.line == "ping") {
                ipc_server.send_line(event.client_id, "pong");
            }
        }

        int status {};
        const auto exited = waitpid(-1, &status, WNOHANG);
        if (exited == ui_pid || exited == flow_pid || exited == view_pid || exited < 0) {
            terminate_child(view_pid);
            terminate_child(ui_pid);
            terminate_child(flow_pid);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    terminate_child(view_pid);
    terminate_child(ui_pid);
    terminate_child(flow_pid);
    waitpid(view_pid, nullptr, 0);
    waitpid(ui_pid, nullptr, 0);
    waitpid(flow_pid, nullptr, 0);
    glide::log(glide::LogLevel::info, "OpenHD-Glide", "device DRM/KMS stack stopped");
    return 0;
#else
    glide::log(glide::LogLevel::error, "OpenHD-Glide", "device DRM/KMS mode is disabled in this build");
    return 1;
#endif
}
#else
int run_preview_stack(char*, const Options&)
{
    glide::log(glide::LogLevel::error, "OpenHD-Glide", "--preview-stack currently requires Linux/WSL process launching");
    return 1;
}

int run_kms_stack(char*, const Options&)
{
    glide::log(glide::LogLevel::error, "OpenHD-Glide", "--kms-stack requires Linux process launching and DRM/KMS");
    return 1;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    glide::log(glide::LogLevel::info, "OpenHD-Glide", "starting hardware discovery");

    const auto drm = glide::platform::probe_drm_planes();
    std::cout << glide::platform::describe_drm_probe(drm);

    const auto cpu = glide::platform::probe_cpu_topology();
    std::cout << glide::platform::describe_cpu_topology(cpu);

    const auto openhd_running = glide::platform::is_openhd_process_running();
    if (openhd_running) {
        glide::log(glide::LogLevel::info, "OpenHD-Glide", "OpenHD process detected; avoiding cpu0 for workers");
    }

    const auto assignments = glide::platform::assign_worker_cores(
        cpu,
        glide::platform::CoreAssignmentOptions {
            .avoid_core0 = openhd_running,
        });
    std::cout << glide::platform::describe_worker_core_assignments(assignments);

    glide::log(glide::LogLevel::info, "OpenHD-Glide", "hardware discovery complete");

    if (options.preview_stack) {
        return run_preview_stack(argv[0], options);
    }
    if (options.kms_video_preview) {
        return run_kms_video_preview(options);
    }
    if (options.kms_stack) {
        return run_kms_stack(argv[0], options);
    }

    return 0;
}
