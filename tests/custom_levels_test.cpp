#include "port/custom_levels.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "kernel/kernel.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;
namespace levels = tenchu::levels;
void app::Save(){}
app::Settings& app::Get(){static app::Settings s;return s;}
fs::path app::ExeDir(){static fs::path p=fs::current_path()/"test_artifacts"/"custom_levels_test";return p;}
namespace logx {void Write(Level,const char*,...){} }
uint8_t* kernel::g_base=nullptr;
const kernel::Disc* kernel::MountedDisc(){return nullptr;}
const kernel::Disc::Entry* kernel::Disc::Find(const std::vector<std::string>&) const{return nullptr;}
uint32_t kernel::Disc::Read(const Entry&,uint64_t,void*,uint32_t) const{return 0;}

static int failures=0;
static void check(bool ok,const char* what){std::printf("%s: %s\n",ok?"PASS":"FAIL",what);if(!ok)failures++;}
static uint32_t U32(const std::vector<uint8_t>& b,size_t at){uint32_t v;std::memcpy(&v,&b[at],4);return v;}
static void Put32(std::vector<uint8_t>& b,uint32_t v){uint8_t t[4];std::memcpy(t,&v,4);b.insert(b.end(),t,t+4);}

// a collision.bin with objects of the given triangle counts; object i's first byte after its header is i
static std::vector<uint8_t> Objects(std::vector<uint32_t> triangles){
    std::vector<uint8_t> b;Put32(b,uint32_t(triangles.size()));
    for(size_t i=0;i<triangles.size();i++){
        std::vector<uint8_t> o(0x50+triangles[i]*0x50,0);std::memcpy(o.data(),&triangles[i],4);o[0x50-1]=uint8_t(i+1);
        b.insert(b.end(),o.begin(),o.end());
    }
    return b;
}

int main(){
    // a stage chunk like BOH.BIN's: 4 slots (first object at 0x40), 0x400 bytes
    std::vector<uint8_t> original(0x400,0xAB);
    std::memset(original.data(),0,0x30);
    original[0]=3;original[4]=0x30;original[8]=0x40;
    std::string error;
    auto chunk=levels::CollisionChunk(original,Objects({2,1}),error);
    check(chunk.size()==original.size(),"the collision chunk keeps the stage's size");
    check(U32(chunk,0)==2&&U32(chunk,0x30)==0x40&&U32(chunk,0x34)==0x40+0x50+2*0x50&&U32(chunk,0x38)==0,"objects are counted and laid out after the offset table");
    check(chunk[0x40]==2&&chunk[0x40+0x4F]==1&&chunk[U32(chunk,0x34)+0x4F]==2,"each object's bytes land at its offset");
    check(levels::CollisionChunk(original,Objects({1,1,1,1,1}),error).empty()&&error.find("1 to 4")!=std::string::npos,"more objects than the stage's slots is refused");
    check(levels::CollisionChunk(original,Objects({20}),error).empty()&&error.find("holds")!=std::string::npos,"objects bigger than the chunk are refused");
    auto cut=Objects({2});cut.resize(cut.size()-1);
    check(levels::CollisionChunk(original,cut,error).empty()&&error.find("ends inside")!=std::string::npos,"a truncated collision.bin is refused");
    // any size: as many objects and bytes as the level has, the table sized for them
    auto large=levels::CollisionChunk(original,Objects({1,1,1,1,1,20}),error,true);
    check(U32(large,0)==6&&U32(large,8)==0x30+6*4&&U32(large,0x30)==0x48,"a chunk of any size has a table just big enough");
    check(large.size()==0x48+5*0xA0+0x50+20*0x50&&large[U32(large,0x44)+0x4F]==6,"a chunk of any size takes all its objects");

    // area blocks: header kept, padded to the entry's size; an empty one draws a single degenerate triangle
    std::vector<uint8_t> header(36,0x11);
    auto empty=levels::AreaBlock(header,{},"jimmen1",4096,error);
    check(empty.size()==4096&&std::memcmp(empty.data(),header.data(),36)==0,"an emptied area keeps its header and size");
    check(U32(empty,36)==3&&levels::FirstTexture(empty,36)=="jimmen1","an emptied area is one triangle with the area's texture");
    std::vector<uint8_t> mesh(5000,1);
    check(levels::AreaBlock(header,mesh,"",4096,error).empty()&&error.find("holds 4096")!=std::string::npos,"a mesh bigger than the stage's first area is refused");
    check(levels::AreaBlock({},std::vector<uint8_t>(100,7),"",200,error).size()==200,"a mesh block is padded to the entry size");
    auto big=levels::AreaBlock(header,mesh,"",0,error);
    check(big.size()==5036&&big[36]==1,"with no size a block takes the whole mesh after its header");
    check(levels::AreaBlock({},std::vector<uint8_t>(101,7),"",0,error).size()==104,"a block of any size is whole dwords, as the game decrypts it");

    // packs: a folder needs level.toml, mesh.bgm and collision.bin
    fs::remove_all(levels::LevelsDir());
    fs::create_directories(levels::LevelsDir()/"Plane");
    std::ofstream(levels::LevelsDir()/"Plane"/"level.toml")<<"name = \"Flat plane\"\nauthor = \"me\"\nstage = 3\nspawn = [1163.0, -451.0, -11384.0, -2.6]\n";
    levels::Rescan();
    auto list=levels::List();
    check(list.size()==1&&list[0].name=="Flat plane"&&list[0].stage==3&&list[0].spawn[1]==-451.f,"level.toml is read");
    check(!list.empty()&&list[0].problem=="mesh.bgm is missing","a pack without its mesh says so");
    std::string status;
    check(!levels::Activate("Plane",status)&&status=="mesh.bgm is missing","a broken pack is not activated");
    check(levels::Activate("",status)&&levels::Active().empty(),"no custom level is the default");

    std::printf("%s\n",failures?"custom levels test FAILED":"custom levels test passed");
    return failures?1:0;
}
