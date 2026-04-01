/*
 * worker.cpp — Worker Node (Execution Unit)
 *
 * Connects to Master, receives shell commands, executes them
 * via fork/pipe/dup2/execvp/waitpid, and sends output back.
 */

#include "protocol.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdlib>

using namespace scheduler;

class TaskExecutor {
public:
    // Execute a shell command and return captured output + exit code
    static std::string execute(const std::string& command) {
        int pipe_fd[2];
        if (pipe(pipe_fd) < 0) {
            return "[ERROR] pipe() failed: " + std::string(strerror(errno));
        }

        pid_t pid = fork();

        if (pid < 0) {
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);
            return "[ERROR] fork() failed: " + std::string(strerror(errno));
        }

        if (pid == 0) {
            // ── Child Process ──
            dup2(pipe_fd[1], STDOUT_FILENO);
            dup2(pipe_fd[1], STDERR_FILENO);
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);

            auto argv = tokenize(command);
            std::vector<char*> args;
            for (auto& s : argv) args.push_back(&s[0]);
            args.push_back(nullptr);

            execvp(args[0], args.data());
            // Only reached if execvp fails
            fprintf(stderr, "execvp failed for '%s': %s\n",
                    args[0], strerror(errno));
            _exit(127);
        }

        // ── Parent Process ──
        ::close(pipe_fd[1]);

        std::string output;
        char buf[BUFFER_SIZE];
        ssize_t n;
        while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            output += buf;
        }
        ::close(pipe_fd[0]);

        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            output += "\n[Exit Code: " + std::to_string(WEXITSTATUS(status)) + "]";
        } else if (WIFSIGNALED(status)) {
            output += "\n[Killed by signal: " + std::to_string(WTERMSIG(status)) + "]";
        }

        return output;
    }

private:
    static std::vector<std::string> tokenize(const std::string& cmd) {
        std::vector<std::string> tokens;
        std::istringstream iss(cmd);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }
};

class WorkerNode {
public:
    explicit WorkerNode(const std::string& master_ip)
        : master_fd_(connect_to_master(master_ip)) {}

    void run() {
        std::cout << "[WORKER] Ready. Waiting for tasks...\n\n";

        while (true) {
            std::string command = protocol::recv_message(master_fd_.fd());
            if (command.empty()) {
                std::cout << "[WORKER] Master disconnected. Shutting down.\n";
                break;
            }

            std::cout << "[WORKER] Received task: " << command << "\n";

            std::string result = TaskExecutor::execute(command);

            std::cout << "[WORKER] Task completed. Sending result...\n";

            if (protocol::send_message(master_fd_.fd(), result) < 0) {
                std::cout << "[WORKER] Failed to send result.\n";
                break;
            }
        }

        std::cout << "[WORKER] Shutdown complete.\n";
    }

private:
    Socket master_fd_;

    Socket connect_to_master(const std::string& ip) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("socket"); exit(1); }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(PORT);

        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "Invalid Master IP: " << ip << "\n";
            exit(1);
        }

        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("connect — is the Master running?");
            exit(1);
        }

        std::cout << "===========================================\n"
                  << "  DISTRIBUTED TASK SCHEDULER — WORKER\n"
                  << "===========================================\n"
                  << "[WORKER] Connected to Master at " << ip
                  << ":" << PORT << "\n";

        return Socket(fd);
    }
};

int main(int argc, char* argv[]) {
    std::string master_ip = "127.0.0.1";
    if (argc >= 2) master_ip = argv[1];

    WorkerNode worker(master_ip);
    worker.run();
    return 0;
}
