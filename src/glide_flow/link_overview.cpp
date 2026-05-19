#include "glide_flow/link_overview.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace glide::flow {
namespace {

constexpr RgbaColor panel_bg { .red = 0.055F, .green = 0.075F, .blue = 0.095F, .alpha = 0.94F };
constexpr RgbaColor panel_line { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.90F };
constexpr RgbaColor text_color { .red = 0.92F, .green = 0.96F, .blue = 1.0F, .alpha = 0.96F };
constexpr RgbaColor muted_color { .red = 0.45F, .green = 0.50F, .blue = 0.52F, .alpha = 0.75F };
constexpr RgbaColor warn_color { .red = 1.0F, .green = 0.84F, .blue = 0.18F, .alpha = 0.96F };
constexpr RgbaColor bad_color { .red = 1.0F, .green = 0.16F, .blue = 0.14F, .alpha = 0.96F };

std::string fixed_1(float value)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << value;
    return stream.str();
}

std::string fixed_5(double value)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(5);
    stream << value;
    return stream.str();
}

RgbaColor quality_color(int value)
{
    if (value >= 70) {
        return panel_line;
    }
    if (value >= 35) {
        return warn_color;
    }
    return bad_color;
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

void draw_panel_left(GlesTextRenderer& renderer, float x, float y, float width, float height, SurfaceSize surface)
{
    renderer.draw_filled_quad(
        { x, y },
        { x + width, y },
        { x, y + height },
        { x + (width * 0.80F), y + height },
        panel_bg,
        surface);
}

void draw_panel_right(GlesTextRenderer& renderer, float x, float y, float width, float height, SurfaceSize surface)
{
    renderer.draw_filled_quad(
        { x, y },
        { x + width, y },
        { x + (width * 0.20F), y + height },
        { x + width, y + height },
        panel_bg,
        surface);
}

void draw_skew_blocks(GlesTextRenderer& renderer, float x, float y, int value, SurfaceSize surface)
{
    const auto scale = layout_scale(surface);
    constexpr int count = 8;
    const auto block_width = sx(18.0F, scale);
    const auto block_height = sx(9.0F, scale);
    const auto skew = sx(5.0F, scale);
    const auto spacing = sx(4.0F, scale);

    for (int i = 0; i < count; ++i) {
        const auto bx = x + static_cast<float>(i) * (block_width + skew + spacing);
        const auto active = value >= (i + 1) * 10;
        const auto base = active ? quality_color(value) : muted_color;
        const RgbaColor fill {
            .red = base.red,
            .green = base.green,
            .blue = base.blue,
            .alpha = active ? 0.92F : 0.18F,
        };

        renderer.draw_filled_quad(
            { bx + skew, y },
            { bx + block_width + skew, y },
            { bx, y + block_height },
            { bx + block_width, y + block_height },
            fill,
            surface);
        renderer.draw_line({ bx, y + block_height }, { bx + skew, y }, sx(1.2F, scale), panel_line, surface);
        renderer.draw_line({ bx + skew, y }, { bx + block_width + skew, y }, sx(1.2F, scale), panel_line, surface);
        renderer.draw_line({ bx + block_width + skew, y }, { bx + block_width, y + block_height }, sx(1.2F, scale), panel_line, surface);
        renderer.draw_line({ bx + block_width, y + block_height }, { bx, y + block_height }, sx(1.2F, scale), panel_line, surface);
    }
}

void draw_left(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample)
{
    const auto scale = layout_scale(surface);
    constexpr float x = 0.0F;
    constexpr float y = 0.0F;
    const auto width = sx(420.0F, scale);
    const auto height = sx(72.0F, scale);

    draw_panel_left(renderer, x, y, width, height, surface);
    renderer.draw_circle_outline({ x + sx(26.0F, scale), y + sx(28.0F, scale) }, sx(16.0F, scale), sx(2.0F, scale), panel_line, surface);
    draw_text(renderer, "O", x + sx(17.0F, scale), y + sx(36.0F, scale), sx(16.0F, scale), surface);
    draw_text(
        renderer,
        std::to_string(sample.rssi_dbm) + " DBM " + std::to_string(sample.txc_temp_c) + "C",
        x + sx(66.0F, scale),
        y + sx(36.0F, scale),
        sx(18.0F, scale),
        surface);
    draw_text(renderer, "MCS:" + std::to_string(sample.mcs), x + sx(300.0F, scale), y + sx(36.0F, scale), sx(15.0F, scale), surface);
    draw_skew_blocks(renderer, x + sx(66.0F, scale), y + sx(50.0F, scale), sample.downlink_quality, surface);
}

void draw_right(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample)
{
    const auto scale = layout_scale(surface);
    const auto width = sx(420.0F, scale);
    const auto height = sx(72.0F, scale);
    const auto x = static_cast<float>(surface.width) - width;
    constexpr float y = 0.0F;

    draw_panel_right(renderer, x, y, width, height, surface);
    draw_text(renderer, sample.uplink_ok ? "UP" : "NO", x + sx(92.0F, scale), y + sx(36.0F, scale), sx(15.0F, scale), surface);
    draw_text(
        renderer,
        std::to_string(sample.frequency_mhz) + "MHZ",
        x + sx(165.0F, scale),
        y + sx(36.0F, scale),
        sx(15.0F, scale),
        surface);
    draw_text(
        renderer,
        fixed_1(sample.bitrate_mbit) + "MBIT",
        x + sx(270.0F, scale),
        y + sx(36.0F, scale),
        sx(15.0F, scale),
        surface);

    const auto record_color = sample.recording ? bad_color : text_color;
    renderer.draw_circle_outline({ x + width - sx(24.0F, scale), y + sx(33.0F, scale) }, sx(8.0F, scale), sx(3.0F, scale), record_color, surface);
    draw_skew_blocks(renderer, x + sx(162.0F, scale), y + sx(50.0F, scale), sample.rc_quality, surface);
}

struct BottomSlot {
    std::string label;
    std::string value;
};

std::string distance_text(float meters)
{
    if (meters >= 1000.0F) {
        return fixed_1(meters / 1000.0F) + "km";
    }
    return std::to_string(static_cast<int>(std::round(std::max(0.0F, meters)))) + "m";
}

std::string wind_direction_text(float degrees)
{
    constexpr std::string_view names[] {
        "N",
        "NE",
        "E",
        "SE",
        "S",
        "SW",
        "W",
        "NW",
    };
    const auto normalized = std::fmod(std::fmod(degrees, 360.0F) + 360.0F, 360.0F);
    const auto index = static_cast<std::size_t>(std::floor((normalized + 22.5F) / 45.0F)) % (sizeof(names) / sizeof(names[0]));
    return std::string(names[index]);
}

std::string coordinates_text(double latitude, double longitude)
{
    return fixed_5(latitude) + "," + fixed_5(longitude);
}

std::string kmh_text(float meters_per_second)
{
    return std::to_string(static_cast<int>(std::round(std::max(0.0F, meters_per_second) * 3.6F))) + "km/h";
}

std::vector<BottomSlot> primary_bottom_slots(const LinkOverviewSample& sample)
{
    return {
        { "GND", fixed_1(sample.ground_voltage_v) + "V" },
        { "AIR", fixed_1(sample.air_voltage_v) + "V" },
        { "CUR", fixed_1(sample.air_current_a) + "A" },
        { "HOME", fixed_1(sample.home_distance_m) + "m" },
        { "TOTAL", distance_text(sample.total_distance_m) },
        { "SATS", std::to_string(sample.satellites) },
    };
}

std::vector<BottomSlot> secondary_bottom_slots(const LinkOverviewSample& sample)
{
    return {
        { "SPD", kmh_text(sample.air_speed_mps) },
        { "WIND", kmh_text(sample.wind_speed_mps) + " " + wind_direction_text(sample.wind_direction_deg) },
        { "", fixed_1(sample.mah_per_km) + "mAh/km" },
    };
}

void draw_bottom_slot(
    GlesTextRenderer& renderer,
    const BottomSlot& slot,
    float center_x,
    float baseline,
    float scale,
    SurfaceSize surface)
{
    if (slot.value.empty() || slot.value == "N/A") {
        return;
    }

    const auto text = slot.label.empty() ? slot.value : slot.label + " " + slot.value;
    const auto font_scale = sx(12.5F, scale);
    const auto estimated_width = static_cast<float>(text.size()) * font_scale * 0.56F;
    draw_text(renderer, text, center_x - estimated_width * 0.5F, baseline, font_scale, surface);
}

void draw_bottom_slots(
    GlesTextRenderer& renderer,
    const std::vector<BottomSlot>& slots,
    float start_x,
    float width,
    float baseline,
    float scale,
    SurfaceSize surface)
{
    if (slots.empty()) {
        return;
    }

    const auto slot_width = width / static_cast<float>(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        draw_bottom_slot(
            renderer,
            slots[i],
            start_x + (static_cast<float>(i) + 0.5F) * slot_width,
            baseline,
            scale,
            surface);
    }
}

void draw_top_center_mode(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample)
{
    const auto scale = layout_scale(surface);
    const auto center_x = static_cast<float>(surface.width) * 0.5F;
    const auto width = sx(220.0F, scale);
    const auto height = sx(42.0F, scale);
    const auto y = sx(8.0F, scale);
    const auto x = center_x - width * 0.5F;
    const auto skew = sx(14.0F, scale);
    const auto mode = sample.flight_mode != nullptr ? sample.flight_mode : "MANUAL";
    const auto mode_scale = sx(sample.armed ? 18.0F : 17.0F, scale);
    const auto estimated_width = static_cast<float>(std::string_view(mode).size()) * mode_scale * 0.56F;

    renderer.draw_filled_quad(
        { x + skew, y },
        { x + width - skew, y },
        { x, y + height },
        { x + width, y + height },
        panel_bg,
        surface);
    renderer.draw_line({ x + skew, y }, { x + width - skew, y }, sx(1.5F, scale), panel_line, surface);
    renderer.draw_line({ x, y + height }, { x + width, y + height }, sx(1.5F, scale), panel_line, surface);
    draw_text(renderer, mode, center_x - estimated_width * 0.5F, y + sx(28.0F, scale), mode_scale, surface);
}

void draw_coordinate_block(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample)
{
    if (!sample.show_coordinates) {
        return;
    }

    const auto scale = layout_scale(surface);
    const auto block_width = sx(286.0F, scale);
    const auto block_height = sx(34.0F, scale);
    const auto margin = sx(20.0F, scale);
    const auto bottom_bar_height = sx(58.0F, scale);
    const auto x = static_cast<float>(surface.width) - block_width - margin;
    const auto y = static_cast<float>(surface.height) - bottom_bar_height - block_height - sx(7.0F, scale);
    const auto text = coordinates_text(sample.latitude_deg, sample.longitude_deg);
    const auto font_scale = sx(12.5F, scale);

    renderer.draw_filled_quad(
        { x, y },
        { x + block_width, y },
        { x, y + block_height },
        { x + block_width, y + block_height },
        panel_bg,
        surface);
    renderer.draw_line({ x, y }, { x + block_width, y }, sx(1.2F, scale), panel_line, surface);
    draw_text(renderer, "COORD", x + sx(12.0F, scale), y + sx(22.0F, scale), sx(11.5F, scale), surface);
    draw_text(renderer, text, x + sx(68.0F, scale), y + sx(22.0F, scale), font_scale, surface);
}

void draw_bottom(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample)
{
    const auto scale = layout_scale(surface);
    const auto height = sx(58.0F, scale);
    const auto y = static_cast<float>(surface.height) - height;
    const auto side_margin = sx(20.0F, scale);
    const auto content_width = std::max(0.0F, static_cast<float>(surface.width) - side_margin * 2.0F);

    renderer.draw_filled_quad(
        { 0.0F, y },
        { static_cast<float>(surface.width), y },
        { 0.0F, y + height },
        { static_cast<float>(surface.width), y + height },
        panel_bg,
        surface);
    renderer.draw_line({ 0.0F, y }, { static_cast<float>(surface.width), y }, sx(1.5F, scale), panel_line, surface);
    draw_bottom_slots(renderer, primary_bottom_slots(sample), side_margin, content_width, y + sx(23.0F, scale), scale, surface);
    draw_bottom_slots(renderer, secondary_bottom_slots(sample), side_margin, content_width, y + sx(47.0F, scale), scale, surface);
}

} // namespace

LinkOverviewSample SimulatedLinkOverview::sample(std::chrono::steady_clock::time_point now) const
{
    const auto seconds = std::chrono::duration<float>(now - start_).count();
    const auto wave = std::sin(seconds * 0.55F);
    const auto faster = std::sin(seconds * 1.10F);

    return LinkOverviewSample {
        .rssi_dbm = -58 + static_cast<int>(std::round(wave * 5.0F)),
        .txc_temp_c = 62 + static_cast<int>(std::round(faster * 4.0F)),
        .mcs = 2,
        .downlink_quality = std::clamp(76 + static_cast<int>(std::round(wave * 18.0F)), 0, 100),
        .frequency_mhz = 5800,
        .bitrate_mbit = 12.5F + faster * 1.8F,
        .recording = std::sin(seconds * 0.35F) > 0.0F,
        .uplink_ok = std::sin(seconds * 0.23F) > -0.85F,
        .rc_quality = std::clamp(68 + static_cast<int>(std::round(faster * 20.0F)), 0, 100),
        .ground_voltage_v = 11.8F + wave * 0.2F,
        .ground_mah = 0,
        .air_voltage_v = 15.7F + faster * 0.3F,
        .air_current_a = 4.2F + std::sin(seconds * 0.75F) * 1.4F,
        .air_speed_mps = 23.0F + std::sin(seconds * 0.36F) * 6.0F,
        .home_distance_m = 145.0F + std::sin(seconds * 0.18F) * 45.0F,
        .total_distance_m = 1820.0F + seconds * 7.5F,
        .wind_speed_mps = 5.0F + std::sin(seconds * 0.31F) * 2.0F,
        .wind_direction_deg = 290.0F + std::sin(seconds * 0.14F) * 35.0F,
        .latitude_deg = 47.397742 + std::sin(seconds * 0.025F) * 0.003,
        .longitude_deg = 8.545594 + std::cos(seconds * 0.021F) * 0.003,
        .mah_per_km = 132.0F + std::sin(seconds * 0.42F) * 9.0F,
        .satellites = 14 + static_cast<int>(std::round(std::sin(seconds * 0.20F) * 2.0F)),
        .show_coordinates = true,
        .flight_time = std::chrono::seconds(static_cast<int>(seconds)),
        .armed = std::sin(seconds * 0.08F) > -0.30F,
        .flight_mode = "LOITER",
    };
}

void LinkOverviewRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample) const
{
    draw_left(renderer, surface, sample);
    draw_right(renderer, surface, sample);
    draw_top_center_mode(renderer, surface, sample);
    draw_coordinate_block(renderer, surface, sample);
    draw_bottom(renderer, surface, sample);
}

} // namespace glide::flow
