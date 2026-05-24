#include "glide_flow/rover_osd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>

namespace glide::flow {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr RgbaColor blue { .red = 0.32F, .green = 0.66F, .blue = 1.0F, .alpha = 0.92F };
constexpr RgbaColor blue_dim { .red = 0.32F, .green = 0.66F, .blue = 1.0F, .alpha = 0.35F };
constexpr RgbaColor blue_faint { .red = 0.32F, .green = 0.66F, .blue = 1.0F, .alpha = 0.16F };

float layout_scale(SurfaceSize surface)
{
    return std::max(0.70F, std::min(
        static_cast<float>(surface.width) / 1280.0F,
        static_cast<float>(surface.height) / 720.0F));
}

float sx(float value, float scale)
{
    return value * scale;
}

float normalize_degrees(float degrees)
{
    return std::fmod(std::fmod(degrees, 360.0F) + 360.0F, 360.0F);
}

void draw_text(GlesTextRenderer& renderer, std::string text, float x, float baseline, float scale, SurfaceSize surface)
{
    renderer.draw(TextPlacement {
        .text = std::move(text),
        .x = x,
        .y = baseline,
        .scale = scale,
    }, surface);
}

RenderPoint compass_point(RenderPoint center, float radius, float arc_degrees)
{
    const auto radians = arc_degrees * pi / 180.0F;
    return RenderPoint {
        .x = center.x + std::sin(radians) * radius,
        .y = center.y - std::cos(radians) * radius,
    };
}

void draw_compass_tick(
    GlesTextRenderer& renderer,
    RenderPoint center,
    float radius,
    float arc_degrees,
    float length,
    float thickness,
    RgbaColor color,
    SurfaceSize surface)
{
    const auto outer = compass_point(center, radius, arc_degrees);
    const auto inner = compass_point(center, radius - length, arc_degrees);
    renderer.draw_line(inner, outer, thickness, color, surface);
}

std::string heading_suffix(float heading)
{
    const auto value = normalize_degrees(heading);
    if (value >= 337.5F || value < 22.5F) {
        return "N";
    }
    if (value < 67.5F) {
        return "NE";
    }
    if (value < 112.5F) {
        return "E";
    }
    if (value < 157.5F) {
        return "SE";
    }
    if (value < 202.5F) {
        return "S";
    }
    if (value < 247.5F) {
        return "SW";
    }
    if (value < 292.5F) {
        return "W";
    }
    return "NW";
}

void draw_cardinal_label(GlesTextRenderer& renderer, std::string label, RenderPoint center, float radius, float arc_degrees, float scale, SurfaceSize surface)
{
    const auto point = compass_point(center, radius, arc_degrees);
    const auto width = renderer.measure_text_width(label, sx(11.0F, scale));
    draw_text(renderer, std::move(label), point.x - width * 0.5F, point.y + sx(5.0F, scale), sx(11.0F, scale), surface);
}

} // namespace

RoverOsdSample SimulatedRoverOsd::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    return RoverOsdSample {
        .speed_kmh = 95.0F + std::sin(seconds * 0.55F) * 8.0F,
        .heading_degrees = normalize_degrees(128.0F + std::sin(seconds * 0.20F) * 16.0F),
    };
}

void RoverOsdRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, const RoverOsdSample& sample) const
{
    const auto scale = layout_scale(surface);
    const RenderPoint center {
        .x = static_cast<float>(surface.width) * 0.5F,
        .y = sx(190.0F, scale),
    };
    const auto radius = sx(165.0F, scale);
    constexpr float visible_degrees = 110.0F;
    constexpr int segments = 28;

    auto previous = compass_point(center, radius, -visible_degrees * 0.5F);
    for (int i = 1; i <= segments; ++i) {
        const auto t = static_cast<float>(i) / static_cast<float>(segments);
        const auto angle = -visible_degrees * 0.5F + visible_degrees * t;
        const auto current = compass_point(center, radius, angle);
        renderer.draw_line(previous, current, sx(2.0F, scale), blue_dim, surface);
        previous = current;
    }

    for (int tick = -10; tick <= 10; ++tick) {
        const auto arc_angle = static_cast<float>(tick) * (visible_degrees / 20.0F);
        const auto compass_heading = normalize_degrees(sample.heading_degrees + arc_angle);
        const auto cardinal = std::fmod(compass_heading, 90.0F);
        const auto major = cardinal < 4.0F || cardinal > 86.0F || tick == 0;
        draw_compass_tick(
            renderer,
            center,
            radius,
            arc_angle,
            sx(major ? 18.0F : 10.0F, scale),
            sx(major ? 2.0F : 1.2F, scale),
            major ? blue : blue_faint,
            surface);
    }

    for (const auto heading : std::array<float, 4> { 0.0F, 90.0F, 180.0F, 270.0F }) {
        auto delta = normalize_degrees(heading - sample.heading_degrees);
        if (delta > 180.0F) {
            delta -= 360.0F;
        }
        if (std::abs(delta) <= visible_degrees * 0.5F) {
            const char* label = heading == 0.0F ? "N" : (heading == 90.0F ? "E" : (heading == 180.0F ? "S" : "W"));
            draw_cardinal_label(renderer, label, center, radius - sx(31.0F, scale), delta, scale, surface);
        }
    }

    renderer.draw_line(
        { center.x - sx(8.0F, scale), center.y - radius - sx(12.0F, scale) },
        { center.x, center.y - radius + sx(10.0F, scale) },
        sx(2.4F, scale),
        blue,
        surface);
    renderer.draw_line(
        { center.x + sx(8.0F, scale), center.y - radius - sx(12.0F, scale) },
        { center.x, center.y - radius + sx(10.0F, scale) },
        sx(2.4F, scale),
        blue,
        surface);

    const auto speed = std::to_string(static_cast<int>(std::round(sample.speed_kmh)));
    const auto speed_scale = sx(42.0F, scale);
    const auto speed_width = renderer.measure_text_width(speed, speed_scale);
    draw_text(renderer, speed, center.x - speed_width * 0.5F, center.y - sx(56.0F, scale), speed_scale, surface);
    const auto unit = std::string("km/h");
    const auto unit_scale = sx(15.0F, scale);
    const auto unit_width = renderer.measure_text_width(unit, unit_scale);
    draw_text(renderer, unit, center.x - unit_width * 0.5F, center.y - sx(26.0F, scale), unit_scale, surface);

    std::ostringstream heading;
    heading << static_cast<int>(std::round(normalize_degrees(sample.heading_degrees))) << "deg " << heading_suffix(sample.heading_degrees);
    const auto heading_text = heading.str();
    const auto heading_scale = sx(16.0F, scale);
    const auto heading_width = renderer.measure_text_width(heading_text, heading_scale);
    draw_text(renderer, heading_text, center.x - heading_width * 0.5F, center.y + sx(30.0F, scale), heading_scale, surface);
}

} // namespace glide::flow
