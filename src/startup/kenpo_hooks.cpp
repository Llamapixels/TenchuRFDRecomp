#include "cpu/x86_ops.h"
#include <algorithm>
#include <array>

X86_EXTERN_FUNC(__imp__sub_002BF0C0);
X86_EXTERN_FUNC(__imp__sub_002BE330);

namespace {
bool Kenpo(uint8_t* base) {
    return X86_MEM32(0x0049BDC8) == 0x004CF5E0 &&
        X86_MEM32(0x004CF5E0) == 0x00013B00 &&
        X86_MEM32(0x004CF5FC) == 0x00495C50 &&
        X86_MEM32(0x00455E60) == 0x00402950;
}
}

// The original resource-list selector returns one model for Rikimaru, but
// the Priest's animation tables use both EM23 and EM23MT. Reuse the retail
// Priest list only when the complete Kenpo text patch is present. Keep the
// story character ID at zero: story placement/save tables have three columns.
X86_FUNC(sub_002BF0C0) {
    if (X86_MEM16(ctx.esp + 4) == 0 && Kenpo(base)) {
        ctx.eax = 0x004F03D0;
        ctx.esp += 4; // retail selector's cdecl return
        return;
    }
    __imp__sub_002BF0C0(ctx, base);
}

// Story missions bypass 002BF0C0. Extend their sorted model lists on each
// loader call, before allocation/loading begins. Keep all mission resources,
// including the PL20 costume variants, and let the retail state machine load
// EM23 and EM23MT as normal. Its task cursor remains valid across calls.
X86_FUNC(sub_002BE330) {
    const uint32_t task=X86_MEM32(ctx.esp+4), list=X86_MEM32(ctx.esp+8);
    if (!Kenpo(base) || X86_MEM16(0x008E40A8)!=0 ||
        list<0x10000 || uint64_t(list)+64*4>0x04000000 || ctx.esp<0x400) {
        __imp__sub_002BE330(ctx,base);
        return;
    }
    std::array<uint32_t,67> models{};
    unsigned count=0;
    while (count<64 && X86_MEM16(list+count*4)!=0xFFFF) {
        models[count]=X86_MEM32(list+count*4);
        ++count;
    }
    if (count==64) {
        __imp__sub_002BE330(ctx,base);
        return;
    }
    for (uint32_t model=0x3B;model<=0x3C;++model) {
        auto end=models.begin()+count;
        auto found=std::find_if(models.begin(),end,[&](uint32_t x){return (x&0xFFFF)==model;});
        if(found==end)models[count++]=model;
        else *found=model; // Both mesh and motion are required for the player.
    }
    std::sort(models.begin(),models.begin()+count,[](uint32_t a,uint32_t b){return (a&0xFFFF)<(b&0xFFFF);});
    models[count++]=0xFFFFFFFF;
    const uint32_t incoming=ctx.esp;
    const uint32_t scratch=(incoming-count*4)&~15u;
    for(unsigned i=0;i<count;++i)X86_MEM32(scratch+i*4)=models[i];
    // The scratch list is above the nested call's stack, so its frames cannot
    // overwrite it. The retail loader uses the pointer only during this call.
    ctx.esp=scratch-12;
    X86_MEM32(ctx.esp)=0;
    X86_MEM32(ctx.esp+4)=task;
    X86_MEM32(ctx.esp+8)=scratch;
    __imp__sub_002BE330(ctx,base);
    ctx.esp=incoming+4;
}
