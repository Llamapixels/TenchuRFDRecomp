#include "port/custom_levels.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "kernel/kernel.h"
#include "port/connect_file.h"
#include "port/mods.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace tenchu::levels {
namespace {
namespace fs = std::filesystem;

// default.xbe's per-stage tables (stage = word [0x018E412C])
constexpr uint32_t kBgmTable = 0x4C08E0;    // char* "\STG\BGH\STxxBGH.K2V;1", below Media\k2
constexpr uint32_t kBohOffsets = 0x4A9030;  // u32 offset of the stage's chunk in k2\STG\BOH.BIN
constexpr uint32_t kBohSizes = 0x4A90B0;    // u32 its size
// Media\g.con as the game keeps it open (the area loader 00043CB0 reads blocks through it, 00043E50):
// entry count at +0x84, then pointers to its entries' sizes (+0x88) and offsets (+0x8C); a block is
// read whole into a buffer of its size and decrypted with ~size
constexpr uint32_t kGcon = 0x5AE3D8;
// story enemies: {s32 offset, s32 size} of a chunk of k2\ARC\EN_SDATA.BIN per character (0xF0),
// layout (0x50) and mission 0-9 (8); -1 for none
constexpr uint32_t kEnemyChunks = 0x4A6248;
// a story mission's ground items (placer 00288D70): {u32 count, u32 entries} + stage * 8 per character
// and layout, 16-byte entries (u32 item, x, y, z) lying in default.xbe between kItemsFrom and kItemsTo
constexpr uint32_t kItemLists[9] = {0x4D68C8, 0x4D69C0, 0x4D6A20, 0x4D6A80, 0x4D6AE0, 0x4D6B40, 0x4D6BA0, 0x4D6C00, 0x4D6C60};
constexpr uint32_t kItemsFrom = 0x4D0000, kItemsTo = 0x4D6800;
// where the story stages' placed-light tables lie in default.xbe (the Workbench's kLightTables)
constexpr int64_t kLightTablesFrom = 0x4B8080, kLightTablesTo = 0x4BF000;
constexpr int kStages = 32;
constexpr size_t kAreaHeader = 36;          // on the first block of every area
constexpr size_t kVertex = 24, kGroup = 80, kObjectHeader = 0x50, kTriangle = 0x50;

struct Patch {
    std::string file;  // upper-case Xbox path without the drive, e.g. \MEDIA\G.CON
    uint64_t offset = 0;
    std::vector<uint8_t> bytes;
    bool logged = false;
};
struct Prepared {
    std::string folder;
    std::vector<Patch> patches;
    int32_t enemy_offset = -1;  // where enemies.bin is served in EN_SDATA.BIN, -1 without one
    uint32_t enemy_size = 0;
    // words of the game's memory set while the level is in use (g.con's table for blocks served past
    // the archive's end)
    std::vector<std::pair<uint32_t, uint32_t>> words;
};

std::mutex g_lock;
std::vector<LevelInfo> g_list;
std::shared_ptr<Prepared> g_active;
std::atomic<bool> g_any{false};
std::vector<std::pair<uint32_t, std::vector<uint8_t>>> g_saved_starts;  // guest address, original bytes

std::string Upper(std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string Utf8(const fs::path& p) { auto s = p.u8string(); return {s.begin(), s.end()}; }
std::string FileKey(std::string path) {
    path = Upper(path);
    if (path.size() > 1 && path[1] == ':') path.erase(0, 2);
    return path;
}
bool ReadFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}
uint32_t U32(const std::vector<uint8_t>& b, size_t at) { uint32_t v = 0; if (at + 4 <= b.size()) std::memcpy(&v, &b[at], 4); return v; }
void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }

// the bytes the game reads for a disc path: a mod's file, the disc, else the extracted copy
bool OpenGameFile(const std::vector<std::string>& parts, con::Source& source, uint64_t& total) {
    std::error_code ec;
    if (std::string host = mods::Override(parts); !host.empty()) {
        source = con::FileSource(host);
        total = fs::file_size(host, ec);
        return !ec;
    }
    if (const kernel::Disc* disc = kernel::MountedDisc()) {
        const kernel::Disc::Entry* entry = disc->Find(parts);
        if (!entry || entry->dir) return false;
        source = [disc, entry](uint64_t offset, void* out, uint32_t len) {
            return offset + len <= entry->size && disc->Read(*entry, offset, out, len) == len;
        };
        total = entry->size;
        return true;
    }
    fs::path path = app::ExeDir() / "extracted";
    for (const auto& p : parts) path /= p;
    source = con::FileSource(path);
    total = fs::file_size(path, ec);
    return !ec;
}

std::string GuestString(uint32_t va) {
    if (!kernel::g_base || va < 0x10000 || va >= 0x04000000) return {};
    const char* s = static_cast<const char*>(kernel::HostPtr(va));
    return std::string(s, strnlen(s, 64));
}
uint32_t GuestU32(uint32_t va) { uint32_t v = 0; if (kernel::g_base) std::memcpy(&v, kernel::HostPtr(va), 4); return v; }
// a word as the game has it without the active level (whose changes are kept to put back)
uint32_t OriginalU32(uint32_t va) {
    std::lock_guard l(g_lock);
    for (auto& [at, bytes] : g_saved_starts)
        if (va >= at && va + 4 <= at + bytes.size()) {
            uint32_t v;
            std::memcpy(&v, bytes.data() + (va - at), 4);
            return v;
        }
    return GuestU32(va);
}

// guest addresses of the start records (x, y, z, rotation) the stage's player 1 can use
std::vector<uint32_t> StartRecords(int stage) {
    std::vector<uint32_t> out;
    if (stage <= 10 || stage == 0x1D || stage == 0x1E) {  // story starts, one per character
        const int row = stage <= 10 ? stage : stage - 0x13;
        for (int c = 0; c < 3; c++) out.push_back(0x4D9320 + row * 0x30 + c * 16);
    } else {  // single player shares multiplayer's player 1 table; stage 26 also uses the next record
        out.push_back(0x4D9560 + (stage - 10) * 16);
        if (stage == 0x1A) out.push_back(0x4D9560 + (stage - 9) * 16);
    }
    return out;
}

void RestoreStarts() {
    for (auto& [va, bytes] : g_saved_starts)
        if (kernel::g_base) std::memcpy(kernel::HostPtr(va), bytes.data(), bytes.size());
    g_saved_starts.clear();
}

bool ReadLevel(const fs::path& dir, LevelInfo& info) {
    info.folder = Utf8(dir.filename());
    try {
        auto t = toml::parse_file(Utf8(dir / "level.toml"));
        info.name = t["name"].value_or(info.folder);
        info.author = t["author"].value_or(std::string());
        info.description = t["description"].value_or(std::string());
        info.stage = int(t["stage"].value_or(int64_t(-1)));
        if (auto* s = t["spawn"].as_array())
            for (size_t i = 0; i < 4 && i < s->size(); i++) info.spawn[i] = float((*s)[i].value_or(0.0));
        const float around[4] = {info.spawn[0] - 20000, info.spawn[2] - 20000, info.spawn[0] + 20000, info.spawn[2] + 20000};
        std::memcpy(info.bounds, around, sizeof(around));
        if (auto* b = t["bounds"].as_array(); b && b->size() == 4)
            for (size_t i = 0; i < 4; i++) info.bounds[i] = float((*b)[i].value_or(double(around[i])));
        if (auto* skip = t["no_ledges"].as_array())
            for (auto& n : *skip)
                if (auto v = n.value<int64_t>(); v && *v >= 0) info.no_ledges.push_back(uint32_t(*v));
        if (auto* items = t["items"].as_array()) {
            info.own_items = true;
            for (auto& n : *items)
                if (auto* i = n.as_array(); i && i->size() == 4) {
                    LevelInfo::Item item{uint32_t((*i)[0].value_or(int64_t(10003))), {}};
                    for (int k = 0; k < 3; k++) item.position[k] = float((*i)[k + 1].value_or(0.0));
                    info.items.push_back(item);
                }
        }
        if (auto* lights = t["lights"].as_array())
            for (auto& n : *lights)
                if (auto* l = n.as_array(); l && l->size() == 4) {
                    const int64_t va = (*l)[0].value_or(int64_t(0));
                    if (va < kLightTablesFrom || va + 12 > kLightTablesTo) continue;  // only the stages' light tables
                    LevelInfo::Light light{uint32_t(va), {}};
                    for (int i = 0; i < 3; i++) light.position[i] = float((*l)[i + 1].value_or(0.0));
                    info.lights.push_back(light);
                }
    } catch (const std::exception& e) {
        info.name = info.folder;
        info.problem = std::string("level.toml: ") + e.what();
        return false;
    }
    std::error_code ec;
    if (info.stage < 0 || info.stage >= kStages) info.problem = "level.toml has no stage between 0 and 31";
    else if (!fs::exists(dir / "mesh.bgm", ec)) info.problem = "mesh.bgm is missing";
    else if (!fs::exists(dir / "collision.bin", ec)) info.problem = "collision.bin is missing";
    return info.problem.empty();
}

// a height grid's points (w + 1 by h + 1, x = x0 + column * cell, z = z0 - row * cell) from the pack's
// collision: each the highest floor over it (smallest y: y points down), elsewhere the level's lowest
// floor (`ground` without floors): the grid also keeps the player from going below it, so where nothing
// lies it must not sit above anything the level has, as the start height did for a level with no ground.
// The game finds ledges in these heights; with every point at the ground the player could not hang
// from the top of a box, and could once the points over it held its top. It also blocks the player
// below them: a castle's footprint blocked like a box up to its roof, so objects in `skip` stay out.
std::vector<float> FloorHeights(const std::vector<uint8_t>& objects, const std::vector<uint32_t>& skip, uint16_t w, uint16_t h,
                                float cell, float x0, float z0, float ground) {
    std::vector<float> top(size_t(w + 1) * (h + 1), INFINITY);
    float lowest = -INFINITY;
    const uint32_t count = U32(objects, 0);
    size_t o = 4;
    for (uint32_t i = 0; i < count && o + kObjectHeader <= objects.size(); i++) {
        const uint32_t n = U32(objects, o) & 0xFFFF;
        size_t t = o + kObjectHeader;
        o += kObjectHeader + size_t(n) * kTriangle;
        if (o > objects.size()) break;
        const bool skipped = std::find(skip.begin(), skip.end(), i) != skip.end();
        for (uint32_t k = 0; k < n; k++, t += kTriangle) {
            float f[16];  // corners at 0, 4, 8 (the plane's distance at 7), the normal at 12
            std::memcpy(f, &objects[t], sizeof(f));
            if (!(f[13] < -0.5f)) continue;  // floors face up
            lowest = std::max({lowest, f[1], f[5], f[9]});
            if (skipped) continue;
            const float ax = f[0], az = f[2], bx = f[4], bz = f[6], cx = f[8], cz = f[10];
            const float area = (bx - ax) * (cz - az) - (cx - ax) * (bz - az);
            if (std::fabs(area) < 1e-3f) continue;
            const float sign = area > 0 ? 1.f : -1.f, slack = -1e-4f * std::fabs(area);
            const int c0 = std::max(0, int(std::ceil((std::min({ax, bx, cx}) - x0) / cell)));
            const int c1 = std::min(int(w), int(std::floor((std::max({ax, bx, cx}) - x0) / cell)));
            const int r0 = std::max(0, int(std::ceil((z0 - std::max({az, bz, cz})) / cell)));
            const int r1 = std::min(int(h), int(std::floor((z0 - std::min({az, bz, cz})) / cell)));
            for (int r = r0; r <= r1; r++)
                for (int c = c0; c <= c1; c++) {
                    const float x = x0 + c * cell, z = z0 - r * cell;
                    const float e0 = sign * ((bx - ax) * (z - az) - (x - ax) * (bz - az));
                    const float e1 = sign * ((cx - bx) * (z - bz) - (x - bx) * (cz - bz));
                    const float e2 = sign * ((ax - cx) * (z - cz) - (x - cx) * (az - cz));
                    if (e0 < slack || e1 < slack || e2 < slack) continue;
                    const float y = -(f[12] * x + f[14] * z + f[7]) / f[13];
                    float& at = top[size_t(r) * (w + 1) + c];
                    at = std::min(at, y);
                }
        }
    }
    const float none = std::isfinite(lowest) ? lowest : ground;
    for (float& y : top)
        if (!std::isfinite(y)) y = none;
    return top;
}

// the replacements for one pack, read from the stage's current files
bool Prepare(const LevelInfo& info, Prepared& out, std::string& error) {
    const fs::path dir = LevelsDir() / fs::u8path(info.folder);
    std::vector<uint8_t> mesh, objects;
    if (!ReadFile(dir / "mesh.bgm", mesh) || !ReadFile(dir / "collision.bin", objects)) { error = "cannot read the pack's files"; return false; }
    // the stage's files from default.xbe's tables
    std::string bgh = GuestString(GuestU32(kBgmTable + info.stage * 4));  // "\STG\BGH\ST03BGH.K2V;1"
    if (auto semi = bgh.find(';'); semi != std::string::npos) bgh.resize(semi);
    const size_t at = Upper(bgh).find("BGH\\ST");
    if (at == std::string::npos) { error = "stage " + std::to_string(info.stage) + " has no map"; return false; }
    const std::string folder = Upper(bgh.substr(at + 4, 4));  // ST03
    std::vector<std::string> bgh_parts{"Media", "k2"};
    for (size_t s = 1, e; s < bgh.size(); s = e + 1) { e = bgh.find('\\', s); if (e == std::string::npos) e = bgh.size(); bgh_parts.push_back(bgh.substr(s, e - s)); }

    // render: the stage's area blocks in Media\g.con; the first area gets the mesh, the rest are emptied
    con::Source source;
    uint64_t total = 0;
    con::Archive archive;
    if (!OpenGameFile({"Media", "g.con"}, source, total) || !con::Parse(source, total, archive, error)) { error = "Media\\g.con: " + error; return false; }
    std::vector<const con::Entry*> blocks;
    for (const auto& e : archive.entries)
        if (Upper(e.name).rfind(folder + "\\", 0) == 0 && Upper(e.name).size() > 4 && Upper(e.name).substr(Upper(e.name).size() - 4) == ".BGM") blocks.push_back(&e);
    std::sort(blocks.begin(), blocks.end(), [](auto* a, auto* b) { return Upper(a->name) < Upper(b->name); });
    if (blocks.empty()) { error = "Media\\g.con has no " + folder + " blocks"; return false; }
    // the pack's meshes, mesh.bgm then mesh_1.bgm, ..., one per block in that order (the first area's
    // first; each under 65536 corners, as the game's own are)
    std::vector<std::vector<uint8_t>> meshes{mesh};
    for (int k = 1;; k++) {
        std::vector<uint8_t> more;
        if (!ReadFile(dir / ("mesh_" + std::to_string(k) + ".bgm"), more)) break;
        meshes.push_back(std::move(more));
    }
    if (meshes.size() > blocks.size()) {
        error = "the level has " + std::to_string(meshes.size()) + " meshes; " + folder + " has " + std::to_string(blocks.size()) + " blocks";
        return false;
    }
    // a block bigger than the stage's is served past the end of g.con, and the game's table of the
    // open archive points its entry there; the table must be g.con's
    const uint32_t entries = OriginalU32(kGcon + 0x84), sizes = OriginalU32(kGcon + 0x88), offsets = OriginalU32(kGcon + 0x8C);
    const bool table = entries == archive.entries.size() && sizes && offsets;
    uint64_t tail = (total + 0x7FF) & ~uint64_t(0x7FF);
    for (size_t i = 0; i < blocks.size(); i++) {
        std::vector<uint8_t> original;
        if (!con::ReadEntry(source, *blocks[i], original)) { error = "cannot read " + blocks[i]->name; return false; }
        const bool first_of_area = Upper(blocks[i]->name).find("_00.BGM") != std::string::npos;
        std::vector<uint8_t> header(original.begin(), original.begin() + (first_of_area ? std::min(kAreaHeader, original.size()) : 0));
        const std::string texture = FirstTexture(original, header.size());
        std::vector<uint8_t> block = AreaBlock(header, i < meshes.size() ? meshes[i] : std::vector<uint8_t>{}, texture, 0, error);
        if (block.empty()) { error = blocks[i]->name + ": " + error; return false; }
        if (block.size() <= blocks[i]->size) {  // in its place
            block.resize(blocks[i]->size, 0);
            con::Crypt(block.data(), block.size(), ~blocks[i]->size);
            out.patches.push_back({"\\MEDIA\\G.CON", blocks[i]->offset, std::move(block)});
            continue;
        }
        const uint32_t index = uint32_t(blocks[i] - archive.entries.data());
        if (!table || OriginalU32(sizes + index * 4) != blocks[i]->size || OriginalU32(offsets + index * 4) != blocks[i]->offset) {
            error = blocks[i]->name + " is " + std::to_string(blocks[i]->size) + " bytes and the mesh " + std::to_string(block.size()) +
                    "; the game's table of g.con was not found to make it bigger";
            return false;
        }
        block.resize((block.size() + 0x7FF) & ~size_t(0x7FF), 0);  // whole sectors, as the game reads them
        const uint32_t size = uint32_t(block.size());
        con::Crypt(block.data(), block.size(), ~size);
        out.words.push_back({sizes + index * 4, size});
        out.words.push_back({offsets + index * 4, uint32_t(tail)});
        out.patches.push_back({"\\MEDIA\\G.CON", tail, std::move(block)});
        tail = (tail + size + 0x7FF) & ~uint64_t(0x7FF);
    }

    // the level's own pictures: textures\<name>.dds takes the place of the first area's texture of that
    // name in Media\t.con (STxx_A00.CON\<name>.dds), which the Workbench made it the same size as
    std::error_code ec;
    if (fs::is_directory(dir / "textures", ec)) {
        std::string area = Upper(blocks[0]->name.substr(blocks[0]->name.find('\\') + 1));  // ST03_A00_00.BGM
        area.resize(area.rfind('_'));
        area += ".CON\\";
        con::Archive textures;
        if (!OpenGameFile({"Media", "t.con"}, source, total) || !con::Parse(source, total, textures, error)) { error = "Media\\t.con: " + error; return false; }
        std::unordered_map<std::string, const con::Entry*> by_name;
        for (const auto& e : textures.entries) by_name[Upper(e.name)] = &e;
        for (const auto& f : fs::directory_iterator(dir / "textures", ec)) {
            const std::string name = Upper(f.path().filename().string());  // the game's names are ASCII
            if (name.size() < 4 || name.substr(name.size() - 4) != ".DDS") continue;
            std::vector<uint8_t> dds;
            const auto e = by_name.find(area + name);
            if (e == by_name.end() || !ReadFile(f.path(), dds) || dds.size() != e->second->size) {
                LOGR("levels: %s: textures\\%s matches no texture of %s that size; left out", info.folder.c_str(), name.c_str(), area.c_str());
                continue;
            }
            con::Crypt(dds.data(), dds.size(), ~e->second->size);
            out.patches.push_back({"\\MEDIA\\T.CON", e->second->offset, std::move(dds)});
        }
    }

    // map objects: the stage's models in Media\i.con (STxx\*.msh: gates, sliding doors, lanterns),
    // placed by the stage itself; each becomes one invisible triangle so a new level starts bare
    if (OpenGameFile({"Media", "i.con"}, source, total)) {
        con::Archive models;
        std::string model_error;
        if (con::Parse(source, total, models, model_error))
            for (const auto& e : models.entries) {
                const std::string k = Upper(e.name);
                if (k.rfind(folder + "\\", 0) != 0 || k.size() < 4 || k.substr(k.size() - 4) != ".MSH") continue;
                std::vector<uint8_t> original;
                if (!con::ReadEntry(source, e, original)) continue;
                std::vector<uint8_t> block = AreaBlock({}, {}, FirstTexture(original, 0), e.size, model_error);
                if (block.empty()) continue;
                con::Crypt(block.data(), block.size(), ~e.size);
                out.patches.push_back({"\\MEDIA\\I.CON", e.offset, std::move(block)});
            }
    }

    // collision: the stage's chunk of BOH.BIN
    const uint32_t boh_at = OriginalU32(kBohOffsets + info.stage * 4), boh_size = OriginalU32(kBohSizes + info.stage * 4);
    std::vector<uint8_t> chunk(boh_size);
    if (!boh_size || !OpenGameFile({"Media", "k2", "STG", "BOH.BIN"}, source, total) || !source(boh_at, chunk.data(), boh_size)) {
        error = "cannot read the stage's collision";
        return false;
    }
    std::vector<uint8_t> collision = CollisionChunk(chunk, objects, error, true);
    if (collision.empty()) return false;
    if (collision.size() <= boh_size && U32(objects, 0) <= (U32(chunk, 8) - 0x30) / 4) {  // in its place
        collision = CollisionChunk(chunk, objects, error);
        if (collision.empty()) return false;
        out.patches.push_back({"\\MEDIA\\K2\\STG\\BOH.BIN", boh_at, std::move(collision)});
    } else {  // bigger than the stage's: past the end of BOH.BIN, the stage's offset and size pointed there
        const uint32_t tail_at = uint32_t((total + 0x7FF) & ~uint64_t(0x7FF));
        collision.resize((collision.size() + 0x7FF) & ~size_t(0x7FF), 0);
        out.words.push_back({kBohOffsets + uint32_t(info.stage) * 4, tail_at});
        out.words.push_back({kBohSizes + uint32_t(info.stage) * 4, uint32_t(collision.size())});
        out.patches.push_back({"\\MEDIA\\K2\\STG\\BOH.BIN", tail_at, std::move(collision)});
    }

    // enemies: enemies.bin is served over Rikimaru's first-layout chunk of the mission in EN_SDATA.BIN,
    // and Activate points every character's and layout's entry at it
    std::vector<uint8_t> enemies;
    if (info.stage <= 9 && ReadFile(dir / "enemies.bin", enemies) && !enemies.empty()) {
        const int32_t at = int32_t(GuestU32(kEnemyChunks + info.stage * 8));
        if (at >= 0) {
            out.enemy_offset = at;
            out.enemy_size = uint32_t(enemies.size());
            out.patches.push_back({"\\MEDIA\\K2\\ARC\\EN_SDATA.BIN", uint64_t(at), std::move(enemies)});
        }
    }

    // height grids (THIT blocks; the camera, ground and ledge checks sample them, sampler 00213DA0):
    // every block stretched over the level's bounds, its heights the level's floors, with no water.
    // Cells are square: header float +0x18; origin +0x1C (x) and +0x24 (z, rows run towards -z).
    if (OpenGameFile(bgh_parts, source, total)) {
        std::vector<uint8_t> grid(total);
        if (source(0, grid.data(), uint32_t(total))) {
            std::string key = "\\MEDIA\\K2";
            for (size_t i = 2; i < bgh_parts.size(); i++) key += "\\" + Upper(bgh_parts[i]);
            const float span = std::max(info.bounds[2] - info.bounds[0], info.bounds[3] - info.bounds[1]);
            for (size_t p = 0; p + 64 <= grid.size(); p += 16) {
                if (std::memcmp(&grid[p], "THIT", 4)) continue;
                uint16_t w, h;
                std::memcpy(&w, &grid[p + 6], 2);
                std::memcpy(&h, &grid[p + 8], 2);
                const size_t heights = p + U32(grid, p + 48), water = p + U32(grid, p + 52), count = size_t(w + 1) * (h + 1);
                if (heights + count * 4 > grid.size()) continue;
                const float cell = span / std::max<uint16_t>(1, std::min(w, h)), origin[3] = {info.bounds[0], 0, info.bounds[3]};
                std::vector<uint8_t> header(16);
                std::memcpy(&header[0], &cell, 4);
                std::memcpy(&header[4], origin, 12);
                out.patches.push_back({key, p + 0x18, std::move(header)});
                const std::vector<float> top = FloorHeights(objects, info.no_ledges, w, h, cell, origin[0], origin[2], info.spawn[1]);
                std::vector<uint8_t> bytes(count * 4);
                std::memcpy(bytes.data(), top.data(), bytes.size());
                out.patches.push_back({key, heights, std::move(bytes)});
                if (water > heights && water + count <= grid.size()) out.patches.push_back({key, water, std::vector<uint8_t>(count, 0)});
            }
        }
    }
    out.folder = info.folder;
    return true;
}

}  // namespace

std::vector<uint8_t> CollisionChunk(const std::vector<uint8_t>& original, const std::vector<uint8_t>& objects, std::string& error,
                                    bool any_size) {
    uint32_t first = U32(original, 8);
    const uint32_t count = U32(objects, 0);
    if (original.size() < 0x30 || first < 0x30 || first > original.size()) { error = "the stage's collision chunk is unreadable"; return {}; }
    if (count == 0 || count > 0x7FFF || (!any_size && count > (first - 0x30) / 4)) {
        error = "collision.bin has " + std::to_string(count) + " objects; this stage holds 1 to " + std::to_string((first - 0x30) / 4);
        return {};
    }
    if (any_size) first = 0x30 + count * 4;  // the loader (001FAE70) reads the table at +4 for the count at +0
    std::vector<uint8_t> chunk(original.begin(), original.begin() + 0x30);
    chunk.resize(first, 0);
    Put32(chunk, 8, first);
    Put32(chunk, 0, count);
    size_t at = 4;
    for (uint32_t i = 0; i < count; i++) {
        if (at + kObjectHeader > objects.size()) { error = "collision.bin ends inside object " + std::to_string(i); return {}; }
        const size_t length = kObjectHeader + size_t(U32(objects, at)) * kTriangle;
        if (at + length > objects.size()) { error = "collision.bin ends inside object " + std::to_string(i); return {}; }
        Put32(chunk, 0x30 + i * 4, uint32_t(chunk.size()));
        chunk.insert(chunk.end(), objects.begin() + at, objects.begin() + at + length);
        at += length;
    }
    if (any_size) return chunk;
    if (chunk.size() > original.size()) {
        error = "collision.bin is " + std::to_string(chunk.size() - first) + " bytes; this stage holds " + std::to_string(original.size() - first);
        return {};
    }
    chunk.resize(original.size(), 0);
    return chunk;
}

std::vector<uint8_t> AreaBlock(const std::vector<uint8_t>& header, const std::vector<uint8_t>& mesh, const std::string& texture,
                               size_t size, std::string& error) {
    std::vector<uint8_t> block = header;
    if (!mesh.empty()) {
        block.insert(block.end(), mesh.begin(), mesh.end());
    } else {  // one triangle with all corners at the origin: draws nothing
        auto put = [&](uint32_t v) { block.insert(block.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4); };
        put(3);
        block.resize(block.size() + 3 * kVertex, 0);
        put(3);
        const uint16_t strip[3] = {0, 1, 2};
        block.insert(block.end(), reinterpret_cast<const uint8_t*>(strip), reinterpret_cast<const uint8_t*>(strip) + 6);
        put(1);
        std::vector<uint8_t> group(kGroup, 0);
        Put32(group, 4, 3);
        Put32(group, 12, 3);
        std::memcpy(&group[20], texture.data(), std::min<size_t>(texture.size(), 31));
        block.insert(block.end(), group.begin(), group.end());
    }
    if (!size) {  // as big as it needs: whole dwords, which the game decrypts
        block.resize((block.size() + 3) & ~size_t(3), 0);
        return block;
    }
    if (block.size() > size) {
        error = "the mesh is " + std::to_string(block.size()) + " bytes; this stage's first area holds " + std::to_string(size);
        return {};
    }
    block.resize(size, 0);
    return block;
}

std::string FirstTexture(const std::vector<uint8_t>& block, size_t start) {
    const uint32_t nv = U32(block, start);
    const size_t indices = start + 4 + size_t(nv) * kVertex;
    const uint32_t ni = U32(block, indices);
    const size_t groups = indices + 4 + size_t(ni) * 2 + 4;
    if (groups + kGroup > block.size()) return {};
    const char* name = reinterpret_cast<const char*>(&block[groups + 20]);
    return std::string(name, strnlen(name, 32));
}

fs::path LevelsDir() { return app::ExeDir() / "levels"; }

void Rescan() {
    std::vector<LevelInfo> list;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(LevelsDir(), ec)) {
        if (!e.is_directory(ec) || !fs::exists(e.path() / "level.toml", ec)) continue;
        LevelInfo info;
        ReadLevel(e.path(), info);
        list.push_back(std::move(info));
    }
    std::sort(list.begin(), list.end(), [](const LevelInfo& a, const LevelInfo& b) { return Upper(a.name) < Upper(b.name); });
    std::lock_guard l(g_lock);
    g_list = std::move(list);
}

std::vector<LevelInfo> List() {
    std::lock_guard l(g_lock);
    return g_list;
}

bool Activate(const std::string& folder, std::string& status) {
    std::shared_ptr<Prepared> next;
    if (!folder.empty()) {
        LevelInfo info;
        if (!ReadLevel(LevelsDir() / fs::u8path(folder), info)) { status = info.problem; return false; }
        if (!kernel::g_base) { status = "the game is not running"; return false; }
        next = std::make_shared<Prepared>();
        if (!Prepare(info, *next, status)) { LOGR("levels: %s not loaded: %s", folder.c_str(), status.c_str()); return false; }
        std::lock_guard l(g_lock);
        RestoreStarts();
        for (uint32_t va : StartRecords(info.stage)) {
            uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(va));
            g_saved_starts.push_back({va, std::vector<uint8_t>(p, p + 16)});
            // the mission's own x and z: a start elsewhere can land where the mission acts at the
            // start (stage 3's river left the screen white); the level sets the height and facing
            std::memcpy(p + 4, &info.spawn[1], 4);
            std::memcpy(p + 12, &info.spawn[3], 4);
        }
        for (const auto& [va, value] : next->words) {  // g.con's table: the level's bigger blocks
            uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(va));
            g_saved_starts.push_back({va, std::vector<uint8_t>(p, p + 4)});
            std::memcpy(p, &value, 4);
        }
        for (const LevelInfo::Light& light : info.lights) {  // read by the stage's setup as the mission starts
            uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(light.va));
            g_saved_starts.push_back({light.va, std::vector<uint8_t>(p, p + 12)});
            std::memcpy(p, light.position, 12);
        }
        if (info.own_items && info.stage <= 9) {  // into the stage's longest list, which all nine then name
            auto save = [](uint32_t va, size_t n) {
                uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(va));
                g_saved_starts.push_back({va, std::vector<uint8_t>(p, p + n)});
                return p;
            };
            uint32_t room = 0, entries = 0;
            for (uint32_t list : kItemLists) {
                const uint32_t count = GuestU32(list + uint32_t(info.stage) * 8), at = GuestU32(list + uint32_t(info.stage) * 8 + 4);
                if (count > room && count <= 64 && at >= kItemsFrom && at + count * 16 <= kItemsTo) { room = count; entries = at; }
            }
            const uint32_t count = std::min<uint32_t>(room, uint32_t(info.items.size()));
            if (info.items.size() > room) LOGR("levels: %s has %zu items, stage %d takes %u", folder.c_str(), info.items.size(), info.stage, room);
            if (entries) {
                uint8_t* p = save(entries, size_t(room) * 16);
                for (uint32_t i = 0; i < count; i++) {
                    std::memcpy(p + i * 16, &info.items[i].id, 4);
                    std::memcpy(p + i * 16 + 4, info.items[i].position, 12);
                }
                const uint32_t header[2] = {count, entries};
                for (uint32_t list : kItemLists) std::memcpy(save(list + uint32_t(info.stage) * 8, 8), header, 8);
            }
        }
        if (next->enemy_offset >= 0)  // the level's enemies for whichever character and layout is played
            for (uint32_t character = 0; character < 3; character++)
                for (uint32_t layout = 0; layout < 3; layout++) {
                    const uint32_t va = kEnemyChunks + character * 0xF0 + layout * 0x50 + uint32_t(info.stage) * 8;
                    uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(va));
                    int32_t entry[2];
                    std::memcpy(entry, p, 8);
                    if (entry[0] < 0) continue;  // that character has no such mission
                    g_saved_starts.push_back({va, std::vector<uint8_t>(p, p + 8)});
                    entry[0] = next->enemy_offset;
                    entry[1] = int32_t(next->enemy_size);
                    std::memcpy(p, entry, 8);
                }
        g_active = next;
        g_any = true;
        status = info.name + " loads with stage " + std::to_string(info.stage) + "'s mission";
        LOGR("levels: %s active for stage %d, %zu replacements", folder.c_str(), info.stage, next->patches.size());
        return true;
    }
    std::lock_guard l(g_lock);
    RestoreStarts();
    g_active.reset();
    g_any = false;
    status = "No custom level";
    LOGR("levels: none active");
    return true;
}

std::string Active() {
    std::lock_guard l(g_lock);
    return g_active ? g_active->folder : std::string();
}

void PatchRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size) {
    if (!g_any.load(std::memory_order_relaxed) || !size) return;
    std::shared_ptr<Prepared> active;
    {
        std::lock_guard l(g_lock);
        active = g_active;
    }
    if (!active) return;
    const std::string key = FileKey(xbox_path);
    for (Patch& p : active->patches) {
        if (p.file != key) continue;
        const uint64_t lo = std::max<uint64_t>(offset, p.offset), hi = std::min<uint64_t>(offset + size, p.offset + p.bytes.size());
        if (lo >= hi) continue;
        std::memcpy(data + (lo - offset), p.bytes.data() + (lo - p.offset), size_t(hi - lo));
        if (!p.logged) {
            p.logged = true;
            LOGR("levels: %s served %s at %llX", active->folder.c_str(), key.c_str(), (unsigned long long)p.offset);
        }
    }
}

size_t TailRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size) {
    if (!g_any.load(std::memory_order_relaxed) || !size) return 0;
    std::shared_ptr<Prepared> active;
    {
        std::lock_guard l(g_lock);
        active = g_active;
    }
    if (!active) return 0;
    const std::string key = FileKey(xbox_path);
    // the file reads on, as zeros, up to the end of the last of its patches past offset (rounded up
    // to 64 KB): the game reads whole sectors and calls a short read a bad disc
    uint64_t end = 0;
    for (const Patch& p : active->patches)
        if (p.file == key && p.offset + p.bytes.size() > offset) end = std::max<uint64_t>(end, p.offset + p.bytes.size());
    if (!end) return 0;
    end = (end + 0xFFFF) & ~uint64_t(0xFFFF);
    const size_t done = size_t(std::min<uint64_t>(size, end - offset));
    std::memset(data, 0, done);
    for (Patch& p : active->patches) {
        if (p.file != key) continue;
        const uint64_t lo = std::max<uint64_t>(offset, p.offset), hi = std::min<uint64_t>(offset + done, p.offset + p.bytes.size());
        if (lo >= hi) continue;
        std::memcpy(data + (lo - offset), p.bytes.data() + (lo - p.offset), size_t(hi - lo));
        if (!p.logged) {
            p.logged = true;
            LOGR("levels: %s served %s at %llX (past the end)", active->folder.c_str(), key.c_str(), (unsigned long long)p.offset);
        }
    }
    return done;
}

uint64_t FileSize(const std::string& xbox_path, uint64_t size) {
    if (!g_any.load(std::memory_order_relaxed)) return size;
    std::shared_ptr<Prepared> active;
    {
        std::lock_guard l(g_lock);
        active = g_active;
    }
    if (!active) return size;
    const std::string key = FileKey(xbox_path);
    for (const Patch& p : active->patches)
        if (p.file == key && p.offset + p.bytes.size() > size) size = (p.offset + p.bytes.size() + 0x7FF) & ~uint64_t(0x7FF);
    return size;
}

}  // namespace tenchu::levels

void TenchuLevelPatchRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size) {
    tenchu::levels::PatchRead(xbox_path, offset, data, size);
}
size_t TenchuLevelTailRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size) {
    return tenchu::levels::TailRead(xbox_path, offset, data, size);
}
uint64_t TenchuLevelFileSize(const std::string& xbox_path, uint64_t size) {
    return tenchu::levels::FileSize(xbox_path, size);
}
