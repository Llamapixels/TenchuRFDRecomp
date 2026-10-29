#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace tenchu::cheats {
inline std::atomic<bool> god_mode{false};
// Multiplayer alone: controller port 2 reports a pad, idle unless yours is handed to it.
inline std::atomic<bool> second_player{false},control_second{false};
inline std::atomic<bool> second_start{false};  // press Start on port 2 once the F6 menu closes
inline std::atomic<bool> single_screen{false};  // split-screen levels draw only player 1, full screen
inline std::atomic<bool> story_start{false};    // in multiplayer, player 1 starts at the single-player start
inline std::atomic<bool> two_players_next{false};  // the next stage load creates player 2 (sets the two-player bit)
inline std::atomic<uint32_t> protected_writes{0};
// Called on the guest thread immediately after health writes, before the
// original code can test for death. Preserve guest registers and flags.
inline void ProtectHealth(uint8_t* base,uint32_t actor){
    if(!god_mode.load(std::memory_order_relaxed)||actor<0x10000||actor>0x03FFF800)return;
    uint32_t players[2];std::memcpy(players,base+0x01BAB110,sizeof(players));
    if(actor!=players[0]&&actor!=players[1])return;
    float maximum;std::memcpy(&maximum,base+actor+0x50,4);
    if(!std::isfinite(maximum)||maximum<=0||maximum>1000000)return;
    std::memcpy(base+actor+0x48,&maximum,4);
    std::memcpy(base+actor+0x54,&maximum,4);
    // The player task also restores health from this per-slot mirror.
    const uint8_t slot=base[actor+0x393];
    if(slot<2){
        std::memcpy(base+0x008E17AC+slot*4,&maximum,4);
        std::memcpy(base+0x008E17B4+slot*4,&maximum,4);
    }
    protected_writes.fetch_add(1,std::memory_order_relaxed);
}
}
