#include "render/display.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace d2k::render {

Display::Display(SDL_Renderer* renderer, Aspect aspect) : renderer_(renderer), aspect_(aspect) {
    screen_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET,
                                kWidth, kHeight);
    if (!screen_) throw std::runtime_error(std::string("screen texture: ") + SDL_GetError());
    SDL_SetTextureScaleMode(screen_, SDL_SCALEMODE_NEAREST);
}

Display::~Display() {
    if (upscaled_) SDL_DestroyTexture(upscaled_);
    if (screen_) SDL_DestroyTexture(screen_);
}

void Display::begin_frame() {
    SDL_SetRenderTarget(renderer_, screen_);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
}

SDL_FRect Display::dest_rect(int out_w, int out_h) const {
    if (aspect_ == Aspect::Square) {
        int k = std::max(1, std::min(out_w / kWidth, out_h / kHeight));
        float w = float(kWidth * k), h = float(kHeight * k);
        return {std::floor((out_w - w) / 2), std::floor((out_h - h) / 2), w, h};
    }
    // 4:3 frame, as large as fits.
    float h = std::min(float(out_h), float(out_w) * 3.0f / 4.0f);
    float w = std::round(h * 4.0f / 3.0f);
    h = std::round(h);
    return {std::floor((out_w - w) / 2), std::floor((out_h - h) / 2), w, h};
}

void Display::end_frame() {
    SDL_SetRenderTarget(renderer_, nullptr);
    int out_w = 0, out_h = 0;
    SDL_GetRenderOutputSize(renderer_, &out_w, &out_h);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_FRect dst = dest_rect(out_w, out_h);
    confine_mouse(dst);

    if (aspect_ == Aspect::Square) {
        SDL_RenderTexture(renderer_, screen_, nullptr, &dst);
    } else {
        // Sharp bilinear: nearest to the smallest integer multiple at least
        // as large as the target, then linear down to the exact size.
        int k = std::max(1, int(std::ceil(std::max(dst.w / kWidth, dst.h / kHeight))));
        if (k != upscale_) {
            if (upscaled_) SDL_DestroyTexture(upscaled_);
            upscaled_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET,
                                          kWidth * k, kHeight * k);
            SDL_SetTextureScaleMode(upscaled_, SDL_SCALEMODE_LINEAR);
            upscale_ = k;
        }
        SDL_SetRenderTarget(renderer_, upscaled_);
        SDL_RenderTexture(renderer_, screen_, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_RenderTexture(renderer_, upscaled_, nullptr, &dst);
    }
    SDL_RenderPresent(renderer_);
}

void Display::present_picture(SDL_Texture* picture) {
    SDL_SetRenderTarget(renderer_, nullptr);
    int out_w = 0, out_h = 0;
    SDL_GetRenderOutputSize(renderer_, &out_w, &out_h);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_FRect dst = dest_rect(out_w, out_h);
    confine_mouse(dst);
    if (picture) SDL_RenderTexture(renderer_, picture, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

void Display::confine_mouse(const SDL_FRect& dst) {
    // QoL (settings mouse_grab): the original ran full screen, so the mouse
    // could not leave the 640x400 picture. With the window grab on, keep it
    // inside the drawn frame, not the black borders, so the edge scroll
    // stops as soon as the mouse moves back.
    SDL_Window* window = SDL_GetRenderWindow(renderer_);
    if (!window) return;
    SDL_Rect r{};
    if (SDL_GetWindowMouseGrab(window)) {
        const float density = SDL_GetWindowPixelDensity(window);
        const float d = density > 0 ? density : 1.0f;
        r = {int(std::ceil(dst.x / d)), int(std::ceil(dst.y / d)), int(std::floor(dst.w / d)),
             int(std::floor(dst.h / d))};
    }
    if (r.x == mouse_rect_.x && r.y == mouse_rect_.y && r.w == mouse_rect_.w && r.h == mouse_rect_.h) return;
    mouse_rect_ = r;
    SDL_SetWindowMouseRect(window, r.w > 0 && r.h > 0 ? &r : nullptr);
}

SDL_FPoint Display::window_to_screen(float x, float y) const {
    SDL_Window* window = SDL_GetRenderWindow(renderer_);
    float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    int out_w = 0, out_h = 0;
    SDL_GetRenderOutputSize(renderer_, &out_w, &out_h);
    SDL_FRect dst = dest_rect(out_w, out_h);
    return {(x * density - dst.x) * kWidth / dst.w, (y * density - dst.y) * kHeight / dst.h};
}

} // namespace d2k::render
