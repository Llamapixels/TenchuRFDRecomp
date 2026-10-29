#include "port/memory_patches.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace tenchu::patches {

namespace {

namespace fs = std::filesystem;

std::string Trim(std::string s) {
    auto space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && space(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && space(s[i])) i++;
    return s.substr(i);
}

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "Key=value" or "Key: value", false when the line is not that key
bool KeyValue(const std::string& line, const char* key, std::string& value) {
    size_t n = std::strlen(key);
    if (line.size() <= n || Lower(line.substr(0, n)) != key) return false;
    std::string rest = Trim(line.substr(n));
    if (rest.empty() || (rest[0] != '=' && rest[0] != ':')) return false;
    value = Trim(rest.substr(1));
    return true;
}

bool Hex(std::string s, uint64_t& out, size_t& digits) {
    s = Trim(s);
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.empty() || s.size() > 16 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
    out = std::stoull(s, nullptr, 16);
    digits = s.size();
    return true;
}

// Address,<address>,value,<value>[,<type>]: the type, when given, is byte, 2 bytes, 4 bytes or float
std::string ReadWrite(const std::string& line, Write& w) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    for (std::string part; std::getline(ss, part, ',');) parts.push_back(Trim(part));
    if (parts.size() < 4 || Lower(parts[2]) != "value") return "not Address,<address>,value,<value>";
    uint64_t address = 0, value = 0;
    size_t digits = 0;
    if (!Hex(parts[1], address, digits)) return "the address is not hexadecimal";
    // Cheat Engine shows the host address: game memory starts at 0x100000000 (or 0x200000000, 0x300000000)
    uint64_t high = address >> 32;
    if (high > 3) return "the address is outside game memory";
    w.va = uint32_t(address);
    if (w.va >= 0x80000000u) return "the address is outside game memory";
    std::string type = parts.size() > 4 ? Lower(parts[4]) : "";
    std::string v = parts[3];
    if (type == "float" || (type.empty() && v.find('.') != std::string::npos)) {
        try {
            float f = std::stof(v);
            std::memcpy(&w.value, &f, 4);
        } catch (...) {
            return "the value is not a number";
        }
        w.size = 4;
        return {};
    }
    if (!Hex(v, value, digits) || digits > 8) return "the value is not 1 to 8 hexadecimal digits";
    w.value = uint32_t(value);
    w.size = digits <= 2 ? 1 : digits <= 4 ? 2 : 4;
    if (type == "byte" || type == "1 byte") w.size = 1;
    else if (type == "2 bytes" || type == "2bytes") w.size = 2;
    else if (type == "4 bytes" || type == "4bytes") w.size = 4;
    return {};
}

// XBE section names that hold data; every other section holds code (the game's or a library's)
bool DataSection(const std::string& name) {
    static const char* kData[] = {".data", ".rdata", ".data1", "XON_RD", "$$XTIMAGE", "$$XSIMAGE", ".XTLID"};
    for (const char* d : kData)
        if (name == d) return true;
    return false;
}

uint32_t Read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

std::vector<Patch> Parse(const std::string& text, const std::string& file) {
    std::vector<Patch> out;
    std::string author;  // an Author line before the first patch counts for the whole file
    std::stringstream ss(text);
    std::string line;
    int number = 0;
    while (std::getline(ss, line)) {
        number++;
        if (number == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line = line.substr(3);  // UTF-8 BOM
        line = Trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']' && line.size() > 2) {
            out.push_back({});
            out.back().name = Trim(line.substr(1, line.size() - 2));
            out.back().author = author;
            out.back().file = file;
            continue;
        }
        std::string value;
        if (KeyValue(line, "author", value)) {
            (out.empty() ? author : out.back().author) = value;
            continue;
        }
        if (!out.empty() && (KeyValue(line, "notes", value) || KeyValue(line, "note", value) || KeyValue(line, "description", value))) {
            out.back().notes += (out.back().notes.empty() ? "" : "\n") + value;
            continue;
        }
        if (Lower(line).rfind("address", 0) == 0 && line.find(',') != std::string::npos) {
            if (out.empty()) continue;  // an address before any [name]
            Write w;
            std::string problem = ReadWrite(line, w);
            if (problem.empty()) out.back().writes.push_back(w);
            else out.back().problems.push_back("line " + std::to_string(number) + ": " + problem);
        }
        // anything else (titles, instructions, chat) is left alone
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Patch& p) { return p.writes.empty() && p.problems.empty(); }), out.end());
    return out;
}

std::vector<Range> CodeRanges(const std::function<const uint8_t*(uint32_t, uint32_t)>& map) {
    constexpr uint32_t kImage = 0x10000;
    const uint8_t* h = map(kImage, 0x178);
    if (!h || std::memcmp(h, "XBEH", 4) || Read32(h + 0x104) != kImage) return {};
    uint32_t count = Read32(h + 0x11C), headers = Read32(h + 0x120);
    std::vector<Range> out;
    for (uint32_t i = 0; i < count && i < 64; i++) {
        const uint8_t* s = map(headers + i * 56, 56);
        if (!s) return {};
        uint32_t va = Read32(s + 4), size = Read32(s + 8), name_va = Read32(s + 20);
        std::string name;
        for (uint32_t k = 0; k < 16; k++) {
            const uint8_t* c = map(name_va + k, 1);
            if (!c || !*c) break;
            name += char(*c);
        }
        if (!DataSection(name)) out.push_back({va, va + size, name});
    }
    return out;
}

const Range* InCode(const Patch& p, const std::vector<Range>& code) {
    for (const Write& w : p.writes)
        for (const Range& r : code)
            if (w.va < r.end && w.va + w.size > r.begin) return &r;
    return nullptr;
}

// Set

Set::Saved& Set::Slot(const std::string& id) {
    for (Saved& s : saved_)
        if (s.id == id) return s;
    saved_.push_back({id, {}, State::Off});
    return saved_.back();
}

void Set::Restore(Saved& s, const Patch& p, const Map& map) {
    for (size_t i = 0; i < s.original.size() && i < p.writes.size(); i++) {
        if (s.original[i].empty()) continue;
        if (uint8_t* at = map(p.writes[i].va, p.writes[i].size)) std::memcpy(at, s.original[i].data(), s.original[i].size());
        s.original[i].clear();
    }
}

void Set::Load(std::vector<Patch> patches, const Map& map) {
    // the old patches put their bytes back; the new ones take them again on the next Apply
    for (Saved& s : saved_)
        for (const Patch& p : patches_)
            if (p.Id() == s.id) Restore(s, p, map);
    saved_.clear();
    patches_ = std::move(patches);
}

void Set::SetEnabled(const std::string& id, bool on) {
    auto it = std::find(enabled_.begin(), enabled_.end(), id);
    if (on && it == enabled_.end()) enabled_.push_back(id);
    if (!on && it != enabled_.end()) enabled_.erase(it);
}

bool Set::Enabled(const std::string& id) const { return std::find(enabled_.begin(), enabled_.end(), id) != enabled_.end(); }

void Set::Apply(const Map& map) {
    for (const Patch& p : patches_) {
        Saved& s = Slot(p.Id());
        if (!Enabled(s.id)) {
            Restore(s, p, map);
            s.state = State::Off;
            continue;
        }
        s.original.resize(p.writes.size());
        s.state = p.writes.empty() ? State::Outside : State::Active;
        for (size_t i = 0; i < p.writes.size(); i++) {
            const Write& w = p.writes[i];
            uint8_t* at = map(w.va, w.size);
            if (!at) {
                s.state = State::Waiting;
                continue;
            }
            if (s.original[i].empty()) s.original[i].assign(at, at + w.size);
            std::memcpy(at, &w.value, w.size);
        }
    }
}

State Set::StateOf(const std::string& id) const {
    for (const Saved& s : saved_)
        if (s.id == id) return s.state;
    return State::Off;
}

// The game's

namespace {

std::mutex g_lock;
Set g_set;
std::vector<Range> g_code;
std::atomic<bool> g_started{false};

std::string Utf8(const fs::path& p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}

// game memory that is committed (and writable when write is set), else null
uint8_t* GameMemory(uint32_t va, uint32_t size, bool write) {
    if (!kernel::g_base || !size || uint64_t(va) + size > 0x80000000ull) return nullptr;
    uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(va));
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof(mi)) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    const DWORD writable = PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    if (write && !(mi.Protect & writable)) return nullptr;
    if (p + size > static_cast<uint8_t*>(mi.BaseAddress) + mi.RegionSize) return nullptr;
    return p;
}
uint8_t* GameMap(uint32_t va, uint32_t size) { return GameMemory(va, size, true); }

void Writer() {
    for (;;) {
        {
            std::lock_guard l(g_lock);
            if (g_code.empty()) {
                g_code = CodeRanges([](uint32_t va, uint32_t n) -> const uint8_t* { return GameMemory(va, n, false); });
                for (const Range& r : g_code) LOGI("Memory patches: code section %s %08X-%08X", r.name.c_str(), r.begin, r.end);
                if (!g_code.empty()) LOGR("Memory patches: %zu code sections found in the game image", g_code.size());
            }
            g_set.Apply(GameMap);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::vector<Patch> ReadFolder() {
    std::vector<Patch> all;
    std::error_code ec;
    fs::create_directories(Folder(), ec);
    std::vector<fs::path> files;
    for (fs::directory_iterator it(Folder(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (!it->is_regular_file(ec) || _wcsicmp(p.extension().c_str(), L".txt") || !_wcsicmp(p.filename().c_str(), L"README.txt")) continue;
        files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        auto patches = Parse(text, Utf8(f.filename()));
        for (const Patch& p : patches)
            for (const std::string& problem : p.problems) LOGR("Memory patches: %s [%s] %s", p.file.c_str(), p.name.c_str(), problem.c_str());
        all.insert(all.end(), patches.begin(), patches.end());
    }
    return all;
}

}  // namespace

fs::path Folder() { return app::ExeDir() / "memory_patches"; }

void Rescan() {
    auto patches = ReadFolder();
    std::lock_guard l(g_lock);
    LOGR("Memory patches: %zu in %s", patches.size(), Utf8(Folder()).c_str());
    for (const Patch& p : patches)
        if (g_set.Enabled(p.Id())) LOGR("Memory patch on: %s (%s)", p.name.c_str(), p.file.c_str());
    g_set.Load(std::move(patches), GameMap);
}

void Start() {
    if (g_started.exchange(true)) return;
    Rescan();
    std::thread(Writer).detach();
}

std::vector<Status> List() {
    std::lock_guard l(g_lock);
    std::vector<Status> out;
    for (const Patch& p : g_set.Patches()) {
        Status s{p.Id(), p.name, p.author, p.notes, p.file, p.problems};
        if (const Range* r = InCode(p, g_code)) s.code = r->name;
        s.enabled = g_set.Enabled(s.id);
        s.state = g_set.StateOf(s.id);
        out.push_back(std::move(s));
    }
    return out;
}

void SetEnabled(const std::string& id, bool on) {
    std::lock_guard l(g_lock);
    g_set.SetEnabled(id, on);
    for (const Patch& p : g_set.Patches())
        if (p.Id() == id) LOGR("Memory patch %s: %s (%s)", on ? "on" : "off", p.name.c_str(), p.file.c_str());
}

void Load(const void* value) {
    const auto& t = *static_cast<const toml::table*>(value);
    std::vector<std::string> ids;
    if (auto* on = t["memory_patches"]["enabled"].as_array())
        for (auto&& v : *on)
            if (auto s = v.value<std::string>()) ids.push_back(*s);
    std::lock_guard l(g_lock);
    g_set.SetEnabledIds(std::move(ids));
}

void Save(void* value) {
    auto& t = *static_cast<toml::table*>(value);
    toml::array on;
    std::lock_guard l(g_lock);
    for (const std::string& id : g_set.EnabledIds()) on.push_back(id);
    t.insert_or_assign("memory_patches", toml::table{{"enabled", on}});
}

}  // namespace tenchu::patches
