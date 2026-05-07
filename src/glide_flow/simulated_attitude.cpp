#include "glide_flow/simulated_attitude.hpp"

#include <cmath>

namespace glide::flow {

AttitudeSample SimulatedAttitude::sample(std::chrono::steady_clock::time_point now) const
{
    constexpr float pi = 3.14159265358979323846F;
    const auto seconds = std::chrono::duration<float>(now - start_).count();

    return AttitudeSample {
        .roll_degrees = std::sin(seconds * 0.75F) * 28.0F,
        .pitch_degrees = std::sin((seconds * 0.52F) + (pi * 0.35F)) * 8.0F,
    };
}

} // namespace glide::flow

