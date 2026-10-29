// Measures how well a PS2 pack matches the game's textures (not a test):
// workbench_probe <extracted folder> <pack folder> <cache file> [sheet folder]
// Compares the shipped rule with a tolerant one (coarser detail, colour evened
// out, a clear winner) and writes contact sheets of what the tolerant rule adds.
#include "images.h"
#include "graphics/texture_packs.h"
#include "port/connect_file.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;
namespace packs = tenchu::packs;
namespace con = tenchu::con;

struct Coarse { float l16[256], l8[64], col[768]; bool flat16 = true; };

static void Unit(float* v, int n, bool* flat) {
    double m = 0, s = 0;
    for (int i = 0; i < n; i++) m += v[i];
    m /= n;
    for (int i = 0; i < n; i++) { v[i] -= float(m); s += double(v[i]) * v[i]; }
    s = std::sqrt(s);
    if (flat) *flat = s < 1e-3;
    if (s > 1e-6) for (int i = 0; i < n; i++) v[i] = float(v[i] / s);
}
static Coarse MakeCoarse(const packs::Descriptor& d) {
    Coarse c{};
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
        c.l16[y * 16 + x] = d.lum[(2 * y) * 32 + 2 * x] + d.lum[(2 * y) * 32 + 2 * x + 1] + d.lum[(2 * y + 1) * 32 + 2 * x] + d.lum[(2 * y + 1) * 32 + 2 * x + 1];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        c.l8[y * 8 + x] = c.l16[(2 * y) * 16 + 2 * x] + c.l16[(2 * y) * 16 + 2 * x + 1] + c.l16[(2 * y + 1) * 16 + 2 * x] + c.l16[(2 * y + 1) * 16 + 2 * x + 1];
    Unit(c.l16, 256, &c.flat16);
    Unit(c.l8, 64, nullptr);
    for (int ch = 0; ch < 3; ch++) {
        float v[256];
        for (int i = 0; i < 256; i++) v[i] = d.color[i * 3 + ch];
        double m = 0, s = 0;
        for (float x : v) m += x;
        m /= 256;
        for (float x : v) s += (x - m) * (x - m);
        s = std::sqrt(s / 256) + 4;
        for (int i = 0; i < 256; i++) c.col[i * 3 + ch] = float((v[i] - m) / s);
    }
    return c;
}
static float Dot(const float* a, const float* b, int n) { double s = 0; for (int i = 0; i < n; i++) s += double(a[i]) * b[i]; return float(s); }
static float Tolerant(const Coarse& a, const Coarse& b) {
    return .45f * Dot(a.l16, b.l16, 256) + .25f * Dot(a.l8, b.l8, 64) + .30f * Dot(a.col, b.col, 768) / 768;
}

struct Tex { fs::path archive; uint32_t entry; std::string name; uint32_t w, h; packs::Descriptor d; Coarse c; };

static std::vector<uint8_t> Fit(const uint8_t* px, uint32_t w, uint32_t h, uint32_t side) {
    auto out = wb::Resize(px, w, h, side, side);
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    return out;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::vector<Tex> tex;
    std::unordered_set<uint64_t> seen;
    for (fs::recursive_directory_iterator it(argv[1]), end; it != end; ++it) {
        if (it->is_directory() && it->path().extension() == ".files") { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file() || it->path().extension() != ".con") continue;
        con::Archive a; std::string error; auto src = con::FileSource(it->path());
        if (!con::Parse(src, fs::file_size(it->path()), a, error)) continue;
        for (uint32_t i = 0; i < a.entries.size(); i++) {
            auto& e = a.entries[i];
            if (e.name.size() < 4 || _stricmp(e.name.c_str() + e.name.size() - 4, ".dds")) continue;
            std::vector<uint8_t> f, px; wb::Dds d;
            if (!con::ReadEntry(src, e, f) || !wb::ParseDds(f, d) || !wb::DecodeDds(f, d, px)) continue;
            uint64_t k = wb::GameKey(f, d);
            if (k && !seen.insert(k).second) continue;
            Tex t{it->path(), i, e.name, d.w, d.h, packs::Describe(px.data(), d.w, d.h)};
            t.c = MakeCoarse(t.d);
            tex.push_back(std::move(t));
        }
    }
    std::atomic<size_t> t{0}, dn{0}, er{0};
    packs::PackIndex index = packs::IndexFolder(argv[2], argv[3], t, dn, er);
    std::vector<Coarse> pc(index.size());
    for (size_t k = 0; k < index.size(); k++) pc[k] = MakeCoarse(index[k].d);
    std::printf("%zu unique game textures, %zu pack images\n", tex.size(), index.size());

    // per texture: the same-shape images by detail likeness, best first (top 8)
    struct Hit { int k; packs::Score s; };
    std::vector<std::vector<Hit>> top(tex.size());
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    for (unsigned th = 0; th < std::thread::hardware_concurrency(); th++) pool.emplace_back([&] {
        for (size_t i; (i = next++) < tex.size();) {
            const Tex& x = tex[i];
            if (x.d.flat) continue;
            const float aspect = std::log(float(x.w) / x.h);
            std::vector<Hit> all;
            for (size_t k = 0; k < index.size(); k++) {
                const auto& img = index[k];
                if (!img.ok || img.d.flat || std::fabs(std::log(float(img.d.w) / img.d.h) - aspect) > .05f) continue;
                all.push_back({int(k), packs::Compare(x.d, img.d)});
            }
            size_t n = std::min<size_t>(8, all.size());
            std::partial_sort(all.begin(), all.begin() + n, all.end(), [](const Hit& a, const Hit& b) { return a.s.ncc > b.s.ncc; });
            all.resize(n);
            top[i] = std::move(all);
        }
    });
    for (auto& p : pool) p.join();
    // the runner-up that is a different picture (pack folders repeat images)
    auto margin = [&](const std::vector<Hit>& h) {
        for (size_t j = 1; j < h.size(); j++)
            if (packs::Compare(index[h[0].k].d, index[h[j].k].d).ncc < .97f) return h[0].s.ncc - h[j].s.ncc;
        return 1.f;
    };
    (void)margin;
    // one to one: images that are already a sure match are taken; of the rest, a pair is
    // mutual when the image's best unsure game texture is this one
    size_t sure = 0;
    std::unordered_set<int> used;
    for (size_t i = 0; i < tex.size(); i++)
        if (!top[i].empty() && packs::Judge(top[i][0].s) == packs::Verdict::Accept) { used.insert(top[i][0].k); sure++; }
    std::vector<int> pick(tex.size(), -1);
    std::map<int, std::pair<float, size_t>> rev;
    for (size_t i = 0; i < tex.size(); i++) {
        if (top[i].empty() || packs::Judge(top[i][0].s) == packs::Verdict::Accept) continue;
        for (size_t j = 0; j < top[i].size(); j++)
            if (!used.count(top[i][j].k)) { pick[i] = int(j); break; }
        if (pick[i] < 0) continue;
        auto& r = rev[top[i][pick[i]].k];
        if (top[i][pick[i]].s.ncc > r.first) r = {top[i][pick[i]].s.ncc, i};
    }
    std::map<int, std::vector<size_t>> band;  // 93: [.93,1) 90: [.90,.93) 85: [.85,.90) 80: [.80,.85)
    std::map<int, size_t> loose, mutual;
    for (size_t i = 0; i < tex.size(); i++) {
        if (pick[i] < 0) continue;
        const Hit h = top[i][pick[i]];
        const int b = h.s.ncc >= .93f ? 93 : h.s.ncc >= .90f ? 90 : h.s.ncc >= .85f ? 85 : h.s.ncc >= .80f ? 80 : 0;
        if (!b) continue;
        loose[b]++;
        if (rev[h.k].second != i) continue;
        mutual[b]++;
        if (h.s.color <= .12f && h.s.region <= .20f) {
            std::swap(top[i][0], top[i][pick[i]]);
            band[b].push_back(i);
        }
    }
    std::printf("sure %zu, pack images taken by them %zu\n", sure, used.size());
    for (int b : {93, 90, 85, 80})
        std::printf("band %d: %zu unsure, %zu mutual best, %zu mutual with colour close\n", b, loose[b], mutual[b], band[b].size());
    if (argc < 5) return 0;
    std::mt19937 rng(7);
    const uint32_t side = 128, cols = 4, rows_n = 6, pw = side * 2 + 8, ph = side + 8;
    for (int b : {93, 90, 85, 80}) {
        auto list = band[b];
        std::shuffle(list.begin(), list.end(), rng);
        list.resize(std::min<size_t>(list.size(), cols * rows_n));
        std::vector<uint8_t> sheet(size_t(pw * cols) * ph * rows_n * 4, 0);
        for (size_t n = 0; n < list.size(); n++) {
            const Tex& x = tex[list[n]];
            const Hit& h = top[list[n]][0];
            con::Archive a; std::string error; auto src = con::FileSource(x.archive);
            con::Parse(src, fs::file_size(x.archive), a, error);
            std::vector<uint8_t> f, px, pp; wb::Dds d; uint32_t w, hh;
            con::ReadEntry(src, a.entries[x.entry], f); wb::ParseDds(f, d); wb::DecodeDds(f, d, px);
            auto left = Fit(px.data(), d.w, d.h, side);
            packs::ReadImage(fs::path(argv[2]) / fs::path(std::u8string(index[h.k].file.begin(), index[h.k].file.end())), pp, w, hh);
            auto right = Fit(pp.data(), w, hh, side);
            const uint32_t ox = uint32_t(n % cols) * pw, oy = uint32_t(n / cols) * ph;
            for (uint32_t y = 0; y < side; y++) {
                std::memcpy(&sheet[((size_t(oy + y) * pw * cols) + ox) * 4], &left[size_t(y) * side * 4], side * 4);
                std::memcpy(&sheet[((size_t(oy + y) * pw * cols) + ox + side + 2) * 4], &right[size_t(y) * side * 4], side * 4);
            }
            std::printf("band %d #%zu: %s -> %s (ncc %.3f colour %.3f region %.3f margin %.3f)\n", b, n, x.name.c_str(), index[h.k].file.c_str(),
                        h.s.ncc, h.s.color, h.s.region, margin(top[list[n]]));
        }
        char name[64];
        std::snprintf(name, sizeof(name), "rule_%d.png", b);
        wb::WritePng(fs::path(argv[4]) / name, sheet.data(), pw * cols, ph * rows_n);
    }
    return 0;
}
