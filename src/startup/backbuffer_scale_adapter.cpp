#include "xdk/hle.h"
#include "core/log.h"
#include <bit>
#include <cmath>

uint32_t TenchuSetBackbufferScale(float x, float y);

// This title's two-float SetBackBufferScale entry, routed through raster and
// presentation state while keeping guest-visible allocation dimensions intact.
X86_FUNC(sub_00340C20) {
    (void)base;
    kernel::Args args{ctx};
    const float x = std::bit_cast<float>(args[0]), y = std::bit_cast<float>(args[1]);
    LOGR("Backbuffer scale request: %.6f x %.6f", double(x), double(y));
    if (!std::isfinite(x) || !std::isfinite(y) || x<=0 || y<=0 || x>1 || y>1)
        x86_trap(ctx, 0x00340C20, "invalid backbuffer subsampling scale");
    const uint32_t result = TenchuSetBackbufferScale(x, y);
    if (result) x86_trap(ctx, 0x00340C20, "backbuffer-scale device update failed");
    kernel::Return(ctx, result, 2);
}
