#pragma once

#include "glide_flow/fps_overlay.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace glide::flow {

struct RgbaColor {
    float red {};
    float green {};
    float blue {};
    float alpha { 1.0F };
};

struct RenderPoint {
    float x {};
    float y {};
};

class GlesTextRenderer {
public:
    ~GlesTextRenderer();

    bool available() const;
    void clear(float red, float green, float blue, float alpha, SurfaceSize surface);
    void draw_filled_quad(
        RenderPoint top_left,
        RenderPoint top_right,
        RenderPoint bottom_left,
        RenderPoint bottom_right,
        RgbaColor color,
        SurfaceSize surface);
    void draw_line(RenderPoint start, RenderPoint end, float thickness, RgbaColor color, SurfaceSize surface);
    void draw_circle_outline(RenderPoint center, float radius, float thickness, RgbaColor color, SurfaceSize surface);
    void draw(const TextPlacement& placement, SurfaceSize surface);
    std::string last_error() const;

private:
    struct GlyphTexture {
        std::uint32_t texture {};
        int width {};
        int height {};
        int bearing_x {};
        int bearing_y {};
        unsigned int advance {};
    };

    bool ensure_initialized();
    bool ensure_text_initialized();
    bool draw_antialiased_text(const TextPlacement& placement, SurfaceSize surface);
    GlyphTexture* load_glyph(char character, unsigned int pixel_size);

    std::string last_error_;
    std::uint32_t program_ {};
    std::int32_t position_location_ { -1 };
    std::int32_t color_location_ { -1 };
    std::uint32_t text_program_ {};
    std::int32_t text_position_location_ { -1 };
    std::int32_t text_texcoord_location_ { -1 };
    std::int32_t text_color_location_ { -1 };
    std::int32_t text_sampler_location_ { -1 };
    void* freetype_library_ {};
    void* freetype_face_ {};
    bool text_init_attempted_ {};
    std::unordered_map<std::uint32_t, GlyphTexture> glyph_cache_;
};

} // namespace glide::flow
