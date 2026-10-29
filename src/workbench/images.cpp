#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "images.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace wb {
namespace {
constexpr size_t kHeader = 128;
uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
void Set32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
size_t Blocks(uint32_t w, uint32_t h) { return size_t(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4); }

size_t LevelBytes(uint32_t format, uint32_t w, uint32_t h) {
    switch (format) {
        case kDxt1: return Blocks(w, h) * 8;
        case kDxt3:
        case kDxt5: return Blocks(w, h) * 16;
        case kA8R8G8B8: return size_t(w) * h * 4;
    }
    return 0;
}

// BGRA from a 565 colour
void Expand(uint16_t c, uint8_t out[4]) {
    uint32_t r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    out[0] = uint8_t(b << 3 | b >> 2);
    out[1] = uint8_t(g << 2 | g >> 4);
    out[2] = uint8_t(r << 3 | r >> 2);
    out[3] = 255;
}

void DecodeColor(const uint8_t* block, bool four, uint8_t out[16][4]) {
    const uint16_t c0 = uint16_t(block[0] | block[1] << 8), c1 = uint16_t(block[2] | block[3] << 8);
    uint8_t pal[4][4];
    Expand(c0, pal[0]);
    Expand(c1, pal[1]);
    for (int c = 0; c < 3; c++) {
        if (four || c0 > c1) {
            pal[2][c] = uint8_t((2 * pal[0][c] + pal[1][c]) / 3);
            pal[3][c] = uint8_t((pal[0][c] + 2 * pal[1][c]) / 3);
        } else {
            pal[2][c] = uint8_t((pal[0][c] + pal[1][c]) / 2);
            pal[3][c] = 0;
        }
    }
    pal[2][3] = 255;
    pal[3][3] = four || c0 > c1 ? 255 : 0;
    const uint32_t bits = U32(block + 4);
    for (int i = 0; i < 16; i++) std::memcpy(out[i], pal[(bits >> (2 * i)) & 3], 4);
}

void DecodeAlpha(const uint8_t* block, uint8_t out[16][4]) {
    const uint32_t a0 = block[0], a1 = block[1];
    uint8_t pal[8] = {uint8_t(a0), uint8_t(a1)};
    for (int i = 2; i < 8; i++)
        pal[i] = a0 > a1 ? uint8_t(((8 - i) * a0 + (i - 1) * a1) / 7) : i < 6 ? uint8_t(((6 - i) * a0 + (i - 1) * a1) / 5) : i == 6 ? 0 : 255;
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(block[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) out[i][3] = pal[(bits >> (3 * i)) & 7];
}

uint16_t To565(const float c[3]) {  // c: B G R
    auto q = [](float v, int max) { return uint32_t(std::clamp(std::lround(v * max / 255.f), 0l, long(max))); };
    return uint16_t(q(c[2], 31) << 11 | q(c[1], 63) << 5 | q(c[0], 31));
}

// endpoints along the block's principal axis, pulled in a sixteenth, then the nearest of the four colours
void EncodeColor(const uint8_t px[16][4], uint8_t* out) {
    float mean[3] = {};
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) mean[c] += px[i][c] / 16.f;
    float cov[3][3] = {};
    for (int i = 0; i < 16; i++)
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) cov[a][b] += (px[i][a] - mean[a]) * (px[i][b] - mean[b]);
    float axis[3] = {.577f, .577f, .577f};
    for (int it = 0; it < 8; it++) {
        float v[3] = {};
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) v[a] += cov[a][b] * axis[b];
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 1e-6f) break;
        for (int a = 0; a < 3; a++) axis[a] = v[a] / len;
    }
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 16; i++) {
        float p = 0;
        for (int c = 0; c < 3; c++) p += (px[i][c] - mean[c]) * axis[c];
        lo = std::min(lo, p);
        hi = std::max(hi, p);
    }
    const float inset = (hi - lo) / 16;
    lo += inset;
    hi -= inset;
    float e0[3], e1[3];
    for (int c = 0; c < 3; c++) {
        e0[c] = std::clamp(mean[c] + axis[c] * hi, 0.f, 255.f);
        e1[c] = std::clamp(mean[c] + axis[c] * lo, 0.f, 255.f);
    }
    uint16_t c0 = To565(e0), c1 = To565(e1);
    if (c0 < c1) std::swap(c0, c1);  // four colours however the block is read
    uint32_t bits = 0;
    if (c0 != c1) {
        uint8_t pal[4][4];
        Expand(c0, pal[0]);
        Expand(c1, pal[1]);
        for (int c = 0; c < 3; c++) {
            pal[2][c] = uint8_t((2 * pal[0][c] + pal[1][c]) / 3);
            pal[3][c] = uint8_t((pal[0][c] + 2 * pal[1][c]) / 3);
        }
        for (int i = 0; i < 16; i++) {
            int best = 0, best_d = 1 << 30;
            for (int k = 0; k < 4; k++) {
                int d = 0;
                for (int c = 0; c < 3; c++) d += (px[i][c] - pal[k][c]) * (px[i][c] - pal[k][c]);
                if (d < best_d) { best_d = d; best = k; }
            }
            bits |= uint32_t(best) << (2 * i);
        }
    }
    out[0] = uint8_t(c0);
    out[1] = uint8_t(c0 >> 8);
    out[2] = uint8_t(c1);
    out[3] = uint8_t(c1 >> 8);
    Set32(out + 4, bits);
}

void EncodeAlpha(const uint8_t px[16][4], uint8_t* out) {
    uint8_t a0 = 0, a1 = 255;
    for (int i = 0; i < 16; i++) {
        a0 = std::max(a0, px[i][3]);
        a1 = std::min(a1, px[i][3]);
    }
    out[0] = a0;
    out[1] = a1;
    uint64_t bits = 0;
    if (a0 != a1) {
        uint8_t pal[8] = {a0, a1};
        for (int k = 2; k < 8; k++) pal[k] = uint8_t(((8 - k) * a0 + (k - 1) * a1) / 7);
        for (int i = 0; i < 16; i++) {
            int best = 0;
            for (int k = 1; k < 8; k++)
                if (std::abs(px[i][3] - pal[k]) < std::abs(px[i][3] - pal[best])) best = k;
            bits |= uint64_t(best) << (3 * i);
        }
    }
    for (int i = 0; i < 6; i++) out[2 + i] = uint8_t(bits >> (8 * i));
}

IWICImagingFactory* Wic() {
    thread_local ComPtr<IWICImagingFactory> wic;
    if (!wic) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    }
    return wic.Get();
}
}  // namespace

bool ParseDds(const std::vector<uint8_t>& file, Dds& dds) {
    if (file.size() < kHeader || std::memcmp(file.data(), "DDS ", 4) || U32(&file[4]) != 124) return false;
    dds.h = U32(&file[12]);
    dds.w = U32(&file[16]);
    const uint32_t flags = U32(&file[80]), fourcc = U32(&file[84]), bits = U32(&file[88]);
    if (flags & 4) {
        if (!std::memcmp(&file[84], "DXT1", 4)) dds.format = kDxt1;
        else if (!std::memcmp(&file[84], "DXT3", 4)) dds.format = kDxt3;
        else if (!std::memcmp(&file[84], "DXT5", 4)) dds.format = kDxt5;
        else return false;
    } else if (bits == 32 && U32(&file[92]) == 0xFF0000 && U32(&file[96]) == 0xFF00 && U32(&file[100]) == 0xFF) {
        dds.format = kA8R8G8B8;
    } else {
        (void)fourcc;
        return false;
    }
    return dds.w && dds.h && dds.w <= 8192 && dds.h <= 8192 && file.size() >= kHeader + LevelBytes(dds.format, dds.w, dds.h);
}

const char* FormatName(uint32_t format) {
    switch (format) {
        case kDxt1: return "DXT1";
        case kDxt3: return "DXT3";
        case kDxt5: return "DXT5";
        case kA8R8G8B8: return "A8R8G8B8";
    }
    return "unknown";
}

bool CanEncode(uint32_t format) { return format == kDxt5 || format == kA8R8G8B8; }

bool DecodeDds(const std::vector<uint8_t>& file, const Dds& dds, std::vector<uint8_t>& bgra) {
    const uint32_t w = dds.w, h = dds.h;
    bgra.assign(size_t(w) * h * 4, 0);
    const uint8_t* data = file.data() + kHeader;
    if (dds.format == kA8R8G8B8) {
        std::memcpy(bgra.data(), data, bgra.size());
        return true;
    }
    const size_t step = dds.format == kDxt1 ? 8 : 16;
    for (uint32_t by = 0; by < (h + 3) / 4; by++)
        for (uint32_t bx = 0; bx < (w + 3) / 4; bx++, data += step) {
            uint8_t px[16][4];
            if (dds.format == kDxt1) {
                DecodeColor(data, false, px);
            } else {
                DecodeColor(data + 8, true, px);
                if (dds.format == kDxt5) {
                    DecodeAlpha(data, px);
                } else {
                    for (int i = 0; i < 16; i++) px[i][3] = uint8_t(((data[i / 2] >> (4 * (i & 1))) & 15) * 17);
                }
            }
            for (int i = 0; i < 16; i++) {
                const uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x < w && y < h) std::memcpy(&bgra[(size_t(y) * w + x) * 4], px[i], 4);
            }
        }
    return true;
}

std::vector<uint8_t> EncodeDds(const std::vector<uint8_t>& like, const Dds& dds, const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!CanEncode(dds.format) || like.size() < kHeader) return {};
    std::vector<uint8_t> out(like.begin(), like.begin() + kHeader);
    Set32(&out[12], h);
    Set32(&out[16], w);
    Set32(&out[20], dds.format == kDxt5 ? uint32_t(Blocks(w, h) * 16) : w * 4);
    if (dds.format == kA8R8G8B8) {
        out.insert(out.end(), bgra, bgra + size_t(w) * h * 4);
        return out;
    }
    out.resize(kHeader + Blocks(w, h) * 16);
    uint8_t* block = out.data() + kHeader;
    for (uint32_t by = 0; by < (h + 3) / 4; by++)
        for (uint32_t bx = 0; bx < (w + 3) / 4; bx++, block += 16) {
            uint8_t px[16][4];
            for (int i = 0; i < 16; i++) {  // edges repeat the last row and column
                const uint32_t x = std::min(bx * 4 + i % 4, w - 1), y = std::min(by * 4 + i / 4, h - 1);
                std::memcpy(px[i], bgra + (size_t(y) * w + x) * 4, 4);
            }
            EncodeAlpha(px, block);
            EncodeColor(px, block + 8);
        }
    return out;
}

uint64_t GameKey(const std::vector<uint8_t>& file, const Dds& dds) {
    if (dds.format == kA8R8G8B8) return 0;
    const uint32_t shape[] = {dds.format, dds.w, dds.h};
    const uint64_t key = Hash(file.data() + kHeader, LevelBytes(dds.format, dds.w, dds.h), Hash(shape, sizeof(shape)));
    return key ? key : 1;
}

uint64_t Hash(const void* p, size_t n, uint64_t h) {
    constexpr uint64_t k0 = 0x9E3779B97F4A7C15ull, k1 = 0xE7037ED1A0B428DBull;
    auto mix = [](uint64_t a, uint64_t b) {
        unsigned __int128 r = static_cast<unsigned __int128>(a) * b;
        return uint64_t(r) ^ uint64_t(r >> 64);
    };
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint64_t seed = h ^ k0;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint64_t x, y;
        std::memcpy(&x, b + i, 8);
        std::memcpy(&y, b + i + 8, 8);
        seed = mix(x ^ k1, y ^ seed);
    }
    if (size_t r = n - i) {
        uint64_t x = 0, y = 0;
        if (r >= 8) {
            std::memcpy(&x, b + i, 8);
            std::memcpy(&y, b + n - 8, 8);
        } else {
            std::memcpy(&x, b + i, r);
        }
        seed = mix(x ^ k1, y ^ seed ^ r);
    }
    return mix(seed ^ k0, uint64_t(n) ^ k1);
}

// area average when shrinking, bilinear when growing
std::vector<uint8_t> Resize(const uint8_t* bgra, uint32_t w, uint32_t h, uint32_t out_w, uint32_t out_h) {
    std::vector<uint8_t> out(size_t(out_w) * out_h * 4);
    for (uint32_t y = 0; y < out_h; y++)
        for (uint32_t x = 0; x < out_w; x++) {
            float sum[4] = {};
            if (out_w <= w && out_h <= h) {
                const uint32_t x0 = x * w / out_w, x1 = std::max(x0 + 1, (x + 1) * w / out_w);
                const uint32_t y0 = y * h / out_h, y1 = std::max(y0 + 1, (y + 1) * h / out_h);
                for (uint32_t yy = y0; yy < y1; yy++)
                    for (uint32_t xx = x0; xx < x1; xx++)
                        for (int c = 0; c < 4; c++) sum[c] += bgra[(size_t(yy) * w + xx) * 4 + c];
                for (int c = 0; c < 4; c++) sum[c] /= float((x1 - x0) * (y1 - y0));
            } else {
                const float fx = std::clamp((x + .5f) * w / out_w - .5f, 0.f, float(w - 1));
                const float fy = std::clamp((y + .5f) * h / out_h - .5f, 0.f, float(h - 1));
                const uint32_t x0 = uint32_t(fx), y0 = uint32_t(fy), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
                const float tx = fx - x0, ty = fy - y0;
                for (int c = 0; c < 4; c++) {
                    auto at = [&](uint32_t xx, uint32_t yy) { return float(bgra[(size_t(yy) * w + xx) * 4 + c]); };
                    sum[c] = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
                }
            }
            for (int c = 0; c < 4; c++) out[(size_t(y) * out_w + x) * 4 + c] = uint8_t(std::lround(sum[c]));
        }
    return out;
}

bool WritePng(const std::filesystem::path& path, const uint8_t* bgra, uint32_t w, uint32_t h) {
    IWICImagingFactory* wic = Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = wic ? wic->CreateStream(&stream) : E_FAIL;
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 4, w * h * 4, const_cast<uint8_t*>(bgra));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return SUCCEEDED(hr);
}

}  // namespace wb
