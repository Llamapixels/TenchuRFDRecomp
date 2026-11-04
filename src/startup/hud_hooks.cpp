#include "cpu/x86_ops.h"
#include "kernel/memory.h"
#include "hud_settings.h"
#include <cstring>

namespace {
template<class T>T Read(uint8_t* base,uint32_t at){T v;std::memcpy(&v,base+at,sizeof(v));return v;}
void Write(uint8_t* base,uint32_t at,uint32_t value){std::memcpy(base+at,&value,4);}
bool HudCaller(const X86Context& ctx,uint8_t* base){
    // Retail EBP frames also see direct native calls from both HUD phases.
    if(ctx.esp<mem::kStackBase||uint64_t(ctx.esp)+8>uint64_t(mem::kStackBase)+mem::kStackSize)return false;
    const uint64_t end=std::min(uint64_t(ctx.esp)+0x10000,uint64_t(mem::kStackBase)+mem::kStackSize);
    uint32_t frame=ctx.ebp;
    for(unsigned i=0;i<16;++i){
        if(frame<ctx.esp||uint64_t(frame)+8>end||(frame&3))return false;
        uint32_t caller=Read<uint32_t>(base,frame+4);
        // Separate retail UI subsystems: SSgau owns the kanji row; SEL and
        // ITEM_CUR own the selected-item graphic and rotating background.
        // Their painters are not in the older 200xxx gauge/health task group.
        if((caller>=0x00200950&&caller<0x00203230)||
           (caller>=0x00203920&&caller<0x00204590)||
           (caller>=0x00197A30&&caller<0x00197FC6)||
           (caller>=0x002C1A10&&caller<0x002C1FB2)||
           // Includes ITEM_CUR update 2C2950 (draw at2C2F32) as well as
           // its separate render callback2C2F70. Both emit the spinner.
           (caller>=0x002C2470&&caller<0x002C3055)||
           (caller>=0x002C3A90&&caller<0x002C461C))return true;
        // Shared digits/fonts (203230..203920 and item-selection renderers)
        // require a recognized HUD ancestor; do not classify them on their own.
        uint32_t previous=Read<uint32_t>(base,frame);
        if(previous<=frame)return false;
        frame=previous;
    }
    return false;
}
}

// Runs BEFORE the count-increment store in all five native sprite builders.
// The current record is complete, but count still indexes it. Capacity early-
// outs skip these sites. Each HUD sprite is adjusted once, without depending
// on callback interposition or changing the guest CPU state.
void TenchuHudSpriteEnqueue(X86Context& ctx,uint8_t* base){
    using namespace tenchu::hud;
    const unsigned percent=Opacity();
    if(percent==100||Read<uint16_t>(base,0x008E38BC)!=3)return;
    if(ctx.ecx!=queue&&ctx.edx!=queue)return;
    const uint32_t index=Read<uint32_t>(base,count_address);
    if(index>=capacity||!HudCaller(ctx,base))return;
    const uint32_t sprite=queue+index*stride;
    const uint32_t vertices=Read<uint32_t>(base,sprite+0x70);
    if(vertices<1||vertices>4)return;
    // Type 8 is the untextured XYZRHW/color layout (00012560). Other
    // sprite builders also carry UV coordinates and use a 0x1C-byte stride.
    const uint32_t vertex_stride=Read<uint32_t>(base,sprite+0x74)==8?0x14:0x1C;
    for(uint32_t i=0;i<vertices;++i){
        uint32_t at=sprite+i*vertex_stride+0x10;
        Write(base,at,FadeColor(Read<uint32_t>(base,at),percent));
    }
    if(Read<uint32_t>(base,sprite+0x7C)==1)Write(base,sprite+0x7C,2);
}
