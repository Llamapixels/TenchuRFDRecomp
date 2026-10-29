#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

// The game's textures are DDS files inside the archives: DXT5, and a few 32-bit
// A8R8G8B8 ones (skies, overlays), one level each.
namespace wb {

enum : uint32_t { kA8R8G8B8 = 0x06, kDxt1 = 0x0C, kDxt3 = 0x0E, kDxt5 = 0x0F };  // NV2A format numbers, as the game keys them

struct Dds {
    uint32_t w = 0, h = 0, format = 0;
};
bool ParseDds(const std::vector<uint8_t>& file, Dds& dds);
const char* FormatName(uint32_t format);
bool CanEncode(uint32_t format);
// level 0 as BGRA
bool DecodeDds(const std::vector<uint8_t>& file, const Dds& dds, std::vector<uint8_t>& bgra);
// a DDS with the header of `like`, in its format, holding bgra at w x h; empty when the format cannot be written
std::vector<uint8_t> EncodeDds(const std::vector<uint8_t>& like, const Dds& dds, const uint8_t* bgra, uint32_t w, uint32_t h);
// the key the game gives this texture (F6 > Textures, textures\pack_choices.toml); 0 when the
// game's copy is not the file's bytes (A8R8G8B8 is swizzled when loaded)
uint64_t GameKey(const std::vector<uint8_t>& file, const Dds& dds);
// gpu::Hash in the SDK (runtime/gpu/shaders.cpp): keys only match the game's while this is the same
uint64_t Hash(const void* p, size_t n, uint64_t h = 1469598103934665603ull);
std::vector<uint8_t> Resize(const uint8_t* bgra, uint32_t w, uint32_t h, uint32_t out_w, uint32_t out_h);
bool WritePng(const std::filesystem::path& path, const uint8_t* bgra, uint32_t w, uint32_t h);

}  // namespace wb
