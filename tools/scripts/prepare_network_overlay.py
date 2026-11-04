"""Fix accepted-socket mode and immediate TCP rejoining in an SDK build copy."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
text = (root / 'tools/XboxXBERecomp/runtime/xdk/xnet.cpp').read_text()
edits = {
    '    ::ioctlsocket(s, FIONBIO, &on);': '''    ::ioctlsocket(s, FIONBIO, &on);
    if (!udp && tenchu::online::enabled.load()) {
        const int immediate = 1;
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&immediate), sizeof(immediate));
    }''',
    'uint32_t JoinAddress() {': '''uint32_t JoinAddress() {
    if (tenchu::online::enabled.load()) {
        std::string ip;
        if (!tenchu::online::ResolveHost(tenchu::online::Snapshot().join, ip)) return 0;
        in_addr address{};
        return inet_pton(AF_INET, ip.c_str(), &address) == 1 ? address.s_addr : 0;
    }''',
    'uint32_t PrimaryAddress() {': '''uint32_t PrimaryAddress() {
    if (tenchu::online::enabled.load()) {
        const auto ip = tenchu::online::Snapshot().bind_ip;
        in_addr address{};
        if (!ip.empty() && inet_pton(AF_INET, ip.c_str(), &address) == 1) return address.s_addr;
    }''',
    '        LOGI("xnet: %s address %s", g_instance ? "loopback instance" : "LAN", Ip(g_local).c_str());':
        '        tenchu::online::SetRuntimeAddress(Ip(g_local));\n        LOGI("xnet: %s address %s", g_instance ? "loopback instance" : "LAN", Ip(g_local).c_str());',
    'static uint32_t AddSocket(SOCKET s, bool udp) {':
        'static uint32_t AddSocket(SOCKET s, bool udp, bool nonblocking = false) {',
    'g_sockets[id] = {s, udp, false, 0};':
        'g_sockets[id] = {s, udp, nonblocking, 0};',
    '''    sockaddr_in to = GuestAddr(a[1]);
    if (::connect(k.s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) return 0;''': '''    sockaddr_in to = GuestAddr(a[1]);
    if (!k.udp && tenchu::online::enabled.load()) {
        // RFD binds client and listener to the same TCP game port. Reusing that
        // peer tuple during TIME_WAIT can fail asynchronously, so choose an
        // ephemeral client source before connect. The guest handle stays stable.
        sockaddr_in local{};
        int local_size = sizeof(local);
        if (::getsockname(k.s, reinterpret_cast<sockaddr*>(&local), &local_size) == 0 &&
                local.sin_port && local.sin_port == to.sin_port) {
            SOCKET fresh = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fresh != INVALID_SOCKET) {
                u_long nonblocking = 1;
                bool prepared = ::ioctlsocket(fresh, FIONBIO, &nonblocking) == 0;
                for (const auto& option : std::array<std::pair<int,int>,5>{{
                        {SOL_SOCKET,SO_SNDBUF}, {SOL_SOCKET,SO_RCVBUF},
                        {SOL_SOCKET,SO_KEEPALIVE}, {SOL_SOCKET,SO_LINGER},
                        {IPPROTO_TCP,TCP_NODELAY}}}) {
                    std::array<char,16> value{};
                    int size = int(value.size());
                    if (::getsockopt(k.s, option.first, option.second, value.data(), &size) == 0)
                        prepared = (::setsockopt(fresh, option.first, option.second, value.data(), size) == 0) && prepared;
                }
                local.sin_port = 0;
                prepared = prepared && ::bind(fresh, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
                if (prepared) {
                    local_size = sizeof(local);
                    prepared = ::getsockname(fresh, reinterpret_cast<sockaddr*>(&local), &local_size) == 0;
                }
                bool replaced = false;
                if (prepared) {
                    std::lock_guard<std::mutex> lock(g_lock);
                    auto it = g_sockets.find(a[0]);
                    if (it != g_sockets.end() && it->second.s == k.s) {
                        ::closesocket(k.s);
                        it->second.s = fresh;
                        it->second.port = local.sin_port;
                        k = it->second;
                        replaced = true;
                    }
                }
                if (!replaced) ::closesocket(fresh);
                else {
                    LOGR("Online TCP: outbound socket=%X source=%s for immediate rejoining", a[0], Addr(local).c_str());
                    logx::Flush();
                }
            }
        }
    }
    if (::connect(k.s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) return 0;''',
    'return AddSocket(s, false);': '''// An accepted socket inherits the listener's guest FIONBIO mode.
            // The host socket is always nonblocking; losing the guest mode here
            // makes recv wait inside RFD's main-thread packet polling loop.
            const auto id = AddSocket(s, false, k.nonblocking);
            if (tenchu::online::enabled.load()) {
                LOGR("Online TCP: accepted socket=%X listener=%X peer=%s nonblocking=%u", id, a[0], Addr(from).c_str(), unsigned(k.nonblocking));
                logx::Flush();
            }
            return id;''',
    'int n = ::recv(k.s, static_cast<char*>(HostPtr(a[1])), int(a[2]), int(a[3]));': '''int n = ::recv(k.s, static_cast<char*>(HostPtr(a[1])), int(a[2]), int(a[3]));
        if (tenchu::online::enabled.load() && n >= 0) {
            static std::atomic<unsigned> budget{0};
            if (budget.fetch_add(1) < 32) {
                LOGR("Online TCP: receive socket=%X requested=%u result=%d", a[0], a[2], n);
                logx::Flush();
            }
        }''',
    'int n = ::send(k.s, static_cast<const char*>(HostPtr(a[1])), int(a[2]), int(a[3]));': '''int n = ::send(k.s, static_cast<const char*>(HostPtr(a[1])), int(a[2]), int(a[3]));
        if (tenchu::online::enabled.load() && n >= 0) {
            static std::atomic<unsigned> budget{0};
            if (budget.fetch_add(1) < 32) {
                LOGR("Online TCP: send socket=%X requested=%u result=%d", a[0], a[2], n);
                logx::Flush();
            }
        }''',
}
for old, new in edits.items():
    assert text.count(old) == 1, f'Upstream network source changed: {old}'
    text = text.replace(old, new)
text = '#include "port/online.h"\n' + text
out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
if not out.exists() or out.read_text() != text:
    out.write_text(text)
if len(sys.argv)>2:
    boot=(root/'tools/XboxXBERecomp/runtime/kernel/boot.cpp').read_text()
    for old,new in {
        'void Shutdown(int code) {':'void Shutdown(int code) {\n    tenchu::online::router::Stop();',
        '    xnet::CloseSockets();':'    tenchu::online::router::Stop();\n    xnet::CloseSockets();',
    }.items():
        assert boot.count(old)==1,f'Upstream shutdown changed: {old}'
        boot=boot.replace(old,new)
    boot='#include "port/online_router.h"\n'+boot
    target=Path(sys.argv[2]);target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or target.read_text()!=boot:target.write_text(boot)
