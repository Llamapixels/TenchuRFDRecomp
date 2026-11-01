#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "game_camera.h"
namespace tenchu::camera {
inline Motion PadFlightMotion(const input::Pad& pad){
    Motion m;
    m.forward=Axis(pad.ly);m.right=Axis(pad.lx);
    m.look_x=Axis(pad.rx);m.look_y=Axis(pad.ry);
    m.up=(pad.analog[7]-pad.analog[6])/255.f;
    m.multiplier=pad.analog[4]?4.f:(pad.analog[5]?.25f:1.f);
    return m;
}
inline void KeyboardFlightMotion(Motion& motion,bool enabled,POINT& previous,bool& previous_valid){
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    POINT cursor{};const bool cursor_ok=GetCursorPos(&cursor)!=0;
    if(enabled&&keyboard_mouse.load()&&!menu_open.load()&&foreground==GetCurrentProcessId()){
        auto held=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
        motion.forward+=float(held('W'))-float(held('S'));motion.right+=float(held('D'))-float(held('A'));
        motion.up+=float(held('E'))-float(held('Q'));
        motion.look_x+=float(held(VK_RIGHT))-float(held(VK_LEFT));motion.look_y+=float(held(VK_UP))-float(held(VK_DOWN));
        if(held(VK_SHIFT))motion.multiplier=4;if(held(VK_CONTROL))motion.multiplier=.25f;
        if(held(VK_RBUTTON)&&cursor_ok&&previous_valid){motion.mouse_x=float(cursor.x-previous.x);motion.mouse_y=float(previous.y-cursor.y);}
    }
    previous=cursor;previous_valid=cursor_ok;
}
}
