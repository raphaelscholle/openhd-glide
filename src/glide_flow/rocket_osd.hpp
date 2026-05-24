#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"

#include <chrono>

namespace glide::flow {

struct RocketOsdSample {
    float velocity_mps {};
    float altitude_km {};
    float g_force {};
    float fuel_percent {};
    int stage {};
    std::chrono::seconds mission_time {};
    const char* status {};
};

class SimulatedRocketOsd {
public:
    RocketOsdSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class RocketOsdRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const RocketOsdSample& sample) const;
};

} // namespace glide::flow
