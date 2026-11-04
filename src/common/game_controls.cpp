#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "game_controls.h"
#include "game_camera.h"
#include "core/log.h"
#include <chrono>

namespace tenchu::controls {
namespace {
std::atomic<HWND> window{nullptr};
constexpr UINT update_mouse_message=WM_APP+0x327;
std::atomic<bool> captured{false};
std::mutex state_lock;
std::mutex capture_lock;
input::Pad physical_pad;
KeyboardGate keyboard_gate;
std::atomic<unsigned> input_epoch{0};
float mouse_x=0,mouse_y=0,aim_x=0,aim_y=0;
std::atomic<float> aim_stick_x{0},aim_stick_y{0};
std::atomic<bool> released{false},ignore_click{false};
POINT return_cursor{};
auto last_ui=std::chrono::steady_clock::now();
std::atomic<int64_t> last_gameplay{0};
int64_t Milliseconds(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool Focused(){auto hwnd=window.load();return hwnd&&GetForegroundWindow()==hwnd&&!IsIconic(hwnd);}
bool WantMouse(){auto s=Snapshot();return Focused()&&s.keyboard&&s.mouse&&!menu_open.load()&&GameplayActive()&&(camera::follow_control.load()||native_aim.load())&&!camera::free_requested.load()&&!noclip::active.load()&&!released;}
void Capture(bool on){
    std::lock_guard guard(capture_lock);
    if(on){
        if(!captured.exchange(true))GetCursorPos(&return_cursor);
        RECT r{};auto hwnd=window.load();GetClientRect(hwnd,&r);
        POINT a{r.left,r.top},b{r.right,r.bottom};ClientToScreen(hwnd,&a);ClientToScreen(hwnd,&b);
        RECT clip{a.x,a.y,b.x,b.y};ClipCursor(&clip);SetCursor(nullptr);
    }else if(captured.exchange(false)){
        ClipCursor(nullptr);SetCursor(LoadCursor(nullptr,IDC_ARROW));SetCursorPos(return_cursor.x,return_cursor.y);DiscardMouse();
    }
}
}
void MarkGameplay(bool active){gameplay=active;if(active)last_gameplay=Milliseconds();}
bool GameplayActive(){return gameplay.load()&&Milliseconds()-last_gameplay.load()<500;}
void InitializeWindow(void* value){
    if(!value)return;
    HWND hwnd=static_cast<HWND>(value);if(window.exchange(hwnd)==hwnd)return;
    RAWINPUTDEVICE device{0x01,0x02,0,hwnd};
    if(!RegisterRawInputDevices(&device,1,sizeof(device)))LOGR("Mouse: raw input registration failed (%lu)",GetLastError());
}
void SetMenuOpen(bool open){if(menu_open.exchange(open)!=open)++input_epoch;if(open)DiscardMouse();if(auto hwnd=window.load())PostMessageW(hwnd,update_mouse_message,0,0);}
void UpdateWindow(){
    auto now=std::chrono::steady_clock::now();float dt=std::clamp(std::chrono::duration<float>(now-last_ui).count(),.001f,.1f);last_ui=now;
    // Cursor ownership stays on the window thread, outside the render/UI lock.
    if(auto hwnd=window.load())PostMessageW(hwnd,update_mouse_message,0,0);
    float x,y;{std::lock_guard guard(state_lock);x=aim_x;y=aim_y;aim_x=aim_y=0;}
    auto s=Snapshot();
    aim_stick_x=captured?std::clamp(x*s.aim_speed/(dt*700.f),-1.f,1.f):0;
    aim_stick_y=captured?std::clamp(-y*s.aim_speed/(dt*700.f),-1.f,1.f):0;
}
bool Message(void*,uint32_t msg,uintptr_t key,intptr_t data,intptr_t* result){
    if(msg==update_mouse_message){Capture(WantMouse());*result=0;return true;}
    if(msg==WM_INPUT){
        RAWINPUT raw{};UINT size=sizeof(raw);
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(data),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=UINT(-1)&&raw.header.dwType==RIM_TYPEMOUSE&&captured&&Focused()&&!menu_open.load()&&!(raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){
            std::lock_guard guard(state_lock);
            mouse_x+=float(raw.data.mouse.lLastX);mouse_y+=float(raw.data.mouse.lLastY);
            aim_x+=float(raw.data.mouse.lLastX);aim_y+=float(raw.data.mouse.lLastY);
        }
        return false; // DefWindowProc performs foreground WM_INPUT cleanup.
    }
    if(msg==WM_KILLFOCUS||(msg==WM_ACTIVATE&&LOWORD(key)==WA_INACTIVE)||msg==WM_DESTROY){++input_epoch;released=true;Capture(false);DiscardMouse();}
    if(msg==WM_KEYDOWN&&key==VK_ESCAPE&&!menu_open.load()){released=true;Capture(false);}
    if(msg==WM_LBUTTONDOWN&&released&&!menu_open.load()&&GameplayActive()&&Focused()){released=false;ignore_click=true;Capture(WantMouse());}
    if(msg==WM_SETCURSOR&&captured){SetCursor(nullptr);*result=TRUE;return true;}
    return false;
}
std::array<bool,256> HeldKeys(){
    std::array<bool,256> held{};if(!Focused())return held;
    for(int i=1;i<256;++i)held[i]=(GetAsyncKeyState(i)&0x8000)!=0;
    // Alt+Tab and Alt+F4 must never become in-game actions.
    if(held[VK_MENU]&&(held[VK_TAB]||held[VK_F4]))held.fill(false);
    if(ignore_click){if(!held[VK_LBUTTON])ignore_click=false;held[VK_LBUTTON]=false;}
    return held;
}
input::Pad PhysicalPad(){std::lock_guard guard(state_lock);return physical_pad;}
void Apply(int port,input::Pad& p){
    if(port==0){std::lock_guard guard(state_lock);physical_pad=p;}
    auto s=Snapshot();
    const bool keyboard=port==0&&s.keyboard&&Focused()&&!menu_open.load()&&!camera::free_active.load()&&!noclip::active.load();
    auto held=HeldKeys();const bool playing=GameplayActive();
    if(port==0){
        std::lock_guard guard(state_lock);
        keyboard_gate.Filter(held,menu_open.load()?2:(!Focused()?3:(playing?0:1)),input_epoch.load());
    }
    p=Map(s,p,held,keyboard,!playing);
    // A keyboard is a persistent virtual controller, including F6 and Alt-Tab.
    if(port==0&&s.keyboard&&window.load())p.connected=true;
    if(menu_open.load()||(!Focused()&&window.load())){const bool connected=p.connected;p={};p.connected=connected;return;}
    if(port==0){
        const bool aim=Aiming(p)||native_aim.load();
        if(aim){
            // Retail first-person and projectile aiming use the LEFT stick.
            float x=p.rx/32767.f*s.aim_speed,y=p.ry/32767.f*s.aim_speed;
            if(captured){x+=aim_stick_x.load();y+=aim_stick_y.load();}
            x*=camera::horizontal.load()*(camera::invert_x.load()?-1:1);y*=camera::vertical.load()*(camera::invert_y.load()?-1:1);
            if(std::abs(x)>.001f)p.lx=int16_t(std::clamp(x,-1.f,1.f)*32767);
            if(std::abs(y)>.001f)p.ly=int16_t(std::clamp(y,-1.f,1.f)*32767);
        }
    }
}
std::array<float,2> ConsumeMouse(){std::lock_guard guard(state_lock);auto v=std::array<float,2>{mouse_x,-mouse_y};mouse_x=mouse_y=0;return v;}
void DiscardMouse(){std::lock_guard guard(state_lock);mouse_x=mouse_y=aim_x=aim_y=0;aim_stick_x=aim_stick_y=0;}
bool MouseCaptured(){return captured.load();}
bool ReservedKey(int key){return key==VK_F3||key==VK_F6||key==VK_F10||key==VK_F12||key==VK_LWIN||key==VK_RWIN;}
std::string KeyName(int key){
    if(key<=0)return "Unbound";
    switch(key){case VK_LBUTTON:return "Mouse left";case VK_RBUTTON:return "Mouse right";case VK_MBUTTON:return "Mouse middle";case VK_XBUTTON1:return "Mouse 4";case VK_XBUTTON2:return "Mouse 5";}
    char name[80]{};UINT scan=MapVirtualKeyA(UINT(key),MAPVK_VK_TO_VSC);
    if(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_DELETE||key==VK_INSERT||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT)scan|=0x100;
    if(GetKeyNameTextA(LONG(scan<<16),name,sizeof(name)))return name;
    return "Key "+std::to_string(key);
}
}
