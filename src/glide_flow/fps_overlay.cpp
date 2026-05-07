#include "glide_flow/fps_overlay.hpp"

#include "glide_flow/vector_font.hpp"

#include <algorithm>
#include <sstream>

namespace glide::flow {

TextPlacement FpsOverlay::layout(double fps, SurfaceSize surface) const
{
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << fps << " FPS";

    const auto value = text.str();
    const auto surface_scale = std::max(0.70F, std::min(
        static_cast<float>(surface.width) / 1280.0F,
        static_cast<float>(surface.height) / 720.0F));
    const auto scale = scale_ * surface_scale;
    const auto text_height = vector_text_height() * scale;
    const auto surface_height = static_cast<float>(surface.height);
    const auto margin = margin_ * surface_scale;

    return TextPlacement {
        .text = value,
        .x = margin,
        .y = std::max(text_height, surface_height - margin),
        .scale = scale,
    };
}

} // namespace glide::flow
