#include "diagnostics/autoplay.h"
#include "core/log.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include "game_camera.h"
#include "game_cheats.h"
#include <mutex>
namespace {
std::mutex handed_lock;
input::Pad handed;  // port 1's last report, while it goes to port 2
// Port 2 is connected while the second player is on; with control handed over it
// gets port 1's input and port 1 stays connected but idle.
void SecondPlayer(int port,input::Pad& pad){
    using namespace tenchu::cheats;
    const bool on=second_player.load(),handing=on&&control_second.load();
    if(port==0&&handing){
        {std::lock_guard lock(handed_lock);handed=pad;}
        const bool connected=pad.connected;pad={};pad.connected=connected;
    }else if(port==1&&on){
        if(handing){std::lock_guard lock(handed_lock);pad=handed;}
        pad.connected=true;
        // Start held for a third of a second after the menu closes (the game is paused while it is open)
        static uint64_t start_until=0;
        const uint64_t now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if(second_start.load()&&!tenchu::camera::menu_open.load()){second_start=false;start_until=now+330;}
        if(now<start_until)pad.buttons|=0x10;
    }
}
std::atomic<uint64_t> start{0};
std::atomic<bool> story_started{false};
uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
bool Enabled(){static bool value=std::getenv("TENCHU_AUTOPLAY")!=nullptr;return value;}
}
void TenchuStartAutomatedInput(){
    if(!Enabled())return;
    start=Now();
    LOGR("AUTOTEST: synthetic controller enabled for a bounded default Story Mode route");
}
void TenchuAutomatedPad(int port,input::Pad& pad){
    if(Enabled()){
        const auto epoch=start.load();
        if(epoch)pad=tenchu::AutoplayPad(port,Now()-epoch,story_started.load());
    }
    const int phase=tenchu::camera::diagnostic_phase.load();
    if(port==0&&std::getenv("TENCHU_CAMERA_PROBE")&&phase>=0){
        pad={};pad.connected=true;
        if(phase>=1&&phase<=4)pad.rx=16384;
        if(phase==3||phase==4)pad.ly=32767;
    }
    tenchu::camera::FilterPad(port,pad);
    SecondPlayer(port,pad);
}
void TenchuAutoplayStoryStarted(){story_started=true;}
