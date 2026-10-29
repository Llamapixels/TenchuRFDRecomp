#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "cpu/x86_ops.h"
#include "game_camera.h"
#include <chrono>
#include <cstring>
#include <cstdlib>
#include "core/log.h"
void TenchuCaptureCameraFrame(int phase);
X86_EXTERN_FUNC(sub_0009CD40);
X86_EXTERN_FUNC(sub_0009C4F0);

namespace {
using namespace tenchu::camera;
template<class T> T Read(uint8_t* base,uint32_t address){T v;std::memcpy(&v,base+address,sizeof(v));return v;}
template<class T> void Write(uint8_t* base,uint32_t address,T value){std::memcpy(base+address,&value,sizeof(value));}
bool Valid(uint32_t p,uint32_t size){return p>=0x10000&&p<=0x04000000-size;}
struct Saved {
    uint32_t camera=0,projection=0;float focal=0,camera_focal=0;Vec eye{},target{};
    // what this frame wrote, so End only puts back values the game left alone
    Vec wrote_eye{},wrote_target{};float wrote_cam_focal=0,wrote_proj_focal=0;bool posed=false,focused=false,photo=false;
    unsigned scripted=0;  // frames left with overrides paused after the game moved the camera itself
    FreeCamera free;FollowCamera follow;uint32_t owner=0;unsigned reset=0;
    std::chrono::steady_clock::time_point last{};POINT cursor{};bool cursor_valid=false;
    std::chrono::steady_clock::time_point probe_start{};
    int probe_phase=-1;bool probe_sample=false,probe_captured=false;unsigned probe_checks=0,probe_failures=0;
};
thread_local Saved saved;
void RebuildFollowView(X86Context& ctx,uint8_t* base,uint32_t camera){
    // Native camera simulation has already built its basis/matrix caches.
    // Rebuild those caches for the modified follow pose before drawing.
    // Preserve the complete CPU context, including flags and floating point.
    const X86Context original=ctx;
    for(auto fn:{sub_0009CD40,sub_0009C4F0}){
        ctx=original;
        ctx.esp-=4;Write(base,ctx.esp,camera);
        ctx.esp-=4;Write(base,ctx.esp,uint32_t(0));
        fn(ctx,base);
    }
    ctx=original;
}
}
// Restore before the player-camera simulation (002A1400), then apply after its
// common exit (002A1929). Keep the pose for ALL render/culling consumers until
// the next simulation tick rather than restoring midway through rendering.
void TenchuCameraBegin(X86Context& ctx,uint8_t* base){
    using namespace tenchu::camera;
    saved.camera=0;available=false;free_active=false;++scene_frames;
    auto now=std::chrono::steady_clock::now();
    float dt=saved.last.time_since_epoch().count()?std::chrono::duration<float>(now-saved.last).count():1.f/60;
    saved.last=now;
    uint32_t owner=Read<uint32_t>(base,0x01BAB0F0),player=Read<uint32_t>(base,0x01BAB110);
    if(!Valid(owner,0x2A8)||!Valid(player,0x7D8)){saved.free.initialized=false;mode=-1;return;}
    int camera_mode=Read<int>(base,owner+0x20C);mode=camera_mode;
    static int last_mode=-999;
    if(camera_mode!=last_mode){LOGR("Camera path: scene 0005E720, mode %d, player state %u",camera_mode,Read<uint32_t>(base,player+0x300));last_mode=camera_mode;}
    uint32_t cam=Read<uint32_t>(base,owner+0x25C),projection=Read<uint32_t>(base,owner+0x260);
    // Mode 0 is the normal player-follow camera. Scripted modes retain framing.
    if(camera_mode<0||camera_mode>=0x30||!Valid(cam,0x60)||!Valid(projection,0x24)||Read<uint32_t>(base,0x008E38C0)!=cam){saved.free.initialized=false;return;}
    Vec eye=Read<Vec>(base,cam),target=Read<Vec>(base,cam+0x40);
    float length=Length(target-eye),focal=Read<float>(base,cam+0x50);
    if(!Finite(eye)||!Finite(target)||length<.01f||!std::isfinite(length)||!std::isfinite(focal)||focal<=1){saved.free.initialized=false;return;}
    available=true;native_fov=std::atan(240.f/focal)*114.591559f;
    if(saved.owner!=owner||saved.reset!=reset_sequence.load()){saved.free.initialized=false;saved.follow={};saved.owner=owner;saved.reset=reset_sequence.load();}
    const bool probe=std::getenv("TENCHU_CAMERA_PROBE")!=nullptr;
    if(probe){
        if(saved.probe_start.time_since_epoch().count()==0)saved.probe_start=now;
        int phase=std::min(6,int(std::chrono::duration<float>(now-saved.probe_start).count()/1.5f));
        if(phase!=saved.probe_phase){
            saved.probe_phase=phase;saved.probe_sample=false;saved.probe_captured=false;saved.follow={};
            diagnostic_phase=phase;
            free_requested=phase==4;horizontal=phase==2?2.f:1.f;
            custom_fov=phase==5;fov=90;distance=phase==5?2.f:1.f;
            running_horizontal=.5f;
            if(phase==6){Reset();LOGR("CAMERA PROBE finished: %u actual scene-builder samples, %u failures",saved.probe_checks,saved.probe_failures);}
            else LOGR("CAMERA PROBE phase %d: actual scene mode %d",phase,camera_mode);
        }
        float elapsed=std::chrono::duration<float>(now-saved.probe_start).count();
        if(!saved.probe_captured&&elapsed-phase*1.5f>.9f){
            TenchuCaptureCameraFrame(phase);saved.probe_captured=true;
            LOGR("CAMERA PROBE: presentation capture phase %d",phase);
        }
    }
    bool photo=free_requested.load();free_active=photo;
    Motion motion;
    // Poll through the ordinary input service. Its title filter retains a raw
    // controller snapshot while withholding movement from the player in photo mode.
    input::Poll(0);
    input::Pad pad;{std::lock_guard lock(pad_lock);pad=raw_pad;}
    // Separate profile for movement camera: high left-stick input selects the
    // running settings. Photo mode retains the profile active when entered.
    if(!photo)running=std::hypot(Axis(pad.lx),Axis(pad.ly))>.65f;
    const bool run=running.load()&&!link_profiles.load();
    const float hs=run?running_horizontal.load():horizontal.load();
    const float vs=run?running_vertical.load():vertical.load();
    const float distance_scale=run?running_distance.load():distance.load();
    const bool override_fov=run?running_custom_fov.load():custom_fov.load();
    const float degrees=run?running_fov.load():fov.load();
    if(!menu_open.load()){
        motion.forward=Axis(pad.ly);motion.right=Axis(pad.lx);
        motion.look_x=Axis(pad.rx);motion.look_y=Axis(pad.ry);
        motion.up=(pad.analog[7]-pad.analog[6])/255.f;
        motion.multiplier=pad.analog[4]?4.f:(pad.analog[5]?.25f:1.f);
    }
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    POINT cursor{};bool cursor_ok=GetCursorPos(&cursor)!=0;
    if(photo&&keyboard_mouse.load()&&!menu_open.load()&&foreground==GetCurrentProcessId()){
        auto held=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
        motion.forward+=float(held('W'))-float(held('S'));motion.right+=float(held('D'))-float(held('A'));
        motion.up+=float(held('E'))-float(held('Q'));
        motion.look_x+=float(held(VK_RIGHT))-float(held(VK_LEFT));motion.look_y+=float(held(VK_UP))-float(held(VK_DOWN));
        if(held(VK_SHIFT))motion.multiplier=4;if(held(VK_CONTROL))motion.multiplier=.25f;
        if(held(VK_RBUTTON)&&cursor_ok&&saved.cursor_valid){motion.mouse_x=float(cursor.x-saved.cursor.x);motion.mouse_y=float(saved.cursor.y-cursor.y);}
    }
    saved.cursor=cursor;saved.cursor_valid=cursor_ok;
    if(photo){
        if(!saved.free.initialized){saved.free.Initialize(eye,target);LOGR("Camera: free camera acquired gameplay camera %08X",cam);}
        target=saved.free.Step(motion,dt,horizontal.load(),vertical.load(),invert_x.load(),invert_y.load());eye=saved.free.eye;
    }else{
        saved.free.initialized=false;
        if(saved.scripted){
            // Paused while the game drives the camera. Keep checking with a one-step
            // change to the focal lengths, invisible on screen: the scripted camera
            // rewrites them inside this scope, gameplay leaves them alone. Checking by
            // applying the custom FOV for a frame made cutscenes flicker.
            --saved.scripted;saved.follow={};
            saved.camera=cam;saved.projection=projection;saved.posed=saved.photo=false;saved.focused=true;
            saved.wrote_eye=eye;saved.wrote_target=target;
            saved.camera_focal=focal;saved.focal=Read<float>(base,projection);
            saved.wrote_cam_focal=std::nextafter(focal,0.f);saved.wrote_proj_focal=std::nextafter(saved.focal,0.f);
            Write(base,cam+0x50,saved.wrote_cam_focal);Write(base,projection,saved.wrote_proj_focal);
            return;
        }
        if(follow_control.load()){
            if(pad.buttons&(1u<<7))saved.follow={};
            saved.follow.Step(motion.look_x,motion.look_y,dt,hs,vs,invert_x.load(),invert_y.load());
        }else saved.follow={};
        if(distance_scale==1&&!override_fov&&saved.follow.yaw==0&&saved.follow.pitch==0)return;
        eye=saved.follow.Apply(eye,target,distance_scale);
    }
    saved.camera=cam;saved.projection=projection;saved.focal=Read<float>(base,projection);saved.camera_focal=focal;
    saved.eye=Read<Vec>(base,cam);saved.target=Read<Vec>(base,cam+0x40);
    // A FOV-only change leaves the pose alone and needs no extra rebuild: the
    // scene calls the view and frustum builders right after this hook.
    saved.posed=std::memcmp(&eye,&saved.eye,sizeof(Vec))||std::memcmp(&target,&saved.target,sizeof(Vec));
    saved.wrote_eye=eye;saved.wrote_target=target;saved.focused=override_fov;saved.photo=photo;
    if(saved.posed){Write(base,cam,eye);Write(base,cam+0x40,target);}
    if(override_fov){
        saved.wrote_cam_focal=saved.wrote_proj_focal=FocalLength(degrees);
        Write(base,cam+0x50,saved.wrote_cam_focal);
        Write(base,projection,saved.wrote_proj_focal);
    }
    if(!photo&&saved.posed)RebuildFollowView(ctx,base,cam);
}
void TenchuCameraEnd(X86Context&,uint8_t* base){
    if(!saved.camera)return;
    if(std::getenv("TENCHU_CAMERA_PROBE")&&!saved.probe_sample){
        // The real guest view builder writes its normalized forward vector at
        // camera+20. Verify it consumed the override, before restoring the rig.
        Vec eye=Read<Vec>(base,saved.camera),target=Read<Vec>(base,saved.camera+0x40);
        Vec expected=target-eye;expected=expected*(1/Length(expected));
        Vec actual=Read<Vec>(base,saved.camera+0x20);
        float dot=expected.x*actual.x+expected.y*actual.y+expected.z*actual.z;
        bool ok=Finite(actual)&&dot>.99f;
        ++saved.probe_checks;if(!ok)++saved.probe_failures;
        LOGR("CAMERA PROBE sample phase %d: view dot %.5f, eye delta %.3f, orbit yaw %.5f, running %d, free %d, focal %.2f, %s",saved.probe_phase,dot,Length(eye-saved.eye),saved.follow.yaw,int(running.load()),int(free_active.load()),Read<float>(base,saved.camera+0x50),ok?"PASS":"FAIL");
        saved.probe_sample=true;
    }
    // Put back only what still holds this frame's values. Event cameras share the
    // player camera in mode 0 (the mission 1 intro): when the game moves it inside
    // this scope, keep the game's pose and pause overrides, or the restore pins
    // the cutscene view while the scene plays on.
    bool moved=false;
    auto vec=[&](uint32_t at,Vec wrote,Vec original,bool ours){
        Vec now=Read<Vec>(base,at);
        if(std::memcmp(&now,&wrote,sizeof(Vec)))moved=true;else if(ours)Write(base,at,original);
    };
    auto scalar=[&](uint32_t at,float wrote,float original){
        float now=Read<float>(base,at);
        if(std::memcmp(&now,&wrote,sizeof(float)))moved=true;else Write(base,at,original);
    };
    vec(saved.camera,saved.wrote_eye,saved.eye,saved.posed);
    vec(saved.camera+0x40,saved.wrote_target,saved.target,saved.posed);
    if(saved.focused){scalar(saved.camera+0x50,saved.wrote_cam_focal,saved.camera_focal);scalar(saved.projection,saved.wrote_proj_focal,saved.focal);}
    static bool reported=false;
    if(moved&&!saved.photo){
        if(!reported)LOGR("Camera: the game moved the camera itself (scripted scene); custom camera paused");
        reported=true;saved.scripted=30;saved.follow={};
    }else if(!saved.scripted)reported=false;
    saved.camera=0;
}
