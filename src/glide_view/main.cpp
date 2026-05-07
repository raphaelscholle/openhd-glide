#include "common/ipc.hpp"
#include "common/logging.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#if OPENHD_GLIDE_HAS_GSTREAMER
#include <gst/gst.h>
#endif

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
    stop_requested = 1;
}

struct Options {
    std::string ipc_socket { glide::ipc::default_socket_path };
    std::uint16_t udp_port { 5600 };
    std::optional<int> plane_id;
    std::optional<int> connector_id;
    bool stay_alive {};
    bool udp_video {};
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--ipc-socket" && i + 1 < argc) {
            options.ipc_socket = argv[++i];
        } else if (argument == "--stay-alive") {
            options.stay_alive = true;
        } else if (argument == "--udp-video") {
            options.udp_video = true;
        } else if (argument == "--udp-port" && i + 1 < argc) {
            options.udp_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
            options.udp_video = true;
        } else if (argument == "--plane-id" && i + 1 < argc) {
            options.plane_id = std::stoi(argv[++i]);
            options.udp_video = true;
        } else if (argument == "--connector-id" && i + 1 < argc) {
            options.connector_id = std::stoi(argv[++i]);
            options.udp_video = true;
        }
    }
    return options;
}

void poll_ipc(glide::ipc::Client& ipc, std::chrono::steady_clock::time_point& next_heartbeat)
{
    if (!ipc.connected()) {
        return;
    }

    for (const auto& line : ipc.poll_lines()) {
        if (line == "pong") {
            glide::log(glide::LogLevel::info, "GlideView", "IPC pong");
        }
    }
    if (std::chrono::steady_clock::now() >= next_heartbeat) {
        ipc.send_line("heartbeat glide-view");
        next_heartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
}

#if OPENHD_GLIDE_HAS_GSTREAMER
bool has_property(GObject* object, const char* name)
{
    return g_object_class_find_property(G_OBJECT_GET_CLASS(object), name) != nullptr;
}

void set_bool_property_if_present(GstElement* element, const char* name, bool value)
{
    auto* object = G_OBJECT(element);
    if (has_property(object, name)) {
        g_object_set(object, name, value ? TRUE : FALSE, nullptr);
    }
}

void set_int_property_if_present(GstElement* element, const char* name, int value)
{
    auto* object = G_OBJECT(element);
    if (has_property(object, name)) {
        g_object_set(object, name, value, nullptr);
    }
}

void set_int64_property_if_present(GstElement* element, const char* name, gint64 value)
{
    auto* object = G_OBJECT(element);
    if (has_property(object, name)) {
        g_object_set(object, name, value, nullptr);
    }
}

GstElement* make_element(const char* factory, const char* name)
{
    auto* element = gst_element_factory_make(factory, name);
    if (element == nullptr) {
        glide::log(glide::LogLevel::error, "GlideView", std::string("missing GStreamer element: ") + factory);
    }
    return element;
}

GstElement* make_decoder()
{
    for (const auto* factory : { "v4l2h264dec", "v4l2slh264dec", "avdec_h264" }) {
        if (auto* decoder = gst_element_factory_make(factory, "decoder"); decoder != nullptr) {
            if (std::string(factory).find("v4l2") != std::string::npos) {
                glide::log(glide::LogLevel::info, "GlideView", std::string("using hardware-oriented decoder ") + factory);
            } else {
                glide::log(glide::LogLevel::warning, "GlideView", std::string("using software decoder fallback ") + factory);
            }
            return decoder;
        }
    }
    glide::log(glide::LogLevel::error, "GlideView", "no H.264 decoder found; install Raspberry Pi/GStreamer codec plugins");
    return nullptr;
}

class VideoPipeline {
public:
    ~VideoPipeline()
    {
        stop();
    }

    bool start(const Options& options)
    {
        gst_init(nullptr, nullptr);

        pipeline_ = gst_pipeline_new("openhd-glide-view");
        auto* source = make_element("udpsrc", "udp-source");
        auto* capsfilter = make_element("capsfilter", "rtp-caps");
        auto* depay = make_element("rtph264depay", "h264-depay");
        auto* parse = make_element("h264parse", "h264-parse");
        auto* input_queue = make_element("queue", "input-queue");
        auto* decoder = make_decoder();
        auto* output_queue = make_element("queue", "output-queue");
        auto* sink = make_element("kmssink", "kms-sink");

        if (pipeline_ == nullptr || source == nullptr || capsfilter == nullptr || depay == nullptr || parse == nullptr
            || input_queue == nullptr || decoder == nullptr || output_queue == nullptr || sink == nullptr) {
            stop();
            return false;
        }

        g_object_set(source, "port", static_cast<int>(options.udp_port), nullptr);
        set_int_property_if_present(source, "buffer-size", 65536);

        auto* caps = gst_caps_new_simple(
            "application/x-rtp",
            "media",
            G_TYPE_STRING,
            "video",
            "clock-rate",
            G_TYPE_INT,
            90000,
            "encoding-name",
            G_TYPE_STRING,
            "H264",
            "payload",
            G_TYPE_INT,
            96,
            nullptr);
        g_object_set(capsfilter, "caps", caps, nullptr);
        gst_caps_unref(caps);

        for (auto* queue : { input_queue, output_queue }) {
            set_int_property_if_present(queue, "max-size-buffers", 1);
            set_int_property_if_present(queue, "max-size-bytes", 0);
            set_int_property_if_present(queue, "max-size-time", 0);
            set_int_property_if_present(queue, "leaky", 2);
        }

        set_bool_property_if_present(sink, "sync", false);
        set_bool_property_if_present(sink, "async", false);
        set_bool_property_if_present(sink, "qos", false);
        set_int64_property_if_present(sink, "max-lateness", -1);
        if (options.plane_id) {
            set_int_property_if_present(sink, "plane-id", *options.plane_id);
        }
        if (options.connector_id) {
            set_int_property_if_present(sink, "connector-id", *options.connector_id);
        }

        gst_bin_add_many(
            GST_BIN(pipeline_),
            source,
            capsfilter,
            depay,
            parse,
            input_queue,
            decoder,
            output_queue,
            sink,
            nullptr);

        if (!gst_element_link_many(source, capsfilter, depay, parse, input_queue, decoder, output_queue, sink, nullptr)) {
            glide::log(glide::LogLevel::error, "GlideView", "failed to link GStreamer UDP video pipeline");
            stop();
            return false;
        }

        bus_ = gst_element_get_bus(pipeline_);
        const auto result = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        if (result == GST_STATE_CHANGE_FAILURE) {
            glide::log(glide::LogLevel::error, "GlideView", "failed to start GStreamer UDP video pipeline");
            stop();
            return false;
        }

        std::ostringstream status;
        status << "UDP RTP/H264 video listening on 0.0.0.0:" << options.udp_port;
        if (options.plane_id) {
            status << " plane-id=" << *options.plane_id;
        }
        if (options.connector_id) {
            status << " connector-id=" << *options.connector_id;
        }
        glide::log(glide::LogLevel::info, "GlideView", status.str());
        return true;
    }

    bool poll()
    {
        if (bus_ == nullptr) {
            return true;
        }

        while (auto* message = gst_bus_pop_filtered(
                   bus_,
                   static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED))) {
            switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_ERROR: {
                GError* error {};
                gchar* debug {};
                gst_message_parse_error(message, &error, &debug);
                glide::log(glide::LogLevel::error, "GlideView", error != nullptr ? error->message : "GStreamer error");
                if (debug != nullptr) {
                    glide::log(glide::LogLevel::warning, "GlideView", debug);
                }
                if (error != nullptr) {
                    g_error_free(error);
                }
                if (debug != nullptr) {
                    g_free(debug);
                }
                gst_message_unref(message);
                return false;
            }
            case GST_MESSAGE_EOS:
                glide::log(glide::LogLevel::warning, "GlideView", "GStreamer video pipeline reached EOS");
                gst_message_unref(message);
                return false;
            case GST_MESSAGE_STATE_CHANGED:
                if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline_)) {
                    GstState old_state {};
                    GstState new_state {};
                    GstState pending {};
                    gst_message_parse_state_changed(message, &old_state, &new_state, &pending);
                    glide::log(
                        glide::LogLevel::info,
                        "GlideView",
                        std::string("pipeline state ") + gst_element_state_get_name(old_state) + " -> "
                            + gst_element_state_get_name(new_state));
                }
                break;
            default:
                break;
            }
            gst_message_unref(message);
        }
        return true;
    }

private:
    void stop()
    {
        if (pipeline_ != nullptr) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
        }
        if (bus_ != nullptr) {
            gst_object_unref(bus_);
            bus_ = nullptr;
        }
        if (pipeline_ != nullptr) {
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }
    }

    GstElement* pipeline_ {};
    GstBus* bus_ {};
};
#endif

} // namespace

int main(int argc, char** argv)
{
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    const auto options = parse_options(argc, argv);
    glide::log(glide::LogLevel::info, "GlideView", "video renderer started");

    glide::ipc::Client ipc;
    if (ipc.connect_to(options.ipc_socket)) {
        ipc.send_line("hello glide-view");
        ipc.send_line("status glide-view ready");
    } else {
        glide::log(glide::LogLevel::warning, "GlideView", "IPC controller unavailable");
    }

    auto next_heartbeat = std::chrono::steady_clock::now();

    if (options.udp_video) {
#if OPENHD_GLIDE_HAS_GSTREAMER
        VideoPipeline pipeline;
        if (!pipeline.start(options)) {
            return 1;
        }
        if (ipc.connected()) {
            ipc.send_line("status glide-view udp-video-ready");
        }
        while (stop_requested == 0) {
            if (!pipeline.poll()) {
                return 1;
            }
            poll_ipc(ipc, next_heartbeat);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return 0;
#else
        glide::log(glide::LogLevel::error, "GlideView", "GStreamer support was not found at build time");
        return 1;
#endif
    }

    if (options.stay_alive) {
        while (stop_requested == 0) {
            poll_ipc(ipc, next_heartbeat);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    return 0;
}
