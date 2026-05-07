#include "dev/sdl_gles_window.hpp"

#if OPENHD_GLIDE_HAS_SDL2
#include <SDL.h>
#endif

namespace glide::dev {

bool sdl_gles_available()
{
#if OPENHD_GLIDE_HAS_SDL2 && OPENHD_GLIDE_HAS_GLESV2
    return true;
#else
    return false;
#endif
}

SdlGlesWindow::~SdlGlesWindow()
{
#if OPENHD_GLIDE_HAS_SDL2
    if (context_ != nullptr) {
        SDL_GL_DeleteContext(context_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
#endif
}

bool SdlGlesWindow::create(const char* title, std::uint32_t width, std::uint32_t height)
{
    return create(title, WindowPlacement {
        .width = width,
        .height = height,
    });
}

bool SdlGlesWindow::create(const char* title, WindowPlacement placement)
{
#if OPENHD_GLIDE_HAS_SDL2 && OPENHD_GLIDE_HAS_GLESV2
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        last_error_ = SDL_GetError();
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
    if (placement.borderless) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    window_ = SDL_CreateWindow(
        title,
        placement.positioned ? placement.x : SDL_WINDOWPOS_CENTERED,
        placement.positioned ? placement.y : SDL_WINDOWPOS_CENTERED,
        static_cast<int>(placement.width),
        static_cast<int>(placement.height),
        flags);

    if (window_ == nullptr) {
        last_error_ = SDL_GetError();
        return false;
    }

    if (placement.opacity < 1.0F) {
        SDL_SetWindowOpacity(window_, placement.opacity);
    }
    if (placement.always_on_top) {
        SDL_SetWindowAlwaysOnTop(window_, SDL_TRUE);
    }

    context_ = SDL_GL_CreateContext(window_);
    if (context_ == nullptr) {
        last_error_ = SDL_GetError();
        return false;
    }

    SDL_GL_SetSwapInterval(0);
    surface_ = flow::SurfaceSize {
        .width = placement.width,
        .height = placement.height,
    };
    return true;
#else
    (void)title;
    (void)placement;
    last_error_ = "SDL2 and OpenGL ES 2.0 are required for preview windows";
    return false;
#endif
}

bool SdlGlesWindow::poll()
{
#if OPENHD_GLIDE_HAS_SDL2
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            surface_.width = static_cast<std::uint32_t>(event.window.data1);
            surface_.height = static_cast<std::uint32_t>(event.window.data2);
        }
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            click_x_ = event.button.x;
            click_y_ = event.button.y;
            has_click_ = true;
        }
    }
#endif

    return true;
}

bool SdlGlesWindow::consume_click(int& x, int& y)
{
    if (!has_click_) {
        return false;
    }

    x = click_x_;
    y = click_y_;
    has_click_ = false;
    return true;
}

void SdlGlesWindow::swap()
{
#if OPENHD_GLIDE_HAS_SDL2
    if (window_ != nullptr) {
        SDL_GL_SwapWindow(window_);
    }
#endif
}

flow::SurfaceSize SdlGlesWindow::surface_size() const
{
    return surface_;
}

const std::string& SdlGlesWindow::last_error() const
{
    return last_error_;
}

} // namespace glide::dev
