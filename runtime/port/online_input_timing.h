#pragma once
#include "core/input.h"
#include <chrono>
#include <mutex>
#include <optional>

namespace tenchu::online::timing {
inline int64_t Now(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// Observe local rendered input even when no network simulation tick runs.
// This measures observation-to-next-native-batch, not physical device latency
// or remote action latency. Quantize axes to ignore analog stick noise.
struct InputProbe {
    uint32_t previous=0;int64_t edge=0;bool pending=false;unsigned reports=0;
    void Observe(uint32_t activity,int64_t now){if(activity!=previous){previous=activity;edge=now;pending=true;}}
    std::optional<double> Sent(int64_t now){
        if(!pending||reports>=24)return {};
        pending=false;++reports;
        return double(now>=edge?now-edge:0)/1000.;
    }
};
inline std::mutex lock;
inline InputProbe input;
inline uint32_t Activity(const input::Pad& pad){
    uint32_t bits=pad.buttons;
    for(int i=0;i<8;++i)if(pad.analog[i]>=128)bits|=1u<<(8+i);
    const int axes[]={pad.lx,pad.ly,pad.rx,pad.ry};
    for(int i=0;i<4;++i){if(axes[i]>8192)bits|=1u<<(16+2*i);if(axes[i]<-8192)bits|=1u<<(17+2*i);}
    return bits;
}
inline void Observe(const input::Pad& pad){std::lock_guard guard(lock);input.Observe(Activity(pad),Now());}
inline std::optional<double> Sent(){std::lock_guard guard(lock);return input.Sent(Now());}
}
