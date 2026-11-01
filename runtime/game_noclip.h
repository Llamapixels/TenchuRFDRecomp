#pragma once
#include "game_camera.h"
#include "noclip_state.h"
#include <mutex>
namespace tenchu::noclip {
// Camera and player hooks run on the guest thread; publish the completed pose
// for the camera consumer without exposing session state to the patch thread.
inline std::mutex view_lock;
inline camera::Vec view_eye{},view_target{};
inline uint32_t view_player=0;
inline void PublishView(uint32_t player,camera::Vec eye,camera::Vec target){
    std::lock_guard lock(view_lock);view_player=player;view_eye=eye;view_target=target;
}
inline bool View(uint32_t player,camera::Vec& eye,camera::Vec& target){
    if(!requested.load()||!active.load())return false;
    std::lock_guard lock(view_lock);
    if(view_player!=player)return false;
    eye=view_eye;target=view_target;return true;
}
}
