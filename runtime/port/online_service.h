#pragma once
#include "online.h"
#include <functional>
#include <span>

namespace tenchu::online {
// Verified x86 public thunks in this disc. Guest pointers never cross the wire.
enum class Api : uint32_t {
    Continue=0x3CB6A0, Close=0x3CB6AB, Users=0x3CB6B6,
    Logon=0x3CB6C1, LogonResult=0x3CB6D0, LogonUsers=0x3CB6DB, ServiceInfo=0x3CB6E6,
    Create=0x3CB6FC, Update=0x3CB70B, SessionResult=0x3CB73B, Delete=0x3CB746,
    Search=0x3CB764, SearchResults=0x3CB773, Attributes=0x3CB77E, ResultSize=0x3CB789,
    IsLoggedOn=0x3CB794, Presence=0x3CB79F, Notification=0x3CB7C3, Friends=0x3CB7CE, FriendsList=0x3CB7D9,
    FriendsLatest=0x3CB7EF, AccountChanges=0x3CB895,
    QosListen=0x3E5538, QosLookup=0x3E5547, QosRelease=0x3E5563
    , StatsWrite=0x3CB92E, StatsRead=0x3CB939, StatsResults=0x3CB944
};
struct Environment {
    uint8_t* base=nullptr;
    uint32_t arena=0;
    std::function<bool(uint32_t,uint32_t)> valid;
    std::array<uint8_t,36> address{};
    std::filesystem::path stats_file;
};
class Services {
public:
    static constexpr uint32_t arena_size=0x101000;
    explicit Services(Environment);
    ~Services();
    uint32_t Call(Api,std::span<const uint32_t>);
    void Pump();
    std::string Status()const;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
} // namespace tenchu::online
