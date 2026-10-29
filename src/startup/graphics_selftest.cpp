#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "graphics/frame_readback.h"
#include "graphics/widescreen.h"
#include "graphics/cutscene_motion_blur.h"
#include "core/diag.h"
#include "port/port_settings.h"
#include <set>
#include <fstream>

namespace {
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
uint32_t Allocate(uint32_t bytes) { return mem::AllocContiguous(bytes,0,0xFFFFFFFF,0x1000); }
}

void TenchuMaybeRunGraphicsTest() {
    if (!tenchu::GraphicsTestEnabled()) return;
    // Baseline presentation assertions use a 4:3 frame, independently of the
    // user's saved widescreen preference. Wide HUD is tested separately below.
    // The FPS overlay would cover the top-right sample in a small window.
    tenchu::wide::enabled=false;tenchu::post::Reset();g_tenchu_performance_overlay=false;
    bool passed=false;
    try {
        auto backend=gpu::CreateBackend();
        Require(backend->Init(64,64),"D3D12 test backend init failed");
        uint32_t data=Allocate(64*64*4), header=mem::AllocSystem(24);
        std::memset(kernel::HostPtr(data),0,64*64*4);
        kernel::W32(header,0x000D0001); kernel::W32(header+4,mem::PhysicalFromVirtual(data));
        kernel::W32(header+12,0x00010021|(0x12u<<8)); // linear A8R8G8B8
        kernel::W32(header+16,63u|(63u<<12)|(3u<<24)); // 64 pixels, 256-byte pitch
        gpu::State s{};
        s.rt={}; s.rt.header=header; s.rt.data=kernel::R32(header+4);
        s.rt.format=kernel::R32(header+12); s.rt.size=kernel::R32(header+16);
        s.viewport={0,0,64,64,0,1};
        backend->Clear(s,0,nullptr,gpu::kClearTarget,0xFFFF00FF,1,0);
        const float test_scale=GetEnvironmentVariableW(L"TENCHU_GPU_THREE_QUARTER",nullptr,0)!=0?.75f:.5f;
        s.rt.tenchu_scale_x=s.rt.tenchu_scale_y=test_scale;
        const uint32_t colors[]={0xFFFF0000,0xFF00FF00,0xFF0000FF,0xFFFFFF00};
        for(int q=0;q<4;++q) {
            int32_t rect[4]={(q&1)*32,(q>>1)*32,(q&1)*32+32,(q>>1)*32+32};
            backend->Clear(s,1,reinterpret_cast<uint8_t*>(rect),gpu::kClearTarget,colors[q],1,0);
        }
        s.simple[gpu::kMColorWriteEnable/4]=0x01010101;
        s.tss[0][gpu::kTssColorOp]=2; s.tss[0][gpu::kTssColorArg1]=0;
        s.tss[0][gpu::kTssAlphaOp]=2; s.tss[0][gpu::kTssAlphaArg1]=0;
        for(int i=1;i<4;++i) s.tss[i][gpu::kTssColorOp]=1;
        struct ScreenVertex {float x,y,z,w; uint32_t color;};
        ScreenVertex vertices[]={{8,24,.5f,1,~0u},{16,24,.5f,1,~0u},{8,40,.5f,1,~0u},
                                 {8,40,.5f,1,~0u},{16,24,.5f,1,~0u},{16,40,.5f,1,~0u}};
        uint32_t vb_data=Allocate(sizeof(vertices)),vb=mem::AllocSystem(24);
        std::memcpy(kernel::HostPtr(vb_data),vertices,sizeof(vertices));
        kernel::W32(vb,1); kernel::W32(vb+4,mem::PhysicalFromVirtual(vb_data));
        s.streams[0]={vb,sizeof(ScreenVertex)}; s.vs_handle=0x44;
        backend->Draw(s,5,0,6,nullptr);
        struct Vertex {float x,y,z; uint32_t color;};
        Vertex fixed[]={{.25f,.25f,.5f,0xFF00FFFF},{.75f,.25f,.5f,0xFF00FFFF},{.25f,-.25f,.5f,0xFF00FFFF},
                        {.25f,-.25f,.5f,0xFF00FFFF},{.75f,.25f,.5f,0xFF00FFFF},{.75f,-.25f,.5f,0xFF00FFFF}};
        uint32_t fixed_data=Allocate(sizeof(fixed)),fixed_vb=mem::AllocSystem(24);
        std::memcpy(kernel::HostPtr(fixed_data),fixed,sizeof(fixed));
        kernel::W32(fixed_vb,1); kernel::W32(fixed_vb+4,mem::PhysicalFromVirtual(fixed_data));
        for(int index:{2,3,256}) for(int i=0;i<4;++i) s.transforms[index][i*4+i]=1;
        s.streams[0]={fixed_vb,sizeof(Vertex)}; s.vs_handle=0x42;
        backend->Draw(s,5,0,6,nullptr);
        const auto capture_root=app::ExeDir()/diag::CaptureDir();
        std::set<std::filesystem::path> old_captures;
        if(std::filesystem::exists(capture_root))for(auto& file:std::filesystem::directory_iterator(capture_root))old_captures.insert(file.path());
        gpu::g_capture=true;
        backend->Draw(s,5,0,6,nullptr);backend->Present(s.rt);
        backend->Sync(data,64*64*4);
        // Scaling must not clear the unused part of the physical allocation.
        Require(kernel::R32(data+(63*64+63)*4)==0xFFFF00FF,"clear escaped the half-sized active region");
        Require(tenchu::graphics_test_capture_passed,"presentation pixel check failed");
        tenchu::post::enabled=true;backend->Draw(s,5,0,6,nullptr);backend->Present(s.rt);
        Require(tenchu::graphics_test_capture_passed,"neutral shader must preserve presentation pixels");
        tenchu::post::values[0]=.25f;tenchu::post::values[1]=.05f;tenchu::post::values[2]=1.1f;
        tenchu::post::values[3]=.2f;tenchu::post::values[4]=1.2f;tenchu::post::values[5]=.2f;tenchu::post::values[7]=.25f;
        backend->Draw(s,5,0,6,nullptr);backend->Present(s.rt);
        Require(tenchu::graphics_test_capture_passed,"adjustable shader GPU colors differ from reference");
        tenchu::post::values[13]=.6f;tenchu::post::values[14]=.5f;tenchu::post::values[15]=.1f;
        backend->Draw(s,5,0,6,nullptr);backend->Present(s.rt);
        Require(tenchu::graphics_test_capture_passed,"highlight roll-off/vibrance/shadow lift GPU pixels differ from reference");
        tenchu::post::compare=true;tenchu::post::divider=.4f;backend->Draw(s,5,0,6,nullptr);backend->Present(s.rt);
        Require(tenchu::graphics_test_capture_passed,"split comparison must preserve left image and grade right image");
        unsigned new_captures=0;
        for(auto& file:std::filesystem::directory_iterator(capture_root))if(!old_captures.count(file.path())&&std::filesystem::exists(file.path()/"final.bmp")){
            std::ifstream input(file.path()/"frame.txt");std::string text((std::istreambuf_iterator<char>(input)),{});
            Require(!text.empty(),"burst capture draw report is empty");++new_captures;
        }
        Require(new_captures==3,"one F3 request must produce three unique completed frame captures");
        LOGR("PASS: default F3 directory and three consecutive unique frame captures");
        tenchu::post::Reset();
        LOGR("PASS: disabled, neutral, adjusted color, highlight/shadow/vibrance and split-compare GPU pixels");
        // Match the title's cutscene chain: a scaled 64-wide backbuffer is
        // copied into the left half of a 128-wide offscreen texture.
        auto target=[&](uint32_t width){
            gpu::Surface surface{};
            uint32_t pixels=Allocate(width*64*4),h=mem::AllocSystem(24);
            std::memset(kernel::HostPtr(pixels),0,width*64*4);
            kernel::W32(h,0x000D0001);kernel::W32(h+4,mem::PhysicalFromVirtual(pixels));
            kernel::W32(h+12,0x00010021|(0x12u<<8));
            kernel::W32(h+16,(width-1)|(63u<<12)|((width/16-1)<<24));
            surface.header=h;surface.data=kernel::R32(h+4);surface.format=kernel::R32(h+12);surface.size=kernel::R32(h+16);
            return std::pair{surface,pixels};
        };
        auto [wide,wide_data]=target(128);
        gpu::State copy_state=s;copy_state.rt=wide;copy_state.viewport={0,0,128,64,0,1};
        backend->Clear(copy_state,0,nullptr,gpu::kClearTarget,0xFFFF00FF,1,0);
        backend->Copy(s.rt,wide,0,0,0);backend->Sync(wide_data,128*64*4);
        for(auto corner: {std::array<uint32_t,3>{3,3,colors[0]},{60,3,colors[1]},{3,60,colors[2]},{60,60,colors[3]}}) {
            const auto actual=kernel::R32(wide_data+(corner[1]*128+corner[0])*4);
            LOGR("Copy GPU pixel %u,%u: %08X expected %08X",corner[0],corner[1],actual,corner[2]);
            Require(actual==corner[2],"scaled source copy sampled unused backbuffer edge");
        }
        Require(kernel::R32(wide_data+(32*128+96)*4)==0xFFFF00FF,"copy stretched across unused wide texture half");
        const uint32_t rect=mem::AllocSystem(16),point=mem::AllocSystem(8);
        kernel::W32(rect,32);kernel::W32(rect+4,0);kernel::W32(rect+8,64);kernel::W32(rect+12,32);
        kernel::W32(point,64);kernel::W32(point+4,32);
        backend->Copy(s.rt,wide,1,rect,point);backend->Sync(wide_data,128*64*4);
        Require(kernel::R32(wide_data+(48*128+80)*4)==colors[1],"scaled source rectangle/destination offset mismatch");
        Require(kernel::R32(wide_data+(63*128+127)*4)==0xFFFF00FF,"explicit copy escaped destination rectangle");
        auto [back,back_data]=target(64);
        copy_state.rt=back;copy_state.viewport={0,0,64,64,0,1};
        backend->Clear(copy_state,0,nullptr,gpu::kClearTarget,0xFFFF00FF,1,0);
        back.tenchu_scale_x=back.tenchu_scale_y=test_scale;
        backend->Copy(wide,back,0,0,0);backend->Sync(back_data,64*64*4);
        const uint32_t edge=uint32_t(64*test_scale)-3;
        Require(kernel::R32(back_data+(3*64+edge)*4)==colors[1],"scaled destination copy has wrong active width");
        Require(kernel::R32(back_data+(edge*64+3)*4)==colors[2],"scaled destination copy has wrong active height");
        Require(kernel::R32(back_data+(63*64+63)*4)==0xFFFF00FF,"copy overwrote inactive destination area");
        // Real capture: mode 00041021 is a 2x1 sample grid. The title
        // copies to a double-width texture, then samples 75% of that grid.
        auto [sampled,sampled_data]=target(128);
        copy_state.rt=sampled;copy_state.viewport={0,0,128,64,0,1};
        backend->Clear(copy_state,0,nullptr,gpu::kClearTarget,0xFFFF00FF,1,0);
        gpu::Surface multisampled=s.rt;multisampled.tenchu_samples_x=2;
        backend->Copy(multisampled,sampled,0,0,0);backend->Sync(sampled_data,128*64*4);
        const uint32_t sample_right=uint32_t(128*test_scale)-3,sample_bottom=uint32_t(64*test_scale)-3;
        Require(kernel::R32(sampled_data+(3*128+sample_right)*4)==colors[1],"raw multisample copy has wrong horizontal extent");
        Require(kernel::R32(sampled_data+(sample_bottom*128+3)*4)==colors[2],"raw multisample copy has wrong vertical extent");
        auto [resolved,resolved_data]=target(64);
        copy_state.rt=resolved;copy_state.viewport={0,0,64,64,0,1};
        backend->Clear(copy_state,0,nullptr,gpu::kClearTarget,0xFFFF00FF,1,0);
        struct TexturedVertex{float x,y,z,w;uint32_t color;float u,v;};
        const float umax=128*test_scale,vmax=64*test_scale;
        TexturedVertex quad[]={{0,0,.5f,1,~0u,0,0},{64,0,.5f,1,~0u,umax,0},{0,64,.5f,1,~0u,0,vmax},{64,64,.5f,1,~0u,umax,vmax}};
        uint32_t quad_data=Allocate(sizeof(quad)),quad_header=mem::AllocSystem(20);
        std::memcpy(kernel::HostPtr(quad_data),quad,sizeof(quad));
        kernel::W32(quad_header,1);kernel::W32(quad_header+4,mem::PhysicalFromVirtual(quad_data));
        copy_state.streams[0]={quad_header,sizeof(TexturedVertex)};copy_state.vs_handle=0x144;copy_state.textures[0]=sampled.header;
        copy_state.tss[0][gpu::kTssColorArg1]=2;copy_state.tss[0][gpu::kTssAlphaArg1]=2;
        backend->Draw(copy_state,6,0,4,nullptr);backend->Sync(resolved_data,64*64*4);
        for(auto corner: {std::array<uint32_t,3>{3,3,colors[0]},{60,3,colors[1]},{3,60,colors[2]},{60,60,colors[3]}})
            Require(kernel::R32(resolved_data+(corner[1]*64+corner[0])*4)==corner[2],"captured cutscene resolve coordinates do not fill the output");
        // Captured cutscene draw141: 0x6C alpha blends a new frame into
        // persistent history. New blue over old red must become pure blue
        // only for the identified history pass with the compatibility fix on.
        gpu::State history_source=s;history_source.rt=sampled;history_source.viewport={0,0,128,64,0,1};
        backend->Clear(history_source,0,nullptr,gpu::kClearTarget,0xFF0000FF,1,0);
        for(auto& vertex:quad){vertex.z=0;vertex.color=0x6CFFFFFF;}
        std::memcpy(kernel::HostPtr(quad_data),quad,sizeof(quad));
        copy_state.tss[0][gpu::kTssAlphaArg1]=0;
        copy_state.simple[gpu::kMBlendEnable/4]=1;copy_state.simple[gpu::kMSrcBlend/4]=0x302;
        copy_state.simple[gpu::kMDestBlend/4]=0x303;copy_state.simple[gpu::kMBlendOp/4]=0x8006;
        for(bool original:{true,false})for(bool backbuffer:{false,true}){
            tenchu::motion::original_blur=original;copy_state.rt.tenchu_backbuffer=backbuffer;
            backend->Clear(copy_state,0,nullptr,gpu::kClearTarget,0xFFFF0000,1,0);
            backend->Draw(copy_state,6,0,4,nullptr);backend->Sync(resolved_data,64*64*4);
            const auto pixel=kernel::R32(resolved_data+(32*64+32)*4);
            const unsigned red=(pixel>>16)&255,blue=pixel&255;
            if(!original&&!backbuffer)Require(red<3&&blue>252,"history blend bypass must eliminate previous-frame red trail");
            else Require(red>140&&red<155&&blue>100&&blue<116,"original history/ordinary backbuffer transparency must retain alpha blend");
        }
        tenchu::motion::original_blur=false;
        LOGR("PASS: cutscene history ghosting bypass, original-blur option and ordinary transparency preservation");
        LOGR("PASS: 2x1 multisample layout copy and captured cutscene quad resolve");
        LOGR("PASS: scaled backbuffer to wide texture and back GPU copy checks");
        // Real GPU check of centered HUD geometry versus offscreen geometry.
        auto [hud,hud_data]=target(64);
        gpu::State hud_state=s;hud_state.rt=hud;hud_state.viewport={0,0,64,64,0,1};
        hud_state.vs_handle=0x44;hud_state.streams[0]={vb,sizeof(ScreenVertex)};
        ScreenVertex movie[]={{0,0,.5f,1,~0u},{64,0,.5f,1,~0u},{0,64,.5f,1,~0u},
                              {0,64,.5f,1,~0u},{64,0,.5f,1,~0u},{64,64,.5f,1,~0u}};
        uint32_t movie_data=Allocate(sizeof(movie)),movie_vb=mem::AllocSystem(24);
        std::memcpy(kernel::HostPtr(movie_data),movie,sizeof(movie));
        kernel::W32(movie_vb,1);kernel::W32(movie_vb+4,mem::PhysicalFromVirtual(movie_data));
        const bool previous_wide=tenchu::wide::enabled.load();tenchu::wide::enabled=true;
        for(float raster:{.5f,.75f,1.f})for(bool presentable:{false,true}){
            hud_state.rt.tenchu_backbuffer=presentable;
            hud_state.rt.tenchu_scale_x=hud_state.rt.tenchu_scale_y=raster;
            hud_state.streams[0]={vb,sizeof(ScreenVertex)};
            backend->Clear(hud_state,0,nullptr,gpu::kClearTarget,0xFF000000,1,0);
            backend->Draw(hud_state,5,0,6,nullptr);backend->Sync(hud_data,64*64*4);
            const uint32_t y=uint32_t(32*raster),old_x=uint32_t(10*raster),new_x=uint32_t(18*raster);
            Require(kernel::R32(hud_data+(y*64+old_x)*4)==(presentable?0xFF000000u:0xFFFFFFFFu),"scaled movie/HUD original-edge classification failed");
            Require(kernel::R32(hud_data+(y*64+new_x)*4)==(presentable?0xFFFFFFFFu:0xFF000000u),"scaled movie/HUD active-area centering failed");
            if(presentable){
                hud_state.streams[0]={movie_vb,sizeof(ScreenVertex)};
                backend->Clear(hud_state,0,nullptr,gpu::kClearTarget,0xFF000000,1,0);
                backend->Draw(hud_state,5,0,6,nullptr);backend->Sync(hud_data,64*64*4);
                const uint32_t left=uint32_t(4*raster),right=uint32_t(60*raster),center=uint32_t(32*raster);
                Require(kernel::R32(hud_data+(y*64+left)*4)==0xFF000000,"movie left pillarbox missing");
                Require(kernel::R32(hud_data+(y*64+right)*4)==0xFF000000,"scaled movie right pillarbox missing/off-center");
                Require(kernel::R32(hud_data+(y*64+center)*4)==0xFFFFFFFF,"scaled movie center is not filled");
                // Reproduce F3 captures 143518/143543: solid black XYRHW
                // cutscene masks, with a 75%-sized active backbuffer.
                ScreenVertex bars[]={{0,0,.5f,1,0xFF000000},{64,0,.5f,1,0xFF000000},{0,8,.5f,1,0xFF000000},
                                     {0,8,.5f,1,0xFF000000},{64,0,.5f,1,0xFF000000},{64,8,.5f,1,0xFF000000}};
                std::memcpy(kernel::HostPtr(movie_data),bars,sizeof(bars));
                backend->Clear(hud_state,0,nullptr,gpu::kClearTarget,0xFFFFFFFF,1,0);
                backend->Draw(hud_state,5,0,6,nullptr);backend->Sync(hud_data,64*64*4);
                const uint32_t bar_y=uint32_t(4*raster);
                for(uint32_t x=1;x<uint32_t(64*raster)-1;++x)
                    Require(kernel::R32(hud_data+(bar_y*64+x)*4)==0xFF000000,"cutscene black bar fails to cover full active width");
                Require(kernel::R32(hud_data+(y*64+center)*4)==0xFFFFFFFF,"cutscene bar leaked into scene");
                ScreenVertex sky[6];std::memcpy(sky,movie,sizeof(sky));
                for(auto& v:sky){v.x-=4;v.z=0;}
                std::memcpy(kernel::HostPtr(movie_data),sky,sizeof(sky));
                backend->Clear(hud_state,0,nullptr,gpu::kClearTarget,0xFF000000,1,0);
                backend->Draw(hud_state,5,0,6,nullptr);backend->Sync(hud_data,64*64*4);
                Require(kernel::R32(hud_data+(y*64+uint32_t(1*raster))*4)==0xFFFFFFFF,"scrolling sky has inset HUD border");
                std::memcpy(kernel::HostPtr(movie_data),movie,sizeof(movie));
            }
        }
        tenchu::wide::enabled=previous_wide;
        LOGR("GPU widescreen: centered HUD/offscreen bypass and symmetric movie pillarboxes at 50/75/100 percent verified");
        // A bright patch must cast a soft halo onto a neighboring darker area.
        tenchu::wide::enabled=false;tenchu::post::Reset();
        backend->Clear(s,0,nullptr,gpu::kClearTarget,0xFF404040,1,0);
        int32_t bright[4]={32,16,48,48};backend->Clear(s,1,reinterpret_cast<uint8_t*>(bright),gpu::kClearTarget,0xFFFFFFFF,1,0);
        tenchu::post_probe_enabled=true;backend->Present(s.rt);
        const float original_probe=tenchu::post_probe_value;
        tenchu::post::enabled=true;tenchu::post::values[9]=1;tenchu::post::values[10]=.5f;tenchu::post::values[11]=24;tenchu::post::values[12]=1;
        backend->Present(s.rt);
        Require(tenchu::post_probe_value>original_probe+2,"bloom does not spread bright pixels into surrounding dark region");
        tenchu::post::compare=true;tenchu::post::divider=.5f;backend->Present(s.rt);
        Require(std::abs(tenchu::post_probe_value-original_probe)<2,"split original side must bypass spatial bloom");
        tenchu::post_probe_enabled=false;tenchu::post::Reset();
        LOGR("PASS: spatial bloom halo and split original-side bypass verified on GPU");
        passed=true;
        LOGR("PASS: backbuffer scale %.2f clear, XYRHW, fixed-function draw and presentation GPU checks",double(test_scale));
        backend->Flush();
    } catch(const std::exception& e) { LOGE("graphics selftest: %s",e.what()); }
    logx::Flush();
    ExitProcess(passed?0:1);
}
