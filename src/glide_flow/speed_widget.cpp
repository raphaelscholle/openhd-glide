#include "glide_flow/speed_widget.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace glide::flow {
namespace {

constexpr RgbaColor line_color { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.96F };
constexpr RgbaColor glow_color { .red = 0.02F, .green = 0.95F, .blue = 0.45F, .alpha = 0.40F };
constexpr RgbaColor panel_bg { .red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.35F };

float layout_scale(SurfaceSize surface)
{
    return std::max(0.70F, std::min(
        static_cast<float>(surface.width) / 1280.0F,
        static_cast<float>(surface.height) / 720.0F));
}

void draw_rect(
    GlesTextRenderer& renderer,
    float x,
    float y,
    float width,
    float height,
    RgbaColor color,
    SurfaceSize surface)
{
    renderer.draw_filled_quad(
        { x, y },
        { x + width, y },
        { x, y + height },
        { x + width, y + height },
        color,
        surface);
}

void draw_text(
    GlesTextRenderer& renderer,
    std::string text,
    float x,
    float baseline,
    float scale,
    SurfaceSize surface)
{
    renderer.draw(TextPlacement {
        .text = std::move(text),
        .x = x,
        .y = baseline,
        .scale = scale,
    }, surface);
}

std::string speed_text(float speed_mps)
{
    const auto rounded_speed = static_cast<int>(std::round(std::max(0.0F, speed_mps)));
    std::ostringstream stream;
    stream << rounded_speed;
    if (rounded_speed < 99) {
        stream << " m/s";
    }
    return stream.str();
}

float fitted_text_scale(const std::string& text, float requested_scale, float available_width)
{
    constexpr auto average_glyph_width = 0.58F;
    const auto estimated_width = static_cast<float>(text.size()) * requested_scale * average_glyph_width;
    if (estimated_width <= available_width) {
        return requested_scale;
    }
    return std::max(10.0F, requested_scale * (available_width / estimated_width));
}

} // namespace

SpeedSample SimulatedSpeed::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    return SpeedSample {
        .speed_mps = 22.0F + std::sin(seconds * 0.36F) * 7.0F + std::sin(seconds * 0.93F) * 2.0F,
    };
}

void SpeedWidgetRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, SpeedSample sample) const
{
    const auto scale = layout_scale(surface);
    const auto ladder_height = ladder_height_ * scale;
    const auto pointer_width = pointer_width_ * scale;
    const auto pointer_height = pointer_height_ * scale;
    const auto left_margin = 42.0F * scale;
    const auto center_y = static_cast<float>(surface.height) * 0.5F;
    const auto pointer_x = left_margin;
    const auto pointer_y = center_y - pointer_height * 0.5F;
    const auto ladder_x = pointer_x + pointer_width + 10.0F * scale;
    const auto ladder_top = center_y - ladder_height * 0.5F;
    const auto tick_x = ladder_x + 32.0F * scale;
    const auto ratio = ladder_height / speed_range_mps_;
    const auto rounded_speed = std::round(std::max(0.0F, sample.speed_mps));

    for (int speed = static_cast<int>(rounded_speed - speed_range_mps_ / 2.0F);
         speed <= static_cast<int>(rounded_speed + speed_range_mps_ / 2.0F);
         ++speed) {
        const auto y = center_y - (static_cast<float>(speed) - rounded_speed) * ratio;
        if (y < ladder_top - 14.0F * scale || y > ladder_top + ladder_height + 14.0F * scale) {
            continue;
        }

        if (speed % 10 == 0) {
            if (speed >= 0) {
                draw_rect(renderer, tick_x, y, 12.0F * scale, 3.0F * scale, glow_color, surface);
                draw_rect(renderer, tick_x, y, 12.0F * scale, 2.0F * scale, line_color, surface);
                if (speed > rounded_speed + 5.0F || speed < rounded_speed - 5.0F) {
                    const auto label = std::to_string(speed);
                    const auto label_x = tick_x - ((label.size() > 2U) ? 34.0F : 25.0F) * scale;
                    draw_text(renderer, label, label_x, y + 6.0F * scale, 12.0F * scale, surface);
                }
            } else {
                draw_rect(renderer, tick_x, y - 12.0F * scale, 15.0F * scale, 15.0F * scale, glow_color, surface);
                draw_rect(renderer, tick_x + scale, y - 11.0F * scale, 13.0F * scale, 13.0F * scale, line_color, surface);
            }
        } else if (speed % 5 == 0 && speed > speed_minimum_mps_) {
            draw_rect(renderer, tick_x + 5.0F * scale, y, 7.0F * scale, 2.0F * scale, glow_color, surface);
            draw_rect(renderer, tick_x + 5.0F * scale, y, 7.0F * scale, scale, line_color, surface);
        }
    }

    const RenderPoint top_left { .x = pointer_x, .y = pointer_y };
    const RenderPoint top_notch { .x = pointer_x + pointer_width - 14.0F * scale, .y = pointer_y };
    const RenderPoint tip { .x = pointer_x + pointer_width, .y = center_y };
    const RenderPoint bottom_notch { .x = pointer_x + pointer_width - 14.0F * scale, .y = pointer_y + pointer_height };
    const RenderPoint bottom_left { .x = pointer_x, .y = pointer_y + pointer_height };

    renderer.draw_filled_quad(top_left, top_notch, bottom_left, bottom_notch, panel_bg, surface);
    renderer.draw_line(top_left, top_notch, 4.0F * scale, glow_color, surface);
    renderer.draw_line(top_notch, tip, 4.0F * scale, glow_color, surface);
    renderer.draw_line(tip, bottom_notch, 4.0F * scale, glow_color, surface);
    renderer.draw_line(bottom_notch, bottom_left, 4.0F * scale, glow_color, surface);
    renderer.draw_line(bottom_left, top_left, 4.0F * scale, glow_color, surface);

    renderer.draw_line(top_left, top_notch, 1.4F * scale, line_color, surface);
    renderer.draw_line(top_notch, tip, 1.4F * scale, line_color, surface);
    renderer.draw_line(tip, bottom_notch, 1.4F * scale, line_color, surface);
    renderer.draw_line(bottom_notch, bottom_left, 1.4F * scale, line_color, surface);
    renderer.draw_line(bottom_left, top_left, 1.4F * scale, line_color, surface);

    const auto value_text = speed_text(sample.speed_mps);
    const auto value_scale = fitted_text_scale(value_text, 16.0F * scale, pointer_width - 24.0F * scale);
    draw_text(
        renderer,
        value_text,
        pointer_x + 8.0F * scale,
        center_y + value_scale * 0.44F,
        value_scale,
        surface);
}

} // namespace glide::flow
