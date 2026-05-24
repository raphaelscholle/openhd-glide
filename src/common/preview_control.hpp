#pragma once

#include <string>

namespace glide::preview_control {

bool fps_overlay_enabled();
void set_fps_overlay_enabled(bool enabled);
bool coordinates_overlay_enabled();
void set_coordinates_overlay_enabled(bool enabled);
bool compact_readouts_enabled();
void set_compact_readouts_enabled(bool enabled);
std::string osd_layout();
void set_osd_layout(const std::string& layout);

} // namespace glide::preview_control
