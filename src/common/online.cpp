#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "port/online.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <ctime>
#include <unordered_set>

namespace tenchu::online {
namespace {
std::mutex settings_lock;
Settings settings;
std::string runtime_address;
std::mutex capability_lock;std::unordered_set<uint32_t> per_frame_peers;
std::string Trim(std::string s){const auto first=s.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);}
constexpr char alphabet[]="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
uint8_t Check(uint32_t a){return uint8_t((a>>24)+3*((a>>16)&255)+7*((a>>8)&255)+13*(a&255)+0x5a);}
bool Ip(const std::string& s,uint32_t& address){in_addr a{};if(inet_pton(AF_INET,s.c_str(),&a)!=1)return false;address=a.s_addr;return address!=0&&address!=0xffffffffu;}
std::string Ip(uint32_t a){in_addr v{};v.s_addr=a;char s[32];return inet_ntop(AF_INET,&v,s,sizeof(s))?s:"";}
uint64_t Random(){std::random_device r;return uint64_t(r())<<32|r();}
template<class T>void Put(std::vector<uint8_t>& b,T x){for(size_t i=0;i<sizeof(T);i++)b.push_back(uint8_t(x>>(i*8)));}
template<class T>bool Get(const std::vector<uint8_t>& b,size_t& at,T& x){if(at+sizeof(T)>b.size())return false;x=0;for(size_t i=0;i<sizeof(T);i++)x|=T(b[at++])<<(i*8);return true;}
constexpr uint32_t magic=0x4f444652, title=0x41560026;
using Clock=std::chrono::steady_clock;
}
bool PeerSupportsPerFrameInput(uint32_t ip){std::lock_guard guard(capability_lock);return per_frame_peers.contains(ip);}
Settings Snapshot(){std::lock_guard l(settings_lock);if(!settings.player_id)settings.player_id=Random()|1;return settings;}
bool ValidName(const std::string& s){return !s.empty()&&s.size()<=15&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32&&c<127;});}
bool ValidBindAddress(const std::string& s){uint32_t ip=0;return s.empty()||(Ip(s,ip)&&(ntohl(ip)>>24)<224);}
void SetRuntimeAddress(const std::string& ip){std::lock_guard l(settings_lock);runtime_address=ip;}
std::string RuntimeAddress(){std::lock_guard l(settings_lock);return runtime_address;}
std::vector<std::string> LocalAddresses(){
    static std::once_flag once;std::call_once(once,[]{WSADATA w;WSAStartup(MAKEWORD(2,2),&w);});
    std::vector<std::string> out;char host[256]{};addrinfo hints{},*list=nullptr;hints.ai_family=AF_INET;
    if(gethostname(host,sizeof(host))||getaddrinfo(host,nullptr,&hints,&list))return out;
    for(auto* a=list;a;a=a->ai_next){auto ip=reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr.s_addr;const auto text=Ip(ip);if((ntohl(ip)>>24)!=127&&std::find(out.begin(),out.end(),text)==out.end())out.push_back(text);}
    freeaddrinfo(list);return out;
}
void RememberHost(const std::string& text){std::string ip;if(!ResolveHost(text,ip))return;const auto code=JoinCode(ip);std::lock_guard l(settings_lock);std::erase_if(settings.recent,[&](const RecentHost& h){return h.host==code;});std::time_t now=std::time(nullptr);std::tm local{};localtime_s(&local,&now);char date[16]{};std::strftime(date,sizeof(date),"%Y-%m-%d",&local);settings.recent.insert(settings.recent.begin(),{code,date});if(settings.recent.size()>5)settings.recent.resize(5);}
void ForgetHost(const std::string& host){std::lock_guard l(settings_lock);std::erase_if(settings.recent,[&](const RecentHost& h){return h.host==host;});}
void Configure(const Settings& s){std::lock_guard l(settings_lock);settings=s;settings.join=Trim(settings.join);settings.bind_ip=Trim(settings.bind_ip);if(!ValidBindAddress(settings.bind_ip))settings.bind_ip.clear();if(!ValidName(settings.name))settings.name="Player";if(!settings.player_id)settings.player_id=Random()|1;if(!settings.port)settings.port=47626;std::erase_if(settings.recent,[](const RecentHost& h){std::string ip;return !ResolveHost(h.host,ip);});if(settings.recent.size()>5)settings.recent.resize(5);}
void Load(const std::filesystem::path& path){try{
    auto t=toml::parse_file(path.string());Settings s;s.name=t["name"].value_or(std::string("Player"));s.join=t["join"].value_or(std::string());s.bind_ip=t["bind_ip"].value_or(std::string());s.router_setup=t["router_setup"].value_or(false);s.player_id=uint64_t(t["player_id"].value_or(int64_t(0)));const auto port=t["directory_port"].value_or(47626);s.port=port>0&&port<65536?uint16_t(port):47626;
    if(auto* recent=t["recent_hosts"].as_array())for(auto& node:*recent){if(auto* item=node.as_table())s.recent.push_back({(*item)["host"].value_or(std::string()),(*item)["date"].value_or(std::string())});if(s.recent.size()==5)break;}
    Configure(s);enabled=t["enabled"].value_or(false);
}catch(...){Configure({});enabled=false;}}
void Save(const std::filesystem::path& path){auto s=Snapshot();auto temp=path;temp+=".tmp";toml::array recent;for(const auto& host:s.recent)recent.push_back(toml::table{{"host",host.host},{"date",host.date}});std::ofstream f(temp);f<<toml::table{{"enabled",enabled.load()},{"name",s.name},{"join",s.join},{"player_id",int64_t(s.player_id)},{"directory_port",s.port},{"bind_ip",s.bind_ip},{"router_setup",s.router_setup},{"recent_hosts",std::move(recent)}};f.close();if(f)MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);}
std::string JoinCode(const std::string& ip){uint32_t address;if(!Ip(Trim(ip),address))return {};address=ntohl(address);uint64_t value=uint64_t(address)<<8|Check(address);std::string s(8,'0');for(int i=7;i>=0;i--){s[i]=alphabet[value&31];value>>=5;}s.insert(4,"-");return s;}
bool ResolveHost(const std::string& input,std::string& ip){const auto text=Trim(input);uint32_t a;if(Ip(text,a)){ip=Ip(a);return true;}uint64_t v=0;int n=0;for(unsigned char c:text){if(c=='-'||c==' ')continue;c=uint8_t(std::toupper(c));if(c=='O')c='0';if(c=='I'||c=='L')c='1';auto p=std::strchr(alphabet,c);if(!c||!p||++n>8)return false;v=(v<<5)|uint64_t(p-alphabet);}a=uint32_t(v>>8);if(n!=8||Check(a)!=uint8_t(v)||!a||a==0xffffffffu)return false;ip=Ip(htonl(a));return true;}
std::vector<uint8_t> EncodeSession(const Session& s){if(s.attributes.size()>16)return {};std::vector<uint8_t>b;for(auto x:s.id)b.push_back(x);for(auto x:s.key)b.push_back(x);for(auto x:s.address)b.push_back(x);for(auto x:s.slots)Put(b,x);Put(b,uint32_t(s.attributes.size()));for(const auto& a:s.attributes){if(a.text.size()>32)return {};Put(b,a.id);Put(b,a.value);Put(b,uint32_t(a.text.size()));for(auto c:a.text)Put(b,uint16_t(c));}return b;}
bool DecodeSession(const std::vector<uint8_t>& b,Session& s){if(b.size()<80||b.size()>2048)return false;Session v;size_t at=0;std::memcpy(v.id.data(),b.data(),8);at+=8;std::memcpy(v.key.data(),b.data()+at,16);at+=16;std::memcpy(v.address.data(),b.data()+at,36);at+=36;for(auto& n:v.slots)if(!Get(b,at,n)||n>4)return false;uint32_t count;if(!Get(b,at,count)||count>16)return false;for(uint32_t i=0;i<count;i++){Attribute a;uint32_t len;if(!Get(b,at,a.id)||!Get(b,at,a.value)||!Get(b,at,len)||len>32)return false;for(uint32_t j=0;j<len;j++){uint16_t c;if(!Get(b,at,c))return false;a.text.push_back(char16_t(c));}v.attributes.push_back(std::move(a));}if(at!=b.size())return false;s=std::move(v);return true;}
struct Directory::Impl {
    mutable std::mutex lock;
    SOCKET socket=INVALID_SOCKET;
    std::string local,status="Not connected";
    uint16_t port=0;
    Session session;bool published=false;
    uint64_t nonce=0;
    Clock::time_point searched{};
    std::vector<Session> results;
};
Directory::Directory():p_(std::make_unique<Impl>()){}
Directory::~Directory(){Close();}
bool Directory::Open(const std::string& local,uint16_t port){Close();std::lock_guard l(p_->lock);static std::once_flag once;std::call_once(once,[]{WSADATA w;WSAStartup(MAKEWORD(2,2),&w);});uint32_t address;if(!Ip(local,address))return false;SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET){p_->status="Cannot create session socket";return false;}sockaddr_in at{};at.sin_family=AF_INET;at.sin_addr.s_addr=address;at.sin_port=htons(port);if(bind(s,reinterpret_cast<sockaddr*>(&at),sizeof(at))!=0){p_->status="Session port is busy ("+std::to_string(WSAGetLastError())+")";closesocket(s);return false;}u_long nonblocking=1;ioctlsocket(s,FIONBIO,&nonblocking);BOOL broadcast=TRUE;setsockopt(s,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&broadcast),sizeof(broadcast));p_->socket=s;p_->local=local;p_->port=port;p_->status="Ready on "+local+":"+std::to_string(port);return true;}
void Directory::Close(){std::lock_guard l(p_->lock);if(p_->socket!=INVALID_SOCKET)closesocket(p_->socket);p_->socket=INVALID_SOCKET;p_->published=false;p_->results.clear();p_->nonce=0;p_->status="Not connected";}
void Directory::Publish(const Session& s){std::lock_guard l(p_->lock);p_->session=s;p_->published=true;p_->status="Hosting on "+p_->local+":"+std::to_string(p_->port);}
void Directory::Unpublish(){std::lock_guard l(p_->lock);p_->published=false;}
void Directory::Search(const std::string& target){std::lock_guard l(p_->lock);{std::lock_guard guard(capability_lock);per_frame_peers.clear();}if(p_->socket==INVALID_SOCKET)return;p_->nonce=Random();p_->searched=Clock::now();p_->results.clear();std::vector<uint8_t>b;Put(b,magic);Put(b,title);Put(b,uint32_t(1));Put(b,uint32_t(1));Put(b,p_->nonce);std::vector<std::string> targets;if(target.empty()){if(p_->local.starts_with("127."))for(int i=1;i<=8;i++)targets.push_back("127.0.0."+std::to_string(i));else targets.push_back("255.255.255.255");}else{std::string ip;if(ResolveHost(target,ip))targets.push_back(ip);else{p_->status="Invalid host IP or join code";return;}}for(const auto& ip:targets){if(ip==p_->local)continue;sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(p_->port);inet_pton(AF_INET,ip.c_str(),&to.sin_addr);sendto(p_->socket,reinterpret_cast<const char*>(b.data()),int(b.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof(to));}p_->status="Searching for a host";}
void Directory::Pump(){std::lock_guard l(p_->lock);if(p_->socket==INVALID_SOCKET)return;for(int i=0;i<32;i++){std::array<uint8_t,4096> buffer{};sockaddr_in peer{};int len=sizeof(peer);int got=recvfrom(p_->socket,reinterpret_cast<char*>(buffer.data()),int(buffer.size()),0,reinterpret_cast<sockaddr*>(&peer),&len);if(got<0)break;std::vector<uint8_t>b(buffer.begin(),buffer.begin()+got);size_t at=0;uint32_t m,t,v,type;uint64_t nonce;if(!Get(b,at,m)||!Get(b,at,t)||!Get(b,at,v)||!Get(b,at,type)||!Get(b,at,nonce)||m!=magic||t!=title||v!=1)continue;if(type==1&&at==b.size()&&p_->published){auto payload=EncodeSession(p_->session);if(payload.empty())continue;b.resize(at);b[12]=2;b.insert(b.end(),payload.begin(),payload.end());sendto(p_->socket,reinterpret_cast<const char*>(b.data()),int(b.size()),0,reinterpret_cast<sockaddr*>(&peer),len);
// Separate optional capability response; old directories ignore unknown types.
b.resize(at);b[12]=4;b.insert(b.end(),p_->session.id.begin(),p_->session.id.end());
sendto(p_->socket,reinterpret_cast<const char*>(b.data()),int(b.size()),0,reinterpret_cast<sockaddr*>(&peer),len);
}else if(type==4&&nonce==p_->nonce&&Clock::now()-p_->searched<std::chrono::seconds(5)&&b.size()==at+8){
for(const auto& session:p_->results)if(std::equal(session.id.begin(),session.id.end(),b.begin()+at)&&std::memcmp(session.address.data(),&peer.sin_addr.s_addr,4)==0){
std::lock_guard guard(capability_lock);per_frame_peers.insert(peer.sin_addr.s_addr);break;}
}else if(type==2&&nonce==p_->nonce&&Clock::now()-p_->searched<std::chrono::seconds(5)){Session s;if(!DecodeSession({b.begin()+at,b.end()},s))continue;std::memcpy(s.address.data(),&peer.sin_addr.s_addr,4);bool known=false;for(auto& old:p_->results)if(old.id==s.id){old=s;known=true;}if(!known&&p_->results.size()<25)p_->results.push_back(std::move(s));p_->status="Found "+std::to_string(p_->results.size())+" host(s)";}}}
std::vector<Session> Directory::Results()const{std::lock_guard l(p_->lock);return p_->results;}
std::string Directory::Status()const{std::lock_guard l(p_->lock);return p_->status;}
} // namespace tenchu::online
