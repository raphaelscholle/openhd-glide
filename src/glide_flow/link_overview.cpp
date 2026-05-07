#include "glide_flow/link_overview.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
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

std::string mmss(std::chrono::seconds duration)
{
    const auto total = std::max<std::chrono::seconds::rep>(0, duration.count());
    const auto minutes = total / 60;
    const auto seconds = total % 60;
    std::ostringstream stream;
    stream << std::setw(2) << std::setfill('0') << minutes
           << ':' << std::setw(2) << std::setfill('0') << seconds;
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

void draw_bottom_panel(GlesTextRenderer& renderer, SurfaceSize surface)
{
    const auto scale = layout_scale(surface);
    const auto height = sx(40.0F, scale);
    const auto y = static_cast<float>(surface.height) - height;
    const auto width = static_cast<float>(surface.width);
    const auto top_y = y + sx(8.0F, scale);
    const auto notch_width = sx(140.0F, scale);
    const auto notch_slope = sx(14.0F, scale);
    const auto notch_start = (width - notch_width) * 0.5F;
    const auto notch_end = notch_start + notch_width;
    const auto notch_left = std::max(sx(6.0F, scale), notch_start - notch_slope);
    const auto notch_right = std::min(width - sx(6.0F, scale), notch_end + notch_slope);
    const auto bottom_y = y + height;

    renderer.draw_filled_quad({ 0.0F, top_y }, { notch_left, top_y }, { 0.0F, bottom_y }, { notch_left, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_right, top_y }, { width, top_y }, { notch_right, bottom_y }, { width, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_left, top_y }, { notch_start, y }, { notch_left, bottom_y }, { notch_start, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_start, y }, { notch_end, y }, { notch_start, bottom_y }, { notch_end, bottom_y }, panel_bg, surface);
    renderer.draw_filled_quad({ notch_end, y }, { notch_right, top_y }, { notch_end, bottom_y }, { notch_right, bottom_y }, panel_bg, surface);
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

std::vector<BottomSlot> left_bottom_slots(const LinkOverviewSample& sample)
{
    return {
        { "GND", fixed_1(sample.ground_voltage_v) + "V" },
        { "USED", sample.ground_mah > 0 ? std::to_string(sample.ground_mah) + "mAh" : "N/A" },
        { "AIR", fixed_1(sample.air_voltage_v) + "V" },
        { "AMP", fixed_1(sample.air_current_a) + "A" },
    };
}

std::vector<BottomSlot> right_bottom_slots(const LinkOverviewSample& sample)
{
    return {
        { "SPD", std::to_string(static_cast<int>(std::round(sample.air_speed_mps))) + " m/s" },
        { "HOME", fixed_1(sample.home_distance_m) + "m" },
        { "SAT", std::to_string(sample.satellites) },
        { "", "" },
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
    const auto estimated_width = static_cast<float>(text.size()) * sx(13.0F, scale) * 0.56F;
    draw_text(renderer, text, center_x - estimated_width * 0.5F, baseline, sx(13.0F, scale), surface);
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
    const auto slot_width = width / 4.0F;
    for (std::size_t i = 0; i < slots.size() && i < 4U; ++i) {
        draw_bottom_slot(
            renderer,
            slots[i],
            start_x + (static_cast<float>(i) + 0.5F) * slot_width,
            baseline,
            scale,
            surface);
    }
}

void draw_bottom(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample)
{
    const auto scale = layout_scale(surface);
    const auto height = sx(40.0F, scale);
    const auto y = static_cast<float>(surface.height) - height;
    const auto center_x = static_cast<float>(surface.width) * 0.5F;
    const auto notch_safe_width = sx(168.0F, scale);
    const auto side_margin = sx(20.0F, scale);
    const auto content_top = y + sx(8.0F, scale);
    const auto side_baseline = content_top + sx(24.0F, scale);
    const auto left_width = std::max(0.0F, center_x - notch_safe_width * 0.5F - side_margin);
    const auto right_x = center_x + notch_safe_width * 0.5F;
    const auto right_width = std::max(0.0F, static_cast<float>(surface.width) - right_x - side_margin);

    draw_bottom_panel(renderer, surface);
    draw_text(
        renderer,
        sample.flight_mode != nullptr ? sample.flight_mode : "MANUAL",
        center_x - sx(36.0F, scale),
        y + sx(15.0F, scale),
        sx(sample.armed ? 15.0F : 14.0F, scale),
        surface);
    draw_text(renderer, mmss(sample.flight_time), center_x - sx(26.0F, scale), y + sx(33.0F, scale), sx(14.0F, scale), surface);
    draw_bottom_slots(renderer, left_bottom_slots(sample), side_margin, left_width, side_baseline, scale, surface);
    draw_bottom_slots(renderer, right_bottom_slots(sample), right_x, right_width, side_baseline, scale, surface);
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
        .satellites = 14 + static_cast<int>(std::round(std::sin(seconds * 0.20F) * 2.0F)),
        .flight_time = std::chrono::seconds(static_cast<int>(seconds)),
        .armed = std::sin(seconds * 0.08F) > -0.30F,
        .flight_mode = "LOITER",
    };
}

void LinkOverviewRenderer::draw(GlesTextRenderer& renderer, SurfaceSize surface, const LinkOverviewSample& sample) const
{
    draw_left(renderer, surface, sample);
    draw_right(renderer, surface, sample);
    draw_bottom(renderer, surface, sample);
}

} // namespace glide::flow
