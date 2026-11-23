#include "audio/sample_player.hpp"

#include <SDL3/SDL.h>

#include <cctype>

#include "audio/channels.hpp"
#include "formats/aud.hpp"
#include "formats/file_io.hpp"

namespace d2k::audio {

SampleCache& sample_cache() {
    static SampleCache cache;
    return cache;
}

SamplePlayer::SamplePlayer(const platform::GameData& gd) : gd_(gd) {}

SamplePlayer::~SamplePlayer() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool SamplePlayer::open_stream(int rate) {
    if (stream_ && stream_rate_ == rate) return true;
    if (stream_) SDL_DestroyAudioStream(stream_);
    SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, rate}; // stereo: the pan is applied per sample
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    stream_rate_ = stream_ ? rate : 0;
    if (!stream_) SDL_Log("samples: no audio device: %s", SDL_GetError());
    return stream_ != nullptr;
}

void SamplePlayer::set_volume(int volume) {
    volume_ = volume;
    if (stream_) SDL_SetAudioStreamGain(stream_, volume_gain(volume_));
}

void SamplePlayer::stop() {
    if (stream_) SDL_ClearAudioStream(stream_);
}

bool SamplePlayer::play_file(const std::string& file, int volume, int pan) {
    std::vector<int16_t> samples;
    int rate = 0;
    if (auto path = gd_.find("Data/GAMESFX/" + file + ".AUD")) {
        auto sound = formats::decode_aud(formats::read_file(*path));
        samples = std::move(sound.samples);
        rate = sound.header.sample_rate;
    } else {
        load_rs();
        std::string want = file + ".WAV";
        for (char& c : want) c = char(std::toupper(static_cast<unsigned char>(c)));
        for (const auto& e : rs_entries_) {
            std::string name = e.name;
            for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (name != want) continue;
            auto pcm = formats::decode_wav(std::span(rs_data_).subspan(e.offset, e.size));
            samples = std::move(pcm.samples);
            rate = int(pcm.sample_rate);
            break;
        }
    }
    if (rate == 0) {
        SDL_Log("sample %s not found", file.c_str());
        return false;
    }
    if (!open_stream(rate)) return false;
    SDL_ClearAudioStream(stream_);
    SDL_SetAudioStreamGain(stream_, volume_gain(volume)); // Miles' DirectSound law, docs/re/59 §7
    const auto [gl, gr] = pan_gains(pan);
    std::vector<int16_t> stereo(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        stereo[2 * i] = int16_t(float(samples[i]) * gl);
        stereo[2 * i + 1] = int16_t(float(samples[i]) * gr);
    }
    SDL_PutAudioStreamData(stream_, stereo.data(), int(stereo.size() * sizeof(int16_t)));
    SDL_FlushAudioStream(stream_);
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void SamplePlayer::load_rs() {
    if (!rs_entries_.empty()) return;
    if (auto rs = gd_.find("Data/GAMESFX/SOUND.RS")) {
        rs_data_ = formats::read_file(*rs);
        rs_entries_ = formats::parse_sound_rs(rs_data_);
    }
}

bool SamplePlayer::has_aud(const std::string& file) const {
    return gd_.find("Data/GAMESFX/" + file + ".AUD").has_value();
}

bool SamplePlayer::in_sound_rs(const std::string& file) {
    load_rs();
    std::string want = file + ".WAV";
    for (char& c : want) c = char(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& e : rs_entries_) {
        std::string name = e.name;
        for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (name == want) return true;
    }
    return false;
}

bool SamplePlayer::playing() const { return stream_ && SDL_GetAudioStreamQueued(stream_) > 0; }

} // namespace d2k::audio
