#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "port/online_service.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <random>
#include <stdexcept>
#include <fstream>
#include <tuple>

namespace tenchu::online {
namespace {
constexpr uint32_t ok=0, complete=1, invalid=0x80070057, failed=0x80004005, no_memory=0x8007000e, logon_complete=0x001510f0;
using Clock=std::chrono::steady_clock;
struct BadPointer{};
}
struct Services::Impl {
    Environment e;
    Directory directory;
    bool ready=false;
    struct Task { bool used=false;Api kind{};uint32_t handle=0,polls=0;Clock::time_point start{};Session session;uint32_t buffer=0,count=0; };
    uint32_t next_generation=0;
    std::array<Task,32> tasks{};
    std::map<uint32_t,uint32_t> qos;
    Session hosted;
    using StatKey=std::tuple<uint64_t,uint32_t,uint32_t>;
    std::map<StatKey,std::array<uint8_t,16>> stats;
    explicit Impl(Environment v):e(std::move(v)){
        if(e.stats_file.empty())return;
        std::ifstream f(e.stats_file,std::ios::binary);uint32_t magic=0,count=0;
        f.read(reinterpret_cast<char*>(&magic),4);f.read(reinterpret_cast<char*>(&count),4);
        if(magic!=0x31545352||count>4096)return;
        for(uint32_t i=0;i<count;i++){uint64_t user;uint32_t board,id;std::array<uint8_t,16> value{};f.read(reinterpret_cast<char*>(&user),8);f.read(reinterpret_cast<char*>(&board),4);f.read(reinterpret_cast<char*>(&id),4);f.read(reinterpret_cast<char*>(value.data()),16);if(!f){stats.clear();return;}stats[{user,board,id}]=value;}
    }
    bool SaveStats(){if(e.stats_file.empty())return true;auto temp=e.stats_file;temp+=".tmp";std::ofstream f(temp,std::ios::binary);const uint32_t magic=0x31545352,count=uint32_t(stats.size());f.write(reinterpret_cast<const char*>(&magic),4);f.write(reinterpret_cast<const char*>(&count),4);for(const auto& [key,value]:stats){const auto& [user,board,id]=key;f.write(reinterpret_cast<const char*>(&user),8);f.write(reinterpret_cast<const char*>(&board),4);f.write(reinterpret_cast<const char*>(&id),4);f.write(reinterpret_cast<const char*>(value.data()),16);}f.close();if(!f)return false;return MoveFileExW(temp.c_str(),e.stats_file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
    uint8_t* At(uint32_t p,uint32_t size){if(!p||!e.valid||!e.valid(p,size))throw BadPointer{};return e.base+p;}
    uint32_t Read(uint32_t p){uint32_t v;std::memcpy(&v,At(p,4),4);return v;}
    void Write(uint32_t p,uint32_t v){std::memcpy(At(p,4),&v,4);}
    void Copy(uint32_t p,const void* data,uint32_t size){if(size)std::memcpy(At(p,size),data,size);}
    Task* Find(uint32_t h){if((h&0xff000000)!=0x7a000000)return nullptr;auto& t=tasks[h&31];return t.used&&t.handle==h?&t:nullptr;}
    uint32_t Buffer(const Task& t){return e.arena+0x1000+uint32_t(&t-tasks.data())*0x8000;}
    uint32_t Create(Api kind,uint32_t out){At(out,4);for(size_t i=0;i<tasks.size();i++)if(!tasks[i].used){auto& t=tasks[i];t={};t.used=true;t.kind=kind;t.start=Clock::now();const auto h=0x7a000000|((++next_generation&0x7ffff)<<5)|uint32_t(i);t.handle=h;Write(out,h);return ok;}Write(out,0);return no_memory;}
    void Users(uint32_t p,bool all=false){const uint32_t size=all?0x1c0:0x70;std::memset(At(p,size),0,size);const auto settings=Snapshot();Copy(p,&settings.player_id,8);Copy(p+12,settings.name.c_str(),uint32_t(settings.name.size()+1));}
    std::vector<Attribute> ReadAttributes(uint32_t ptr,uint32_t count){if(count>16)throw BadPointer{};std::vector<Attribute> out;for(uint32_t i=0;i<count;i++){
        const uint32_t at=ptr+i*16;Attribute a;a.id=Read(at);const uint32_t low=Read(at+8),high=Read(at+12);a.value=uint64_t(high)<<32|low;
        if((a.id&0x00f00000)==0x00100000){for(uint32_t j=0;j<32;j++){uint16_t c;std::memcpy(&c,At(low+j*2,2),2);if(!c)break;a.text.push_back(char16_t(c));}}
        out.push_back(std::move(a));}return out;}
    uint32_t Results(Task& t){if(t.buffer)return ok;const auto sessions=directory.Results();const uint32_t start=Buffer(t);uint32_t next=start+100;
        // A record is packed exactly as RFD reads it: length, id@4, XNADDR@C,
        // key@30, public/private counts@40..4C, attributes@50.
        for(size_t k=0;k<sessions.size();k++){
            const auto& s=sessions[k];const uint32_t record=next;const uint32_t text_start=record+0x54+uint32_t(s.attributes.size())*4;uint32_t text=text_start;
            for(const auto& a:s.attributes)text+=(a.id&0xf00000)==0x100000?uint32_t(7+a.text.size()):12;
            if(text>start+0x8000)return no_memory;
            std::memset(At(record,text-record),0,text-record);Write(start+uint32_t(k)*4,record);Write(record,text-record);
            Copy(record+4,s.id.data(),8);Copy(record+12,s.address.data(),36);Copy(record+48,s.key.data(),16);Copy(record+64,s.slots.data(),16);
            Write(record+0x50,uint32_t(s.attributes.size()));
            uint32_t dest=text_start;
            for(size_t j=0;j<s.attributes.size();j++){const auto& a=s.attributes[j];Write(record+0x54+uint32_t(j)*4,dest-record);Write(dest,a.id&0xf00000);if((a.id&0xf00000)==0x100000){uint16_t size=uint16_t(a.text.size()+1);Copy(dest+4,&size,2);for(size_t c=0;c<a.text.size();c++)At(dest+6+uint32_t(c),1)[0]=a.text[c]<128?uint8_t(a.text[c]):'?';dest+=6+size;}else{Copy(dest+4,&a.value,8);dest+=12;}}
            next=(text+7)&~7u;
        }
        t.buffer=start;t.count=uint32_t(sessions.size());return ok;
    }
    bool Open(){if(ready)return true;uint32_t ip;std::memcpy(&ip,e.address.data(),4);const std::string address=std::to_string(ip&255)+"."+std::to_string((ip>>8)&255)+"."+std::to_string((ip>>16)&255)+"."+std::to_string(ip>>24);ready=directory.Open(address,Snapshot().port);return ready;}
};
Services::Services(Environment e):p_(std::make_unique<Impl>(std::move(e))){}
Services::~Services()=default;
void Services::Pump(){p_->directory.Pump();}
std::string Services::Status()const{return p_->directory.Status();}
uint32_t Services::Call(Api api,std::span<const uint32_t> args){auto& p=*p_;auto arg=[&](size_t i){if(i>=args.size())throw BadPointer{};return args[i];};try{
    p.directory.Pump();
    switch(api){
    case Api::Users:p.Users(arg(0));p.Write(arg(1),1);return ok;
    case Api::AccountChanges:p.Write(arg(0),0);return ok;
    case Api::LogonUsers:p.Users(p.e.arena,true);return p.e.arena;
    case Api::Logon:{if(!p.Open())return failed;p.Users(p.e.arena,true);return p.Create(api,arg(4));}
    case Api::LogonResult:return p.Find(arg(0))?logon_complete:invalid;
    case Api::ServiceInfo:p.Write(arg(1),arg(0));p.Copy(arg(1)+4,p.e.address.data(),4);p.Write(arg(1)+8,0);return ok;
    case Api::Continue:{auto* t=p.Find(arg(0));if(!t)return invalid;if(t->polls++==0)return ok;
        if(t->kind==Api::Search&&p.directory.Results().empty()&&Clock::now()-t->start<std::chrono::seconds(2))return ok;
        return t->kind==Api::Logon?logon_complete:complete;}
    case Api::Close:{auto* t=p.Find(arg(0));if(!t)return invalid;*t={};return ok;}
    case Api::Create:case Api::Update:{const bool update=api==Api::Update;const size_t offset=update?2:0;
        Session s=p.hosted;if(update){const uint64_t id=uint64_t(arg(1))<<32|arg(0);std::memcpy(s.id.data(),&id,8);}else{std::random_device r;for(auto& b:s.id)b=uint8_t(r());s.id[0]&=15;for(auto& b:s.key)b=uint8_t(r());s.address=p.e.address;}
        for(int i=0;i<4;i++){s.slots[i]=arg(offset+i);if(s.slots[i]>4)return invalid;}
        s.attributes=p.ReadAttributes(arg(offset+5),arg(offset+4));const auto result=p.Create(api,arg(offset+7));if(result)return result;
        auto* task=p.Find(p.Read(arg(offset+7)));task->session=s;p.hosted=s;p.directory.Publish(s);return ok;}
    case Api::SessionResult:{auto* t=p.Find(arg(0));if(!t||(t->kind!=Api::Create&&t->kind!=Api::Update))return invalid;p.Copy(arg(1),t->session.id.data(),8);p.Copy(arg(2),t->session.key.data(),16);return ok;}
    case Api::Delete:{const auto r=p.Create(api,arg(3));if(!r)p.directory.Unpublish();return r;}
    case Api::ResultSize:return 25*1024;
    case Api::Search:{if(!p.Open())return failed;const auto r=p.Create(api,arg(6));if(!r)p.directory.Search(Snapshot().join);return r;}
    case Api::SearchResults:{auto* t=p.Find(arg(0));if(!t||t->kind!=Api::Search)return invalid;const auto r=p.Results(*t);if(r)return r;p.Write(arg(1),t->buffer);p.Write(arg(2),t->count);return ok;}
    case Api::Attributes:{const auto record=arg(0),count=arg(1);if(count>16)return invalid;const uint32_t length=p.Read(record),attrs=p.Read(record+0x50);if(length<0x54||length>0x8000||attrs>16||0x54+attrs*4>length)return invalid;uint32_t out=arg(3);
        for(uint32_t i=0;i<count;i++){const auto type=p.Read(arg(2)+i*8),size=type?p.Read(arg(2)+i*8+4):8;if(size>1024)return invalid;std::memset(p.At(out,size),0,size);if(i<attrs){const auto offset=p.Read(record+0x54+i*4);if(offset+4>length)return invalid;const auto at=record+offset;
            if(type==0&&p.Read(at)==0){if(offset+12>length)return invalid;p.Copy(out,p.At(at+4,8),8);}else if(type==0x100000&&p.Read(at)==type){uint16_t n;std::memcpy(&n,p.At(at+4,2),2);if(offset+6+n>length||size%2)return invalid;
                // Native XOnlineMatchSearchGetAttributes converts the wire UTF-8
                // string to WCHARs. Its descriptor size is bytes (34 = WCHAR[17]).
                // This directory emits ASCII names, a strict subset of UTF-8.
                const auto chars=std::min(uint32_t(n),size>=2?size/2-1:0u);
                for(uint32_t j=0;j<chars;j++){const uint16_t c=*p.At(at+6+j,1);p.Copy(out+j*2,&c,2);if(!c)break;}
            }}
            out+=size;}return ok;}
    case Api::IsLoggedOn:return arg(0)==0;
    case Api::Presence:return 1; // BOOL state publication, consumed as optional presence only
    case Api::Notification:return 0; // no original Live invitations/notifications
    case Api::Friends:return p.Create(api,arg(1));
    case Api::FriendsList:return p.Create(api,arg(2));
    case Api::FriendsLatest:return 0; // empty optional list; caller uses returned count
    case Api::StatsRead:return p.Create(api,arg(3));
    case Api::StatsWrite:{const auto users=arg(0),records=arg(1);if(users>4)return invalid;
        for(uint32_t i=0;i<users;i++){const uint32_t at=records+i*24;uint64_t user;std::memcpy(&user,p.At(at,8),8);const uint32_t board=p.Read(at+12),count=p.Read(at+16),values=p.Read(at+20);if(count>64||p.stats.size()+count>4096)return invalid;
            for(uint32_t j=0;j<count;j++){const auto v=values+j*16;const auto id=p.Read(v);std::array<uint8_t,16> value;std::memcpy(value.data(),p.At(v,16),16);p.stats[{user,board,id}]=value;}}
        if(!p.SaveStats())return failed;return p.Create(api,arg(3));}
    case Api::StatsResults:{const auto* task=p.Find(arg(0));if(!task||task->kind!=Api::StatsRead)return invalid;const auto users=arg(1),records=arg(2);if(users>4)return invalid;
        for(uint32_t i=0;i<users;i++){const uint32_t at=records+i*24;uint64_t user;std::memcpy(&user,p.At(at,8),8);const uint32_t board=p.Read(at+12),count=p.Read(at+16),values=p.Read(at+20);if(count>64)return invalid;
            for(uint32_t j=0;j<count;j++){const auto out=values+j*16;const auto found=p.stats.find({user,board,p.Read(out)});if(found==p.stats.end())return 0x80070490;p.Copy(out,found->second.data(),16);}}
        return ok;}
    case Api::QosListen:return ok;
    case Api::QosLookup:{const auto count=arg(0);if(count>25)return invalid;const auto out=arg(11);const auto r=p.Create(api,out);if(r)return r;auto* t=p.Find(p.Read(out));const auto buffer=p.Buffer(*t);std::memset(p.At(buffer,8+count*24),0,8+count*24);p.Write(buffer,count);for(uint32_t i=0;i<count;i++){p.At(buffer+8+i*24,1)[0]=3;p.Write(buffer+8+i*24+12,1);p.Write(buffer+8+i*24+16,1);}p.qos[buffer]=p.Read(out);p.Write(out,buffer);return ok;}
    case Api::QosRelease:{auto i=p.qos.find(arg(0));if(i==p.qos.end())return invalid;auto* t=p.Find(i->second);if(t)*t={};p.qos.erase(i);return ok;}
    }
    return invalid;
}catch(const BadPointer&){return invalid;}catch(...){return failed;}}
} // namespace tenchu::online
