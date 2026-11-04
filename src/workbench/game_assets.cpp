#include "game_assets.h"
#include "images.h"
#include "port/connect_file.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

namespace wb::assets {
namespace {
namespace con = tenchu::con;
uint32_t Word(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
float Float(const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }
uint16_t Index(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
struct Archive {
    con::Source source;
    con::Archive entries;
    std::map<std::string, size_t> lookup;
};
struct Library {
    std::map<std::string, Archive> archives;
    std::map<std::string, std::vector<std::string>> textures;
    std::vector<Asset> assets;
    Library() {
        for (const char* name : {"t.con", "i.con", "g.con"}) {
            auto& a = archives[name];
            const auto file = Extracted() / "Media" / name;
            std::error_code ec;
            const auto size = fs::file_size(file, ec);
            if (ec) continue;
            a.source = con::FileSource(file);
            std::string error;
            if (!con::Parse(a.source, size, a.entries, error)) continue;
            for (size_t i = 0; i < a.entries.entries.size(); i++) {
                const auto& entry = a.entries.entries[i].name;
                a.lookup[con::Key(entry)] = i;
                const auto slash = entry.find_last_of("\\/");
                const auto folder = slash == std::string::npos ? "" : entry.substr(0, slash);
                const auto base = entry.substr(slash + 1);
                const auto lower = Lower(base);
                const bool texture = lower.ends_with(".dds");
                const bool model = lower.ends_with(".msh") || lower.ends_with(".bgm");
                if (!texture && !model) continue;
                assets.push_back({texture ? Kind::Texture : Kind::Model, name, entry, folder, base, lower.ends_with(".bgm")});
                if (texture) textures[Lower(lv::TextureName(entry))].push_back(entry);
            }
        }
        std::sort(assets.begin(), assets.end(), [](const Asset& a, const Asset& b) {
            if (a.kind != b.kind) return a.kind < b.kind;
            return Lower(a.entry) < Lower(b.entry);
        });
    }
};
Library& Lib() { static Library l; return l; }
bool Bytes(const Asset& asset, std::vector<uint8_t>& out) {
    const auto a = Lib().archives.find(asset.archive);
    if (a == Lib().archives.end()) return false;
    const auto e = a->second.lookup.find(con::Key(asset.entry));
    // Each worker gets its own seekable stream rather than sharing seek/read state.
    return e != a->second.lookup.end() && con::ReadEntry(con::FileSource(Extracted() / "Media" / asset.archive), a->second.entries.entries[e->second], out);
}
std::string ResolveTexture(const Asset& asset, const std::string& name) {
    const auto found = Lib().textures.find(Lower(name));
    if (found == Lib().textures.end()) return {};
    auto folder = Lower(asset.folder);
    auto area = Lower(lv::TextureName(asset.entry));
    if (const auto cut = area.rfind('_'); cut != std::string::npos) area.resize(cut);
    std::string best;
    int best_score = -1;
    for (const auto& entry : found->second) {
        const auto path = Lower(entry);
        const auto prefix = path.substr(0, path.find_last_of("\\/"));
        int score = 0;
        if (prefix == folder + ".con") score = 80;
        if (folder.starts_with("st") && prefix == "mapobj" + folder.substr(2) + ".con") score = 100;
        if (folder.starts_with("st") && prefix.starts_with(folder + "_")) score = std::max(score, 60);
        if (prefix == area + ".con") score = asset.stage_piece ? 120 : std::max(score, 70);
        if (prefix == "item.con" && folder == "item") score = 100;
        if (score > best_score) { best_score = score; best = entry; }
    }
    return best;
}
// BGM uses strips and 80-byte groups; MSH uses triangle lists. MSH's two
// vertex layouts have different material records (56 / 120 bytes).
bool Parse(const Asset& asset, const std::vector<uint8_t>& b, size_t offset, size_t stride, Geometry& out) {
    if (offset + 4 > b.size()) return false;
    const uint32_t nv = Word(&b[offset]);
    if (!nv || nv > 1000000) return false;
    const size_t vertices = offset + 4, indices_count = vertices + size_t(nv) * stride;
    if (indices_count + 4 > b.size()) return false;
    const uint32_t ni = Word(&b[indices_count]);
    const size_t indices = indices_count + 4, groups_count = indices + size_t(ni) * 2;
    if (!ni || ni > 4000000 || groups_count + 4 > b.size()) return false;
    const uint32_t ng = Word(&b[groups_count]);
    const size_t groups = groups_count + 4, group_size = asset.stage_piece ? 80 : stride == 24 ? 56 : 120;
    if (!ng || ng > 10000 || groups + size_t(ng) * group_size > b.size()) return false;
    for (uint32_t i = 0; i < nv; i++) {
        const auto* v = &b[vertices + size_t(i) * stride];
        for (size_t at : {size_t(0), size_t(4), size_t(8), stride - 8, stride - 4})
            if (!std::isfinite(Float(v + at)) || std::fabs(Float(v + at)) > 1e8f) return false;
    }
    Geometry g;
    for (uint32_t k = 0; k < ng; k++) {
        const auto* r = &b[groups + size_t(k) * group_size];
        const uint64_t multiplier = asset.stage_piece ? 1 : 3;
        const uint64_t first = Word(r) * multiplier, count = Word(r + 4) * multiplier;
        if (first + count > ni) return false;
        const std::string texture(reinterpret_cast<const char*>(r + 20), strnlen(reinterpret_cast<const char*>(r + 20), 32));
        g.mesh.materials.push_back(texture.empty() ? "material_" + std::to_string(k) : texture);
        g.textures.push_back(ResolveTexture(asset, texture));
        for (uint64_t i = 0; i + 2 < count; i += asset.stage_piece ? 1 : 3) {
            uint16_t ix[3];
            for (int j = 0; j < 3; j++) ix[j] = Index(&b[indices + (first + i + j) * 2]);
            if (ix[0] >= nv || ix[1] >= nv || ix[2] >= nv) return false;
            if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) continue;
            if (asset.stage_piece && i % 2) std::swap(ix[0], ix[1]);
            lv::ModelMesh::Face face{};
            face.material = int(k);
            for (int j = 0; j < 3; j++) {
                const auto* v = &b[vertices + size_t(ix[j]) * stride];
                face.p[j] = {Float(v), Float(v + 4), Float(v + 8)};
                face.uv[j][0] = Float(v + stride - 8);
                face.uv[j][1] = Float(v + stride - 4);
            }
            g.mesh.faces.push_back(face);
        }
    }
    if (g.mesh.faces.empty()) return false;
    auto& lo = g.mesh.low; auto& hi = g.mesh.high;
    lo = hi = g.mesh.faces[0].p[0];
    for (const auto& f : g.mesh.faces) for (const auto& p : f.p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    // Local placement origin: centre in x/z, lowest point at y=0.
    const lv::Vec3 origin{(lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2};
    for (auto& f : g.mesh.faces) for (auto& p : f.p) { p.x -= origin.x; p.y -= origin.y; p.z -= origin.z; }
    lo = {lo.x - origin.x, 0, lo.z - origin.z};
    hi = {hi.x - origin.x, hi.y - origin.y, hi.z - origin.z};
    g.mesh.images.resize(g.mesh.materials.size());
    out = std::move(g);
    return true;
}
fs::path Cache(const std::string& key) {
    std::ostringstream name; name << std::hex << Hash(key.data(), key.size());
    return GameDir() / "tools" / "cache" / "level-assets-v1" / name.str();
}
bool NativeTexture(const lv::Level& level, const std::string& entry) {
    if (entry.empty()) return false;
    static const auto stages = [] { std::string error; return lv::Stages(error); }();
    for (const auto& stage : stages) if (stage.stage == level.stage) {
        const auto textures = lv::StageTextures(stage);
        return std::find(textures.begin(), textures.end(), entry) != textures.end();
    }
    return false;
}
}  // namespace

const std::vector<Asset>& Catalogue() { return Lib().assets; }
Geometry ReadGeometry(const Asset& asset) {
    Geometry g;
    std::vector<uint8_t> bytes;
    if (!Bytes(asset, bytes)) { g.mesh.error = "Cannot read " + asset.entry; return g; }
    if (asset.stage_piece) {
        const bool header = Lower(asset.entry).ends_with("_00.bgm");
        if (Parse(asset, bytes, header ? 36 : 0, 24, g)) return g;
    } else {
        for (size_t stride : {24u, 32u}) if (Parse(asset, bytes, 0, stride, g)) return g;
    }
    g.mesh.error = "Unsupported or empty geometry: " + asset.entry;
    return g;
}

bool TextureFile(const std::string& entry, fs::path& png, std::string& error) {
    png = Cache("texture|" + entry) / "texture.png";
    std::error_code ec;
    if (fs::is_regular_file(png, ec)) return true;
    Bitmap b;
    if (!lv::LoadTexture(entry, b.px, b.w, b.h)) { error = "Cannot decode " + entry; return false; }
    fs::create_directories(png.parent_path(), ec);
    if (!WritePng(png, b.px.data(), b.w, b.h)) { error = "Cannot cache " + entry; return false; }
    return true;
}

bool PlaceModel(lv::Level& level, const Asset& asset, float x, float z, std::string& error) {
    if (asset.kind != Kind::Model) { error = "Choose a model to place"; return false; }
    auto g = ReadGeometry(asset);
    if (!g.mesh.error.empty()) { error = g.mesh.error; return false; }
    const auto file = Cache(asset.Key()) / "asset.obj";
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // Each asset has a unique basename when copied into the level's models folder.
    const auto stem = file.parent_path().filename();
    const auto obj = file.parent_path() / (stem.string() + ".obj");
    const auto mtl = file.parent_path() / (stem.string() + ".mtl");
    std::ofstream materials(mtl), model(obj);
    if (!model || !materials) { error = "Cannot write the asset cache"; return false; }
    model << std::setprecision(9) << "mtllib " << U8(mtl.filename()) << '\n';
    for (size_t k = 0; k < g.mesh.materials.size(); k++) {
        fs::path png;
        if (!g.textures[k].empty() && !TextureFile(g.textures[k], png, error)) return false;
        materials << "newmtl material_" << k << "\nKd 1 1 1\n";
        if (!png.empty()) materials << "map_Kd " << U8(png) << '\n';
    }
    // OBJ is right handed: reverse exactly the transform performed by ReadModel.
    size_t vertex = 1;
    for (const auto& face : g.mesh.faces) {
        model << "usemtl material_" << face.material << '\n';
        for (const auto& p : face.p) model << "v " << p.x << ' ' << p.y << ' ' << -p.z << '\n';
        for (const auto& uv : face.uv) model << "vt " << uv[0] << ' ' << 1 - uv[1] << '\n';
        model << "f " << vertex << '/' << vertex << ' ' << vertex + 2 << '/' << vertex + 2 << ' ' << vertex + 1 << '/' << vertex + 1 << '\n';
        vertex += 3;
    }
    model.close(); materials.close();
    if (!model || !materials) { error = "Could not finish caching the model"; return false; }
    if (!lv::AddModel(level, obj, x, z, error)) return false;
    auto& placed = level.models.back();
    placed.name = asset.entry;
    placed.scale = 1;
    placed.y = level.ground_y;
    // Mesh-only collision avoids filling a doorway's entire footprint up to its roof.
    placed.ledges = false;
    const auto imported = lv::ReadModel(obj);
    for (size_t k = 0; k < imported->materials.size(); k++) {
        // Empty native groups emit no usemtl, so OBJ material indices can be compacted.
        const auto source = size_t(std::strtoul(imported->materials[k].c_str() + std::strlen("material_"), nullptr, 10));
        if (source < g.textures.size() && NativeTexture(level, g.textures[source])) {
            placed.textures[k] = g.textures[source]; placed.images[k].clear();
        }
    }
    return true;
}

bool ApplyTexture(lv::Level& level, const Asset& asset, int target, int box, int model, int material, std::string& error) {
    if (asset.kind != Kind::Texture) { error = "Choose a texture to apply"; return false; }
    if (target < 0 || target > 2 || (target == 1 && (box < 0 || box >= int(level.boxes.size()))) ||
        (target == 2 && (model < 0 || model >= int(level.models.size()) || material < 0 ||
                        material >= int(lv::ReadModel(lv::ModelPath(level, level.models[model]))->materials.size())))) {
        error = "Select a box or model material first"; return false;
    }
    const bool native = NativeTexture(level, asset.entry);
    fs::path png;
    if (!native && !TextureFile(asset.entry, png, error)) return false;
    const std::string texture = native ? asset.entry : lv::kImage + U8(png);
    if (target == 0) level.ground_texture = texture;
    if (target == 1) level.boxes[box].texture = texture;
    if (target == 2) {
        auto& m = level.models[model];
        const auto count = lv::ReadModel(lv::ModelPath(level, m))->materials.size();
        m.textures.resize(count, level.ground_texture); m.images.resize(count);
        m.textures[material] = native ? asset.entry : level.ground_texture;
        m.images[material] = native ? "" : U8(png);
    }
    return true;
}

bool Thumbnail(const Asset& asset, Bitmap& out, uint32_t side) {
    if (asset.kind == Kind::Texture) return lv::LoadTexture(asset.entry, out.px, out.w, out.h);
    auto g = ReadGeometry(asset);
    if (!g.mesh.error.empty() || side < 16 || side > 1024) return false;
    out.w = out.h = side;
    out.px.resize(size_t(side) * side * 4);
    for (size_t i = 0; i < out.px.size(); i += 4) { out.px[i] = 38; out.px[i+1] = 34; out.px[i+2] = 30; out.px[i+3] = 255; }
    std::vector<float> depth(size_t(side) * side, std::numeric_limits<float>::max());
    std::vector<Bitmap> pictures(g.textures.size());
    for (size_t k = 0; k < pictures.size(); k++)
        if (!g.textures[k].empty()) lv::LoadTexture(g.textures[k], pictures[k].px, pictures[k].w, pictures[k].h);
    auto project = [](lv::Vec3 p) { return lv::Vec3{.707107f*(p.x-p.z), .816497f*p.y-.408248f*(p.x+p.z), .57735f*(p.x+p.y+p.z)}; };
    float lo[2] = {1e30f,1e30f}, hi[2] = {-1e30f,-1e30f};
    for (const auto& f : g.mesh.faces) for (const auto& p : f.p) {
        const auto q = project(p);
        lo[0]=std::min(lo[0],q.x);lo[1]=std::min(lo[1],q.y);hi[0]=std::max(hi[0],q.x);hi[1]=std::max(hi[1],q.y);
    }
    const float scale = (side-12) / std::max({hi[0]-lo[0],hi[1]-lo[1],.001f});
    auto edge=[](lv::Vec3 a,lv::Vec3 b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);};
    for (const auto& f : g.mesh.faces) {
        lv::Vec3 p[3];
        for(int j=0;j<3;j++){p[j]=project(f.p[j]);p[j].x=side*.5f+(p[j].x-(lo[0]+hi[0])*.5f)*scale;p[j].y=side*.5f-(p[j].y-(lo[1]+hi[1])*.5f)*scale;}
        const float area=edge(p[0],p[1],p[2].x,p[2].y); if(std::fabs(area)<1e-5f)continue;
        const int x0=std::max(0,int(std::floor(std::min({p[0].x,p[1].x,p[2].x})))),x1=std::min(int(side)-1,int(std::ceil(std::max({p[0].x,p[1].x,p[2].x}))));
        const int y0=std::max(0,int(std::floor(std::min({p[0].y,p[1].y,p[2].y})))),y1=std::min(int(side)-1,int(std::ceil(std::max({p[0].y,p[1].y,p[2].y}))));
        const auto& tex=pictures[f.material];
        for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
            const float w[3]={edge(p[1],p[2],x+.5f,y+.5f)/area,edge(p[2],p[0],x+.5f,y+.5f)/area,edge(p[0],p[1],x+.5f,y+.5f)/area};
            if(w[0]<0||w[1]<0||w[2]<0)continue;
            const float z=-(w[0]*p[0].z+w[1]*p[1].z+w[2]*p[2].z); const size_t at=size_t(y)*side+x;
            if(z>=depth[at])continue;
            const uint8_t fallback[4]={178,190,195,255}; const uint8_t* colour=fallback;
            if(tex.w&&tex.h&&!tex.px.empty()){
                float u=0,v=0;for(int j=0;j<3;j++){u+=w[j]*f.uv[j][0];v+=w[j]*f.uv[j][1];}
                const auto tx=std::min(tex.w-1,uint32_t((u-std::floor(u))*tex.w)),ty=std::min(tex.h-1,uint32_t((v-std::floor(v))*tex.h));
                colour=&tex.px[(size_t(ty)*tex.w+tx)*4];if(colour[3]<64)continue;
            }
            depth[at]=z;std::memcpy(&out.px[at*4],colour,3);
        }
    }
    return true;
}
}  // namespace wb::assets
