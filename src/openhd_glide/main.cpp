#include "common/logging.hpp"
#include "common/ipc.hpp"
#include "common/preview_control.hpp"
#include "platform/core_assignment.hpp"
#include "platform/cpu_topology.hpp"
#include "platform/drm_probe.hpp"
#include "platform/process_probe.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
    stop_requested = 1;
}

struct Options {
    bool preview_stack {};
    std::uint32_t preview_width { 1280 };
    std::uint32_t flow_height { 720 };
    std::uint32_t ui_width { 760 };
    std::uint32_t ui_height { 720 };
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
        } else if (argument == "--preview-width" && i + 1 < argc) {
            options.preview_width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--ui-height" && i + 1 < argc) {
            options.ui_height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--ui-width" && i + 1 < argc) {
            options.ui_width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--flow-height" && i + 1 < argc) {
            options.flow_height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
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
    return {
        executable,
        "--stay-alive",
        "--ipc-socket",
        options.ipc_socket,
    };
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
#else
int run_preview_stack(char*, const Options&)
{
    glide::log(glide::LogLevel::error, "OpenHD-Glide", "--preview-stack currently requires Linux/WSL process launching");
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

    return 0;
}
