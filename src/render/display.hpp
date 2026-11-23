#pragma once
// The original's 640x400 screen and how it is shown in the window.
// Everything draws into the 640x400 screen texture between begin_frame()
// and end_frame(); end_frame() scales it to the window and presents.

#include <SDL3/SDL.h>

namespace d2k::render {

enum class Aspect {
    // As a 1998 CRT showed 640x400: stretched to 4:3 (pixels 1.2x taller).
    // Scaled "sharp bilinear": integer nearest upscale, then a smooth
    // downscale to the final size, so edges stay crisp at any window size.
    Crt,
    // Square pixels (16:10), integer scaling only.
    Square,
};

class Display {
public:
    static constexpr int kWidth = 640;  // screen size globals 0x4eb028 / 0x4eb02c
    static constexpr int kHeight = 400;

    Display(SDL_Renderer* renderer, Aspect aspect);
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    SDL_Renderer* renderer() const { return renderer_; }
    Aspect aspect() const { return aspect_; }
    void set_aspect(Aspect a) { aspect_ = a; }

    // Targets the 640x400 screen and clears it to black.
    void begin_frame();
    // Scales the screen into the window (black borders) and presents.
    void end_frame();
    // Presents `picture` (any size) in the place the 640x400 screen would
    // take, smoothly scaled; for upscaled movies (enhanced_movies option).
    void present_picture(SDL_Texture* picture);

    // Window coordinates (as in SDL mouse events) to screen pixels; may lie
    // outside 0..639 / 0..399 over the borders.
    SDL_FPoint window_to_screen(float x, float y) const;

private:
    SDL_FRect dest_rect(int out_w, int out_h) const; // in output pixels
    void confine_mouse(const SDL_FRect& dst);         // dst in output pixels

    SDL_Rect mouse_rect_{}; // the last window mouse rect (0 size = none)

    SDL_Renderer* renderer_;
    Aspect aspect_;
    SDL_Texture* screen_ = nullptr;
    SDL_Texture* upscaled_ = nullptr; // Crt only
    int upscale_ = 0;
};

} // namespace d2k::render
