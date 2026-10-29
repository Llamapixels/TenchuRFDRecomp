#include "cpu/x86_ops.h"
#include "core/log.h"
#include "kernel/object_initialization.h"
#include <map>
#include <memory>
#include <mutex>

X86_EXTERN_FUNC(__imp__sub_002FAEE0);
X86_EXTERN_FUNC(__imp__sub_002FF830);
namespace {
std::mutex registry_lock;
std::map<uint32_t,std::shared_ptr<tenchu::ObjectInitialization>> objects;
}

// The base class starts its worker before the derived constructor initializes
// list heads at +0x48/+0x54. Parallel host guest threads exposed that window.
X86_FUNC(sub_002FAEE0) {
    const uint32_t object=ctx.ecx;
    auto state=std::make_shared<tenchu::ObjectInitialization>();
    { std::lock_guard lock(registry_lock); objects[object]=state; }
    try {
        __imp__sub_002FAEE0(ctx,base);
        LOGR("Audio worker initialization complete: %08X",object);
        state->Complete(true);
    } catch(...) {
        state->Complete(false);
        throw;
    }
}

X86_FUNC(sub_002FF830) {
    std::shared_ptr<tenchu::ObjectInitialization> state;
    { std::lock_guard lock(registry_lock); auto it=objects.find(ctx.ecx); if(it!=objects.end()) state=it->second; }
    if(!state) x86_trap(ctx,0x002FF830,"audio worker has no recorded object initialization");
    if(!state->Wait()) x86_trap(ctx,0x002FF830,"audio worker object construction failed");
    __imp__sub_002FF830(ctx,base);
}
