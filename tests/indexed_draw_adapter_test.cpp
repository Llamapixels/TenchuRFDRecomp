#include "xdk/hle.h"
#include "kernel/memory.h"
#include "core/log.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
X86_EXTERN_FUNC(sub_0033E990);
namespace kernel { uint8_t* g_base=nullptr; }
namespace logx { void Write(Level,const char*,...){} }
[[noreturn]] void x86_trap(X86Context&,uint32_t,const char* message){throw std::runtime_error(message);}
static unsigned allocations=0,frees=0,calls=0;
static bool fail_allocation=false;
uint32_t mem::AllocSystem(uint32_t bytes,uint32_t){
    ++allocations;
    if(bytes!=12)throw std::runtime_error("wrong expanded size");
    return fail_allocation?0:0x1000;
}
void mem::FreeSystem(uint32_t address){if(address!=0x1000)throw std::runtime_error("wrong free");++frees;}
uint32_t kernel::CallGuest(X86Context& ctx,uint32_t fn,const uint32_t* args,size_t count){
    ++calls;
    if(fn!=SYM(D3DDevice_DrawVerticesUP)||count!=4||args[0]!=5||args[1]!=3||args[2]!=0x1000||args[3]!=4)
        throw std::runtime_error("wrong native draw ABI");
    if(R32(0x1000)!=333||R32(0x1004)!=111||R32(0x1008)!=333)
        throw std::runtime_error("index order or repeated vertices changed");
    ctx.ebx=ctx.ecx=ctx.esi=ctx.edi=ctx.ebp=0xBAD;
    return 0;
}
int main(){
    std::array<uint8_t,8192> memory{};kernel::g_base=memory.data();X86Context ctx{};
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto reset=[&]{
        ctx={};ctx.esp=0x400;ctx.ebx=11;ctx.ecx=12;ctx.esi=13;ctx.edi=14;ctx.ebp=15;
        const uint32_t args[]={0x12345678,5,3,0x500,0x600,4};
        std::memcpy(memory.data()+0x400,args,sizeof(args));
        const uint16_t indices[]={2,0,2};std::memcpy(memory.data()+0x500,indices,sizeof(indices));
        kernel::W32(0x600,111);kernel::W32(0x604,222);kernel::W32(0x608,333);
    };
    auto rejects=[&](const char* message){bool trapped=false;try{sub_0033E990(ctx,memory.data());}catch(const std::runtime_error&){trapped=true;}check(trapped,message);};
    try{
        reset();sub_0033E990(ctx,memory.data());
        check(calls==1&&allocations==1&&frees==1&&ctx.eax==0,"draw must execute and release temporary vertices");
        check(ctx.esp==0x418&&kernel::R32(0x400)==0x12345678,"five-argument stack cleanup");
        check(ctx.ebx==11&&ctx.ecx==12&&ctx.esi==13&&ctx.edi==14&&ctx.ebp==15,"register preservation");
        reset();kernel::W32(0x408,0);sub_0033E990(ctx,memory.data());
        check(ctx.esp==0x418&&calls==1&&allocations==1,"zero-count draw must not read or allocate");
        reset();kernel::W32(0x408,0x80000000);rejects("oversized draw must reject");
        reset();kernel::W32(0x410,0xFFFFFFFC);rejects("vertex address overflow must reject");
        reset();kernel::W32(0x40C,0);rejects("null index pointer must reject");
        reset();fail_allocation=true;rejects("failed allocation must reject");
        check(calls==1&&frees==1,"invalid draws must never reach backend or free unallocated memory");
        std::puts("PASS: indexed vertex order, duplicates, ABI, registers, lifetime and bounds");
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
