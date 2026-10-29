#include "diagnostics/autoplay.h"
#include <cstdio>
int main(){
    auto before=tenchu::AutoplayPad(0,0),start=tenchu::AutoplayPad(0,500);
    auto confirm=tenchu::AutoplayPad(0,3000),released=tenchu::AutoplayPad(0,3300);
    auto stopped=tenchu::AutoplayPad(0,120000),other=tenchu::AutoplayPad(1,3000);
    if(!before.connected||before.buttons||start.buttons!=(1u<<4)||confirm.analog[0]!=255||released.analog[0]||stopped.analog[0]||stopped.buttons||other.connected)return 1;
    if(tenchu::AutoplayPad(0,18000).buttons!=(1u<<4)||tenchu::AutoplayPad(0,18000,true).buttons)return 1;
    std::puts("PASS: bounded synthetic Start/A press-release sequence and port isolation");
}
