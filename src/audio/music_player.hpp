#pragma once
// Streams MUSIC\*.AUD like the 1.06 Miles-based player (FUN_0046fd90,
// FUN_0046ff30): restarts from the beginning on play(), loops forever, and
// queues audio in 0x8000-byte buffers. docs/re/04-menu-flow.md#menu-music

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "formats/aud.hpp"
#include "platform/game_data.hpp"

struct SDL_AudioStream;

namespace d2k::audio {

class MusicPlayer {
public:
    explicit MusicPlayer(const platform::GameData& gd);
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    // Opens MUSIC\<file> and plays it from the start; false if missing.
    bool play(const std::string& file);
    void stop();
    void pause(bool paused);

    // Miles sample volume, 0..127, as Miles' DirectSound gain
    // (audio::volume_gain, docs/re/59 §7).
    void set_volume(int volume);

    // FUN_0046ff30: hands Miles (two buffers) the next of three primed app
    // buffers when one is free, then refills that app buffer. Call once per
    // loop pass.
    void service();

    // True once after a refill found the end of the file (FUN_0046ff30's
    // empty read; the exe then sets 0x795600 in campaign games).
    bool take_ended() {
        const bool e = ended_;
        ended_ = false;
        return e;
    }

private:
    const platform::GameData& gd_;
    SDL_AudioStream* stream_ = nullptr;
    int volume_ = 0x40; // FUN_0046fd90 starts at 0x40
    std::unique_ptr<formats::AudStream> aud_;
    static constexpr int kAppBuffers = 3;                 // FUN_0046fd90's 0x18000 bytes
    std::array<std::vector<int16_t>, kAppBuffers> app_{}; // the next chunks, read ahead
    int next_ = 0;                                        // +0x1a8: the buffer Miles gets next
    bool ended_ = false;
};

} // namespace d2k::audio
