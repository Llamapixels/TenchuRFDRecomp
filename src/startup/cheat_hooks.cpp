#include "cpu/x86_context.h"
#include "game_cheats.h"
// Register-specific hooks keep every original health-store instruction and
// its CPU side effects intact. The shared policy ignores all non-player data.
#define HEALTH_HOOK(reg) void TenchuProtectHealth_##reg(X86Context& ctx,uint8_t* base){tenchu::cheats::ProtectHealth(base,ctx.reg);}
HEALTH_HOOK(eax)
HEALTH_HOOK(ebx)
HEALTH_HOOK(ecx)
HEALTH_HOOK(edx)
HEALTH_HOOK(esi)
HEALTH_HOOK(edi)
HEALTH_HOOK(ebp)
HEALTH_HOOK(esp)
