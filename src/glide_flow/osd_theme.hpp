#pragma once

#include "glide_flow/gles_text_renderer.hpp"

namespace glide::flow {

struct OsdTheme {
    RgbaColor text { .red = 0.92F, .green = 0.96F, .blue = 1.0F, .alpha = 0.98F };
    RgbaColor bar_background { .red = 0.055F, .green = 0.075F, .blue = 0.095F, .alpha = 0.94F };
    RgbaColor primary { .red = 0.60F, .green = 1.0F, .blue = 0.72F, .alpha = 0.90F };
    RgbaColor secondary { .red = 0.33F, .green = 0.66F, .blue = 1.0F, .alpha = 0.90F };
};

} // namespace glide::flow
