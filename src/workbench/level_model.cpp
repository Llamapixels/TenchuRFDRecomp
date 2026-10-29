#include "level_model.h"
#include "images.h"
#include "ui.h"
#include "port/connect_file.h"
#include "graphics/texture_packs.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <sstream>

namespace wb::lv {
namespace con = tenchu::con;
namespace {

// default.xbe's per-stage tables (the game's stage number is word [0x018E412C])
constexpr uint32_t kBghTable = 0x4C08E0;    // char* "\STG\BGH\STxxBGH.K2V;1"
constexpr uint32_t kBohOffsets = 0x4A9030;  // u32: the stage's chunk of k2\STG\BOH.BIN
constexpr uint32_t kBohSizes = 0x4A90B0;
constexpr int kStages = 32;
constexpr size_t kAreaHeader = 36, kVertex = 24, kGroup = 80;
constexpr float kTile = 2000;  // ground collision tiles, as tested in game

std::string Upper(std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }
uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
float F32(const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }
template <class T> void Put(std::vector<uint8_t>& b, const T& v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + sizeof(T)); }

// an archive in extracted\Media, parsed once
struct Opened {
    con::Source source;
    con::Archive archive;
    std::map<std::string, const con::Entry*> by_key;
    bool ok = false;
};
Opened& Archive(const char* name) {
    static std::mutex lock;
    static std::map<std::string, std::unique_ptr<Opened>> cache;
    std::lock_guard l(lock);
    auto& slot = cache[name];
    if (!slot) {
        slot = std::make_unique<Opened>();
        const fs::path path = Extracted() / "Media" / name;
        std::error_code ec;
        std::string error;
        slot->source = con::FileSource(path);
        slot->ok = con::Parse(slot->source, fs::file_size(path, ec), slot->archive, error);
        for (const auto& e : slot->archive.entries) slot->by_key[Upper(e.name)] = &e;
    }
    return *slot;
}

// extracted\default.xbe, with its virtual addresses mapped to file offsets
struct Xbe {
    std::vector<uint8_t> data;
    std::vector<std::array<uint32_t, 3>> sections;  // address, size, file offset
    const uint8_t* At(uint32_t va, size_t len) const {
        for (auto& [a, size, raw] : sections)
            if (va >= a && va + len <= a + size && raw + (va - a) + len <= data.size()) return &data[raw + (va - a)];
        return nullptr;
    }
    uint32_t Word(uint32_t va) const { const uint8_t* p = At(va, 4); return p ? U32(p) : 0; }
    std::string Text(uint32_t va) const { const uint8_t* p = At(va, 1); return p ? std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), 64)) : ""; }
};
bool LoadXbe(Xbe& x) {
    if (!ReadFile(Extracted() / "default.xbe", x.data) || x.data.size() < 0x200) return false;
    const uint32_t base = U32(&x.data[0x104]), count = U32(&x.data[0x11C]), headers = U32(&x.data[0x120]);
    for (uint32_t i = 0; i < count; i++) {
        const size_t h = headers - base + i * 56;
        if (h + 20 > x.data.size()) return false;
        x.sections.push_back({U32(&x.data[h + 4]), U32(&x.data[h + 16]), U32(&x.data[h + 12])});
    }
    return true;
}

// start records player 1 can use on a stage (story starts by character, or the shared table)
uint32_t StartRecord(int stage) {
    if (stage <= 10) return 0x4D9320 + stage * 0x30;
    if (stage == 0x1D || stage == 0x1E) return 0x4D9320 + (stage - 0x13) * 0x30;
    return 0x4D9560 + (stage - 10) * 16;
}

std::vector<const con::Entry*> Blocks(const std::string& folder) {
    Opened& g = Archive("g.con");
    std::vector<const con::Entry*> out;
    for (const auto& e : g.archive.entries) {
        const std::string k = Upper(e.name);
        if (k.rfind(folder + "\\", 0) == 0 && k.size() > 4 && k.substr(k.size() - 4) == ".BGM") out.push_back(&e);
    }
    std::sort(out.begin(), out.end(), [](auto* a, auto* b) { return Upper(a->name) < Upper(b->name); });
    return out;
}
// "ST03\st03_a00_00.bgm" -> "ST03_A00.CON\": the t.con folder its textures are in
std::string AreaTextures(const std::string& block) {
    std::string name = Upper(block.substr(block.find('\\') + 1));
    if (auto u = name.rfind('_'); u != std::string::npos) name.resize(u);
    return name + ".CON\\";
}

Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void Quad(std::vector<Triangle>& out, Vec3 p[4], const float uv[4][2], Vec3 outward, const std::string& texture) {
    for (int t = 0; t < 2; t++) {
        const int k[3] = {0, t + 1, t + 2};
        Triangle tri;
        tri.texture = texture;
        for (int i = 0; i < 3; i++) { tri.p[i] = p[k[i]]; tri.uv[i][0] = uv[k[i]][0]; tri.uv[i][1] = uv[k[i]][1]; }
        if (Dot(Cross(Sub(tri.p[1], tri.p[0]), Sub(tri.p[2], tri.p[0])), outward) < 0) {
            std::swap(tri.p[1], tri.p[2]);
            std::swap(tri.uv[1][0], tri.uv[2][0]);
            std::swap(tri.uv[1][1], tri.uv[2][1]);
        }
        out.push_back(tri);
    }
}

// one collision object from render-space triangles (custom_levels.h): game space is y mirrored,
// so the corners go in (a, c, b) order to keep their normal pointing out
std::vector<uint8_t> CollisionObject(const std::vector<Triangle>& tris) {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    std::vector<uint8_t> body;
    for (const Triangle& t : tris) {
        Vec3 a{t.p[0].x, -t.p[0].y, t.p[0].z}, b{t.p[2].x, -t.p[2].y, t.p[2].z}, c{t.p[1].x, -t.p[1].y, t.p[1].z};
        Vec3 n = Cross(Sub(b, a), Sub(c, a));
        const float len = std::sqrt(Dot(n, n));
        if (len <= 0) continue;
        n = {n.x / len, n.y / len, n.z / len};
        // walls block from the side their normal faces away from: a box's walls point inwards (with
        // outward ones the player walked into a box and could not leave). Floors keep facing up.
        if (n.y >= -0.5f) {
            std::swap(b, c);
            n = {-n.x, -n.y, -n.z};
        }
        const Vec3 mid{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        float r = 0;
        for (Vec3 p : {a, b, c}) {
            r = std::max(r, std::sqrt(Dot(Sub(p, mid), Sub(p, mid))));
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        // floor, wall: the pair the stages use most (bit 0x01 marks floors). With 0x0D and 0x34, which
        // lack the 0x40 bit of the common pairs, the player could not hang from a box's edge
        const uint32_t surface = n.y < -0.5f ? 0x00750000 : 0x00740000;
        Put(body, a); Put(body, surface);
        Put(body, b); Put(body, -Dot(n, a));
        Put(body, c); Put(body, 1.0f);
        Put(body, n); Put(body, 0.0f);
        Put(body, mid); Put(body, r);
    }
    std::vector<uint8_t> obj;
    Put(obj, uint32_t(body.size() / 0x50));
    const uint8_t flags[12] = {0, 0, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
    obj.insert(obj.end(), flags, flags + 12);
    const Vec3 mid{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    Put(obj, lo); Put(obj, 1.0f);
    Put(obj, hi); Put(obj, 1.0f);
    Put(obj, mid); Put(obj, std::sqrt(Dot(Sub(hi, mid), Sub(hi, mid))));
    Put(obj, Vec3{mid.x, -450.0f, mid.z}); Put(obj, 0.0f);  // the stages' objects carry y -450 here
    obj.insert(obj.end(), body.begin(), body.end());
    return obj;
}

std::string SafeFolder(std::string name) {
    for (char& c : name)
        if (std::strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32) c = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? "level" : name;
}

// a .mtl file's pictures: material name -> its map_Kd file (absolute, only when it exists). The file
// name may follow options (-s 1 1 1 ...), so the whole rest of the line is tried, then its last word;
// relative names are looked up beside the .mtl, then beside the .obj.
void ParseMtl(const fs::path& path, const fs::path& obj_dir, std::map<std::string, std::string>& images) {
    std::ifstream in(path);
    std::string line, material;
    std::error_code ec;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "newmtl") {
            ls >> material;
        } else if (tag == "map_Kd" && !material.empty()) {
            std::string rest;
            std::getline(ls, rest);
            rest.erase(0, rest.find_first_not_of(" \t"));
            const size_t gap = rest.find_last_of(" \t");
            const std::string last = gap == std::string::npos ? rest : rest.substr(gap + 1);
            for (const std::string& name : {rest, last}) {
                if (name.empty()) continue;
                fs::path f = FromU8(name);
                for (const fs::path& at : {f.is_absolute() ? f : path.parent_path() / f, obj_dir / f.filename()})
                    if (fs::is_regular_file(at, ec)) { images[material] = U8(fs::absolute(at, ec)); break; }
                if (images.count(material)) break;
            }
        }
    }
}

// Wavefront OBJ: v, vt, f (polygons fanned), usemtl, mtllib (for pictures). OBJ is right-handed; the
// game's render space is left-handed with y up, so z is negated and each face's winding turned; v runs
// downwards in DirectX.
void ParseObj(const fs::path& path, ModelMesh& m) {
    std::ifstream in(path);
    if (!in) { m.error = "cannot open " + U8(path); return; }
    std::map<std::string, std::string> pictures;
    std::vector<Vec3> pos;
    std::vector<std::array<float, 2>> uv;
    int material = -1;
    std::string line;
    auto index = [](const std::string& s, size_t count) {
        long i = std::strtol(s.c_str(), nullptr, 10);
        return i < 0 ? long(count) + i : i - 1;
    };
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            Vec3 p;
            ls >> p.x >> p.y >> p.z;
            pos.push_back({p.x, p.y, -p.z});
        } else if (tag == "vt") {
            std::array<float, 2> t{};
            ls >> t[0] >> t[1];
            uv.push_back({t[0], 1 - t[1]});
        } else if (tag == "mtllib") {
            std::string name;
            std::getline(ls, name);
            name.erase(0, name.find_first_not_of(" \t"));
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
            ParseMtl(path.parent_path() / FromU8(name), path.parent_path(), pictures);
        } else if (tag == "usemtl") {
            std::string name;
            ls >> name;
            auto it = std::find(m.materials.begin(), m.materials.end(), name);
            material = int(it - m.materials.begin());
            if (it == m.materials.end()) m.materials.push_back(name);
        } else if (tag == "f") {
            std::vector<std::pair<long, long>> corners;
            std::string c;
            while (ls >> c) {
                const size_t slash = c.find('/');
                const long v = index(c.substr(0, slash), pos.size());
                long t = -1;
                if (slash != std::string::npos && slash + 1 < c.size() && c[slash + 1] != '/') t = index(c.substr(slash + 1), uv.size());
                corners.push_back({v, t});
            }
            if (material < 0) { m.materials.push_back("default"); material = 0; }
            for (size_t k = 1; k + 1 < corners.size(); k++) {
                ModelMesh::Face f{};
                f.material = material;
                const std::pair<long, long> tri[3] = {corners[0], corners[k + 1], corners[k]};  // turned with the z flip
                bool ok = true;
                for (int i = 0; i < 3; i++) {
                    if (tri[i].first < 0 || tri[i].first >= long(pos.size())) { ok = false; break; }
                    f.p[i] = pos[tri[i].first];
                    if (tri[i].second >= 0 && tri[i].second < long(uv.size())) { f.uv[i][0] = uv[tri[i].second][0]; f.uv[i][1] = uv[tri[i].second][1]; }
                }
                if (ok) m.faces.push_back(f);
            }
        }
    }
    if (m.faces.empty()) { m.error = U8(path.filename()) + " has no faces"; return; }
    for (const auto& name : m.materials) m.images.push_back(pictures.count(name) ? pictures[name] : std::string());
    m.low = m.high = m.faces[0].p[0];
    for (auto& f : m.faces)
        for (auto& p : f.p) {
            m.low = {std::min(m.low.x, p.x), std::min(m.low.y, p.y), std::min(m.low.z, p.z)};
            m.high = {std::max(m.high.x, p.x), std::max(m.high.y, p.y), std::max(m.high.z, p.z)};
        }
}

// a model's point placed in the level: scaled, turned about y, moved (render space)
Vec3 Place(const Model& model, Vec3 p) {
    const float c = std::cos(model.yaw), s = std::sin(model.yaw);
    p = {p.x * model.scale, p.y * model.scale, p.z * model.scale};
    return {p.x * c + p.z * s + model.x, p.y - model.y, -p.x * s + p.z * c + model.z};
}

}  // namespace

std::shared_ptr<const ModelMesh> ReadModel(const fs::path& file) {
    static std::mutex lock;
    static std::map<std::string, std::pair<fs::file_time_type, std::shared_ptr<const ModelMesh>>> cache;
    std::error_code ec;
    const auto stamp = fs::last_write_time(file, ec);
    std::lock_guard l(lock);
    auto& slot = cache[U8(file)];
    if (!slot.second || slot.first != stamp) {
        auto m = std::make_shared<ModelMesh>();
        ParseObj(file, *m);
        slot = {stamp, m};
    }
    return slot.second;
}

fs::path LevelFile(const Level& level, const std::string& file) {
    const fs::path p = FromU8(file);
    return p.is_absolute() ? p : LevelsDir() / FromU8(level.folder) / p;
}

fs::path ModelPath(const Level& level, const Model& model) { return LevelFile(level, model.file); }

bool ModelBox(const Level& level, size_t index, Vec3& low, Vec3& high) {
    const Model& m = level.models[index];
    auto mesh = ReadModel(ModelPath(level, m));
    if (!mesh->error.empty()) return false;
    low = {1e30f, 1e30f, 1e30f};
    high = {-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; k++) {
        const Vec3 p = Place(m, {k & 1 ? mesh->high.x : mesh->low.x, k & 2 ? mesh->high.y : mesh->low.y, k & 4 ? mesh->high.z : mesh->low.z});
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    return true;
}

bool AddModel(Level& level, const fs::path& file, float x, float z, std::string& error) {
    auto mesh = ReadModel(file);
    if (!mesh->error.empty()) { error = mesh->error; return false; }
    Model m;
    m.file = U8(file);
    m.name = U8(file.stem());
    m.x = x;
    m.z = z;
    // its lowest point on the ground
    m.y = level.ground_y + mesh->low.y * m.scale;
    m.textures.assign(mesh->materials.size(), level.ground_texture);
    m.images = mesh->images;
    level.models.push_back(m);
    return true;
}

std::vector<StageInfo> Stages(std::string& error) {
    Xbe x;
    if (!LoadXbe(x)) { error = "extracted\\default.xbe is missing or unreadable"; return {}; }
    Opened& g = Archive("g.con");
    if (!g.ok) { error = "extracted\\Media\\g.con is missing or unreadable"; return {}; }
    std::vector<uint8_t> boh;
    ReadFile(Extracted() / "Media" / "k2" / "STG" / "BOH.BIN", boh);
    std::vector<StageInfo> out;
    for (int s = 0; s < kStages; s++) {
        const std::string bgh = Upper(x.Text(x.Word(kBghTable + s * 4)));
        const size_t at = bgh.find("BGH\\ST");
        if (at == std::string::npos) continue;
        StageInfo info;
        info.stage = s;
        info.folder = bgh.substr(at + 4, 4);
        info.boh_offset = x.Word(kBohOffsets + s * 4);
        info.boh_size = x.Word(kBohSizes + s * 4);
        if (!info.boh_size || info.boh_offset + 12 > boh.size()) continue;
        info.collision_slots = (U32(&boh[info.boh_offset + 8]) - 0x30) / 4;
        auto blocks = Blocks(info.folder);
        if (blocks.empty()) continue;
        info.first_block = blocks[0]->name;
        info.mesh_budget = blocks[0]->size > kAreaHeader ? blocks[0]->size - kAreaHeader : 0;
        info.blocks = int(blocks.size());
        for (const con::Entry* b : blocks) info.blocks_bytes += b->size;
        if (const uint8_t* p = x.At(StartRecord(s), 16)) std::memcpy(info.start, p, 16);
        out.push_back(info);
    }
    if (out.empty()) error = "no stages found in extracted\\";
    return out;
}

std::string StageLabel(const StageInfo& s) {
    return "Stage " + std::to_string(s.stage) + " (" + s.folder + ")";
}

namespace {
// a mission's chunk of EN_SDATA.BIN (parser 001D50A0): s16 enemies, s16 patrols; the patrols (0x3C4
// bytes each); per enemy 4 bytes (s16 patrol count, the patrols' numbers as bytes); then, 16-byte
// aligned, 64 bytes per enemy: kind at +0, facing (degrees, s16) at +6, x, y, z (floats) at +0x10
constexpr uint32_t kEnemyChunks = 0x4A6248;  // {offset, size} + character * 0xF0 + layout * 0x50 + mission * 8
constexpr size_t kPatrol = 0x3C4, kEnemyEntry = 64;
struct ChunkLayout {
    int enemies = 0, patrols = 0;
    size_t lists = 0, entries = 0;
};
bool ParseEnemyChunk(const std::vector<uint8_t>& c, ChunkLayout& k) {
    if (c.size() < 4) return false;
    int16_t a, n;
    std::memcpy(&a, &c[0], 2);
    std::memcpy(&n, &c[2], 2);
    if (a < 0 || n < 0) return false;
    k.enemies = a;
    k.patrols = n;
    k.lists = 4 + size_t(n) * kPatrol;
    k.entries = (size_t(n) * kPatrol + size_t(a) * 4 + 0x13) & ~size_t(15);
    return k.entries + size_t(a) * kEnemyEntry <= c.size();
}
}  // namespace

const MissionEnemies& StageEnemies(const StageInfo& stage) {
    static std::mutex lock;
    static std::map<int, MissionEnemies> cache;
    std::lock_guard l(lock);
    if (auto f = cache.find(stage.stage); f != cache.end()) return f->second;
    MissionEnemies& out = cache[stage.stage];
    Xbe x;
    if (stage.stage < 0 || stage.stage > 9 || !LoadXbe(x)) return out;
    const int32_t offset = int32_t(x.Word(kEnemyChunks + stage.stage * 8)), size = int32_t(x.Word(kEnemyChunks + stage.stage * 8 + 4));
    std::vector<uint8_t> file;
    if (offset < 0 || size <= 0 || !ReadFile(Extracted() / "Media" / "k2" / "ARC" / "EN_SDATA.BIN", file) || size_t(offset) + size > file.size())
        return out;
    out.chunk.assign(file.begin() + offset, file.begin() + offset + size);
    ChunkLayout k;
    if (!ParseEnemyChunk(out.chunk, k)) { out.chunk.clear(); return out; }
    for (int i = 0; i < k.enemies; i++) {
        const uint8_t* e = &out.chunk[k.entries + size_t(i) * kEnemyEntry];
        Enemy en;
        uint16_t kind;
        int16_t facing;
        std::memcpy(&kind, e, 2);
        std::memcpy(&facing, e + 6, 2);
        std::memcpy(&en.x, e + 0x10, 4);
        std::memcpy(&en.y, e + 0x14, 4);
        std::memcpy(&en.z, e + 0x18, 4);
        en.kind = kind;
        en.facing = facing;
        en.source = i;
        out.enemies.push_back(en);
    }
    return out;
}

namespace {
// the story stages' placed lights: the tables their setup code (dispatcher 002326B0) hands to the
// candle (000C7620), lantern (000CC2C0) and torch (000C9C60) makers, 16 bytes each (x, y, z, 1),
// and stage 3's paper lanterns (002205E0), 0x18 bytes each with the position at +4. The counts are in
// the code, so a level keeps them.
struct LightTable {
    int stage;
    const char* kind;
    uint32_t va;
    int count, stride, position;
};
constexpr LightTable kLightTables[] = {
    {2, "candle", 0x4B8080, 17, 16, 0}, {2, "candle", 0x4B8190, 6, 16, 0}, {2, "candle", 0x4B81F0, 8, 16, 0},
    {3, "candle", 0x4B8388, 4, 16, 0}, {3, "candle", 0x4B83C8, 3, 16, 0}, {3, "candle", 0x4B83F8, 6, 16, 0},
    {3, "torch", 0x4B8458, 1, 16, 0}, {3, "paper lantern", 0x4B9798, 9, 0x18, 4},
    {5, "candle", 0x4BAC60, 86, 16, 0}, {5, "candle", 0x4BB1C0, 82, 16, 0}, {5, "candle", 0x4BA350, 34, 16, 0},
    {5, "candle", 0x4BA570, 7, 16, 0}, {5, "candle", 0x4BA5E0, 15, 16, 0}, {5, "candle", 0x4BA6D0, 13, 16, 0},
    {5, "candle", 0x4BA7A0, 9, 16, 0}, {5, "candle", 0x4BA830, 22, 16, 0}, {5, "candle", 0x4BA990, 32, 16, 0},
    {5, "candle", 0x4BAB90, 13, 16, 0}, {5, "lantern", 0x4BB6E0, 2, 16, 0}, {5, "lantern", 0x4BB700, 21, 16, 0},
    {5, "torch", 0x4BB850, 3, 16, 0},
    {6, "candle", 0x4BC5B8, 25, 16, 0}, {6, "candle", 0x4BC748, 33, 16, 0}, {6, "candle", 0x4BC958, 4, 16, 0},
    {6, "candle", 0x4BC998, 22, 16, 0}, {6, "candle", 0x4BCAF8, 2, 16, 0},
    {7, "candle", 0x4BD218, 11, 16, 0}, {7, "candle", 0x4BD428, 22, 16, 0}, {7, "candle", 0x4BD2C8, 22, 16, 0},
    {7, "lantern", 0x4BD588, 7, 16, 0}, {7, "lantern", 0x4BD5F8, 13, 16, 0},
    {8, "candle", 0x4BE640, 14, 16, 0}, {8, "candle", 0x4BE720, 9, 16, 0}, {8, "candle", 0x4BE7B0, 6, 16, 0},
    {8, "candle", 0x4BE810, 9, 16, 0}, {8, "candle", 0x4BE8A0, 14, 16, 0},
    {9, "candle", 0x4BEBF8, 11, 16, 0}, {9, "candle", 0x4BECA8, 14, 16, 0}, {9, "candle", 0x4BED88, 14, 16, 0},
    {9, "candle", 0x4BEE68, 9, 16, 0}, {9, "candle", 0x4BEEF8, 5, 16, 0},
};
}  // namespace

const std::vector<Light>& StageLights(const StageInfo& stage) {
    static std::mutex lock;
    static std::map<int, std::vector<Light>> cache;
    std::lock_guard l(lock);
    if (auto f = cache.find(stage.stage); f != cache.end()) return f->second;
    std::vector<Light>& out = cache[stage.stage];
    Xbe x;
    if (!LoadXbe(x)) return out;
    int group = 0;
    for (const LightTable& t : kLightTables) {
        if (t.stage != stage.stage) continue;
        for (int i = 0; i < t.count; i++) {
            Light l;
            l.kind = t.kind;
            l.group = group;
            l.index = i;
            l.va = t.va + uint32_t(i * t.stride + t.position);
            if (const uint8_t* p = x.At(l.va, 12)) {
                std::memcpy(&l.x, p, 4);
                std::memcpy(&l.y, p + 4, 4);
                std::memcpy(&l.z, p + 8, 4);
            }
            out.push_back(l);
        }
        group++;
    }
    return out;
}

// a story mission's ground items (the placer 00288D70): {u32 count, u32 entries} + stage * 8 per
// character and layout, the entries 16 bytes each (u32 item, x, y, z)
constexpr uint32_t kItemLists[9] = {0x4D68C8, 0x4D69C0, 0x4D6A20, 0x4D6A80, 0x4D6AE0, 0x4D6B40, 0x4D6BA0, 0x4D6C00, 0x4D6C60};

const MissionItems& StageItems(const StageInfo& stage) {
    static std::mutex lock;
    static std::map<int, MissionItems> cache;
    std::lock_guard l(lock);
    if (auto f = cache.find(stage.stage); f != cache.end()) return f->second;
    MissionItems& out = cache[stage.stage];
    Xbe x;
    if (stage.stage < 0 || stage.stage > 9 || !LoadXbe(x)) return out;
    for (int list = 0; list < 9; list++) {
        const uint32_t count = x.Word(kItemLists[list] + stage.stage * 8), entries = x.Word(kItemLists[list] + stage.stage * 8 + 4);
        if (count > 64 || !x.At(entries, count * 16)) continue;
        out.room = std::max(out.room, int(count));
        if (list) continue;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* p = x.At(entries + i * 16, 16);
            Item item;
            item.id = int(U32(p));
            std::memcpy(&item.x, p + 4, 4);
            std::memcpy(&item.y, p + 8, 4);
            std::memcpy(&item.z, p + 12, 4);
            out.items.push_back(item);
        }
    }
    return out;
}

std::string EnemyKindName(int kind) {
    // a kind's low byte is its character type: 0x49BDC8 holds each type's definition (as for the
    // players), whose second byte is its model number, four entries a model at 0x458018 (its .s4 first)
    static std::mutex lock;
    static std::map<int, std::string> cache;
    std::lock_guard l(lock);
    if (auto f = cache.find(kind); f != cache.end()) return f->second;
    std::string& name = cache[kind];
    char type[16];
    std::snprintf(type, sizeof(type), "type %02X", kind & 0xFF);
    name = type;
    Xbe x;
    if (!LoadXbe(x)) return name;
    const uint32_t definition = x.Word(0x49BDC8 + uint32_t(kind & 0xFF) * 4);
    const uint8_t* model = x.At(definition + 1, 1);
    if (!model) return name;
    std::string file = x.Text(x.Word(0x458018 + uint32_t(*model) * 16));  // "EM00\EM00.s4"
    if (auto slash = file.find('\\'); slash != std::string::npos) file.resize(slash);
    if (!file.empty()) name = file + " (" + type + ")";
    return name;
}

std::vector<uint8_t> EnemyChunk(const Level& level, const StageInfo& stage, std::string& error) {
    if (!level.own_enemies) return {};
    const MissionEnemies& mission = StageEnemies(stage);
    ChunkLayout k;
    if (!ParseEnemyChunk(mission.chunk, k) || mission.enemies.empty()) { error = StageLabel(stage) + " has no story enemies to start from"; return {}; }
    const std::vector<uint8_t>& c = mission.chunk;
    auto entry = [&](int i) { return &c[k.entries + size_t(i) * kEnemyEntry]; };
    std::vector<std::vector<uint8_t>> entries, patrols;
    std::vector<std::vector<uint8_t>> lists;  // per enemy: its patrols' new numbers
    for (const Enemy& e : level.enemies) {
        int from = e.source >= 0 && e.source < k.enemies ? e.source : -1;
        if (from < 0)  // a new one copies the mission's first enemy of its kind
            for (int i = 0; i < k.enemies && from < 0; i++)
                if (mission.enemies[i].kind == e.kind) from = i;
        if (from < 0) { error = "no enemy of kind " + std::to_string(e.kind) + " in " + StageLabel(stage); return {}; }
        std::vector<uint8_t> en(entry(from), entry(from) + kEnemyEntry);
        const Enemy& was = mission.enemies[from];
        const bool unmoved = e.source == from && std::fabs(e.x - was.x) < 0.5f && std::fabs(e.y - was.y) < 0.5f &&
                             std::fabs(e.z - was.z) < 0.5f && std::fabs(e.facing - was.facing) < 0.5f;
        float pos[3] = {e.x, e.y, e.z};
        if (e.removed) { pos[0] = 0; pos[1] = -100000; pos[2] = 0; }  // where the mission keeps its unused ones
        const int16_t facing = int16_t(std::lround(e.facing));
        std::memcpy(&en[6], &facing, 2);
        std::memcpy(&en[0x10], pos, 12);
        std::vector<uint8_t> list;
        if (unmoved && !e.removed) {  // its patrols come along; moved or turned it stands guard
            int16_t count;
            std::memcpy(&count, &c[k.lists + size_t(from) * 4], 2);
            for (int p = 0; p < count && p < 2; p++) {
                const int number = c[k.lists + size_t(from) * 4 + 2 + p];
                if (number >= k.patrols) continue;
                list.push_back(uint8_t(patrols.size()));
                patrols.emplace_back(c.begin() + 4 + size_t(number) * kPatrol, c.begin() + 4 + size_t(number + 1) * kPatrol);
            }
        }
        entries.push_back(std::move(en));
        lists.push_back(std::move(list));
    }
    if (entries.size() > 0x7FFF || patrols.size() > 255) { error = "too many enemies"; return {}; }
    std::vector<uint8_t> out(4, 0);
    const int16_t a = int16_t(entries.size()), n = int16_t(patrols.size());
    std::memcpy(&out[0], &a, 2);
    std::memcpy(&out[2], &n, 2);
    for (const auto& p : patrols) out.insert(out.end(), p.begin(), p.end());
    for (const auto& list : lists) {
        const int16_t count = int16_t(list.size());
        uint8_t l[4] = {0, 0, 0xFF, 0xFF};
        std::memcpy(l, &count, 2);
        for (size_t p = 0; p < list.size(); p++) l[2 + p] = list[p];
        out.insert(out.end(), l, l + 4);
    }
    out.resize((size_t(n) * kPatrol + size_t(a) * 4 + 0x13) & ~size_t(15), 0);
    for (const auto& en : entries) out.insert(out.end(), en.begin(), en.end());
    return out;
}

bool LoadStage(const StageInfo& stage, StageData& out, std::string& error) {
    Opened& g = Archive("g.con");
    Opened& t = Archive("t.con");
    if (!g.ok) { error = "extracted\\Media\\g.con is unreadable"; return false; }
    for (const con::Entry* e : Blocks(stage.folder)) {
        std::vector<uint8_t> b;
        if (!con::ReadEntry(g.source, *e, b)) continue;
        size_t at = Upper(e->name).find("_00.BGM") != std::string::npos ? kAreaHeader : 0;
        if (at + 4 > b.size()) continue;
        const uint32_t nv = U32(&b[at]);
        const size_t verts = at + 4, strip = verts + size_t(nv) * kVertex + 4;
        if (strip > b.size()) continue;
        const uint32_t ni = U32(&b[strip - 4]);
        const size_t groups = strip + size_t(ni) * 2 + 4;
        if (groups > b.size()) continue;
        const uint32_t ng = U32(&b[groups - 4]);
        if (groups + size_t(ng) * kGroup > b.size()) continue;
        Mesh m;
        m.name = e->name;
        m.vertices.resize(nv);
        for (uint32_t i = 0; i < nv; i++) {
            const uint8_t* v = &b[verts + i * kVertex];
            m.vertices[i] = {F32(v), F32(v + 4), F32(v + 8), U32(v + 12), F32(v + 16), F32(v + 20)};
            out.low = {std::min(out.low.x, m.vertices[i].x), std::min(out.low.y, m.vertices[i].y), std::min(out.low.z, m.vertices[i].z)};
            out.high = {std::max(out.high.x, m.vertices[i].x), std::max(out.high.y, m.vertices[i].y), std::max(out.high.z, m.vertices[i].z)};
        }
        std::vector<uint16_t> ix(ni);
        std::memcpy(ix.data(), &b[strip], size_t(ni) * 2);
        const std::string folder = AreaTextures(e->name);
        for (uint32_t k = 0; k < ng; k++) {
            const uint8_t* r = &b[groups + k * kGroup];
            const uint32_t start = U32(r), count = U32(r + 4);
            Group grp;
            grp.texture = std::string(reinterpret_cast<const char*>(r + 20), strnlen(reinterpret_cast<const char*>(r + 20), 32));
            if (auto f = t.by_key.find(folder + Upper(grp.texture) + ".DDS"); f != t.by_key.end()) grp.texture_entry = f->second->name;
            for (uint32_t i = start; i + 2 < start + count && i + 2 < ni; i++) {
                const uint16_t a = ix[i], bb = ix[i + 1], c = ix[i + 2];
                if (a == bb || bb == c || a == c || a >= nv || bb >= nv || c >= nv) continue;
                grp.triangles.insert(grp.triangles.end(), {a, bb, c});
            }
            m.groups.push_back(std::move(grp));
        }
        out.meshes.push_back(std::move(m));
    }
    // collision
    std::vector<uint8_t> boh;
    if (ReadFile(Extracted() / "Media" / "k2" / "STG" / "BOH.BIN", boh) && stage.boh_offset + stage.boh_size <= boh.size()) {
        const uint8_t* c = &boh[stage.boh_offset];
        const uint32_t count = std::min(U32(c), stage.collision_slots);
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t o = U32(c + 0x30 + i * 4);
            if (o + 0x50 > stage.boh_size) break;
            const uint32_t n = U32(c + o);
            for (uint32_t k = 0; k < n && o + 0x50 + (k + 1) * 0x50 <= stage.boh_size; k++) {
                const uint8_t* r = c + o + 0x50 + k * 0x50;
                std::array<Vec3, 3> tri;
                for (int v = 0; v < 3; v++) tri[v] = {F32(r + v * 16), -F32(r + v * 16 + 4), F32(r + v * 16 + 8)};
                out.collision.push_back(tri);
            }
        }
    }
    if (out.meshes.empty()) { error = "no meshes for " + stage.folder; return false; }
    return true;
}

std::vector<std::string> StageTextures(const StageInfo& stage) {
    Opened& t = Archive("t.con");
    const std::string folder = AreaTextures(stage.first_block);
    std::vector<std::string> out;
    for (const auto& e : t.archive.entries)
        if (Upper(e.name).rfind(folder, 0) == 0 && Upper(e.name).size() > 4 && Upper(e.name).substr(Upper(e.name).size() - 4) == ".DDS")
            out.push_back(e.name);
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return Upper(a) < Upper(b); });
    return out;
}

std::string TextureName(const std::string& entry) {
    std::string name = entry.substr(entry.find_last_of("\\/") + 1);
    if (auto dot = name.rfind('.'); dot != std::string::npos) name.resize(dot);
    return name;
}

bool LoadTexture(const std::string& entry, std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h) {
    if (entry.rfind(kImage, 0) == 0) return tenchu::packs::ReadImage(FromU8(entry.substr(sizeof(kImage) - 1)), bgra, w, h);
    Opened& t = Archive("t.con");
    auto f = t.by_key.find(Upper(entry));
    std::vector<uint8_t> file;
    Dds dds;
    if (f == t.by_key.end() || !con::ReadEntry(t.source, *f->second, file) || !ParseDds(file, dds) || !DecodeDds(file, dds, bgra)) return false;
    w = dds.w;
    h = dds.h;
    return true;
}

void LockStart(Level& level, const StageInfo* stage) {
    if (stage) {
        level.spawn[0] = stage->start[0];
        level.spawn[2] = stage->start[2];
    }
    level.spawn[1] = level.ground_y;
}

Level NewLevel(const StageInfo& stage) {
    Level l;
    l.stage = stage.stage;
    std::memcpy(l.spawn, stage.start, sizeof(l.spawn));
    l.ground_y = stage.start[1];
    auto textures = StageTextures(stage);
    if (!textures.empty()) l.ground_texture = textures[0];
    for (const auto& tex : textures)  // ground-looking names first
        if (Upper(tex).find("JIM") != std::string::npos || Upper(tex).find("TUTI") != std::string::npos ||
            Upper(tex).find("TOCHI") != std::string::npos || Upper(tex).find("SUNA") != std::string::npos) { l.ground_texture = tex; break; }
    return l;
}

std::vector<Triangle> LevelTriangles(const Level& level) {
    std::vector<Triangle> out;
    const float s = level.texture_scale > 1 ? level.texture_scale : 400;
    const float half = level.ground_size / 2, cx = level.spawn[0], cz = level.spawn[2], y = -level.ground_y;
    if (level.ground_size > 0) {  // size 0: no ground, the level is its boxes and models
        Vec3 p[4] = {{cx - half, y, cz - half}, {cx + half, y, cz - half}, {cx + half, y, cz + half}, {cx - half, y, cz + half}};
        float uv[4][2];
        for (int i = 0; i < 4; i++) { uv[i][0] = p[i].x / s; uv[i][1] = p[i].z / s; }
        Quad(out, p, uv, {0, 1, 0}, level.ground_texture);
    }
    for (size_t index = 0; index < level.boxes.size(); index++) {
        const Box& b = level.boxes[index];
        const size_t first = out.size();
        const float x0 = b.x - b.width / 2, x1 = b.x + b.width / 2, z0 = b.z - b.depth / 2, z1 = b.z + b.depth / 2;
        const float y0 = -b.bottom, y1 = -b.bottom + b.height;
        {
            Vec3 p[4] = {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
            float uv[4][2];
            for (int i = 0; i < 4; i++) { uv[i][0] = p[i].x / s; uv[i][1] = p[i].z / s; }
            Quad(out, p, uv, {0, 1, 0}, b.texture);
        }
        struct Side { Vec3 a, b; Vec3 n; } sides[4] = {
            {{x0, 0, z0}, {x1, 0, z0}, {0, 0, -1}}, {{x1, 0, z0}, {x1, 0, z1}, {1, 0, 0}},
            {{x1, 0, z1}, {x0, 0, z1}, {0, 0, 1}}, {{x0, 0, z1}, {x0, 0, z0}, {-1, 0, 0}}};
        for (const Side& side : sides) {
            const float along = std::fabs(side.b.x - side.a.x) + std::fabs(side.b.z - side.a.z);
            Vec3 p[4] = {{side.a.x, y0, side.a.z}, {side.b.x, y0, side.b.z}, {side.b.x, y1, side.b.z}, {side.a.x, y1, side.a.z}};
            const float uv[4][2] = {{0, y0 / s}, {along / s, y0 / s}, {along / s, y1 / s}, {0, y1 / s}};
            Quad(out, p, uv, side.n, b.texture);
        }
        for (size_t t = first; t < out.size(); t++) out[t].owner = int(index);
    }
    for (size_t index = 0; index < level.models.size(); index++) {
        const Model& m = level.models[index];
        auto mesh = ReadModel(ModelPath(level, m));
        for (const auto& f : mesh->faces) {
            Triangle t;
            for (int i = 0; i < 3; i++) { t.p[i] = Place(m, f.p[i]); t.uv[i][0] = f.uv[i][0]; t.uv[i][1] = f.uv[i][1]; }
            t.texture = f.material < int(m.textures.size()) ? m.textures[f.material] : level.ground_texture;
            if (f.material < int(m.images.size()) && !m.images[f.material].empty())
                t.texture = kImage + U8(LevelFile(level, m.images[f.material]));
            if (t.texture.empty()) t.texture = level.ground_texture;
            t.owner = 1000 + int(index);
            out.push_back(t);
        }
    }
    return out;
}

uint32_t TriangleColour(const Level& level, const Triangle& t) {
    Vec3 n = Cross(Sub(t.p[1], t.p[0]), Sub(t.p[2], t.p[0]));  // front faces wind so this points out
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    float shade = 1;
    if (level.sun > 0 && length > 0) {
        const Vec3 to_sun{std::cos(level.sun_height) * std::sin(level.sun_yaw), std::sin(level.sun_height),
                          std::cos(level.sun_height) * std::cos(level.sun_yaw)};
        const float facing = (n.x * to_sun.x + n.y * to_sun.y + n.z * to_sun.z) / length;
        shade = 1 - level.sun + level.sun * std::max(0.f, facing);
    }
    auto byte = [&](float c) { return uint32_t(std::clamp(c * shade * 255 + 0.5f, 0.f, 255.f)); };
    return 0xFF000000u | byte(level.light[0]) << 16 | byte(level.light[1]) << 8 | byte(level.light[2]);
}

namespace {
// triangles cut in two across their longest edge until none is longer than `longest`: the game fogs
// at the corners, so a big triangle with far corners darkened all over as the camera turned
std::vector<Triangle> Split(const std::vector<Triangle>& in, float longest) {
    std::vector<Triangle> out, work(in.rbegin(), in.rend());
    while (!work.empty()) {
        const Triangle t = work.back();
        work.pop_back();
        int edge = 0;
        float most = 0;
        for (int i = 0; i < 3; i++) {
            const Vec3 d = Sub(t.p[(i + 1) % 3], t.p[i]);
            const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (l > most) { most = l; edge = i; }
        }
        if (most <= longest) { out.push_back(t); continue; }
        const int a = edge, b = (edge + 1) % 3;
        const Vec3 mid{(t.p[a].x + t.p[b].x) / 2, (t.p[a].y + t.p[b].y) / 2, (t.p[a].z + t.p[b].z) / 2};
        const float uv[2] = {(t.uv[a][0] + t.uv[b][0]) / 2, (t.uv[a][1] + t.uv[b][1]) / 2};
        Triangle first = t, second = t;  // a, mid, c and mid, b, c: the same winding
        first.p[b] = mid; first.uv[b][0] = uv[0]; first.uv[b][1] = uv[1];
        second.p[a] = mid; second.uv[a][0] = uv[0]; second.uv[a][1] = uv[1];
        work.push_back(second);
        work.push_back(first);
    }
    return out;
}
}  // namespace

bool BuildPack(const Level& level, const StageInfo& stage, std::vector<std::vector<uint8_t>>& meshes, std::vector<uint8_t>& collision,
               std::string& error, const std::map<std::string, std::string>* slots, std::vector<uint32_t>* no_ledges) {
    const std::vector<Triangle> tris = LevelTriangles(level);
    // render blocks: per texture a group, its triangles joined into one strip; long triangles cut. A
    // block holds under 65536 corners and strip indices (u16, and the game's own stay under), so a
    // level spreads over as many of the stage's blocks as it needs
    const std::vector<Triangle> drawn = Split(tris, 1000);
    std::map<std::string, std::vector<const Triangle*>> by_texture;
    for (const Triangle& t : drawn) by_texture[t.texture].push_back(&t);
    struct Block {
        std::vector<uint8_t> vertices, groups;
        std::vector<uint16_t> strip;
        uint32_t nv = 0, ng = 0;
    };
    constexpr uint32_t kMost = 65000;
    std::vector<Block> built(1);
    for (auto& [texture, list] : by_texture) {
        if (texture.empty()) { error = "a surface has no texture"; return false; }
        std::string named = texture;  // a picture goes by the stage texture it was converted into
        if (slots)
            if (auto slot = slots->find(texture); slot != slots->end()) named = slot->second;
        const std::string name = TextureName(named);
        size_t next = 0;
        while (next < list.size()) {  // a group, carried on in the next block when this one is full
            if (built.back().nv + 3 > kMost || built.back().strip.size() + 8 > kMost) built.emplace_back();
            Block& b = built.back();
            const uint32_t first_vertex = b.nv, first_index = uint32_t(b.strip.size());
            Vec3 lo{0, 0, 0}, hi{0, 0, 0};  // the game's group boxes include the origin
            std::vector<uint16_t> s;
            std::map<std::tuple<float, float, float, float, float, uint32_t>, uint16_t> shared;  // same place, uv and colour: one vertex
            for (; next < list.size(); next++) {
                if (b.nv + 3 > kMost || b.strip.size() + s.size() + 8 > kMost) break;
                const Triangle* t = list[next];
                uint16_t ids[3];
                const uint32_t colour = TriangleColour(level, *t);
                for (int i = 0; i < 3; i++) {
                    const auto key = std::make_tuple(t->p[i].x, t->p[i].y, t->p[i].z, t->uv[i][0], t->uv[i][1], colour);
                    if (auto f = shared.find(key); f != shared.end()) { ids[i] = f->second; continue; }
                    Put(b.vertices, t->p[i]); Put(b.vertices, colour); Put(b.vertices, t->uv[i][0]); Put(b.vertices, t->uv[i][1]);
                    lo = {std::min(lo.x, t->p[i].x), std::min(lo.y, t->p[i].y), std::min(lo.z, t->p[i].z)};
                    hi = {std::max(hi.x, t->p[i].x), std::max(hi.y, t->p[i].y), std::max(hi.z, t->p[i].z)};
                    ids[i] = uint16_t(b.nv++);
                    shared[key] = ids[i];
                }
                if (!s.empty()) {  // join with degenerate triangles; the next starts on an even position
                    s.push_back(s.back());
                    s.push_back(ids[0]);
                    if (s.size() % 2) s.push_back(ids[0]);
                }
                s.insert(s.end(), ids, ids + 3);
            }
            if (s.empty()) continue;
            b.strip.insert(b.strip.end(), s.begin(), s.end());
            std::vector<uint8_t> g(kGroup, 0);
            const uint32_t head[5] = {first_index, uint32_t(s.size()), first_vertex, b.nv - first_vertex, b.ng};
            std::memcpy(g.data(), head, sizeof(head));
            std::memcpy(&g[20], name.data(), std::min<size_t>(name.size(), 31));
            const float box[6] = {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z};
            std::memcpy(&g[56], box, sizeof(box));
            b.groups.insert(b.groups.end(), g.begin(), g.end());
            b.ng++;
        }
    }
    meshes.clear();
    for (const Block& b : built) {
        if (!b.nv) continue;
        std::vector<uint8_t> mesh;
        Put(mesh, b.nv);
        mesh.insert(mesh.end(), b.vertices.begin(), b.vertices.end());
        Put(mesh, uint32_t(b.strip.size()));
        for (uint16_t i : b.strip) Put(mesh, i);
        Put(mesh, b.ng);
        mesh.insert(mesh.end(), b.groups.begin(), b.groups.end());
        meshes.push_back(std::move(mesh));
    }
    if (meshes.size() > size_t(stage.blocks)) {
        error = "the level needs " + std::to_string(meshes.size()) + " blocks of 65000 corners; " + StageLabel(stage) + " has " +
                std::to_string(stage.blocks);
        return false;
    }

    // collision: one object per box, solid models in pieces of up to kPiece triangles (nearby ones
    // together; the stages' own objects have up to 229), then ground tiles. A chunk bigger than the
    // stage's, in objects or bytes, is served past the end of BOH.BIN
    constexpr size_t kPiece = 128;
    std::vector<std::vector<uint8_t>> pieces;
    std::vector<uint32_t> ledgeless;  // pieces of models without ledges
    std::map<int, std::vector<Triangle>> by_owner;
    for (const Triangle& t : tris)
        if (t.owner >= 0 && (t.owner < 1000 || level.models[t.owner - 1000].solid)) by_owner[t.owner].push_back(t);
    for (auto& [owner, list] : by_owner) {
        std::sort(list.begin(), list.end(), [](const Triangle& a, const Triangle& b) {
            const float ax = std::floor((a.p[0].x + a.p[1].x + a.p[2].x) / 1500), bx = std::floor((b.p[0].x + b.p[1].x + b.p[2].x) / 1500);
            if (ax != bx) return ax < bx;
            return a.p[0].z + a.p[1].z + a.p[2].z < b.p[0].z + b.p[1].z + b.p[2].z;
        });
        const bool ledges = owner < 1000 || level.models[owner - 1000].ledges;
        for (size_t at = 0; at < list.size(); at += kPiece) {
            if (!ledges) ledgeless.push_back(uint32_t(pieces.size()));
            pieces.push_back(CollisionObject(std::vector<Triangle>(list.begin() + at, list.begin() + std::min(list.size(), at + kPiece))));
        }
    }
    // ground tiles of kTile while the stage's slots leave room for them, else at least 4 by 4
    const uint32_t free_slots = stage.collision_slots > pieces.size() ? stage.collision_slots - uint32_t(pieces.size()) : 0;
    const int wanted = level.ground_size > 0 ? std::max(1, int(std::ceil(level.ground_size / kTile))) : 0;  // none without a ground
    const int per_side = std::min(wanted, std::max(int(std::sqrt(double(free_slots))), 4));
    std::vector<std::vector<uint8_t>> objects;
    const float tile = per_side ? level.ground_size / per_side : 0, x0 = level.spawn[0] - level.ground_size / 2, z0 = level.spawn[2] - level.ground_size / 2;
    const float y = -level.ground_y;
    for (int i = 0; i < per_side; i++)
        for (int j = 0; j < per_side; j++) {
            Vec3 p[4] = {{x0 + i * tile, y, z0 + j * tile}, {x0 + (i + 1) * tile, y, z0 + j * tile},
                         {x0 + (i + 1) * tile, y, z0 + (j + 1) * tile}, {x0 + i * tile, y, z0 + (j + 1) * tile}};
            const float uv[4][2] = {};
            std::vector<Triangle> t;
            Quad(t, p, uv, {0, 1, 0}, "");
            objects.push_back(CollisionObject(t));
        }
    if (no_ledges) {  // the pieces follow the ground tiles
        no_ledges->clear();
        for (uint32_t piece : ledgeless) no_ledges->push_back(uint32_t(objects.size()) + piece);
    }
    objects.insert(objects.end(), pieces.begin(), pieces.end());
    collision.clear();
    Put(collision, uint32_t(objects.size()));
    for (auto& o : objects) collision.insert(collision.end(), o.begin(), o.end());
    if (objects.empty()) { error = "nothing is solid: the level needs a ground, a box or a solid model"; return false; }
    if (objects.size() > 0x7FFF) { error = std::to_string(objects.size()) + " collision objects; the game counts at most 32767"; return false; }
    return true;
}

fs::path LevelsDir() { return GameDir() / "levels"; }

namespace {
// a stage texture a picture can take the place of
struct FreeTexture {
    std::string entry;          // in t.con
    std::vector<uint8_t> file;  // its DDS, whose header and size the picture takes
    Dds dds;
};
// the stage's first-area DXT5 textures the level does not use, but for the sky's and the water's
std::vector<FreeTexture> FreeTextures(const Level& level, const StageInfo& stage) {
    std::set<std::string> used{Upper(level.ground_texture)};
    for (const Box& b : level.boxes) used.insert(Upper(b.texture));
    for (const Model& m : level.models)
        for (size_t k = 0; k < m.textures.size(); k++)
            if (k >= m.images.size() || m.images[k].empty()) used.insert(Upper(m.textures[k]));
    Opened& t = Archive("t.con");
    std::vector<FreeTexture> out;
    for (const std::string& entry : StageTextures(stage)) {
        const std::string name = Upper(TextureName(entry));
        bool keep = used.count(Upper(entry)) > 0;
        for (const char* word : {"MOON", "SKY", "SORA", "KUMO", "SUIMEN", "MIZU"}) keep = keep || name.find(word) != std::string::npos;
        auto f = t.by_key.find(Upper(entry));
        FreeTexture spare{entry};
        if (keep || f == t.by_key.end() || !con::ReadEntry(t.source, *f->second, spare.file) || !ParseDds(spare.file, spare.dds) ||
            spare.dds.format != kDxt5)
            continue;
        out.push_back(std::move(spare));
    }
    return out;
}
}  // namespace

bool SaveLevel(Level& level, const StageInfo& stage, std::string& error) {
    LockStart(level, &stage);
    // pictures first: each converted into a free stage texture, the biggest pictures into the biggest
    // textures, before anything is written
    struct Picture {
        std::string texture;  // kImage + path, as the triangles name it
        fs::path file;
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
    };
    std::vector<Picture> pictures;
    for (const Model& m : level.models)
        for (const std::string& image : m.images) {
            if (image.empty()) continue;
            Picture p{kImage + U8(LevelFile(level, image)), LevelFile(level, image)};
            if (std::any_of(pictures.begin(), pictures.end(), [&](const Picture& q) { return q.texture == p.texture; })) continue;
            if (!tenchu::packs::ReadImage(p.file, p.px, p.w, p.h)) { error = "cannot read the picture " + U8(p.file); return false; }
            pictures.push_back(std::move(p));
        }
    std::vector<FreeTexture> spare = pictures.empty() ? std::vector<FreeTexture>{} : FreeTextures(level, stage);
    if (pictures.size() > spare.size()) {
        error = std::to_string(pictures.size()) + " pictures, but " + StageLabel(stage) + " has " + std::to_string(spare.size()) +
                " textures free for them";
        return false;
    }
    // biggest pictures choose first, each the free texture nearest its own width and height (the
    // bigger one when two are as near), so little is stretched or lost
    std::stable_sort(pictures.begin(), pictures.end(), [](const Picture& a, const Picture& b) { return uint64_t(a.w) * a.h > uint64_t(b.w) * b.h; });
    std::map<std::string, std::string> slots;                             // picture -> the texture it becomes
    std::vector<std::pair<std::string, std::vector<uint8_t>>> converted;  // file name in textures\, DDS
    for (const Picture& p : pictures) {
        auto off = [&](const FreeTexture& t) { return std::abs(std::log2(float(t.dds.w) / p.w)) + std::abs(std::log2(float(t.dds.h) / p.h)); };
        auto best = std::min_element(spare.begin(), spare.end(), [&](const FreeTexture& a, const FreeTexture& b) {
            return off(a) != off(b) ? off(a) < off(b) : uint64_t(a.dds.w) * a.dds.h > uint64_t(b.dds.w) * b.dds.h;
        });
        const FreeTexture t = std::move(*best);
        spare.erase(best);
        const std::vector<uint8_t> px = p.w == t.dds.w && p.h == t.dds.h ? p.px : Resize(p.px.data(), p.w, p.h, t.dds.w, t.dds.h);
        std::vector<uint8_t> dds = EncodeDds(t.file, t.dds, px.data(), t.dds.w, t.dds.h);
        if (dds.empty() || dds.size() > t.file.size()) { error = "converting " + U8(p.file.filename()) + " did not fit the texture"; return false; }
        dds.resize(t.file.size(), 0);  // the game's files end in zeros up to whole 2048-byte sectors
        slots[p.texture] = t.entry;
        converted.push_back({t.entry.substr(t.entry.find_last_of('\\') + 1), std::move(dds)});
    }
    std::vector<std::vector<uint8_t>> meshes;
    std::vector<uint8_t> collision;
    std::vector<uint32_t> no_ledges;
    if (!BuildPack(level, stage, meshes, collision, error, &slots, &no_ledges)) return false;
    if (meshes.empty()) meshes.emplace_back();  // mesh.bgm is always there
    const std::vector<uint8_t> enemies = EnemyChunk(level, stage, error);
    if (level.own_enemies && enemies.empty()) return false;
    if (level.folder.empty()) level.folder = SafeFolder(level.name);
    const fs::path dir = LevelsDir() / FromU8(level.folder);
    // imported models travel with the level: copied (with their .mtl) into its models folder
    std::error_code ec;
    for (Model& m : level.models) {
        const fs::path from = FromU8(m.file);
        if (!from.is_absolute()) continue;
        fs::create_directories(dir / "models", ec);
        if (!fs::equivalent(from, dir / "models" / from.filename(), ec)) fs::copy_file(from, dir / "models" / from.filename(), fs::copy_options::overwrite_existing, ec);
        fs::path mtl = from;
        mtl.replace_extension(".mtl");
        if (fs::exists(mtl, ec)) fs::copy_file(mtl, dir / "models" / mtl.filename(), fs::copy_options::overwrite_existing, ec);
        m.file = U8(fs::path("models") / from.filename());
    }
    // and their pictures into images\ (a different picture with a name already taken gets a number)
    std::map<std::string, std::string> copied;  // source -> path in the level
    std::set<std::string> names;
    for (Model& m : level.models)
        for (std::string& image : m.images) {
            const fs::path from = FromU8(image);
            if (image.empty() || !from.is_absolute()) continue;
            std::string& to = copied[U8(from)];
            if (to.empty()) {
                fs::path name = from.filename();
                for (int n = 2; names.count(Upper(U8(name))); n++)
                    name = FromU8(U8(from.stem()) + "_" + std::to_string(n) + U8(from.extension()));
                names.insert(Upper(U8(name)));
                fs::create_directories(dir / "images", ec);
                if (!fs::equivalent(from, dir / "images" / name, ec)) fs::copy_file(from, dir / "images" / name, fs::copy_options::overwrite_existing, ec);
                to = U8(fs::path("images") / name);
            }
            image = to;
        }
    // the converted textures, named as the stage textures they replace; none left from an earlier save
    for (auto& e : fs::directory_iterator(dir / "textures", ec))
        if (Upper(U8(e.path().extension())) == ".DDS") fs::remove(e.path(), ec);
    if (!converted.empty()) fs::create_directories(dir / "textures", ec);
    for (const auto& [name, dds] : converted)
        if (!WriteFile(dir / "textures" / FromU8(name), dds)) { error = "could not write " + U8(dir / "textures" / FromU8(name)); return false; }
    // what the camera and ground checks must cover: every triangle and the ground, with room around
    float bounds[4] = {level.spawn[0] - level.ground_size / 2, level.spawn[2] - level.ground_size / 2,
                       level.spawn[0] + level.ground_size / 2, level.spawn[2] + level.ground_size / 2};
    for (const Triangle& t : LevelTriangles(level))
        for (const Vec3& p : t.p) {
            bounds[0] = std::min(bounds[0], p.x); bounds[1] = std::min(bounds[1], p.z);
            bounds[2] = std::max(bounds[2], p.x); bounds[3] = std::max(bounds[3], p.z);
        }
    toml::array spawn{level.spawn[0], level.spawn[1], level.spawn[2], level.spawn[3]};
    toml::array covered{bounds[0] - 2000, bounds[1] - 2000, bounds[2] + 2000, bounds[3] + 2000};
    toml::table pack{{"name", level.name}, {"author", level.author}, {"description", level.description},
                     {"stage", level.stage}, {"spawn", spawn}, {"bounds", covered}};
    if (!no_ledges.empty()) {
        toml::array objects;
        for (uint32_t o : no_ledges) objects.push_back(int64_t(o));
        pack.insert("no_ledges", objects);
    }
    if (level.own_items) {  // [item, x, y, z], all of them (none takes the mission's away)
        toml::array items;
        for (const Item& i : level.items) items.push_back(toml::array{int64_t(i.id), i.x, i.y, i.z});
        pack.insert("items", items);
    }
    if (level.own_lights && !level.lights.empty()) {  // [where in memory, x, y, z]; taken ones go far below
        toml::array lights;
        for (const Light& l : level.lights)
            lights.push_back(toml::array{int64_t(l.va), l.removed ? 0.f : l.x, l.removed ? 100000.f : l.y, l.removed ? 0.f : l.z});
        pack.insert("lights", lights);
    }
    toml::array boxes;
    for (const Box& b : level.boxes)
        boxes.push_back(toml::table{{"x", b.x}, {"z", b.z}, {"bottom", b.bottom}, {"width", b.width}, {"depth", b.depth},
                                    {"height", b.height}, {"texture", b.texture}});
    toml::array models;
    for (const Model& m : level.models) {
        toml::array textures, images;
        for (const auto& t : m.textures) textures.push_back(t);
        for (const auto& i : m.images) images.push_back(i);
        models.push_back(toml::table{{"file", m.file}, {"name", m.name}, {"x", m.x}, {"y", m.y}, {"z", m.z}, {"yaw", m.yaw},
                                     {"scale", m.scale}, {"solid", m.solid}, {"ledges", m.ledges}, {"textures", textures}, {"images", images}});
    }
    toml::table editor{{"ground_size", level.ground_size}, {"ground_y", level.ground_y}, {"ground_texture", level.ground_texture},
                       {"texture_scale", level.texture_scale}, {"boxes", boxes}, {"models", models},
                       {"light", toml::array{level.light[0], level.light[1], level.light[2]}}, {"sun", level.sun},
                       {"sun_yaw", level.sun_yaw}, {"sun_height", level.sun_height}, {"own_enemies", level.own_enemies}};
    toml::array enemy_list;
    for (const Enemy& e : level.enemies)
        enemy_list.push_back(toml::table{{"kind", e.kind}, {"x", e.x}, {"y", e.y}, {"z", e.z}, {"facing", e.facing},
                                         {"source", e.source}, {"removed", e.removed}});
    editor.insert("enemies", enemy_list);
    editor.insert("own_lights", level.own_lights);
    toml::array light_list;
    for (const Light& l : level.lights)
        light_list.push_back(toml::table{{"kind", l.kind}, {"group", l.group}, {"index", l.index}, {"va", int64_t(l.va)},
                                         {"x", l.x}, {"y", l.y}, {"z", l.z}, {"removed", l.removed}});
    editor.insert("lights", light_list);
    editor.insert("own_items", level.own_items);
    toml::array item_list;
    for (const Item& i : level.items) item_list.push_back(toml::table{{"id", i.id}, {"x", i.x}, {"y", i.y}, {"z", i.z}});
    editor.insert("items", item_list);
    // the enemies replace the mission's only while a level has its own
    if (enemies.empty()) fs::remove(dir / "enemies.bin", ec);
    else if (!WriteFile(dir / "enemies.bin", enemies)) { error = "could not write " + U8(dir / "enemies.bin"); return false; }
    std::ostringstream a, b;
    a << "# A custom level for Tenchu: Return from Darkness (F6 > Levels). Made with RFD Switchblade.\n" << pack << "\n";
    b << "# RFD Switchblade's Levels page reopens the level from this file.\n" << editor << "\n";
    const std::string at = a.str(), bt = b.str();
    // the meshes: mesh.bgm, mesh_1.bgm ...; none left past them from an earlier save
    for (size_t k = 1; k < meshes.size(); k++)
        if (!WriteFile(dir / ("mesh_" + std::to_string(k) + ".bgm"), meshes[k])) { error = "could not write " + U8(dir); return false; }
    for (size_t k = meshes.size(); fs::exists(dir / ("mesh_" + std::to_string(k) + ".bgm"), ec); k++)
        fs::remove(dir / ("mesh_" + std::to_string(k) + ".bgm"), ec);
    if (!WriteFile(dir / "mesh.bgm", meshes[0]) || !WriteFile(dir / "collision.bin", collision) ||
        !WriteFile(dir / "level.toml", std::vector<uint8_t>(at.begin(), at.end())) ||
        !WriteFile(dir / "editor.toml", std::vector<uint8_t>(bt.begin(), bt.end()))) {
        error = "could not write " + U8(dir);
        return false;
    }
    return true;
}

bool LoadLevel(const fs::path& dir, Level& level, std::string& error) {
    try {
        auto pack = toml::parse_file(U8(dir / "level.toml"));
        auto editor = toml::parse_file(U8(dir / "editor.toml"));
        level = Level{};
        level.folder = U8(dir.filename());
        level.name = pack["name"].value_or(level.folder);
        level.author = pack["author"].value_or(std::string());
        level.description = pack["description"].value_or(std::string());
        level.stage = int(pack["stage"].value_or(int64_t(3)));
        if (auto* s = pack["spawn"].as_array())
            for (size_t i = 0; i < 4 && i < s->size(); i++) level.spawn[i] = float((*s)[i].value_or(0.0));
        level.ground_size = float(editor["ground_size"].value_or(20000.0));
        level.ground_y = float(editor["ground_y"].value_or(double(level.spawn[1])));
        level.ground_texture = editor["ground_texture"].value_or(std::string());
        level.texture_scale = float(editor["texture_scale"].value_or(400.0));
        if (auto* light = editor["light"].as_array())
            for (size_t i = 0; i < 3 && i < light->size(); i++) level.light[i] = float((*light)[i].value_or(0.5));
        level.sun = float(editor["sun"].value_or(0.0));
        level.sun_yaw = float(editor["sun_yaw"].value_or(double(level.sun_yaw)));
        level.sun_height = float(editor["sun_height"].value_or(double(level.sun_height)));
        level.own_enemies = editor["own_enemies"].value_or(false);
        if (auto* list = editor["enemies"].as_array())
            for (auto& node : *list)
                if (auto* t = node.as_table()) {
                    Enemy e;
                    e.kind = int((*t)["kind"].value_or(int64_t(0)));
                    e.x = float((*t)["x"].value_or(0.0)); e.y = float((*t)["y"].value_or(0.0)); e.z = float((*t)["z"].value_or(0.0));
                    e.facing = float((*t)["facing"].value_or(0.0));
                    e.source = int((*t)["source"].value_or(int64_t(-1)));
                    e.removed = (*t)["removed"].value_or(false);
                    level.enemies.push_back(e);
                }
        level.own_items = editor["own_items"].value_or(false);
        if (auto* list = editor["items"].as_array())
            for (auto& node : *list)
                if (auto* t = node.as_table()) {
                    Item i;
                    i.id = int((*t)["id"].value_or(int64_t(10003)));
                    i.x = float((*t)["x"].value_or(0.0)); i.y = float((*t)["y"].value_or(0.0)); i.z = float((*t)["z"].value_or(0.0));
                    level.items.push_back(i);
                }
        level.own_lights = editor["own_lights"].value_or(false);
        if (auto* list = editor["lights"].as_array())
            for (auto& node : *list)
                if (auto* t = node.as_table()) {
                    Light l;
                    l.kind = (*t)["kind"].value_or(std::string("light"));
                    l.group = int((*t)["group"].value_or(int64_t(0)));
                    l.index = int((*t)["index"].value_or(int64_t(0)));
                    l.va = uint32_t((*t)["va"].value_or(int64_t(0)));
                    l.x = float((*t)["x"].value_or(0.0)); l.y = float((*t)["y"].value_or(0.0)); l.z = float((*t)["z"].value_or(0.0));
                    l.removed = (*t)["removed"].value_or(false);
                    level.lights.push_back(l);
                }
        if (auto* boxes = editor["boxes"].as_array())
            for (auto& node : *boxes)
                if (auto* t = node.as_table()) {
                    Box b;
                    b.x = float((*t)["x"].value_or(0.0)); b.z = float((*t)["z"].value_or(0.0));
                    b.bottom = float((*t)["bottom"].value_or(0.0));
                    b.width = float((*t)["width"].value_or(400.0)); b.depth = float((*t)["depth"].value_or(400.0));
                    b.height = float((*t)["height"].value_or(200.0));
                    b.texture = (*t)["texture"].value_or(std::string());
                    level.boxes.push_back(b);
                }
        if (auto* models = editor["models"].as_array())
            for (auto& node : *models)
                if (auto* t = node.as_table()) {
                    Model m;
                    m.file = (*t)["file"].value_or(std::string());
                    m.name = (*t)["name"].value_or(std::string());
                    m.x = float((*t)["x"].value_or(0.0)); m.y = float((*t)["y"].value_or(0.0)); m.z = float((*t)["z"].value_or(0.0));
                    m.yaw = float((*t)["yaw"].value_or(0.0));
                    m.scale = float((*t)["scale"].value_or(100.0));
                    m.solid = (*t)["solid"].value_or(true);
                    m.ledges = (*t)["ledges"].value_or(true);
                    if (auto* tex = (*t)["textures"].as_array())
                        for (auto& n : *tex) m.textures.push_back(n.value_or(std::string()));
                    if (auto* pictures = (*t)["images"].as_array())
                        for (auto& n : *pictures) m.images.push_back(n.value_or(std::string()));
                    level.models.push_back(m);
                }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    return true;
}

std::vector<fs::path> SavedLevels() {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(LevelsDir(), ec))
        if (e.is_directory(ec) && fs::exists(e.path() / "editor.toml", ec)) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace wb::lv
