#pragma once
// A non-blocking IPv4 UDP socket (Winsock on Windows, BSD sockets
// elsewhere) for network games (docs/design/multiplayer.md section 6).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2k::net {

struct Address {
    uint32_t ip = 0;   // host byte order (127.0.0.1 = 0x7F000001)
    uint16_t port = 0; // host byte order
    bool operator==(const Address&) const = default;
    std::string text() const; // "192.168.1.5:1998"
    // "192.168.1.5", "192.168.1.5:1998" or a host name; `port` when none given.
    static std::optional<Address> parse(const std::string& s, uint16_t port);
};

struct Datagram {
    Address from;
    std::vector<uint8_t> data;
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds to `port` on every interface (0 = any free port) with
    // broadcast sending allowed; `loopback_only` binds to 127.0.0.1 (tests:
    // no firewall prompt). False with error() set on failure.
    bool open(uint16_t port, bool loopback_only = false);
    void close();
    bool is_open() const;
    uint16_t port() const { return port_; } // the bound port
    bool send(const Address& to, std::span<const uint8_t> data);
    std::optional<Datagram> receive(); // nothing when no datagram waits
    const std::string& error() const { return error_; }
    // The IPv4 broadcast addresses to probe for LAN games: 255.255.255.255
    // and each interface's directed broadcast where the system tells it.
    static std::vector<uint32_t> broadcast_addresses();

private:
    uintptr_t sock_ = ~uintptr_t(0);
    uint16_t port_ = 0;
    std::string error_;
};

} // namespace d2k::net
