#include "xdk/hle.h"
#include "core/log.h"
#include <array>
#include <bit>
#include <limits>
#include <cstdio>
#include <stdexcept>
X86_EXTERN_FUNC(sub_00340C20);
namespace kernel { uint8_t* g_base=nullptr; }
namespace logx { void Write(Level,const char*,...){} }
[[noreturn]] void x86_trap(X86Context&,uint32_t,const char* message){throw std::runtime_error(message);}
static unsigned calls=0;static float last_x=0,last_y=0;
uint32_t TenchuSetBackbufferScale(float x,float y){++calls;last_x=x;last_y=y;return 0;}
int main(){
    std::array<uint8_t,4096> memory{};kernel::g_base=memory.data();X86Context ctx{};
    auto reset=[&](float x,float y){ctx={};ctx.esp=0x400;ctx.ebx=13;kernel::W32(0x400,0x1234);kernel::W32(0x404,std::bit_cast<uint32_t>(x));kernel::W32(0x408,std::bit_cast<uint32_t>(y));};
    try{
        for(auto scale: {std::array<float,2>{.75f,.75f},{.5f,1},{1,.75f},{.25f,.125f}}){
            reset(scale[0],scale[1]);sub_00340C20(ctx,memory.data());
            if(last_x!=scale[0]||last_y!=scale[1]||ctx.esp!=0x40C||ctx.eax!=0||ctx.ebx!=13||kernel::R32(0x400)!=0x1234)throw std::runtime_error("scale forwarding or ABI mismatch");
        }
        const unsigned before=calls;
        for(float invalid:{0.f,-1.f,1.5f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
            reset(invalid,.75f);bool trapped=false;try{sub_00340C20(ctx,memory.data());}catch(const std::runtime_error&){trapped=true;}
            if(!trapped||calls!=before)throw std::runtime_error("invalid scale reached backend");
        }
        std::puts("PASS: 75% and mixed subsampling scales, ABI, and invalid-value guards");
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
