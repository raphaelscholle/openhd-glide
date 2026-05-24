#include "glide_flow/rocket_osd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace glide::flow {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr RgbaColor cyan_line { .red = 0.44F, .green = 0.86F, .blue = 0.84F, .alpha = 0.34F };
constexpr RgbaColor cyan_dim { .red = 0.44F, .green = 0.86F, .blue = 0.84F, .alpha = 0.17F };
constexpr RgbaColor panel_bg { .red = 0.025F, .green = 0.035F, .blue = 0.050F, .alpha = 0.78F };
constexpr RgbaColor panel_edge { .red = 0.55F, .green = 0.72F, .blue = 0.82F, .alpha = 0.22F };
constexpr RgbaColor white_line { .red = 0.86F, .green = 0.92F, .blue = 0.95F, .alpha = 0.80F };
constexpr RgbaColor purple { .red = 0.78F, .green = 0.48F, .blue = 1.0F, .alpha = 0.92F };
constexpr RgbaColor purple_dim { .red = 0.78F, .green = 0.48F, .blue = 1.0F, .alpha = 0.34F };

float layout_scale(SurfaceSize surface)
{
    return std::max(0.74F, std::min(
        static_cast<float>(surface.width) / 1280.0F,
        static_cast<float>(surface.height) / 720.0F));
}

float sx(float value, float scale)
{
    return value * scale;
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

std::string fixed_1(float value)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << value;
    return stream.str();
}

std::string mission_time_text(std::chrono::seconds duration)
{
    const auto total = std::max<std::chrono::seconds::rep>(0, duration.count());
    const auto hours = total / 3600;
    const auto minutes = (total / 60) % 60;
    const auto seconds = total % 60;
    std::ostringstream stream;
    stream << "T+ " << std::setfill('0') << std::setw(2) << hours << ':'
           << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return stream.str();
}

RenderPoint polar(RenderPoint center, float degrees, float radius)
{
    const auto radians = degrees * pi / 180.0F;
    return RenderPoint {
        .x = center.x + std::cos(radians) * radius,
        .y = center.y + std::sin(radians) * radius,
    };
}

void draw_dashed_circle(GlesTextRenderer& renderer, RenderPoint center, float radius, float thickness, RgbaColor color, SurfaceSize surface)
{
    constexpr int dashes = 40;
    for (int i = 0; i < dashes; i += 2) {
        auto previous = polar(center, static_cast<float>(i) * 360.0F / static_cast<float>(dashes), radius);
        for (int step = 1; step <= 3; ++step) {
            const auto degrees = (static_cast<float>(i) + static_cast<float>(step) * 0.30F) * 360.0F / static_cast<float>(dashes);
            const auto current = polar(center, degrees, radius);
            renderer.draw_line(previous, current, thickness, color, surface);
            previous = current;
        }
    }
}

void draw_panel(GlesTextRenderer& renderer, float x, float y, float width, float height, SurfaceSize surface)
{
    renderer.draw_filled_quad({ x, y }, { x + width, y }, { x, y + height }, { x + width, y + height }, panel_bg, surface);
    renderer.draw_line({ x, y }, { x + width, y }, 1.2F, panel_edge, surface);
    renderer.draw_line({ x + width, y }, { x + width, y + height }, 1.2F, panel_edge, surface);
    renderer.draw_line({ x, y + height }, { x + width, y + height }, 1.2F, panel_edge, surface);
    renderer.draw_line({ x, y }, { x, y + height }, 1.2F, panel_edge, surface);
}

void draw_left_metric(GlesTextRenderer& renderer, const char* label, std::string value, const char* unit, float y, float scale, SurfaceSize surface)
{
    const auto x = sx(42.0F, scale);
    renderer.draw_line({ sx(14.0F, scale), y + sx(64.0F, scale) }, { sx(152.0F, scale), y + sx(64.0F, scale) }, sx(1.0F, scale), cyan_dim, surface);
    draw_text(renderer, label, x, y + sx(18.0F, scale), sx(12.0F, scale), surface);
    draw_text(renderer, std::move(value), x + sx(8.0F, scale), y + sx(48.0F, scale), sx(25.0F, scale), surface);
    draw_text(renderer, unit, x + sx(19.0F, scale), y + sx(68.0F, scale), sx(11.0F, scale), surface);
}

void draw_right_status(GlesTextRenderer& renderer, const RocketOsdSample& sample, float scale, SurfaceSize surface)
{
    const auto width = sx(184.0F, scale);
    const auto height = sx(286.0F, scale);
    const auto x = static_cast<float>(surface.width) - width - sx(38.0F, scale);
    const auto y = sx(54.0F, scale);
    draw_panel(renderer, x, y, width, height, surface);

    const std::array labels { "T+", "STAGE", "FUEL", "STATUS" };
    const std::array values {
        mission_time_text(sample.mission_time).substr(3),
        std::to_string(sample.stage),
        std::to_string(static_cast<int>(std::round(sample.fuel_percent))) + " %",
        std::string(sample.status != nullptr ? sample.status : "NOMINAL"),
    };
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto row_y = y + sx(18.0F + static_cast<float>(i) * 68.0F, scale);
        if (i > 0) {
            renderer.draw_line({ x, row_y - sx(14.0F, scale) }, { x + width, row_y - sx(14.0F, scale) }, sx(1.0F, scale), panel_edge, surface);
        }
        draw_text(renderer, labels[i], x + sx(24.0F, scale), row_y, sx(11.0F, scale), surface);
        draw_text(renderer, values[i], x + sx(24.0F, scale), row_y + sx(34.0F, scale), sx(i == 3 ? 17.0F : 22.0F, scale), surface);
    }
}

void draw_rocket(GlesTextRenderer& renderer, RenderPoint base, float scale, SurfaceSize surface)
{
    const auto s = scale;
    renderer.draw_line({ base.x, base.y - sx(138.0F, s) }, { base.x - sx(8.0F, s), base.y - sx(112.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x, base.y - sx(138.0F, s) }, { base.x + sx(8.0F, s), base.y - sx(112.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x - sx(8.0F, s), base.y - sx(112.0F, s) }, { base.x - sx(8.0F, s), base.y - sx(52.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x + sx(8.0F, s), base.y - sx(112.0F, s) }, { base.x + sx(8.0F, s), base.y - sx(52.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x - sx(8.0F, s), base.y - sx(52.0F, s) }, { base.x + sx(8.0F, s), base.y - sx(52.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x - sx(8.0F, s), base.y - sx(74.0F, s) }, { base.x - sx(22.0F, s), base.y - sx(40.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x + sx(8.0F, s), base.y - sx(74.0F, s) }, { base.x + sx(22.0F, s), base.y - sx(40.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x - sx(22.0F, s), base.y - sx(40.0F, s) }, { base.x - sx(8.0F, s), base.y - sx(52.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x + sx(22.0F, s), base.y - sx(40.0F, s) }, { base.x + sx(8.0F, s), base.y - sx(52.0F, s) }, sx(2.0F, s), white_line, surface);
    renderer.draw_line({ base.x - sx(3.0F, s), base.y - sx(45.0F, s) }, { base.x - sx(5.0F, s), base.y }, sx(1.2F, s), purple_dim, surface);
    renderer.draw_line({ base.x + sx(3.0F, s), base.y - sx(45.0F, s) }, { base.x + sx(5.0F, s), base.y }, sx(1.2F, s), purple_dim, surface);
}

void draw_trajectory(GlesTextRenderer& renderer, SurfaceSize surface, float scale)
{
    const RenderPoint center {
        .x = static_cast<float>(surface.width) * 0.47F,
        .y = static_cast<float>(surface.height) * 0.42F,
    };
    draw_dashed_circle(renderer, center, sx(250.0F, scale), sx(1.5F, scale), cyan_line, surface);
    draw_dashed_circle(renderer, center, sx(145.0F, scale), sx(1.3F, scale), cyan_dim, surface);

    RenderPoint previous { .x = center.x - sx(12.0F, scale), .y = center.y + sx(160.0F, scale) };
    for (int i = 1; i <= 18; ++i) {
        const auto t = static_cast<float>(i) / 18.0F;
        const RenderPoint current {
            .x = center.x + sx(-12.0F + 86.0F * std::sin(t * 1.7F), scale),
            .y = center.y + sx(160.0F - 430.0F * t, scale),
        };
        renderer.draw_line(previous, current, sx(1.8F, scale), i < 11 ? white_line : purple, surface);
        previous = current;
    }
    for (int i = 0; i < 5; ++i) {
        const auto y = center.y + sx(25.0F - static_cast<float>(i) * 74.0F, scale);
        const auto x = center.x + sx(18.0F + static_cast<float>(i) * 12.0F, scale);
        renderer.draw_filled_quad(
            { x, y - sx(10.0F, scale) },
            { x + sx(9.0F, scale), y },
            { x - sx(9.0F, scale), y },
            { x, y - sx(10.0F, scale) },
            purple,
            surface);
    }
    draw_rocket(renderer, { center.x - sx(12.0F, scale), center.y + sx(178.0F, scale) }, scale, surface);
}

void draw_timeline(GlesTextRenderer& renderer, SurfaceSize surface, float scale)
{
    const auto margin = sx(20.0F, scale);
    const auto y = static_cast<float>(surface.height) - sx(72.0F, scale);
    const auto height = sx(58.0F, scale);
    const auto width = static_cast<float>(surface.width) - margin * 2.0F;
    draw_panel(renderer, margin, y, width, height, surface);
    const std::array labels { "LIFTOFF", "MAX-Q", "STAGE SEP", "MECO", "SECO" };
    const std::array times { "T+ 00:00:00", "T+ 00:01:05", "T+ 00:02:37", "T+ 00:06:12", "T+ 00:08:47" };
    const auto slot = width / static_cast<float>(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto x = margin + slot * static_cast<float>(i);
        if (i > 0) {
            renderer.draw_line({ x, y }, { x, y + height }, sx(1.0F, scale), panel_edge, surface);
        }
        const auto active = i == 2;
        draw_text(renderer, labels[i], x + sx(28.0F, scale), y + sx(26.0F, scale), sx(11.0F, scale), surface);
        draw_text(renderer, times[i], x + sx(22.0F, scale), y + sx(50.0F, scale), sx(14.0F, scale), surface);
        if (active) {
            renderer.draw_line({ x + sx(22.0F, scale), y + sx(1.0F, scale) }, { x + slot - sx(22.0F, scale), y + sx(1.0F, scale) }, sx(2.0F, scale), purple, surface);
        }
    }
}

} // namespace

RocketOsdSample SimulatedRocketOsd::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    return RocketOsdSample {
        .velocity_mps = 512.0F + std::sin(seconds * 0.45F) * 18.0F,
        .altitude_km = 11.2F + std::sin(seconds * 0.22F) * 0.4F,
        .g_force = 2.1F + std::sin(seconds * 0.62F) * 0.2F,
        .fuel_percent = std::clamp(62.0F - seconds * 0.025F, 0.0F, 100.0F),
        .stage = 2,
        .mission_time = std::chrono::seconds(83 + static_cast<int>(seconds)),
        .status = "NOMINAL",
    };
}

void RocketOsdRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, const RocketOsdSample& sample) const
{
    const auto scale = layout_scale(surface);
    draw_left_metric(renderer, "VELOCITY", std::to_string(static_cast<int>(std::round(sample.velocity_mps))), "m/s", sx(70.0F, scale), scale, surface);
    draw_left_metric(renderer, "ALTITUDE", fixed_1(sample.altitude_km), "km", sx(174.0F, scale), scale, surface);
    draw_left_metric(renderer, "G-FORCE", fixed_1(sample.g_force), "G", sx(278.0F, scale), scale, surface);
    draw_trajectory(renderer, surface, scale);
    draw_right_status(renderer, sample, scale, surface);
    draw_timeline(renderer, surface, scale);
}

} // namespace glide::flow
