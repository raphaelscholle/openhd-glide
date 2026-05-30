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

#include "common/ipc.hpp"
#include "common/logging.hpp"

#include <filesystem>
#include <fstream>
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
        if (line.rfind("ui key ", 0) == 0) {
            const auto path = std::filesystem::temp_directory_path() / "openhd-glide-ui.key";
            std::ofstream file(path, std::ios::app);
            if (file) {
                file << line << '\n';
                return 0;
            }
        }
        glide::log(glide::LogLevel::error, "GlideSend", "failed to connect to IPC socket " + socket_path);
        return 1;
    }
    if (!ipc.send_line(line)) {
        glide::log(glide::LogLevel::error, "GlideSend", "failed to send IPC line");
        return 1;
    }
    return 0;
}
