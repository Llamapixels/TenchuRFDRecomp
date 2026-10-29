#include "cpu/x86_ops.h"
#include "graphics/widescreen.h"
#include <cstring>
X86_EXTERN_FUNC(__imp__sub_0009C3A0);
X86_EXTERN_FUNC(__imp__sub_0009C090);
namespace {
uint32_t Output(X86Context& ctx,uint8_t* base){uint32_t v;std::memcpy(&v,base+ctx.esp+4,4);return v;}
void Expand(uint8_t* base,uint32_t matrix){
    if(!tenchu::wide::enabled.load()||matrix<0x10000||matrix>0x03FFFFC0)return;
    float x;std::memcpy(&x,base+matrix,4);x*=tenchu::wide::kHorizontal;std::memcpy(base+matrix,&x,4);
}
}
// The engine's perspective builders bake focal X/Y plus viewport-center terms.
// Reduce focal X only; leave vertical FOV, center, depth and viewport matrices.
X86_FUNC(sub_0009C3A0){auto out=Output(ctx,base);__imp__sub_0009C3A0(ctx,base);Expand(base,out);}
// 9C090 writes BOTH clip-space and screen-space projection matrices.
// Native camera 9C4F0 consumes both; keep their horizontal scales consistent.
X86_FUNC(sub_0009C090){
    auto out=Output(ctx,base);uint32_t screen;std::memcpy(&screen,base+ctx.esp+8,4);
    __imp__sub_0009C090(ctx,base);Expand(base,out);
    if(screen!=out)Expand(base,screen);
}
