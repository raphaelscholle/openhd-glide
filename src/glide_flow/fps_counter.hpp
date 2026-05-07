#pragma once

#include <chrono>
#include <optional>

namespace glide::flow {

class FpsCounter {
public:
    using Clock = std::chrono::steady_clock;

    std::optional<double> frame(Clock::time_point now = Clock::now());
    double latest_fps() const;

private:
    Clock::time_point window_start_ { Clock::now() };
    unsigned int frames_in_window_ {};
    double latest_fps_ {};
};

} // namespace glide::flow

