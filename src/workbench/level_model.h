#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The Levels page's data, behind its UI: the game's stages read from extracted\, and custom levels
// (runtime/port/custom_levels.h describes the pack the game loads). No drawing here.
//
// Coordinates: the game keeps positions with y negative upwards (collision, starts); its render
// meshes keep y positive upwards. Everything here that is drawn is in render space (y up), so the
// view matches the game's camera; level fields (heights, spawn) are in the game's coordinates.
namespace wb::lv {
namespace fs = std::filesystem;

struct Vec3 { float x = 0, y = 0, z = 0; };

struct StageInfo {
    int stage = -1;
    std::string folder;                 // ST03: its blocks in Media\g.con, its height grid name
    uint32_t boh_offset = 0, boh_size = 0;  // its chunk of k2\STG\BOH.BIN
    uint32_t collision_slots = 0;       // objects that chunk's table holds
    std::string first_block;            // the area block a pack's mesh goes in
    size_t mesh_budget = 0;             // bytes that first block holds
    int blocks = 0;                     // the stage's blocks, which a pack's meshes take in turn
    size_t blocks_bytes = 0;            // all of them together: the memory the stage's map takes
    float start[4]{};                   // Rikimaru's start: x, y, z (game coordinates), rotation
};
// every stage with a map, from extracted\default.xbe's tables and the archives
std::vector<StageInfo> Stages(std::string& error);
std::string StageLabel(const StageInfo& s);

struct Vertex { float x, y, z; uint32_t colour; float u, v; };  // render space
struct Group {
    std::string texture;        // as the block names it
    std::string texture_entry;  // its entry in Media\t.con, empty when not found
    std::vector<uint32_t> triangles;  // three vertex numbers each
};
struct Mesh {
    std::string name;  // the block, e.g. ST03\st03_a00_00.bgm
    std::vector<Vertex> vertices;
    std::vector<Group> groups;
};
struct StageData {
    std::vector<Mesh> meshes;
    std::vector<std::array<Vec3, 3>> collision;  // render space
    Vec3 low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f};  // bounds of the meshes
};
bool LoadStage(const StageInfo& stage, StageData& out, std::string& error);
// textures a pack on this stage can use: the first area's set, as t.con entries
std::vector<std::string> StageTextures(const StageInfo& stage);
std::string TextureName(const std::string& entry);  // "st03_a00.con\jimmen1.dds" -> "jimmen1"
// a t.con texture decoded to BGRA (level 0)
bool LoadTexture(const std::string& entry, std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h);

// a custom level as the editor keeps it (levels\<folder>\editor.toml beside the pack)
struct Box {
    float x = 0, z = 0;       // centre
    float bottom = 0;         // game y of its base (negative is up)
    float width = 400, depth = 400, height = 200;
    std::string texture;      // t.con entry
};
// an imported model (OBJ): its file is copied into the level's models folder when saved
struct Model {
    std::string file;          // absolute until saved, then relative to the level folder (models\x.obj)
    std::string name;
    float x = 0, z = 0;        // where its origin goes
    float y = 0;               // game y of its origin (negative is up)
    float yaw = 0;             // radians
    float scale = 100;         // OBJ units to game units (Blender metres -> centimetre-sized game units)
    bool solid = true;         // its triangles become collision
    // its floors fill the height grid, where the game finds ledges to hang from; that grid holds one
    // height per point, so with them a building's whole footprint blocks like a block up to its roof.
    // Off: only its mesh collides (rooms, doorways), with no ledges
    bool ledges = true;
    std::vector<std::string> textures;  // per material of the file (t.con entries)
    // per material, its own picture used instead of the texture: absolute until saved, then relative
    // to the level folder (images\x.png); saving converts it into one of the stage's free textures
    std::vector<std::string> images;
};
// a model file read once: render-space triangles (y up, left-handed) before placement
struct ModelMesh {
    std::vector<std::string> materials;
    std::vector<std::string> images;  // per material, the .mtl's map_Kd picture (absolute), empty when none
    struct Face { Vec3 p[3]; float uv[3][2]; int material; };
    std::vector<Face> faces;
    Vec3 low{}, high{};
    std::string error;
};
std::shared_ptr<const ModelMesh> ReadModel(const fs::path& file);  // cached by path
// an enemy as the mission places it (k2\ARC\EN_SDATA.BIN, runtime/port/custom_levels.h)
struct Enemy {
    int kind = 0;               // the game's number for it, its entry's first word
    float x = 0, y = 0, z = 0;  // game coordinates (y negative upwards)
    float facing = 0;           // degrees
    int source = -1;            // the mission's enemy it began as: it keeps that one's patrol while unmoved
    bool removed = false;       // kept out of sight, so the mission's numbering of its enemies stays
};
struct MissionEnemies {
    std::vector<Enemy> enemies;  // in the mission's order, each its own source
    std::vector<uint8_t> chunk;  // the mission's chunk of EN_SDATA.BIN (Rikimaru, first layout)
};
// the stage's story enemies, read once; none for stages without them
const MissionEnemies& StageEnemies(const StageInfo& stage);
// what a kind is: its model folder in m.con and character type, e.g. "EM00 (type 2E)"; EM are enemies,
// BS bosses and story characters, NPC people
std::string EnemyKindName(int kind);
// a light the stage's own code places from a table in default.xbe (candles, lanterns, torches, paper
// lanterns); a level can move it or take it away, the stage keeps its number of each
struct Light {
    std::string kind;           // "candle", "lantern", "torch", "paper lantern"
    int group = 0, index = 0;   // which of the stage's tables, and which entry of it
    uint32_t va = 0;            // where its x, y, z floats are in the game's memory
    float x = 0, y = 0, z = 0;  // game coordinates
    bool removed = false;
};
const std::vector<Light>& StageLights(const StageInfo& stage);  // read once
// an item lying in the mission when it starts (keys, potions, tools): the game's number for it (10000
// and up; 10049-10052 are the missions' own, such as stage 3's gate key) and where it lies
struct Item {
    int id = 10003;
    float x = 0, y = 0, z = 0;  // game coordinates
};
struct MissionItems {
    std::vector<Item> items;  // Rikimaru's first layout
    int room = 0;             // how many a level can have: the stage's longest list
};
const MissionItems& StageItems(const StageInfo& stage);  // read once; story stages 0-9
struct Level;
fs::path ModelPath(const Level& level, const Model& model);
fs::path LevelFile(const Level& level, const std::string& file);  // a model or picture path as the level keeps it
// a triangle texture naming a picture file instead of a t.con entry ("image:<absolute path>"); LoadTexture reads both
inline constexpr char kImage[] = "image:";

struct Level {
    std::string folder, name = "New level", author, description;
    int stage = 3;
    float ground_size = 20000, ground_y = 0;  // a square around the spawn; game y
    std::string ground_texture;               // t.con entry
    float texture_scale = 400;                // world units per texture repeat
    float spawn[4]{};
    std::vector<Box> boxes;
    std::vector<Model> models;
    // light baked into the vertex colours: its colour (0.5 is the plain brightness the game's colours
    // double to), and a sun shading the faces turned from it by `sun` (0 none, 1 those faces black)
    float light[3] = {0.5f, 0.5f, 0.5f};
    float sun = 0, sun_yaw = 0.6f, sun_height = 0.9f;  // radians: from +z towards +x, above the horizon
    // the level's own enemies in place of the mission's (for every character and layout)
    bool own_enemies = false;
    std::vector<Enemy> enemies;
    // the stage's lights where the level puts them (all of them once it has its own)
    bool own_lights = false;
    std::vector<Light> lights;
    // the items lying in the mission, for every character and layout
    bool own_items = false;
    std::vector<Item> items;
};
// the level's enemies as an EN_SDATA.BIN chunk (enemies.bin); empty without own enemies
std::vector<uint8_t> EnemyChunk(const Level& level, const StageInfo& stage, std::string& error);
// a model added to the level at a point, sitting on the ground, its materials on the ground texture
bool AddModel(Level& level, const fs::path& file, float x, float z, std::string& error);
// a placed model's box in render space; false when its file cannot be read
bool ModelBox(const Level& level, size_t index, Vec3& low, Vec3& high);
Level NewLevel(const StageInfo& stage);
// the start is the stage's own (x, z), on the level's ground; only its facing is the level's
void LockStart(Level& level, const StageInfo* stage);
// the level's triangles in render space with their texture (t.con entry) and outward normals
struct Triangle {
    Vec3 p[3];
    float uv[3][2];
    std::string texture;
    int owner = -1;  // the ground -1, boxes 0.., models 1000..
};
std::vector<Triangle> LevelTriangles(const Level& level);
// a triangle's vertex colour (BGRA) under the level's light
uint32_t TriangleColour(const Level& level, const Triangle& t);
// the pack files: mesh.bgm and collision.bin as runtime/port/custom_levels.h describes; `slots` names the
// stage texture each picture (kImage texture) was converted into; `no_ledges` gets the collision objects
// (by number) of models without ledges
bool BuildPack(const Level& level, const StageInfo& stage, std::vector<std::vector<uint8_t>>& meshes, std::vector<uint8_t>& collision,
               std::string& error, const std::map<std::string, std::string>* slots = nullptr,
               std::vector<uint32_t>* no_ledges = nullptr);
fs::path LevelsDir();  // levels\ beside the game
// pack + editor.toml in LevelsDir()/folder; sets the folder and makes model and picture paths relative.
// Pictures are copied into images\ and converted into textures\<slot>.dds: each takes the place of one
// of the stage's first-area textures the level does not use, in that texture's format and size.
bool SaveLevel(Level& level, const StageInfo& stage, std::string& error);
bool LoadLevel(const fs::path& dir, Level& level, std::string& error);
std::vector<fs::path> SavedLevels();  // folders in LevelsDir() with an editor.toml

}  // namespace wb::lv
