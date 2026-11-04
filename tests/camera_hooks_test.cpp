#include "cpu/x86_context.h"
#include "game_camera.h"
#include "port/online.h"
#include "core/log.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <chrono>
void TenchuCameraBegin(X86Context&,uint8_t*);
void TenchuCameraEnd(X86Context&,uint8_t*);
void TenchuCameraRenderBegin(X86Context&,uint8_t*);
void TenchuCameraRenderEnd(X86Context&,uint8_t*);
void TenchuCaptureCameraFrame(int){}
namespace logx {void Write(Level,const char*,...){} }
static input::Pad test_pad;
input::Pad input::Poll(int port){input::Pad p=test_pad;p.connected=true;tenchu::camera::FilterPad(port,p);return p;}
static unsigned view_rebuilds=0,frustum_rebuilds=0,network_rebuilds=0;
static uint32_t rebuilt_camera=0;
extern "C" void sub_0009CD40(X86Context& ctx,uint8_t* base){
    ++view_rebuilds;std::memcpy(&rebuilt_camera,base+ctx.esp+4,4);ctx.eax=0xBAD;ctx.cf=1;
}
extern "C" void sub_0009C4F0(X86Context& ctx,uint8_t*){++frustum_rebuilds;ctx.edx=0xBAD;ctx.zf=1;}
extern "C" void sub_0009C980(X86Context& ctx,uint8_t* base){
    ++network_rebuilds;uint32_t cam=0;std::memcpy(&cam,base+ctx.esp+4,4);
    // Model the distinct P2 world-view cache, which the sky-only regression
    // missed by rebuilding the main camera's cache instead.
    std::memcpy(base+0x8E38F0,base+cam,12);ctx.edx=0xBAD;ctx.zf=1;
}
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
    // Match the live P2 snapshot: 018FC548 is rendered; 008E38C0 is the other
    // camera. Camera task arguments and rig objects are different structures.
    uint32_t guest=0x110000,guest_cam=0x310000,guest_proj=0x410000;
    write(guest+0x20,5840.f);write(guest+0x25C,guest_cam);write(guest+0x260,guest_proj);
    write(guest_cam,Vec{0,0,-100});write(guest_cam+0x40,Vec{});write(guest_cam+0x50,400.f);write(guest_proj,400.f);
    write(0x1BAB0F4,guest);write(0x1BAB114,player);write(0x18FC548,guest_cam);
    write(0x5A97D0,1u);write(0x5A97CC,1u);write(0x8E38BC,uint16_t(3));
    ctx.ebp=0x3E00000;write(ctx.ebp+8,0x120000u);write(0x120020,player);
    tenchu::online::enabled=true;distance=2;custom_fov=true;fov=90;
    TenchuCameraBegin(ctx,base);
    check(read(guest_cam+8)==-100&&read(guest_proj)==400,"P2 task hook waits for playback to finish");
    TenchuCameraRenderBegin(ctx,base);
    check(available&&tenchu::controls::GameplayActive()&&std::abs(read(guest_cam+8)+200)<.01f&&std::abs(read(guest_proj)-240)<.01f,"P2 online rig receives modern camera and mouse gameplay gate");
    check(read(cam+8)==-100&&read(cam+0x50)==400,"P2 camera leaves P1 rig alone");
    check(network_rebuilds==1&&frustum_rebuilds==0&&read(0x8E38F8)==-200,"P2 must rebuild its world-view cache, not only the sky basis or P1 view");
    TenchuCameraRenderEnd(ctx,base);
    check(read(guest_cam+8)==-100&&read(guest_proj)==400,"P2 render restores native network pose");
    // Playback overwrites the camera again; local input must still work with
    // no camera-update task tick (and hence no new server data) this frame.
    distance=1;test_pad.ry=25000;
    write(guest_cam,Vec{0,0,-100});std::this_thread::sleep_for(std::chrono::milliseconds(10));
    TenchuCameraRenderBegin(ctx,base);
    check(std::abs(read(guest_cam+4))>.01f&&rebuilt_camera==guest_cam,"P2 vertical orbit updates the rendered camera independently of network ticks");
    TenchuCameraRenderEnd(ctx,base);test_pad={};Reset();
    custom_fov=true;fov=90;auto before_fov=view_rebuilds;
    TenchuCameraRenderBegin(ctx,base);
    check(read(guest_proj)==240&&view_rebuilds==before_fov+1,"P2 FOV-only render must rebuild derived view/frustum caches");
    TenchuCameraRenderEnd(ctx,base);
    write(0x5A97B8,1u);before_fov=view_rebuilds;TenchuCameraRenderBegin(ctx,base);
    check(view_rebuilds==before_fov&&read(cam+0x50)==400,"Host does not use the P2 render override");
    ctx.ebp=0;tenchu::online::enabled=false;Reset();
    memory=initial;base=memory.data();view_rebuilds=frustum_rebuilds=0;
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
    // The retail frustum builder (0009C8BC) mirrors camera+50 into the
    // projection focal. Those values can differ after an event-camera handoff.
    // Reproduce the one-ULP mismatch seen in the user's live capture.
    write(proj,std::nextafter(400.f,0.f));
    for(int i=0;i<31;++i){
        TenchuCameraBegin(ctx,base);
        write(proj,read(cam+0x50)); // the actual downstream retail copy
        TenchuCameraEnd(ctx,base);
    }
    check(read(cam+0x50)==400.f&&read(proj)==std::nextafter(400.f,0.f),"focal probes must leave the original values behind despite the retail focal copy");
    TenchuCameraBegin(ctx,base);check(std::abs(read(cam+0x50)-240)<.01f,"overrides must resume once the game stops moving the camera");TenchuCameraEnd(ctx,base);
    write(0x8E38BC,uint16_t(3));test_pad.ry=25000;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));TenchuCameraBegin(ctx,base);
    check(tenchu::controls::GameplayActive()&&orbit_active,"a completed cutscene must re-enable the gameplay/mouse gate and orbit");
    check(std::abs(read(cam+4)-60)>.01f,"vertical stick must change the actual pose after an event-camera handoff");
    check(std::abs(read(cam+0x50)-240)<.01f,"custom FOV must apply alongside vertical orbit after the handoff");
    TenchuCameraEnd(ctx,base);test_pad={};Reset();
    write(cam,Vec{0,0,-100});write(cam+0x40,Vec{0,0,0});
    auto previous_rebuilds=view_rebuilds;
    free_requested=true;TenchuCameraBegin(ctx,base);check(free_active,"free camera must acquire normal gameplay");TenchuCameraEnd(ctx,base);
    check(view_rebuilds==previous_rebuilds,"confirmed free-camera path must remain unchanged");
    input::Pad p;p.connected=true;p.lx=20000;p.analog[0]=255;p.buttons=16;FilterPad(0,p);
    check(p.lx==0&&p.analog[0]==0&&p.buttons==0&&p.connected,"photo mode must withhold movement/action while preserving connection");
    write(owner+0x20C,0x31);initial=memory;TenchuCameraBegin(ctx,base);TenchuCameraEnd(ctx,base);
    check(!available&&!free_active&&initial==memory,"scripted camera must bypass overrides");
    Reset();write(owner+0x20C,0);TenchuCameraBegin(ctx,base);TenchuCameraEnd(ctx,base);
    check(!free_requested&&distance==1&&!custom_fov&&follow_control,"reset must disable free camera and enable modern gameplay camera");
    FreeCamera a,b;a.Initialize({0,0,0},{0,0,100});b=a;Motion m;m.forward=1;m.look_x=.5f;
    for(int i=0;i<60;++i)a.Step(m,1.f/60,1,1,false,false);
    for(int i=0;i<120;++i)b.Step(m,1.f/120,1,1,false,false);
    check(Length(a.eye-b.eye)<5,"movement must be independent of frame rate");
    FreeCamera inverted;inverted.Initialize({0,0,0},{0,0,100});inverted.Step(m,.02f,1,1,true,false);check(inverted.yaw<0,"inversion must reverse look");
    follow_control=false;p={};p.rx=-32768;FilterPad(0,p);check(p.rx==-32768,"native follow input must preserve exact stick values");
    follow_control=true;orbit_active=true;p.rx=10000;FilterPad(0,p);check(p.rx==0&&raw_pad.rx==10000,"follow overlay must retain raw look and suppress native quantized look");
    p.analog[5]=255;p.rx=12345;FilterPad(0,p);check(p.rx==12345,"first person must retain native input");p.analog[5]=0;
    p.analog[7]=255;p.rx=12345;FilterPad(0,p);check(p.rx==12345,"lock-on must retain native input");
    FollowCamera slow,fast;slow.Step(1,.5f,.02f,1,1,false,false);fast.Step(1,.5f,.02f,2,3,false,false);
    check(std::abs(fast.yaw-2*slow.yaw)<.0001f&&std::abs(fast.pitch-3*slow.pitch)<.0001f,"full-tilt angular speeds must scale without input saturation");
    FollowCamera orbit;orbit.Initialize({0,-50,-100},{0,0,0});orbit.Step(1,1,.02f,1,1,false,false);
    Vec fixed=orbit.Apply({0,-50,-100},{0,0,0},1),nativeTurned=orbit.Apply({100,-50,0},{0,0,0},1);
    check(Length(fixed-nativeTurned)<.001f,"manual heading must persist when retail camera automatically turns");
    Vec moved=orbit.Apply({110,-50,0},{10,0,0},1);
    check(Length((moved-Vec{10,0,0})-fixed)<.001f,"camera must follow player translation without losing orbit");
    FollowCamera mouse30,mouse120;mouse30.Initialize({0,0,-100},{0,0,0});mouse120=mouse30;
    for(int i=0;i<30;++i)mouse30.Step(0,0,1.f/30,1,1,false,false,4,2);
    for(int i=0;i<120;++i)mouse120.Step(0,0,1.f/120,1,1,false,false,1,.5f);
    check(std::abs(mouse30.yaw-mouse120.yaw)<.0001f&&std::abs(mouse30.pitch-mouse120.pitch)<.0001f,"mouse turn distance must not depend on frame rate");
    link_profiles=true;horizontal=2;vertical=.5f;distance=1.5f;fov=75;custom_fov=true;SyncLinked();
    check(running_horizontal==2&&running_vertical==.5f&&running_distance==1.5f&&running_fov==75&&running_custom_fov,"linked profiles must copy all main slider values");
    running_horizontal=3;running_distance=2;SyncLinked(true);
    check(horizontal==3&&distance==2,"editing linked running sliders must update main values");
    link_profiles=false;Reset();
    write(owner+0x20C,14);distance=2;TenchuCameraBegin(ctx,base);
    check(available&&std::abs(read(cam+8)+200)<.01f,"nonzero gameplay camera modes must support controls");TenchuCameraEnd(ctx,base);
    Reset();test_pad.rx=20000;test_pad.ry=25000;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));TenchuCameraBegin(ctx,base);
    check(std::abs(read(cam))>.01f&&std::abs(read(cam+4))>.01f,"both stick axes must reach the actual guest camera pose");
    float orbitX=read(cam),orbitY=read(cam+4);TenchuCameraEnd(ctx,base);
    test_pad={};write(cam,Vec{100,0,0});TenchuCameraBegin(ctx,base);
    check(std::abs(read(cam)-orbitX)<.01f&&std::abs(read(cam+4)-orbitY)<.01f,"guest camera must retain user angle after native reorientation");TenchuCameraEnd(ctx,base);
    write(owner+0x218,4);TenchuCameraBegin(ctx,base);
    check(read(cam)==100&&!orbit_active&&tenchu::controls::native_aim,"first-person mode must bypass third-person orbit");TenchuCameraEnd(ctx,base);
    write(0x1BAB0F0,0xFFFFFFF0u);TenchuCameraBegin(ctx,base);check(!available,"invalid camera pointer must be rejected");
    puts("PASS: camera scope/restore, FOV, distance, free movement, frame rate, input capture, inversion, scripted bypass, reset and bounds");
}catch(const std::exception& e){puts(e.what());return 1;}}
