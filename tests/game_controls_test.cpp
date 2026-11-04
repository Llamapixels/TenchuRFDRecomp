#include "game_controls.h"
#include "game_camera.h"
#include "core/log.h"
#include <cstdio>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace logx {void Write(Level,const char*,...){} }
int main(){try{
    using namespace tenchu::controls;
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    Settings s;std::array<bool,256> held{};input::Pad pad{};
    held['W']=held['D']=held[0x01]=held[0x20]=true;
    auto mapped=Map(s,pad,held,true);
    check(mapped.connected&&mapped.lx>23000&&mapped.ly>23000&&std::hypot(float(mapped.lx),float(mapped.ly))<32769,"keyboard diagonal must be normalized and connect virtual pad");
    check(mapped.analog[2]==255&&mapped.analog[0]==255,"default mouse attack and Space jump must reach Xbox buttons");
    held.fill(false);held['W']=held[0x0D]=true;mapped=Map(s,pad,held,true,true);
    check((mapped.buttons&1)&&mapped.analog[0]==255,"keyboard must navigate menus and confirm without a controller");
    held.fill(false);held[0x1B]=true;mapped=Map(s,pad,held,true,true);
    check(mapped.analog[1]==255&&!(mapped.buttons&16),"menu back must not also press Start");
    KeyboardGate gate;gate.Filter(held,0,0);auto stillHeld=held;gate.Filter(stillHeld,1,0);
    check(!stillHeld[0x1B],"the Escape press that pauses must not immediately become menu Back");
    stillHeld=held;gate.Filter(stillHeld,1,0);check(!stillHeld[0x1B],"held transition keys must stay blocked across repeated polls");
    stillHeld.fill(false);gate.Filter(stillHeld,1,0);stillHeld=held;gate.Filter(stillHeld,1,0);check(stillHeld[0x1B],"release and repress must enable menu Back");
    stillHeld=held;gate.Filter(stillHeld,1,2);check(!stillHeld[0x1B],"opening and closing F6 between polls must still gate held keys");
    held.fill(false);held['W']=held[0x10]=true;mapped=Map(s,pad,held,true);
    check(mapped.ly>14000&&mapped.ly<15000,"walk modifier must produce partial analog movement");
    s.keyboard_bindings[X]='J';held.fill(false);held[0x01]=true;mapped=Map(s,pad,held,true);check(!mapped.analog[2],"remapped mouse input must stop activating old action");
    held['J']=true;mapped=Map(s,pad,held,true);check(mapped.analog[2]==255,"keyboard remap must activate replacement action");
    pad.connected=true;pad.analog[7]=137;s.controller_bindings[A]=RT;s.controller_bindings[RT]=-1;
    mapped=Map(s,pad,held,false);check(mapped.analog[0]==137&&!mapped.analog[7]&&!mapped.analog[2],"controller remap must preserve trigger pressure and respect keyboard gate");
    s.controller_bindings[MoveForward]=LookUp;pad.ry=23000;mapped=Map(s,pad,held,false);check(mapped.ly==23000,"stick directions must be remappable without digital quantization");
    s=Settings{};pad={};pad.buttons=0xA5;for(int i=0;i<8;++i)pad.analog[i]=uint8_t(i*31);pad.lx=-15200;pad.ly=21111;pad.rx=12345;pad.ry=-9000;
    mapped=Map(s,pad,held,false);check(mapped.buttons==pad.buttons&&mapped.lx==pad.lx&&mapped.ly==pad.ly&&mapped.rx==pad.rx&&mapped.ry==pad.ry,"default controller bindings must preserve analog values");
    for(int i=0;i<8;++i)check(mapped.analog[i]==pad.analog[i],"default analog buttons must pass through");
    check(Axis(4000,.2f)==0&&Axis(32767,.2f)==1&&Axis(-32768,.2f)==-1,"deadzone must preserve full camera range");
    check(ReservedKey(0x75)&&ReservedKey(0x79)&&!ReservedKey('J'),"port hotkeys must be reserved");
    // No game window/focus: no desktop keys, mouse deltas or virtual connection.
    Configure(Settings{});pad={};Apply(0,pad);check(!pad.connected&&pad.buttons==0&&pad.lx==0,"unfocused keyboard must never drive the game");
    SetMenuOpen(true);pad.connected=true;pad.analog[2]=255;pad.lx=30000;Apply(0,pad);
    check(pad.connected&&!pad.analog[2]&&!pad.lx,"F6 must retain connection while suppressing gameplay");SetMenuOpen(false);
    check(!MouseCaptured()&&ConsumeMouse()==std::array<float,2>{0,0},"mouse must start released without a window");
    HWND window=CreateWindowExW(0,L"STATIC",L"Input unit test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr,"hidden input test window must initialize");InitializeWindow(window);
    SetMenuOpen(true);pad={};Apply(0,pad);check(pad.connected&&!pad.buttons&&!pad.lx,"keyboard virtual controller must stay connected while F6 is open");
    SetMenuOpen(false);pad={};Apply(0,pad);check(pad.connected&&!pad.buttons&&!pad.lx,"Alt-Tab must neutralize keyboard without disconnecting its controller");
    DestroyWindow(window);
    puts("PASS: keyboard gameplay, analog walking, diagonal limit, controller/stick remaps, deadzone, focus/menu isolation");
}catch(const std::exception& e){puts(e.what());return 1;}}
