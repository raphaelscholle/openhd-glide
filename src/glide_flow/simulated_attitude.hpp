#pragma once

#include "glide_flow/performance_horizon.hpp"

#include <chrono>

namespace glide::flow {

class SimulatedAttitude {
public:
    AttitudeSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

} // namespace glide::flow

