#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tenchu::online {
inline std::atomic<bool> enabled{false};
struct RecentHost { std::string host, date; };
struct Settings {
    std::string name="Player", join, bind_ip;
    uint64_t player_id=0;
    uint16_t port=47626;
    bool router_setup=false;
    std::vector<RecentHost> recent;
};
Settings Snapshot();
void Configure(const Settings&);
void Load(const std::filesystem::path&);
void Save(const std::filesystem::path&);
bool ValidName(const std::string&);
std::string JoinCode(const std::string& ip);
bool ResolveHost(const std::string& text, std::string& ip);
bool ValidBindAddress(const std::string&);
void SetRuntimeAddress(const std::string&);
std::string RuntimeAddress();
bool PeerSupportsPerFrameInput(uint32_t network_order_ip);
std::vector<std::string> LocalAddresses();
void RememberHost(const std::string&);
void ForgetHost(const std::string&);
struct Attribute { uint32_t id=0; uint64_t value=0; std::u16string text; };
struct Session {
    std::array<uint8_t,8> id{};
    std::array<uint8_t,16> key{};
    std::array<uint8_t,36> address{};
    std::array<uint32_t,4> slots{};
    std::vector<Attribute> attributes;
};
// The directory exchanges session descriptors only. RFD's own protocol carries gameplay.
class Directory {
public:
    Directory(); ~Directory();
    bool Open(const std::string& local_ip, uint16_t port);
    void Close();
    void Publish(const Session&);
    void Unpublish();
    void Search(const std::string& target);
    void Pump();
    std::vector<Session> Results() const;
    std::string Status() const;
private:
    struct Impl; std::unique_ptr<Impl> p_;
};
std::vector<uint8_t> EncodeSession(const Session&);
bool DecodeSession(const std::vector<uint8_t>&, Session&);
} // namespace tenchu::online
