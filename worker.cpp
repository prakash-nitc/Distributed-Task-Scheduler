/*
 * worker.cpp — Worker Node (Execution Unit)
 *
 * Connects to Master with retry logic, receives shell commands, executes them
 * via fork/pipe/dup2/execvp/waitpid, and sends output + timing back.
 */

#include "protocol.hpp"
#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>
#include <netdb.h>
#include <csignal>

// Set to 0 by SIGINT/SIGTERM — exits the recv loop cleanly
static volatile sig_atomic_t g_running = 1;
static void handle_signal(int) { g_running = 0; }

using namespace scheduler;

class TaskExecutor {
public:
    static std::string execute(const std::string& command) {
        int pipe_fd[2];
        if (pipe(pipe_fd) < 0)
            return "[ERROR] pipe() failed: " + std::string(strerror(errno));

        pid_t pid = fork();
        if (pid < 0) {
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);
            return "[ERROR] fork() failed: " + std::string(strerror(errno));
        }

        if (pid == 0) {
            // ── Child Process ──────────────────────────────────────────
            dup2(pipe_fd[1], STDOUT_FILENO);
            dup2(pipe_fd[1], STDERR_FILENO);
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);

            auto argv = tokenize(command);
            std::vector<char*> args;
            for (auto& s : argv) args.push_back(&s[0]);
            args.push_back(nullptr);

            execvp(args[0], args.data());
            fprintf(stderr, "execvp failed for '%s': %s\n", args[0], strerror(errno));
            _exit(127);
        }

        // ── Parent Process ─────────────────────────────────────────────
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

        if (WIFEXITED(status))
            output += "\n[Exit Code: " + std::to_string(WEXITSTATUS(status)) + "]";
        else if (WIFSIGNALED(status))
            output += "\n[Killed by signal: " + std::to_string(WTERMSIG(status)) + "]";

        return output;
    }

private:
    static std::vector<std::string> tokenize(const std::string& cmd) {
        std::vector<std::string> tokens;
        std::istringstream iss(cmd);
        std::string token;
        while (iss >> token) tokens.push_back(token);
        return tokens;
    }
};

class WorkerNode {
public:
    explicit WorkerNode(const std::string& master_ip)
        : master_fd_(connect_to_master(master_ip)) {}

    void run() {
        LOG_INFO("WORKER") << "Ready — waiting for tasks";

        while (g_running) {
            std::string command = protocol::recv_message(master_fd_.fd());
            if (command.empty()) {
                if (g_running)
                    LOG_WARN("WORKER") << "Master disconnected — shutting down";
                else
                    LOG_INFO("WORKER") << "Shutdown signal received — exiting";
                break;
            }

            LOG_INFO("WORKER") << "Received task: \"" << command << "\"";

            auto t0 = std::chrono::steady_clock::now();
            std::string result = TaskExecutor::execute(command);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();

            LOG_INFO("WORKER") << "Task completed in " << ms << "ms — sending result";

            if (protocol::send_message(master_fd_.fd(), result) < 0) {
                LOG_ERROR("WORKER") << "Failed to send result — connection lost";
                break;
            }
        }

        LOG_INFO("WORKER") << "Shutdown complete";
    }

private:
    Socket master_fd_;

    Socket connect_to_master(const std::string& host) {
        constexpr int MAX_RETRIES  = 10;
        constexpr int RETRY_DELAY  = 2;  // seconds

        std::cout << "===========================================\n"
                  << "  DISTRIBUTED TASK SCHEDULER — WORKER\n"
                  << "===========================================\n";

        // getaddrinfo resolves both hostnames ("master" in Docker) and IPs ("192.168.1.1")
        struct addrinfo hints{}, *res;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(PORT).c_str(), &hints, &res) != 0) {
            LOG_ERROR("WORKER") << "Cannot resolve host: " << host;
            exit(1);
        }

        for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
            int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (fd < 0) {
                LOG_ERROR("WORKER") << "socket() failed: " << strerror(errno);
                freeaddrinfo(res);
                exit(1);
            }

            if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
                freeaddrinfo(res);
                LOG_INFO("WORKER") << "Connected to Master at " << host << ":" << PORT;
                return Socket(fd);
            }

            ::close(fd);
            LOG_WARN("WORKER") << "Connection attempt " << attempt << "/" << MAX_RETRIES
                               << " failed (" << strerror(errno) << ")"
                               << " — retrying in " << RETRY_DELAY << "s";
            sleep(RETRY_DELAY);
        }

        freeaddrinfo(res);
        LOG_ERROR("WORKER") << "Could not connect to Master after "
                            << MAX_RETRIES << " attempts — exiting";
        exit(1);
    }
};

int main(int argc, char* argv[]) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    std::string master_ip = (argc >= 2) ? argv[1] : "127.0.0.1";
    WorkerNode worker(master_ip);
    worker.run();
    return 0;
}
