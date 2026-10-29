#include "cpu/x86_ops.h"
#include "game_cheats.h"
#include <cstring>
#include "core/log.h"
X86_EXTERN_FUNC(__imp__sub_000139C0);
namespace {
void SetViewport(uint8_t* base,uint32_t viewport,uint32_t x,uint32_t y,uint32_t w,uint32_t h){
    const uint32_t v[4]={x,y,w,h};
    std::memcpy(base+viewport,v,sizeof(v));
}
}
// Multiplayer alone, player 1 full screen. The split-screen render fills the viewport at
// [ebp-0x174] before each player's pass: player 1's becomes the whole 640 x 480 screen.
void TenchuPlayer1Viewport(X86Context& ctx,uint8_t* base){
    if(tenchu::cheats::single_screen.load(std::memory_order_relaxed))SetViewport(base,ctx.ebp-0x174,0,0,640,480);
}
// The scene pass for one player (cdecl, the player's index). Player 2's pass in the split-screen
// render (00236B20, returning to 00236B25) is skipped: shrinking its viewport to a pixel instead
// made the game stutter.
X86_FUNC(sub_000139C0){
    uint32_t ret,player;
    std::memcpy(&ret,base+ctx.esp,4);
    std::memcpy(&player,base+ctx.esp+4,4);
    if(ret==0x00236B25&&player==1&&tenchu::cheats::single_screen.load(std::memory_order_relaxed)){
        ctx.esp+=4;  // the caller pops the argument
        return;
    }
    __imp__sub_000139C0(ctx,base);
}
//
// Player positions, found live (not used yet): a player actor holds its position at 0x20,
// which other code reads, with copies at 0x110, 0x130 (head height), 0x1F0, 0x258, 0x268 and
// 0x2A8. Shifting only 0x20 is undone the next frame; shifting all of them moves the player,
// and the game's collision sets them back on the floor.
// Player placement: on the jnz that sends split screen to the multiplayer start tables. Player 1
// (slot byte 0x393 = 0) takes the single-player branch instead, so it starts where the story does
// and the opening cutscene triggers; the game brings player 2 over when the cutscene ends.
void TenchuStoryStart(X86Context& ctx,uint8_t* base){
    if(!tenchu::cheats::story_start.load(std::memory_order_relaxed))return;
    uint32_t actor;std::memcpy(&actor,base+ctx.ebp-8,sizeof(actor));
    if(actor>=0x10000&&actor<=0x03FFF800&&base[actor+0x393]==0)ctx.zf=1;
}
// Two players for the next mission: the stage loader runs with the two-player bit from its first
// step, so it creates player 2 like a Multiplayer stage; disarmed once player 2 exists.
void TenchuLoadTwoPlayers(X86Context&,uint8_t* base){
    if(!tenchu::cheats::two_players_next.load(std::memory_order_relaxed))return;
    uint32_t mode;std::memcpy(&mode,base+0x018FE6CC,4);
    mode|=0x04000000;std::memcpy(base+0x018FE6CC,&mode,4);
}
void TenchuTwoPlayersLoaded(X86Context&,uint8_t* base){
    uint32_t second;std::memcpy(&second,base+0x01BAB114,4);
    if(second&&tenchu::cheats::two_players_next.exchange(false))LOGR("Cheats: mission loaded with two players");
}
