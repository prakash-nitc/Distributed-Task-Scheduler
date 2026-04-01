/*
 * master.cpp — Master Node (Control Center)
 *
 * TCP server with select()-based event loop, worker registry,
 * task queue, and first-available load balancing.
 */

#include "protocol.hpp"
#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <sys/select.h>

using namespace scheduler;

class Master {
public:
    Master() : server_fd_(setup_server()) {}

    void run() {
        std::cout << "[MASTER] Type a shell command to distribute "
                  << "(e.g., 'uptime', 'ls -la'):\n\n";

        while (true) {
            fd_set read_fds;
            FD_ZERO(&read_fds);

            FD_SET(STDIN_FILENO, &read_fds);
            int max_fd = STDIN_FILENO;

            FD_SET(server_fd_.fd(), &read_fds);
            if (server_fd_.fd() > max_fd) max_fd = server_fd_.fd();

            for (auto& w : workers_) {
                FD_SET(w.fd, &read_fds);
                if (w.fd > max_fd) max_fd = w.fd;
            }

            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
            if (activity < 0) {
                if (errno == EINTR) continue;
                perror("select() failed");
                break;
            }

            // New worker connection
            if (FD_ISSET(server_fd_.fd(), &read_fds)) {
                accept_worker();
            }

            // New command from stdin
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                std::string command;
                if (!std::getline(std::cin, command)) break;
                if (command.empty()) continue;

                if (command == "quit") {
                    std::cout << "[MASTER] Shutting down...\n";
                    break;
                }
                if (command == "status") {
                    print_status();
                    continue;
                }

                if (workers_.empty()) {
                    std::cout << "[MASTER] No workers connected yet!\n";
                    continue;
                }

                // Try to assign or queue
                if (!assign_task(command)) {
                    task_queue_.push(command);
                    std::cout << "[MASTER] All workers busy — task queued ("
                              << task_queue_.size() << " pending)\n";
                }
            }

            // Results from workers
            for (size_t i = 0; i < workers_.size(); i++) {
                if (FD_ISSET(workers_[i].fd, &read_fds)) {
                    handle_result(i);
                    i--; // Recheck index after possible removal
                }
            }
        }

        cleanup();
    }

private:
    Socket server_fd_;
    std::vector<Worker> workers_;
    std::queue<std::string> task_queue_;

    // ── Server Setup ─────────────────────────────────────────────────────

    Socket setup_server() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("socket"); exit(1); }

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(PORT);

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("bind"); exit(1);
        }
        if (listen(fd, 5) < 0) {
            perror("listen"); exit(1);
        }

        std::cout << "===========================================\n"
                  << "  DISTRIBUTED TASK SCHEDULER — MASTER\n"
                  << "===========================================\n"
                  << "[MASTER] Listening on port " << PORT << "...\n"
                  << "[MASTER] Waiting for workers to connect...\n\n";

        return Socket(fd);
    }

    // ── Accept Worker ────────────────────────────────────────────────────

    void accept_worker() {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd_.fd(),
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &addr_len);
        if (client_fd < 0) { perror("accept"); return; }

        if (static_cast<int>(workers_.size()) >= MAX_WORKERS) {
            std::cout << "[MASTER] Max workers reached. Rejecting.\n";
            ::close(client_fd);
            return;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

        workers_.emplace_back(client_fd, std::string(ip_str));
        std::cout << "[MASTER] Worker #" << workers_.size() - 1
                  << " connected from " << ip_str << "\n";
    }

    // ── Load Balancer (First-Available) ──────────────────────────────────

    bool assign_task(const std::string& command) {
        for (size_t i = 0; i < workers_.size(); i++) {
            if (!workers_[i].is_busy) {
                std::cout << "[MASTER] Assigning task to Worker #" << i
                          << " (" << workers_[i].ip << "): " << command << "\n";

                if (protocol::send_message(workers_[i].fd, command) == 0) {
                    workers_[i].is_busy = true;
                    return true;
                } else {
                    std::cout << "[MASTER] Failed to send to Worker #" << i << "\n";
                }
            }
        }
        return false;
    }

    // ── Handle Result ────────────────────────────────────────────────────

    void handle_result(size_t index) {
        std::string result = protocol::recv_message(workers_[index].fd);

        if (result.empty()) {
            std::cout << "[MASTER] Worker #" << index
                      << " (" << workers_[index].ip << ") disconnected.\n";
            ::close(workers_[index].fd);
            workers_.erase(workers_.begin() + index);
            return;
        }

        std::cout << "\n───────────────────────────────────────────\n"
                  << "  Result from Worker #" << index
                  << " (" << workers_[index].ip << "):\n"
                  << "───────────────────────────────────────────\n"
                  << result << "\n"
                  << "───────────────────────────────────────────\n\n";

        workers_[index].is_busy = false;

        // Dispatch queued task if any
        if (!task_queue_.empty()) {
            std::string next_task = task_queue_.front();
            task_queue_.pop();
            std::cout << "[MASTER] Dispatching queued task (" 
                      << task_queue_.size() << " remaining)\n";
            if (!assign_task(next_task)) {
                task_queue_.push(next_task);
            }
        }
    }

    // ── Status Display ───────────────────────────────────────────────────

    void print_status() {
        std::cout << "\n[MASTER] === Worker Status ===\n";
        for (size_t i = 0; i < workers_.size(); i++) {
            std::cout << "  Worker #" << i
                      << " | IP: " << workers_[i].ip
                      << " | " << (workers_[i].is_busy ? "BUSY" : "FREE") << "\n";
        }
        std::cout << "  Total workers: " << workers_.size()
                  << " | Queued tasks: " << task_queue_.size() << "\n\n";
    }

    // ── Cleanup ──────────────────────────────────────────────────────────

    void cleanup() {
        for (auto& w : workers_) ::close(w.fd);
        workers_.clear();
        std::cout << "[MASTER] Shutdown complete.\n";
    }
};

int main() {
    Master master;
    master.run();
    return 0;
}
