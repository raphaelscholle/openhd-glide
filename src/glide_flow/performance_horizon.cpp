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

float normalize_degrees(float degrees)
{
    return std::fmod(std::fmod(degrees, 360.0F) + 360.0F, 360.0F);
}

RenderPoint point_from_screen_degrees(RenderPoint origin, float degrees, float distance)
{
    const auto radians = degrees * pi / 180.0F;
    return RenderPoint {
        .x = origin.x + std::sin(radians) * distance,
        .y = origin.y - std::cos(radians) * distance,
    };
}

void draw_arc(
    GlesTextRenderer& renderer,
    RenderPoint center,
    float radius,
    float start_degrees,
    float end_degrees,
    float thickness,
    RgbaColor color,
    SurfaceSize surface)
{
    constexpr int segments = 12;
    auto previous = point_from_screen_degrees(center, start_degrees, radius);
    for (int i = 1; i <= segments; ++i) {
        const auto t = static_cast<float>(i) / static_cast<float>(segments);
        const auto degrees = start_degrees + (end_degrees - start_degrees) * t;
        const auto current = point_from_screen_degrees(center, degrees, radius);
        renderer.draw_line(previous, current, thickness, color, surface);
        previous = current;
    }
}

void draw_wind_indicator(GlesTextRenderer& renderer, RenderPoint horizon_center, WindSample wind, float scale, SurfaceSize surface)
{
    const RgbaColor glow { .red = 0.02F, .green = 0.95F, .blue = 0.45F, .alpha = 0.34F };
    const RgbaColor line { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.92F };
    const auto speed_kmh = std::max(0.0F, wind.speed_mps * 3.6F);
    const auto wind_scale = 0.80F + std::min(speed_kmh, 45.0F) / 45.0F * 0.75F;
    const auto blowing_to_degrees = normalize_degrees(wind.direction_degrees + 180.0F);
    const auto marker_center = point_from_screen_degrees(horizon_center, blowing_to_degrees, (78.0F + speed_kmh * 0.35F) * scale);
    const auto arc_center = point_from_screen_degrees(marker_center, blowing_to_degrees + 180.0F, 18.0F * wind_scale * scale);
    const auto start = blowing_to_degrees - 34.0F;
    const auto end = blowing_to_degrees + 34.0F;

    for (int i = 0; i < 3; ++i) {
        const auto radius = (16.0F + static_cast<float>(i) * 8.0F) * wind_scale * scale;
        draw_arc(renderer, arc_center, radius, start, end, 4.2F * wind_scale * scale, glow, surface);
        draw_arc(renderer, arc_center, radius, start, end, 1.7F * wind_scale * scale, line, surface);
    }
}

} // namespace

void PerformanceHorizon::draw(GlesTextRenderer& renderer, SurfaceSize surface, AttitudeSample attitude, WindSample wind) const
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

    if (wind.valid) {
        draw_wind_indicator(renderer, center, wind, scale, surface);
    }
}

} // namespace glide::flow
