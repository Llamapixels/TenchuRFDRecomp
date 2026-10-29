#include "graphics/presentation_resize.h"
#include <cstdio>
#include <stdexcept>
int main(){try{
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    tenchu::resize::State state;
    state.Message(WM_SIZE,SIZE_RESTORED,10);check(!state.Paused(11),"initial window sizing must not block first presentation");
    state.presented=true;
    state.Message(WM_ENTERSIZEMOVE,0,100);check(state.Paused(10000),"presentation must stay paused through a long drag");
    for(uint64_t t=101;t<1000;++t){state.Message(WM_SIZE,SIZE_RESTORED,t);check(state.Paused(t),"resize storm must not present");}
    state.Message(WM_EXITSIZEMOVE,0,1000);check(state.Paused(1149)&&!state.Paused(1150),"resume only after dimensions settle");
    state.Message(WM_SIZE,SIZE_MINIMIZED,2000);check(state.Paused(9999),"minimized window must not create tiny swapchains");
    state.Message(WM_SIZE,SIZE_RESTORED,10000);check(state.Paused(10001)&&!state.Paused(10150),"restore must debounce then resume");
    state.Message(WM_SIZE,SIZE_MAXIMIZED,11000);state.Message(WM_SIZE,SIZE_RESTORED,11100);
    check(state.Paused(11200)&&!state.Paused(11250),"programmatic maximize/restore must use final size");
    puts("PASS: resize storms, drag-end debounce, minimize/restore, initial frame and maximize transitions");
}catch(const std::exception& e){puts(e.what());return 1;}}
