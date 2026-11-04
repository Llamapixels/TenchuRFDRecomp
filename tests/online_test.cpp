#include "port/online_service.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>
using namespace tenchu::online;
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Guest {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x400000);
    Environment Env(uint8_t instance){Environment e;e.base=bytes.data();e.arena=0x100000;e.valid=[this](uint32_t p,uint32_t n){return p>=0x10000&&uint64_t(p)+n<=bytes.size();};e.address[0]=127;e.address[3]=instance;return e;}
    void Put(uint32_t p,uint32_t v){std::memcpy(bytes.data()+p,&v,4);}
    uint32_t Get(uint32_t p){uint32_t v;std::memcpy(&v,bytes.data()+p,4);return v;}
};
int main(){try{
    std::string ip;
    Check(JoinCode("127.0.0.1")=="FW00-00F6"&&JoinCode("192.168.1.15")=="R2M0-23YW"&&JoinCode("203.0.113.7")=="SC07-21WQ","Tenchu Z join-code format compatibility fixtures");
    Check(ResolveHost(JoinCode("127.0.0.1"),ip)&&ip=="127.0.0.1","Join codes round trip");
    Check(!ResolveHost("garbage",ip)&&!ResolveHost("999.1.2.3",ip)&&!ResolveHost("0000-0000",ip),"Invalid hosts rejected");
    Check(ValidName("Rikimaru")&&!ValidName("\n")&&!ValidName(std::string(16,'a')),"Profile bounds");
    Session s;s.id[0]=7;s.key[0]=3;s.address[0]=127;s.address[3]=1;s.slots={1,1,0,0};s.attributes={{0x100002,0,u"Rikimaru"},{3,42,{}},{4,7,{}}};
    const auto wire=EncodeSession(s);Session restored;
    Check(DecodeSession(wire,restored)&&restored.id==s.id&&restored.attributes[0].text==u"Rikimaru","Bounded session serialization");
    for(size_t n=0;n<wire.size();n++)Check(!DecodeSession({wire.begin(),wire.begin()+n},restored),"Truncated session rejected");
    auto extra=wire;extra.push_back(0);Check(!DecodeSession(extra,restored),"Trailing session data rejected");
    auto huge=s;huge.attributes[0].text=std::u16string(33,u'x');Check(EncodeSession(huge).empty(),"Oversized session fields rejected");
    const uint16_t port=uint16_t(49000+(std::chrono::steady_clock::now().time_since_epoch().count()%1000));
    Settings config;config.name="Rikimaru";config.player_id=0x123456789abcdef;config.port=port;Configure(config);
    const auto test_root=std::filesystem::current_path()/"test_artifacts"/"online"/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(test_root);
    Check(ResolveHost(" \r\n"+JoinCode("192.168.1.15")+" \t",ip)&&ip=="192.168.1.15","Pasted codes tolerate whitespace");
    Check(ResolveHost(" 127.0.0.1 \n",ip)&&ip=="127.0.0.1","Pasted IPs tolerate whitespace");
    Check(ValidBindAddress("")&&ValidBindAddress("192.168.1.2")&&!ValidBindAddress("0.0.0.0")&&!ValidBindAddress("224.1.1.1"),"Bind address validation");
    SetRuntimeAddress("127.0.0.2");Check(RuntimeAddress()=="127.0.0.2","Runtime address for code display");
    config.bind_ip="192.168.1.2";config.router_setup=true;config.join=JoinCode("192.168.1.15");Configure(config);enabled=true;
    for(int n=1;n<=7;n++)RememberHost("192.168.1."+std::to_string(n));
    RememberHost("192.168.1.7");RememberHost("garbage");
    Check(Snapshot().recent.size()==5&&Snapshot().recent.front().host==JoinCode("192.168.1.7")&&!Snapshot().recent.front().date.empty(),"Recent hosts deduplicate and retain five newest");
    Save(test_root/"online.toml");Configure({});enabled=false;Load(test_root/"online.toml");
    const auto saved=Snapshot();Check(enabled&&saved.bind_ip==config.bind_ip&&saved.router_setup&&saved.port==port&&saved.join==config.join&&saved.player_id==config.player_id&&saved.recent.size()==5,"Settings and recent codes survive restart");
    ForgetHost(saved.recent.front().host);Check(Snapshot().recent.size()==4,"Forget recent host");
    {std::ofstream old(test_root/"legacy.toml");old<<"enabled = true\nname = 'Legacy'\njoin = '127.0.0.1'\nplayer_id = 123\ndirectory_port = 47626\n";}
    Load(test_root/"legacy.toml");Check(Snapshot().name=="Legacy"&&Snapshot().player_id==123&&!Snapshot().router_setup&&Snapshot().bind_ip.empty()&&Snapshot().recent.empty(),"Old settings remain compatible and do not opt in to router changes");
    config.bind_ip.clear();config.router_setup=false;config.join.clear();Configure(config);
    Guest h,c;auto host_env=h.Env(1);host_env.stats_file=test_root/"stats.bin";Services host(host_env),client(c.Env(2));
    auto call=[](Services& svc,Api api,std::initializer_list<uint32_t> args){return svc.Call(api,{args.begin(),args.size()});};
    Check(call(host,Api::Users,{0x10000,0x11000})==0&&h.Get(0x11000)==1,"Local account exists");
    Check(std::string(reinterpret_cast<char*>(h.bytes.data()+0x1000c))=="Rikimaru","Account name at native offset 12");
    Check(call(host,Api::Users,{0xfffffff0,0x11000})==0x80070057,"Invalid guest buffers rejected");
    Check(call(host,Api::Logon,{0x10000,0,0,0,0x11000})==0,"Host logon starts without Xbox Live");auto lh=h.Get(0x11000);
    Check(call(host,Api::Continue,{lh})==0&&call(host,Api::Continue,{lh})==0x1510f0,"Native logon pending/completion codes");
    Check(call(host,Api::LogonResult,{lh})==0x1510f0,"Logon results status");
    h.Put(0x18000,1);h.Put(0x1800c,1);h.Put(0x18010,1);h.Put(0x18014,0x18100);h.Put(0x18100,7);
    Check(call(host,Api::StatsRead,{1,0x18000,0,0x18200})==0,"Profile statistics read always creates a real task");const auto stats_task=h.Get(0x18200);
    Check(call(host,Api::Continue,{stats_task})==0&&call(host,Api::Continue,{stats_task})==1,"Profile statistics read finishes");
    Check(call(host,Api::StatsResults,{stats_task,1,0x18000,0,0})==0x80070490,"New profile reports no record through the native handled path");
    h.Put(0x18104,1);h.Put(0x18108,1234);
    Check(call(host,Api::StatsWrite,{1,0x18000,0,0x18300})==0,"Local statistics write creates a task");h.Put(0x18108,0);
    Check(call(host,Api::StatsResults,{stats_task,1,0x18000,0,0})==0&&h.Get(0x18108)==1234,"Local profile values are returned through native output buffers");
    {Services restarted(host_env);h.Put(0x18108,0);Check(call(restarted,Api::StatsRead,{1,0x18000,0,0x18400})==0,"Saved statistics can be read after restart");Check(call(restarted,Api::StatsResults,{h.Get(0x18400),1,0x18000,0,0})==0&&h.Get(0x18108)==1234,"Local statistics survive service restart");}
    call(host,Api::Close,{stats_task});call(host,Api::Close,{h.Get(0x18300)});
    const auto logged=call(host,Api::LogonUsers,{});Check(logged==0x100000&&h.Get(logged)==uint32_t(config.player_id),"Logon results use valid guest storage");
    // Native attributes: id@0, owner@4, 64-bit value or string pointer@8, 16-byte stride.
    h.Put(0x12000,1);h.Put(0x12008,42);
    h.Put(0x12010,0x100002);h.Put(0x12018,0x13000);std::memcpy(h.bytes.data()+0x13000,u"Rikimaru",18);
    h.Put(0x12020,3);h.Put(0x12028,7);
    Check(call(host,Api::Create,{1,1,0,0,3,0x12000,0,0x14000})==0,"Native host session created");const auto creation=h.Get(0x14000);
    Check(call(host,Api::SessionResult,{creation,0x14100,0x14110})==0,"Session id/key provided");
    Check(call(host,Api::Close,{creation})==0&&call(host,Api::Continue,{creation})==0x80070057,"Closed task is invalid");
    Check(call(host,Api::Friends,{0,0x14000})==0&&call(host,Api::Continue,{creation})==0x80070057,"Reused task slots never revive stale handles");
    call(host,Api::Close,{h.Get(0x14000)});
    config.join=JoinCode("127.0.0.1");Configure(config);
    Check(call(client,Api::Logon,{0,0,0,0,0x11000})==0,"Client logon opens second loopback instance");
    Check(call(client,Api::Search,{3,25,0,0,0,0,0x14000})==0,"Client session search starts");const auto search=c.Get(0x14000);
    for(int i=0;i<200;i++){host.Pump();client.Pump();if(call(client,Api::Continue,{search})!=0)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    Check(call(client,Api::SearchResults,{search,0x14100,0x14104})==0&&c.Get(0x14104)==1,"Actual UDP directory roundtrip finds host");
    const auto record=c.Get(c.Get(0x14100));Check(c.Get(record+0xc)==0x0100007f,"Search returns reachable host XNADDR");
    Check(PeerSupportsPerFrameInput(0x0100007f),"Optional capability response enables per-frame input only for the discovered host");
    Check(c.Get(record+0x40)==1&&c.Get(record+0x44)==1&&c.Get(record+0x50)==3,"Native session header counts and slots");
    Check(std::memcmp(c.bytes.data()+record+4,h.bytes.data()+0x14100,8)==0&&std::memcmp(c.bytes.data()+record+0x30,h.bytes.data()+0x14110,16)==0,"Both peers agree on session id/key");
    // Exact descriptor at game address004067E0: u64, WCHAR[17], u64 =50bytes.
    c.Put(0x15000,0);c.Put(0x15004,8);c.Put(0x15008,0x100000);c.Put(0x1500c,34);c.Put(0x15010,0);c.Put(0x15014,8);
    c.Put(0x16000+50,0xdeadbeef);
    Check(call(client,Api::Attributes,{record,3,0x15000,0x16000})==0,"Native packed result attributes decode");
    Check(c.Get(0x16000)==42&&std::u16string(reinterpret_cast<char16_t*>(c.bytes.data()+0x16008))==u"Rikimaru"&&c.Get(0x16000+42)==7,"Game's numeric/UTF-16/numeric output layout");
    Check(c.Get(0x16000+50)==0xdeadbeef,"Native attribute output must not overwrite the session ID at offset50");
    Check(call(client,Api::QosLookup,{1,0,0,0,0,0,0,0,0,0,0,0x17000})==0,"QoS compatibility result exists");const auto qos=c.Get(0x17000);
    Check(c.Get(qos)==1&&c.Get(qos+4)==0&&c.bytes[qos+8]==3,"QoS complete flags and zero pending");
    Check(call(client,Api::QosRelease,{qos})==0&&call(client,Api::QosRelease,{qos})!=0,"QoS release lifecycle");
    Check(call(host,Api::Delete,{0,0,0,0x18000})==0,"Session unpublish starts");
    // Task exhaustion is reported rather than returning a dangling success handle.
    size_t made=0;for(int i=0;i<40;i++)if(call(client,Api::Friends,{0,0x19000})==0)made++;
    Check(made>0&&made<32&&call(client,Api::Friends,{0,0x19000})==0x8007000e,"Bounded task allocation");
    std::puts("PASS: profiles, join codes, malformed packets/buffers, native task/attribute layouts, QoS lifecycle, real two-endpoint UDP discovery and matching session keys.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
