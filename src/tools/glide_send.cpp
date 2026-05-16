#include "common/ipc.hpp"
#include "common/logging.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    std::string socket_path { glide::ipc::default_socket_path };
    std::string line;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--ipc-socket" && i + 1 < argc) {
            socket_path = argv[++i];
            continue;
        }
        if (!line.empty()) {
            line += ' ';
        }
        line += argument;
    }

    if (line.empty()) {
        std::cerr << "usage: glide-send [--ipc-socket /tmp/openhd-glide.sock] <line...>\n";
        std::cerr << "examples:\n";
        std::cerr << "  glide-send ui key enter\n";
        std::cerr << "  glide-send mav alive air 1\n";
        std::cerr << "  glide-send mav link 5745 20 2 1200\n";
        return 2;
    }

    glide::ipc::Client ipc;
    if (!ipc.connect_to(socket_path)) {
        glide::log(glide::LogLevel::error, "GlideSend", "failed to connect to IPC socket " + socket_path);
        return 1;
    }
    if (!ipc.send_line(line)) {
        glide::log(glide::LogLevel::error, "GlideSend", "failed to send IPC line");
        return 1;
    }
    return 0;
}
