#pragma once
#include <algorithm>
#include <cstdint>
namespace tenchu {
inline uint32_t ClampInternalPercent(uint32_t value){return std::clamp(value,50u,400u);}
inline uint32_t InternalDimension(uint32_t native,uint32_t percent){return (native*ClampInternalPercent(percent)+50)/100;}
}
