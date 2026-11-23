#include "platform/setup_sources.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>

#include "formats/file_io.hpp"
#include "formats/installshield3.hpp"
#include "formats/iso9660.hpp"
#include "formats/zip.hpp"
#include "platform/manifest.hpp"
#include "platform/sha256.hpp"

namespace d2k::platform::setup {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// The CD's installer archive: every file under DUNE\ (API\ is Westwood
// Online support, not the game).
void add_setup_z(Source& src, std::vector<uint8_t> blob, const std::string& where) {
    auto data = std::make_shared<std::vector<uint8_t>>(std::move(blob));
    for (const auto& e : formats::is3_index(*data)) {
        const std::string dir = upper(e.dir);
        if (dir != "DUNE" && dir.rfind("DUNE\\", 0) != 0) continue;
        src.items.push_back({where + ":" + e.dir + "\\" + e.name, e.size,
                             [data, e] { return formats::is3_extract(*data, e); }});
    }
}

// A ZIP (the patch's appended archive); false when it is not one.
bool add_zip(Source& src, std::vector<uint8_t> blob, const std::string& where) {
    auto data = std::make_shared<std::vector<uint8_t>>(std::move(blob));
    std::shared_ptr<formats::ZipArchive> zip;
    try {
        zip = std::make_shared<formats::ZipArchive>(*data);
    } catch (const std::exception&) {
        return false;
    }
    for (const auto& e : zip->entries()) {
        if (e.directory) continue;
        src.items.push_back({where + ":" + e.name, e.size, [data, zip, e] { return zip->extract(e); }});
    }
    return true;
}

std::string file_label(const fs::path& p, const char* what) { return p.filename().string() + " (" + what + ")"; }

} // namespace

Source open_cd_image(const fs::path& iso_path) {
    Source src;
    src.kind = SourceKind::CdImage;
    src.location = iso_path;
    std::shared_ptr<formats::IsoImage> iso;
    try {
        iso = std::make_shared<formats::IsoImage>(iso_path);
    } catch (const std::exception&) {
        throw std::runtime_error(iso_path.filename().string() + " is not a CD image this game can read (.iso or .bin).");
    }
    src.label = file_label(iso_path, ("CD image " + iso->volume_id()).c_str());
    for (const auto& f : iso->files()) {
        if (upper(f.path) == "SETUP/SETUP.Z") add_setup_z(src, iso->read(f), "SETUP/SETUP.Z");
        src.items.push_back({f.path, f.size, [iso, f] { return iso->read(f); }});
    }
    return src;
}

Source open_patch(const fs::path& exe) {
    Source src;
    src.kind = SourceKind::Patch;
    src.location = exe;
    src.label = file_label(exe, "patch");
    std::error_code ec;
    const auto size = fs::file_size(exe, ec);
    if (ec || size > 64u << 20 || !add_zip(src, formats::read_file(exe), exe.filename().string())) {
        throw std::runtime_error(exe.filename().string() + " is not the Dune 2000 patch (d2k106us.exe).");
    }
    return src;
}

Source open_folder(const fs::path& dir) {
    Source src;
    src.kind = SourceKind::Folder;
    src.location = dir;
    src.label = (dir.filename().empty() ? dir.string() : dir.filename().string()) + " (folder)";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) throw std::runtime_error(dir.string() + " is not a folder.");
    constexpr size_t kMaxFiles = 40000; // a whole drive picked by mistake
    auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 6) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (src.items.size() >= kMaxFiles) {
            throw std::runtime_error("That folder holds too many files. Choose the Dune 2000 folder itself.");
        }
        const fs::path p = it->path();
        const std::string rel = fs::relative(p, dir, ec).generic_string();
        const uint64_t size = it->file_size(ec);
        const std::string name = upper(p.filename().string());
        if (name == "SETUP.Z") {
            try {
                add_setup_z(src, formats::read_file(p), rel);
            } catch (const std::exception&) {
            }
        } else if (name.size() > 4 && name.ends_with(".EXE") && size > 1u << 20 && size < 64u << 20) {
            try {
                add_zip(src, formats::read_file(p), rel); // the patch kept next to the game
            } catch (const std::exception&) {
            }
        }
        src.items.push_back({rel, size, [p] { return formats::read_file(p); }});
    }
    return src;
}

Collector::Collector() : match_(manifest_106().size()) {}

size_t Collector::found() const {
    return size_t(std::count_if(match_.begin(), match_.end(), [](const Match& m) { return m.source >= 0; }));
}

size_t Collector::needed() const { return match_.size(); }

size_t Collector::supplied_by(size_t source) const {
    return size_t(std::count_if(match_.begin(), match_.end(), [&](const Match& m) { return m.source == int(source); }));
}

size_t Collector::add(Source source, const Progress& progress) {
    const auto& man = manifest_106();
    // Missing entries by size: only items of such a size are hashed.
    std::multimap<uint64_t, size_t> want;
    for (size_t i = 0; i < man.size(); ++i) {
        if (match_[i].source < 0) want.emplace(man[i].size, i);
    }
    uint64_t total = 0;
    std::vector<size_t> candidates;
    for (size_t k = 0; k < source.items.size(); ++k) {
        if (want.count(source.items[k].size)) {
            candidates.push_back(k);
            total += source.items[k].size;
        }
    }
    const int si = int(sources_.size());
    size_t added = 0;
    uint64_t done = 0;
    for (size_t k : candidates) {
        if (progress && !progress(done, total)) break;
        const auto& item = source.items[k];
        std::vector<uint8_t> bytes;
        try {
            bytes = item.read();
        } catch (const std::exception&) {
            done += item.size;
            continue; // unreadable: another source may have it
        }
        done += item.size;
        const std::string hash = sha256_hex(bytes);
        auto [lo, hi] = want.equal_range(item.size);
        for (auto w = lo; w != hi;) {
            if (match_[w->second].source < 0 && hash == man[w->second].sha256) {
                match_[w->second] = {si, int(k)};
                ++added;
                w = want.erase(w);
            } else {
                ++w;
            }
        }
    }
    if (progress) progress(total, total);
    sources_.push_back(std::move(source));
    return added;
}

std::vector<std::string> Collector::missing_summary(size_t examples) const {
    const auto& man = manifest_106();
    std::map<std::string, size_t> groups;
    std::vector<std::string> names;
    bool exe = false;
    for (size_t i = 0; i < man.size(); ++i) {
        if (match_[i].source >= 0) continue;
        const std::string p = man[i].path;
        if (p == "DUNE2000.DAT") {
            exe = true;
            continue;
        }
        ++groups[upper(p.substr(0, p.find('/')))];
        if (names.size() < examples) names.push_back(p);
    }
    std::vector<std::string> out;
    if (exe) out.push_back("the 1.06 patch (d2k106us.exe)");
    for (const auto& [group, n] : groups) {
        const std::string count = std::to_string(n) + " file" + (n == 1 ? "" : "s");
        if (group == "MOVIES") out.push_back("the movies, " + count + " (add the CD or CD image)");
        else if (group == "MUSIC") out.push_back("the music, " + count + " (add the CD or CD image)");
        else if (group == "MISSIONS") out.push_back("the missions, " + count + " (add the CD or CD image)");
        else out.push_back(count + " of the game data");
    }
    if (!out.empty() && !names.empty()) {
        std::string e = "for example ";
        for (size_t i = 0; i < names.size(); ++i) e += (i ? ", " : "") + names[i];
        out.push_back(e);
    }
    return out;
}

bool Collector::install(const fs::path& dest, const Progress& progress) const {
    if (!complete()) throw std::runtime_error("Not all game files were found yet.");
    const auto& man = manifest_106();
    fs::path tmp = dest;
    tmp += ".tmp";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) throw std::runtime_error("Cannot create " + tmp.string() + ": " + ec.message());
    uint64_t total = 0, done = 0;
    for (const auto& e : man) total += e.size;
    for (size_t i = 0; i < man.size(); ++i) {
        if (progress && !progress(done, total)) {
            fs::remove_all(tmp, ec);
            return false;
        }
        const auto& m = match_[i];
        const auto bytes = sources_[size_t(m.source)].items[size_t(m.item)].read();
        if (bytes.size() != man[i].size || sha256_hex(bytes) != man[i].sha256) {
            fs::remove_all(tmp, ec);
            throw std::runtime_error(std::string("A source changed while installing: ") + man[i].path);
        }
        const fs::path out = tmp / fs::path(man[i].path);
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        f.close();
        if (!f) {
            fs::remove_all(tmp, ec);
            throw std::runtime_error("Cannot write " + out.string() + " (is the disk full?)");
        }
        done += man[i].size;
    }
    fs::remove_all(dest, ec); // an earlier, broken import
    fs::rename(tmp, dest, ec);
    if (ec) throw std::runtime_error("Cannot rename " + tmp.string() + ": " + ec.message());
    if (progress) progress(total, total);
    return true;
}

} // namespace d2k::platform::setup
