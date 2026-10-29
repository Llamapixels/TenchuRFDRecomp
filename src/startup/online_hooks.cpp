#include "cpu/x86_context.h"
#include <cstring>
// Xbox Live for the original Xbox closed in 2010, and the SDK only stubs its
// startup. At boot, 0003D940 checks whether Live is usable and stores the result
// at 005AC8EC; the title screen (002342B2) reads it to offer the Live features.
// With the stub it read "usable", and choosing the Live option stopped on an
// empty Account Selection screen with no way back. Report "not usable", as on a
// console without Live, so the title keeps to its offline path.
void TenchuXboxLiveUnavailable(X86Context&,uint8_t* base){
    const uint32_t unavailable=0;
    std::memcpy(base+0x005AC8EC,&unavailable,sizeof(unavailable));
}

// That flag alone did not hide the option. The title menu handler (002A6A60)
// maps the chosen item to a next state; item 5 stores state 400 (0x190) at
// 002A6E9B, and state 400 hands over to the Xbox Live task (0002B120), which
// opens Account Selection. Store state 0x0A instead: the way into the menu
// list (0x0A fade, 0x0B open the list with the choice cleared, 0x0C wait,
// 0x0D act on the choice), so the menu simply reopens. State 0 restarted the
// whole title sequence, intro movie included.
void TenchuBlockXboxLiveMenu(X86Context& ctx,uint8_t* base){
    const uint32_t menu_list=0x0A;
    std::memcpy(base+ctx.ecx+0x28,&menu_list,sizeof(menu_list));
}
