#include "cpu/x86_ops.h"
#include "core/log.h"
#include "game_cheats.h"

X86_EXTERN_FUNC(__imp__sub_00306CBC);
X86_EXTERN_FUNC(__imp__sub_0005C860);
X86_EXTERN_FUNC(__imp__sub_0029FD30);
X86_EXTERN_FUNC(__imp__sub_002BD860);
X86_EXTERN_FUNC(__imp__sub_002C9580);
X86_EXTERN_FUNC(__imp__sub_002A8750);
void TenchuAutoplayStoryStarted();

// Observation only: forward the original registers/memory to the generated body.
X86_FUNC(sub_00306CBC) {
    LOGR("Milestone: entered title initialization callback 00306CBC");
    __imp__sub_00306CBC(ctx, base);
}
X86_FUNC(sub_0005C860) {
    LOGR("Milestone: entered game startup routine 0005C860");
    __imp__sub_0005C860(ctx, base);
}
X86_FUNC(sub_0029FD30) {
    LOG_FIRST(1,"Milestone: entered post-character loading task 0029FD30");
    uint32_t actor;std::memcpy(&actor,base+ctx.esp+4,4);
    tenchu::cheats::ProtectHealth(base,actor);
    __imp__sub_0029FD30(ctx,base);
    tenchu::cheats::ProtectHealth(base,actor);
}
X86_FUNC(sub_002A8750) {
    TenchuAutoplayStoryStarted();
    LOG_FIRST(1,"Milestone: entered Story Mode selection handler 002A8750");
    __imp__sub_002A8750(ctx,base);
}
X86_FUNC(sub_002BD860) {
    LOG_FIRST(1,"Milestone: entered Story Mode presentation task 002BD860");
    __imp__sub_002BD860(ctx,base);
}
X86_FUNC(sub_002C9580) {
    LOG_FIRST(1,"Milestone: entered character-confirmation task 002C9580");
    __imp__sub_002C9580(ctx,base);
}
