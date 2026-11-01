#include "cpu/x86_ops.h"
#include "core/log.h"
#include "cloth_body.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <array>
#include <unordered_map>

X86_EXTERN_FUNC(__imp__sub_00051A60);
X86_EXTERN_FUNC(__imp__sub_00055550);
X86_EXTERN_FUNC(__imp__sub_00051250);
X86_EXTERN_FUNC(__imp__sub_00053640);
X86_EXTERN_FUNC(__imp__sub_00052330);
X86_EXTERN_FUNC(__imp__sub_000490B0);
X86_EXTERN_FUNC(__imp__sub_0004FDD0);
namespace {
template<class T>T Read(uint8_t* base,uint32_t at){T v;std::memcpy(&v,base+at,sizeof(v));return v;}
bool Probe(){static bool on=std::getenv("TENCHU_CLOTH_PROBE")!=nullptr;return on;}
struct Counts {unsigned update=0,draw=0;};
thread_local std::unordered_map<uint32_t,Counts> counts;
thread_local const tenchu::cloth::Body* active_body=nullptr;
thread_local std::unordered_map<uint32_t,tenchu::cloth::RestShape> rest_shapes;
}
// Character instances clone their cloth buffers without calling 00051250.
// Carry authored rest shapes over at that exact lifecycle boundary.
X86_FUNC(sub_0004FDD0){
    const auto destination=Read<uint32_t>(base,ctx.esp+4),source=Read<uint32_t>(base,ctx.esp+8);
    __imp__sub_0004FDD0(ctx,base);
    if(!tenchu::cloth::GuestRange(destination,0x200)||!tenchu::cloth::GuestRange(source,0x200))return;
    const unsigned count=base[destination+0x94];if(count>80)return;
    for(unsigned i=0;i<count;i++){
        const auto to=Read<uint32_t>(base,destination+0x98+i*4),from=Read<uint32_t>(base,source+0x98+i*4);
        auto found=rest_shapes.find(from);
        if(found!=rest_shapes.end())rest_shapes[to]=found->second;
        else rest_shapes[to]=tenchu::cloth::CaptureRest(base,to);
    }
}
// Scope animated body colliders to the original owner update. Rebuild for
// every call, so reused model pools and forced character changes cannot leave
// another actor's body attached to a panel. Stage cloth has no owner scope.
X86_FUNC(sub_000490B0){
    const auto body=tenchu::cloth::enhanced.load()&&tenchu::cloth::body_collision.load()
        ?tenchu::cloth::MakeBody(base,ctx.ecx):tenchu::cloth::Body{};
    if(Probe()&&counts[ctx.ecx].update++%120==0)
        LOGR("CLOTH body owner %08X capsules %u speed %.2f size %.2f",ctx.ecx,body.count,tenchu::cloth::response_speed.load(),tenchu::cloth::body_size.load());
    struct Scope {
        const tenchu::cloth::Body* previous=active_body;
        explicit Scope(const tenchu::cloth::Body* body){active_body=body;}
        ~Scope(){active_body=previous;}
    } scope(&body);
    __imp__sub_000490B0(ctx,base);
}
// Retain the original loader and draw calls.
// Enable for a reproduction run with TENCHU_CLOTH_PROBE=1.
X86_FUNC(sub_00051250){
    const auto cloth=ctx.ecx;
    __imp__sub_00051250(ctx,base);
    rest_shapes[cloth]=tenchu::cloth::CaptureRest(base,cloth);
    if(Probe()){
        const auto n=Read<unsigned>(base,cloth+0x3200);
        unsigned pinned=0;
        if(n<=128)for(unsigned i=0;i<n;i++)pinned+=base[cloth+0xC3C+i*64]!=0;
        counts[cloth]={};
        LOGR("CLOTH load object %08X points %u pinned %u grid %ux%u bytes %u",cloth,n,pinned,unsigned(base[cloth+0x3222]),unsigned(base[cloth+0x3223]),ctx.eax);
    }
}
X86_FUNC(sub_00051A60){
    const auto cloth=ctx.ecx;
    const unsigned n=Probe()?Read<unsigned>(base,cloth+0x3200):0;
    const bool sample=Probe()&&n>0&&n<=128&&counts[cloth].update++%120==0;
    std::array<float,128*6> before{};
    if(sample)std::memcpy(before.data(),base+cloth,n*24);
    auto rest=rest_shapes.find(cloth);
    if(tenchu::cloth::enhanced.load()&&tenchu::cloth::Advance(base,cloth,active_body,rest==rest_shapes.end()?nullptr:&rest->second)){
        // Rebuild lighting normals with the game's own routine. Its void
        // thiscall has the same stack cleanup as the original solver.
        __imp__sub_00052330(ctx,base);
    }else __imp__sub_00051A60(ctx,base);
    if(sample){
        float movement=0;unsigned invalid=0;
        for(unsigned i=0;i<n;i++)for(unsigned j=0;j<3;j++){
            const float value=Read<float>(base,cloth+i*24+j*4);
            if(!std::isfinite(value))++invalid;
            else movement=std::fmax(movement,std::fabs(value-before[i*6+j]));
        }
        LOGR("CLOTH simulation object %08X points %u max movement %.6f invalid %u force %.6f %.6f %.6f",cloth,n,movement,invalid,Read<float>(base,cloth+0x3230),Read<float>(base,cloth+0x3234),Read<float>(base,cloth+0x3238));
    }
}
X86_FUNC(sub_00055550){
    static thread_local unsigned calls=0;
    if(Probe()&&calls++%120==0)LOGR("CLOTH stage draw count %u flags %08X",Read<unsigned>(base,0x7A528C),Read<unsigned>(base,0x18FE6CC));
    __imp__sub_00055550(ctx,base);
}
X86_FUNC(sub_00053640){
    const auto cloth=ctx.ecx;
    if(Probe()&&counts[cloth].draw++%120==0)LOGR("CLOTH mesh draw object %08X points %u triangles %u",cloth,Read<unsigned>(base,cloth+0x3200),Read<unsigned>(base,cloth+0x3204));
    __imp__sub_00053640(ctx,base);
}
