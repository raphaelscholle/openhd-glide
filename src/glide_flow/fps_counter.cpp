#include "glide_flow/fps_counter.hpp"

namespace glide::flow {

std::optional<double> FpsCounter::frame(Clock::time_point now)
{
    ++frames_in_window_;

    const auto elapsed = now - window_start_;
    if (elapsed < std::chrono::seconds(1)) {
        return std::nullopt;
    }

    const auto seconds = std::chrono::duration<double>(elapsed).count();
    latest_fps_ = static_cast<double>(frames_in_window_) / seconds;
    frames_in_window_ = 0;
    window_start_ = now;

    return latest_fps_;
}

double FpsCounter::latest_fps() const
{
    return latest_fps_;
}

} // namespace glide::flow

