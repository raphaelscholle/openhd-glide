#include "common/ipc.hpp"
#include "common/logging.hpp"

#include <chrono>
#include <string>
#include <thread>

namespace {

struct Options {
    std::string ipc_socket { glide::ipc::default_socket_path };
    bool stay_alive {};
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
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    glide::log(glide::LogLevel::info, "GlideView", "video renderer placeholder started; UDP video ingest will be added here");

    glide::ipc::Client ipc;
    if (ipc.connect_to(options.ipc_socket)) {
        ipc.send_line("hello glide-view");
        ipc.send_line("status glide-view ready");
    } else {
        glide::log(glide::LogLevel::warning, "GlideView", "IPC controller unavailable");
    }

    if (options.stay_alive) {
        auto next_heartbeat = std::chrono::steady_clock::now();
        while (true) {
            if (ipc.connected()) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    return 0;
}
