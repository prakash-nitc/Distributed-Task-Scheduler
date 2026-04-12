/*
 * master.cpp — Master Node (Control Center)
 *
 * TCP server with select()-based event loop, worker registry,
 * task queue, and first-available load balancing.
 *
 * Interactive mode: reads commands from stdin (terminal).
 * Server-only mode: TCP clients only (auto-detected in Docker via isatty).
 */

#include "protocol.hpp"
#include "logger.hpp"
#include <iostream>
#include <vector>
#include <queue>
#include <sys/select.h>
#include <csignal>

// Set to 0 by SIGINT/SIGTERM — breaks the select() loop cleanly
static volatile sig_atomic_t g_running = 1;
static void handle_signal(int) { g_running = 0; }

using namespace scheduler;

class Master {
public:
    Master() : server_fd_(setup_server()), interactive_(isatty(STDIN_FILENO)) {
        if (interactive_) {
            std::cout << "[MASTER] Type a shell command to distribute "
                      << "(e.g., 'uptime', 'ls -la').\n"
                      << "         Type 'status' to inspect workers, 'quit' to exit.\n\n";
        } else {
            LOG_INFO("MASTER") << "Running in server-only mode — use client.py to submit tasks";
        }
    }

    void run() {
        while (true) {
            fd_set read_fds;
            FD_ZERO(&read_fds);

            int max_fd = server_fd_.fd();
            FD_SET(server_fd_.fd(), &read_fds);

            if (interactive_) {
                FD_SET(STDIN_FILENO, &read_fds);
                // STDIN_FILENO == 0, always < server_fd_, no max_fd update needed
            }

            for (auto& w : workers_) {
                FD_SET(w.fd, &read_fds);
                if (w.fd > max_fd) max_fd = w.fd;
            }

            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
            if (activity < 0) {
                if (errno == EINTR) {
                    if (!g_running) {
                        LOG_INFO("MASTER") << "Shutdown signal received — closing all connections";
                        break;
                    }
                    continue;
                }
                LOG_ERROR("MASTER") << "select() failed: " << strerror(errno);
                break;
            }

            // New worker connection
            if (FD_ISSET(server_fd_.fd(), &read_fds))
                accept_worker();

            // Command from stdin (interactive mode only)
            if (interactive_ && FD_ISSET(STDIN_FILENO, &read_fds)) {
                std::string command;
                if (!std::getline(std::cin, command)) {
                    LOG_INFO("MASTER") << "stdin closed — switching to server-only mode";
                    interactive_ = false;
                    continue;
                }
                if (command.empty()) continue;

                if (command == "quit") {
                    LOG_INFO("MASTER") << "Shutting down...";
                    break;
                }
                if (command == "status") { print_status(); continue; }

                if (workers_.empty()) {
                    LOG_WARN("MASTER") << "No workers connected — command ignored";
                    continue;
                }
                if (!assign_task(command)) {
                    task_queue_.push(command);
                    LOG_WARN("MASTER") << "All workers busy — task queued ("
                                       << task_queue_.size() << " pending)";
                }
            }

            // Results from workers
            for (size_t i = 0; i < workers_.size(); ++i) {
                if (FD_ISSET(workers_[i].fd, &read_fds)) {
                    handle_result(i);
                    --i; // recheck after possible erase
                }
            }
        }

        cleanup();
    }

private:
    Socket             server_fd_;
    std::vector<Worker> workers_;
    std::queue<std::string> task_queue_;
    bool               interactive_;

    // ── Server Setup ─────────────────────────────────────────────────────

    Socket setup_server() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            LOG_ERROR("MASTER") << "socket() failed: " << strerror(errno);
            exit(1);
        }

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(PORT);

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOG_ERROR("MASTER") << "bind() failed: " << strerror(errno);
            exit(1);
        }
        if (listen(fd, 5) < 0) {
            LOG_ERROR("MASTER") << "listen() failed: " << strerror(errno);
            exit(1);
        }

        std::cout << "===========================================\n"
                  << "  DISTRIBUTED TASK SCHEDULER — MASTER\n"
                  << "===========================================\n";
        LOG_INFO("MASTER") << "Listening on port " << PORT;
        LOG_INFO("MASTER") << "Waiting for workers to connect...";
        std::cout << "\n";

        return Socket(fd);
    }

    // ── Accept Worker ────────────────────────────────────────────────────

    void accept_worker() {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd_.fd(),
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &addr_len);
        if (client_fd < 0) {
            LOG_ERROR("MASTER") << "accept() failed: " << strerror(errno);
            return;
        }

        if (static_cast<int>(workers_.size()) >= MAX_WORKERS) {
            LOG_WARN("MASTER") << "Max workers (" << MAX_WORKERS << ") reached — rejecting connection";
            ::close(client_fd);
            return;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

        workers_.emplace_back(client_fd, std::string(ip_str));
        LOG_INFO("MASTER") << "Worker #" << (workers_.size() - 1)
                           << " connected from " << ip_str
                           << " (pool size: " << workers_.size() << ")";
    }

    // ── Load Balancer (First-Available) ──────────────────────────────────

    bool assign_task(const std::string& command) {
        for (size_t i = 0; i < workers_.size(); ++i) {
            if (!workers_[i].is_busy) {
                LOG_INFO("MASTER") << "Dispatching to Worker #" << i
                                   << " [" << workers_[i].ip << "]: \"" << command << "\"";
                if (protocol::send_message(workers_[i].fd, command) == 0) {
                    workers_[i].is_busy = true;
                    return true;
                }
                LOG_ERROR("MASTER") << "Send failed for Worker #" << i << " — skipping";
            }
        }
        return false;
    }

    // ── Handle Result ────────────────────────────────────────────────────

    void handle_result(size_t index) {
        std::string result = protocol::recv_message(workers_[index].fd);

        if (result.empty()) {
            LOG_WARN("MASTER") << "Worker #" << index
                               << " [" << workers_[index].ip << "] disconnected";
            ::close(workers_[index].fd);
            workers_.erase(workers_.begin() + index);
            return;
        }

        LOG_INFO("MASTER") << "Result received from Worker #" << index
                           << " [" << workers_[index].ip << "]";

        std::cout << "\n───────────────────────────────────────────\n"
                  << "  Result from Worker #" << index
                  << " [" << workers_[index].ip << "]\n"
                  << "───────────────────────────────────────────\n"
                  << result << "\n"
                  << "───────────────────────────────────────────\n\n";

        workers_[index].is_busy = false;

        if (!task_queue_.empty()) {
            std::string next = task_queue_.front();
            task_queue_.pop();
            LOG_INFO("MASTER") << "Dispatching queued task ("
                               << task_queue_.size() << " remaining): \"" << next << "\"";
            if (!assign_task(next))
                task_queue_.push(next);
        }
    }

    // ── Status Display ───────────────────────────────────────────────────

    void print_status() {
        std::cout << "\n[STATUS] Workers: " << workers_.size()
                  << " | Queued: " << task_queue_.size() << "\n";
        for (size_t i = 0; i < workers_.size(); ++i) {
            std::cout << "  Worker #" << i
                      << " | " << workers_[i].ip
                      << " | " << (workers_[i].is_busy ? "BUSY" : "FREE") << "\n";
        }
        std::cout << "\n";
    }

    // ── Cleanup ──────────────────────────────────────────────────────────

    void cleanup() {
        for (auto& w : workers_) ::close(w.fd);
        workers_.clear();
        LOG_INFO("MASTER") << "Shutdown complete";
    }
};

int main() {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    Master master;
    master.run();
    return 0;
}
