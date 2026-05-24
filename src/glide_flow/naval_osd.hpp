#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"

#include <array>
#include <chrono>

namespace glide::flow {

struct NavalRadarContact {
    float bearing_degrees {};
    float range_normalized {};
    bool ship {};
    bool selected {};
};

struct NavalOsdSample {
    float heading_degrees {};
    std::array<NavalRadarContact, 7> contacts {};
};

class SimulatedNavalOsd {
public:
    NavalOsdSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class NavalOsdRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, const NavalOsdSample& sample) const;
};

} // namespace glide::flow
