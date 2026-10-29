#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "app/app.h"
#include "core/log.h"
#include "graphics/texture_packs.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace packs = tenchu::packs;

void app::Save(){}
app::Settings& app::Get(){static app::Settings s;return s;}
fs::path app::ExeDir(){static fs::path p=fs::current_path()/"test_artifacts"/"texture_packs_test";return p;}
namespace logx {void Write(Level,const char*,...){} }

namespace {
using Pixels=std::vector<uint8_t>;
// a detailed pattern in two colours; size scales it, so a bigger copy looks the same
Pixels Pattern(uint32_t size,const uint8_t a[3],const uint8_t b[3],uint8_t alpha,int variant=0){
    Pixels px(size_t(size)*size*4);
    for(uint32_t y=0;y<size;++y)for(uint32_t x=0;x<size;++x){
        float u=float(x)/size,v=float(y)/size;
        float t=variant==0?.5f+.5f*std::sin(u*19+v*7)*std::cos(v*13-u*3):.5f+.5f*std::sin(v*23-u*11)*std::cos(u*17);
        uint8_t* p=&px[(size_t(y)*size+x)*4];
        for(int c=0;c<3;++c)p[c]=uint8_t(a[c]*t+b[c]*(1-t));
        p[3]=alpha;
    }
    return px;
}
void WritePng(const fs::path& path,const Pixels& px,uint32_t size){
    ComPtr<IWICImagingFactory> wic;ComPtr<IWICStream> file;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    if(SUCCEEDED(hr))hr=wic->CreateStream(&file);
    if(SUCCEEDED(hr))hr=file->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(file.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=frame->SetSize(size,size);
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr))hr=frame->WritePixels(size,size*4,UINT(px.size()),const_cast<BYTE*>(px.data()));
    if(SUCCEEDED(hr))hr=frame->Commit();
    if(SUCCEEDED(hr))hr=encoder->Commit();
    if(FAILED(hr))throw std::runtime_error("cannot write test PNG");
}
bool WaitReady(){
    for(int i=0;i<400;++i){
        auto list=packs::Packs();
        if(!list.empty()&&!list[0].indexing)return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}
packs::Result Find(uint64_t key,fs::path& path,bool& ps2){
    for(int i=0;i<200;++i){auto r=packs::Find(key,path,ps2);if(r!=packs::Result::Pending)return r;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    return packs::Result::Pending;
}
}

int main(){try{
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::error_code ec;fs::remove_all(app::ExeDir(),ec);
    const fs::path pack=app::ExeDir()/"textures"/"packs"/"Test pack";fs::create_directories(pack/"level");
    const uint8_t brown[3]={40,80,140},tan[3]={120,170,210},green[3]={60,160,60},grey[3]={128,128,128},red[3]={30,30,200};
    // the pack: an HD copy with PS2 alpha, a recoloured copy, and a different pattern in the same colours
    WritePng(pack/"level"/"hd_brown.png",Pattern(256,brown,tan,128),256);
    WritePng(pack/"hd_green.png",Pattern(256,green,tan,255),256);
    WritePng(pack/"other.png",Pattern(256,brown,tan,255,1),256);
    // an edited Xbox dump keeps its key in its name; its alpha is the Xbox's even when it looks like the PS2's
    WritePng(pack/"64x64_00000000000beef0_DXT5.png",Pattern(64,red,grey,128),64);

    // Matching core
    auto xbox=Pattern(64,brown,tan,255);
    auto a=packs::Describe(xbox.data(),64,64);
    auto hd=Pattern(256,brown,tan,128);auto b=packs::Describe(hd.data(),256,256);
    check(!a.flat&&!a.ps2_alpha&&b.ps2_alpha,"descriptors must find detail and PS2 alpha");
    check(packs::Judge(packs::Compare(a,b))==packs::Verdict::Accept,"an upscaled copy must be accepted");
    auto other=Pattern(256,brown,tan,255,1);
    check(packs::Judge(packs::Compare(a,packs::Describe(other.data(),256,256)))==packs::Verdict::None,"a different pattern in the same colours must not match");
    auto recolour=Pattern(256,green,tan,255);
    check(packs::Judge(packs::Compare(a,packs::Describe(recolour.data(),256,256)))!=packs::Verdict::Accept,"a recoloured copy must not be accepted");
    // an atlas whose top-left panel this version redrew goes to review, not straight in
    auto atlas=Pattern(256,brown,tan,255);
    for(uint32_t y=0;y<128;++y)for(uint32_t x=0;x<128;++x){uint8_t* p=&atlas[(size_t(y)*256+x)*4];p[0]=uint8_t(255-p[0]);p[2]=uint8_t(255-p[2]);}
    check(packs::Judge(packs::Compare(a,packs::Describe(atlas.data(),256,256)))==packs::Verdict::Review,"an atlas with one redrawn panel must go to review");
    // cut-outs: transparent pixels match whatever colour each version left under them
    auto cut=Pattern(64,brown,tan,255);
    for(uint32_t y=0;y<32;++y)for(uint32_t x=0;x<32;++x)std::fill_n(&cut[(size_t(y)*64+x)*4],4,uint8_t(0));
    auto cut_hd=Pattern(256,brown,tan,128);
    for(uint32_t y=0;y<128;++y)for(uint32_t x=0;x<128;++x){uint8_t* p=&cut_hd[(size_t(y)*256+x)*4];p[0]=30;p[1]=30;p[2]=200;p[3]=0;}
    check(packs::Judge(packs::Compare(packs::Describe(cut.data(),64,64),packs::Describe(cut_hd.data(),256,256)))==packs::Verdict::Accept,"cut-outs must not count as colour differences");
    uint8_t alpha[]={0,0,0,0,0,0,0,64,0,0,0,128,0,0,0,200};packs::ScaleAlpha(alpha,4);
    check(alpha[3]==0&&alpha[7]==128&&alpha[11]==255&&alpha[15]==255,"PS2 alpha must scale 0x80 to opaque");

    // Index and match through the loader interface
    app::Get().custom_textures=true;
    packs::Rescan();
    check(packs::Active(),"an enabled pack must make matching active");
    check(WaitReady(),"pack must finish indexing");
    auto list=packs::Packs();check(list.size()==1&&list[0].images==4&&list[0].errors==0,"pack must index four images");
    check(fs::exists(app::ExeDir()/"textures"/"cache"/"Test_pack.idx"),"index must be cached");
    check(packs::Wants(1)&&!packs::Wants(1),"each key is wanted once");
    packs::Submit(1,64,64,xbox);
    fs::path path;bool ps2=false;
    check(Find(1,path,ps2)==packs::Result::Found&&path.filename()=="hd_brown.png"&&ps2,"the Xbox texture must find its HD copy with PS2 alpha");
    packs::Wants(2);auto green_xbox=Pattern(64,green,tan,255);packs::Submit(2,64,64,green_xbox);
    check(Find(2,path,ps2)==packs::Result::Found&&path.filename()=="hd_green.png"&&!ps2,"the recoloured texture must find its own colours");
    packs::Wants(3);auto unknown=Pattern(64,red,grey,255);packs::Submit(3,64,64,unknown);
    check(Find(3,path,ps2)!=packs::Result::Found,"a texture in other colours must not be replaced");
    packs::Wants(4);Pixels flat(64*64*4,90);packs::Submit(4,64,64,flat);
    check(Find(4,path,ps2)==packs::Result::None,"flat textures are never matched");
    packs::Wants(5);packs::Submit(5,8,8,Pixels(8*8*4,0));
    check(Find(5,path,ps2)==packs::Result::None,"tiny textures are never matched");
    check(packs::Find(99,path,ps2)==packs::Result::None,"unseen keys have no replacement");
    auto stats=packs::Counts();check(stats.seen==5&&stats.matched==2,"counts must report seen and replaced textures");
    // exact keys: a dump's name, a keyed pack image, a textures\replacement image the loader used
    check(packs::KeyInFileName(u8"256x256_0123456789abcdef_DXT5")==0x0123456789abcdefull&&!packs::KeyInFileName(u8"hd_brown"),"dump names carry their key");
    check(packs::Find(0xbeef0,path,ps2)==packs::Result::Found&&path.filename()=="64x64_00000000000beef0_DXT5.png"&&!ps2,"a keyed pack image replaces its texture without matching");
    packs::NoteExact(7,app::ExeDir()/"textures"/"replacement"/"64x64_0000000000000007_DXT1.png");
    stats=packs::Counts();check(stats.seen==6&&stats.matched==3,"replacement folder images count as seen and replaced");
    auto replaced=packs::Replaced(50);
    check(std::any_of(replaced.begin(),replaced.end(),[](const packs::Entry& e){return e.key==7&&e.pack=="replacement"&&e.image.filename()=="64x64_0000000000000007_DXT1.png";}),"replacement folder images are listed with their file");
    packs::ClearExact();check(packs::Counts().matched==2,"listing the folder again forgets old replacements");

    // Choices
    auto before=packs::Revision();packs::Block(1);
    check(packs::Find(1,path,ps2)==packs::Result::None&&packs::Revision()!=before,"a blocked texture keeps the original");
    check(fs::exists(app::ExeDir()/"textures"/"pack_choices.toml"),"choices must be saved");
    packs::ClearChoices();check(Find(1,path,ps2)==packs::Result::Found,"clearing choices restores the match");
    packs::SetEnabled("Test pack",false);
    check(!packs::Active()&&packs::Find(1,path,ps2)==packs::Result::None,"a disabled pack replaces nothing");
    packs::SetEnabled("Test pack",true);check(Find(1,path,ps2)==packs::Result::Found,"enabling the pack again restores matches");

    // The pack's own matches (RFD Switchblade): before automatic matching, after F6 choices
    const fs::path pack_dir=app::ExeDir()/"textures"/"packs"/"Test pack";
    packs::PackMatches m;m.use[3]="hd_green.png";m.skip.insert(2);
    check(packs::WriteMatches(pack_dir,m),"matches must be written");
    packs::PackMatches back;check(packs::ReadMatches(pack_dir,back)&&back.use.size()==1&&back.use[3]=="hd_green.png"&&back.skip.count(2),"matches must read back");
    packs::Rescan();
    check(Find(3,path,ps2)==packs::Result::Found&&path.filename()=="hd_green.png","a pack's own match replaces a texture it would not match");
    check(packs::Find(2,path,ps2)==packs::Result::None,"a texture the pack skips keeps the original");
    check(Find(1,path,ps2)==packs::Result::Found&&path.filename()=="hd_brown.png","textures without an entry still match automatically");
    packs::Block(3);check(packs::Find(3,path,ps2)==packs::Result::None,"F6 choices come before the pack's matches");
    packs::ClearChoices();
    fs::remove(pack_dir/packs::kMatchesFile);packs::Rescan();
    check(Find(2,path,ps2)==packs::Result::Found,"removing the matches file restores automatic matching");
    app::Get().custom_textures=false;check(!packs::Active()&&!packs::Wants(6),"replacement off stops matching");
    puts("PASS: texture pack descriptors, PS2 alpha, index cache, matching, recolours, choices, enable switches and pack matches files");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
