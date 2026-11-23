#include "video/movie_player.hpp"

#include <SDL3/SDL.h>

#include <deque>
#include <stdexcept>
#include <vector>

#include "formats/file_io.hpp"
#include "render/display.hpp"
#include "formats/vqa.hpp"
#include "video/enhanced_video.hpp"

namespace d2k::video {

namespace {

// Frames decoded ahead of display; keeps ~0.5 s of audio queued at 15 fps.
constexpr size_t kDecodeAhead = 8;

// Every MOVIE block in UI_DATA\*.uil places movies at "0, 0" size
// "640, 400", the whole 640x400 screen.
constexpr float kMovieY = 0.0f;

// Display option enhanced_movies (docs/enhanced-movies.md); empty = off.
std::filesystem::path g_enhanced_dir;

// A streaming NV12 texture for the upscaled pictures (BT.709 limited range,
// as docs/enhanced-movies.md encodes them), smoothly scaled.
SDL_Texture* create_picture_texture(SDL_Renderer* renderer, int w, int h) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_NV12);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
    SDL_Texture* tex = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    return tex;
}

struct DecodedFrame {
    int index;
    std::vector<uint16_t> pixels;
};

} // namespace

void set_enhanced_movies_dir(const std::filesystem::path& dir) { g_enhanced_dir = dir; }

MovieResult play_movie(render::Display& display, const std::filesystem::path& path) {
    SDL_Renderer* renderer = display.renderer();
    formats::VqaDecoder vqa(formats::read_file(path));
    const auto& h = vqa.header();

    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB1555,
                                         SDL_TEXTUREACCESS_STREAMING, h.width, h.height);
    if (!tex) throw std::runtime_error(std::string("texture: ") + SDL_GetError());
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    // Upscaled pictures, if the option is on and this movie has a file.
    std::unique_ptr<EnhancedVideo> enhanced;
    SDL_Texture* picture = nullptr;
    bool has_picture = false;
    if (!g_enhanced_dir.empty()) {
        enhanced = EnhancedVideo::open(enhanced_path_for(g_enhanced_dir, path));
        if (enhanced) picture = create_picture_texture(renderer, enhanced->width(), enhanced->height());
        if (enhanced && !picture) {
            SDL_Log("enhanced movie: texture failed (%s); playing the original pictures", SDL_GetError());
            enhanced.reset();
        }
        if (enhanced) SDL_Log("enhanced movie %dx%d", enhanced->width(), enhanced->height());
    }

    SDL_AudioStream* audio = nullptr;
    if (h.has_audio()) {
        SDL_AudioSpec spec{SDL_AUDIO_S16LE, 1, h.audio_rate};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!audio) SDL_Log("no audio device (%s); playing silently", SDL_GetError());
    }

    std::deque<DecodedFrame> queue;
    bool eof = false;
    uint64_t samples_pushed = 0;
    auto decode_more = [&] {
        while (!eof && queue.size() < kDecodeAhead) {
            if (!vqa.next_frame()) {
                eof = true;
                // Without a flush SDL keeps the last few samples back, the
                // queue never empties and the last frame would hang.
                if (audio) SDL_FlushAudioStream(audio);
                break;
            }
            auto pcm = vqa.take_audio();
            if (audio && !pcm.empty()) {
                SDL_PutAudioStreamData(audio, pcm.data(), int(pcm.size() * sizeof(int16_t)));
            }
            samples_pushed += pcm.size();
            auto f = vqa.frame();
            queue.push_back({vqa.frame_index(), {f.begin(), f.end()}});
        }
    };

    decode_more();
    const uint64_t start_ns = SDL_GetTicksNS();
    if (audio) SDL_ResumeAudioStreamDevice(audio);

    MovieResult result = MovieResult::Finished;
    int shown = -1;
    while (true) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                result = MovieResult::Quit;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE && !ev.key.repeat) {
                // Only Esc skips: the play loop FUN_00406d10 polls the key
                // array entry 0x7983ab (0x798390 + VK_ESCAPE), which the
                // window procedure sets on a new key-down (0x4a33aa, not on
                // auto-repeat) and FUN_0048d8f0 clears before playing. Mouse
                // buttons do nothing.
                result = MovieResult::Skipped;
            }
        }
        if (result != MovieResult::Finished) break;

        // Playback clock: samples actually played, or wall time if silent.
        double seconds;
        if (audio) {
            int queued_bytes = SDL_GetAudioStreamQueued(audio);
            uint64_t played = samples_pushed - uint64_t(queued_bytes) / sizeof(int16_t);
            seconds = double(played) / h.audio_rate;
        } else {
            seconds = double(SDL_GetTicksNS() - start_ns) / 1e9;
        }
        int due = int(seconds * h.frame_rate);

        while (queue.size() > 1 && queue[1].index <= due) queue.pop_front();
        if (!queue.empty() && queue.front().index != shown && queue.front().index <= due) {
            SDL_UpdateTexture(tex, nullptr, queue.front().pixels.data(), h.width * 2);
            shown = queue.front().index;
        }
        decode_more();

        if (enhanced) {
            if (auto f = enhanced->take_due(seconds)) {
                const int w = enhanced->width(), hh = enhanced->height();
                SDL_UpdateNVTexture(picture, nullptr, f->nv12.data(), w, f->nv12.data() + size_t(w) * hh, w);
                has_picture = true;
            }
        }
        if (has_picture) {
            display.present_picture(picture);
        } else {
            display.begin_frame();
            SDL_FRect dst{0, kMovieY, float(h.width), float(h.height)};
            if (shown >= 0) SDL_RenderTexture(renderer, tex, nullptr, &dst);
            display.end_frame();
        }

        bool all_shown = eof && (queue.empty() || (queue.size() == 1 && queue.front().index == shown));
        if (all_shown && (!audio || SDL_GetAudioStreamQueued(audio) == 0)) break;
        SDL_Delay(1);
    }

    if (audio) SDL_DestroyAudioStream(audio);
    enhanced.reset();
    if (picture) SDL_DestroyTexture(picture);
    SDL_DestroyTexture(tex);
    return result;
}

} // namespace d2k::video
