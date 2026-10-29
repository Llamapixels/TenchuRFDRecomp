#include "xdk/hle.h"
#include "core/log.h"
#include <bit>

uint32_t TenchuSetStreamRolloff(uint32_t object, float value, uint32_t apply);

// Public stream wrapper 0036E77F -> 0036E6C7 -> parameter setter
// 0036E4B1, which writes rolloff at internal 3D parameter offset 0x48.
X86_FUNC(sub_0036E77F) {
    (void)base;
    kernel::Args args{ctx};
    const float value = std::bit_cast<float>(args[1]);
    const uint32_t result = TenchuSetStreamRolloff(args[0], value, args[2]);
    LOGR("Audio stream rolloff: factor %.3f, apply %u, HRESULT %08X", double(value), args[2], result);
    if (result == 0x80004001u) x86_trap(ctx, 0x0036E77F, "stream rolloff factors other than zero/one need validation");
    kernel::Return(ctx, result, 3);
}
