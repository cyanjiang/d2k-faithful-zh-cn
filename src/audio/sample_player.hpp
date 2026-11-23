#pragma once
// Plays one sound sample at a time (UI speech, e.g. the campaign map's
// "C_A1a" lines). Samples are named in UI_DATA\samples.uib and stored as
// Data\GAMESFX\<file>.AUD.

#include <string>
#include <vector>

#include "formats/sound_rs.hpp"
#include "platform/game_data.hpp"

struct SDL_AudioStream;

namespace d2k::audio {

class SamplePlayer {
public:
    explicit SamplePlayer(const platform::GameData& gd);
    ~SamplePlayer();
    SamplePlayer(const SamplePlayer&) = delete;
    SamplePlayer& operator=(const SamplePlayer&) = delete;

    // Plays Data\GAMESFX\<file>.AUD, or <FILE>.WAV from SOUND.RS (as the
    // exe's sample loader: AUD first), replacing what is playing, at the
    // set_volume() level and centred.
    bool play_file(const std::string& file) { return play_file(file, volume_, 0x40); }
    // The same at Miles volume 0..127 and pan 0..127 (64 = centre), as
    // FUN_0046f330 sets them on the sample when it starts (docs/re/59).
    bool play_file(const std::string& file, int volume, int pan);
    void stop();
    bool playing() const;
    // Data\GAMESFX\<file>.AUD exists (speech streams it, FUN_0046faa0).
    bool has_aud(const std::string& file) const;
    // <FILE>.WAV is in SOUND.RS (the sample cache's source, FUN_0046ecc0).
    bool in_sound_rs(const std::string& file);
    // Miles sample volume 0..127 (the exe passes gSFXVolume 0..100 in the
    // game, 100 outside it), as Miles' DirectSound gain (docs/re/59 §7).
    void set_volume(int volume);

private:
    bool open_stream(int rate);
    void load_rs();

    const platform::GameData& gd_;
    SDL_AudioStream* stream_ = nullptr;
    int stream_rate_ = 0;
    int volume_ = 100; // outside the game the exe passes 100
    std::vector<uint8_t> rs_data_;               // SOUND.RS, loaded on first use
    std::vector<formats::RsEntry> rs_entries_;
};

} // namespace d2k::audio
