#include "port/mods.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "port/connect_file.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <iterator>
#include <tuple>

namespace tenchu::mods {
namespace {
namespace fs = std::filesystem;

struct Mod {
    ModStatus status;
    fs::path dir;
    std::vector<std::pair<std::string, fs::path>> files;  // upper-case disc path -> host file
    // files inside an archive: <disc path of the .con>.files\<entry> replaces that one entry
    std::vector<std::tuple<std::string, std::string, fs::path>> entries;  // archive key, entry key, host file
};

std::mutex g_lock;
std::vector<Mod> g_mods;                  // priority order
std::vector<std::string> g_order;         // saved order, absent mods included
std::set<std::string> g_disabled;
std::unordered_map<std::string, std::pair<fs::path, std::string>> g_map;  // disc path -> host file, mod
// archive key -> entry key -> host file, mod: the enabled mods' entries, the first mod in order winning
std::unordered_map<std::string, std::map<std::string, std::pair<fs::path, std::string>>> g_entries;
std::unordered_map<std::string, std::string> g_archives;  // archive key -> host file served this session ("" = original)
std::atomic<size_t> g_replaced{0};
std::deque<std::string> g_recent;

std::string Utf8(const fs::path& p) { auto s = p.u8string(); return {s.begin(), s.end()}; }
std::string Upper(std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }

// the winning file for every disc path, from the enabled mods in order; under g_lock
void Rebuild() {
    g_map.clear();
    g_entries.clear();
    g_archives.clear();
    for (Mod& m : g_mods) {
        m.status.hidden = 0;
        if (!m.status.enabled) continue;
        for (auto& [key, host] : m.files)
            if (!g_map.emplace(key, std::make_pair(host, m.status.name)).second) m.status.hidden++;
        for (auto& [archive, entry, host] : m.entries)
            if (!g_entries[archive].emplace(entry, std::make_pair(host, m.status.name)).second) m.status.hidden++;
    }
}

void ReadInfo(Mod& m) {
    try {
        fs::path info = m.dir / "mod.toml";
        if (!fs::exists(info)) return;
        auto t = toml::parse_file(Utf8(info));
        m.status.title = t["title"].value_or(std::string{});
        m.status.author = t["author"].value_or(std::string{});
        m.status.version = t["version"].value_or(std::string{});
        m.status.description = t["description"].value_or(std::string{});
    } catch (const std::exception& e) {
        LOGE("mods: %s/mod.toml not read: %s", m.status.name.c_str(), e.what());
    }
}

// files at the mod's top level that describe it rather than replace disc files
bool ModMetadata(const fs::path& relative) {
    if (relative.has_parent_path()) return false;
    std::string name = Upper(Utf8(relative));
    return name == "MOD.TOML" || name.rfind("README", 0) == 0 || name.rfind("LICENSE", 0) == 0 || name == "DESKTOP.INI";
}

}  // namespace

fs::path ModsDir() { return app::ExeDir() / "mods"; }

void Rescan() {
    std::error_code ec;
    fs::create_directories(ModsDir(), ec);
    std::vector<Mod> found;
    for (fs::directory_iterator it(ModsDir(), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        Mod m;
        m.dir = it->path();
        m.status.name = Utf8(it->path().filename());
        ReadInfo(m);
        std::error_code walk;
        for (fs::recursive_directory_iterator f(m.dir, fs::directory_options::follow_directory_symlink, walk), fend; !walk && f != fend; f.increment(walk)) {
            if (!f->is_regular_file(walk)) continue;
            fs::path relative = f->path().lexically_relative(m.dir);
            if (ModMetadata(relative)) continue;
            std::string key = Upper(Utf8(relative.make_preferred()));
            if (key.size() >= 4 && key.compare(key.size() - 4, 4, ".XBE") == 0) continue;  // code is precompiled
            if (size_t at = key.find(".CON.FILES\\"); at != std::string::npos) {
                m.entries.emplace_back(key.substr(0, at + 4), tenchu::con::Key(key.substr(at + 11)), f->path());
                continue;
            }
            m.files.emplace_back(key, f->path());
        }
        m.status.files = m.files.size() + m.entries.size();
        found.push_back(std::move(m));
    }
    std::lock_guard l(g_lock);
    auto rank = [&](const std::string& n) { auto i = std::find(g_order.begin(), g_order.end(), n); return size_t(i - g_order.begin()); };
    std::sort(found.begin(), found.end(), [&](const Mod& a, const Mod& b) {
        return rank(a.status.name) != rank(b.status.name) ? rank(a.status.name) < rank(b.status.name) : a.status.name < b.status.name;
    });
    for (Mod& m : found) {
        m.status.enabled = !g_disabled.count(m.status.name);
        if (std::find(g_order.begin(), g_order.end(), m.status.name) == g_order.end()) g_order.push_back(m.status.name);
    }
    g_mods = std::move(found);
    Rebuild();
    size_t active = 0;
    for (auto& m : g_mods) active += m.status.enabled;
    if (!g_mods.empty()) LOGR("mods: %zu found, %zu enabled, %zu disc files replaced", g_mods.size(), active, g_map.size());
}

std::vector<ModStatus> List() {
    std::lock_guard l(g_lock);
    std::vector<ModStatus> out;
    for (auto& m : g_mods) out.push_back(m.status);
    return out;
}

void SetEnabled(const std::string& name, bool enabled) {
    std::lock_guard l(g_lock);
    for (auto& m : g_mods)
        if (m.status.name == name) m.status.enabled = enabled;
    if (enabled) g_disabled.erase(name); else g_disabled.insert(name);
    Rebuild();
}

void Move(const std::string& name, int delta) {
    std::lock_guard l(g_lock);
    auto it = std::find_if(g_mods.begin(), g_mods.end(), [&](const Mod& m) { return m.status.name == name; });
    if (it == g_mods.end() || it + delta < g_mods.begin() || it + delta >= g_mods.end()) return;
    std::iter_swap(it, it + delta);
    g_order.clear();
    for (auto& m : g_mods) g_order.push_back(m.status.name);
    Rebuild();
}

namespace {
int64_t Stamp(const fs::path& p) {
    std::error_code ec;
    return int64_t(fs::last_write_time(p, ec).time_since_epoch().count());
}

// The host file to serve for an archive the game opens: "" keeps the original. Built
// once per session under g_lock, from the base (a whole-file mod, extracted\ or the
// disc) with the mods' entries and the player's edits in extracted\...\<name>.con.files
// applied; reused while its inputs are unchanged.
std::string ArchiveFor(const std::string& key, const std::vector<std::string>& parts) {
    if (auto done = g_archives.find(key); done != g_archives.end()) return done->second;
    std::string& result = g_archives[key];
    fs::path relative;
    for (const std::string& part : parts)
        if (!part.empty() && part != ".") relative /= fs::path(std::u8string(part.begin(), part.end()));
    const fs::path extracted = app::ExeDir() / "extracted";
    std::error_code ec;
    const bool unpacked = fs::exists(extracted / ".complete", ec);

    // what replaces entries: the mods first, then edits in extracted\ the mods leave alone
    std::map<std::string, std::pair<fs::path, std::string>> sources;
    if (auto e = g_entries.find(key); e != g_entries.end()) sources = e->second;
    const fs::path loose = extracted / fs::path(relative.u8string() + u8".files");
    const int64_t marker = Stamp(loose / ".extracted");
    std::vector<std::pair<std::string, fs::path>> edits;
    if (unpacked && marker)
        for (fs::recursive_directory_iterator it(loose, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().filename() != ".extracted" && it->path().extension() != ".partial") {
                auto rel = it->path().lexically_relative(loose).u8string();
                edits.emplace_back(tenchu::con::Key(std::string(rel.begin(), rel.end())), it->path());
            }
    if (sources.empty() && edits.empty()) return result = g_map.count(key) ? g_map[key].first.string() : "";

    // the base archive
    tenchu::con::Source source;
    uint64_t total = 0;
    std::string base_id;
    if (auto whole = g_map.find(key); whole != g_map.end()) {
        source = tenchu::con::FileSource(whole->second.first);
        total = fs::file_size(whole->second.first, ec);
        base_id = whole->second.first.string() + "|" + std::to_string(Stamp(whole->second.first));
    } else if (unpacked && fs::exists(extracted / relative, ec)) {
        source = tenchu::con::FileSource(extracted / relative);
        total = fs::file_size(extracted / relative, ec);
        base_id = (extracted / relative).string() + "|" + std::to_string(total);
    } else if (const kernel::Disc* disc = kernel::MountedDisc()) {
        const kernel::Disc::Entry* entry = disc->Find(parts);
        if (!entry || entry->dir) return result;
        source = [disc, entry](uint64_t offset, void* out, uint32_t len) {
            return offset + len <= entry->size && disc->Read(*entry, offset, out, len) == len;
        };
        total = entry->size;
        base_id = "disc|" + std::to_string(total);
    } else {
        return result;
    }
    tenchu::con::Archive archive;
    std::string error;
    if (!tenchu::con::Parse(source, total, archive, error)) {
        LOGR("mods: %s is not an archive (%s); entry replacements skipped", key.c_str(), error.c_str());
        return result = g_map.count(key) ? g_map[key].first.string() : "";
    }
    std::map<std::string, const tenchu::con::Entry*> base_entries;
    for (const auto& e : archive.entries) base_entries[tenchu::con::Key(e.name)] = &e;
    // A loose file is the player's edit when it differs from the archive it was unpacked from: the disc's. Neither
    // a whole-archive mod nor extracted\<name>.con is that archive once a tool saves over it, and compared against
    // either, every file they changed looked edited and the unpacked original went back over the new version.
    tenchu::con::Archive original;
    tenchu::con::Source original_source;
    if (!edits.empty()) {
        std::string original_error;
        const kernel::Disc* disc = kernel::MountedDisc();
        const kernel::Disc::Entry* on_disc = disc ? disc->Find(parts) : nullptr;
        if (on_disc && !on_disc->dir) {
            original_source = [disc, on_disc](uint64_t offset, void* out, uint32_t len) {
                return offset + len <= on_disc->size && disc->Read(*on_disc, offset, out, len) == len;
            };
            if (!tenchu::con::Parse(original_source, on_disc->size, original, original_error)) original_source = nullptr;
        }
        const fs::path original_path = extracted / relative;
        if (!original_source && fs::exists(original_path, ec)) {
            original_source = tenchu::con::FileSource(original_path);
            if (!tenchu::con::Parse(original_source, fs::file_size(original_path, ec), original, original_error)) original_source = nullptr;
        }
        if (!original_source) edits.clear();  // nothing to tell edits from unpacked originals by: the base is served as it is
    }
    std::map<std::string, const tenchu::con::Entry*> original_entries;
    for (const auto& e : original.entries) original_entries[tenchu::con::Key(e.name)] = &e;
    for (auto& [entry, path] : edits) {
        auto base_entry = base_entries.find(entry);
        auto original_entry = original_entries.find(entry);
        if (base_entry == base_entries.end() || original_entry == original_entries.end() || sources.count(entry)) continue;
        if (fs::file_size(path, ec) == original_entry->second->size && Stamp(path) <= marker) continue;  // the unpacked original
        // mods come before extracted\: a file a whole-archive mod changed keeps the mod's version
        if (g_map.count(key)) {
            std::vector<uint8_t> modded, unmodded;
            if (base_entry->second->size != original_entry->second->size ||
                !tenchu::con::ReadEntry(source, *base_entry->second, modded) ||
                !tenchu::con::ReadEntry(original_source, *original_entry->second, unmodded) || modded != unmodded) {
                LOGR("mods: %s keeps %s from %s over the edit in extracted", key.c_str(), entry.c_str(), g_map[key].second.c_str());
                continue;
            }
        }
        sources.emplace(entry, std::make_pair(path, std::string("extracted")));
    }
    for (auto it = sources.begin(); it != sources.end();) {
        if (!base_entries.count(it->first)) {
            LOGR("mods: %s has no entry %s (from %s); ignored", key.c_str(), it->first.c_str(), it->second.second.c_str());
            it = sources.erase(it);
        } else {
            ++it;
        }
    }
    if (sources.empty()) return result = g_map.count(key) ? g_map[key].first.string() : "";

    // reuse the cached archive while its inputs are the same
    std::string signature = base_id;
    for (auto& [entry, src] : sources)
        signature += "\n" + entry + "|" + src.first.string() + "|" + std::to_string(fs::file_size(src.first, ec)) + "|" + std::to_string(Stamp(src.first));
    const fs::path cache = app::ExeDir() / "cache" / "archives" / relative;
    fs::path sig = cache;
    sig += L".sig";
    {
        std::ifstream in(sig, std::ios::binary);
        std::string old((std::istreambuf_iterator<char>(in)), {});
        if (old == signature && fs::exists(cache, ec)) {
            LOGR("mods: %s with %zu replaced files (cached)", key.c_str(), sources.size());
            return result = cache.string();
        }
    }
    std::map<std::string, std::vector<uint8_t>> replacements;
    for (auto& [entry, src] : sources) {
        std::ifstream in(src.first, std::ios::binary);
        replacements[entry].assign(std::istreambuf_iterator<char>(in), {});
        LOGR("mods: %s <- %s (%s)", key.c_str(), entry.c_str(), src.second.c_str());
    }
    if (!tenchu::con::Write(source, archive, replacements, cache, error)) {
        LOGE("mods: building %s failed: %s", key.c_str(), error.c_str());
        return result = g_map.count(key) ? g_map[key].first.string() : "";
    }
    std::ofstream(sig, std::ios::binary) << signature;
    LOGR("mods: built %s with %zu replaced files", key.c_str(), sources.size());
    return result = cache.string();
}
}  // namespace

std::string Override(const std::vector<std::string>& parts) {
    std::string key;
    for (const std::string& part : parts) {
        if (part.empty() || part == ".") continue;
        if (!key.empty()) key += '\\';
        key += part;
    }
    key = Upper(key);
    std::lock_guard l(g_lock);
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".CON") == 0) {
        std::string archive = ArchiveFor(key, parts);
        if (!archive.empty()) {
            g_replaced++;
            if (g_recent.empty() || g_recent.front() != key) {
                g_recent.push_front(key);
                if (g_recent.size() > 50) g_recent.pop_back();
            }
        }
        return archive;
    }
    if (g_map.empty()) return {};
    auto it = g_map.find(key);
    if (it == g_map.end()) return {};
    g_replaced++;
    std::string note = key + "  (" + it->second.second + ")";
    if (g_recent.empty() || g_recent.front() != note) {
        LOGR("mods: %s from %s", key.c_str(), it->second.second.c_str());
        g_recent.push_front(note);
        if (g_recent.size() > 50) g_recent.pop_back();
    }
    return it->second.first.string();
}

size_t Replaced() { return g_replaced.load(); }

std::vector<std::string> Recent(size_t max) {
    std::lock_guard l(g_lock);
    return {g_recent.begin(), g_recent.begin() + std::min(max, g_recent.size())};
}

bool WriteDiscList(std::string& status) {
    const kernel::Disc* disc = kernel::MountedDisc();
    const kernel::Disc::Entry* root = disc ? disc->Find(std::vector<std::string>{}) : nullptr;
    if (!root) { status = "The game disc is not mounted."; return false; }
    std::error_code ec;
    fs::create_directories(ModsDir(), ec);
    std::ofstream out(ModsDir() / "disc_files.txt");
    out << "# Every file on the game disc with its size in bytes. A mod replaces one by\n"
           "# putting a file at the same path inside mods\\<mod name>\\.\n";
    size_t count = 0;
    std::function<void(const kernel::Disc::Entry&, const std::string&)> walk = [&](const kernel::Disc::Entry& dir, const std::string& prefix) {
        for (const auto& e : dir.children) {
            std::string path = prefix.empty() ? e.name : prefix + "\\" + e.name;
            if (e.dir) walk(e, path);
            else { out << path << '\t' << e.size << '\n'; count++; }
        }
    };
    walk(*root, "");
    if (!out) { status = "Could not write mods\\disc_files.txt."; return false; }
    status = "Wrote " + std::to_string(count) + " disc files to mods\\disc_files.txt.";
    return true;
}

void Load(const void* value) {
    const auto& t = *static_cast<const toml::table*>(value);
    std::lock_guard l(g_lock);
    g_order.clear();
    g_disabled.clear();
    if (auto* order = t["mods"]["order"].as_array())
        for (auto&& v : *order) if (auto s = v.value<std::string>()) g_order.push_back(*s);
    if (auto* off = t["mods"]["disabled"].as_array())
        for (auto&& v : *off) if (auto s = v.value<std::string>()) g_disabled.insert(*s);
}

void Save(void* value) {
    auto& t = *static_cast<toml::table*>(value);
    toml::array order, off;
    std::lock_guard l(g_lock);
    for (auto& name : g_order) order.push_back(name);
    for (auto& name : g_disabled) off.push_back(name);
    t.insert_or_assign("mods", toml::table{{"order", order}, {"disabled", off}});
}

}  // namespace tenchu::mods

// Called by the kernel file overlay for every path on the game disc.
std::string TenchuModOverride(const std::vector<std::string>& parts) { return tenchu::mods::Override(parts); }
