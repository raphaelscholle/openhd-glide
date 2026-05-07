#pragma once

#include "glide_flow/fps_overlay.hpp"
#include "glide_flow/gles_text_renderer.hpp"

namespace glide::flow {

struct AttitudeSample {
    float roll_degrees {};
    float pitch_degrees {};
};

class PerformanceHorizon {
public:
    void draw(GlesTextRenderer& renderer, SurfaceSize surface, AttitudeSample attitude) const;

private:
    static constexpr float widget_width_ = 250.0F;
    static constexpr float widget_height_ = 48.0F;
    static constexpr float horizon_thickness_ = 4.0F;
    static constexpr float horizon_gap_ = 20.0F;
    static constexpr float pitch_pixels_per_degree_ = 4.0F;
};

} // namespace glide::flow

