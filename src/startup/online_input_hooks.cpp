#include "cpu/x86_ops.h"
#include "kernel/memory.h"
#include "port/online.h"
#include "port/online_input_window.h"
#include "core/log.h"
#include <cstring>

X86_EXTERN_FUNC(__imp__sub_00021490);
X86_EXTERN_FUNC(__imp__sub_000216D0);
X86_EXTERN_FUNC(sub_00017880);
namespace {
using namespace tenchu::online::input_window;
bool modern_receiver=false;
bool Valid(uint32_t p,uint32_t bytes){
    if(!p||uint64_t(p)+bytes>(uint64_t(1)<<32))return false;
    for(uint64_t page=p;page<uint64_t(p)+bytes;page=(page+4096)&~uint64_t(4095))if(!mem::IsCommitted(uint32_t(page)))return false;
    return true;
}
uint32_t Read(uint8_t* b,uint32_t p){uint32_t n=0;std::memcpy(&n,b+p,4);return n;}
void Write(uint8_t* b,uint32_t p,uint32_t n){std::memcpy(b+p,&n,4);}
bool Active(uint8_t* b){return tenchu::online::enabled.load()&&Read(b,0x5A97D0)&&Read(b,0x5A97CC);}
}
// Preserve stock capture and its four-frame packets. Extra overlapping history
// sends the newest genuine sample immediately, only to a capable host.
X86_FUNC(sub_00021490){
    const auto before=Read(base,0x5A8758);
    __imp__sub_00021490(ctx,base);
    if(!Active(base)||Read(base,0x5A97B8)||before>=200)return;
    const auto after=Read(base,0x5A8758);
    if(after!=(before+1)%200||!tenchu::online::PeerSupportsPerFrameInput(Read(base,0x58309C)))return;
    const auto block=Read(base,0x583090),phase=after%4;
    const uint64_t end=uint64_t(block)+phase;
    if(!end||end>sequence_mask)return;
    const unsigned count=end<4?unsigned(end):4;
    const auto first=uint32_t(end-count);
    const auto original=ctx;
    if(ctx.esp<0x800||!Valid(ctx.esp-0x800,0x800))return;
    const auto scratch=ctx.esp-0x800,packet=scratch+0x40,address=scratch+0x600;
    std::memset(base+packet,0,0x4E8);Write(base,packet,0x000B0076);Write(base,packet+4,Tag(first,count));
    for(unsigned i=0;i<count;++i){const auto index=(after+200-count+i)%200;std::memcpy(base+packet+8+i*28,base+0x59CD80+index*28,28);}
    std::memset(base+address,0,16);base[address]=2;base[address+2]=3;base[address+3]=0xE8;
    Write(base,address+4,Read(base,0x58309C));
    ctx.esp=scratch+0x30;ctx.esp-=4;Write(base,ctx.esp,address);ctx.esp-=4;Write(base,ctx.esp,packet);ctx.esp-=4;Write(base,ctx.esp,0);
    sub_00017880(ctx,base);ctx=original;
    static bool reported=false;if(!reported){reported=true;LOGR("Online input: negotiated per-frame P2 sending with four-sample redundant history");}
}
// Old peers keep the untouched decoder. Once enhanced history arrives, merge
// it and stock packets by sequence so overlap never repeats movement or attacks.
X86_FUNC(sub_000216D0){
    if(!Active(base)||!Read(base,0x5A97B8)||!Valid(ctx.esp+4,116)){__imp__sub_000216D0(ctx,base);return;}
    const auto encoded=Read(base,ctx.esp+4),expected=Read(base,0x583090),queued=Read(base,0x5A97EC);
    if(!expected&&!queued)modern_receiver=false;
    const bool enhanced=(encoded&marker)!=0;
    if(!enhanced&&!modern_receiver){__imp__sub_000216D0(ctx,base);return;}
    Window window;window.first=enhanced?encoded&sequence_mask:encoded;window.count=enhanced?((encoded>>29)&3)+1:4;
    std::memcpy(window.samples.data(),base+ctx.esp+8,112);
    const auto cursor=Read(base,0x5A8758);const auto update=Accept(expected,queued,window);
    if(update&&cursor<200){
        for(size_t i=0;i<update->samples.size();++i)std::memcpy(base+0x59CD80+((cursor+uint32_t(i))%200)*28,update->samples[i].data(),28);
        if(!update->samples.empty()){
            // 00021910 retains its last read slot on queue underflow. Once a
            // fresh sample arrives, advance past that already-consumed slot.
            // Otherwise one-frame delivery would replay the previous input.
            if(!queued&&expected&&Read(base,0x5A8E7C)==(cursor+199)%200)Write(base,0x5A8E7C,cursor);
            Write(base,0x5A8758,(cursor+uint32_t(update->samples.size()))%200);
            Write(base,0x5A97EC,queued+uint32_t(update->samples.size()));Write(base,0x583090,update->next);
            Write(base,0x5A97DC,0);Write(base,0x5A97D4,1);
        }
        if(enhanced&&!modern_receiver){modern_receiver=true;LOGR("Online input: host accepting per-frame P2 samples with sequence deduplication");}
    }
    ctx.esp+=4; // Native cdecl void return; caller owns its argument storage.
}
