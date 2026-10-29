#include "xdk/hle.h"
#include "core/log.h"

// The title calls the four-argument PauseEx wrapper at 0036DEEC.
// Its inner method 003722F5 accepts a 64-bit time followed by pause mode;
// zero time forwards directly to the same method used by stream Pause.
X86_FUNC(sub_0036DEEC) {
    (void)base;
    kernel::Args args{ctx};
    const uint32_t object=args[0], low=args[1], high=args[2], mode=args[3];
    if (low || high)
        x86_trap(ctx, 0x0036DEEC, "scheduled audio pause is unsupported");
    if (!object || mode > 3) {
        kernel::Return(ctx, 0x80070057u, 4);
        return;
    }
    // Mode 3 is pause-until-ready in the Xbox implementation; the mixer
    // does not yet model that transition. Keep it explicit if requested.
    if (mode == 3)
        x86_trap(ctx, 0x0036DEEC, "pause-until-ready audio mode is unsupported");
    const auto abi=hle::Find("CDirectSoundStream_Pause", hle::Make({hle::stack, hle::stack}));
    const uint32_t result=hle::Call(ctx, SYM(CDirectSoundStream_Pause), abi, {object, mode});
    LOGR("Audio PauseEx adapter: mode %u, HRESULT %08X", mode, result);
    kernel::Return(ctx, result, 4);
}
