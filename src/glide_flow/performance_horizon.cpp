#include "glide_flow/performance_horizon.hpp"

#include <cmath>

namespace glide::flow {
namespace {

constexpr float pi = 3.14159265358979323846F;

RenderPoint rotate_around(RenderPoint point, RenderPoint origin, float degrees)
{
    const auto radians = degrees * pi / 180.0F;
    const auto s = std::sin(radians);
    const auto c = std::cos(radians);
    const auto x = point.x - origin.x;
    const auto y = point.y - origin.y;

    return RenderPoint {
        .x = origin.x + (x * c) - (y * s),
        .y = origin.y + (x * s) + (y * c),
    };
}

void draw_horizon_segment(
    GlesTextRenderer& renderer,
    RenderPoint center,
    float local_start_x,
    float local_end_x,
    float local_y,
    float roll_degrees,
    float thickness,
    SurfaceSize surface)
{
    const RgbaColor glow { .red = 0.02F, .green = 0.95F, .blue = 0.45F, .alpha = 0.42F };
    const RgbaColor line { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.96F };
    const auto rotation = -roll_degrees;

    const RenderPoint start {
        .x = center.x + local_start_x,
        .y = center.y + local_y,
    };
    const RenderPoint end {
        .x = center.x + local_end_x,
        .y = center.y + local_y,
    };

    const auto rotated_start = rotate_around(start, center, rotation);
    const auto rotated_end = rotate_around(end, center, rotation);
    renderer.draw_line(rotated_start, rotated_end, thickness + 3.0F, glow, surface);
    renderer.draw_line(rotated_start, rotated_end, thickness, line, surface);
}

} // namespace

void PerformanceHorizon::draw(GlesTextRenderer& renderer, SurfaceSize surface, AttitudeSample attitude) const
{
    const auto scale = std::max(0.70F, std::min(
        static_cast<float>(surface.width) / 1280.0F,
        static_cast<float>(surface.height) / 720.0F));
    const RenderPoint center {
        .x = static_cast<float>(surface.width) * 0.5F,
        .y = static_cast<float>(surface.height) * 0.5F,
    };

    const auto half_width = widget_width_ * 0.5F * scale;
    const auto pitch_y = attitude.pitch_degrees * pitch_pixels_per_degree_ * scale;
    const auto gap = horizon_gap_ * scale;
    const auto thickness = horizon_thickness_ * scale;

    draw_horizon_segment(
        renderer,
        center,
        -half_width,
        -gap,
        pitch_y,
        attitude.roll_degrees,
        thickness,
        surface);
    draw_horizon_segment(
        renderer,
        center,
        gap,
        half_width,
        pitch_y,
        attitude.roll_degrees,
        thickness,
        surface);

    const RgbaColor center_glow { .red = 0.02F, .green = 0.95F, .blue = 0.45F, .alpha = 0.38F };
    const RgbaColor center_line { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.96F };
    renderer.draw_circle_outline(center, 10.0F * scale, 5.0F * scale, center_glow, surface);
    renderer.draw_circle_outline(center, 10.0F * scale, 2.0F * scale, center_line, surface);
    renderer.draw_line(
        RenderPoint { .x = center.x - (15.0F * scale), .y = center.y },
        RenderPoint { .x = center.x + (15.0F * scale), .y = center.y },
        5.0F * scale,
        center_glow,
        surface);
    renderer.draw_line(
        RenderPoint { .x = center.x - (15.0F * scale), .y = center.y },
        RenderPoint { .x = center.x + (15.0F * scale), .y = center.y },
        2.0F * scale,
        center_line,
        surface);
}

} // namespace glide::flow
