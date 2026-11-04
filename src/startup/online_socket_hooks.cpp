#include "cpu/x86_ops.h"
#include "port/online.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "port/online_input_timing.h"
#include <cstring>

namespace {
bool Read(const uint8_t* base,uint32_t at,void* out,uint32_t bytes){
    if(!at||uint64_t(at)+bytes>(uint64_t(1)<<32))return false;
    for(uint64_t page=at;page<uint64_t(at)+bytes;page=(page+0x1000)&~uint64_t(0xfff))if(!mem::IsCommitted(uint32_t(page)))return false;
    std::memcpy(out,base+at,bytes);return true;
}
void ControlTiming(uint8_t* base,uint32_t data,uint32_t bytes,bool sending){
    if(!tenchu::online::enabled.load()||bytes<8)return;
    uint8_t header[8]{};if(!Read(base,data,header,8))return;
    const auto type=header[2];
    if(type!=0x0B&&type!=0x08)return; // native controls / world stream only
    uint32_t sequence=0;std::memcpy(&sequence,header+4,4);
    const bool enhanced=type==0x0B&&(sequence&0x80000000u);
    if(enhanced)sequence=(sequence&0x1fffffffu)+((sequence>>29)&3);
    uint32_t host=0,queued=0,playback=0,latest=0;
    Read(base,0x5A97B8,&host,4);Read(base,0x5A97EC,&queued,4);
    Read(base,0x59E370,&playback,4);Read(base,0x5A1640,&latest,4);
    if(sending&&type==0x0B&&!host){
        if(const auto delay=tenchu::online::timing::Sent()){
            LOGR("Online input timing: observed local change -> next UDP control batch %.2f ms, sequence=%u",*delay,sequence);
        }
    }
    struct Rate {int64_t last=0,start=0,total=0,min=INT64_MAX,max=0;unsigned count=0,reports=0;};
    static std::mutex mutex;static Rate streams[6];std::lock_guard guard(mutex);
    auto& rate=streams[(sending?3:0)+(type==0x0B?(enhanced?2:1):0)];const auto now=tenchu::online::timing::Now();
    if(!rate.start)rate.start=now;
    if(rate.last){const auto gap=now-rate.last;rate.total+=gap;rate.min=std::min(rate.min,gap);rate.max=std::max(rate.max,gap);++rate.count;}
    rate.last=now;
    if(now-rate.start>=5000000&&rate.reports<12&&rate.count){
        LOGR("Online UDP timing: %s %s %s, sequence=%u, intervals=%u min/avg/max=%.2f/%.2f/%.2f ms, queued=%u playback-gap=%u frames",
            host?"host":"guest",sending?"send":"receive",type==0x0B?(enhanced?"controls-fast":"controls-stock"):"world",sequence,rate.count,
            rate.min/1000.,double(rate.total)/rate.count/1000.,rate.max/1000.,queued,
            latest<200&&playback<200?(latest+200-playback)%200:0);
        ++rate.reports;rate.start=now;rate.total=0;rate.min=INT64_MAX;rate.max=0;rate.count=0;
    }
}
void Trace(const char* what,uint8_t* base,uint32_t object,uint32_t address,uint32_t bytes,uint32_t result,unsigned& budget){
    if(!tenchu::online::enabled.load()||budget++>=24)return;
    uint8_t a[16]{};uint32_t socket=0;Read(base,object,&socket,4);Read(base,address,a,16);
    LOGR("Online socket: %s socket=%X peer=%u.%u.%u.%u:%u bytes=%u result=%08X",what,socket,a[4],a[5],a[6],a[7],unsigned(a[2])*256+a[3],bytes,result);logx::Flush();
}
}
X86_EXTERN_FUNC(__imp__sub_00303970);
X86_FUNC(sub_00303970){
    uint32_t address=0;Read(base,ctx.esp+4,&address,4);const auto object=ctx.ecx;
    __imp__sub_00303970(ctx,base);static unsigned budget=0;Trace("bind",base,object,address,0,ctx.eax,budget);
}
X86_EXTERN_FUNC(__imp__sub_00303DA0);
X86_FUNC(sub_00303DA0){
    uint32_t args[3]{};Read(base,ctx.esp+4,args,sizeof(args));const auto object=ctx.ecx;
    __imp__sub_00303DA0(ctx,base);static unsigned budget=0;Trace("send",base,object,args[2],args[1],ctx.eax,budget);
    if(int32_t(ctx.eax)>0)ControlTiming(base,args[0],uint32_t(ctx.eax),true);
}
X86_EXTERN_FUNC(__imp__sub_00303A60);
X86_FUNC(sub_00303A60){
    uint32_t args[3]{};Read(base,ctx.esp+4,args,sizeof(args));const auto object=ctx.ecx;
    __imp__sub_00303A60(ctx,base);if(int32_t(ctx.eax)>0){static unsigned budget=0;Trace("receive",base,object,args[2],ctx.eax,ctx.eax,budget);ControlTiming(base,args[0],ctx.eax,false);}
}
