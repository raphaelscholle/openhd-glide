#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace glide::flow {

struct VectorPoint {
    float x {};
    float y {};
};

struct VectorStroke {
    VectorPoint start;
    VectorPoint end;
};

struct VectorGlyph {
    std::vector<VectorStroke> strokes;
    float advance {};
};

const VectorGlyph* vector_glyph(char character);
float vector_text_width(std::string_view text);
constexpr float vector_text_height()
{
    return 1.0F;
}

} // namespace glide::flow

