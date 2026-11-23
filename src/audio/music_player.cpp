#include "audio/music_player.hpp"

#include <SDL3/SDL.h>

#include <vector>

#include "audio/channels.hpp"
#include "formats/file_io.hpp"

namespace d2k::audio {

namespace {
constexpr size_t kBufferBytes = 0x8000; // per buffer (app and Miles)
} // namespace

MusicPlayer::MusicPlayer(const platform::GameData& gd) : gd_(gd) {}

MusicPlayer::~MusicPlayer() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool MusicPlayer::play(const std::string& file) {
    stop();
    auto path = gd_.find("MUSIC/" + file);
    if (!path) {
        SDL_Log("music %s not found", file.c_str());
        return false;
    }
    aud_ = std::make_unique<formats::AudStream>(formats::read_file(*path));
    ended_ = false;
    if (!stream_) {
        // The exe plays every track at 22050 Hz (0x5622), 16-bit mono.
        SDL_AudioSpec spec{SDL_AUDIO_S16LE, 1, 22050};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) {
            SDL_Log("music: no audio device: %s", SDL_GetError());
            return false;
        }
        set_volume(volume_);
    }
    // FUN_0046fd90: AIL_init_sample drops what the sample had queued (MSS32
    // 0x2000e340 releases its DirectSound buffer), then the three app
    // buffers are primed; Miles gets the first one at the next refill.
    SDL_ClearAudioStream(stream_);
    for (auto& b : app_) {
        b.assign(kBufferBytes / 2, 0);
        aud_->fill(b);
    }
    next_ = 0;
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void MusicPlayer::stop() {
    aud_.reset();
    if (stream_) SDL_ClearAudioStream(stream_);
}

void MusicPlayer::pause(bool paused) {
    if (!stream_) return;
    if (paused) {
        SDL_PauseAudioStreamDevice(stream_);
    } else {
        SDL_ResumeAudioStreamDevice(stream_);
    }
}

void MusicPlayer::set_volume(int volume) {
    volume_ = volume;
    if (stream_) SDL_SetAudioStreamGain(stream_, volume_gain(volume));
}

void MusicPlayer::service() {
    // FUN_0046ff30: when one of Miles' two sample buffers is free (at most
    // one buffer still pending), hand it app buffer `next_` (the whole
    // 0x8000 bytes, zero-filled tail included) and read the next chunk into
    // that app buffer; an empty read seeks back to the start and reads
    // again (`ended_`). One buffer per call, as the exe per loop pass.
    if (!stream_ || !aud_) return;
    if (SDL_GetAudioStreamQueued(stream_) > int(kBufferBytes)) return; // AIL_sample_buffer_ready = -1
    auto& b = app_[size_t(next_)];
    SDL_PutAudioStreamData(stream_, b.data(), int(kBufferBytes));
    bool wrapped = false;
    aud_->fill_looping(b, &wrapped);
    ended_ |= wrapped;
    next_ = (next_ + 1) % kAppBuffers;
}

} // namespace d2k::audio
