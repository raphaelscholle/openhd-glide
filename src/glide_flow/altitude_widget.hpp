#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"

#include <chrono>

namespace glide::flow {

struct AltitudeSample {
    float altitude_m {};
};

class SimulatedAltitude {
public:
    AltitudeSample sample(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::chrono::steady_clock::time_point start_ { std::chrono::steady_clock::now() };
};

class AltitudeWidgetRenderer {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, AltitudeSample sample) const;

private:
    static constexpr float ladder_width_ = 50.0F;
    static constexpr float ladder_height_ = 300.0F;
    static constexpr float pointer_width_ = 74.0F;
    static constexpr float pointer_height_ = 28.0F;
    static constexpr float altitude_range_m_ = 100.0F;
};

} // namespace glide::flow

