#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace glide::flow {

using GlyphRows = std::array<std::uint8_t, 7>;

std::optional<GlyphRows> bitmap_glyph(char character);
std::size_t bitmap_text_width(std::string_view text);
constexpr std::size_t bitmap_text_height()
{
    return 7;
}

} // namespace glide::flow

