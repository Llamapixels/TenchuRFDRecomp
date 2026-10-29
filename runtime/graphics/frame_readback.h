#pragma once
#include "graphics/post_processing.h"
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include "graphics/screenshot.h"

namespace tenchu {
inline std::atomic<bool> graphics_test_capture_passed{false};
inline bool post_probe_enabled=false;
inline float post_probe_value=0;
inline std::atomic<bool> capture_next_presentation{false};
inline std::atomic<int> camera_capture_phase{-1};
inline bool GraphicsTestEnabled() { return GetEnvironmentVariableW(L"TENCHU_GPU_SELFTEST", nullptr, 0) != 0; }
inline bool CaptureTitleFrame(uint64_t frame) {
    if(shots::NextFrame())return true;
    if(capture_next_presentation.exchange(false))return true;
    if (GetEnvironmentVariableW(L"TENCHU_AUTOPLAY", nullptr, 0) != 0)
        return frame && frame % 300 == 0 && frame <= 7200;
    return GetEnvironmentVariableW(L"TENCHU_CAPTURE_TITLE", nullptr, 0) != 0 &&
           (frame == 120 || frame == 360 || frame == 900 || frame == 1500);
}

// An optional readback of the actual presentation image, before DXGI owns it.
// Normal game execution allocates nothing here.
struct FrameReadback {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint32_t width=0, height=0;
    void Record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* image) {
        auto desc=image->GetDesc();
        width=uint32_t(desc.Width); height=desc.Height;
        UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=bytes;
        bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1;
        bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))
            throw std::runtime_error("presentation readback allocation failed");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=image;
        src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=buffer.Get();
        dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=footprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        list->ResourceBarrier(1,&barrier);
    }
    bool SaveAndCheck(const std::filesystem::path& directory, bool validate = true, uint64_t frame = 0) {
        const bool screenshot=shots::saving.exchange(false);
        const auto output_directory=screenshot?directory.parent_path()/"screenshots":directory;
        std::error_code directory_error;
        std::filesystem::create_directories(output_directory,directory_error);
        if(directory_error){if(screenshot)shots::Result("Cannot create screenshot folder: "+directory_error.message());return false;}
        uint8_t* mapped=nullptr;
        if (FAILED(buffer->Map(0,nullptr,reinterpret_cast<void**>(&mapped)))) {if(screenshot)shots::Result("Unable to read the rendered frame.");return false;}
        std::vector<uint8_t> pixels(size_t(width)*height*4);
        for (uint32_t y=0;y<height;++y)
            std::memcpy(pixels.data()+size_t(y)*width*4,mapped+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,size_t(width)*4);
        D3D12_RANGE no_write{0,0}; buffer->Unmap(0,&no_write);
        std::array<uint8_t,54> header{};
        header[0]='B'; header[1]='M';
        auto put=[&](size_t at,uint32_t value){std::memcpy(header.data()+at,&value,4);};
        put(2,54+uint32_t(pixels.size())); put(10,54); put(14,40); put(18,width); put(22,uint32_t(-int32_t(height)));
        header[26]=1; header[28]=32; put(34,uint32_t(pixels.size()));
        const bool three_quarter=GetEnvironmentVariableW(L"TENCHU_GPU_THREE_QUARTER",nullptr,0)!=0;
        const int camera_phase=camera_capture_phase.exchange(-1);
        SYSTEMTIME time{};GetLocalTime(&time);char shot_name[100];
        std::snprintf(shot_name,sizeof(shot_name),"Tenchu_%04u-%02u-%02u_%02u-%02u-%02u_%03u_%llu.bmp",unsigned(time.wYear),unsigned(time.wMonth),unsigned(time.wDay),unsigned(time.wHour),unsigned(time.wMinute),unsigned(time.wSecond),unsigned(time.wMilliseconds),static_cast<unsigned long long>(frame));
        const auto name=screenshot?std::string(shot_name):camera_phase>=0?std::string("camera_probe_")+std::to_string(camera_phase)+".bmp":validate ? std::string(three_quarter?"backbuffer_three_quarter.bmp":"backbuffer_half.bmp") : std::string("title_")+std::to_string(frame)+".bmp";
        std::ofstream bitmap(output_directory/name,std::ios::binary);
        bitmap.write(reinterpret_cast<const char*>(header.data()),header.size());
        bitmap.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
        bitmap.close();
        if(screenshot){shots::Result(bitmap?"Saved: "+(output_directory/name).string():"Screenshot could not be written.");return bool(bitmap);}
        if (!validate) return true;
        std::ofstream report(directory/(three_quarter?"backbuffer_three_quarter_gpu_test.txt":"backbuffer_gpu_test.txt"));
        bool ok=true;
        if(post_probe_enabled){
            const float vw=std::min(float(width),float(height)*4/3),vh=vw*3/4;
            uint32_t x=uint32_t((width-vw)/2+.47f*vw),y=uint32_t((height-vh)/2+.5f*vh);
            auto* sample=pixels.data()+(size_t(y)*width+x)*4;
            post_probe_value=(sample[0]+sample[1]+sample[2])/3.f;
            graphics_test_capture_passed=true;return true;
        }
        auto pixel=[&](float x,float y,uint32_t expected,const char* label){
            // The presentation keeps a 4:3 frame centered in the output window.
            const float vw=std::min(float(width),float(height)*4/3), vh=vw*3/4;
            uint32_t px=uint32_t((width-vw)/2+x*vw), py=uint32_t((height-vh)/2+y*vh);
            const auto* p=pixels.data()+(size_t(py)*width+px)*4;
            if(post::enabled.load()&&(!post::compare.load()||x>=post::divider.load())){
                float rgb[3]={float((expected>>16)&255)/255,float((expected>>8)&255)/255,float(expected&255)/255};
                float shadow=1-std::clamp(rgb[0]*.2126f+rgb[1]*.7152f+rgb[2]*.0722f,0.f,1.f);
                for(auto& v:rgb)v+=post::values[15].load()*shadow*shadow;
                for(auto& v:rgb)v=(v*std::exp2(post::values[0].load())+post::values[1].load()-.5f)*post::values[2].load()+.5f;
                float luma=rgb[0]*.2126f+rgb[1]*.7152f+rgb[2]*.0722f;
                for(auto& v:rgb)v=luma+(v-luma)*post::values[3].load();
                float chroma=std::clamp(*std::max_element(rgb,rgb+3)-*std::min_element(rgb,rgb+3),0.f,1.f);
                for(unsigned channel=0;channel<3;++channel){
                    float v=luma+(rgb[channel]-luma)*(1+post::values[14].load()*(1-chroma));
                    v*=channel==0?1+post::values[5].load()*.15f:channel==2?1-post::values[5].load()*.15f:1;
                    v/=1+post::values[13].load()*std::max(v-.5f,0.f);
                    v=std::pow(std::clamp(v,0.f,1.f),1/post::values[4].load());
                    v*=1-post::values[7].load()*std::clamp(((x*2-1)*(x*2-1)+(y*2-1)*(y*2-1))*.5f,0.f,1.f);
                    rgb[channel]=v;
                }
                expected=0xFF000000|(uint32_t(std::lround(rgb[0]*255))<<16)|(uint32_t(std::lround(rgb[1]*255))<<8)|uint32_t(std::lround(rgb[2]*255));
            }
            bool match=true;
            for (unsigned c=0;c<3;++c) match &= std::abs(int(p[c])-int((expected>>(8*c))&255))<=8;
            report<<(match?"PASS ":"FAIL ")<<label<<" BGRA "<<unsigned(p[0])<<','<<unsigned(p[1])<<','<<unsigned(p[2])<<'\n';
            ok &= match;
        };
        pixel(.05f,.05f,0xFFFF0000,"top-left red");
        pixel(.95f,.05f,0xFF00FF00,"top-right green");
        pixel(.05f,.95f,0xFF0000FF,"bottom-left blue");
        pixel(.95f,.95f,0xFFFFFF00,"bottom-right yellow");
        pixel(.999f,.999f,0xFFFFFF00,"cropped edge excludes magenta sentinel");
        pixel(.1875f,.5f,0xFFFFFFFF,"scaled XYRHW marker");
        pixel(.75f,.5f,0xFF00FFFF,"scaled fixed-function marker");
        report<<(ok?"PASS":"FAIL")<<" actual D3D12 presentation pixels\n";
        graphics_test_capture_passed=ok;
        return ok;
    }
};
}
