#pragma once
#include "core/input.h"
#include <cstdint>
namespace tenchu {
inline input::Pad AutoplayPad(int port,uint64_t elapsed,bool story_started=false) {
    input::Pad pad{};
    if(port!=0)return pad;
    pad.connected=true;
    if(elapsed<3000) {
        const auto phase=elapsed%1500;
        if(phase>=500&&phase<680)pad.buttons=1u<<4; // Start
    } else if(elapsed<120000 && (elapsed-3000)%1800<180) {
        pad.analog[0]=255; // A: title, default Story Mode, default character
    }
    // The title screen requires Start rather than A. Stop Start pulses as
    // soon as a real Story Mode task runs, so tests do not pause gameplay.
    if(!story_started && elapsed>=3000 && elapsed<120000 && elapsed%6000<180)
        pad.buttons=1u<<4;
    return pad;
}
}
