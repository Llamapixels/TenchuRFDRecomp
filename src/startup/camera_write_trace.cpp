#include "cpu/x86_ops.h"
#include "game_camera.h"
#include "core/log.h"
#include <cstring>
#include <array>
X86_EXTERN_FUNC(__imp__sub_00233EC0);
X86_FUNC(sub_00233EC0){
    // Observation only: identify native pose copies that replace the override.
    // No guest register, stack or memory changes beyond the original function.
    if(tenchu::camera::free_active.load()){
        uint32_t dest,caller,cam;
        std::memcpy(&dest,base+ctx.esp+4,4);std::memcpy(&caller,base+ctx.esp,4);
        std::memcpy(&cam,base+0x008E38C0,4);
        if(dest==cam||dest==cam+0x40){
            static thread_local std::array<uint32_t,32> seen{};static thread_local unsigned count=0;
            bool found=false;for(unsigned i=0;i<count;++i)if(seen[i]==caller)found=true;
            if(!found&&count<seen.size()){
                seen[count++]=caller;
                LOGR("Camera overwrite trace: pose copy to %08X, caller return %08X",dest,caller);
            }
        }
    }
    __imp__sub_00233EC0(ctx,base);
}
