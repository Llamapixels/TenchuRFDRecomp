#include "kernel/exports.h"
#include "cpu/x86_ops.h"
#include "core/log.h"
#include <array>
#include <map>
#include <stdexcept>
#include <cstdio>
#include <vector>

namespace {
auto& Exports(){static std::map<uint32_t,kernel::KernelFn> entries;return entries;}
std::vector<uint32_t> completion_args;
}
namespace kernel {
uint8_t* g_base=nullptr;
void RegisterExport(uint32_t ordinal,const char*,KernelFn fn){Exports()[ordinal]=fn;}
uint32_t CallGuest(X86Context&,uint32_t fn,const uint32_t* args,size_t count){
    if(fn==0xFE000000u+301*4){
        if(count!=1)throw std::runtime_error("status conversion argument mismatch");
        return args[0]==0?0:args[0]==0xC0000011u?38:317;
    }
    if(fn!=0x2FAD60 || count!=3)throw std::runtime_error("completion signature mismatch");
    completion_args.assign(args,args+count);return 0;
}
}
namespace dispatch {uint32_t KernelThunkAddress(uint32_t ordinal){return 0xFE000000u+ordinal*4;}}
namespace logx {void Write(Level,const char*,...) {}}
[[noreturn]] void x86_trap(X86Context&,uint32_t,const char* message){throw std::runtime_error(message);}

int main(){
    std::array<uint8_t,4096> memory{};kernel::g_base=memory.data();
    X86Context ctx{};
    auto setup=[&](uint32_t status,uint32_t bytes){
        ctx={};ctx.esp=0x400;ctx.ecx=0xABCDEF01;completion_args.clear();
        kernel::W32(0x400,0xFFFFFFF0);kernel::W32(0x404,0x2FAD60);
        kernel::W32(0x408,0x500);kernel::W32(0x40C,0);
        kernel::W32(0x500,status);kernel::W32(0x504,bytes);
    };
    auto check=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    try{
        check(Exports().count(232)==1,"kernel ordinal 232 must be registered");
        setup(0,4096);Exports().at(232)(ctx,memory.data());
        check(completion_args==std::vector<uint32_t>({0,4096,0x500}),"success must preserve bytes and OVERLAPPED identity");
        check(ctx.esp==0x410 && ctx.ecx==0xABCDEF01,"dispatcher must pop three arguments without corrupting context");
        setup(0xC0000011,0);Exports().at(232)(ctx,memory.data());
        check(completion_args==std::vector<uint32_t>({38,0,0x500}),"EOF must use Win32 error conversion");
        setup(0xDEADBEEF,7);Exports().at(232)(ctx,memory.data());
        check(completion_args==std::vector<uint32_t>({317,7,0x500}),"unknown status must use existing converter");
        setup(0,1);kernel::W32(0x408,0);bool trapped=false;
        try{Exports().at(232)(ctx,memory.data());}catch(const std::runtime_error&){trapped=true;}
        check(trapped && completion_args.empty(),"null status block must fail explicitly");
        std::puts("PASS: I/O APC registration, callback ABI, status conversion and request identity");
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
    return 0;
}
