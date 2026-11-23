// d2k-tool: command-line access to original Dune 2000 data.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "formats/aud.hpp"
#include "formats/file_io.hpp"
#include "formats/map.hpp"
#include "render/objects.hpp"
#include "render/terrain.hpp"
#include "sim/game_map.hpp"
#include "sim/objects.hpp"
#include "formats/palette.hpp"
#include "formats/r8.hpp"
#include "formats/sound_rs.hpp"
#include "formats/vqa.hpp"
#include "formats/string_table.hpp"
#include "platform/game_data.hpp"
#include "png_writer.hpp"
#include "campaign/campaign_table.hpp"
#include "campaign/regions.hpp"
#include "formats/pe_image.hpp"
#include "ui/assets.hpp"
#include "ui/globe.hpp"
#include "ui/menu_hooks.hpp"
#include "ui/render.hpp"
#include "ui/screen.hpp"
#include "i18n/i18n.hpp"
#include "i18n/utf8.hpp"

namespace {

using namespace d2k;

int usage() {
    std::fputs("usage:\n"
               "  d2k-tool check <gamedir>\n"
               "  d2k-tool info <file>\n"
               "  d2k-tool r8-info <file.R8|R16> [--list]\n"
               "  d2k-tool r8-sheet <file.R8|R16> <out.png> [--palette PALETTE.BIN]\n"
               "                    [--range FIRST:END] [--width PIXELS]\n"
               "  d2k-tool vqa-info <file.VQA>\n"
               "  d2k-tool vqa-frame <file.VQA> <frame> <out.png>\n"
               "  d2k-tool vqa-wav <file.VQA> <out.wav>\n"
               "  d2k-tool vqa-rgb <file.VQA> <out.rgb|->   (raw RGB24 frames)\n"
               "  d2k-tool aud-wav <file.AUD> <out.wav>\n"
               "  d2k-tool rs-list <SOUND.RS>\n"
               "  d2k-tool ui-render <gamedir> <MENU[+MENU...]> <out.png>\n"
               "  d2k-tool text-preview <gamedir> <TEXT.UIB key> <out.png>\n"
               "  d2k-tool globe <gamedir> <side 0-2> <mission 1-9> <out.png>\n"
               "  --lang CODE     load i18n/<CODE>/ for ui-render (offline preview)\n",
               stderr);
    return 2;
}

std::optional<std::string> option(const std::vector<std::string>& args, const std::string& name) {
    auto it = std::find(args.begin(), args.end(), name);
    if (it == args.end() || it + 1 == args.end()) return std::nullopt;
    return *(it + 1);
}

bool flag(const std::vector<std::string>& args, const std::string& name) {
    return std::find(args.begin(), args.end(), name) != args.end();
}

int cmd_check(const std::string& dir) {
    auto problems = platform::GameData::validate(dir);
    for (const auto& p : problems) {
        std::printf("problem: %s\n", p.c_str());
    }
    std::printf("%s\n", problems.empty() ? "OK: looks like Dune 2000 1.06" : "NOT OK");
    return problems.empty() ? 0 : 1;
}

int cmd_info(const std::string& file) {
    auto data = formats::read_file(file);
    std::printf("%s: %zu bytes\nheader:", file.c_str(), data.size());
    for (size_t i = 0; i < data.size() && i < 32; ++i) {
        std::printf(" %02x", data[i]);
    }
    std::printf("\n");
    return 0;
}

int cmd_r8_info(const std::string& file, bool list) {
    auto images = formats::parse_r8(formats::read_file(file));
    std::map<std::string, int> kinds;
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& im = images[i];
        std::string kind = im.empty() ? "empty"
                                      : "type" + std::to_string(im.type) + "/" +
                                            std::to_string(im.bits_per_pixel) + "bpp" +
                                            (im.palette ? "+pal" : "");
        kinds[kind]++;
        if (list && !im.empty()) {
            std::printf("%5zu  t%u %3ux%-3u off(%d,%d) frame(%u,%u) %ubpp%s\n", i, im.type,
                        im.width, im.height, im.offset_x, im.offset_y, im.frame_width,
                        im.frame_height, im.bits_per_pixel, im.palette ? " +pal" : "");
        }
    }
    std::printf("%s: %zu entries\n", file.c_str(), images.size());
    for (const auto& [k, n] : kinds) {
        std::printf("  %-18s %d\n", k.c_str(), n);
    }
    return 0;
}

// Lays out frames left-to-right in rows; index 0 of 8-bit images is drawn
// transparent. Palette choice per 8-bit image: its embedded palette, else
// (type 2) the most recent embedded palette, else the given VGA palette.
// TODO(verify): this palette selection mirrors our current hypothesis only.
int cmd_r8_sheet(const std::string& file, const std::string& out,
                 const std::vector<std::string>& args) {
    auto images = formats::parse_r8(formats::read_file(file));
    formats::Palette vga{};
    for (int i = 0; i < 256; ++i) {
        vga[i] = {uint8_t(i), uint8_t(i), uint8_t(i)}; // grayscale fallback
    }
    if (auto p = option(args, "--palette")) {
        vga = formats::parse_vga_palette(formats::read_file(*p));
    }
    size_t first = 0, end = images.size();
    if (auto r = option(args, "--range")) {
        auto colon = r->find(':');
        first = std::stoul(r->substr(0, colon));
        if (colon != std::string::npos && colon + 1 < r->size()) {
            end = std::min(end, size_t(std::stoul(r->substr(colon + 1))));
        }
    }
    uint32_t sheet_w = 1024;
    if (auto w = option(args, "--width")) sheet_w = uint32_t(std::stoul(*w));

    // Pass 1: layout. Palettes are tracked from the start of the file so
    // type-2 images inside a range still get the right palette.
    struct Placed { size_t index; uint32_t x, y; };
    std::vector<Placed> placed;
    uint32_t x = 0, y = 0, row_h = 0;
    constexpr uint32_t kGap = 2;
    for (size_t i = first; i < end; ++i) {
        const auto& im = images[i];
        if (im.empty()) continue;
        if (x > 0 && x + im.width > sheet_w) {
            x = 0;
            y += row_h + kGap;
            row_h = 0;
        }
        placed.push_back({i, x, y});
        x += im.width + kGap;
        row_h = std::max(row_h, im.height);
    }
    uint32_t sheet_h = std::max<uint32_t>(1, y + row_h);
    std::vector<uint8_t> rgba(size_t(sheet_w) * sheet_h * 4, 0);

    std::optional<std::array<uint16_t, 256>> last_pal;
    size_t next_place = 0;
    for (size_t i = 0; i < end && next_place < placed.size(); ++i) {
        const auto& im = images[i];
        if (im.palette) last_pal = im.palette;
        if (placed[next_place].index != i) continue;
        const auto& pl = placed[next_place++];
        for (uint32_t py = 0; py < im.height; ++py) {
            for (uint32_t px = 0; px < im.width; ++px) {
                size_t src = size_t(py) * im.width + px;
                formats::Rgb c;
                bool opaque = true;
                if (im.bits_per_pixel == 8) {
                    uint8_t idx = im.pixels[src];
                    opaque = idx != 0;
                    const auto* pal = im.palette ? &*im.palette
                                      : (im.type == 2 && last_pal) ? &*last_pal
                                                                   : nullptr;
                    c = pal ? formats::rgb565_to_rgb((*pal)[idx]) : vga[idx];
                } else {
                    uint16_t v = uint16_t(im.pixels[src * 2] | (im.pixels[src * 2 + 1] << 8));
                    c = formats::rgb555_to_rgb(v); // TODO(verify): transparency key
                }
                size_t dst = (size_t(pl.y + py) * sheet_w + pl.x + px) * 4;
                rgba[dst] = c.r;
                rgba[dst + 1] = c.g;
                rgba[dst + 2] = c.b;
                rgba[dst + 3] = opaque ? 255 : 0;
            }
        }
    }
    tool::write_png_rgba(out, sheet_w, sheet_h, rgba);
    std::printf("wrote %s (%ux%u, %zu frames)\n", out.c_str(), sheet_w, sheet_h, placed.size());
    return 0;
}

int cmd_vqa_info(const std::string& file) {
    formats::VqaDecoder vqa(formats::read_file(file));
    const auto& h = vqa.header();
    std::printf("%s: v%u flags=0x%x %ux%u %u frames @ %u fps, blocks %ux%u, audio %u Hz %uch %ubit\n",
                file.c_str(), h.version, h.flags, h.width, h.height, h.num_frames, h.frame_rate,
                h.block_width, h.block_height, h.audio_rate, h.audio_channels, h.audio_bits);
    size_t samples = 0;
    while (vqa.next_frame()) {
        samples += vqa.take_audio().size();
    }
    const auto& s = vqa.stats();
    std::printf("decoded %d frames (header says %u), %zu audio samples (%.2f s)\n",
                vqa.frame_index() + 1, h.num_frames, samples,
                h.audio_rate ? double(samples) / h.audio_rate : 0.0);
    std::printf("SN2J state checks: %zu, mismatches: %zu; frames with leftover vptr bytes: %zu\n",
                s.sn2j_checked, s.sn2j_mismatches, s.vptr_leftover_frames);
    std::printf("codebook trailing bytes: %zu\n", s.codebook_trailing_bytes);
    std::printf("vptr opcodes:");
    for (int i = 0; i < 8; ++i) std::printf(" %d:%zu", i, s.opcode_counts[i]);
    std::printf("\n");
    bool ok = vqa.frame_index() + 1 == h.num_frames && s.sn2j_mismatches == 0 &&
              s.vptr_leftover_frames == 0;
    return ok ? 0 : 1;
}

int cmd_vqa_frame(const std::string& file, int wanted, const std::string& out) {
    formats::VqaDecoder vqa(formats::read_file(file));
    while (vqa.frame_index() < wanted && vqa.next_frame()) {
    }
    if (vqa.frame_index() != wanted) {
        std::fprintf(stderr, "only %d frames\n", vqa.frame_index() + 1);
        return 1;
    }
    const auto& h = vqa.header();
    std::vector<uint8_t> rgba(size_t(h.width) * h.height * 4);
    auto px = vqa.frame();
    for (size_t i = 0; i < px.size(); ++i) {
        auto c = formats::rgb555_to_rgb(px[i]);
        rgba[i * 4] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = 255;
    }
    tool::write_png_rgba(out, h.width, h.height, rgba);
    std::printf("wrote %s\n", out.c_str());
    return 0;
}

// Every frame as raw RGB24, for an encoder (`-` = stdout), e.g.
// d2k-tool vqa-rgb X.VQA - | ffmpeg -f rawvideo -pix_fmt rgb24 -s 640x400 -r 15 -i - ...
// Used to make the source for upscaled movies (docs/enhanced-movies.md).
int cmd_vqa_rgb(const std::string& file, const std::string& out) {
    formats::VqaDecoder vqa(formats::read_file(file));
    std::FILE* f = out == "-" ? stdout : std::fopen(out.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
#ifdef _WIN32
    if (f == stdout) _setmode(_fileno(stdout), _O_BINARY);
#endif
    const auto& h = vqa.header();
    std::vector<uint8_t> rgb(size_t(h.width) * h.height * 3);
    int frames = 0;
    while (vqa.next_frame()) {
        auto px = vqa.frame();
        for (size_t i = 0; i < px.size(); ++i) {
            auto c = formats::rgb555_to_rgb(px[i]);
            rgb[i * 3] = c.r;
            rgb[i * 3 + 1] = c.g;
            rgb[i * 3 + 2] = c.b;
        }
        if (std::fwrite(rgb.data(), 1, rgb.size(), f) != rgb.size()) {
            std::fprintf(stderr, "write failed after %d frames\n", frames);
            return 1;
        }
        ++frames;
    }
    if (f != stdout) std::fclose(f);
    std::fprintf(stderr, "%d frames %dx%d @ %d fps\n", frames, h.width, h.height, h.frame_rate);
    return 0;
}

void write_wav(const std::string& out, uint32_t rate, const std::vector<int16_t>& pcm) {
    uint32_t bytes = uint32_t(pcm.size() * 2);
    std::vector<uint8_t> wav;
    auto put = [&](uint32_t v, int n) {
        for (int i = 0; i < n; ++i) wav.push_back(uint8_t(v >> (8 * i)));
    };
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    put(36 + bytes, 4);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(16, 4); put(1, 2); put(1, 2); put(rate, 4); put(rate * 2, 4); put(2, 2); put(16, 2);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    put(bytes, 4);
    for (int16_t s : pcm) put(uint16_t(s), 2);
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f || std::fwrite(wav.data(), 1, wav.size(), f) != wav.size()) {
        if (f) std::fclose(f);
        throw std::runtime_error("cannot write " + out);
    }
    std::fclose(f);
    std::printf("wrote %s (%zu samples @ %u Hz)\n", out.c_str(), pcm.size(), rate);
}

int cmd_vqa_wav(const std::string& file, const std::string& out) {
    formats::VqaDecoder vqa(formats::read_file(file));
    std::vector<int16_t> pcm;
    while (vqa.next_frame()) {
        auto a = vqa.take_audio();
        pcm.insert(pcm.end(), a.begin(), a.end());
    }
    write_wav(out, vqa.header().audio_rate, pcm);
    return 0;
}

int cmd_aud_wav(const std::string& file, const std::string& out) {
    auto aud = formats::decode_aud(formats::read_file(file));
    write_wav(out, aud.header.sample_rate, aud.samples);
    return 0;
}

int cmd_rs_list(const std::string& file) {
    auto data = formats::read_file(file);
    for (const auto& e : formats::parse_sound_rs(data)) {
        auto wav = formats::decode_wav(std::span<const uint8_t>(data).subspan(e.offset, e.size));
        std::printf("%-14s %8u bytes  %5u Hz  %.2f s\n", e.name.c_str(), e.size, wav.sample_rate,
                    double(wav.samples.size()) / wav.sample_rate);
    }
    return 0;
}

void write_surface_png(const ui::Surface& surface, const std::string& out) {
    std::vector<uint8_t> rgba(size_t(surface.width()) * surface.height() * 4);
    for (size_t i = 0; i < surface.pixels().size(); ++i) {
        auto c = formats::rgb555_to_rgb(surface.pixels()[i]);
        rgba[i * 4] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = 255;
    }
    tool::write_png_rgba(out, uint32_t(surface.width()), uint32_t(surface.height()), rgba);
    std::printf("wrote %s\n", out.c_str());
}

// Renders a menu screen (by menu name, e.g. MAINMENU) to a 640x400 PNG.
// "A+B" draws a menu stack bottom to top, as the original does for pop-ups.
int cmd_ui_render(const std::string& gamedir, const std::string& menus, const std::string& out) {
    platform::GameData gd(gamedir);
    ui::Assets assets(gd);
    ui::Surface surface(640, 400);
    std::stringstream names(menus);
    std::string menu;
    while (std::getline(names, menu, '+')) {
        auto script = assets.menu_script(menu);
        if (!script) {
            std::fprintf(stderr, "unknown menu %s\n", menu.c_str());
            return 1;
        }
        auto screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(*script)));
        ui::apply_menu_hooks(menu, screen, ui::HookContext{});
        ui::apply_image_sizes(screen, assets);
        ui::split_textboxes(screen, assets);
        ui::RenderState st;
        ui::render_screen(screen, assets, surface, st);
        std::printf("%s: %zu elements from %s\n", menu.c_str(), screen.elements.size(),
                    script->filename().string().c_str());
    }
    write_surface_png(surface, out);
    return 0;
}

// Draws a single TEXT.UIB value (after localisation) as wrapped text. Static
// menu rendering never fills briefing textboxes, so this is the offline way to
// check long translations, the CKF glyph coverage and the wrapping behaviour.
int cmd_text_preview(const std::string& gamedir, const std::string& key, const std::string& out) {
    platform::GameData gd(gamedir);
    ui::Assets assets(gd);
    ui::Surface surface(640, 400);
    surface.clear(0x2104); // dark blue, close to the original briefing panel
    const auto& font = assets.menu_font();
    const std::string text = assets.lookup_text(key);
    const int line_h = std::max(16, int(font.height) + 2);
    // Use the game's own wrapping routine so the preview matches what a real
    // briefing textbox shows (including the CJK character-boundary path).
    const auto lines = ui::split_textbox_text(font, text, 624);
    int y = 8;
    for (const auto& line : lines) {
        if (y >= 400) break;
        surface.draw_text_solid(font, 8, y, line, 0x7fff);
        y += line_h;
    }
    std::printf("%s: %zu bytes, %zu lines (drawn %d)\n", key.c_str(), text.size(), lines.size(),
                y / line_h);
    write_surface_png(surface, out);
    return 0;
}

// Runs the campaign map (GLOBE) for a house and mission until its reveal
// has finished, then writes the screen with the arrows' first frame.
int cmd_globe(const std::string& gamedir, int side, int mission, const std::string& out) {
    platform::GameData gd(gamedir);
    ui::Assets assets(gd);
    auto table = campaign::CampaignTable::parse(formats::read_file(gd.find("Data/UI_DATA/campaign.uib").value()));
    auto regions = campaign::load_regions(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    auto screen = ui::parse_screen(formats::parse_uil_lines(formats::read_file(*assets.menu_script("GLOBE"))));
    ui::apply_menu_hooks("GLOBE", screen, ui::HookContext{});
    ui::apply_image_sizes(screen, assets);
    ui::Surface background(640, 400), surface(640, 400);
    ui::render_background(screen, assets, background, nullptr);
    ui::render_screen(screen, assets, surface, {});
    ui::Globe globe(assets, table, regions, side, mission);
    int frames = 0;
    std::vector<ui::Globe::Arrow> arrows;
    for (; frames < 10000 && arrows.empty(); ++frames) {
        auto f = globe.update(surface, background);
        if (f.speech) std::printf("frame %d: speech %s\n", frames, f.speech->c_str());
        arrows = f.arrows;
    }
    // As the exe: when the reveal ends the whole menu redraws on the
    // finished background, then the arrows appear.
    ui::render_background(screen, assets, surface, &background);
    for (size_t i : ui::draw_order(screen)) ui::render_element(screen, i, assets, surface, {});
    for (const auto& a : arrows) {
        std::printf("arrow %s at %d,%d (%s)\n", a.name.c_str(), a.x, a.y, a.gfx.c_str());
        if (const auto* img = assets.image(a.gfx)) {
            surface.blit(*img, {0, 0, 72, 70}, a.x, a.y, ui::BlitMode::KeyedShadow);
        }
    }
    std::printf("reveal finished after %d updates\n", frames);
    write_surface_png(surface, out);
    return 0;
}

// Renders a mission's terrain: every cell's tile from the mission's
// tileset (BLOX*.R16, 32x32) after the game's map set-up (spice placed and
// smoothed; RNG seed 1 for the thick-spice variants).
int cmd_map_render(const std::string& gamedir, const std::string& name, const std::string& out) {
    platform::GameData gd(gamedir);
    auto map_path = gd.find("MISSIONS/" + name + ".MAP");
    auto mis_path = gd.find("MISSIONS/_" + name + ".MIS");
    if (!map_path || !mis_path) {
        std::fprintf(stderr, "mission %s not found\n", name.c_str());
        return 1;
    }
    auto map = formats::parse_map(formats::read_file(*map_path));
    auto mis = formats::parse_mission(formats::read_file(*mis_path));
    auto tiles = formats::parse_r8(formats::read_file(gd.find("Data/" + mis.tileset + ".R16").value()));
    auto attributes = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap game_map(map, attributes, tables, rng);
    std::printf("%s: %ux%u, tileset %s (%zu tiles), attributes %s\n", name.c_str(), map.width, map.height,
                mis.tileset.c_str(), tiles.size(), mis.tile_attributes.c_str());
    const uint32_t w = map.width * 32u, h = map.height * 32u;
    std::vector<uint8_t> rgba(size_t(w) * h * 4, 255);
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            uint16_t tile = game_map.at(x, y).tile;
            if (tile >= tiles.size() || tiles[tile].empty()) continue;
            const auto& t = tiles[tile];
            for (uint32_t ty = 0; ty < t.height && ty < 32; ++ty) {
                for (uint32_t tx = 0; tx < t.width && tx < 32; ++tx) {
                    size_t si = (size_t(ty) * t.width + tx) * 2;
                    uint16_t px = uint16_t(t.pixels[si] | t.pixels[si + 1] << 8);
                    auto rgb = formats::rgb555_to_rgb(px);
                    size_t di = ((size_t(y) * 32 + ty) * w + size_t(x) * 32 + tx) * 4;
                    rgba[di] = rgb.r;
                    rgba[di + 1] = rgb.g;
                    rgba[di + 2] = rgb.b;
                }
            }
        }
    }
    tool::write_png_rgba(out, w, h, rgba);
    std::printf("wrote %s\n", out.c_str());
    return 0;
}

// Draws image `slot` (0-2) of each of the 15 controls of a UI_<lang>.R16
// at its position on a 640x400 canvas, and lists everything.
int cmd_ui_res(const std::string& file, int slot, const std::string& out) {
    auto ui = formats::parse_ui_resource(formats::read_file(file));
    ui::Surface surface(640, 400);
    surface.clear(0x4210);
    // Type-2 images use the most recent type-1 palette (FUN_00460550),
    // so palettes are resolved in file order.
    std::array<uint16_t, 256> last_palette{};
    auto palette_of = [&](const formats::R8Image& img) {
        if (img.palette) last_palette = *img.palette;
        return last_palette;
    };
    std::vector<std::array<uint16_t, 256>> control_palettes;
    for (const auto& c : ui.controls) {
        for (const auto& img : c.images) control_palettes.push_back(palette_of(img));
    }
    auto blit = [&](const formats::R8Image& img, const std::array<uint16_t, 256>& pal, int x, int y) {
        if (img.pixels.empty()) return;
        for (uint32_t yy = 0; yy < img.height; ++yy) {
            for (uint32_t xx = 0; xx < img.width; ++xx) {
                int tx = x + int(xx), ty = y + int(yy);
                if (tx < 0 || ty < 0 || tx >= 640 || ty >= 400) continue;
                size_t i = size_t(yy) * img.width + xx;
                uint16_t px = img.bits_per_pixel == 16 ? uint16_t(img.pixels[2 * i] | img.pixels[2 * i + 1] << 8)
                                                       : pal[img.pixels[i]];
                surface.pixels()[size_t(ty) * 640 + tx] = px;
            }
        }
    };
    for (size_t i = 0; i < ui.controls.size(); ++i) {
        const auto& c = ui.controls[i];
        std::printf("control %2zu at %3d,%3d:", i, c.x, c.y);
        for (const auto& img : c.images) {
            std::printf("  %ux%u/%u%s", img.width, img.height, unsigned(img.bits_per_pixel),
                        img.pixels.empty() ? "(-)" : "");
        }
        std::printf("\n");
        blit(c.images[size_t(slot)], control_palettes[i * 3 + size_t(slot)], c.x, c.y);
    }
    for (size_t i = 0; i < ui.extra.size(); ++i) {
        const auto& img = ui.extra[i];
        std::printf("extra %zu: %ux%u%s\n", i, img.width, img.height, img.pixels.empty() ? "(-)" : "");
    }
    write_surface_png(surface, out);
    return 0;
}

// The in-game screen at a scroll position: UIBB.R16 backdrop, the map in
// the 480x380 playfield at (0, 20) as FUN_00423d20 draws it, and the
// UI_ENG controls (first image).
int cmd_game_screen(const std::string& gamedir, const std::string& name, int scroll_x, int scroll_y,
                    const std::string& out) {
    platform::GameData gd(gamedir);
    auto map = formats::parse_map(formats::read_file(gd.find("MISSIONS/" + name + ".MAP").value()));
    auto mis = formats::parse_mission(formats::read_file(gd.find("MISSIONS/_" + name + ".MIS").value()));
    auto tiles = formats::parse_r8(formats::read_file(gd.find("Data/" + mis.tileset + ".R16").value()));
    auto attributes = formats::TileAttributes::parse(
        formats::read_file(gd.find("Data/bin/" + mis.tile_attributes + ".BIN").value()));
    auto tables = sim::ExeTables::load(formats::PeImage(formats::read_file(gd.find("DUNE2000.DAT").value())));
    sim::Rng rng(1);
    sim::GameMap game_map(map, attributes, tables, rng);

    ui::Surface screen(640, 400);
    auto backdrop = formats::read_file(gd.find("Data/UIBB.R16").value());
    for (size_t i = 0; i < screen.pixels().size() && 2 * i + 1 < backdrop.size(); ++i) {
        screen.pixels()[i] = uint16_t(backdrop[2 * i] | backdrop[2 * i + 1] << 8);
    }
    render::draw_terrain(game_map, tiles, screen, scroll_x, scroll_y);
    auto td = formats::TileData::parse(formats::read_file(gd.find("Data/bin/TILEDATA.BIN").value()));
    auto templates = formats::Templates::parse(formats::read_file(gd.find("Data/bin/Templates.bin").value()));
    auto objects = sim::place_map_objects(map, td, templates, mis.house, tables);
    render::SpriteBank sprites(formats::parse_r8(formats::read_file(gd.find("Data/DATA.R16").value())),
                               formats::read_file(gd.find("Data/bin/COLOURS.BIN").value()));
    render::draw_objects(objects, templates, sprites, screen, scroll_x, scroll_y);
    for (const auto& o : objects) {
        std::printf("%s %s owner %d house %d at cell %d,%d record %d\n", o.building ? "building" : "unit",
                    (o.building ? templates.building_name[size_t(o.type)] : templates.unit_name[size_t(o.type)]).c_str(),
                    o.owner, o.house, o.cell_x, o.cell_y, o.record);
    }
    write_surface_png(screen, out);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2) {
        return usage();
    }
    const std::string& cmd = args[0];
    // Optional localisation for offline rendering: `ui-render ... --lang zh-cn`.
    // d2k-tool has no SDL, so seed the search roots from the working directory
    // and the executable's directory (I18n walks up a few parents).
    if (const auto lang = option(args, "--lang")) {
        std::error_code ec;
        std::vector<std::filesystem::path> dirs;
        dirs.push_back(std::filesystem::current_path(ec));
        dirs.push_back(std::filesystem::absolute(std::filesystem::path(argv[0]), ec).parent_path());
        auto& i18n = d2k::i18n::I18n::instance();
        i18n.init(dirs, *lang);
        std::fprintf(stderr, "i18n: %s\n", i18n.status().c_str());
    }
    try {
        if (cmd == "check") return cmd_check(args[1]);
        if (cmd == "info") return cmd_info(args[1]);
        if (cmd == "r8-info") return cmd_r8_info(args[1], flag(args, "--list"));
        if (cmd == "r8-sheet" && args.size() >= 3) return cmd_r8_sheet(args[1], args[2], args);
        if (cmd == "vqa-info") return cmd_vqa_info(args[1]);
        if (cmd == "vqa-frame" && args.size() >= 4) return cmd_vqa_frame(args[1], std::stoi(args[2]), args[3]);
        if (cmd == "vqa-wav" && args.size() >= 3) return cmd_vqa_wav(args[1], args[2]);
    if (cmd == "vqa-rgb" && args.size() >= 3) return cmd_vqa_rgb(args[1], args[2]);
        if (cmd == "aud-wav" && args.size() >= 3) return cmd_aud_wav(args[1], args[2]);
        if (cmd == "rs-list") return cmd_rs_list(args[1]);
        if (cmd == "ui-render" && args.size() >= 4) return cmd_ui_render(args[1], args[2], args[3]);
        if (cmd == "text-preview" && args.size() >= 4) return cmd_text_preview(args[1], args[2], args[3]);
        if (cmd == "game-screen" && args.size() >= 6) return cmd_game_screen(args[1], args[2], std::stoi(args[3]), std::stoi(args[4]), args[5]);
        if (cmd == "ui-res" && args.size() >= 4) return cmd_ui_res(args[1], std::stoi(args[2]), args[3]);
        if (cmd == "map-render" && args.size() >= 4) return cmd_map_render(args[1], args[2], args[3]);
        if (cmd == "globe" && args.size() >= 5) return cmd_globe(args[1], std::stoi(args[2]), std::stoi(args[3]), args[4]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return usage();
}
