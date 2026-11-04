#include "xdk/hle.h"
#include "kernel/memory.h"
#include "port/online_service.h"
#include "core/log.h"
#include "app/app.h"
#include <array>
#include <cstring>
#include <mutex>

namespace {
std::unique_ptr<tenchu::online::Services> services;
std::mutex status_lock;
std::recursive_mutex service_lock;
std::atomic<bool> in_use{false};
std::string status="Online mode is disabled";
bool Valid(uint32_t p,uint32_t size){if(!p||uint64_t(p)+size>(uint64_t(1)<<32))return false;for(uint64_t a=p;a<uint64_t(p)+size;a=(a+0x1000)&~uint64_t(0xfff))if(!mem::IsCommitted(uint32_t(a)))return false;return true;}
uint32_t Dispatch(X86Context& ctx,uint8_t* base,tenchu::online::Api api,int count){
    using namespace tenchu::online;
    std::lock_guard guard(service_lock);
    if(api==Api::Logon)in_use=true;
    if(!services){
        const auto arena=mem::AllocSystem(Services::arena_size);if(!arena)return 0x8007000e;
        auto saved=ctx;
        const uint32_t scratch=arena+0x800;
        GUEST_CALL(XNetGetTitleXnAddr,ctx,{scratch});ctx=saved;
        Environment e;e.base=base;e.arena=arena;e.valid=Valid;e.stats_file=app::ExeDir()/"online_stats.bin";std::memcpy(e.address.data(),base+scratch,36);
        services=std::make_unique<Services>(std::move(e));
    }
    std::array<uint32_t,12> args{};
    if(!Valid(ctx.esp+4,uint32_t(count)*4))return 0x80070057;
    std::memcpy(args.data(),base+ctx.esp+4,size_t(count)*4);
    const uint32_t result=services->Call(api,{args.data(),size_t(count)});
    {std::lock_guard l(status_lock);status=services->Status();}
    if(api!=Api::Continue&&api!=Api::Presence&&api!=Api::Notification&&api!=Api::IsLoggedOn){
        static unsigned traced=0;
        if(traced++<80){LOGR("Online: API %08X caller %08X result %08X",uint32_t(api),kernel::R32(ctx.esp),result);logx::Flush();}
    }
    return result;
}
}
std::string TenchuOnlineStatus(){std::lock_guard l(status_lock);return status;}
bool TenchuOnlineInUse(){return in_use.load();}
#define ONLINE(address,api,nargs) \
    X86_EXTERN_FUNC(__imp__sub_##address); \
    X86_FUNC(sub_##address){if(!tenchu::online::enabled.load()){__imp__sub_##address(ctx,base);return;}const auto result=Dispatch(ctx,base,tenchu::online::Api::api,nargs);kernel::Return(ctx,result,nargs);}
ONLINE(003CB6A0,Continue,1)
ONLINE(003CB6AB,Close,1)
ONLINE(003CB6B6,Users,2)
ONLINE(003CB6C1,Logon,5)
ONLINE(003CB6D0,LogonResult,1)
ONLINE(003CB6DB,LogonUsers,0)
ONLINE(003CB6E6,ServiceInfo,2)
ONLINE(003CB6FC,Create,8)
ONLINE(003CB70B,Update,10)
ONLINE(003CB73B,SessionResult,3)
ONLINE(003CB746,Delete,4)
ONLINE(003CB764,Search,7)
ONLINE(003CB773,SearchResults,3)
ONLINE(003CB77E,Attributes,4)
ONLINE(003CB789,ResultSize,3)
ONLINE(003CB794,IsLoggedOn,1)
ONLINE(003CB79F,Presence,6)
ONLINE(003CB7C3,Notification,2)
ONLINE(003CB7CE,Friends,2)
ONLINE(003CB7D9,FriendsList,3)
ONLINE(003CB7EF,FriendsLatest,3)
ONLINE(003CB895,AccountChanges,1)
ONLINE(003E5538,QosListen,5)
ONLINE(003E5547,QosLookup,12)
ONLINE(003E5563,QosRelease,1)
ONLINE(003CB92E,StatsWrite,4)
ONLINE(003CB939,StatsRead,4)
ONLINE(003CB944,StatsResults,5)
