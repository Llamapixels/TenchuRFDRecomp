#include "xdk/hle.h"
#include "core/log.h"

// 0036F5EF is the four-stack-argument device stream-creation wrapper.
// The matched global DirectSoundCreateStream calls the same internal routine
// 0036F27D, passing its default device and a null aggregation argument.
X86_FUNC(sub_0036F5EF) {
    (void)base;
    kernel::Args args{ctx};
    const uint32_t device = args[0], descriptor = args[1], output = args[2], outer = args[3];
    if (outer) x86_trap(ctx, 0x0036F5EF, "aggregated audio streams are unsupported");
    if (!device || !descriptor || !output) {
        kernel::Return(ctx, 0x80070057u, 4); // E_INVALIDARG
        return;
    }
    const uint32_t saved_sp = ctx.esp;
    const auto abi = hle::Find("DirectSoundCreateStream", hle::Make({hle::stack, hle::stack}));
    const uint32_t result = hle::Call(ctx, SYM(DirectSoundCreateStream), abi, {descriptor, output});
    if (ctx.esp != saved_sp) x86_trap(ctx, 0x0036F5EF, "audio stream adapter stack imbalance");
    LOGR("Audio stream adapter: HRESULT %08X, object %08X", result, result & 0x80000000u ? 0 : kernel::R32(output));
    kernel::Return(ctx, result, 4);
}
