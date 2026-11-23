#pragma once
// The exe's in-game sound rules, without any audio device: the 6 Miles
// channels (0 = speech, 1-5 = effects), the channel pick, the positional
// pan / volume and the two voice rings. docs/re/59-sound-channels.md

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace d2k::audio {

constexpr int kChannels = 6; // 0x795608 + 4 + ch * 0x24, ch < 6
constexpr int kPanCentre = 0x40;

// One channel record: AIL_sample_status & 4, +0x18 volume, +0x1c sample.
struct Channel {
    bool playing = false;
    int volume = 0;
    std::string sample;
};
using Channels = std::array<Channel, kChannels>;

// FUN_0046f210: the first idle channel of first..last; when all are busy,
// the first quietest one if mode 0, or mode 2 and it is strictly quieter
// than `volume`; mode 1 never cuts a sound off. -1 = not played.
inline int pick_channel(const Channels& ch, int first, int last, int mode, int volume) {
    if (last < first) last = first;
    if (first < 0 || first >= kChannels || last >= kChannels) return -1;
    int best = -1;
    int best_vol = 0xff;
    for (int c = first; c <= last; ++c) {
        if (!ch[size_t(c)].playing) return c;
        if (ch[size_t(c)].volume < best_vol) {
            best_vol = ch[size_t(c)].volume;
            best = c;
        }
    }
    if (mode != 1 && best >= 0 && (best_vol < volume || mode == 0)) return best;
    return -1;
}

// FUN_0046f080: the sample manager's 16-slot cache of SOUND.RS samples,
// keyed by samples.uib id, which every channel play (FUN_0046f170) goes
// through; and the slot each channel last played (record +0x20, -1
// from FUN_0046eda0). A hit does not refresh the slot's stamp. When no slot
// is free, every playing channel's slot is stamped `now` (gGameTicks
// 0x5173f4); a playing channel whose slot index is invalid (a speech
// stream on channel 0 that never played a cached sample) fails the whole
// load. Then the first slot with the lowest stamp below `now` is
// reloaded; none: -1. `in_rs` = the sample's file is in SOUND.RS
// (FUN_0046ecc0; every 1.06 entry is under the 0xffff size limit).
// docs/re/59 section 1.
struct SampleCache {
    struct Slot {
        int id = -1;
        uint32_t stamp = 0;
    };
    std::array<Slot, 16> slot{};
    std::array<int, kChannels> channel_slot{-1, -1, -1, -1, -1, -1};
    uint32_t now = 0; // gGameTicks as last seen (kept in the front end, as the exe's global)

    void reset() { *this = SampleCache{.now = now}; } // FUN_0046eda0 / FUN_0046eee0 (a new manager)

    int load(int id, bool in_rs, const std::array<bool, kChannels>& playing) {
        for (size_t i = 0; i < slot.size(); ++i) {
            if (slot[i].id == id) return int(i);
        }
        for (size_t i = 0; i < slot.size(); ++i) {
            if (slot[i].id >= 0) continue;
            if (!in_rs) return -1;
            slot[i] = {id, now};
            return int(i);
        }
        for (size_t c = 0; c < size_t(kChannels); ++c) {
            if (!playing[c]) continue;
            const int s = channel_slot[c];
            if (s < 0 || s > 15) return -1;
            slot[size_t(s)].stamp = now;
        }
        int best = -1;
        uint32_t low = now;
        for (size_t i = 0; i < slot.size(); ++i) {
            if (slot[i].stamp < low) {
                low = slot[i].stamp;
                best = int(i);
            }
        }
        if (best < 0 || !in_rs) return -1;
        slot[size_t(best)] = {id, now};
        return best;
    }
};

// The one sample manager of the process (0x795608): it outlives a game
// and is only replaced at start-up and after each movie.
SampleCache& sample_cache();

// FUN_0046f4f0: any channel playing this sample.
inline bool sample_playing(const Channels& ch, const std::string& sample) {
    return std::any_of(ch.begin(), ch.end(), [&](const Channel& c) { return c.playing && c.sample == sample; });
}

struct PanVolume {
    int pan = kPanCentre; // Miles: 0 left, 64 centre, 127 right
    int volume = 0;       // Miles 0..127
};

// FUN_0046f540's pan and volume for a sound at cell (cx, cy), the view at
// pixel (scroll_x, scroll_y) of size view_w x view_h, a map map_w cells
// wide, gSFXVolume sfx (0..100). The exe's unsigned arithmetic.
inline PanVolume positional(int cx, int cy, int scroll_x, int scroll_y, int view_w, int view_h, int map_w,
                            int sfx) {
    const uint32_t x = uint32_t(cx) & 0xff, y = uint32_t(cy) & 0xff;
    const int hw = (view_w / 2) / 32, hh = (view_h / 2) / 32; // 7, 5
    const int centre_x = scroll_x / 32 + hw, centre_y = scroll_y / 32 + hh;
    const uint32_t dx = uint32_t(std::abs(centre_x - int(x)));
    const uint32_t dy = uint32_t(std::abs(centre_y - int(y)));
    const uint32_t w = uint32_t(map_w);
    PanVolume r;
    uint32_t pan = kPanCentre;
    if (int(dx) > hw && w != 0) {
        pan = 0x40 - dx * 0x40 / w;
        if (pan < 0x10) pan = 0x10;
        if (x > uint32_t(centre_x)) pan = 0x80 - pan;
    }
    uint32_t vol = 50;
    const uint32_t d = std::max(dx, dy);
    if (int(dx) > hw || int(dy) > hh) {
        if (map_w <= int(d) || w == 0) {
            vol = 5;
        } else {
            vol = 35 - d * 35 / w;
            if (vol < 5) vol = 5;
        }
    }
    r.pan = int(pan);
    r.volume = int(vol * uint32_t(sfx) / 100);
    return r;
}

// Miles' volume and pan law on the game's DirectSound path (docs/re/59 §7):
// every sample is its own DirectSound buffer, and MSS32.DLL turns volume
// and pan into DirectSound's hundredths of a decibel.

// MSS32.DLL FUN_2000de70(0, hi, x): 0 -> -10000 (silent), hi -> 0, else
// trunc(-2000 * log10(hi / x)). The x87 code rounds the quotient to double
// and truncates (__ftol); every input 1..hi is at least 0.0024 from an
// integer, so double gives the same results.
inline int miles_ds_attenuation(int hi, int x) {
    if (x == 0) return -10000;
    if (x == hi) return 0;
    return int(-2000.0 * std::log10(double(hi) / double(x)));
}

// FUN_2000de00: the sample volume (clamped to 0..127) times the master
// volume (127, the game never changes it) / 127, as DSBVOLUME.
inline int miles_volume_db(int volume) {
    volume = std::clamp(volume, 0, 127);
    return miles_ds_attenuation(127, 127 * volume / 127);
}

// FUN_2000def0: the pan as DSBPAN (negative = right side quieter). Pan 63
// and 64 are both centre; the far side falls as pan / 63 or (127 - pan) / 63.
// The DLL does not clamp; the game passes 0x10..0x70 only.
inline int miles_pan_db(int pan) {
    pan = std::clamp(pan, 0, 127);
    if (pan > 0x40) return -miles_ds_attenuation(0x3f, 0x7f - pan);
    if (pan < 0x40) return miles_ds_attenuation(0x3f, pan);
    return 0;
}

// DirectSound's amplitude for hundredths of a decibel (-10000 = silent).
inline float ds_db_gain(int hundredths) {
    if (hundredths <= -10000) return 0.0f;
    return float(std::pow(10.0, double(hundredths) / 2000.0));
}

// Linear gain of a Miles sample volume 0..127 (about volume / 127).
inline float volume_gain(int volume) { return ds_db_gain(miles_volume_db(volume)); }

// Left / right gain for a Miles pan on the stereo device: DSBPAN
// attenuates only the far side; the near side stays at full gain.
inline std::pair<float, float> pan_gains(int pan) {
    const int p = miles_pan_db(pan);
    const float left = p > 0 ? ds_db_gain(-p) : 1.0f;
    const float right = p < 0 ? ds_db_gain(p) : 1.0f;
    return {left, right};
}

// The speech queue (FUN_0046f820 after its repeat limit, FUN_0046f770,
// FUN_0046f7d0): two rings of 5 slots, prio 0 and 1; a ring is full at 4.
class VoiceQueue {
public:
    // `speech` = the sample channel 0 is playing, "" when it is idle.
    // True when queued.
    bool push(const std::string& name, bool mode0, int prio, const std::string& speech) {
        prio = std::clamp(prio, 0, 1); // the exe logs "Error with SFX priority" above 1
        if (mode0) {
            if (!empty()) return false; // FUN_0046f6e0
            if (!speech.empty() && (name == kMoneyTick || speech == kMoneyTick)) return false;
        } else {
            if (!speech.empty() && speech == name) return false;
            if (contains(prio, name)) return false; // FUN_0046f700: this ring only
        }
        Ring& r = rings_[size_t(prio)];
        const int next = (r.tail + 1) % 5;
        if (next == r.head) return false; // full: dropped
        r.slot[size_t(r.tail)] = name;
        r.tail = next;
        return true;
    }
    // FUN_0046f7d0: ring 1 first, then ring 0.
    std::optional<std::string> pop() {
        for (int p = 1; p >= 0; --p) {
            Ring& r = rings_[size_t(p)];
            if (r.head == r.tail) continue;
            std::string s = std::move(r.slot[size_t(r.head)]);
            r.head = (r.head + 1) % 5;
            return s;
        }
        return std::nullopt;
    }
    bool empty() const { return rings_[0].head == rings_[0].tail && rings_[1].head == rings_[1].tail; }
    void clear() { rings_ = {}; }

private:
    static constexpr const char* kMoneyTick = "S_MONEYTICK";
    struct Ring {
        std::array<std::string, 5> slot;
        int head = 0, tail = 0; // 0x7955b0 / 0x795580
    };
    bool contains(int prio, const std::string& name) const {
        const Ring& r = rings_[size_t(prio)];
        for (int i = r.head; i != r.tail; i = (i + 1) % 5) {
            if (r.slot[size_t(i)] == name) return true;
        }
        return false;
    }
    std::array<Ring, 2> rings_{};
};

} // namespace d2k::audio
