#include "images.h"
#include "pack_model.h"
#include "port/connect_file.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

// a DDS header like the game's: DXT5 by FourCC, or 32-bit A8R8G8B8 masks
static std::vector<uint8_t> Header(uint32_t format,uint32_t w,uint32_t h){
    std::vector<uint8_t> b(128,0);auto set=[&](size_t at,uint32_t v){std::memcpy(&b[at],&v,4);};
    std::memcpy(b.data(),"DDS ",4);set(4,124);set(8,0x1007);set(12,h);set(16,w);set(76,32);
    if(format==wb::kDxt5){set(80,4);std::memcpy(&b[84],"DXT5",4);}
    else{set(80,0x41);set(88,32);set(92,0xFF0000);set(96,0xFF00);set(100,0xFF);set(104,0xFF000000);}
    set(108,0x1000);return b;
}

// blocks of two colours laid out by a seed: the same seed is the same picture
static std::vector<uint8_t> Pattern(uint32_t seed,uint32_t side,const uint8_t a[3],const uint8_t b[3]){
    std::vector<uint8_t> px(size_t(side)*side*4);uint32_t s=seed*2654435761u+1;bool cell[8][8];
    for(auto& row:cell)for(bool& c:row){s=s*1664525u+1013904223u;c=(s>>16)&1;}
    for(uint32_t y=0;y<side;y++)for(uint32_t x=0;x<side;x++){const uint8_t* c=cell[y*8/side][x*8/side]?a:b;uint8_t* p=&px[(size_t(y)*side+x)*4];p[0]=c[0];p[1]=c[1];p[2]=c[2];p[3]=255;}
    return px;
}

static void PackModel(){
    namespace pm=wb::pm;auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    check(pm::GroupOf("village\\1013e8f22977e470-3e4c17984f93affc-00006213.png")=="1013e8f22977e470"&&pm::GroupOf("map\\castle.png")=="castle","PCSX2 picture hashes group palettes");
    const uint8_t brown[3]={40,70,120},tan[3]={150,190,210},green[3]={40,140,40},pale[3]={170,220,170},grey[3]={128,128,128},dark[3]={30,30,30};
    pm::Model m;m.dir="pack";
    auto image=[&](const char* file,uint32_t seed,const uint8_t* a,const uint8_t* b){
        tenchu::packs::PackImage img;img.file=file;img.ok=true;auto px=Pattern(seed,256,a,b);img.d=tenchu::packs::Describe(px.data(),256,256);m.index.push_back(img);};
    image("a\\00000000000000aa-0000000000000001-00006213.png",1,brown,tan);   // one picture in two palettes
    image("a\\00000000000000aa-0000000000000002-00006213.png",1,green,pale);
    image("b\\other.png",2,grey,dark);
    pm::Prepare(m);
    auto tex=[&](uint32_t seed,const uint8_t* a,const uint8_t* b,uint64_t key){
        pm::Tex t;t.w=t.h=64;t.key=key;auto px=Pattern(seed,64,a,b);t.d=tenchu::packs::Describe(px.data(),64,64);t.places.push_back({"Media\\t.con","x.dds"});m.tex.push_back(t);};
    tex(1,brown,tan,11);tex(1,green,pale,12);tex(2,grey,dark,13);tex(3,brown,grey,14);
    for(auto& t:m.tex)t.cands=pm::Match(t,m);
    pm::Rank(m);
    check(pm::AutoImage(m.tex[0])==0&&pm::AutoImage(m.tex[1])==1,"each palette goes to the texture with its colours");
    check(m.tex[0].cands.size()==2,"a palette group is offered once");
    check(pm::AutoImage(m.tex[2])==2&&pm::StatusOf(m.tex[3])==pm::kNoMatch,"other pictures match only themselves");
    auto all=pm::AllImages(m.tex[3],m);check(all.size()==3,"every image can be browsed");
    m.tex[2].choice=pm::Choice::Skip;m.tex[3].choice=pm::Choice::Pick;m.tex[3].pick=2;
    auto saved=pm::Collect(m);
    check(saved.use.size()==3&&saved.use[11]==m.index[0].file&&saved.use[14]=="b\\other.png"&&saved.skip.count(13),"saving writes every image in use and the skips");
    for(auto& t:m.tex){t.choice=pm::Choice::Auto;t.pick=-1;}
    pm::Apply(m,saved);
    check(m.tex[0].choice==pm::Choice::Auto&&m.tex[2].choice==pm::Choice::Skip&&m.tex[3].choice==pm::Choice::Pick&&m.tex[3].pick==2,"saved matches load back");
}

int main(int argc,char** argv){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    // keys as the game computes them (values from its own hash, checked against keys the game logged)
    auto key=[&](uint32_t w,uint32_t h,int mul,int add){
        auto f=Header(wb::kDxt5,w,h);for(uint32_t i=0;i<((w+3)/4)*((h+3)/4)*16;i++)f.push_back(uint8_t(i*mul+add));
        wb::Dds d;check(wb::ParseDds(f,d)&&d.format==wb::kDxt5&&d.w==w&&d.h==h,"DXT5 header must parse");return wb::GameKey(f,d);};
    check(key(8,4,7,3)==0x7a4d4037d58550a0ull,"texture key must match the game's (8x4)");
    check(key(16,16,13,5)==0x6f0f101ed936e304ull,"texture key must match the game's (16x16)");

    // DXT5 round trip of a smooth image with an alpha ramp stays close
    const uint32_t w=64,h=48;std::vector<uint8_t> px(w*h*4);
    for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){uint8_t* p=&px[(y*w+x)*4];p[0]=uint8_t(x*4);p[1]=uint8_t(y*5);p[2]=uint8_t(255-x*2);p[3]=uint8_t(y*5);}
    wb::Dds like{w,h,wb::kDxt5};auto file=wb::EncodeDds(Header(wb::kDxt5,w,h),like,px.data(),w,h);
    wb::Dds d;std::vector<uint8_t> back;
    check(wb::ParseDds(file,d)&&d.w==w&&d.h==h&&wb::DecodeDds(file,d,back),"encoded DXT5 must decode");
    double err[4]={};for(size_t i=0;i<px.size();i++)err[i%4]+=std::abs(int(px[i])-int(back[i]));
    for(double& e:err)e/=w*h;
    std::printf("DXT5 mean error B %.2f G %.2f R %.2f A %.2f\n",err[0],err[1],err[2],err[3]);
    check(err[0]<6&&err[1]<6&&err[2]<6&&err[3]<3,"DXT5 encoding must stay close to the source");
    // a size that is not a multiple of 4
    auto odd=wb::EncodeDds(Header(wb::kDxt5,6,5),wb::Dds{6,5,wb::kDxt5},px.data(),6,5);
    check(wb::ParseDds(odd,d)&&d.w==6&&d.h==5&&odd.size()==128+4*16,"edge blocks must be written");

    // A8R8G8B8 is copied exactly, and its key is unknown offline (the game swizzles it)
    wb::Dds argb{w,h,wb::kA8R8G8B8};auto raw=wb::EncodeDds(Header(wb::kA8R8G8B8,w,h),argb,px.data(),w,h);
    check(wb::ParseDds(raw,d)&&d.format==wb::kA8R8G8B8&&wb::DecodeDds(raw,d,back)&&back==px,"A8R8G8B8 must round trip exactly");
    check(wb::GameKey(raw,d)==0,"A8R8G8B8 has no offline key");

    // resizing keeps flat colour flat both ways
    std::vector<uint8_t> flat(32*32*4,0);for(size_t i=0;i<flat.size();i+=4){flat[i]=10;flat[i+1]=200;flat[i+2]=30;flat[i+3]=255;}
    auto small=wb::Resize(flat.data(),32,32,8,4),big=wb::Resize(flat.data(),32,32,100,60);
    check(small.size()==8*4*4&&big.size()==100*60*4&&small[1]==200&&big[(59*100+99)*4+2]==30,"resizing");
    check(!wb::ParseDds(std::vector<uint8_t>(20,0),d),"short files are not textures");
    PackModel();
    // real archives, when given: every texture decodes, re-encodes and decodes again close to itself
    for(int a=1;a<argc;++a){
        namespace con=tenchu::con;con::Archive arc;std::string error;auto src=con::FileSource(argv[a]);
        check(con::Parse(src,std::filesystem::file_size(argv[a]),arc,error),"archive must parse");
        size_t n=0,keyed=0;double worst=0;
        for(const auto& e:arc.entries){
            std::vector<uint8_t> file,px,again;
            if(e.name.size()<4||_stricmp(e.name.c_str()+e.name.size()-4,".dds"))continue;
            check(con::ReadEntry(src,e,file)&&wb::ParseDds(file,d)&&wb::DecodeDds(file,d,px),e.name.c_str());
            auto re=wb::EncodeDds(file,d,px.data(),d.w,d.h);wb::Dds d2;
            check(wb::ParseDds(re,d2)&&d2.w==d.w&&d2.h==d.h&&d2.format==d.format&&wb::DecodeDds(re,d2,again),"re-encoded texture must read back");
            double sum=0;for(size_t i=0;i<px.size();i++)sum+=std::abs(int(px[i])-int(again[i]));
            worst=std::max(worst,sum/px.size());n++;keyed+=wb::GameKey(file,d)!=0;
        }
        std::printf("%s: %zu textures re-encoded, worst mean error %.2f, %zu with game keys\n",argv[a],n,worst,keyed);
        check(worst<12,"re-encoding real textures must stay close");
    }
    std::puts("PASS: game texture keys, DXT5 encode/decode, edge blocks, A8R8G8B8 copy, resize and the pack matcher model");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
