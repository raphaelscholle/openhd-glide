#include "glide_flow/vector_font.hpp"

namespace glide::flow {
namespace {

const VectorGlyph space {
    .strokes = {},
    .advance = 0.36F,
};

const VectorGlyph dot {
    .strokes = {
        { { 0.24F, 0.03F }, { 0.24F, 0.03F } },
    },
    .advance = 0.32F,
};

const VectorGlyph dash {
    .strokes = {
        { { 0.10F, 0.50F }, { 0.56F, 0.50F } },
    },
    .advance = 0.66F,
};

const VectorGlyph colon {
    .strokes = {
        { { 0.22F, 0.68F }, { 0.22F, 0.68F } },
        { { 0.22F, 0.28F }, { 0.22F, 0.28F } },
    },
    .advance = 0.32F,
};

const VectorGlyph digit_0 {
    .strokes = {
        { { 0.18F, 0.06F }, { 0.50F, 0.06F } },
        { { 0.50F, 0.06F }, { 0.62F, 0.18F } },
        { { 0.62F, 0.18F }, { 0.62F, 0.82F } },
        { { 0.62F, 0.82F }, { 0.50F, 0.94F } },
        { { 0.50F, 0.94F }, { 0.18F, 0.94F } },
        { { 0.18F, 0.94F }, { 0.06F, 0.82F } },
        { { 0.06F, 0.82F }, { 0.06F, 0.18F } },
        { { 0.06F, 0.18F }, { 0.18F, 0.06F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_1 {
    .strokes = {
        { { 0.22F, 0.78F }, { 0.42F, 0.94F } },
        { { 0.42F, 0.94F }, { 0.42F, 0.06F } },
        { { 0.24F, 0.06F }, { 0.60F, 0.06F } },
    },
    .advance = 0.68F,
};

const VectorGlyph digit_2 {
    .strokes = {
        { { 0.08F, 0.78F }, { 0.18F, 0.92F } },
        { { 0.18F, 0.92F }, { 0.52F, 0.92F } },
        { { 0.52F, 0.92F }, { 0.62F, 0.80F } },
        { { 0.62F, 0.80F }, { 0.62F, 0.62F } },
        { { 0.62F, 0.62F }, { 0.08F, 0.06F } },
        { { 0.08F, 0.06F }, { 0.64F, 0.06F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_3 {
    .strokes = {
        { { 0.08F, 0.90F }, { 0.58F, 0.90F } },
        { { 0.58F, 0.90F }, { 0.38F, 0.52F } },
        { { 0.38F, 0.52F }, { 0.58F, 0.48F } },
        { { 0.58F, 0.48F }, { 0.64F, 0.34F } },
        { { 0.64F, 0.34F }, { 0.56F, 0.14F } },
        { { 0.56F, 0.14F }, { 0.40F, 0.06F } },
        { { 0.40F, 0.06F }, { 0.10F, 0.10F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_4 {
    .strokes = {
        { { 0.56F, 0.06F }, { 0.56F, 0.94F } },
        { { 0.08F, 0.42F }, { 0.64F, 0.42F } },
        { { 0.08F, 0.42F }, { 0.48F, 0.94F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_5 {
    .strokes = {
        { { 0.62F, 0.92F }, { 0.12F, 0.92F } },
        { { 0.12F, 0.92F }, { 0.08F, 0.54F } },
        { { 0.08F, 0.54F }, { 0.48F, 0.54F } },
        { { 0.48F, 0.54F }, { 0.62F, 0.40F } },
        { { 0.62F, 0.40F }, { 0.58F, 0.18F } },
        { { 0.58F, 0.18F }, { 0.42F, 0.06F } },
        { { 0.42F, 0.06F }, { 0.10F, 0.10F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_6 {
    .strokes = {
        { { 0.58F, 0.88F }, { 0.22F, 0.88F } },
        { { 0.22F, 0.88F }, { 0.08F, 0.66F } },
        { { 0.08F, 0.66F }, { 0.08F, 0.22F } },
        { { 0.08F, 0.22F }, { 0.22F, 0.06F } },
        { { 0.22F, 0.06F }, { 0.50F, 0.06F } },
        { { 0.50F, 0.06F }, { 0.62F, 0.22F } },
        { { 0.62F, 0.22F }, { 0.56F, 0.44F } },
        { { 0.56F, 0.44F }, { 0.38F, 0.54F } },
        { { 0.38F, 0.54F }, { 0.10F, 0.50F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_7 {
    .strokes = {
        { { 0.08F, 0.92F }, { 0.64F, 0.92F } },
        { { 0.64F, 0.92F }, { 0.30F, 0.06F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_8 {
    .strokes = {
        { { 0.20F, 0.52F }, { 0.08F, 0.66F } },
        { { 0.08F, 0.66F }, { 0.16F, 0.88F } },
        { { 0.16F, 0.88F }, { 0.50F, 0.92F } },
        { { 0.50F, 0.92F }, { 0.62F, 0.74F } },
        { { 0.62F, 0.74F }, { 0.50F, 0.54F } },
        { { 0.50F, 0.54F }, { 0.20F, 0.52F } },
        { { 0.20F, 0.52F }, { 0.08F, 0.34F } },
        { { 0.08F, 0.34F }, { 0.18F, 0.10F } },
        { { 0.18F, 0.10F }, { 0.52F, 0.08F } },
        { { 0.52F, 0.08F }, { 0.64F, 0.30F } },
        { { 0.64F, 0.30F }, { 0.50F, 0.50F } },
    },
    .advance = 0.74F,
};

const VectorGlyph digit_9 {
    .strokes = {
        { { 0.58F, 0.48F }, { 0.30F, 0.46F } },
        { { 0.30F, 0.46F }, { 0.12F, 0.56F } },
        { { 0.12F, 0.56F }, { 0.08F, 0.78F } },
        { { 0.08F, 0.78F }, { 0.20F, 0.94F } },
        { { 0.20F, 0.94F }, { 0.50F, 0.94F } },
        { { 0.50F, 0.94F }, { 0.64F, 0.78F } },
        { { 0.64F, 0.78F }, { 0.64F, 0.32F } },
        { { 0.64F, 0.32F }, { 0.48F, 0.08F } },
        { { 0.48F, 0.08F }, { 0.12F, 0.08F } },
    },
    .advance = 0.74F,
};

const VectorGlyph letter_f {
    .strokes = {
        { { 0.10F, 0.06F }, { 0.10F, 0.94F } },
        { { 0.10F, 0.94F }, { 0.62F, 0.94F } },
        { { 0.10F, 0.52F }, { 0.52F, 0.52F } },
    },
    .advance = 0.68F,
};

const VectorGlyph letter_p {
    .strokes = {
        { { 0.10F, 0.06F }, { 0.10F, 0.94F } },
        { { 0.10F, 0.94F }, { 0.48F, 0.94F } },
        { { 0.48F, 0.94F }, { 0.62F, 0.80F } },
        { { 0.62F, 0.80F }, { 0.58F, 0.62F } },
        { { 0.58F, 0.62F }, { 0.44F, 0.52F } },
        { { 0.44F, 0.52F }, { 0.10F, 0.52F } },
    },
    .advance = 0.72F,
};

const VectorGlyph letter_s {
    .strokes = {
        { { 0.62F, 0.88F }, { 0.46F, 0.94F } },
        { { 0.46F, 0.94F }, { 0.18F, 0.90F } },
        { { 0.18F, 0.90F }, { 0.08F, 0.72F } },
        { { 0.08F, 0.72F }, { 0.18F, 0.56F } },
        { { 0.18F, 0.56F }, { 0.52F, 0.46F } },
        { { 0.52F, 0.46F }, { 0.64F, 0.30F } },
        { { 0.64F, 0.30F }, { 0.54F, 0.12F } },
        { { 0.54F, 0.12F }, { 0.26F, 0.06F } },
        { { 0.26F, 0.06F }, { 0.08F, 0.12F } },
    },
    .advance = 0.72F,
};

const VectorGlyph letter_b {
    .strokes = {
        { { 0.10F, 0.06F }, { 0.10F, 0.94F } },
        { { 0.10F, 0.94F }, { 0.48F, 0.94F } },
        { { 0.48F, 0.94F }, { 0.60F, 0.80F } },
        { { 0.60F, 0.80F }, { 0.48F, 0.56F } },
        { { 0.48F, 0.56F }, { 0.10F, 0.52F } },
        { { 0.10F, 0.52F }, { 0.50F, 0.50F } },
        { { 0.50F, 0.50F }, { 0.64F, 0.32F } },
        { { 0.64F, 0.32F }, { 0.50F, 0.08F } },
        { { 0.50F, 0.08F }, { 0.10F, 0.06F } },
    },
    .advance = 0.74F,
};

const VectorGlyph letter_c {
    .strokes = {
        { { 0.62F, 0.84F }, { 0.48F, 0.94F } },
        { { 0.48F, 0.94F }, { 0.18F, 0.90F } },
        { { 0.18F, 0.90F }, { 0.08F, 0.70F } },
        { { 0.08F, 0.70F }, { 0.08F, 0.30F } },
        { { 0.08F, 0.30F }, { 0.20F, 0.10F } },
        { { 0.20F, 0.10F }, { 0.50F, 0.06F } },
        { { 0.50F, 0.06F }, { 0.64F, 0.16F } },
    },
    .advance = 0.72F,
};

const VectorGlyph letter_d {
    .strokes = {
        { { 0.10F, 0.06F }, { 0.10F, 0.94F } },
        { { 0.10F, 0.94F }, { 0.44F, 0.90F } },
        { { 0.44F, 0.90F }, { 0.62F, 0.70F } },
        { { 0.62F, 0.70F }, { 0.62F, 0.30F } },
        { { 0.62F, 0.30F }, { 0.44F, 0.10F } },
        { { 0.44F, 0.10F }, { 0.10F, 0.06F } },
    },
    .advance = 0.76F,
};

const VectorGlyph letter_h {
    .strokes = {
        { { 0.10F, 0.06F }, { 0.10F, 0.94F } },
        { { 0.62F, 0.06F }, { 0.62F, 0.94F } },
        { { 0.10F, 0.52F }, { 0.62F, 0.52F } },
    },
    .advance = 0.78F,
};

const VectorGlyph letter_i {
    .strokes = {
        { { 0.12F, 0.94F }, { 0.54F, 0.94F } },
        { { 0.33F, 0.94F }, { 0.33F, 0.06F } },
        { { 0.12F, 0.06F }, { 0.54F, 0.06F } },
    },
    .advance = 0.66F,
};

const VectorGlyph letter_m {
    .strokes = {
        { { 0.08F, 0.06F }, { 0.08F, 0.94F } },
        { { 0.08F, 0.94F }, { 0.38F, 0.48F } },
        { { 0.38F, 0.48F }, { 0.68F, 0.94F } },
        { { 0.68F, 0.94F }, { 0.68F, 0.06F } },
    },
    .advance = 0.84F,
};

const VectorGlyph letter_n {
    .strokes = {
        { { 0.08F, 0.06F }, { 0.08F, 0.94F } },
        { { 0.08F, 0.94F }, { 0.62F, 0.06F } },
        { { 0.62F, 0.06F }, { 0.62F, 0.94F } },
    },
    .advance = 0.78F,
};

const VectorGlyph letter_o {
    .strokes = digit_0.strokes,
    .advance = 0.74F,
};

const VectorGlyph letter_t {
    .strokes = {
        { { 0.08F, 0.94F }, { 0.64F, 0.94F } },
        { { 0.36F, 0.94F }, { 0.36F, 0.06F } },
    },
    .advance = 0.72F,
};

const VectorGlyph letter_u {
    .strokes = {
        { { 0.10F, 0.94F }, { 0.10F, 0.22F } },
        { { 0.10F, 0.22F }, { 0.24F, 0.06F } },
        { { 0.24F, 0.06F }, { 0.50F, 0.06F } },
        { { 0.50F, 0.06F }, { 0.64F, 0.22F } },
        { { 0.64F, 0.22F }, { 0.64F, 0.94F } },
    },
    .advance = 0.78F,
};

const VectorGlyph letter_z {
    .strokes = {
        { { 0.08F, 0.94F }, { 0.64F, 0.94F } },
        { { 0.64F, 0.94F }, { 0.08F, 0.06F } },
        { { 0.08F, 0.06F }, { 0.64F, 0.06F } },
    },
    .advance = 0.72F,
};

} // namespace

const VectorGlyph* vector_glyph(char character)
{
    switch (character) {
    case '0':
        return &digit_0;
    case '1':
        return &digit_1;
    case '2':
        return &digit_2;
    case '3':
        return &digit_3;
    case '4':
        return &digit_4;
    case '5':
        return &digit_5;
    case '6':
        return &digit_6;
    case '7':
        return &digit_7;
    case '8':
        return &digit_8;
    case '9':
        return &digit_9;
    case '-':
        return &dash;
    case ':':
        return &colon;
    case 'B':
        return &letter_b;
    case 'C':
        return &letter_c;
    case 'D':
        return &letter_d;
    case 'F':
        return &letter_f;
    case 'H':
        return &letter_h;
    case 'I':
        return &letter_i;
    case 'M':
        return &letter_m;
    case 'N':
        return &letter_n;
    case 'O':
        return &letter_o;
    case 'P':
        return &letter_p;
    case 'S':
        return &letter_s;
    case 'T':
        return &letter_t;
    case 'U':
        return &letter_u;
    case 'Z':
        return &letter_z;
    case '.':
        return &dot;
    case ' ':
        return &space;
    default:
        return nullptr;
    }
}

float vector_text_width(std::string_view text)
{
    float width = 0.0F;
    for (const auto character : text) {
        const auto* glyph = vector_glyph(character);
        width += glyph != nullptr ? glyph->advance : 0.72F;
    }

    return width;
}

} // namespace glide::flow
