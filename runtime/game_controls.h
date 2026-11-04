#pragma once
#include "core/input.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>

namespace tenchu::controls {
// Logical Xbox inputs; the original game's selected control scheme still applies.
enum Action { Up,Down,Left,Right,Start,Back,LClick,RClick,A,B,X,Y,Black,White,LT,RT,
    MoveForward,MoveBack,MoveLeft,MoveRight,LookUp,LookDown,LookLeft,LookRight,Count };
inline constexpr const char* keys[Count]={"up","down","left","right","start","back","left_click","right_click",
    "a","b","x","y","black","white","lt","rt","move_forward","move_back","move_left","move_right","look_up","look_down","look_left","look_right"};
inline constexpr const char* labels[Count]={"Grappling hook / D-pad up","Health potion / D-pad down","Previous item / D-pad left","Next item / D-pad right",
    "Pause / Start","Back","Left stick click","Detonate / right stick click","Jump / A","Block / B","Attack / X","Use or aim item / Y",
    "Cycle items / Black (RB)","First-person look / White (LB)","Stealth / LT","Lock on or strafe / RT",
    "Move forward","Move backward","Move left","Move right","Look up","Look down","Look left","Look right"};
inline constexpr const char* sources[Count]={"D-pad up","D-pad down","D-pad left","D-pad right","Start","Back","Left stick click","Right stick click",
    "A / Cross","B / Circle","X / Square","Y / Triangle","RB / R1","LB / L1","LT / L2","RT / R2",
    "Left stick up","Left stick down","Left stick left","Left stick right","Right stick up","Right stick down","Right stick left","Right stick right"};
struct Settings {
    bool keyboard=true,mouse=true;
    float deadzone=.18f,mouse_speed=1,aim_speed=1,walk_speed=.45f;
    int walk_key=0x10; // Shift
    int confirm_key=0x0D,cancel_key=0x1B;
    std::array<int,Count> keyboard_bindings={ 'G','H','Q','E',0x1B,0x09,'V','C',0x20,0x02,0x01,'F','R',0x12,0x11,0x05,
        'W','S','A','D',0x26,0x28,0x25,0x27 };
    std::array<int,Count> controller_bindings{};
    Settings(){for(int i=0;i<Count;++i)controller_bindings[i]=i;}
};
inline std::mutex settings_lock;
inline Settings settings;
inline Settings Snapshot(){std::lock_guard guard(settings_lock);return settings;}
inline void Configure(const Settings& value){std::lock_guard guard(settings_lock);settings=value;}
inline void Reset(){Configure(Settings{});}
inline std::atomic<bool> menu_open{false},gameplay{false},native_aim{false};
struct KeyboardGate {
    bool initialized=false;int context=0;unsigned epoch=0;
    std::array<bool,256> blocked{};
    void Filter(std::array<bool,256>& held,int next,unsigned sequence){
        if(initialized&&(context!=next||epoch!=sequence))for(int i=1;i<256;++i)blocked[i]=blocked[i]||held[i];
        initialized=true;context=next;epoch=sequence;
        for(int i=1;i<256;++i){if(!held[i])blocked[i]=false;if(blocked[i])held[i]=false;}
    }
};
inline float Axis(int value,float dz){
    float v=std::clamp(value/32767.f,-1.f,1.f);dz=std::clamp(dz,0.f,.5f);
    return std::abs(v)<=dz?0.f:std::copysign((std::abs(v)-dz)/(1-dz),v);
}
inline std::array<float,Count> Values(const input::Pad& p){
    std::array<float,Count> v{};
    for(int i=0;i<8;++i)v[i]=float((p.buttons>>i)&1);
    for(int i=0;i<8;++i)v[8+i]=p.analog[i]/255.f;
    auto axis=[&](int n,int value){float f=std::clamp(value/32767.f,-1.f,1.f);v[n]=std::max(f,0.f);v[n+1]=std::max(-f,0.f);};
    axis(MoveForward,p.ly);axis(MoveLeft,-int(p.lx));axis(LookUp,p.ry);axis(LookLeft,-int(p.rx));return v;
}
inline input::Pad Compose(const std::array<float,Count>& v,bool connected){
    input::Pad p;p.connected=connected;
    for(int i=0;i<8;++i)if(v[i]>.5f)p.buttons|=uint16_t(1u<<i);
    for(int i=0;i<8;++i)p.analog[i]=uint8_t(std::clamp(v[8+i],0.f,1.f)*255+.5f);
    auto axis=[&](int positive,int negative){return int16_t(std::clamp(v[positive]-v[negative],-1.f,1.f)*32767);};
    p.lx=axis(MoveRight,MoveLeft);p.ly=axis(MoveForward,MoveBack);p.rx=axis(LookRight,LookLeft);p.ry=axis(LookUp,LookDown);return p;
}
inline input::Pad Map(const Settings& s,const input::Pad& physical,const std::array<bool,256>& held,bool keyboard,bool menus=false){
    auto source=Values(physical);std::array<float,Count> v{};
    const bool walk=keyboard&&s.walk_key>0&&s.walk_key<256&&held[s.walk_key];
    for(int i=0;i<Count;++i){
        int from=s.controller_bindings[i];if(from>=0&&from<Count)v[i]=source[from];
        int key=s.keyboard_bindings[i];
        if(keyboard&&key>0&&key<256&&held[key])v[i]=std::max(v[i],walk&&i>=MoveForward&&i<=MoveRight?s.walk_speed:1.f);
    }
    // Keyboard diagonal movement has the same maximum speed as a stick.
    float x=v[MoveRight]-v[MoveLeft],y=v[MoveForward]-v[MoveBack],len=std::hypot(x,y);
    if(len>1)for(int i=MoveForward;i<=MoveRight;++i)v[i]/=len;
    auto p=Compose(v,physical.connected||keyboard);
    if(menus&&keyboard){
        auto down=[&](int action){int key=s.keyboard_bindings[action];return key>0&&key<256&&held[key];};
        if(down(MoveForward)||down(LookUp))p.buttons|=1;if(down(MoveBack)||down(LookDown))p.buttons|=2;
        if(down(MoveLeft)||down(LookLeft))p.buttons|=4;if(down(MoveRight)||down(LookRight))p.buttons|=8;
        if(s.confirm_key>0&&s.confirm_key<256&&held[s.confirm_key])p.analog[0]=255;
        if(s.cancel_key>0&&s.cancel_key<256&&held[s.cancel_key]){p.analog[1]=255;p.buttons&=~uint16_t(16);}
    }
    return p;
}
inline bool Aiming(const input::Pad& p){return p.analog[White-8]>=128||p.analog[Y-8]>=128;}
void InitializeWindow(void*);
void SetMenuOpen(bool);
void UpdateWindow();
void MarkGameplay(bool);
bool GameplayActive();
bool Message(void*,uint32_t,uintptr_t,intptr_t,intptr_t*);
void Apply(int port,input::Pad&);
std::array<bool,256> HeldKeys();
input::Pad PhysicalPad();
std::array<float,2> ConsumeMouse();
void DiscardMouse();
std::string KeyName(int);
bool ReservedKey(int);
bool MouseCaptured();
void RenderMenu();
}
