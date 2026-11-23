#include "net/udp.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace d2k::net {

namespace {

constexpr uintptr_t kNone = ~uintptr_t(0);

#ifdef _WIN32
// WSAStartup once per process; the matching WSACleanup at exit.
struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA d;
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~WinsockInit() {
        if (ok) WSACleanup();
    }
};
bool winsock() {
    static WinsockInit w;
    return w.ok;
}
using SockLen = int;
void close_socket(uintptr_t s) { closesocket(SOCKET(s)); }
#else
bool winsock() { return true; }
using SockLen = socklen_t;
void close_socket(uintptr_t s) { ::close(int(s)); }
#endif

sockaddr_in to_sockaddr(const Address& a) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(a.ip);
    sa.sin_port = htons(a.port);
    return sa;
}

} // namespace

std::string Address::text() const {
    return std::to_string((ip >> 24) & 0xff) + "." + std::to_string((ip >> 16) & 0xff) + "." +
           std::to_string((ip >> 8) & 0xff) + "." + std::to_string(ip & 0xff) + ":" + std::to_string(port);
}

std::optional<Address> Address::parse(const std::string& s, uint16_t port) {
    if (!winsock()) return std::nullopt;
    std::string host = s;
    const size_t colon = s.rfind(':');
    if (colon != std::string::npos) {
        host = s.substr(0, colon);
        const std::string p = s.substr(colon + 1);
        if (p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
        const unsigned long v = std::stoul(p);
        if (v == 0 || v > 65535) return std::nullopt;
        port = uint16_t(v);
    }
    if (host.empty()) return std::nullopt;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return std::nullopt;
    Address a;
    a.ip = ntohl(reinterpret_cast<const sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
    a.port = port;
    freeaddrinfo(res);
    return a;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::is_open() const { return sock_ != kNone; }

void UdpSocket::close() {
    if (sock_ != kNone) close_socket(sock_);
    sock_ = kNone;
    port_ = 0;
}

bool UdpSocket::open(uint16_t port, bool loopback_only) {
    close();
    if (!winsock()) {
        error_ = "network start-up failed";
        return false;
    }
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        error_ = "socket() failed";
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    // A send to a closed port must not make the next recvfrom fail
    // (WSAECONNRESET from an ICMP "port unreachable").
    BOOL no_reset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &no_reset, sizeof no_reset, nullptr, 0, &bytes, nullptr, nullptr);
    const uintptr_t h = uintptr_t(s);
#else
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        error_ = "socket() failed";
        return false;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    const uintptr_t h = uintptr_t(s);
#endif
    int yes = 1;
    setsockopt(decltype(s)(h), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(decltype(s)(h), reinterpret_cast<const sockaddr*>(&sa), sizeof sa) != 0) {
        error_ = "port " + std::to_string(port) + " is in use";
        close_socket(h);
        return false;
    }
    SockLen len = sizeof sa;
    getsockname(decltype(s)(h), reinterpret_cast<sockaddr*>(&sa), &len);
    sock_ = h;
    port_ = ntohs(sa.sin_port);
    error_.clear();
    return true;
}

bool UdpSocket::send(const Address& to, std::span<const uint8_t> data) {
    if (sock_ == kNone) return false;
    const sockaddr_in sa = to_sockaddr(to);
#ifdef _WIN32
    const int n = sendto(SOCKET(sock_), reinterpret_cast<const char*>(data.data()), int(data.size()), 0,
                         reinterpret_cast<const sockaddr*>(&sa), sizeof sa);
#else
    const auto n = sendto(int(sock_), data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&sa), sizeof sa);
#endif
    return n == decltype(n)(data.size());
}

std::optional<Datagram> UdpSocket::receive() {
    if (sock_ == kNone) return std::nullopt;
    uint8_t buf[2048];
    sockaddr_in sa{};
    SockLen len = sizeof sa;
#ifdef _WIN32
    const int n = recvfrom(SOCKET(sock_), reinterpret_cast<char*>(buf), int(sizeof buf), 0,
                           reinterpret_cast<sockaddr*>(&sa), &len);
#else
    const auto n = recvfrom(int(sock_), buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&sa), &len);
#endif
    if (n <= 0) return std::nullopt;
    Datagram d;
    d.from.ip = ntohl(sa.sin_addr.s_addr);
    d.from.port = ntohs(sa.sin_port);
    d.data.assign(buf, buf + n);
    return d;
}

std::vector<uint32_t> UdpSocket::broadcast_addresses() {
    std::vector<uint32_t> out{0xFFFFFFFFu};
#ifndef _WIN32
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_BROADCAST) ||
                !i->ifa_broadaddr) {
                continue;
            }
            const uint32_t b = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_broadaddr)->sin_addr.s_addr);
            if (b != 0 && std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
        }
        freeifaddrs(list);
    }
#else
    // Windows sends 255.255.255.255 out of every interface with a default
    // route only; the directed broadcasts of the other interfaces come from
    // the address table, as /24 guesses. Our transport, not the exe's (it
    // used DirectPlay's enumeration): a LAN with another mask still gets
    // 255.255.255.255, and a player heard once is then probed directly
    // (docs/design/multiplayer.md section 6.3).
    if (winsock()) {
        char name[256];
        if (gethostname(name, sizeof name) == 0) {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            addrinfo* res = nullptr;
            if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
                for (addrinfo* r = res; r; r = r->ai_next) {
                    const uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(r->ai_addr)->sin_addr.s_addr);
                    const uint32_t b = ip | 0xFFu; // a /24 guess per address
                    if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
                }
                freeaddrinfo(res);
            }
        }
    }
#endif
    return out;
}

} // namespace d2k::net
