// Offline check of the texture pack matcher on real data: indexes the packs in
// <root>\textures\packs and matches every distinct texture saved by F3 render
// captures. Usage: texture_pack_probe <root> <capture folder>...
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "app/app.h"
#include "core/log.h"
#include "graphics/texture_packs.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace packs = tenchu::packs;
static fs::path g_root;
void app::Save(){}
app::Settings& app::Get(){static app::Settings s;return s;}
fs::path app::ExeDir(){return g_root;}
namespace logx {void Write(Level,const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);} }

static bool Read(const fs::path& path,std::vector<uint8_t>& px,UINT& w,UINT& h){
    ComPtr<IWICImagingFactory> wic;ComPtr<IWICBitmapDecoder> dec;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICBitmapSource> bgra;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    if(SUCCEEDED(hr))hr=wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec);
    if(SUCCEEDED(hr))hr=dec->GetFrame(0,&frame);
    if(SUCCEEDED(hr))hr=WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA,frame.Get(),&bgra);
    if(SUCCEEDED(hr))hr=bgra->GetSize(&w,&h);
    if(FAILED(hr))return false;
    px.resize(size_t(w)*h*4);
    return SUCCEEDED(bgra->CopyPixels(nullptr,w*4,UINT(px.size()),px.data()));
}

int main(int argc,char** argv){
    if(argc<3){std::puts("usage: texture_pack_probe <root> <capture folder>...");return 2;}
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    g_root=argv[1];app::Get().custom_textures=true;
    auto start=std::chrono::steady_clock::now();
    packs::Rescan();
    for(;;){bool busy=false;for(auto& p:packs::Packs())busy|=p.indexing;if(!busy)break;std::this_thread::sleep_for(std::chrono::milliseconds(100));}
    std::printf("indexed in %.1f s\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    std::set<uint64_t> keys;std::map<uint64_t,std::string> source;
    for(int i=2;i<argc;++i)for(auto& e:fs::recursive_directory_iterator(argv[i])){
        auto name=e.path().filename().string();
        if(name.rfind("tex_",0)||e.path().extension()!=".bmp")continue;
        std::vector<uint8_t> px;UINT w=0,h=0;
        if(!Read(e.path(),px,w,h))continue;
        for(size_t p=3;p<px.size();p+=4)px[p]=255;  // BMP captures carry no alpha
        uint64_t key=1469598103934665603ull;for(uint8_t b:px)key=(key^b)*1099511628211ull;
        if(!keys.insert(key).second)continue;
        source[key]=e.path().string();
        packs::Wants(key);packs::Submit(key,w,h,std::move(px));
    }
    auto s=packs::Counts();
    std::printf("distinct textures %zu, replaced %zu, review %zu, no match %zu\n",s.seen,s.matched,s.review,s.unmatched);
    for(auto& e:packs::ReviewList(50))std::printf("review\t%.3f\t%.3f\t%s\t%s\n",e.ncc,e.color,source[e.key].c_str(),e.file.c_str());
    for(auto& e:packs::Replaced(1000))std::printf("accept\t%.3f\t%.3f\t%s\t%s\n",e.ncc,e.color,source[e.key].c_str(),e.file.c_str());
    return 0;
}
