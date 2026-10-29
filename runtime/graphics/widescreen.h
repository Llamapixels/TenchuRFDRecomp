#pragma once
#include <atomic>
namespace tenchu::wide {
inline std::atomic<bool> enabled{false};
inline constexpr float kHorizontal=0.75f; // (4/3) / (16/9), preserve vertical FOV
inline void ExpandProjection(float* matrix){if(enabled.load())matrix[0]*=kHorizontal;}
inline float HudScale(bool backbuffer,bool screen_space,bool samples_target){
    return enabled.load()&&backbuffer&&screen_space&&!samples_target?kHorizontal:1.f;
}
inline float HudPositionX(float raster_x,float buffer_width,float raster_scale,float hud_scale){
    const float center=buffer_width*raster_scale*.5f;
    return (raster_x-center)*hud_scale+center;
}
}
