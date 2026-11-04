#define NOMINMAX
#include <windows.h>
#include "kernel/memory.h"
#include <memory>
#include "cpu/x86_context.h"
#include "game_camera.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <limits>
void TenchuWorldDrawLimit(X86Context&,uint8_t*);
void TenchuObjectDrawLimit(X86Context&,uint8_t*);
extern "C" void sub_002387B0(X86Context&,uint8_t*);
extern "C" void sub_002396E0(X86Context&,uint8_t*);
static float observed;
static unsigned calls;
extern "C" void __imp__sub_002387B0(X86Context& ctx,uint8_t* base){
    ctx.xmm[0].f[0]=5000;TenchuObjectDrawLimit(ctx,base);observed=ctx.xmm[0].f[0];++calls;ctx.esp+=4;
}
extern "C" void __imp__sub_002396E0(X86Context& ctx,uint8_t* base){
    ctx.xmm[0].f[0]=5000;TenchuObjectDrawLimit(ctx,base);observed=ctx.xmm[0].f[0];++calls;ctx.esp+=4;
}
int main(){try{
    using namespace tenchu::camera;
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    // Match the real port: actor data is low, but guest stacks are at D0000000.
    auto release=[](uint8_t* p){if(p)VirtualFree(p,0,MEM_RELEASE);};
    std::unique_ptr<uint8_t,decltype(release)> memory(static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS)),release);
    auto* base=memory.get();check(base!=nullptr,"reserve sparse guest address space");
    check(VirtualAlloc(base,0x04000000,MEM_COMMIT,PAGE_READWRITE)==base,"commit low actor memory");
    check(VirtualAlloc(base+mem::kStackBase+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"commit real high guest stack");
    X86Context ctx{};ctx.esp=mem::kStackBase+0x1F000;
    auto write=[&](uint32_t at,auto value){std::memcpy(base+at,&value,sizeof(value));};
    write(0x008E38BC,uint16_t(3));write(0x01BAB110,uint32_t(0x20000));write(0x01BAB114,uint32_t(0x30000));
    ctx.eax=123;ctx.cf=1;ctx.xmm[0].f[1]=42;ctx.xmm[1].f[0]=77;
    for(float scale:{.25f,1.f,4.f}){
        world_draw_distance=scale;
        for(float original:{3500.f,4000.f,5000.f}){ctx.xmm[0].f[0]=original;TenchuWorldDrawLimit(ctx,base);check(ctx.xmm[0].f[0]==original*scale,"all mission limits must scale linearly");}
        check(ctx.eax==123&&ctx.cf==1&&ctx.xmm[0].f[1]==42&&ctx.xmm[1].f[0]==77,"hook must preserve other registers/lanes/flags");
        sub_002387B0(ctx,base);check(observed==5000*scale,"world painter must scale shared object culling");
        enemy_draw_distance=scale;write(ctx.esp+4,uint32_t(0x400A0));sub_002396E0(ctx,base);check(observed==5000*scale,"NPC painter must use separate distance");
        ctx.xmm[0].f[0]=5000;TenchuObjectDrawLimit(ctx,base);check(ctx.xmm[0].f[0]==5000,"scope must restore gameplay queries");
    }
    enemy_draw_distance=.25f;
    for(uint32_t object:{0x200A0u,0x300A0u}){write(ctx.esp+4,object);sub_002396E0(ctx,base);check(observed==5000,"both actual players stay visible regardless of substituted model");}
    follow_control=false;world_draw_distance=2;ctx.xmm[0].f[0]=3500;TenchuWorldDrawLimit(ctx,base);check(ctx.xmm[0].f[0]==7000,"works with modern camera disabled");
    world_draw_distance=std::numeric_limits<float>::quiet_NaN();ctx.xmm[0].f[0]=3500;TenchuWorldDrawLimit(ctx,base);check(ctx.xmm[0].f[0]==3500,"nonfinite settings must be neutral");
    write(0x008E38BC,uint16_t(7));world_draw_distance=4;ctx.xmm[0].f[0]=3500;TenchuWorldDrawLimit(ctx,base);check(ctx.xmm[0].f[0]==3500,"menus/cutscenes retain original limits");
    write(ctx.esp+4,uint32_t(0x400A0));sub_002396E0(ctx,base);check(observed==5000,"character scope is neutral outside gameplay");
    Reset();check(world_draw_distance==1&&enemy_draw_distance==1,"reset restores original distances");
    write(0x008E38BC,uint16_t(3));enemy_draw_distance=4;
    for(uint32_t bad_stack:{0x03F00000u,mem::kStackBase-4,mem::kStackBase+mem::kStackSize-4,0xFFFFFFFCu}){
        ctx.esp=bad_stack;sub_002396E0(ctx,base);check(observed==5000,"invalid stack ranges must not read unmapped arguments");
    }
    check(calls==13,"native painters remain called exactly once");
    puts("PASS: native render distance scopes, mission limits, lower/upper values, player protection, default/reset and camera independence");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
