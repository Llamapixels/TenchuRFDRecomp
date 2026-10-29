#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core/diag.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "graphics/frame_readback.h"
#include "graphics/renderer_selection.h"
#include "port/port_settings.h"
#include <cstdlib>
bool TenchuDisplayMenuMessage(uint32_t,uintptr_t,intptr_t,intptr_t*);

void TenchuCaptureCameraFrame(int phase){
    tenchu::camera_capture_phase=phase;
    tenchu::capture_next_presentation=true;
}
bool TenchuDiagnosticMessage(void*,uint32_t msg,uintptr_t key,intptr_t flags,intptr_t* result){
    if(TenchuRendererMessage(msg,key,flags,result))return true;
    if(TenchuDisplayMenuMessage(msg,key,flags,result))return true;
    if(msg!=WM_KEYDOWN||key!=VK_F3)return false;
    if(GetKeyState(VK_CONTROL)&0x8000){
        if(!(flags&(intptr_t(1)<<30)))TenchuDumpThreads("Ctrl+F3");
        *result=0;return true;
    }
    // Each press writes about 20 MB of textures and draw data. Off unless
    // F6 > Tools enables it or XBE_CAPTURE_DIR is set explicitly.
    const char* env=std::getenv("XBE_CAPTURE_DIR");
    if(!g_tenchu_debug_capture&&!(env&&*env)){*result=0;return true;}
    if(diag::CaptureDir().empty())return false;
    if(!(flags&(intptr_t(1)<<30))){
        gpu::g_capture=true;
        tenchu::capture_next_presentation=true;
        LOGR("Render diagnostics: F3 requested three consecutive frames, textures and draw-state capture");
    }
    *result=0;
    return true;
}
