// Real loopback HTTP/SOAP fixture: never discovers or modifies a real router.
#include "../src/common/online_router.cpp"
#include <atomic>
#include <cstdio>
#include <map>
#include <stdexcept>
namespace logx {void Write(Level,const char*,...){} }
using namespace tenchu::online::router;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct FakeRouter {
    SOCKET listener=INVALID_SOCKET;uint16_t port=0;
    std::atomic<bool> quit=false;std::thread worker;std::mutex lock;
    std::map<std::string,std::string> mappings;
    std::vector<std::string> actions;
    std::string deny;
    FakeRouter(){
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        Check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"mock bind");
        int size=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size);port=ntohs(address.sin_port);
        Check(listen(listener,8)==0,"mock listen");
        worker=std::thread([this]{Run();});
    }
    ~FakeRouter(){quit=true;if(worker.joinable())worker.join();closesocket(listener);}
    std::string Location(){return "http://127.0.0.1:"+std::to_string(port)+"/device.xml";}
    void Run(){
        while(!quit){
            fd_set f;FD_ZERO(&f);FD_SET(listener,&f);timeval timeout{0,100000};
            if(select(0,&f,nullptr,nullptr,&timeout)!=1)continue;
            const auto peer=accept(listener,nullptr,nullptr);if(peer==INVALID_SOCKET)continue;
            DWORD ms=1000;setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));
            std::string request;char buffer[2048];
            while(request.size()<65536){
                const int n=recv(peer,buffer,sizeof(buffer),0);if(n<=0)break;request.append(buffer,n);
                const auto split=request.find("\r\n\r\n");if(split==std::string::npos)continue;
                const auto length=Header(request.substr(0,split),"content-length");
                if(request.size()>=split+4+std::strtoul(length.c_str(),nullptr,10))break;
            }
            int status=200;std::string body;
            {
                std::lock_guard guard(lock);
                const auto action=Header(request,"soapaction");
                const auto key=Tag(request,"NewExternalPort")+"/"+Tag(request,"NewProtocol");
                actions.push_back(action+" "+key);
                auto fault=[&](int code){status=500;body="<errorCode>"+std::to_string(code)+"</errorCode>";};
                if(request.starts_with("GET "))body="<root><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>/control</controlURL></service></root>";
                else if(action.find("#GetExternalIPAddress")!=std::string::npos)body="<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>";
                else if(action.find("#GetSpecificPortMappingEntry")!=std::string::npos){
                    if(mappings.contains(key))body=mappings[key];else fault(714);
                }else if(action.find("#AddPortMapping")!=std::string::npos){
                    if(key==deny)fault(606);else{mappings[key]=request;body="<ok/>";}
                }else if(action.find("#DeletePortMapping")!=std::string::npos){mappings.erase(key);body="<ok/>";}
                else fault(401);
            }
            const auto response="HTTP/1.1 "+std::to_string(status)+" Result\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
            for(size_t at=0;at<response.size();){const int n=send(peer,response.data()+at,int(response.size()-at),0);if(n<=0)break;at+=n;}
            closesocket(peer);
        }
    }
};
int main(){try{
    WSADATA wsa;Check(WSAStartup(MAKEWORD(2,2),&wsa)==0,"winsock");
    Url url;Check(ParseUrl("http://127.0.0.1:1000/path",&url)&&url.port==1000&&url.path=="/path","URL parse");
    for(auto text:{"https://host/","http://host:65536/","http://host:-1/","http://host:abc/","http://host:0/","http:///"})Check(!ParseUrl(text,&url),"invalid router URL");
    Check(Tag("<x:NewInternalClient> 127.0.0.1 </x:NewInternalClient>","NewInternalClient")=="127.0.0.1","namespaced SOAP tags");
    Check(Dechunk("3\r\nabc\r\n2;ext=1\r\nde\r\n0\r\n\r\n")=="abcde","chunk extensions");
    Check(Dechunk("FFFF\r\nabc\r\n").empty()&&Dechunk("bad!\r\n").empty(),"truncated chunks");
    uint8_t request[20]={0,1,0,0,0x21,0x12,0xa4,0x42};for(int i=8;i<20;i++)request[i]=uint8_t(i);
    uint8_t reply[32]{};std::memcpy(reply,request,20);reply[0]=1;reply[1]=1;reply[3]=12;reply[21]=0x20;reply[23]=8;reply[25]=1;
    const uint32_t encoded=0xcb007107^0x2112a442;for(int i=0;i<4;i++)reply[28+i]=uint8_t(encoded>>(24-8*i));
    Check(StunAddress(reply,32,request)=="203.0.113.7","STUN XOR address");
    for(int n=0;n<32;n++)Check(StunAddress(reply,n,request).empty(),"truncated STUN");
    reply[8]^=1;Check(StunAddress(reply,32,request).empty(),"wrong STUN transaction");reply[8]^=1;
    reply[4]^=1;Check(StunAddress(reply,32,request).empty(),"wrong STUN cookie");reply[4]^=1;
    reply[23]=255;Check(StunAddress(reply,32,request).empty(),"overlong STUN attribute");
    Check(IsPrivate("100.64.0.1")&&IsPrivate("192.168.1.1")&&!IsPrivate("203.0.113.7"),"CGNAT distinction");
    tenchu::online::Settings settings;settings.port=49123;tenchu::online::Configure(settings);
    FakeRouter server;Router router;
    Check(FindService(server.Location(),&router)&&router.control.path=="/control","real HTTP router description");
    {std::lock_guard guard(g_mutex);g_status.local_ip="127.0.0.1";}
    std::string external;
    Check(SetupRouter(router,"127.0.0.1",&external).empty()&&external=="203.0.113.7","real SOAP setup");
    {std::lock_guard guard(server.lock);Check(server.mappings.size()==4&&server.mappings.contains("1000/UDP")&&server.mappings.contains("1001/UDP")&&server.mappings.contains("1002/TCP")&&server.mappings.contains("49123/UDP"),"RFD ports and configurable discovery port");}
    Stop();{std::lock_guard guard(server.lock);Check(server.mappings.empty(),"Stop removes owned mappings");}
    // A manual forward, even to this PC, must never be replaced or deleted.
    {std::lock_guard guard(server.lock);server.mappings["1000/UDP"]="<NewInternalClient>127.0.0.1</NewInternalClient><NewInternalPort>1000</NewInternalPort><NewPortMappingDescription>Manual rule</NewPortMappingDescription>";server.deny="1001/UDP";}
    {std::lock_guard guard(g_mutex);g_status.local_ip="127.0.0.1";}
    Check(!SetupRouter(router,"127.0.0.1",&external).empty(),"mapping conflicts reported");Stop();
    {std::lock_guard guard(server.lock);Check(server.mappings.size()==1&&server.mappings.at("1000/UDP").find("Manual rule")!=std::string::npos,"partial setup cleans only its own mappings");}
    {std::lock_guard guard(server.lock);server.deny.clear();}
    {std::lock_guard guard(g_mutex);g_status.local_ip="127.0.0.1";}
    Check(SetupRouter(router,"127.0.0.1",&external).empty(),"usable manual forward counts as ready without taking ownership");
    Stop();{std::lock_guard guard(server.lock);Check(server.mappings.size()==1&&server.mappings.contains("1000/UDP"),"usable manual forward survives cleanup");}
    // A rule changed after setup no longer belongs to this process.
    {std::lock_guard guard(server.lock);server.deny.clear();server.mappings.clear();}
    {std::lock_guard guard(g_mutex);g_status.local_ip="127.0.0.1";}
    Check(SetupRouter(router,"127.0.0.1",&external).empty(),"repeat setup");
    {std::lock_guard guard(server.lock);server.mappings["1001/UDP"]="<NewPortMappingDescription>Other app</NewPortMappingDescription>";}
    Stop();{std::lock_guard guard(server.lock);Check(server.mappings.size()==1&&server.mappings.contains("1001/UDP"),"cleanup preserves replaced rule");}
    std::puts("PASS: bounded STUN/HTTP parsing; mock-router RFD port setup, conflict handling and owned-rule cleanup.");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
