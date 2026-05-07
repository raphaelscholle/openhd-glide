#pragma once

#include <string_view>

namespace glide {

enum class LogLevel {
    info,
    warning,
    error,
};

void log(LogLevel level, std::string_view component, std::string_view message);

} // namespace glide

