#include "platform/udp_socket.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// mstcpip.h's; some SDKs lack it.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace hailstone::platform {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
const NativeSocket kInvalid = INVALID_SOCKET;
using SockLen = int;

// Set at exit when a lookup was still in getaddrinfo and was left to run (resolve_async): Winsock is not shut down
// under it; the process's end frees it.
std::atomic<bool> lookups_left_running{false};

struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockInit() {
        if (ok && !lookups_left_running.load()) WSACleanup();
    }
};

bool ensure_init() {
    static WinsockInit init;
    return init.ok;
}

int last_socket_error() { return WSAGetLastError(); }
bool would_block(int e) { return e == WSAEWOULDBLOCK; }
// An ICMP port-unreachable reply surfaces as WSAECONNRESET on the next
// recvfrom; for UDP it is not a socket error.
bool transient(int e) { return e == WSAECONNRESET || e == WSAEMSGSIZE; }
std::string error_text(int e) { return std::format("winsock error {}", e); }
void close_native(NativeSocket s) { closesocket(s); }
// winsock2.h's since Windows 7 SP1; older SDKs lack the name.
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif
bool inheritable(NativeSocket s) {
    DWORD flags = 0;
    return GetHandleInformation(reinterpret_cast<HANDLE>(s), &flags) && (flags & HANDLE_FLAG_INHERIT) != 0;
}
// A socket no child process inherits (SDL starts the helpers with inheritable pipes, so handles are passed on).
// WSA_FLAG_OVERLAPPED is what socket() gives; a system without the no-inherit flag takes the plain call instead.
NativeSocket open_socket(int af, int type, int protocol) {
    NativeSocket s = WSASocketW(af, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == kInvalid) {
        s = ::socket(af, type, protocol);
        if (s != kInvalid) SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);
    }
    return s;
}
#else
using NativeSocket = int;
const NativeSocket kInvalid = -1;
using SockLen = socklen_t;

bool ensure_init() { return true; }
std::atomic<bool> lookups_left_running{false};
int last_socket_error() { return errno; }
bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool transient(int e) { return e == ECONNREFUSED || e == EINTR; }
std::string error_text(int e) { return std::strerror(e); }
void close_native(NativeSocket s) { ::close(s); }
bool inheritable(NativeSocket s) {
    const int flags = fcntl(s, F_GETFD);
    return flags >= 0 && (flags & FD_CLOEXEC) == 0;
}
// A socket closed across exec, so no child process (the browser helper) holds it: SOCK_CLOEXEC where the system has it
// (Linux, the BSDs), else (macOS) fcntl after it.
NativeSocket open_socket(int af, int type, int protocol) {
#ifdef SOCK_CLOEXEC
    NativeSocket s = ::socket(af, type | SOCK_CLOEXEC, protocol);
    if (s != kInvalid) return s;
#endif
    NativeSocket plain = ::socket(af, type, protocol);
    if (plain != kInvalid) {
        const int flags = fcntl(plain, F_GETFD);
        if (flags >= 0) fcntl(plain, F_SETFD, flags | FD_CLOEXEC);
    }
    return plain;
}
#endif

NativeSocket native(std::intptr_t h) { return static_cast<NativeSocket>(h); }
std::intptr_t widen(NativeSocket s) { return static_cast<std::intptr_t>(s); }
bool valid(std::intptr_t h) { return native(h) != kInvalid; }

SockLen to_sockaddr(const Endpoint& ep, sockaddr_storage& ss) {
    std::memset(&ss, 0, sizeof ss);
    if (ep.family == Endpoint::Family::V4) {
        sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_port = htons(ep.port);
        std::memcpy(&sin.sin_addr, ep.address.data(), 4);
        std::memcpy(&ss, &sin, sizeof sin);
        return static_cast<SockLen>(sizeof sin);
    }
    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(ep.port);
    std::memcpy(&sin6.sin6_addr, ep.address.data(), 16);
    sin6.sin6_scope_id = ep.scope_id; // a link-local address names its interface
    std::memcpy(&ss, &sin6, sizeof sin6);
    return static_cast<SockLen>(sizeof sin6);
}

std::optional<Endpoint> from_sockaddr(const sockaddr* sa) {
    Endpoint ep;
    if (sa->sa_family == AF_INET) {
        sockaddr_in sin;
        std::memcpy(&sin, sa, sizeof sin);
        ep.family = Endpoint::Family::V4;
        std::memcpy(ep.address.data(), &sin.sin_addr, 4);
        ep.port = ntohs(sin.sin_port);
        return ep;
    }
    if (sa->sa_family == AF_INET6) {
        sockaddr_in6 sin6;
        std::memcpy(&sin6, sa, sizeof sin6);
        ep.family = Endpoint::Family::V6;
        std::memcpy(ep.address.data(), &sin6.sin6_addr, 16);
        ep.port = ntohs(sin6.sin6_port);
        ep.scope_id = sin6.sin6_scope_id;
        return ep;
    }
    return std::nullopt;
}

} // namespace

Endpoint Endpoint::v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t p) {
    Endpoint ep;
    ep.family = Family::V4;
    ep.address[0] = a;
    ep.address[1] = b;
    ep.address[2] = c;
    ep.address[3] = d;
    ep.port = p;
    return ep;
}

std::string Endpoint::address_string() const {
    if (family == Family::V4)
        return std::format("{}.{}.{}.{}", address[0], address[1], address[2], address[3]);
    std::string out;
    for (std::size_t i = 0; i < 16; i += 2) {
        if (i) out += ':';
        out += std::format("{:x}", (address[i] << 8) | address[i + 1]);
    }
    if (scope_id != 0) out += std::format("%{}", scope_id);
    return out;
}

std::string Endpoint::to_string() const {
    if (family == Family::V4) return std::format("{}:{}", address_string(), port);
    return std::format("[{}]:{}", address_string(), port);
}

namespace {

std::vector<Endpoint> lookup(const std::string& host, std::uint16_t port, std::string& error, bool numeric_only) {
    std::vector<Endpoint> out;
    if (!ensure_init()) {
        error = "socket library initialisation failed";
        return out;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (numeric_only) hints.ai_flags = AI_NUMERICHOST; // never asks a name server
    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || !result) {
#ifdef _WIN32
        error = std::format("cannot resolve {}: error {}", host, rc);
#else
        error = std::format("cannot resolve {}: {}", host, gai_strerror(rc));
#endif
        return out;
    }
    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        if (auto ep = from_sockaddr(ai->ai_addr)) {
            ep->port = port;
            bool dup = false;
            for (const auto& e : out) dup = dup || e == *ep;
            if (!dup) out.push_back(*ep);
        }
    }
    freeaddrinfo(result);
    std::stable_partition(out.begin(), out.end(), [](const Endpoint& e) { return e.family == Endpoint::Family::V4; });
    if (out.empty()) error = std::format("no usable address for {}", host);
    return out;
}

} // namespace

std::vector<Endpoint> resolve(const std::string& host, std::uint16_t port, std::string& error) {
    return lookup(host, port, error, false);
}

std::optional<Endpoint> numeric_endpoint(const std::string& host, std::uint16_t port) {
    if (host.empty()) return std::nullopt;
    std::string error;
    auto out = lookup(host, port, error, true);
    if (out.empty()) return std::nullopt;
    return out.front();
}

namespace {

// resolve_async's threads, at most kMaxThreads at once: more lookups wait in a queue, and a thread that finishes one
// takes the next (a script that looks up many names starts no thread per name). A finished thread is joined when the
// next lookup starts, and at exit. One still in getaddrinfo at exit cannot be cut off and may wait out the
// resolver's timeout (tens of seconds with an unreachable name server), so it is not waited for: it is detached and
// left to the process's end, and WinsockInit then skips WSACleanup so the socket library is not shut down under it.
// The lookups still queued then never run. Their `done` must own what it touches (a shared_ptr), as it may run after
// the caller is gone. ensure_init() is called before this is first made, so this is destroyed first.
class Lookups {
public:
    static constexpr std::size_t kMaxThreads = 4;

    ~Lookups() {
        std::vector<Entry> all;
        {
            std::lock_guard lock(shared_->mutex);
            all.swap(entries_);
            shared_->queue.clear();
        }
        for (auto& e : all) {
            if (!e.thread.joinable()) continue;
            if (e.done->load()) {
                e.thread.join();
            } else {
                lookups_left_running.store(true);
                e.thread.detach();
            }
        }
    }
    void start(std::function<void()> fn) {
        std::lock_guard lock(shared_->mutex);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->done->load()) {
                it->thread.join();
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        if (entries_.size() >= kMaxThreads) {
            shared_->queue.push_back(std::move(fn));
            return;
        }
        auto done = std::make_shared<std::atomic<bool>>(false);
        Entry e;
        e.done = done;
        // The thread holds the queue's state, not this, so a thread left running at exit outlives it safely.
        e.thread = std::thread([fn = std::move(fn), done, shared = shared_]() mutable {
            for (;;) {
                fn();
                std::lock_guard held(shared->mutex);
                if (shared->queue.empty()) {
                    done->store(true); // under the lock: start() sees it done only once it takes no more
                    return;
                }
                fn = std::move(shared->queue.front());
                shared->queue.pop_front();
            }
        });
        entries_.push_back(std::move(e));
    }

private:
    struct Entry {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    struct Shared {
        std::mutex mutex;
        std::deque<std::function<void()>> queue; // lookups waiting for a thread
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::vector<Entry> entries_; // guarded by shared_->mutex
};

} // namespace

void resolve_async(std::string host, std::uint16_t port,
                   std::function<void(std::vector<Endpoint> endpoints, std::string error)> done) {
    ensure_init(); // its static first, so the lookups' static below is destroyed before it
    static Lookups lookups;
    lookups.start([host = std::move(host), port, done = std::move(done)] {
        std::string error;
        auto endpoints = resolve(host, port, error);
        done(std::move(endpoints), std::move(error));
    });
}

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : handle_(other.handle_), error_(std::move(other.error_)) {
    other.handle_ = widen(kInvalid);
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        error_ = std::move(other.error_);
        other.handle_ = widen(kInvalid);
    }
    return *this;
}

bool UdpSocket::is_open() const { return valid(handle_); }

bool UdpSocket::inherited_by_children() const { return valid(handle_) && inheritable(native(handle_)); }

void UdpSocket::close() {
    if (valid(handle_)) close_native(native(handle_));
    handle_ = widen(kInvalid);
}

bool UdpSocket::open(Endpoint::Family family, std::string& error, const Endpoint* bind_to) {
    close();
    error_.clear();
    if (!ensure_init()) {
        error = "socket library initialisation failed";
        return false;
    }
    int af = family == Endpoint::Family::V4 ? AF_INET : AF_INET6;
    NativeSocket s = open_socket(af, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) {
        error = "socket(): " + error_text(last_socket_error());
        return false;
    }
#ifdef _WIN32
    u_long nonblocking = 1;
    bool nb_ok = ioctlsocket(s, FIONBIO, &nonblocking) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    bool nb_ok = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    if (!nb_ok) {
        error = "cannot make socket non-blocking: " + error_text(last_socket_error());
        close_native(s);
        return false;
    }
#ifdef _WIN32
    // No WSAECONNRESET for an ICMP port-unreachable on the next recvfrom: there is nothing to reset on UDP, and a
    // flood of them would only cost recv_from passes (it still skips one if this fails).
    BOOL report_resets = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &report_resets, sizeof report_resets, nullptr, 0, &returned, nullptr, nullptr);
#endif
    // The original client asks for 128 KiB socket buffers. The receive buffer is 2 MiB: the
    // server's files come in bursts of up to net::ServerFileSync::kWindow pieces (game/server_file_sync.h), which overflow
    // 128 KiB between two service passes, and EQEmu's resends of what was dropped back off for seconds.
    int buf = 0x20000;
    int rcvbuf = 0x200000;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), static_cast<SockLen>(sizeof rcvbuf));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), static_cast<SockLen>(sizeof buf));

    Endpoint any;
    any.family = family;
    sockaddr_storage ss;
    SockLen len = to_sockaddr(bind_to ? *bind_to : any, ss);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        error = "bind(): " + error_text(last_socket_error());
        close_native(s);
        return false;
    }
    handle_ = widen(s);
    return true;
}

std::optional<Endpoint> UdpSocket::local_endpoint() const {
    if (!is_open()) return std::nullopt;
    sockaddr_storage ss{};
    SockLen len = sizeof ss;
    if (getsockname(native(handle_), reinterpret_cast<sockaddr*>(&ss), &len) != 0) return std::nullopt;
    return from_sockaddr(reinterpret_cast<const sockaddr*>(&ss));
}

bool UdpSocket::send_to(const Endpoint& to, std::span<const std::byte> data) {
    if (!is_open()) return false;
    error_.clear(); // last_error() is this call's, not an earlier one's
    sockaddr_storage ss;
    SockLen len = to_sockaddr(to, ss);
#ifdef _WIN32
    int n = ::sendto(native(handle_), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                     reinterpret_cast<const sockaddr*>(&ss), len);
#else
    auto n = ::sendto(native(handle_), data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&ss), len);
#endif
    if (n < 0) {
        int e = last_socket_error();
        if (would_block(e) || transient(e)) return true;
        error_ = "sendto(): " + error_text(e);
        return false;
    }
    return true;
}

std::optional<std::size_t> UdpSocket::recv_from(std::span<std::byte> buffer, Endpoint& from) {
    if (!is_open()) return std::nullopt;
    error_.clear();
    // What is skipped (a transient error, a truncated datagram, an unknown address family) is bounded per call, so a
    // flood of them cannot keep the caller (which holds the session's lock) here: past the bound the call returns
    // nothing, as if nothing were waiting, and the next pass reads on.
    for (int skipped = 0; skipped < kMaxSkippedPerRecv; ++skipped) {
        sockaddr_storage ss{};
        SockLen len = sizeof ss;
#ifdef _WIN32
        // A datagram larger than the buffer fails with WSAEMSGSIZE (transient above): it is dropped.
        int n = ::recvfrom(native(handle_), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                           reinterpret_cast<sockaddr*>(&ss), &len);
        bool truncated = false;
#else
        // POSIX recvfrom would return the first buffer.size() bytes of a larger datagram as if they were all of it;
        // recvmsg reports the cut in msg_flags (MSG_TRUNC, on Linux, macOS and the BSDs alike), and the datagram is
        // dropped as Winsock drops it.
        iovec iov{buffer.data(), buffer.size()};
        msghdr msg{};
        msg.msg_name = &ss;
        msg.msg_namelen = len;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        auto n = ::recvmsg(native(handle_), &msg, 0);
        bool truncated = n >= 0 && (msg.msg_flags & MSG_TRUNC) != 0;
#endif
        if (n < 0) {
            int e = last_socket_error();
            if (transient(e)) continue;
            if (!would_block(e)) error_ = "recvfrom(): " + error_text(e);
            return std::nullopt;
        }
        if (truncated) continue;
        auto ep = from_sockaddr(reinterpret_cast<const sockaddr*>(&ss));
        if (!ep) continue;
        from = *ep;
        return static_cast<std::size_t>(n);
    }
    return std::nullopt;
}

bool UdpSocket::wait_readable(std::chrono::milliseconds timeout) const {
    if (!is_open()) return false;
#ifdef _WIN32
    WSAPOLLFD pfd{};
    pfd.fd = native(handle_);
    pfd.events = POLLRDNORM;
    return WSAPoll(&pfd, 1, static_cast<INT>(timeout.count())) > 0;
#else
    pollfd pfd{};
    pfd.fd = native(handle_);
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, static_cast<int>(timeout.count())) > 0;
#endif
}

} // namespace hailstone::platform
