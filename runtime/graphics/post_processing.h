#pragma once
#include <array>
#include <atomic>
#include <cstddef>
namespace tenchu::post {
inline std::atomic<bool> enabled{false},compare{false};
inline std::atomic<float> divider{.5f};
inline constexpr size_t count=16;
// First eight indices are retained for existing saved settings.
inline constexpr std::array<const char*,count> keys={"exposure","brightness","contrast","saturation","gamma","warmth","sharpness","vignette","clarity","bloom","bloom_threshold","bloom_size","bloom_haze","highlight_rolloff","vibrance","shadow_lift"};
inline constexpr std::array<const char*,count> labels={"Exposure","Brightness","Contrast","Saturation","Gamma","Warmth","Sharpness","Vignette","Clarity","Bloom","Bloom threshold","Bloom size","Bloom haze","Highlight roll-off","Vibrance","Shadow lift"};
inline constexpr std::array<float,count> defaults={0,0,1,1,1,0,0,0,0,0,.7f,4,.35f,0,0,0};
inline constexpr std::array<float,count> minimum={-2,-.25f,.5f,0,.5f,-1,0,0,0,0,0,1,0,0,-1,0};
inline constexpr std::array<float,count> maximum={2,.25f,2,2,2,1,1,1,1,2,1,24,1,1,1,.5f};
inline std::array<std::atomic<float>,count> values{0,0,1,1,1,0,0,0,0,0,.7f,4,.35f,0,0,0};
inline auto PresetValues(int preset){
    auto v=defaults;
    if(preset==1){v[0]=.1f;v[2]=1.08f;v[3]=1.18f;v[6]=.2f;}
    if(preset==2){v[2]=1.12f;v[3]=.85f;v[5]=.12f;v[6]=.15f;v[7]=.2f;}
    if(preset==3){v[0]=.3f;v[1]=.02f;v[4]=1.08f;v[6]=.1f;}
    if(preset==4){v[0]=.1f;v[2]=1.08f;v[3]=.95f;v[5]=.1f;v[6]=.35f;v[8]=.25f;v[9]=.15f;v[10]=.6f;v[11]=6;v[13]=.35f;v[14]=.2f;v[15]=.025f;}
    return v;
}
inline void Preset(int preset){auto v=PresetValues(preset);for(size_t i=0;i<count;++i)values[i]=v[i];}
inline void Reset(){Preset(0);enabled=false;compare=false;divider=.5f;}
}
