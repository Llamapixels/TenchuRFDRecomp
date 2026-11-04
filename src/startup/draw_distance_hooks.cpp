#include "cpu/x86_ops.h"
#include "kernel/memory.h"
#include "game_camera.h"
#include <cmath>
#include <cstring>

X86_EXTERN_FUNC(__imp__sub_002387B0);
X86_EXTERN_FUNC(__imp__sub_002396E0);
namespace {
thread_local float object_scale=1.f;
float Scale(float value){return std::isfinite(value)?std::clamp(value,.25f,4.f):1.f;}
template<class T>T Read(uint8_t* base,uint32_t at){T v;std::memcpy(&v,base+at,sizeof(v));return v;}
bool Gameplay(uint8_t* base){return Read<uint16_t>(base,0x008E38BC)==3;}
struct Scope {
    float previous=object_scale;
    explicit Scope(float scale){object_scale=scale;}
    ~Scope(){object_scale=previous;}
};
float CharacterScale(X86Context& ctx,uint8_t* base){
    // Guest stacks are in D0000000..DFFFFFFF, outside the 64 MiB actor
    // image. A physical-image bound here silently disabled the NPC slider.
    if(!Gameplay(base)||ctx.esp<mem::kStackBase||
        uint64_t(ctx.esp)+8>uint64_t(mem::kStackBase)+mem::kStackSize)return 1;
    const uint32_t object=Read<uint32_t>(base,ctx.esp+4);
    for(uint32_t slot=0;slot<2;++slot){
        const uint32_t player=Read<uint32_t>(base,0x01BAB110+slot*4);
        if(player>=0x10000&&player<=0x03FFF800&&object==player+0xA0)return 1;
    }
    return Scale(tenchu::camera::enemy_draw_distance.load());
}
}
// The map chunk culler (440A0) selects 5000, a mission-specific value, or
// its normal limit. Scale the loaded limit immediately BEFORE comparison.
// Do not change the shared guest constants, transforms, near/side planes,
// portal/room checks, fog, or gameplay distance queries.
void TenchuWorldDrawLimit(X86Context& ctx,uint8_t* base){
    if(Gameplay(base))ctx.xmm[0].f[0]*=Scale(tenchu::camera::world_draw_distance.load());
}
// 4FF10's separate 5000-unit box-depth test is shared by several systems.
// Apply the slider only inside the scene's actual map/character painters.
void TenchuObjectDrawLimit(X86Context& ctx,uint8_t*){ctx.xmm[0].f[0]*=object_scale;}
X86_FUNC(sub_002387B0){
    Scope scope(Gameplay(base)?Scale(tenchu::camera::world_draw_distance.load()):1.f);
    __imp__sub_002387B0(ctx,base);
}
X86_FUNC(sub_002396E0){
    // Native new-model character painter. Both actual player actor objects
    // stay visible, including characters substituted through memory patches.
    Scope scope(CharacterScale(ctx,base));
    __imp__sub_002396E0(ctx,base);
}
