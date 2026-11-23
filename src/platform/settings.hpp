#pragma once
// Our own persistent settings (we never write the original dune2000.cfg).
// Stored as simple key=value lines.

#include <filesystem>
#include <string>

namespace d2k::platform {

struct Settings {
    // Mirrors byte 0 of the original dune2000.cfg (1.06 0x004e7acc): the full
    // intro plays on the first launch only. RE: docs/re/01-boot-sequence.md
    bool play_full_intro = true;

    // Display (not gameplay): "crt" = 640x400 stretched to 4:3 as on a 1998
    // monitor, "square" = square pixels. User choice pending (2026-10-03).
    std::string aspect = "crt";

    // Optional localisation pack name (i18n/<lang>/). "en" = vanilla English.
    // Display only: no game rules, hashes or network data are affected.
    std::string lang = "en";

    // The options "game speed" slider, 0..100 (0x4e3b18, saved in
    // dune2000.cfg by the original). 75 is the exe's default.
    int game_speed = 75;
    // Run fullscreen (the original was always fullscreen). Display only.
    bool fullscreen = false;
    // The fullscreen resolution. 0x0 = the desktop's own mode; otherwise
    // the closest mode the monitor supports (F11 keeps these values).
    int display_width = 0;
    int display_height = 0;
    // The other in-game OPTIONS sliders (docs/re/20 section 2.2):
    // gScrollSize 0x4eb030 (4..40, px per loop), gCDVolume 0x4e5fc8 and
    // gSFXVolume 0x4e5fc4 (0..100).
    int scroll_speed = 20;
    int music_volume = 100;
    int sfx_volume = 100;
    // Keep the mouse inside the window (not gameplay: the original ran
    // fullscreen, where the cursor could not leave the screen).
    bool mouse_grab = true;
    // Upscaled movie pictures the player made from their own movies
    // (docs/enhanced-movies.md). Display only, off by default.
    bool enhanced_movies = false;
    // The skirmish settings (gNet* globals, saved in dune2000.cfg by the
    // original; docs/re/51 section 4.1). Values as the menu globals hold
    // them; the player name is a string. Defaults from FUN_00470710.
    int net_unit_count = 6, net_tech_level = 7, net_credits = 4000, net_ai_players = 0;
    int net_crates = 0, net_worms = 0, net_side = 0, net_colour = 0;
    std::string net_name;
    // Network games (ours, docs/design/multiplayer.md 6.3): the UDP port this
    // game listens on, and the addresses typed in IPXLOBBY's address box
    // (comma-separated, newest first), probed again on every visit.
    int net_port = 1998;
    std::string net_addresses;

    static Settings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

} // namespace d2k::platform
