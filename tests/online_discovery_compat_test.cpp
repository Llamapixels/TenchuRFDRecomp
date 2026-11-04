#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "port/online.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace tenchu::online;
int main(){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    WSADATA wsa;check(WSAStartup(MAKEWORD(2,2),&wsa)==0,"Winsock");
    SOCKET host=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);sockaddr_in at{};at.sin_family=AF_INET;inet_pton(AF_INET,"127.0.0.1",&at.sin_addr);
    check(bind(host,reinterpret_cast<sockaddr*>(&at),sizeof(at))==0,"legacy host bind");int length=sizeof(at);getsockname(host,reinterpret_cast<sockaddr*>(&at),&length);
    DWORD timeout=1000;setsockopt(host,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    Directory client;check(client.Open("127.0.0.2",ntohs(at.sin_port)),"client bind");
    Session session;session.id[0]=7;const auto descriptor=EncodeSession(session);
    std::vector<uint8_t> query(24);sockaddr_in peer{};int peer_size=sizeof(peer);
    auto search=[&](){client.Search("127.0.0.1");check(recvfrom(host,reinterpret_cast<char*>(query.data()),24,0,reinterpret_cast<sockaddr*>(&peer),&peer_size)==24,"actual directory search");};
    auto reply=[&](uint8_t type,const std::vector<uint8_t>& payload,bool wrong_nonce=false){auto bytes=query;bytes[12]=type;if(wrong_nonce)bytes[16]^=1;bytes.insert(bytes.end(),payload.begin(),payload.end());check(sendto(host,reinterpret_cast<const char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<sockaddr*>(&peer),peer_size)==int(bytes.size()),"reply");client.Pump();};
    const uint32_t ip=at.sin_addr.s_addr;std::vector<uint8_t> capability(session.id.begin(),session.id.end());
    search();reply(2,descriptor);
    check(client.Results().size()==1&&!PeerSupportsPerFrameInput(ip),"old host type2-only reply preserves legacy input");
    reply(4,capability,true);check(!PeerSupportsPerFrameInput(ip),"incorrect nonce rejected");
    auto other=capability;other[0]^=1;reply(4,other);check(!PeerSupportsPerFrameInput(ip),"different session capability rejected");
    auto short_cap=capability;short_cap.pop_back();reply(4,short_cap);check(!PeerSupportsPerFrameInput(ip),"truncated capability rejected");
    reply(4,capability);check(PeerSupportsPerFrameInput(ip),"matching nonce/session negotiates enhanced input");
    search();check(!PeerSupportsPerFrameInput(ip),"new search clears prior host capability");
    reply(4,capability);check(!PeerSupportsPerFrameInput(ip),"capability before matching descriptor falls back safely");
    reply(2,descriptor);check(!PeerSupportsPerFrameInput(ip),"legacy reply cannot inherit an earlier capability");
    reply(4,capability);check(PeerSupportsPerFrameInput(ip),"negotiation can resume after re-search");
    SOCKET stranger=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);sockaddr_in other_ip{};other_ip.sin_family=AF_INET;inet_pton(AF_INET,"127.0.0.3",&other_ip.sin_addr);check(bind(stranger,reinterpret_cast<sockaddr*>(&other_ip),sizeof(other_ip))==0,"other peer bind");
    auto bytes=query;bytes[12]=4;bytes.insert(bytes.end(),capability.begin(),capability.end());sendto(stranger,reinterpret_cast<const char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<sockaddr*>(&peer),peer_size);client.Pump();
    check(!PeerSupportsPerFrameInput(other_ip.sin_addr.s_addr),"capability cannot move the known session to another IP");
    closesocket(stranger);closesocket(host);
    puts("PASS: real legacy discovery, capability nonce/session/IP validation, re-search reset and safe reordered fallback.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
