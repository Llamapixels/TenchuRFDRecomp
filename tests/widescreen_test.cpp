#include "cpu/x86_context.h"
#include "graphics/widescreen.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
extern "C" void sub_0009C3A0(X86Context&,uint8_t*);
extern "C" void sub_0009C090(X86Context&,uint8_t*);
static void Projection(X86Context& ctx,uint8_t* base){
    uint32_t output;std::memcpy(&output,base+ctx.esp+4,4);
    float m[16]={800,0,0,0,0,800,0,0,320,240,2,1,0,0,-10,0};
    std::memcpy(base+output,m,sizeof(m));ctx.eax=output;ctx.esp+=4;
}
extern "C" void __imp__sub_0009C3A0(X86Context& ctx,uint8_t* base){Projection(ctx,base);}
extern "C" void __imp__sub_0009C090(X86Context& ctx,uint8_t* base){
    uint32_t screen;std::memcpy(&screen,base+ctx.esp+8,4);
    Projection(ctx,base);
    float m[16]={640,0,0,0,0,480,0,0,0,0,2,1,320,240,-10,0};
    std::memcpy(base+screen,m,sizeof(m));
}
int main(){try{
    using namespace tenchu::wide;
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    std::vector<uint8_t> bytes(0x30000);auto* base=bytes.data();const uint32_t out=0x10000;
    for(auto fn:{sub_0009C3A0,sub_0009C090})for(bool wide:{false,true}){
        enabled=wide;X86Context ctx{};ctx.esp=0x20000;
        const uint32_t screen=0x11000;
        std::memcpy(base+ctx.esp+4,&out,4);std::memcpy(base+ctx.esp+8,&screen,4);fn(ctx,base);
        if(fn==sub_0009C090){
            float sm[16];std::memcpy(sm,base+screen,sizeof(sm));
            check(sm[0]==(wide?480:640),"second screen-space projection must match widened clip projection");
            check(sm[5]==480&&sm[12]==320&&sm[13]==240&&sm[10]==2&&sm[14]==-10,"second projection center/vertical/depth must remain unchanged");
        }
        float m[16];std::memcpy(m,base+out,sizeof(m));
        check(m[0]==(wide?600:800),"horizontal projection must expand by 4/3 at 16:9");
        check(m[5]==800&&m[8]==320&&m[9]==240&&m[10]==2&&m[14]==-10,"vertical FOV, center and depth must remain unchanged");
        check(ctx.eax==out&&ctx.esp==0x20004,"original return value and cdecl stack convention must remain intact");
    }
    enabled=false;check(HudScale(true,true,false)==1,"4:3 UI must be unchanged");
    enabled=true;check(HudScale(true,true,false)==.75f,"screen HUD must preserve proportions");
    check(HudScale(true,true,true)==1,"render-target composite must fill wide output");
    check(HudScale(false,true,false)==1&&HudScale(true,false,false)==1,"offscreen and 3D draws must bypass HUD correction");
    float native_width=640*.75f*(16.f/9)/(4.f/3);
    check(std::abs(native_width-640)<.01f,"HUD screen-space width must remain physically unchanged");
    for(float scale:{.5f,.75f,1.f}){
        const float active_width=640*scale;
        float left=HudPositionX(0,640,scale,.75f),right=HudPositionX(active_width,640,scale,.75f);
        check(std::abs(left-(active_width-right))<.001f,"scaled movie must have symmetric pillarboxes inside active raster");
        check(std::abs((left+right)*.5f-active_width*.5f)<.001f,"scaled movie center must match active frame center");
        check(HudPositionX(100*scale,640,scale,1)==100*scale,"uncorrected 3D/composite coordinates must remain unchanged");
    }
    puts("PASS: both guest perspective builders, wide/off aspect, focal/center/depth preservation, ABI, HUD and composite classification");
}catch(const std::exception& e){puts(e.what());return 1;}}
