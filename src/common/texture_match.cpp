#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "graphics/texture_packs.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <thread>
#include <toml++/toml.hpp>
#include <vector>

using Microsoft::WRL::ComPtr;

// The matching core, shared by the game (texture_packs.cpp) and RFD Switchblade.
namespace tenchu::packs {
namespace {
// Calibrated on the WoH pack against 329 captured Xbox textures. Recoloured
// variants fail on colour; atlases this version redrew in one panel fail on the
// worst 4x4 region of the colour grid and go to review instead.
constexpr float kAcceptNcc = .93f, kAcceptColor = .10f, kAcceptRegion = .15f, kReviewNcc = .80f, kReviewColor = .25f;
constexpr uint32_t kIndexMagic = 0x58495054, kIndexVersion = 2;  // "TPIX"
namespace fs = std::filesystem;

std::string Utf8(const fs::path& p) { auto s = p.u8string(); return {s.begin(), s.end()}; }
fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// WIC per thread: index workers, the texture loader's worker
IWICImagingFactory* Wic() {
    thread_local ComPtr<IWICImagingFactory> wic;
    if (!wic) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    }
    return wic.Get();
}

template <class T> void Put(std::ofstream& o, const T& v) { o.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
template <class T> bool Get(std::ifstream& i, T& v) { return bool(i.read(reinterpret_cast<char*>(&v), sizeof(v))); }

std::map<std::string, PackImage> ReadCache(const fs::path& path) {
    std::map<std::string, PackImage> out;
    std::ifstream in(path, std::ios::binary);
    uint32_t magic = 0, version = 0, count = 0;
    if (!Get(in, magic) || !Get(in, version) || !Get(in, count) || magic != kIndexMagic || version != kIndexVersion) return out;
    for (uint32_t i = 0; i < count; i++) {
        PackImage img;
        uint16_t len = 0;
        uint8_t flags = 0;
        if (!Get(in, len)) break;
        img.file.resize(len);
        if (!in.read(img.file.data(), len) || !Get(in, img.size) || !Get(in, img.time) || !Get(in, img.d.w) || !Get(in, img.d.h) ||
            !Get(in, flags) || !Get(in, img.d.color) || !Get(in, img.d.lum)) break;
        img.ok = flags & 1; img.d.flat = flags & 2; img.d.ps2_alpha = flags & 4;
        out[img.file] = std::move(img);
    }
    return out;
}

void WriteCache(const fs::path& path, const PackIndex& index) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temp = path; temp += L".tmp";
    {
        std::ofstream o(temp, std::ios::binary | std::ios::trunc);
        Put(o, kIndexMagic); Put(o, kIndexVersion); Put(o, uint32_t(index.size()));
        for (const PackImage& img : index) {
            Put(o, uint16_t(img.file.size())); o.write(img.file.data(), img.file.size());
            Put(o, img.size); Put(o, img.time); Put(o, img.d.w); Put(o, img.d.h);
            Put(o, uint8_t((img.ok ? 1 : 0) | (img.d.flat ? 2 : 0) | (img.d.ps2_alpha ? 4 : 0)));
            Put(o, img.d.color); Put(o, img.d.lum);
        }
        if (!o) return;
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
}  // namespace

// box filter into an out_w x out_h BGRA float image; alpha_scale > 0 premultiplies
// colour by min(1, alpha * alpha_scale), so cut-outs compare as empty on both sides
void Box(const uint8_t* px, uint32_t w, uint32_t h, uint32_t out_w, uint32_t out_h, float* out, float alpha_scale) {
    for (uint32_t y = 0; y < out_h; y++) {
        uint32_t y0 = y * h / out_h, y1 = std::max(y0 + 1, (y + 1) * h / out_h);
        for (uint32_t x = 0; x < out_w; x++) {
            uint32_t x0 = x * w / out_w, x1 = std::max(x0 + 1, (x + 1) * w / out_w);
            double sum[4] = {};
            for (uint32_t yy = y0; yy < y1; yy++) {
                const uint8_t* row = px + (size_t(yy) * w + x0) * 4;
                for (uint32_t xx = x0; xx < x1; xx++, row += 4) {
                    double k = alpha_scale > 0 ? std::min(1.f, row[3] * alpha_scale) : 1;
                    for (int c = 0; c < 3; c++) sum[c] += row[c] * k;
                    sum[3] += row[3];
                }
            }
            double n = double(y1 - y0) * (x1 - x0);
            for (int c = 0; c < 4; c++) out[(y * out_w + x) * 4 + c] = float(sum[c] / n);
        }
    }
}

Descriptor Describe(const uint8_t* bgra, uint32_t w, uint32_t h) {
    Descriptor d;
    d.w = w; d.h = h;
    if (!w || !h) return d;
    // PS2 alpha: 0x80 is opaque, so the commonest visible alpha sits near it
    uint32_t hist[256] = {};
    for (size_t i = 3; i < size_t(w) * h * 4; i += 4) hist[bgra[i]]++;
    int mode = 1;
    for (int a = 1; a < 256; a++) if (hist[a] > hist[mode]) mode = a;
    int top = 255;
    while (top > 0 && !hist[top]) top--;
    d.ps2_alpha = hist[mode] && ((mode >= 120 && mode <= 136) || (top >= 64 && top <= 136));
    // all-zero alpha is a format without it; all-opaque needs no weighting
    const bool weighted = top > 0 && hist[255] != size_t(w) * h;
    std::vector<float> box(32 * 32 * 4);
    Box(bgra, w, h, 32, 32, box.data(), weighted ? (d.ps2_alpha ? 1.f / 128 : 1.f / 255) : 0);
    for (uint32_t y = 0; y < 16; y++)
        for (uint32_t x = 0; x < 16; x++)
            for (int c = 0; c < 3; c++) {
                float sum = 0;
                for (uint32_t yy = 0; yy < 2; yy++)
                    for (uint32_t xx = 0; xx < 2; xx++) sum += box[((y * 2 + yy) * 32 + x * 2 + xx) * 4 + c];
                d.color[(y * 16 + x) * 3 + c] = uint8_t(std::lround(sum / 4));
            }
    double mean = 0;
    for (int i = 0; i < 1024; i++) {
        d.lum[i] = .114f * box[i * 4] + .587f * box[i * 4 + 1] + .299f * box[i * 4 + 2];
        mean += d.lum[i];
    }
    mean /= 1024;
    double norm = 0;
    for (float& v : d.lum) { v -= float(mean); norm += double(v) * v; }
    norm = std::sqrt(norm);
    d.flat = norm / 32 < 3;  // luminance standard deviation under 3 of 255
    if (!d.flat) for (float& v : d.lum) v = float(v / norm);
    return d;
}

Score Compare(const Descriptor& a, const Descriptor& b) {
    Score s;
    double sum = 0;
    for (size_t i = 0; i < sizeof(a.color); i++) { double v = (double(a.color[i]) - b.color[i]) / 255; sum += v * v; }
    s.color = float(std::sqrt(sum / sizeof(a.color)));
    s.region = 0;
    for (int by = 0; by < 16; by += 4)
        for (int bx = 0; bx < 16; bx += 4) {
            double block = 0;
            for (int y = by; y < by + 4; y++)
                for (int i = (y * 16 + bx) * 3; i < (y * 16 + bx + 4) * 3; i++) { double v = (double(a.color[i]) - b.color[i]) / 255; block += v * v; }
            s.region = std::max(s.region, float(std::sqrt(block / 48)));
        }
    if (!a.flat && !b.flat) {
        double dot = 0;
        for (int i = 0; i < 1024; i++) dot += double(a.lum[i]) * b.lum[i];
        s.ncc = float(dot);
    }
    return s;
}

Verdict Judge(const Score& s) {
    if (s.ncc >= kAcceptNcc && s.color <= kAcceptColor && s.region <= kAcceptRegion) return Verdict::Accept;
    if (s.ncc >= kReviewNcc && s.color <= kReviewColor) return Verdict::Review;
    return Verdict::None;
}

void ScaleAlpha(uint8_t* bgra, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) bgra[i * 4 + 3] = uint8_t(std::min(255u, (bgra[i * 4 + 3] * 255u + 64) / 128));
}

bool ReadImage(const fs::path& path, std::vector<uint8_t>& px, uint32_t& w, uint32_t& h) {
    IWICImagingFactory* wic = Wic();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapSource> bgra;
    HRESULT hr = wic ? wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder) : E_FAIL;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &bgra);
    UINT uw = 0, uh = 0;
    if (SUCCEEDED(hr)) hr = bgra->GetSize(&uw, &uh);
    if (FAILED(hr) || !uw || !uh || uw > 16384 || uh > 16384) return false;
    px.resize(size_t(uw) * uh * 4);
    if (FAILED(bgra->CopyPixels(nullptr, uw * 4, UINT(px.size()), px.data()))) return false;
    w = uw; h = uh;
    return true;
}

std::string CacheName(const std::string& pack) {
    std::string s;
    for (unsigned char c : pack) s += std::isalnum(c) || c == '-' || c == '_' ? char(c) : '_';
    return s + ".idx";
}

PackIndex IndexFolder(const fs::path& dir, const fs::path& cache, std::atomic<size_t>& total, std::atomic<size_t>& done,
                      std::atomic<size_t>& errors) {
    PackIndex index;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::follow_directory_symlink, ec), end; !ec && it != end; it.increment(ec)) {
        // PNG, and DDS for edited Xbox dumps (WIC decodes DXT1/3/5 and BGRA)
        const fs::path ext = it->path().extension();
        if (!it->is_regular_file(ec) || (_wcsicmp(ext.c_str(), L".png") && _wcsicmp(ext.c_str(), L".dds"))) continue;
        PackImage img;
        img.file = Utf8(it->path().lexically_relative(dir));
        img.size = it->file_size(ec);
        img.time = it->last_write_time(ec).time_since_epoch().count();
        index.push_back(std::move(img));
    }
    std::sort(index.begin(), index.end(), [](const PackImage& a, const PackImage& b) { return a.file < b.file; });
    total = index.size();
    auto cached = ReadCache(cache);
    std::vector<size_t> todo;
    for (size_t i = 0; i < index.size(); i++) {
        PackImage& img = index[i];
        auto c = cached.find(img.file);
        if (c != cached.end() && c->second.size == img.size && c->second.time == img.time) img = std::move(c->second);
        else todo.push_back(i);
    }
    done = index.size() - todo.size();
    if (!todo.empty()) {
        std::atomic<size_t> next{0};
        unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2, 2u, 8u);
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; t++) pool.emplace_back([&] {
            for (size_t n; (n = next++) < todo.size();) {
                PackImage& img = index[todo[n]];
                std::vector<uint8_t> px;
                uint32_t w = 0, h = 0;
                img.ok = ReadImage(dir / FromUtf8(img.file), px, w, h);
                if (img.ok) img.d = Describe(px.data(), w, h);
                else errors++;
                done++;
            }
        });
        for (auto& t : pool) t.join();
        WriteCache(cache, index);
    }
    return index;
}

bool ReadMatches(const fs::path& pack_dir, PackMatches& out, std::string* error) {
    out = {};
    const fs::path path = pack_dir / kMatchesFile;
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    try {
        auto t = toml::parse_file(Utf8(path));
        if (auto* use = t["use"].as_table())
            for (auto&& [k, v] : *use)
                if (auto file = v.value<std::string>()) out.use[std::strtoull(std::string(k.str()).c_str(), nullptr, 16)] = *file;
        if (auto* skip = t["skip"].as_array())
            for (auto&& v : *skip)
                if (auto s = v.value<std::string>()) out.skip.insert(std::strtoull(s->c_str(), nullptr, 16));
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool WriteMatches(const fs::path& pack_dir, const PackMatches& m, std::string* error) {
    auto hex = [](uint64_t key) {
        char b[17];
        std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(key));
        return std::string(b);
    };
    std::map<std::string, std::string> use;  // sorted, so the file diffs well
    for (auto& [k, file] : m.use) use[hex(k)] = file;
    std::vector<std::string> skip;
    for (uint64_t k : m.skip) skip.push_back(hex(k));
    std::sort(skip.begin(), skip.end());
    toml::table uses;
    for (auto& [k, file] : use) uses.insert(k, file);
    toml::array skips;
    for (auto& k : skip) skips.push_back(k);
    toml::table t{{"skip", skips}, {"use", uses}};
    const fs::path path = pack_dir / kMatchesFile;
    fs::path temp = path;
    temp += L".tmp";
    {
        std::ofstream o(temp, std::ios::binary | std::ios::trunc);
        o << "# Tenchu: Return from Darkness (Xbox) matches for this texture pack, made with RFD Switchblade.\n"
             "# use: game texture key = image in this folder. skip: game textures this pack leaves alone.\n"
          << t << "\n";
        if (!o) {
            if (error) *error = "cannot write " + Utf8(temp);
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (error) *error = "cannot replace " + Utf8(path);
        return false;
    }
    return true;
}

}  // namespace tenchu::packs
