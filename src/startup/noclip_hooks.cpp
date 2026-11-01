#include "cpu/x86_ops.h"
#include "game_noclip.h"
#include "flight_input.h"
#include "kernel/memory.h"
#include "core/log.h"
#include <chrono>
#include <cstring>

X86_EXTERN_FUNC(__imp__sub_0029D830);
X86_EXTERN_FUNC(__imp__sub_002059C0);
namespace {
using namespace tenchu;
using camera::Vec;
template<class T>T Read(uint8_t* base,uint32_t at){T v;std::memcpy(&v,base+at,sizeof(v));return v;}
template<class T>void Write(uint8_t* base,uint32_t at,T v){std::memcpy(base+at,&v,sizeof(v));}
bool Valid(uint32_t p,uint32_t n){return p>=0x10000&&uint64_t(p)+n<=0x04000000;}
uint32_t ActorArgument(const X86Context& ctx,uint8_t* base){
    // Guest stacks are in D0000000-DFFFFFFF, separate from low actor memory.
    if(ctx.esp<mem::kStackBase||uint64_t(ctx.esp)+8>uint64_t(mem::kStackBase)+mem::kStackSize)return 0;
    return Read<uint32_t>(base,ctx.esp+4);
}
bool Player(uint8_t* base,uint32_t actor){
    return Valid(actor,0x7D8)&&actor==Read<uint32_t>(base,0x1BAB110)&&
        Read<uint32_t>(base,actor+0x1B)==0x59414C50&&base[actor+0x393]==0;
}
struct Session {
    uint32_t actor=0,bits31c=0,bits4cc=0;
    camera::FreeCamera flight;
    Vec target_offset{};float distance=300;
    std::chrono::steady_clock::time_point last{};
    POINT cursor{};bool cursor_valid=false;
};
thread_local Session session;
void Stop(uint8_t* base){
    if(session.actor)LOGR("No clip flight: released player %08X",session.actor);
    if(session.actor&&Player(base,session.actor)){
        auto p=session.actor;
        Write(base,p+0x31C,(Read<uint32_t>(base,p+0x31C)&~0x800u)|session.bits31c);
        Write(base,p+0x4CC,(Read<uint32_t>(base,p+0x4CC)&~0x40u)|session.bits4cc);
        Write(base,p+0x278,Vec{});
    }
    session={};noclip::active=false;noclip::PublishView(0,{},{});
}
void Pose(uint8_t* base){
    const uint32_t p=session.actor;
    const Vec at=Read<Vec>(base,p+0x20),desired=session.flight.eye;
    const Vec delta=desired-at;
    // Preserve each model/head offset while moving every position cache.
    for(uint32_t off:{0x20u,0x110u,0x130u,0x1F0u,0x258u,0x268u,0x2A8u}){
        Vec value=Read<Vec>(base,p+off);
        if(camera::Finite(value))Write(base,p+off,value+delta);
    }
    // These two are the movement solver's bases, and can contain animation or
    // gravity displacement even when canonical +20 was already corrected.
    Write(base,p+0x20,desired);Write(base,p+0x110,desired);Write(base,p+0x258,desired);
    Write(base,p+0x268,desired);Write(base,p+0x278,Vec{});
    Write(base,p+0x31C,Read<uint32_t>(base,p+0x31C)|0x800u);
    Write(base,p+0x4CC,Read<uint32_t>(base,p+0x4CC)|0x40u);
}
bool Begin(uint8_t* base,uint32_t p){
    if(!Player(base,p)){
        if(session.actor&&!Player(base,session.actor))Stop(base);
        return false;
    }
    if(!noclip::requested.load()||Read<uint16_t>(base,0x8E38BC)!=3){
        if(session.actor)Stop(base);
        return false;
    }
    const auto now=std::chrono::steady_clock::now();
    if(session.actor!=p){
        Stop(base);
        const Vec position=Read<Vec>(base,p+0x20);
        const uint32_t cam=Read<uint32_t>(base,0x8E38C0);
        if(!camera::Finite(position)||!Valid(cam,0x60))return false;
        const Vec eye=Read<Vec>(base,cam),target=Read<Vec>(base,cam+0x40);
        if(!camera::Finite(eye)||!camera::Finite(target)||camera::Length(target-eye)<.01f)return false;
        session.actor=p;session.bits31c=Read<uint32_t>(base,p+0x31C)&0x800u;
        session.bits4cc=Read<uint32_t>(base,p+0x4CC)&0x40u;
        session.flight.Initialize(position,position+(target-eye));
        session.target_offset=target-position;session.distance=camera::Length(target-eye);
    }
    float dt=session.last.time_since_epoch().count()?std::chrono::duration<float>(now-session.last).count():1.f/60;
    session.last=now;
    input::Poll(0);
    input::Pad pad;{std::lock_guard lock(camera::pad_lock);pad=camera::raw_pad;}
    camera::Motion motion;
    const bool moving=!camera::menu_open.load()&&!camera::free_requested.load()&&
        !(Read<uint32_t>(base,0x18FE6CC)&0x80000000u);
    if(moving)motion=camera::PadFlightMotion(pad);
    camera::KeyboardFlightMotion(motion,moving,session.cursor,session.cursor_valid);
    session.flight.Step(motion,dt,camera::horizontal.load(),camera::vertical.load(),camera::invert_x.load(),camera::invert_y.load());
    const Vec direction{std::sin(session.flight.yaw)*std::cos(session.flight.pitch),-std::sin(session.flight.pitch),std::cos(session.flight.yaw)*std::cos(session.flight.pitch)};
    const Vec target=session.flight.eye+session.target_offset;
    noclip::PublishView(p,target-direction*session.distance,target);
    noclip::active=true;Pose(base);return true;
}
}
// Existing named-function override mechanism. The unmodified retail update
// still runs; the wrapper pins controlled position before and after physics.
X86_FUNC(sub_0029D830){
    const uint32_t p=ActorArgument(ctx,base);
    const bool was_active=tenchu::noclip::active.load();
    const bool flight=Begin(base,p);
    if(flight&&!was_active)LOGR("No clip flight: acquired player %08X on guest stack %08X",p,ctx.esp);
    __imp__sub_0029D830(ctx,base);
    if(flight&&Player(base,p)&&session.actor==p)Pose(base);
}
// This point runs AFTER animation/gravity callbacks and BEFORE world collision.
// Reassert the bypass and zero movement here, every frame, instead of racing
// physics from the periodic text writer. Other actors take the original path.
X86_FUNC(sub_002059C0){
    const uint32_t p=ActorArgument(ctx,base);
    if(noclip::active.load()&&session.actor==p&&Player(base,p))Pose(base);
    __imp__sub_002059C0(ctx,base);
}
