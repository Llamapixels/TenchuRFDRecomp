#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace tenchu::hud {
inline std::atomic<bool> shown{true};
inline std::atomic<unsigned> opacity_percent{100};
inline constexpr uint32_t queue=0x01BAB148,stride=0x90,capacity=1024,count_address=queue+0x24004;
inline unsigned Opacity(){return shown.load()?std::min(opacity_percent.load(),100u):0u;}
inline uint32_t FadeColor(uint32_t color,unsigned percent){
    unsigned alpha=color>>24;
    return (color&0xFFFFFFu)|(((alpha*std::min(percent,100u)+50)/100)<<24);
}
inline void Reset(){shown=true;opacity_percent=100;}
}
