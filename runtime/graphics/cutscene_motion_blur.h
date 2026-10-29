#pragma once
#include <atomic>
#include <cstdint>
namespace tenchu::motion {
// Native cutscene accumulation blends new image into an uncleared history target.
inline std::atomic<bool> original_blur{false};
inline bool Bypass(bool backdrop,bool backbuffer,bool source_target,bool depth,bool blend,
                   uint32_t src_blend,uint32_t dst_blend,uint32_t format,
                   uint32_t src_w,uint32_t src_h,uint32_t dst_w,uint32_t dst_h){
    return !original_blur.load() && backdrop && !backbuffer && source_target && !depth && blend &&
        src_blend==0x302 && dst_blend==0x303 && format==0x12 && dst_w && dst_h &&
        uint64_t(src_w)==uint64_t(dst_w)*2 && src_h==dst_h;
}
}
