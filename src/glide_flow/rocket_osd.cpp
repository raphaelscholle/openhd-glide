#include "glide_flow/rocket_osd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace glide::flow {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr RgbaColor panel_bg { .red = 0.055F, .green = 0.075F, .blue = 0.095F, .alpha = 0.94F };
constexpr RgbaColor panel_line { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.90F };
constexpr RgbaColor panel_dim { .red = 0.44F, .green = 0.86F, .blue = 0.84F, .alpha = 0.22F };
constexpr RgbaColor text_color { .red = 0.92F, .green = 0.96F, .blue = 1.0F, .alpha = 0.96F };
constexpr RgbaColor purple { .red = 0.78F, .green = 0.48F, .blue = 1.0F, .alpha = 0.92F };
constexpr RgbaColor purple_dim { .red = 0.78F, .green = 0.48F, .blue = 1.0F, .alpha = 0.30F };

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

std::string time_text(std::chrono::seconds duration)
{
    const auto total = std::max<std::chrono::seconds::rep>(0, duration.count());
    const auto hours = total / 3600;
    const auto minutes = (total / 60) % 60;
    const auto seconds = total % 60;
    std::ostringstream stream;
    stream << "T+" << std::setfill('0') << std::setw(2) << hours << ':'
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

void draw_bottom_panel(GlesTextRenderer& renderer, SurfaceSize surface)
{
    const auto scale = layout_scale(surface);
    const auto height = sx(40.0F, scale);
    const auto y = static_cast<float>(surface.height) - height;
    const auto width = static_cast<float>(surface.width);
    const auto center_x = width * 0.5F;
    const auto top_y = y + sx(8.0F, scale);
    const auto notch_top_y = y - sx(6.0F, scale);
    const auto notch_width = sx(154.0F, scale);
    const auto notch_slope = sx(14.0F, scale);
    const auto notch_start = center_x - notch_width * 0.5F;
    const auto notch_end = center_x + notch_width * 0.5F;
    const auto notch_left = std::max(sx(6.0F, scale), notch_start - notch_slope);
    const auto notch_right = std::min(width - sx(6.0F, scale), notch_end + notch_slope);
    const auto bottom_y = y + height;

    renderer.draw_filled_quad({ 0.0F, top_y }, { notch_left, top_y }, { 0.0F, bottom_y }, { notch_left, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_right, top_y }, { width, top_y }, { notch_right, bottom_y }, { width, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_left, top_y }, { notch_start, notch_top_y }, { notch_left, bottom_y }, { notch_start, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_start, notch_top_y }, { notch_end, notch_top_y }, { notch_start, bottom_y }, { notch_end, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_end, notch_top_y }, { notch_right, top_y }, { notch_end, bottom_y }, { notch_right, bottom_y }, panel_bg, surface);
}

struct BottomSlot {
    std::string label;
    std::string value;
};

void draw_bottom_slot(GlesTextRenderer& renderer, const BottomSlot& slot, float center_x, float available_width, float baseline, float scale, SurfaceSize surface)
{
    const auto text = slot.label + slot.value;
    auto font_scale = sx(10.5F, scale);
    auto width = static_cast<float>(text.size()) * font_scale * 0.56F;
    if (width > available_width) {
        font_scale = std::max(sx(7.5F, scale), font_scale * (available_width / width));
        width = static_cast<float>(text.size()) * font_scale * 0.56F;
    }
    draw_text(renderer, text, center_x - width * 0.5F, baseline, font_scale, surface);
}

void draw_bottom_bars(GlesTextRenderer& renderer, SurfaceSize surface, const RocketOsdSample& sample)
{
    const auto scale = layout_scale(surface);
    const auto height = sx(40.0F, scale);
    const auto y = static_cast<float>(surface.height) - height;
    const auto center_x = static_cast<float>(surface.width) * 0.5F;
    const auto notch_safe_width = sx(168.0F, scale);
    const auto side_margin = sx(20.0F, scale);
    const auto baseline = y + sx(32.0F, scale);
    const auto left_width = std::max(0.0F, center_x - notch_safe_width * 0.5F - side_margin);
    const auto right_x = center_x + notch_safe_width * 0.5F;
    const auto right_width = std::max(0.0F, static_cast<float>(surface.width) - right_x - side_margin);
    const auto status = sample.status != nullptr ? sample.status : "NOMINAL";
    const auto status_scale = sx(13.5F, scale);
    const auto timer_scale = sx(8.6F, scale);
    const auto status_width = renderer.measure_text_width(status, status_scale);
    const auto timer = time_text(sample.mission_time);
    const auto timer_width = renderer.measure_text_width(timer, timer_scale);
    const std::array left {
        BottomSlot { "F", std::to_string(static_cast<int>(std::round(sample.fuel_percent))) + "%" },
        BottomSlot { "G", fixed_1(sample.g_force) },
        BottomSlot { "S", std::to_string(sample.stage) },
    };
    const std::array right {
        BottomSlot { "V", std::to_string(static_cast<int>(std::round(sample.velocity_mps))) },
        BottomSlot { "A", fixed_1(sample.altitude_km) + "km" },
        BottomSlot { "Q", std::to_string(static_cast<int>(std::round(sample.velocity_mps * sample.g_force * 0.08F))) },
    };

    draw_bottom_panel(renderer, surface);
    draw_text(renderer, status, center_x - status_width * 0.5F, y + sx(21.0F, scale), status_scale, surface);
    draw_text(renderer, timer, center_x - timer_width * 0.5F, y + sx(35.0F, scale), timer_scale, surface);
    const auto left_slot = left_width / static_cast<float>(left.size());
    const auto right_slot = right_width / static_cast<float>(right.size());
    for (std::size_t i = 0; i < left.size(); ++i) {
        draw_bottom_slot(renderer, left[i], side_margin + (static_cast<float>(i) + 0.5F) * left_slot, left_slot - sx(3.0F, scale), baseline, scale, surface);
        draw_bottom_slot(renderer, right[i], right_x + (static_cast<float>(i) + 0.5F) * right_slot, right_slot - sx(3.0F, scale), baseline, scale, surface);
    }
}

void draw_dashed_circle(GlesTextRenderer& renderer, RenderPoint center, float radius, float thickness, RgbaColor color, SurfaceSize surface)
{
    constexpr int dashes = 48;
    for (int i = 0; i < dashes; i += 2) {
        auto previous = polar(center, static_cast<float>(i) * 360.0F / static_cast<float>(dashes), radius);
        for (int step = 1; step <= 4; ++step) {
            const auto degrees = (static_cast<float>(i) + static_cast<float>(step) * 0.25F) * 360.0F / static_cast<float>(dashes);
            const auto current = polar(center, degrees, radius);
            renderer.draw_line(previous, current, thickness, color, surface);
            previous = current;
        }
    }
}

void draw_rocket_body(GlesTextRenderer& renderer, RenderPoint base, float scale, float lean_degrees, SurfaceSize surface)
{
    const auto lean = std::sin(lean_degrees * pi / 180.0F);
    const auto top = RenderPoint { base.x + sx(lean * 26.0F, scale), base.y - sx(142.0F, scale) };
    const auto left_shoulder = RenderPoint { base.x - sx(8.0F, scale), base.y - sx(112.0F, scale) };
    const auto right_shoulder = RenderPoint { base.x + sx(8.0F, scale), base.y - sx(112.0F, scale) };
    const auto left_base = RenderPoint { base.x - sx(8.0F, scale), base.y - sx(52.0F, scale) };
    const auto right_base = RenderPoint { base.x + sx(8.0F, scale), base.y - sx(52.0F, scale) };

    renderer.draw_line(top, left_shoulder, sx(2.0F, scale), text_color, surface);
    renderer.draw_line(top, right_shoulder, sx(2.0F, scale), text_color, surface);
    renderer.draw_line(left_shoulder, left_base, sx(2.0F, scale), text_color, surface);
    renderer.draw_line(right_shoulder, right_base, sx(2.0F, scale), text_color, surface);
    renderer.draw_line(left_base, right_base, sx(2.0F, scale), text_color, surface);
    renderer.draw_line({ left_base.x, left_base.y - sx(22.0F, scale) }, { base.x - sx(22.0F, scale), base.y - sx(40.0F, scale) }, sx(2.0F, scale), text_color, surface);
    renderer.draw_line({ right_base.x, right_base.y - sx(22.0F, scale) }, { base.x + sx(22.0F, scale), base.y - sx(40.0F, scale) }, sx(2.0F, scale), text_color, surface);
    renderer.draw_line({ base.x - sx(22.0F, scale), base.y - sx(40.0F, scale) }, left_base, sx(2.0F, scale), text_color, surface);
    renderer.draw_line({ base.x + sx(22.0F, scale), base.y - sx(40.0F, scale) }, right_base, sx(2.0F, scale), text_color, surface);
    renderer.draw_line({ base.x - sx(3.0F, scale), base.y - sx(45.0F, scale) }, { base.x - sx(5.0F, scale), base.y }, sx(1.2F, scale), purple_dim, surface);
    renderer.draw_line({ base.x + sx(3.0F, scale), base.y - sx(45.0F, scale) }, { base.x + sx(5.0F, scale), base.y }, sx(1.2F, scale), purple_dim, surface);
}

void draw_guidance(GlesTextRenderer& renderer, SurfaceSize surface, const RocketOsdSample& sample)
{
    const auto scale = layout_scale(surface);
    const RenderPoint center {
        .x = static_cast<float>(surface.width) * 0.50F,
        .y = static_cast<float>(surface.height) * 0.47F,
    };
    const auto progress = std::clamp(sample.altitude_km / 24.0F, 0.0F, 1.0F);
    const auto energy = std::clamp(sample.velocity_mps / 900.0F, 0.0F, 1.0F);
    const auto outer_radius = sx(190.0F + energy * 80.0F, scale);
    const auto inner_radius = sx(92.0F + progress * 60.0F, scale);
    const auto lean = std::clamp((sample.g_force - 2.0F) * 20.0F, -12.0F, 12.0F);

    draw_dashed_circle(renderer, center, outer_radius, sx(1.4F, scale), panel_dim, surface);
    draw_dashed_circle(renderer, center, inner_radius, sx(1.2F, scale), panel_dim, surface);

    RenderPoint previous {
        .x = center.x - sx(18.0F, scale),
        .y = center.y + sx(180.0F, scale),
    };
    for (int i = 1; i <= 28; ++i) {
        const auto t = static_cast<float>(i) / 28.0F;
        const auto curve = std::sin(t * pi * 0.85F) * sx(34.0F + energy * 54.0F, scale);
        const RenderPoint current {
            .x = center.x - sx(18.0F, scale) + curve,
            .y = center.y + sx(180.0F, scale) - sx(392.0F * t, scale),
        };
        renderer.draw_line(previous, current, sx(i < 17 ? 1.7F : 1.2F, scale), i < 17 ? text_color : purple, surface);
        previous = current;
    }

    const auto marker_count = 3 + static_cast<int>(std::round(progress * 3.0F));
    for (int i = 0; i < marker_count; ++i) {
        const auto t = 0.18F + static_cast<float>(i) * 0.135F;
        const auto x = center.x - sx(18.0F, scale) + std::sin(t * pi * 0.85F) * sx(34.0F + energy * 54.0F, scale);
        const auto y = center.y + sx(180.0F, scale) - sx(392.0F * t, scale);
        renderer.draw_line({ x, y - sx(14.0F, scale) }, { x - sx(8.0F, scale), y }, sx(2.0F, scale), purple, surface);
        renderer.draw_line({ x, y - sx(14.0F, scale) }, { x + sx(8.0F, scale), y }, sx(2.0F, scale), purple, surface);
    }

    const RenderPoint rocket_base {
        .x = center.x - sx(18.0F, scale) + std::sin(progress * pi * 0.85F) * sx(34.0F + energy * 54.0F, scale),
        .y = center.y + sx(180.0F, scale) - sx(110.0F * progress, scale),
    };
    draw_rocket_body(renderer, rocket_base, scale, lean, surface);
}

} // namespace

RocketOsdSample SimulatedRocketOsd::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    return RocketOsdSample {
        .velocity_mps = 512.0F + std::sin(seconds * 0.45F) * 38.0F,
        .altitude_km = 11.2F + std::sin(seconds * 0.22F) * 1.8F,
        .g_force = 2.1F + std::sin(seconds * 0.62F) * 0.35F,
        .fuel_percent = std::clamp(62.0F - seconds * 0.025F, 0.0F, 100.0F),
        .stage = 2,
        .mission_time = std::chrono::seconds(83 + static_cast<int>(seconds)),
        .status = "STAGE SEP",
    };
}

void RocketOsdRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, const RocketOsdSample& sample) const
{
    draw_guidance(renderer, surface, sample);
    draw_bottom_bars(renderer, surface, sample);
}

} // namespace glide::flow
