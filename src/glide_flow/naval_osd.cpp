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

#include "glide_flow/naval_osd.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace glide::flow {
namespace {

constexpr float pi = 3.14159265358979323846F;
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

float sx(float value, float scale)
{
    return value * scale;
}

float normalize_degrees(float degrees)
{
    return std::fmod(std::fmod(degrees, 360.0F) + 360.0F, 360.0F);
}

RenderPoint compass_point(RenderPoint center, float radius, float degrees)
{
    const auto radians = degrees * pi / 180.0F;
    return RenderPoint {
        .x = center.x + std::sin(radians) * radius,
        .y = center.y - std::cos(radians) * radius,
    };
}

void draw_text_centered(GlesTextRenderer& renderer, std::string text, RenderPoint point, float scale, SurfaceSize surface)
{
    const auto width = renderer.measure_text_width(text, scale);
    renderer.draw(TextPlacement {
        .text = std::move(text),
        .x = point.x - width * 0.5F,
        .y = point.y,
        .scale = scale,
    }, surface);
}

void draw_diamond(GlesTextRenderer& renderer, RenderPoint center, float radius, RgbaColor color, SurfaceSize surface)
{
    const RenderPoint top { .x = center.x, .y = center.y - radius };
    const RenderPoint right { .x = center.x + radius, .y = center.y };
    const RenderPoint bottom { .x = center.x, .y = center.y + radius };
    const RenderPoint left { .x = center.x - radius, .y = center.y };
    renderer.draw_line(top, right, 1.6F, color, surface);
    renderer.draw_line(right, bottom, 1.6F, color, surface);
    renderer.draw_line(bottom, left, 1.6F, color, surface);
    renderer.draw_line(left, top, 1.6F, color, surface);
}

void draw_scope(GlesTextRenderer& renderer, SurfaceSize surface, RenderPoint center, float radius, const OsdTheme& theme)
{
    const auto scale = layout_scale(surface);
    renderer.draw_circle_outline(center, radius, sx(2.0F, scale), with_alpha(theme.secondary, 0.30F), surface);
    renderer.draw_circle_outline(center, radius - sx(9.0F, scale), sx(1.0F, scale), with_alpha(theme.secondary, 0.15F), surface);
    renderer.draw_circle_outline(center, radius * 0.52F, sx(1.0F, scale), with_alpha(theme.secondary, 0.15F), surface);

    constexpr int ticks = 72;
    for (int i = 0; i < ticks; ++i) {
        const auto degrees = static_cast<float>(i) * 360.0F / static_cast<float>(ticks);
        const auto major = i % 9 == 0;
        const auto outer = compass_point(center, radius, degrees);
        const auto inner = compass_point(center, radius - sx(major ? 18.0F : 9.0F, scale), degrees);
        renderer.draw_line(inner, outer, sx(major ? 1.6F : 1.0F, scale), major ? theme.primary : with_alpha(theme.secondary, 0.15F), surface);
    }
}

void draw_cardinals(GlesTextRenderer& renderer, SurfaceSize surface, RenderPoint center, float radius, float heading)
{
    const auto scale = layout_scale(surface);
    struct Cardinal {
        const char* label;
        float bearing;
    };
    for (const auto cardinal : { Cardinal { "N", 0.0F }, Cardinal { "E", 90.0F }, Cardinal { "S", 180.0F }, Cardinal { "W", 270.0F } }) {
        auto relative = normalize_degrees(cardinal.bearing - heading);
        if (relative > 180.0F) {
            relative -= 360.0F;
        }
        const auto point = compass_point(center, radius - sx(28.0F, scale), relative);
        draw_text_centered(renderer, cardinal.label, point, sx(18.0F, scale), surface);
    }
}

void draw_heading_marker(GlesTextRenderer& renderer, SurfaceSize surface, RenderPoint center, float radius, const OsdTheme& theme)
{
    const auto scale = layout_scale(surface);
    const auto top = compass_point(center, radius + sx(16.0F, scale), 0.0F);
    renderer.draw_line({ top.x - sx(9.0F, scale), top.y - sx(4.0F, scale) }, top, sx(2.4F, scale), theme.primary, surface);
    renderer.draw_line({ top.x + sx(9.0F, scale), top.y - sx(4.0F, scale) }, top, sx(2.4F, scale), theme.primary, surface);

    const auto bow = compass_point(center, radius * 0.72F, 0.0F);
    const auto stern = compass_point(center, radius * 0.72F, 180.0F);
    renderer.draw_line(stern, bow, sx(1.8F, scale), theme.primary, surface);
    renderer.draw_circle_outline(center, sx(12.0F, scale), sx(2.0F, scale), theme.primary, surface);
    renderer.draw_line({ center.x - sx(18.0F, scale), center.y }, { center.x + sx(18.0F, scale), center.y }, sx(1.5F, scale), with_alpha(theme.secondary, 0.30F), surface);
}

void draw_contacts(GlesTextRenderer& renderer, SurfaceSize surface, RenderPoint center, float radius, const NavalOsdSample& sample, const OsdTheme& theme)
{
    const auto scale = layout_scale(surface);
    for (const auto& contact : sample.contacts) {
        const auto relative = normalize_degrees(contact.bearing_degrees - sample.heading_degrees);
        const auto point = compass_point(center, radius * std::clamp(contact.range_normalized, 0.10F, 0.92F), relative);
        const auto color = contact.ship ? theme.primary : theme.secondary;
        draw_diamond(renderer, point, sx(contact.ship ? 4.5F : 3.2F, scale), color, surface);
        if (contact.selected) {
            renderer.draw_circle_outline(point, sx(8.0F, scale), sx(1.0F, scale), theme.secondary, surface);
        }
    }
}

} // namespace

NavalOsdSample SimulatedNavalOsd::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    const auto heading = normalize_degrees(42.0F + std::sin(seconds * 0.18F) * 24.0F);
    return NavalOsdSample {
        .heading_degrees = heading,
        .contacts = {
            NavalRadarContact { .bearing_degrees = normalize_degrees(12.0F + seconds * 1.2F), .range_normalized = 0.76F, .ship = true, .selected = false },
            NavalRadarContact { .bearing_degrees = 72.0F, .range_normalized = 0.58F, .ship = false, .selected = false },
            NavalRadarContact { .bearing_degrees = 118.0F, .range_normalized = 0.73F, .ship = true, .selected = true },
            NavalRadarContact { .bearing_degrees = 174.0F, .range_normalized = 0.48F, .ship = false, .selected = false },
            NavalRadarContact { .bearing_degrees = 210.0F, .range_normalized = 0.62F, .ship = true, .selected = false },
            NavalRadarContact { .bearing_degrees = 286.0F, .range_normalized = 0.69F, .ship = true, .selected = false },
            NavalRadarContact { .bearing_degrees = 330.0F, .range_normalized = 0.38F, .ship = false, .selected = false },
        },
    };
}

void NavalOsdRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, const NavalOsdSample& sample, const OsdTheme& theme) const
{
    renderer.set_text_color(theme.primary);
    const auto scale = layout_scale(surface);
    const auto radius = sx(205.0F, scale);
    const RenderPoint center {
        .x = static_cast<float>(surface.width) * 0.5F,
        .y = static_cast<float>(surface.height) * 0.50F,
    };

    draw_scope(renderer, surface, center, radius, theme);
    draw_cardinals(renderer, surface, center, radius, sample.heading_degrees);
    draw_contacts(renderer, surface, center, radius, sample, theme);
    draw_heading_marker(renderer, surface, center, radius, theme);
}

} // namespace glide::flow
