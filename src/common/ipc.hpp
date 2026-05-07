#pragma once

#include <string>
#include <vector>

namespace glide::ipc {

constexpr const char* default_socket_path = "/tmp/openhd-glide.sock";

class Client {
public:
    Client() = default;
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool connect_to(std::string path = default_socket_path);
    bool connected() const;
    bool send_line(const std::string& line);
    std::vector<std::string> poll_lines();
    void close();

private:
    int fd_ { -1 };
    std::string read_buffer_;
};

struct ServerEvent {
    int client_id {};
    std::string line;
};

class Server {
public:
    Server() = default;
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool listen_on(std::string path = default_socket_path);
    std::vector<ServerEvent> poll();
    bool send_line(int client_id, const std::string& line);
    void broadcast_line(const std::string& line);
    void close();
    const std::string& last_error() const;

private:
    struct ClientState {
        int id {};
        int fd {};
        std::string read_buffer;
    };

    int listen_fd_ { -1 };
    int next_client_id_ { 1 };
    std::string path_;
    std::string last_error_;
    std::vector<ClientState> clients_;
};

} // namespace glide::ipc
