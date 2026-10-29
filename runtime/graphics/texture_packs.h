#pragma once
// Texture packs made for another version of the game (the PS2 Tenchu: Wrath of
// Heaven packs) carry their own texture IDs, so they cannot be looked up by the
// port's key. Each folder in textures\packs\ is indexed once, and every texture
// the game uses is matched against the index by appearance: a 16x16 colour
// shortlist, then 32x32 luminance correlation. Confident matches replace the
// texture; close calls are listed for review in F6 > Textures.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tenchu::packs {

enum class Result { None, Pending, Found };

// Texture loader side (gpu/texturedumper.cpp overlay)
bool Active();                  // an enabled pack is indexed or being indexed
uint32_t Revision();            // moves when packs, choices or indexes change: textures look up again
bool Wants(uint64_t key);       // true once per key, when it should be decoded and submitted
// level 0 as tightly packed BGRA, from the loader's worker thread; w/h 0 when it could not be decoded
void Submit(uint64_t key, uint32_t w, uint32_t h, std::vector<uint8_t> bgra);
Result Find(uint64_t key, std::filesystem::path& path, bool& ps2_alpha);
// PS2 alpha: 0x80 is opaque
void ScaleAlpha(uint8_t* bgra, size_t pixels);
// textures\replacement: an image the loader used for a key, counted and listed in F6 > Textures; cleared when the
// folder is listed again
void NoteExact(uint64_t key, const std::filesystem::path& file);
void ClearExact();
// the 16 hex digit part of a dump name such as 256x256_0123456789abcdef_DXT5, 0 without one; an image named so,
// in textures\replacement or in a pack, replaces exactly that texture
uint64_t KeyInFileName(const std::u8string& stem);

// Matching core, exposed for tests
struct Descriptor {
    uint32_t w = 0, h = 0;
    bool flat = true;               // no detail to correlate
    bool ps2_alpha = false;         // most pixels have alpha near 0x80
    uint8_t color[16 * 16 * 3]{};   // BGR
    float lum[32 * 32]{};           // zero mean, unit length
};
Descriptor Describe(const uint8_t* bgra, uint32_t w, uint32_t h);
// box filter into an out_w x out_h BGRA float image; alpha_scale > 0 premultiplies
// colour by min(1, alpha * alpha_scale), so cut-outs compare as empty on both sides
void Box(const uint8_t* px, uint32_t w, uint32_t h, uint32_t out_w, uint32_t out_h, float* out, float alpha_scale);
struct Score { float ncc = -1, color = 9, region = 9; };  // region: worst 4x4 block of the colour grid
Score Compare(const Descriptor& a, const Descriptor& b);
enum class Verdict { None, Review, Accept };
Verdict Judge(const Score& s);

// A pack folder's PNG images and their descriptors. textures\cache\<CacheName> keeps the
// descriptors between runs; the game and RFD Switchblade share it.
struct PackImage {
    std::string file;  // UTF-8, relative to the pack folder
    uint64_t size = 0;
    int64_t time = 0;
    bool ok = false;
    uint64_t key = 0;  // named after a game texture key (KeyInFileName): replaces only that one, never matched
    Descriptor d;
};
using PackIndex = std::vector<PackImage>;
std::string CacheName(const std::string& pack);
// lists the folder, reuses cached descriptors, describes the rest on worker threads and
// rewrites the cache; total, done and errors count as it goes. Sorted by file.
PackIndex IndexFolder(const std::filesystem::path& dir, const std::filesystem::path& cache, std::atomic<size_t>& total,
                      std::atomic<size_t>& done, std::atomic<size_t>& errors);
bool ReadImage(const std::filesystem::path& path, std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h);  // any WIC format, as BGRA

// A pack's own record of which game texture each image replaces, made with the Mod
// Workbench and kept in the pack folder, so whoever has the pack has its matches.
// It comes before the automatic matching; textures\pack_choices.toml (F6 > Textures)
// comes before it.
constexpr const char* kMatchesFile = "rfd_matches.toml";
struct PackMatches {
    std::unordered_map<uint64_t, std::string> use;  // game texture key -> image, as PackImage::file
    std::unordered_set<uint64_t> skip;              // textures this pack leaves alone
};
bool ReadMatches(const std::filesystem::path& pack_dir, PackMatches& out, std::string* error = nullptr);  // false when missing or unreadable
bool WriteMatches(const std::filesystem::path& pack_dir, const PackMatches& matches, std::string* error = nullptr);

// Settings and F6 > Textures
struct PackStatus {
    std::string name;
    bool enabled = true;
    bool indexing = false;
    size_t images = 0, indexed = 0, errors = 0;
};
struct Entry {                  // a replaced texture or a review candidate
    uint64_t key = 0;
    std::string pack, file;     // file relative to the pack folder
    float ncc = 0, color = 0;
    std::filesystem::path compare;  // side-by-side image, empty when not written
    std::filesystem::path image;    // the replacement image
};
struct Stats { size_t seen = 0, matched = 0, review = 0, unmatched = 0; };
std::filesystem::path PacksDir();
void Rescan();                          // find pack folders, start indexing
std::vector<PackStatus> Packs();
void SetEnabled(const std::string& pack, bool enabled);
void Move(const std::string& pack, int delta);  // priority, -1 up
void Reindex(const std::string& pack);
Stats Counts();
std::vector<Entry> Replaced(size_t max);
std::vector<Entry> ReviewList(size_t max);
void Approve(uint64_t key);
void Block(uint64_t key);
void ClearChoices();
// [textures] in the port settings
void Load(const void* toml_table);
void Save(void* toml_table);

}  // namespace tenchu::packs
