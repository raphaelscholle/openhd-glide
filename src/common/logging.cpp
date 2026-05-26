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
