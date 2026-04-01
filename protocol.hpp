/*
 * protocol.hpp — Shared protocol definitions for Distributed Task Scheduler
 *
 * Defines:
 *   - Constants (port, buffer sizes)
 *   - RAII Socket wrapper (auto-closes fd)
 *   - Worker struct
 *   - Length-prefix wire protocol (send/recv)
 */

#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <string>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

namespace scheduler {

constexpr int    PORT        = 8080;
constexpr int    MAX_WORKERS = 10;
constexpr size_t BUFFER_SIZE = 4096;

// ── RAII Socket Wrapper ──────────────────────────────────────────────────
// Automatically closes the file descriptor when it goes out of scope.
// Movable but not copyable (unique ownership semantics, like std::unique_ptr).

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}

    // Move constructor
    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    // Move assignment
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // No copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    ~Socket() { close(); }

    int fd() const { return fd_; }
    int release() { int f = fd_; fd_ = -1; return f; }
    bool valid() const { return fd_ >= 0; }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_;
};

// ── Worker Info ──────────────────────────────────────────────────────────

struct Worker {
    int         fd;
    std::string ip;
    bool        is_busy;

    Worker(int fd, const std::string& ip)
        : fd(fd), ip(ip), is_busy(false) {}
};

// ── Wire Protocol (Length-Prefix Framing) ────────────────────────────────
// Every message is sent as: [4-byte big-endian length] + [payload bytes]

namespace protocol {

inline int send_message(int fd, const std::string& msg) {
    uint32_t len = static_cast<uint32_t>(msg.size());
    uint32_t net_len = htonl(len);

    if (send(fd, &net_len, sizeof(net_len), 0) != sizeof(net_len))
        return -1;

    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(fd, msg.c_str() + total_sent, len - total_sent, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
    }
    return 0;
}

inline std::string recv_message(int fd) {
    uint32_t net_len;
    ssize_t r = recv(fd, &net_len, sizeof(net_len), MSG_WAITALL);
    if (r <= 0) return "";

    uint32_t len = ntohl(net_len);
    if (len == 0 || len >= BUFFER_SIZE) return "";

    std::string buf(len, '\0');
    size_t total_read = 0;
    while (total_read < len) {
        ssize_t n = recv(fd, &buf[total_read], len - total_read, 0);
        if (n <= 0) return "";
        total_read += n;
    }
    return buf;
}

} // namespace protocol
} // namespace scheduler

#endif // PROTOCOL_HPP
