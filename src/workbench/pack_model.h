#pragma once
#include "graphics/texture_packs.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// The texture pack matcher's data, behind the Texture packs page: the game's textures,
// the pack's images, the candidates for each texture and what was chosen. No UI here.
namespace wb::pm {
namespace fs = std::filesystem;
namespace packs = tenchu::packs;

struct Place {
    fs::path archive;  // relative to the extracted folder
    std::string entry;
};
struct Candidate {
    int image = -1;
    packs::Score s;
    packs::Verdict v = packs::Verdict::None;
    bool recolour = false;     // the same detail in other colours
    bool other_shape = false;  // width to height differs from the texture's
    bool taken = false;        // already another texture's sure match
};
enum class Choice : uint8_t { Auto, Pick, Skip };
enum Status { kAuto, kCheck, kChosen, kSkipped, kNoMatch, kStatuses };
struct Tex {
    std::vector<Place> places;  // the same texture can sit in several archives
    uint32_t w = 0, h = 0, format = 0;
    uint64_t key = 0;  // 0: the game's key is not known offline
    packs::Descriptor d;
    std::vector<Candidate> cands;  // best first
    bool mutual = false;           // the suggested image likes no other unsure texture better
    Choice choice = Choice::Auto;
    int pick = -1;
};
struct Model {
    fs::path dir;
    std::vector<Tex> tex;
    packs::PackIndex index;
    std::unordered_map<std::string, int> by_file;
    std::unordered_map<std::string, std::vector<int>> groups;  // picture hash -> images
};

constexpr size_t kKeep = 12;
constexpr float kAspect = .05f;      // as the game: |log| of the width/height ratio difference
constexpr float kRecolour = .93f;    // detail this alike with the colour off: the same picture recoloured
constexpr float kOtherShape = .80f;  // other-shaped images are offered from this likeness

// PCSX2 names replacements <picture hash>-<palette hash>-<format>.png: one picture in
// several palettes shares the first part. Other names are their own group.
std::string GroupOf(const std::string& file);
void Prepare(Model& m);  // by_file and groups, after the index changes
// every image against one texture: the game's verdict for same-shaped ones, recolours and
// other shapes offered for review; one image per palette group, the best coloured
std::vector<Candidate> Match(const Tex& t, const Model& m);
// one to one: images that are sure matches are taken for other textures, which then
// prefer free ones; marks mutual pairs
void Rank(Model& m);

int AutoImage(const Tex& t);
int Chosen(const Tex& t);
Status StatusOf(const Tex& t);
int Suggestion(const Tex& t);    // the candidate a review page offers, or -1
float Confidence(const Tex& t);  // review order, most likely first
struct Ranked {
    int image;
    packs::Score s;
    bool other_shape;
};
// one number for how alike two pictures look: detail, less a penalty for other colours
float Similarity(const packs::Score& s);
std::vector<Ranked> AllImages(const Tex& t, const Model& m);  // every readable image, most alike first

void Apply(Model& m, const packs::PackMatches& matches);  // a pack's saved matches onto the model
packs::PackMatches Collect(const Model& m);               // what to save: every image in use, and skips
int AddImage(Model& m, const fs::path& file);             // an image already in the pack folder; its index, -1 if unreadable

}  // namespace wb::pm
