#pragma once

#include "lvgl.h"

#include <cstdint>
#include <string>

namespace glide::ui {

struct MinimapPosition {
    double latitude_deg {};
    double longitude_deg {};
    float heading_deg {};
};

struct MinimapOptions {
    std::string tile_root { "assets/maps" };
    int zoom { 15 };
    int tile_size { 256 };
    int grid_tiles { 5 };
    std::uint32_t background_rgb { 0x111821 };
    bool round { true };
};

class MinimapWidget {
public:
    MinimapWidget(lv_obj_t* parent, int width, int height, MinimapOptions options);
    ~MinimapWidget();

    MinimapWidget(const MinimapWidget&) = delete;
    MinimapWidget& operator=(const MinimapWidget&) = delete;

    lv_obj_t* object() const { return canvas_; }
    void set_home(double latitude_deg, double longitude_deg);
    void set_position(const MinimapPosition& position);
    void set_zoom(int zoom);
    int zoom() const;
    void render();

private:
    struct Impl;
    Impl* impl_ {};
    lv_obj_t* canvas_ {};
};

} // namespace glide::ui
