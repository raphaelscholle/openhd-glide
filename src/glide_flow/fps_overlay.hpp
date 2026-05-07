#pragma once

#include <cstdint>
#include <string>

namespace glide::flow {

struct SurfaceSize {
    std::uint32_t width {};
    std::uint32_t height {};
};

struct TextPlacement {
    std::string text;
    float x {};
    float y {};
    float scale {};
};

class FpsOverlay {
public:
    TextPlacement layout(double fps, SurfaceSize surface) const;

private:
    static constexpr float margin_ = 18.0F;
    static constexpr float scale_ = 22.0F;
};

} // namespace glide::flow
