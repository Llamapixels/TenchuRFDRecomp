#include "game_assets.h"
#include "images.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <future>
#include <stdexcept>

namespace wb {
static fs::path game, extracted;
const fs::path& GameDir() { return game; }
fs::path Extracted() { return extracted; }
std::string U8(const fs::path& p) { const auto s=p.u8string(); return {reinterpret_cast<const char*>(s.data()),s.size()}; }
fs::path FromU8(const std::string& s) { return fs::u8path(s); }
std::string Lower(std::string s) { for(char& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s; }
bool ReadFile(const fs::path& p,std::vector<uint8_t>& b) { std::ifstream f(p,std::ios::binary);if(!f)return false;b.assign(std::istreambuf_iterator<char>(f),{});return true; }
bool WriteFile(const fs::path& p,const std::vector<uint8_t>& b) { fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());return bool(f); }
}
static void Check(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }
int main(int argc,char** argv) { try {
    using namespace wb;
    if(argc!=3){std::puts("game_assets_test <extracted folder> <test output root>");return 2;}
    extracted=fs::absolute(argv[1]);
    game=fs::absolute(argv[2])/std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto& catalogue=assets::Catalogue();
    auto find=[&](const std::string& archive,const std::string& name)->const assets::Asset& {
        const auto it=std::find_if(catalogue.begin(),catalogue.end(),[&](const auto& a){return a.archive==archive&&Lower(a.entry)==Lower(name);});
        Check(it!=catalogue.end(),"Missing asset: "+name);return *it;
    };
    const auto& barrel=find("i.con","ST00\\taru1.msh");
    auto geometry=assets::ReadGeometry(barrel);
    Check(geometry.mesh.error.empty(),geometry.mesh.error);
    Check(geometry.mesh.faces.size()==171,"MSH groups count triangles, not strip indices");
    Check(geometry.mesh.low.y==0&&geometry.mesh.high.y>1,"Original units and ground placement origin");
    Check(Lower(geometry.textures.at(0))=="mapobj00.con\\taru.dds","Props choose the matching stage's mapobj texture");
    size_t models=0,failed=0,missing=0;
    for(const auto& a:catalogue) if(a.kind==assets::Kind::Model) {
        models++;const auto g=assets::ReadGeometry(a);
        if(!g.mesh.error.empty()){failed++;std::printf("Unsupported: %s\n",a.entry.c_str());continue;}
        Check(!g.mesh.faces.empty()&&g.mesh.materials.size()==g.textures.size(),"Parsed model has materials and faces");
        for(const auto& f:g.mesh.faces)for(const auto& p:f.p)Check(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"Finite geometry");
        missing+=std::count(g.textures.begin(),g.textures.end(),std::string());
    }
    Check(models>=700&&failed<10,"Static model catalogue coverage");
    Bitmap thumb;Check(assets::Thumbnail(barrel,thumb)&&thumb.px.size()==128*128*4,"Rendered model thumbnail");
    size_t coloured=0;for(size_t at=0;at<thumb.px.size();at+=4)if(thumb.px[at]!=38||thumb.px[at+1]!=34||thumb.px[at+2]!=30)coloured++;
    Check(coloured>500,"Thumbnail contains visible textured geometry");
    fs::create_directories(game);WritePng(game/"barrel.png",thumb.px.data(),thumb.w,thumb.h);
    std::string error;const auto stages=lv::Stages(error);
    const auto stage=std::find_if(stages.begin(),stages.end(),[](const auto& s){return s.stage==3;});
    Check(stage!=stages.end(),"Stage 3 available");auto level=lv::NewLevel(*stage);level.name=level.folder="asset-browser-regression";level.ground_size=2000;
    const auto& stage_enemies = lv::StageEnemies(*stage).enemies;
    const auto& kinds = lv::EnemyKinds();
    const auto other_kind = std::find_if(kinds.begin(), kinds.end(), [&](int kind) {
        return std::none_of(stage_enemies.begin(), stage_enemies.end(), [&](const auto& e) { return e.kind == kind; });
    });
    Check(other_kind != kinds.end(), "Enemy catalogue contains types from other missions");
    level.own_enemies = true;
    auto foreign_enemy = stage_enemies.front();
    foreign_enemy.kind = *other_kind;
    level.enemies = {foreign_enemy};
    const auto chunk = lv::EnemyChunk(level, *stage, error);
    Check(chunk.size() == 80 && (int(chunk[16]) | (int(chunk[17]) << 8)) == *other_kind,
          "Changing an enemy to a foreign kind writes its template and clears its original patrol");
    Check(assets::PlaceModel(level,barrel,123,456,error),error);
    Check(level.models[0].scale==1&&level.models[0].x==123&&level.models[0].z==456&&!level.models[0].ledges,"Placed model size, position and mesh-only collision");
    const auto imported=lv::ReadModel(lv::ModelPath(level,level.models[0]));
    Check(imported->faces.size()==geometry.mesh.faces.size(),"All native triangles survive OBJ conversion");
    for(size_t k=0;k<geometry.mesh.faces.size();k++)for(int j=0;j<3;j++) {
        const auto& a=geometry.mesh.faces[k];const auto& b=imported->faces[k];
        Check(std::fabs(a.p[j].x-b.p[j].x)<.001f&&std::fabs(a.p[j].y-b.p[j].y)<.001f&&std::fabs(a.p[j].z-b.p[j].z)<.001f,"Handedness/winding round trip");
        Check(std::fabs(a.uv[j][1]-b.uv[j][1])<.00001f,"Texture coordinates round trip");
    }
    level.boxes.push_back({0,0,level.ground_y,100,100,100,level.ground_texture});
    const auto& texture=find("t.con",geometry.textures[0]);
    const auto& texture2=find("t.con","st00_a00.con\\gake11.dds");
    Bitmap original;Check(lv::LoadTexture(texture.entry,original.px,original.w,original.h),"Reference texture decodes");
    std::vector<std::future<bool>> workers;
    for(int i=0;i<6;i++)workers.push_back(std::async(std::launch::async,[&]{
        for(int j=0;j<10;j++){Bitmap b;if(!lv::LoadTexture(texture.entry,b.px,b.w,b.h)||b.px!=original.px)return false;}
        return true;
    }));
    for(auto& worker:workers)Check(worker.get(),"Concurrent thumbnails read archive streams safely");
    Check(assets::ApplyTexture(level,texture,0,-1,-1,0,error),error);
    Check(assets::ApplyTexture(level,texture2,1,0,-1,0,error),error);
    Check(level.ground_texture.starts_with(lv::kImage)&&level.boxes[0].texture.starts_with(lv::kImage),"Cross-stage textures are portable pictures");
    Check(!assets::ApplyTexture(level,texture,1,8,-1,0,error),"Invalid target rejected");
    Check(lv::SaveLevel(level,*stage,error),error);
    lv::Level loaded;Check(lv::LoadLevel(lv::LevelsDir()/level.folder,loaded,error),error);
    const auto ground=loaded.ground_texture.substr(sizeof(lv::kImage)-1),box=loaded.boxes[0].texture.substr(sizeof(lv::kImage)-1);
    Check(!FromU8(ground).is_absolute()&&!FromU8(box).is_absolute()&&ground!=box,"Unique relative picture names");
    Check(fs::exists(lv::LevelFile(loaded,ground))&&fs::exists(lv::LevelFile(loaded,box)),"Ground/box images travel with the level");
    Check(!FromU8(loaded.models[0].file).is_absolute()&&fs::exists(lv::ModelPath(loaded,loaded.models[0])),"Native model travels with the level");
    for(const auto& triangle:lv::LevelTriangles(loaded))if(triangle.texture.starts_with(lv::kImage)) {
        Bitmap b;Check(lv::LoadTexture(triangle.texture,b.px,b.w,b.h),"Reopened surface picture loads");
    }
    std::vector<uint8_t> before,after;ReadFile(lv::LevelFile(loaded,ground),before);
    Check(assets::ApplyTexture(loaded,texture2,1,0,-1,0,error),error);
    Check(lv::SaveLevel(loaded,*stage,error),error);
    ReadFile(lv::LevelFile(loaded,ground),after);Check(before==after,"Second save never overwrites an existing different picture");
    auto count_textures=[&]{size_t n=0;for(const auto& e:fs::directory_iterator(lv::LevelsDir()/level.folder/"textures"))if(e.path().extension()==".dds")n++;return n;};
    const auto slots=count_textures();
    Check(assets::PlaceModel(loaded,barrel,789,123,error),error);
    Check(lv::SaveLevel(loaded,*stage,error),error);
    Check(count_textures()==slots,"Repeated assets share texture slots after saving/reopening");
    Check(assets::ApplyTexture(loaded,texture2,2,-1,0,0,error),error);
    Check(lv::SaveLevel(loaded,*stage,error),error);
    Check(!loaded.models[0].images[0].empty()&&!FromU8(loaded.models[0].images[0]).is_absolute(),"Applied model material saves a portable picture");
    Check(fs::exists(lv::LevelsDir()/level.folder/"mesh.bgm"),"Game-ready mesh saved");
    std::printf("PASS: %zu static models, %zu supported, %zu unresolved material textures; thumbnail, concurrent archive reads, placement, material application and portable save/reopen passed. Output: %s\n",models,models-failed,missing,U8(game).c_str());
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;} }
