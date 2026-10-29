#pragma once
#include "core/input.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace tenchu::camera {
inline std::atomic<bool> free_requested{false},free_active{false},available{false},menu_open{false};
inline std::atomic<bool> invert_x{false},invert_y{false},custom_fov{false},keyboard_mouse{true};
inline std::atomic<float> horizontal{1},vertical{1},fov{48},native_fov{48},distance{1};
// Off by default: on, the port withholds the right stick from the game and orbits the camera itself.
inline std::atomic<bool> follow_control{false},running{false},running_custom_fov{false};
inline std::atomic<bool> link_profiles{false};
inline std::atomic<float> running_horizontal{1},running_vertical{1},running_fov{48},running_distance{1};
inline std::atomic<unsigned> scene_frames{0};
inline std::atomic<int> diagnostic_phase{-1}; // enabled only by TENCHU_CAMERA_PROBE
inline std::atomic<int> mode{-1};
inline std::atomic<unsigned> reset_sequence{0};
inline void SyncLinked(bool from_running=false){
    if(!link_profiles.load())return;
    if(from_running){
        horizontal=running_horizontal.load();vertical=running_vertical.load();
        distance=running_distance.load();fov=running_fov.load();custom_fov=running_custom_fov.load();
    }else{
        running_horizontal=horizontal.load();running_vertical=vertical.load();
        running_distance=distance.load();running_fov=fov.load();running_custom_fov=custom_fov.load();
    }
}
inline std::mutex pad_lock;
inline input::Pad raw_pad;
inline float Axis(int value){float v=std::clamp(value/32767.f,-1.f,1.f);return std::abs(v)<.18f?0.f:std::copysign((std::abs(v)-.18f)/.82f,v);}
inline void FilterPad(int port,input::Pad& pad){
    if(port!=0)return;
    {std::lock_guard lock(pad_lock);raw_pad=pad;}
    if(free_active.load()){
        pad.buttons=0;for(auto& a:pad.analog)a=0;pad.lx=pad.ly=pad.rx=pad.ry=0;return;
    }
    if(available.load()){
        // Native camera code quantizes stick directions. Scaling its integer
        // input saturated at full tilt and did not scale angular speed.
        // The scene hook now integrates raw stick angles at the chosen rate.
        if(follow_control.load())pad.rx=pad.ry=0;
    }
}
inline void Reset(){
    free_requested=false;free_active=false;invert_x=false;invert_y=false;
    horizontal=1;vertical=1;distance=1;custom_fov=false;fov=native_fov.load();
    running_horizontal=1;running_vertical=1;running_distance=1;running_custom_fov=false;
    running_fov=native_fov.load();follow_control=false;
    ++reset_sequence;
}
struct Vec {float x,y,z;};
inline Vec operator+(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator-(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec operator*(Vec a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Length(Vec a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
inline bool Finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
struct Motion {float forward=0,right=0,up=0,look_x=0,look_y=0,mouse_x=0,mouse_y=0,multiplier=1;};
struct FreeCamera {
    Vec eye{};float yaw=0,pitch=0,focus=100;bool initialized=false;
    void Initialize(Vec position,Vec target){
        eye=position;Vec dir=target-position;focus=std::max(Length(dir),1.f);
        yaw=std::atan2(dir.x,dir.z);pitch=std::asin(std::clamp(-dir.y/focus,-1.f,1.f));initialized=true;
    }
    Vec Step(const Motion& in,float dt,float hs,float vs,bool ix,bool iy){
        dt=std::clamp(dt,0.f,.05f);
        yaw+=(in.look_x*2.f*dt+in.mouse_x*.003f)*hs*(ix?-1.f:1.f);
        pitch=std::clamp(pitch+(in.look_y*2.f*dt+in.mouse_y*.003f)*vs*(iy?-1.f:1.f),-1.53f,1.53f);
        Vec direction{std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        Vec right{std::cos(yaw),0,-std::sin(yaw)};
        Vec delta=direction*in.forward+right*in.right+Vec{0,-in.up,0};
        float len=Length(delta);if(len>1)delta=delta*(1/len);
        eye=eye+delta*(600.f*dt*in.multiplier);
        return eye+direction*focus;
    }
};
inline float FocalLength(float degrees){return 240.f/std::tan(std::clamp(degrees,20.f,120.f)*.00872664626f);}
struct FollowCamera {
    float yaw=0,pitch=0;
    void Step(float x,float y,float dt,float hs,float vs,bool ix,bool iy){
        dt=std::clamp(dt,0.f,.05f);
        yaw=std::remainder(yaw+x*2.f*dt*hs*(ix?-1.f:1.f),6.283185307f);
        pitch=std::clamp(pitch+y*2.f*dt*vs*(iy?-1.f:1.f),-1.4f,1.4f);
    }
    Vec Apply(Vec eye,Vec target,float distance_scale)const{
        if(yaw==0&&pitch==0&&distance_scale==1)return eye;
        Vec offset=eye-target;float radius=Length(offset);
        float heading=std::atan2(offset.x,offset.z)+yaw;
        float elevation=std::clamp(std::asin(std::clamp(offset.y/radius,-1.f,1.f))+pitch,-1.45f,1.45f);
        radius*=std::clamp(distance_scale,.25f,4.f);
        return target+Vec{std::sin(heading)*std::cos(elevation),std::sin(elevation),std::cos(heading)*std::cos(elevation)}*radius;
    }
};
}
