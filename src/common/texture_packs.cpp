#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "graphics/texture_packs.h"
#include "app/app.h"
#include "core/log.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace tenchu::packs {
namespace {
namespace fs = std::filesystem;

constexpr float kAspect = .05f;          // |log| of the width/height ratio difference
constexpr size_t kShortlist = 8;
constexpr uint32_t kMinSide = 16, kThumb = 256;

using Image = PackImage;
using Index = PackIndex;

struct Pack {
    std::string name;
    fs::path dir;
    bool enabled = true;
    std::shared_ptr<const Index> index;  // published once complete
    std::unordered_map<std::string, int> by_file;  // index position of each image, with index
    std::unordered_map<uint64_t, int> by_key;      // images named after a game texture key (edited dumps), with index
    PackMatches matches;                 // the pack's own rfd_matches.toml
    bool indexing = false;
    std::atomic<size_t> total{0}, done{0}, errors{0};
};

struct Thumb {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
};

struct Candidate {
    int image = -1;
    Score s;
    Verdict v = Verdict::None;
};

struct Seen {
    std::shared_ptr<const Descriptor> d;     // null: not decoded or too small to match
    std::shared_ptr<const Thumb> thumb;      // kept while a pack is still indexing, for comparisons
    std::map<std::string, Candidate> best;   // per indexed pack
    fs::path compare;
};

std::mutex g_lock;
std::vector<std::unique_ptr<Pack>> g_packs;   // priority order
std::vector<std::string> g_order;             // saved order, packs not present included
std::set<std::string> g_disabled;
std::unordered_set<uint64_t> g_wanted;
std::unordered_map<uint64_t, Seen> g_seen;
std::unordered_map<uint64_t, std::string> g_approved;  // key -> "pack|file"
std::unordered_set<uint64_t> g_blocked;
std::unordered_map<uint64_t, fs::path> g_exact;        // textures\replacement images the loader used, by key
std::atomic<uint32_t> g_revision{1};
bool g_choices_loaded = false;

fs::path TexturesDir() { return app::ExeDir() / "textures"; }
fs::path ChoicesFile() { return TexturesDir() / "pack_choices.toml"; }
fs::path ReviewDir() { return TexturesDir() / "review"; }
std::string Utf8(const fs::path& p) { auto s = p.u8string(); return {s.begin(), s.end()}; }
fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string Hex(uint64_t key) { char b[17]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)key); return b; }
void Bump() { g_revision.fetch_add(1, std::memory_order_relaxed); }

Pack* FindPack(const std::string& name) {
    for (auto& p : g_packs) if (p->name == name) return p.get();
    return nullptr;
}

// WIC per thread: index workers, the texture loader's worker
IWICImagingFactory* Wic() {
    thread_local ComPtr<IWICImagingFactory> wic;
    if (!wic) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    }
    return wic.Get();
}

Thumb MakeThumb(const uint8_t* px, uint32_t w, uint32_t h) {
    Thumb t;
    float scale = std::min(1.f, float(kThumb) / float(std::max(w, h)));
    t.w = std::max(1u, uint32_t(w * scale)); t.h = std::max(1u, uint32_t(h * scale));
    std::vector<float> f(size_t(t.w) * t.h * 4);
    Box(px, w, h, t.w, t.h, f.data(), 0);
    t.px.resize(f.size());
    for (size_t i = 0; i < f.size(); i++) t.px[i] = uint8_t(std::lround(f[i]));
    return t;
}

Candidate Best(const Descriptor& d, const Index& index) {
    Candidate best;
    if (d.flat) return best;
    const float aspect = std::log(float(d.w) / float(d.h));
    std::vector<std::pair<float, int>> shortlist;
    for (size_t i = 0; i < index.size(); i++) {
        const Image& img = index[i];
        if (!img.ok || img.key || img.d.flat || std::fabs(std::log(float(img.d.w) / float(img.d.h)) - aspect) > kAspect) continue;
        shortlist.emplace_back(Compare(d, img.d).color, int(i));
    }
    size_t n = std::min(kShortlist, shortlist.size());
    std::partial_sort(shortlist.begin(), shortlist.begin() + n, shortlist.end());
    for (size_t i = 0; i < n; i++) {
        Candidate c{shortlist[i].second, Compare(d, index[shortlist[i].second].d)};
        c.v = Judge(c.s);
        if (best.image < 0 || int(c.v) > int(best.v) || (c.v == best.v && c.s.ncc > best.s.ncc)) best = c;
    }
    return best;
}

// xbox thumbnail left, pack image right, both 256 pixels high
void WriteCompare(const fs::path& out, const Thumb& xbox, const fs::path& pack_file) {
    std::vector<uint8_t> pack;
    uint32_t pw = 0, ph = 0;
    if (!ReadImage(pack_file, pack, pw, ph)) return;
    const uint32_t H = kThumb;
    uint32_t xw = std::max(1u, xbox.w * H / xbox.h), qw = std::max(1u, pw * H / ph), W = xw + 8 + qw;
    std::vector<uint8_t> canvas(size_t(W) * H * 4, 0);
    for (size_t i = 0; i < canvas.size(); i += 4) { canvas[i] = 64; canvas[i + 2] = 64; canvas[i + 3] = 255; }
    auto blit = [&](const uint8_t* src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t ox) {
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < dw; x++) {
                const uint8_t* s = src + (size_t(y * sh / H) * sw + x * sw / dw) * 4;
                uint8_t* d = &canvas[(size_t(y) * W + ox + x) * 4];
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            }
    };
    blit(xbox.px.data(), xbox.w, xbox.h, xw, 0);
    blit(pack.data(), pw, ph, qw, xw + 8);
    IWICImagingFactory* wic = Wic();
    ComPtr<IWICStream> file;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    HRESULT hr = wic ? wic->CreateStream(&file) : E_FAIL;
    if (SUCCEEDED(hr)) hr = file->InitializeFromFilename(out.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(file.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(W, H);
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WritePixels(H, W * 4, UINT(canvas.size()), canvas.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
}

// Matches one key against the given packs, outside the lock, then stores the
// results and writes comparison images for review candidates.
void MatchAndStore(uint64_t key, std::shared_ptr<const Descriptor> d, std::shared_ptr<const Thumb> thumb,
                   const std::vector<std::pair<std::string, std::pair<fs::path, std::shared_ptr<const Index>>>>& targets) {
    std::vector<std::pair<std::string, Candidate>> found;
    for (auto& [name, target] : targets) found.emplace_back(name, d ? Best(*d, *target.second) : Candidate{});
    fs::path compare;
    for (size_t i = 0; i < found.size() && thumb; i++)
        if (found[i].second.v == Verdict::Review && compare.empty()) {
            compare = ReviewDir() / (Hex(key) + ".png");
            WriteCompare(compare, *thumb, targets[i].second.first / FromUtf8((*targets[i].second.second)[found[i].second.image].file));
        }
    std::lock_guard l(g_lock);
    Seen& s = g_seen[key];
    for (auto& [name, c] : found) s.best[name] = c;
    if (!compare.empty()) s.compare = compare;
}

using Targets = std::vector<std::pair<std::string, std::pair<fs::path, std::shared_ptr<const Index>>>>;
Targets Published() {
    Targets t;
    for (auto& p : g_packs) if (p->index) t.push_back({p->name, {p->dir, p->index}});
    return t;
}
bool AnyIndexing() {
    for (auto& p : g_packs) if (p->indexing) return true;
    return false;
}

// a pack's worker: list, reuse cached descriptors, describe the rest, publish, match what was seen
void IndexPack(Pack* pack) {
    const fs::path dir = pack->dir, cache = TexturesDir() / "cache" / CacheName(pack->name);
    auto index = std::make_shared<Index>(IndexFolder(dir, cache, pack->total, pack->done, pack->errors));
    for (Image& img : *index) img.key = KeyInFileName(FromUtf8(img.file).stem().u8string());
    // match textures already seen against the new index
    std::vector<std::tuple<uint64_t, std::shared_ptr<const Descriptor>, std::shared_ptr<const Thumb>>> work;
    Targets target{{pack->name, {dir, index}}};
    {
        std::lock_guard l(g_lock);
        pack->index = index;
        pack->by_file.clear();
        pack->by_key.clear();
        for (size_t i = 0; i < index->size(); i++) {
            pack->by_file[(*index)[i].file] = int(i);
            if ((*index)[i].key && (*index)[i].ok) pack->by_key[(*index)[i].key] = int(i);
        }
        for (auto& [key, s] : g_seen)
            if (!s.best.count(pack->name)) work.emplace_back(key, s.d, s.thumb);
    }
    for (auto& [key, d, thumb] : work) MatchAndStore(key, d, thumb, target);
    {
        std::lock_guard l(g_lock);
        pack->indexing = false;
        if (!AnyIndexing())
            for (auto& [key, s] : g_seen) s.thumb.reset();
    }
    LOGR("texture packs: %s ready, %zu images, %zu unreadable", pack->name.c_str(), index->size(), pack->errors.load());
    Bump();
}

void StartIndex(Pack* pack) {  // under g_lock
    if (pack->index || pack->indexing || !pack->enabled) return;
    pack->indexing = true;
    pack->done = 0; pack->errors = 0;
    std::thread(IndexPack, pack).detach();
}

void LoadChoices() {
    g_choices_loaded = true;
    try {
        if (!fs::exists(ChoicesFile())) return;
        auto t = toml::parse_file(Utf8(ChoicesFile()));
        if (auto* a = t["approved"].as_table())
            for (auto&& [k, v] : *a) if (auto s = v.value<std::string>()) g_approved[std::strtoull(std::string(k.str()).c_str(), nullptr, 16)] = *s;
        if (auto* b = t["blocked"].as_array())
            for (auto&& v : *b) if (auto s = v.value<std::string>()) g_blocked.insert(std::strtoull(s->c_str(), nullptr, 16));
    } catch (const std::exception& e) {
        LOGE("texture packs: %s not read: %s", Utf8(ChoicesFile()).c_str(), e.what());
    }
}

void SaveChoices() {  // under g_lock
    toml::table approved;
    for (auto& [k, v] : g_approved) approved.insert_or_assign(Hex(k), v);
    toml::array blocked;
    for (uint64_t k : g_blocked) blocked.push_back(Hex(k));
    toml::table t{{"approved", approved}, {"blocked", blocked}};
    std::error_code ec;
    fs::create_directories(TexturesDir(), ec);
    fs::path temp = ChoicesFile(); temp += L".tmp";
    { std::ofstream o(temp); o << "# F6 > Textures choices: approved matches and blocked textures\n" << t << "\n"; }
    MoveFileExW(temp.c_str(), ChoicesFile().c_str(), MOVEFILE_REPLACE_EXISTING);
}

// the replacement the enabled packs choose for a key; Pending while a pack in front is not ready
Result Choose(uint64_t key, const Seen* s, Pack** pack, int* image, float* ncc, float* color) {
    if (g_blocked.count(key)) return Result::None;
    if (auto a = g_approved.find(key); a != g_approved.end()) {
        auto bar = a->second.find('|');
        Pack* p = FindPack(a->second.substr(0, bar));
        if (p && p->enabled) {
            if (!p->index) return Result::Pending;
            const std::string file = a->second.substr(bar + 1);
            for (size_t i = 0; i < p->index->size(); i++)
                if ((*p->index)[i].file == file) { *pack = p; *image = int(i); *ncc = 1; *color = 0; return Result::Found; }
        }
    }
    // an edited dump keeps its key in its name: it replaces exactly that texture, before any pack's matching
    for (auto& p : g_packs) {
        if (!p->enabled) continue;
        if (auto k = p->by_key.find(key); k != p->by_key.end()) {
            *pack = p.get(); *image = k->second; *ncc = 1; *color = 0;
            return Result::Found;
        }
    }
    for (auto& p : g_packs) {
        if (!p->enabled || p->matches.skip.count(key)) continue;
        if (auto m = p->matches.use.find(key); m != p->matches.use.end()) {
            if (!p->index) return Result::Pending;
            if (auto f = p->by_file.find(m->second); f != p->by_file.end()) {
                *pack = p.get(); *image = f->second; *ncc = 1; *color = 0;
                return Result::Found;
            }
        }
        if (!s) return g_wanted.count(key) ? Result::Pending : Result::None;
        auto c = s->best.find(p->name);
        if (c == s->best.end()) return p->indexing || p->index ? Result::Pending : Result::None;
        if (c->second.v == Verdict::Accept) {
            *pack = p.get(); *image = c->second.image; *ncc = c->second.s.ncc; *color = c->second.s.color;
            return Result::Found;
        }
    }
    return Result::None;
}

}  // namespace

uint64_t KeyInFileName(const std::u8string& stem) {
    size_t start = 0;
    while (start <= stem.size()) {
        size_t end = std::min(stem.find(u8'_', start), stem.size());
        std::u8string part = stem.substr(start, end - start);
        if (part.size() == 16 && part.find_first_not_of(u8"0123456789abcdefABCDEF") == std::u8string::npos)
            return std::strtoull(std::string(part.begin(), part.end()).c_str(), nullptr, 16);
        start = end + 1;
    }
    return 0;
}

void NoteExact(uint64_t key, const fs::path& file) {
    std::lock_guard l(g_lock);
    g_exact[key] = file;
}

void ClearExact() {
    std::lock_guard l(g_lock);
    g_exact.clear();
}

// Texture loader side

bool Active() {
    if (!app::Get().custom_textures) return false;
    std::lock_guard l(g_lock);
    for (auto& p : g_packs) if (p->enabled) return true;
    return false;
}

uint32_t Revision() { return g_revision.load(std::memory_order_relaxed); }

bool Wants(uint64_t key) {
    if (!Active()) return false;
    std::lock_guard l(g_lock);
    return g_wanted.insert(key).second;
}

void Submit(uint64_t key, uint32_t w, uint32_t h, std::vector<uint8_t> bgra) {
    std::shared_ptr<const Descriptor> d;
    std::shared_ptr<const Thumb> thumb;
    if (w >= kMinSide && h >= kMinSide && bgra.size() >= size_t(w) * h * 4) {
        d = std::make_shared<Descriptor>(Describe(bgra.data(), w, h));
        thumb = std::make_shared<Thumb>(MakeThumb(bgra.data(), w, h));
    }
    Targets targets;
    {
        std::lock_guard l(g_lock);
        Seen& s = g_seen[key];
        s.d = d;
        if (AnyIndexing()) s.thumb = thumb;
        targets = Published();
    }
    MatchAndStore(key, d, thumb, targets);
}

Result Find(uint64_t key, fs::path& path, bool& ps2_alpha) {
    std::lock_guard l(g_lock);
    auto it = g_seen.find(key);
    Pack* pack = nullptr;
    int image = -1;
    float ncc = 0, color = 0;
    Result r = Choose(key, it == g_seen.end() ? nullptr : &it->second, &pack, &image, &ncc, &color);
    if (r != Result::Found) return r;
    const Image& img = (*pack->index)[image];
    path = pack->dir / FromUtf8(img.file);
    ps2_alpha = img.d.ps2_alpha && !pack->by_key.count(key);  // an Xbox dump's alpha is the Xbox's
    return r;
}

// Settings and F6 > Textures

fs::path PacksDir() { return TexturesDir() / "packs"; }

void Rescan() {
    std::error_code ec;
    fs::create_directories(PacksDir(), ec);
    std::vector<std::pair<std::string, fs::path>> found;
    for (fs::directory_iterator it(PacksDir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) found.emplace_back(Utf8(it->path().filename()), it->path());
    std::lock_guard l(g_lock);
    if (!g_choices_loaded) LoadChoices();
    std::vector<std::unique_ptr<Pack>> next;
    auto rank = [&](const std::string& n) { auto i = std::find(g_order.begin(), g_order.end(), n); return i == g_order.end() ? g_order.size() : size_t(i - g_order.begin()); };
    std::sort(found.begin(), found.end(), [&](auto& a, auto& b) { return rank(a.first) != rank(b.first) ? rank(a.first) < rank(b.first) : a.first < b.first; });
    for (auto& [name, dir] : found) {
        auto old = std::find_if(g_packs.begin(), g_packs.end(), [&](auto& p) { return p && p->name == name; });
        if (old != g_packs.end()) { next.push_back(std::move(*old)); continue; }
        auto p = std::make_unique<Pack>();
        p->name = name; p->dir = dir; p->enabled = !g_disabled.count(name);
        next.push_back(std::move(p));
    }
    // packs whose folder went away stay alive if still indexing; their worker holds the pointer
    for (auto& p : g_packs) if (p && p->indexing) next.push_back(std::move(p)), next.back()->enabled = false;
    g_packs = std::move(next);
    for (auto& p : g_packs) {
        std::string error;
        if (!ReadMatches(p->dir, p->matches, &error) && !error.empty())
            LOGE("texture packs: %s/%s not read: %s", p->name.c_str(), kMatchesFile, error.c_str());
        else if (!p->matches.use.empty() || !p->matches.skip.empty())
            LOGR("texture packs: %s has %zu matches and %zu skips of its own", p->name.c_str(), p->matches.use.size(), p->matches.skip.size());
        if (std::find(g_order.begin(), g_order.end(), p->name) == g_order.end()) g_order.push_back(p->name);
        StartIndex(p.get());
    }
    Bump();  // new packs, and matches files may have changed
}

std::vector<PackStatus> Packs() {
    std::vector<PackStatus> out;
    std::lock_guard l(g_lock);
    for (auto& p : g_packs) out.push_back({p->name, p->enabled, p->indexing, p->total.load(), p->done.load(), p->errors.load()});
    return out;
}

void SetEnabled(const std::string& name, bool enabled) {
    std::lock_guard l(g_lock);
    Pack* p = FindPack(name);
    if (!p || p->enabled == enabled) return;
    p->enabled = enabled;
    if (enabled) g_disabled.erase(name); else g_disabled.insert(name);
    StartIndex(p);
    Bump();
}

void Move(const std::string& name, int delta) {
    std::lock_guard l(g_lock);
    auto it = std::find_if(g_packs.begin(), g_packs.end(), [&](auto& p) { return p->name == name; });
    if (it == g_packs.end()) return;
    auto to = it + delta;
    if (to < g_packs.begin() || to >= g_packs.end()) return;
    std::iter_swap(it, to);
    g_order.clear();
    for (auto& p : g_packs) g_order.push_back(p->name);
    Bump();
}

void Reindex(const std::string& name) {
    std::lock_guard l(g_lock);
    Pack* p = FindPack(name);
    if (!p || p->indexing) return;
    std::error_code ec;
    fs::remove(TexturesDir() / "cache" / CacheName(name), ec);
    p->index.reset();
    for (auto& [key, s] : g_seen) s.best.erase(name);
    StartIndex(p);
    Bump();
}

Stats Counts() {
    Stats st;
    std::lock_guard l(g_lock);
    for (auto& [key, file] : g_exact) {
        if (!g_seen.count(key)) st.seen++;
        st.matched++;
    }
    for (auto& [key, s] : g_seen) {
        if (g_exact.count(key)) continue;
        st.seen++;
        Pack* pack = nullptr;
        int image = -1;
        float ncc = 0, color = 0;
        Result r = Choose(key, &s, &pack, &image, &ncc, &color);
        if (r == Result::Found) { st.matched++; continue; }
        bool review = false;
        if (r == Result::None && !g_blocked.count(key))
            for (auto& p : g_packs) if (p->enabled) if (auto c = s.best.find(p->name); c != s.best.end() && c->second.v == Verdict::Review) review = true;
        if (review) st.review++; else if (r == Result::None) st.unmatched++;
    }
    return st;
}

std::vector<Entry> Replaced(size_t max) {
    std::vector<Entry> out;
    std::lock_guard l(g_lock);
    for (auto& [key, file] : g_exact) out.push_back({key, "replacement", Utf8(file.filename()), 1, 0, {}, file});
    for (auto& [key, s] : g_seen) {
        if (g_exact.count(key)) continue;
        Pack* pack = nullptr;
        int image = -1;
        float ncc = 0, color = 0;
        if (Choose(key, &s, &pack, &image, &ncc, &color) != Result::Found) continue;
        const std::string& file = (*pack->index)[image].file;
        out.push_back({key, pack->name, file, ncc, color, s.compare, pack->dir / FromUtf8(file)});
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.file < b.file; });
    if (out.size() > max) out.resize(max);
    return out;
}

std::vector<Entry> ReviewList(size_t max) {
    std::vector<Entry> out;
    std::lock_guard l(g_lock);
    for (auto& [key, s] : g_seen) {
        if (g_blocked.count(key) || out.size() >= max) continue;
        Pack* pack = nullptr;
        int image = -1;
        float ncc = 0, color = 0;
        if (Choose(key, &s, &pack, &image, &ncc, &color) != Result::None) continue;
        for (auto& p : g_packs) {
            auto c = s.best.find(p->name);
            if (!p->enabled || !p->index || c == s.best.end() || c->second.v != Verdict::Review) continue;
            out.push_back({key, p->name, (*p->index)[c->second.image].file, c->second.s.ncc, c->second.s.color, s.compare});
            break;
        }
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.ncc > b.ncc; });
    return out;
}

void Approve(uint64_t key) {
    std::lock_guard l(g_lock);
    auto it = g_seen.find(key);
    if (it == g_seen.end()) return;
    for (auto& p : g_packs) {
        auto c = it->second.best.find(p->name);
        if (!p->enabled || !p->index || c == it->second.best.end() || c->second.v == Verdict::None) continue;
        g_approved[key] = p->name + "|" + (*p->index)[c->second.image].file;
        g_blocked.erase(key);
        SaveChoices();
        Bump();
        return;
    }
}

void Block(uint64_t key) {
    std::lock_guard l(g_lock);
    g_blocked.insert(key);
    g_approved.erase(key);
    SaveChoices();
    Bump();
}

void ClearChoices() {
    std::lock_guard l(g_lock);
    g_blocked.clear();
    g_approved.clear();
    SaveChoices();
    Bump();
}

void Load(const void* value) {
    const auto& t = *static_cast<const toml::table*>(value);
    std::lock_guard l(g_lock);
    g_order.clear();
    g_disabled.clear();
    if (auto* order = t["textures"]["pack_order"].as_array())
        for (auto&& v : *order) if (auto s = v.value<std::string>()) g_order.push_back(*s);
    if (auto* off = t["textures"]["disabled_packs"].as_array())
        for (auto&& v : *off) if (auto s = v.value<std::string>()) g_disabled.insert(*s);
}

void Save(void* value) {
    auto& t = *static_cast<toml::table*>(value);
    toml::array order, off;
    std::lock_guard l(g_lock);
    for (auto& name : g_order) order.push_back(name);
    for (auto& name : g_disabled) off.push_back(name);
    t.insert_or_assign("textures", toml::table{{"pack_order", order}, {"disabled_packs", off}});
}

}  // namespace tenchu::packs
