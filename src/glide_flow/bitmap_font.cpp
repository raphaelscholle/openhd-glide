#include "glide_flow/bitmap_font.hpp"

namespace glide::flow {
namespace {

constexpr GlyphRows space {
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
};

constexpr GlyphRows digit_0 {
    0b01110,
    0b10001,
    0b10011,
    0b10101,
    0b11001,
    0b10001,
    0b01110,
};

constexpr GlyphRows digit_1 {
    0b00100,
    0b01100,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b01110,
};

constexpr GlyphRows digit_2 {
    0b01110,
    0b10001,
    0b00001,
    0b00010,
    0b00100,
    0b01000,
    0b11111,
};

constexpr GlyphRows digit_3 {
    0b11110,
    0b00001,
    0b00001,
    0b01110,
    0b00001,
    0b00001,
    0b11110,
};

constexpr GlyphRows digit_4 {
    0b00010,
    0b00110,
    0b01010,
    0b10010,
    0b11111,
    0b00010,
    0b00010,
};

constexpr GlyphRows digit_5 {
    0b11111,
    0b10000,
    0b10000,
    0b11110,
    0b00001,
    0b00001,
    0b11110,
};

constexpr GlyphRows digit_6 {
    0b01110,
    0b10000,
    0b10000,
    0b11110,
    0b10001,
    0b10001,
    0b01110,
};

constexpr GlyphRows digit_7 {
    0b11111,
    0b00001,
    0b00010,
    0b00100,
    0b01000,
    0b01000,
    0b01000,
};

constexpr GlyphRows digit_8 {
    0b01110,
    0b10001,
    0b10001,
    0b01110,
    0b10001,
    0b10001,
    0b01110,
};

constexpr GlyphRows digit_9 {
    0b01110,
    0b10001,
    0b10001,
    0b01111,
    0b00001,
    0b00001,
    0b01110,
};

constexpr GlyphRows letter_f {
    0b11111,
    0b10000,
    0b10000,
    0b11110,
    0b10000,
    0b10000,
    0b10000,
};

constexpr GlyphRows letter_p {
    0b11110,
    0b10001,
    0b10001,
    0b11110,
    0b10000,
    0b10000,
    0b10000,
};

constexpr GlyphRows letter_s {
    0b01111,
    0b10000,
    0b10000,
    0b01110,
    0b00001,
    0b00001,
    0b11110,
};

constexpr GlyphRows dot {
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b01100,
    0b01100,
};

} // namespace

std::optional<GlyphRows> bitmap_glyph(char character)
{
    switch (character) {
    case '0':
        return digit_0;
    case '1':
        return digit_1;
    case '2':
        return digit_2;
    case '3':
        return digit_3;
    case '4':
        return digit_4;
    case '5':
        return digit_5;
    case '6':
        return digit_6;
    case '7':
        return digit_7;
    case '8':
        return digit_8;
    case '9':
        return digit_9;
    case 'F':
        return letter_f;
    case 'P':
        return letter_p;
    case 'S':
        return letter_s;
    case '.':
        return dot;
    case ' ':
        return space;
    default:
        return std::nullopt;
    }
}

std::size_t bitmap_text_width(std::string_view text)
{
    if (text.empty()) {
        return 0;
    }

    return (text.size() * 6U) - 1U;
}

} // namespace glide::flow

