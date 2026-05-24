#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"

#include <chrono>

namespace glide::flow {

struct RoverOsdSample {
    float speed_kmh {};
    float heading_degrees {};
};

class SimulatedRoverOsd {
public:
    RoverOsdSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class RoverOsdRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const RoverOsdSample& sample) const;
};

} // namespace glide::flow
