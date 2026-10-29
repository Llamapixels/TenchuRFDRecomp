#pragma once
// Custom levels: packs in levels\ beside the game, made with RFD Switchblade's Levels page.
// A pack replaces one stage's map when that stage loads: the render mesh (the stage's first area;
// its other areas are emptied), the collision (the stage's chunk of k2\STG\BOH.BIN), the height
// grid (flattened to the spawn height) and player 1's start. Chosen in F6 > Levels; nothing is
// written to disc, the bytes are swapped as the game reads them.
//
// A pack folder holds:
//   level.toml     name, author, description, stage = <stage number>, spawn = [x, y, z, rotation]
//                  (game coordinates: y is negative upwards; the start keeps the mission's own x
//                  and z, only y and rotation are taken), bounds = [min x, min z, max x, max z]
//                  (the area the camera and ground checks cover; 20000 around the spawn if absent),
//                  optional no_ledges = [collision object numbers]: their floors are left out of the
//                  height grids, which are otherwise the highest floor over each point (ledges; a
//                  building's footprint blocks up to its roof),
//                  optional lights = [[address, x, y, z], ...]: the stage's placed lights (candles,
//                  lanterns, torches, paper lanterns: tables in default.xbe its setup code reads)
//                  moved while the level is in use; addresses outside those tables are ignored,
//                  optional items = [[item, x, y, z], ...]: the items lying in the mission (story
//                  stages 0-9), for every character and layout, as many as its longest list
//   textures\      optional <name>.dds files, each replacing the stage's first-area texture of that name
//                  in Media\t.con (STxx_A00.CON\<name>.dds); same size, or it is left out
//   mesh.bgm       one render block: u32 vertex count, 24-byte vertices (x, y, z, BGRA, u, v; y
//                  positive upwards), u32 index count, u16 triangle-strip indices, u32 group count,
//                  80-byte groups (index start/count, vertex start/count, group number, texture name
//                  at +20, bounding box at +56). Textures are names the stage's first area loads.
//   mesh_1.bgm ... optional more blocks, the next of the stage's (sorted by name) each; a block may be
//                  any size (one bigger than the stage's is served past g.con's end), under 65536
//                  corners and indices each as the game's own blocks are
//   collision.bin  u32 object count, then collision objects as in BOH.BIN: u32 triangle count,
//                  12 flag bytes, box min, box max, centre + radius, anchor (16 bytes each), then
//                  0x50-byte triangles.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tenchu::levels {

struct LevelInfo {
    std::string folder;  // folder name in levels\, the pack's id
    std::string name, author, description;
    int stage = -1;
    float spawn[4]{};
    float bounds[4]{};  // min x, min z, max x, max z the level covers (game coordinates)
    std::vector<uint32_t> no_ledges;  // collision objects whose floors stay out of the height grids
    struct Light { uint32_t va; float position[3]; };
    std::vector<Light> lights;  // the stage's placed lights moved: where their x, y, z are in memory
    struct Item { uint32_t id; float position[3]; };
    bool own_items = false;     // items = [...] given: they replace the mission's ground items
    std::vector<Item> items;
    std::string problem;  // why it cannot load, empty when it can
};

std::filesystem::path LevelsDir();
void Rescan();
std::vector<LevelInfo> List();
// makes a pack active (folder name; empty for none): reads it and the stage's files and prepares
// the replaced bytes. False, with the reason in status, when it cannot be used.
bool Activate(const std::string& folder, std::string& status);
std::string Active();
// called for every read of a game file (its Xbox path): replaces the bytes the active pack changes
void PatchRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size);
// past the end of a file: the active level's bytes there (blocks bigger than the stage's), how many
size_t TailRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size);
// a file's size as the game is told it: long enough for the active level's bytes past its end
uint64_t FileSize(const std::string& xbox_path, uint64_t size);

// pieces of a pack's replacements, also used by tests
// the BOH.BIN chunk with the pack's objects in place of the stage's; empty with an error when they
// do not fit (the chunk keeps its size and its offset table its slots). any_size: the stage's header
// with a table just big enough, as long as the objects need (served past the file's end)
std::vector<uint8_t> CollisionChunk(const std::vector<uint8_t>& original, const std::vector<uint8_t>& objects,
                                    std::string& error, bool any_size = false);
// an area block sized like the original entry: header (36 bytes on an area's first block, else
// empty) then mesh; empty mesh gives a single invisible triangle with the given texture
std::vector<uint8_t> AreaBlock(const std::vector<uint8_t>& header, const std::vector<uint8_t>& mesh,
                               const std::string& texture, size_t size, std::string& error);
// the texture of a block's first group (blocks start after the header), empty when unreadable
std::string FirstTexture(const std::vector<uint8_t>& block, size_t start);

}  // namespace tenchu::levels
