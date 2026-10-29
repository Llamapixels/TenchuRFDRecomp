#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
namespace tenchu::resize {
struct State {
    std::atomic<bool> interactive{false},minimized{false},presented{false};
    std::atomic<uint64_t> changed{0};
    void Message(uint32_t msg,uintptr_t value,uint64_t now){
        if(msg==WM_ENTERSIZEMOVE){interactive=true;changed=now;}
        if(msg==WM_EXITSIZEMOVE){changed=now;interactive=false;}
        if(msg==WM_SIZE){minimized=value==SIZE_MINIMIZED;changed=now;}
    }
    bool Paused(uint64_t now)const{
        if(interactive.load()||minimized.load())return true;
        const auto last=changed.load();
        return presented.load()&&last&&now>=last&&now-last<150;
    }
};
inline State window;
}
