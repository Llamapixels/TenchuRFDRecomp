#include "cpu/x86_context.h"
#include "game_noclip.h"
#include "flight_input.h"
#include "port/memory_patches.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <span>
#include <memory>
#include <algorithm>
extern "C" void sub_0029D830(X86Context&,uint8_t*);
extern "C" void sub_002059C0(X86Context&,uint8_t*);
namespace logx {void Write(Level,const char*,...){} }
std::filesystem::path app::ExeDir(){return std::filesystem::current_path();}
uint8_t* kernel::g_base=nullptr;
input::Pad test_pad;
input::Pad input::Poll(int port){auto p=test_pad;tenchu::camera::FilterPad(port,p);return p;}
template<class T>T Read(uint8_t* b,uint32_t a){T v;std::memcpy(&v,b+a,sizeof(v));return v;}
template<class T>void Write(uint8_t* b,uint32_t a,T v){std::memcpy(b+a,&v,sizeof(v));}
static unsigned player_calls=0,physics_calls=0;
static bool prephysics_pinned=false;
extern "C" void __imp__sub_002059C0(X86Context& ctx,uint8_t* b){
    ++physics_calls;const auto p=Read<uint32_t>(b,ctx.esp+4);
    const auto velocity=Read<tenchu::camera::Vec>(b,p+0x278);
    prephysics_pinned=(Read<uint32_t>(b,p+0x31C)&0x800)&&(Read<uint32_t>(b,p+0x4CC)&0x40)&&velocity.x==0&&velocity.y==0&&velocity.z==0;
    ctx.esp+=4;
}
extern "C" void __imp__sub_0029D830(X86Context& ctx,uint8_t* b){
    ++player_calls;const auto p=Read<uint32_t>(b,ctx.esp+4);
    // Simulate an action clearing collision bits and recomputing jump/gravity.
    Write(b,p+0x31C,Read<uint32_t>(b,p+0x31C)&~0x800u);
    Write(b,p+0x4CC,Read<uint32_t>(b,p+0x4CC)&~0x40u);
    Write(b,p+0x278,tenchu::camera::Vec{5,90,-5});
    const auto original=ctx;ctx.esp-=8;Write(b,ctx.esp+4,p);sub_002059C0(ctx,b);ctx=original;
    // A later actor adjustment must not cause drift, either.
    auto v=Read<tenchu::camera::Vec>(b,p+0x20);v.y+=100;Write(b,p+0x20,v);
    ctx.eax=0x1234;ctx.cf=1;ctx.esp+=4;
}
int main(){try{
    using namespace tenchu;
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    // Match the actual port: low memory for actors and a sparse high guest
    // stack. A low vector-backed fake stack hid the production range bug.
    auto release=[](uint8_t* b){if(b)VirtualFree(b,0,MEM_RELEASE);};
    std::unique_ptr<uint8_t,decltype(release)> allocation(static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS)),release);
    auto* b=allocation.get();check(b!=nullptr,"reserve sparse guest address space");
    check(VirtualAlloc(b,0x4000000,MEM_COMMIT,PAGE_READWRITE)==b,"commit low actor memory");
    const uint32_t p=0xEB3510,other=0xF00000,cam=0x300000,stack=mem::kStackBase+0x1F000;
    check(VirtualAlloc(b+mem::kStackBase+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"commit actual high guest stack range");
    std::span<uint8_t> memory(b,0x4000000);
    auto setup=[&](uint32_t actor,camera::Vec position){
        Write(b,actor+0x1B,0x59414C50u);b[actor+0x393]=0;
        for(uint32_t off:{0x20u,0x110u,0x130u,0x1F0u,0x258u,0x268u,0x2A8u})Write(b,actor+off,position);
        Write(b,actor+0x31C,0x2004002Bu);Write(b,actor+0x4CC,0x40009000u);
    };
    setup(p,{100,200,300});setup(other,{10,20,30});b[other+0x393]=1;
    Write(b,0x1BAB110,p);Write(b,0x8E38BC,uint16_t(3));Write(b,0x8E38C0,cam);
    Write(b,cam,camera::Vec{100,100,0});Write(b,cam+0x40,camera::Vec{100,100,300});
    camera::keyboard_mouse=false;test_pad.connected=true;
    auto frame=[&](uint32_t actor){X86Context c{};c.esp=stack;Write(b,stack+4,actor);sub_0029D830(c,b);check(c.esp==stack+4&&c.eax==0x1234&&c.cf==1,"original CPU and stack effects preserved");};
    noclip::requested=false;frame(p);check(!noclip::active&&Read<camera::Vec>(b,p+0x20).y==300,"disabled uses original behavior");
    setup(p,{100,200,300});noclip::requested=true;
    for(int i=0;i<60;++i){frame(p);check(Read<camera::Vec>(b,p+0x20).y==200&&prephysics_pinned,"jump/gravity and cleared flags cannot displace hover");}
    check(noclip::active,"flight activates");
    auto position=Read<camera::Vec>(b,p+0x20);frame(other);
    check(noclip::active&&Read<camera::Vec>(b,p+0x20).y==position.y,"player two does not reset player-one flight");
    frame(p);check(prephysics_pinned,"player one remains controlled after player two updates");
    test_pad.lx=20000;test_pad.ly=25000;test_pad.rx=-20000;test_pad.ry=15000;test_pad.analog[7]=255;test_pad.analog[0]=255;
    auto filtered=input::Poll(0);check(filtered.lx==0&&filtered.rx==0&&filtered.analog[0]==0&&filtered.analog[7]==0&&camera::raw_pad.ly==25000,"flight retains raw input and suppresses jump/actions");
    const auto motion=camera::PadFlightMotion(test_pad);check(motion.right>0&&motion.forward>0&&motion.look_x<0&&motion.look_y>0&&motion.up==1,"PhotoCam stick/trigger mapping");
    test_pad.analog[4]=255;check(camera::PadFlightMotion(test_pad).multiplier==4,"PhotoCam fast button");
    test_pad.analog[4]=0;test_pad.analog[5]=255;check(camera::PadFlightMotion(test_pad).multiplier==.25f,"PhotoCam slow button");
    camera::menu_open=true;frame(p);check(Read<camera::Vec>(b,p+0x20).y==200,"F6 holds position despite trigger input");camera::menu_open=false;
    camera::free_requested=true;frame(p);check(Read<camera::Vec>(b,p+0x20).y==200,"photo mode moves camera without moving player");camera::free_requested=false;
    Write(b,0x18FE6CC,0x80000000u);frame(p);check(Read<camera::Vec>(b,p+0x20).y==200,"paused game holds flight position");Write(b,0x18FE6CC,0u);
    test_pad={};test_pad.connected=true;test_pad.analog[7]=255;
    for(int i=0;i<128;++i)frame(p);
    const float risen=Read<camera::Vec>(b,p+0x20).y;check(risen<200,"right trigger actually moves player up");
    test_pad.analog[7]=0;test_pad.analog[6]=255;
    for(int i=0;i<128;++i)frame(p);
    const float lowered=Read<camera::Vec>(b,p+0x20).y;check(lowered>risen,"left trigger actually moves player down");
    test_pad.analog[6]=0;test_pad.ly=32767;
    const float forward_before=Read<camera::Vec>(b,p+0x20).z;
    for(int i=0;i<128;++i)frame(p);
    check(Read<camera::Vec>(b,p+0x20).z>forward_before,"left stick actually moves player in view direction");
    test_pad.ly=0;const auto held=Read<camera::Vec>(b,p+0x20);
    for(int i=0;i<32;++i)frame(p);
    const auto stopped=Read<camera::Vec>(b,p+0x20);
    check(stopped.x==held.x&&stopped.y==held.y&&stopped.z==held.z,"releasing flight controls holds exact position without gravity drift");
    camera::Vec eye,target;check(noclip::View(p,eye,target)&&!noclip::View(other,eye,target),"camera view belongs to controlled player");
    test_pad.analog[0]=255;test_pad.ly=25000;
    noclip::requested=false;filtered=input::Poll(0);check(filtered.analog[0]==255&&filtered.ly==25000,"unchecking releases controls immediately");
    frame(p);check(!noclip::active&&!(Read<uint32_t>(b,p+0x31C)&0x800)&&!(Read<uint32_t>(b,p+0x4CC)&0x40),"unchecking restores physics");
    check(!noclip::View(p,eye,target),"unchecking releases camera");
    test_pad={};setup(p,{100,200,300});noclip::requested=true;frame(p);
    setup(other,{0,100,0});Write(b,0x1BAB110,other);const auto before=Read<uint32_t>(b,p+0x31C);frame(other);
    check(Read<uint32_t>(b,p+0x31C)==before&&Read<camera::Vec>(b,other+0x20).y==100,"allocation change acquires current player without restoring freed object");
    noclip::requested=false;frame(other);
    std::ifstream f(std::filesystem::path(TENCHU_SOURCE_DIR)/"docs/release/memory_patches/examples/RFD - noclip.txt");
    std::string text((std::istreambuf_iterator<char>(f)),{});auto entries=patches::Parse(text,"RFD - noclip.txt");
    check(entries.size()==1&&entries[0].noclip&&entries[0].writes.empty()&&entries[0].problems.empty(),"actual controller-only text patch parses without unsafe periodic writes");
    auto map=[&](uint32_t a,uint32_t n)->uint8_t*{return uint64_t(a)+n<=memory.size()?b+a:nullptr;};
    patches::Set set;set.Load(entries,map);set.SetEnabled(entries[0].Id(),true);std::vector<uint8_t> copy(memory.begin(),memory.end());set.Apply(map);
    check(set.StateOf(entries[0].Id())==patches::State::Active&&std::equal(copy.begin(),copy.end(),memory.begin()),"text patch arms controller without writing game memory");
    Write(b,0x8E38BC,uint16_t(1));set.Apply(map);check(set.StateOf(entries[0].Id())==patches::State::Waiting,"title screen never arms flight");
    auto bad=patches::Parse("[Bad]\nController=NoClip\nPointer=01BAB110,00EC7D10,000007D8\nMode=Once\n","bad.txt");check(!bad[0].problems.empty(),"one-shot flight rejected");
    check(player_calls==physics_calls,"original update and physics integration still execute");
    puts("PASS: noclip hover through gravity/action resets, both override points, controls, pause/photo mode, release, pointer changes and actual text patch");
}catch(const std::exception& e){puts(e.what());return 1;}}
