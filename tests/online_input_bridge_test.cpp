#include "cpu/x86_context.h"
#include "port/online.h"
#include "port/online_input_window.h"
#include "core/log.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
using namespace tenchu::online::input_window;
extern "C" void sub_00021490(X86Context&,uint8_t*);
extern "C" void sub_000216D0(X86Context&,uint8_t*);
namespace mem {bool IsCommitted(uint32_t p){return p>=0x10000&&p<0x4000000;}}
namespace logx {void Write(Level,const char*,...){} }
bool capable=true;unsigned frame=0,legacy_calls=0;
bool tenchu::online::PeerSupportsPerFrameInput(uint32_t ip){return capable&&ip==0x0100007f;}
uint32_t Read(uint8_t* b,uint32_t p){uint32_t v;std::memcpy(&v,b+p,4);return v;}
void Write(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
std::vector<std::array<uint8_t,120>> packets;
extern "C" void __imp__sub_00021490(X86Context& ctx,uint8_t* b){
    auto cursor=Read(b,0x5A8758);auto at=0x59CD80+cursor*28;
    std::memset(b+at,0,28);b[at]=uint8_t(frame);b[at+4]=frame%17==0?1:0;
    cursor=(cursor+1)%200;Write(b,0x5A8758,cursor);
    if(cursor%4==0)Write(b,0x583090,Read(b,0x583090)+4);
    ctx.esp+=4;
}
extern "C" void __imp__sub_000216D0(X86Context& ctx,uint8_t*){++legacy_calls;ctx.esp+=4;}
extern "C" void sub_00017880(X86Context& ctx,uint8_t* b){
    const auto p=Read(b,ctx.esp+4),a=Read(b,ctx.esp+8);
    if(Read(b,a+4)!=0x0100007f||b[a+2]!=3||b[a+3]!=0xE8)throw std::runtime_error("wrong peer/UDP port");
    std::array<uint8_t,120> packet;std::memcpy(packet.data(),b+p,120);packets.push_back(packet);ctx.esp+=4;
}
int main(){try{
    auto check=[](bool b,const char* s){if(!b)throw std::runtime_error(s);};
    std::vector<uint8_t> guest(0x4000000),host(0x4000000);auto* g=guest.data();auto* h=host.data();
    tenchu::online::enabled=true;
    for(auto b:{g,h}){Write(b,0x5A97D0,1);Write(b,0x5A97CC,1);}
    Write(g,0x58309C,0x0100007f);Write(h,0x5A97B8,1);
    X86Context ctx{};ctx.esp=0x3F00000;
    for(frame=0;frame<450;++frame){
        ctx={};ctx.esp=0x3F00000;ctx.eax=123;ctx.cf=1;
        sub_00021490(ctx,g);
        check(ctx.esp==0x3F00004&&ctx.eax==123&&ctx.cf==1,"extra send preserves native capture context");
        check(packets.size()==frame+1,"one immediate packet per captured frame");
        auto& p=packets.back();check(p[0]==0x76&&p[2]==0x0B,"native packet framing");
        // Deliver history through the actual bridge. It catches a lost packet,
        // and each duplicate is ignored before reaching the native actor ring.
        if(frame%13!=7){
            const auto old=Read(h,0x583090);
            auto stock=[&](){
                std::memcpy(h+0x3F00004,p.data()+4,116);
                Write(h,0x3F00004,Read(h,0x3F00004)&sequence_mask);
                ctx={};ctx.esp=0x3F00000;sub_000216D0(ctx,h);
            };
            if(frame%8==3)stock(); // stock block beats the enhanced datagram
            std::memcpy(h+0x3F00004,p.data()+4,116);ctx={};ctx.esp=0x3F00000;
            sub_000216D0(ctx,h);check(ctx.esp==0x3F00004,"native cdecl receiver return");
            const auto next=Read(h,0x583090),queued=Read(h,0x5A97EC),cursor=Read(h,0x5A8758);
            check(next==frame+1&&queued==next-old,"only unseen samples appended");
            for(uint32_t i=0;i<queued;++i){auto at=0x59CD80+((cursor+200-queued+i)%200)*28;check(h[at]==uint8_t(old+i)&&h[at+4]==((old+i)%17==0?1:0),"native ring retains movement and attack edges across wrap/loss");}
            ctx.esp=0x3F00000;sub_000216D0(ctx,h);check(Read(h,0x5A97EC)==queued,"duplicate packet does not enqueue inputs twice");
            if(frame%8==7){stock();check(Read(h,0x5A97EC)==queued,"stock overlap does not repeat already received inputs");}
            // Model the native 00021910 consumer, including its underflow
            // cursor hold. Merely checking the append buffer misses replays.
            for(uint32_t i=0;i<queued;++i){
                auto reader=Read(h,0x5A8E7C);const auto at=0x59CD80+reader*28;
                check(h[at]==uint8_t(old+i)&&h[at+4]==((old+i)%17==0?1:0),"actual native read cursor consumes each input/attack edge exactly once");
                reader=(reader+1)%200;
                if(reader==Read(h,0x5A8758))reader=reader?reader-1:0;
                Write(h,0x5A8E7C,reader);Write(h,0x5A97EC,Read(h,0x5A97EC)-1);
            }
            if(frame>5){
                std::memcpy(h+0x3F00004,packets[frame-5].data()+4,116);ctx.esp=0x3F00000;sub_000216D0(ctx,h);
                check(Read(h,0x5A97EC)==0&&Read(h,0x583090)==next,"late reordered history cannot rewind the input stream");
            }
        }
    }
    capable=false;const auto count=packets.size();ctx.esp=0x3F00000;sub_00021490(ctx,g);check(packets.size()==count,"legacy host never receives enhanced packets");
    Write(h,0x583090,0);Write(h,0x5A97EC,0);Write(h,0x3F00004,0);ctx.esp=0x3F00000;sub_000216D0(ctx,h);check(legacy_calls==1,"legacy receiver path preserved after reset");
    puts("PASS: native bridge sends per frame, preserves context/framing, merges input ring safely, recovers loss and falls back for old peers.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
