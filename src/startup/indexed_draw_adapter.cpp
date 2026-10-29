#include "xdk/hle.h"
#include "kernel/memory.h"
#include "core/log.h"
#include <cstring>
#include <limits>

// 0033E990 consumes 16-bit indices (MOVZX word at 0033EA50), then copies
// indexed vertices to an inline GPU stream. Reproduce that order through
// the existing native DrawVerticesUP path, preserving primitive topology.
X86_FUNC(sub_0033E990) {
    (void)base;
    kernel::Args args{ctx};
    const uint32_t prim=args[0], count=args[1], indices=args[2], vertices=args[3], stride=args[4];
    if (!count) { kernel::Return(ctx,0,5); return; }
    const uint64_t bytes=uint64_t(count)*stride;
    // The native UP ring has a two MiB half. Reject oversized draws instead
    // of letting its existing byte clamp truncate a draw silently.
    if (!indices || !vertices || !stride || bytes>(2u<<20) || uint64_t(indices)+uint64_t(count)*2>0x100000000ull)
        x86_trap(ctx,0x0033E990,"invalid or oversized indexed user-memory draw");
    uint32_t max_index=0;
    for(uint32_t i=0;i<count;++i) {
        uint16_t index;
        std::memcpy(&index,kernel::HostPtr(indices+i*2),2);
        if(index>max_index)max_index=index;
    }
    if(uint64_t(vertices)+(uint64_t(max_index)+1)*stride>0x100000000ull)
        x86_trap(ctx,0x0033E990,"indexed vertex address overflow");
    const uint32_t expanded=mem::AllocSystem(uint32_t(bytes));
    if(!expanded)x86_trap(ctx,0x0033E990,"indexed vertex allocation failed");
    struct Release { uint32_t address; ~Release(){mem::FreeSystem(address);} } release{expanded};
    for(uint32_t i=0;i<count;++i) {
        uint16_t index;
        std::memcpy(&index,kernel::HostPtr(indices+i*2),2);
        std::memcpy(kernel::HostPtr(expanded+i*stride),kernel::HostPtr(vertices+uint32_t(index)*stride),stride);
    }
    const auto abi=hle::Find("D3DDevice_DrawVerticesUP",hle::Make({hle::stack,hle::stack,hle::stack,hle::stack}));
    const uint32_t result=hle::Call(ctx,SYM(D3DDevice_DrawVerticesUP),abi,{prim,count,expanded,stride});
    LOG_FIRST(3,"Indexed user-memory draw: primitive %u, indices %u, stride %u",prim,count,stride);
    kernel::Return(ctx,result,5);
}
