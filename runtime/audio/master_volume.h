#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace tenchu::audio {
inline std::atomic<float> master_gain{1};
inline void SetMaster(uint32_t percent,bool mute){master_gain=mute?0.f:std::min(percent,100u)/100.f;}
inline void ApplyMaster(float* samples,size_t count){
    const float gain=master_gain.load(std::memory_order_relaxed);
    if(gain==1)return;
    for(size_t i=0;i<count;++i)samples[i]*=gain;
}
}
