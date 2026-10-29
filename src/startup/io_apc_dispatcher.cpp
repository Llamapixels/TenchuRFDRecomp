#include "kernel/exports.h"
#include "cpu/x86_ops.h"
#include "core/log.h"
#include <atomic>

namespace dispatch { uint32_t KernelThunkAddress(uint32_t ordinal); }

namespace kernel {
// XAPI ReadFileEx/WriteFileEx pass the title's completion routine as context.
// IO_STATUS_BLOCK aliases the first two OVERLAPPED fields. Preserve that exact
// pointer when calling Completion(error, bytes, overlapped).
KERNEL_EXPORT(232, NtUserIoApcDispatcher, 3) {
    const uint32_t completion=a[0], iosb=a[1];
    if(!completion || !iosb) x86_trap(ctx,ctx.last_icall,"I/O APC has a null callback or status block");
    const uint32_t status=R32(iosb), bytes=R32(iosb+4);
    const uint32_t error=CallGuest(ctx,dispatch::KernelThunkAddress(301),{status});
    CallGuest(ctx,completion,{error,bytes,iosb});
    static std::atomic<unsigned> delivered{0};
    if(delivered.fetch_add(1)==0) LOGR("Milestone: asynchronous file completion delivered to the title");
    return 0;
}
}
