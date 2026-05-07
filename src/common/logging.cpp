#include "common/logging.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace glide {
namespace {

std::mutex log_mutex;

std::string_view level_name(LogLevel level)
{
    switch (level) {
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    }

    return "UNKNOWN";
}

std::tm to_local_time(std::time_t time)
{
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    return local;
}

} // namespace

void log(LogLevel level, std::string_view component, std::string_view message)
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto local_time = to_local_time(time);

    std::lock_guard lock(log_mutex);
    std::cerr << std::put_time(&local_time, "%F %T")
              << " [" << level_name(level) << "]"
              << " [" << component << "] "
              << message << '\n';
}

} // namespace glide
