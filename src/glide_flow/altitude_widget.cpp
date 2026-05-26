/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * © OpenHD, All Rights Reserved.
 ******************************************************************************/

#include "glide_flow/altitude_widget.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace glide::flow {
namespace {

float layout_scale(SurfaceSize surface)
{
    return std::max(0.70F, std::min(
        static_cast<float>(surface.width) / 1280.0F,
        static_cast<float>(surface.height) / 720.0F));
}

RgbaColor with_alpha(RgbaColor color, float alpha)
{
    color.alpha = alpha;
    return color;
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

std::string altitude_text(float altitude_m)
{
    std::ostringstream stream;
    stream << static_cast<int>(std::round(altitude_m));
    if (altitude_m < 999.0F) {
        stream << " m";
    }
    return stream.str();
}

std::string compact_altitude_text(float altitude_m)
{
    return std::to_string(static_cast<int>(std::round(altitude_m)));
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

void draw_compact_altitude(GlesTextRenderer& renderer, SurfaceSize surface, AltitudeSample sample, const OsdTheme& theme)
{
    const auto scale = layout_scale(surface);
    const auto width = 74.0F * scale;
    const auto height = 66.0F * scale;
    const auto x = static_cast<float>(surface.width) - 46.0F * scale - width;
    const auto y = static_cast<float>(surface.height) * 0.5F - 38.0F * scale;
    const auto label = std::string("ALT");
    const auto value = compact_altitude_text(sample.altitude_m);
    const auto label_scale = 9.0F * scale;
    const auto value_scale = fitted_text_scale(value, 23.0F * scale, width - 12.0F * scale);

    renderer.set_text_color(theme.primary);
    draw_rect(renderer, x, y, width, height, with_alpha(theme.secondary, 0.25F), surface);
    draw_text(renderer, label, x + (width - renderer.measure_text_width(label, label_scale)) * 0.5F, y + 19.0F * scale, label_scale, surface);
    draw_text(renderer, value, x + (width - renderer.measure_text_width(value, value_scale)) * 0.5F, y + 51.0F * scale, value_scale, surface);
}

} // namespace

AltitudeSample SimulatedAltitude::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    return AltitudeSample {
        .altitude_m = 72.0F + std::sin(seconds * 0.42F) * 28.0F + std::sin(seconds * 0.11F) * 8.0F,
    };
}

void AltitudeWidgetRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, AltitudeSample sample, const OsdTheme& theme, bool compact) const
{
    const auto line_color = with_alpha(theme.primary, 0.96F);
    const auto glow_color = with_alpha(theme.secondary, 0.40F);
    const auto panel_bg = with_alpha(theme.secondary, 0.25F);
    renderer.set_text_color(theme.primary);
    if (compact) {
        draw_compact_altitude(renderer, surface, sample, theme);
        return;
    }

    const auto scale = layout_scale(surface);
    const auto ladder_height = ladder_height_ * scale;
    const auto pointer_width = pointer_width_ * scale;
    const auto pointer_height = pointer_height_ * scale;
    const auto right_margin = 42.0F * scale;
    const auto center_y = static_cast<float>(surface.height) * 0.5F;
    const auto pointer_x = static_cast<float>(surface.width) - right_margin - pointer_width;
    const auto pointer_y = center_y - pointer_height * 0.5F;
    const auto ladder_x = pointer_x - (42.0F * scale);
    const auto ladder_top = center_y - ladder_height * 0.5F;
    const auto ratio = ladder_height / altitude_range_m_;
    const auto rounded_altitude = std::round(sample.altitude_m);

    for (int altitude = static_cast<int>(rounded_altitude - altitude_range_m_ / 2.0F);
         altitude <= static_cast<int>(rounded_altitude + altitude_range_m_ / 2.0F);
         ++altitude) {
        const auto y = center_y - (static_cast<float>(altitude) - rounded_altitude) * ratio;
        if (y < ladder_top - 12.0F * scale || y > ladder_top + ladder_height + 12.0F * scale) {
            continue;
        }

        if (altitude % 10 == 0) {
            if (altitude >= 0) {
                draw_rect(renderer, ladder_x, y, 12.0F * scale, 3.0F * scale, glow_color, surface);
                draw_rect(renderer, ladder_x, y, 12.0F * scale, 2.0F * scale, line_color, surface);
                if (altitude > rounded_altitude + 5.0F || altitude < rounded_altitude - 5.0F) {
                    draw_text(
                        renderer,
                        std::to_string(altitude - 10),
                        ladder_x + 20.0F * scale,
                        y + 6.0F * scale,
                        12.0F * scale,
                        surface);
                }
            } else {
                draw_rect(renderer, ladder_x, y - 15.0F * scale, 15.0F * scale, 15.0F * scale, glow_color, surface);
                draw_rect(renderer, ladder_x + scale, y - 14.0F * scale, 13.0F * scale, 13.0F * scale, line_color, surface);
            }
        } else if (altitude % 5 == 0 && altitude > 0) {
            draw_rect(renderer, ladder_x, y, 7.0F * scale, 2.0F * scale, glow_color, surface);
            draw_rect(renderer, ladder_x, y, 7.0F * scale, scale, line_color, surface);
        }
    }

    const RenderPoint tip { .x = pointer_x, .y = center_y };
    const RenderPoint top_notch { .x = pointer_x + 14.0F * scale, .y = pointer_y };
    const RenderPoint top_right { .x = pointer_x + pointer_width, .y = pointer_y };
    const RenderPoint bottom_right { .x = pointer_x + pointer_width, .y = pointer_y + pointer_height };
    const RenderPoint bottom_notch { .x = pointer_x + 14.0F * scale, .y = pointer_y + pointer_height };

    renderer.draw_filled_quad(top_notch, top_right, bottom_notch, bottom_right, panel_bg, surface);
    renderer.draw_line(tip, top_notch, 4.0F * scale, glow_color, surface);
    renderer.draw_line(top_notch, top_right, 4.0F * scale, glow_color, surface);
    renderer.draw_line(top_right, bottom_right, 4.0F * scale, glow_color, surface);
    renderer.draw_line(bottom_right, bottom_notch, 4.0F * scale, glow_color, surface);
    renderer.draw_line(bottom_notch, tip, 4.0F * scale, glow_color, surface);

    renderer.draw_line(tip, top_notch, 1.4F * scale, line_color, surface);
    renderer.draw_line(top_notch, top_right, 1.4F * scale, line_color, surface);
    renderer.draw_line(top_right, bottom_right, 1.4F * scale, line_color, surface);
    renderer.draw_line(bottom_right, bottom_notch, 1.4F * scale, line_color, surface);
    renderer.draw_line(bottom_notch, tip, 1.4F * scale, line_color, surface);

    const auto value_text = altitude_text(sample.altitude_m);
    const auto value_scale = fitted_text_scale(value_text, 16.0F * scale, pointer_width - 25.0F * scale);
    draw_text(
        renderer,
        value_text,
        pointer_x + 19.0F * scale,
        center_y + value_scale * 0.44F,
        value_scale,
        surface);
}

} // namespace glide::flow
