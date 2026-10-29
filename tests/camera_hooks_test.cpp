#include "cpu/x86_context.h"
#include "game_camera.h"
#include "core/log.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
void TenchuCameraBegin(X86Context&,uint8_t*);
void TenchuCameraEnd(X86Context&,uint8_t*);
void TenchuCaptureCameraFrame(int){}
namespace logx {void Write(Level,const char*,...){} }
input::Pad input::Poll(int port){input::Pad p;p.connected=true;tenchu::camera::FilterPad(port,p);return p;}
static unsigned view_rebuilds=0,frustum_rebuilds=0;
static uint32_t rebuilt_camera=0;
extern "C" void sub_0009CD40(X86Context& ctx,uint8_t* base){
    ++view_rebuilds;std::memcpy(&rebuilt_camera,base+ctx.esp+4,4);ctx.eax=0xBAD;ctx.cf=1;
}
extern "C" void sub_0009C4F0(X86Context& ctx,uint8_t*){++frustum_rebuilds;ctx.edx=0xBAD;ctx.zf=1;}
int main(){try{
    using namespace tenchu::camera;
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::vector<uint8_t> memory(0x04000000);uint8_t* base=memory.data();X86Context ctx{};ctx.esp=0x3F00000;
    auto write=[&](uint32_t at,auto value){std::memcpy(base+at,&value,sizeof(value));};
    auto read=[&](uint32_t at){float value;std::memcpy(&value,base+at,4);return value;};
    uint32_t owner=0x100000,player=0x200000,cam=0x300000,proj=0x400000;
    write(0x1BAB0F0,owner);write(0x1BAB110,player);write(0x8E38C0,cam);
    write(owner+0x25C,cam);write(owner+0x260,proj);write(cam,Vec{0,0,-100});write(cam+0x40,Vec{0,0,0});write(proj,400.f);write(cam+0x50,400.f);
    auto initial=memory;
    TenchuCameraBegin(ctx,base);TenchuCameraEnd(ctx,base);
    check(available&&initial==memory,"defaults must leave guest memory unchanged");
    distance=2;custom_fov=true;fov=90;
    TenchuCameraBegin(ctx,base);
    check(std::abs(read(cam+8)+200)<.01f,"distance must move eye around target");
    check(std::abs(read(proj)-240)<.01f,"90 degree vertical FOV must set correct focal distance");
    check(std::abs(read(cam+0x50)-240)<.01f,"actual gameplay frustum reads camera+50");
    check(view_rebuilds==1&&frustum_rebuilds==1&&rebuilt_camera==cam,"follow override must rebuild native view and frustum caches");
    check(ctx.eax==0&&ctx.edx==0&&ctx.cf==0&&ctx.zf==0&&ctx.esp==0x3F00000,"cache rebuild must preserve guest CPU context");
    // Scratch stack bytes are not part of the camera-state restoration contract.
    std::memcpy(initial.data()+ctx.esp-8,base+ctx.esp-8,8);
    TenchuCameraEnd(ctx,base);check(initial==memory,"camera scope must restore every touched byte");
    TenchuCameraBegin(ctx,base);
    write(cam+0x20,Vec{1,0,0});write(cam+0x30,Vec{0,1,0}); // real view builder output
    TenchuCameraEnd(ctx,base);
    check(read(cam+0x20)==1&&read(cam+0x34)==1,"restore must retain derived view basis for scene drawing");
    write(cam+0x20,Vec{});write(cam+0x30,Vec{});
    // FOV alone keeps the pose and adds no rebuild: the scene's own builders follow the hook
    distance=1;auto rebuilds=view_rebuilds;
    TenchuCameraBegin(ctx,base);
    check(std::abs(read(cam+0x50)-240)<.01f&&std::abs(read(cam+8)+100)<.01f&&view_rebuilds==rebuilds,"a FOV-only change must keep the pose and skip the extra rebuild");
    TenchuCameraEnd(ctx,base);check(std::abs(read(cam+0x50)-400)<.01f&&std::abs(read(proj)-400)<.01f,"FOV-only scope must restore focal lengths");
    // mission 1 intro: an event camera moves the shared player camera inside the scope
    distance=2;TenchuCameraBegin(ctx,base);write(cam,Vec{0,50,-120});write(cam+0x40,Vec{5,0,0});TenchuCameraEnd(ctx,base);
    check(read(cam+4)==50&&read(cam+0x40)==5,"a camera the game moved itself must keep the game's pose");
    check(std::abs(read(cam+0x50)-400)<.01f&&std::abs(read(proj)-400)<.01f,"focal lengths the game left alone are still restored");
    TenchuCameraBegin(ctx,base);
    check(read(cam+4)==50&&std::abs(read(cam+0x50)-400)<.001f&&std::abs(read(proj)-400)<.001f,"overrides must pause while the game drives the camera");
    write(cam,Vec{0,60,-120});TenchuCameraEnd(ctx,base);  // the scene is still moving it
    check(read(cam+4)==60&&read(cam+0x50)==400.f&&read(proj)==400.f,"a paused scope keeps the game's pose and puts back the focal lengths");
    // a cutscene that sets its own focal lengths each frame must never show the custom FOV, not even for a frame
    for(int i=0;i<90;++i){
        TenchuCameraBegin(ctx,base);
        check(std::abs(read(cam+0x50)-400)<.001f&&std::abs(read(proj)-400)<.001f,"the custom FOV must stay off for the whole scripted scene");
        write(cam+0x50,400.f);write(proj,400.f);TenchuCameraEnd(ctx,base);
    }
    for(int i=0;i<31;++i){TenchuCameraBegin(ctx,base);TenchuCameraEnd(ctx,base);}
    check(read(cam+0x50)==400.f&&read(proj)==400.f,"focal probes must leave the original values behind");
    TenchuCameraBegin(ctx,base);check(std::abs(read(cam+0x50)-240)<.01f,"overrides must resume once the game stops moving the camera");TenchuCameraEnd(ctx,base);
    write(cam,Vec{0,0,-100});write(cam+0x40,Vec{0,0,0});
    auto previous_rebuilds=view_rebuilds;
    free_requested=true;TenchuCameraBegin(ctx,base);check(free_active,"free camera must acquire normal gameplay");TenchuCameraEnd(ctx,base);
    check(view_rebuilds==previous_rebuilds,"confirmed free-camera path must remain unchanged");
    input::Pad p;p.connected=true;p.lx=20000;p.analog[0]=255;p.buttons=16;FilterPad(0,p);
    check(p.lx==0&&p.analog[0]==0&&p.buttons==0&&p.connected,"photo mode must withhold movement/action while preserving connection");
    write(owner+0x20C,0x31);initial=memory;TenchuCameraBegin(ctx,base);TenchuCameraEnd(ctx,base);
    check(!available&&!free_active&&initial==memory,"scripted camera must bypass overrides");
    Reset();write(owner+0x20C,0);TenchuCameraBegin(ctx,base);TenchuCameraEnd(ctx,base);
    check(!free_requested&&distance==1&&!custom_fov&&!follow_control,"reset must disable free camera, the custom right-stick camera and restore defaults");
    FreeCamera a,b;a.Initialize({0,0,0},{0,0,100});b=a;Motion m;m.forward=1;m.look_x=.5f;
    for(int i=0;i<60;++i)a.Step(m,1.f/60,1,1,false,false);
    for(int i=0;i<120;++i)b.Step(m,1.f/120,1,1,false,false);
    check(Length(a.eye-b.eye)<5,"movement must be independent of frame rate");
    FreeCamera inverted;inverted.Initialize({0,0,0},{0,0,100});inverted.Step(m,.02f,1,1,true,false);check(inverted.yaw<0,"inversion must reverse look");
    follow_control=false;p={};p.rx=-32768;FilterPad(0,p);check(p.rx==-32768,"native follow input must preserve exact stick values");
    follow_control=true;p.rx=10000;FilterPad(0,p);check(p.rx==0&&raw_pad.rx==10000,"follow overlay must retain raw look and suppress native quantized look");
    FollowCamera slow,fast;slow.Step(1,.5f,.02f,1,1,false,false);fast.Step(1,.5f,.02f,2,3,false,false);
    check(std::abs(fast.yaw-2*slow.yaw)<.0001f&&std::abs(fast.pitch-3*slow.pitch)<.0001f,"full-tilt angular speeds must scale without input saturation");
    link_profiles=true;horizontal=2;vertical=.5f;distance=1.5f;fov=75;custom_fov=true;SyncLinked();
    check(running_horizontal==2&&running_vertical==.5f&&running_distance==1.5f&&running_fov==75&&running_custom_fov,"linked profiles must copy all main slider values");
    running_horizontal=3;running_distance=2;SyncLinked(true);
    check(horizontal==3&&distance==2,"editing linked running sliders must update main values");
    link_profiles=false;Reset();
    write(owner+0x20C,14);distance=2;TenchuCameraBegin(ctx,base);
    check(available&&std::abs(read(cam+8)+200)<.01f,"nonzero gameplay camera modes must support controls");TenchuCameraEnd(ctx,base);
    write(0x1BAB0F0,0xFFFFFFF0u);TenchuCameraBegin(ctx,base);check(!available,"invalid camera pointer must be rejected");
    puts("PASS: camera scope/restore, FOV, distance, free movement, frame rate, input capture, inversion, scripted bypass, reset and bounds");
}catch(const std::exception& e){puts(e.what());return 1;}}
