"""Exercise the actual SDK socket functions with real Winsock and guest memory.

Only unrelated application/kernel services are replaced. The mode bookkeeping,
accept and recv implementations are extracted unchanged from the runtime input.
Passing the original SDK file reproduces the lost-nonblocking-mode regression.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()

def block(start):
    begin = source.index(start)
    opening = source.index('{', begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]

prefix = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#define LOGI(...) ((void)0)
#define LOGR(...) ((void)0)
#define HLE_STDCALL(name, count) uint32_t hle_##name(const std::vector<uint32_t>& a)
namespace tenchu::online { std::atomic<bool> enabled{false}; }
namespace logx { void Flush() {} }
std::array<uint8_t, 4096> memory;
void* HostPtr(uint32_t p) { return memory.data()+p; }
uint32_t R32(uint32_t p) { uint32_t v;std::memcpy(&v,HostPtr(p),4);return v; }
void W32(uint32_t p,uint32_t v) { std::memcpy(HostPtr(p),&v,4); }
constexpr uint32_t kSocketError=0xffffffff, kUdpConnReset=0x9800000c;
thread_local uint32_t t_error=0;
void Check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
'''
code = prefix + block('struct Socket {') + ';\n'
code += '''std::mutex g_lock;
std::unordered_map<uint32_t, Socket> g_sockets;
uint32_t g_next_socket=0x100;
'''
for start in ('uint32_t Fail(', 'int HostError(', 'Socket Find(', 'sockaddr_in GuestAddr(', 'bool Wait(',
              'static uint32_t AddSocket(', 'HLE_STDCALL(ioctlsocket,',
              'HLE_STDCALL(accept,', 'HLE_STDCALL(recv,', 'HLE_STDCALL(connect,'):
    code += block(start) + '\n'
code += r'''
int main() { try {
    WSADATA wsa{};Check(WSAStartup(MAKEWORD(2,2),&wsa)==0,"Winsock startup");
    tenchu::online::enabled=true;
    const auto immediate=AddSocket(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP),false);
    int nodelay=0,nodelay_size=sizeof(nodelay);
    Check(::getsockopt(Find(immediate).s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&nodelay),&nodelay_size)==0&&nodelay==1,"Online TCP disables small-packet batching");
    ::closesocket(Find(immediate).s);tenchu::online::enabled=false;
    const SOCKET listener=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    Check(listener!=INVALID_SOCKET,"Listener created");
    const auto id=AddSocket(listener,false);
    Check(!Find(id).nonblocking,"New guest sockets keep their default blocking mode");
    W32(64,1);Check(hle_ioctlsocket({id,uint32_t(FIONBIO),64})==0,"Set listener FIONBIO");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    Check(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Bind listener");
    int size=sizeof(address);Check(::getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size)==0,"Get ephemeral port");
    Check(::listen(listener,2)==0,"Listen");
    for(int attempt=0;attempt<2;attempt++) {
        const SOCKET client=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        Check(::connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Connect client");
        const auto accepted=hle_accept({id,128,160});
        Check(accepted!=kSocketError,"Accept client");
        Check(Find(accepted).nonblocking,"Accepted socket must inherit guest FIONBIO; otherwise the game freezes in recv");
        const auto before=std::chrono::steady_clock::now();
        Check(hle_recv({accepted,256,8,0})==kSocketError&&t_error==WSAEWOULDBLOCK,"Empty receive yields to game loop");
        Check(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(250),"Empty receive returns promptly");
        Check(::send(client,"abc",3,0)==3,"Send partial header");
        Check(Wait(Find(accepted).s,false,1000),"Partial header arrives");
        Check(hle_recv({accepted,256,8,0})==3,"Receive preserves partial packet");
        Check(hle_recv({accepted,259,5,0})==kSocketError&&t_error==WSAEWOULDBLOCK,"Partial receive yields until remainder arrives");
        Check(::send(client,"defgh",5,0)==5,"Send remaining header");
        Check(Wait(Find(accepted).s,false,1000),"Remainder arrives");
        Check(hle_recv({accepted,259,5,0})==5&&!std::memcmp(HostPtr(256),"abcdefgh",8),"Packet reassembled without blocking the frame");
        ::closesocket(client);
        Check(Wait(Find(accepted).s,false,1000),"Peer closure arrives");
        Check(hle_recv({accepted,256,8,0})==0,"Stream EOF remains ordinary Winsock EOF");
        ::closesocket(Find(accepted).s);g_sockets.erase(accepted);
    }
    // Blocking listeners still produce blocking children; only the guest mode is inherited.
    W32(64,0);Check(hle_ioctlsocket({id,uint32_t(FIONBIO),64})==0,"Clear listener FIONBIO");
    const SOCKET client=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    Check(::connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Connect blocking child");
    const auto accepted=hle_accept({id,128,160});
    Check(accepted!=kSocketError&&!Find(accepted).nonblocking,"Blocking guest semantics preserved");
    ::closesocket(client);
    for(const auto& [key,value]:g_sockets)::closesocket(value.s);
    g_sockets.clear();

    // Real RFD pattern: the client reuses a fixed local TCP port after a match.
    // A client-initiated graceful close leaves that peer tuple in TIME_WAIT.
    tenchu::online::enabled=true;
    const SOCKET server=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_addr.s_addr=htonl(0x7f000001);
    Check(::bind(server,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==0,"Bind reconnect server");
    size=sizeof(destination);Check(::getsockname(server,reinterpret_cast<sockaddr*>(&destination),&size)==0,"Server port");
    Check(::listen(server,2)==0,"Listen for reconnects");
    sockaddr_in fixed{};fixed.sin_family=AF_INET;fixed.sin_addr.s_addr=htonl(0x7f000002);
    fixed.sin_port=destination.sin_port; // Both native sockets use the game port.
    for(int attempt=0;attempt<3;attempt++) {
        SOCKET raw=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        Check(::bind(raw,reinterpret_cast<sockaddr*>(&fixed),sizeof(fixed))==0,"Rebind client port after previous match");
        if(!attempt){size=sizeof(fixed);Check(::getsockname(raw,reinterpret_cast<sockaddr*>(&fixed),&size)==0,"Remember fixed source port");}
        const int on=1;Check(::setsockopt(raw,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&on),sizeof(on))==0,"Set guest TCP option");
        const auto guest=AddSocket(raw,false);
        W32(64,1);Check(hle_ioctlsocket({guest,uint32_t(FIONBIO),64})==0,"Nonblocking outbound socket");
        std::memcpy(HostPtr(512),&destination,sizeof(destination));
        const auto status=hle_connect({guest,512,sizeof(destination)});
        Check(status==0||(status==kSocketError&&t_error==WSAEWOULDBLOCK),"Immediate rejoin must start; fixed-port TIME_WAIT must not return10048");
        if(!Wait(Find(guest).s,true,1000)) {
            std::fprintf(stderr,"Fixed-port connect stalled on attempt %d (status=%X, guest error=%u)\n",attempt+1,status,t_error);
            Check(false,"Rejoined connection completes promptly");
        }
        int error=0;size=sizeof(error);Check(::getsockopt(Find(guest).s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)==0&&!error,"Rejoin has no pending connection error");
        Check(Wait(server,false,1000),"Host sees new connection");
        SOCKET remote=::accept(server,nullptr,nullptr);Check(remote!=INVALID_SOCKET,"Accept repeat connection");
        int nodelay=0;size=sizeof(nodelay);Check(::getsockopt(Find(guest).s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&nodelay),&size)==0&&nodelay==1,"Replacement preserves TCP options");
        if(attempt){sockaddr_in local{};size=sizeof(local);Check(::getsockname(Find(guest).s,reinterpret_cast<sockaddr*>(&local),&size)==0&&local.sin_port!=fixed.sin_port,"Outbound TCP uses an ephemeral source to prevent asynchronous fixed-port reuse failures");}
        Check(::send(Find(guest).s,"join",4,0)==4,"Send new join request");
        char request[4]{};Check(::recv(remote,request,4,MSG_WAITALL)==4&&!std::memcmp(request,"join",4),"Host receives new join request");
        Check(::shutdown(Find(guest).s,SD_SEND)==0,"Client starts clean match-end shutdown");
        char byte;Check(::recv(remote,&byte,1,0)==0,"Host sees client EOF");
        Check(::shutdown(remote,SD_SEND)==0&&Wait(Find(guest).s,false,1000),"Client sees host EOF");
        Check(hle_recv({guest,256,1,0})==0,"Guest EOF semantics preserved");
        ::closesocket(remote);::closesocket(Find(guest).s);g_sockets.erase(guest);
    }
    ::closesocket(server);
    WSACleanup();
    std::puts("PASS: actual HLE accept/recv/connect preserve modes and options, yield on partial packets, and rejoin immediately despite fixed-port TIME_WAIT.");
    return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; } }
'''
out = Path(sys.argv[2])
out.parent.mkdir(parents=True, exist_ok=True)
if not out.exists() or out.read_text() != code:
    out.write_text(code)
