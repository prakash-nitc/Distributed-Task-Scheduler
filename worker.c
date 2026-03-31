/*
 * ============================================================================
 *  worker.c — The Worker Node (Execution Unit)
 * ============================================================================
 *
 *  ROLE: The Worker connects to the Master, receives shell commands,
 *        executes them in a child process, captures the output, and
 *        sends the result back.
 *
 *  THIS IS THE HEART OF THE PROJECT — it demonstrates:
 *    fork()    → Create a new process
 *    pipe()    → Create a unidirectional data channel between processes
 *    dup2()    → Redirect stdout to the pipe (so we can capture output)
 *    execvp()  → Replace the child process with the shell command
 *    waitpid() → Wait for the child to finish and get its exit status
 *
 *  PROCESS FLOW:
 *  ┌─────────────────────────────────────────────────────────┐
 *  │  Worker (Parent)                                        │
 *  │    │                                                    │
 *  │    ├─ pipe()  → creates pipe_fd[0] (read), pipe_fd[1]   │
 *  │    │              (write)                                │
 *  │    ├─ fork()  → creates child process                   │
 *  │    │                                                    │
 *  │    ├─ [CHILD PROCESS]                                   │
 *  │    │    ├─ dup2(pipe_fd[1], STDOUT) → redirect stdout   │
 *  │    │    ├─ dup2(pipe_fd[1], STDERR) → redirect stderr   │
 *  │    │    ├─ close unused pipe ends                       │
 *  │    │    └─ execvp(command) → replace with shell command │
 *  │    │                                                    │
 *  │    ├─ [PARENT PROCESS]                                  │
 *  │    │    ├─ close write end of pipe                      │
 *  │    │    ├─ read() from pipe → capture command output    │
 *  │    │    ├─ waitpid() → get child's exit status          │
 *  │    │    └─ send result back to Master                   │
 *  │    │                                                    │
 *  └─────────────────────────────────────────────────────────┘
 *
 *  SOCKET LIFECYCLE (Client-side):
 *    socket()  → Create a TCP socket
 *    connect() → Initiate connection to the Master's IP:PORT
 * ============================================================================
 */

#include "protocol.h"

/* ── Function: connect_to_master() ─────────────────────────────────────── */
/*
 * Establishes a TCP connection to the Master node.
 *
 *   socket()  — Creates a new TCP socket (file descriptor).
 *   connect() — Initiates the TCP 3-way handshake with the Master.
 *
 *   The 3-Way Handshake (Interview favorite!):
 *     Client → Server: SYN  (synchronize)
 *     Server → Client: SYN-ACK  (synchronize-acknowledge)
 *     Client → Server: ACK  (acknowledge)
 *     → Connection established!
 *
 * Interview Q: "What happens if the Master is not running when connect() is called?"
 * Answer: "connect() will fail and return -1. The kernel sends a SYN packet,
 *          but receives a RST (reset) back, or times out. errno will be set
 *          to ECONNREFUSED or ETIMEDOUT."
 *
 * Parameters:
 *   master_ip — the IP address of the Master (e.g., "127.0.0.1" for localhost)
 *
 * Returns: the connected socket file descriptor.
 */
int connect_to_master(const char *master_ip) {
    int sock_fd;
    struct sockaddr_in master_addr;

    /* Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    /* Configure the Master's address */
    memset(&master_addr, 0, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port   = htons(PORT);

    /* Convert IP string to binary form */
    if (inet_pton(AF_INET, master_ip, &master_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid Master IP address: %s\n", master_ip);
        exit(EXIT_FAILURE);
    }

    /* Connect to the Master (triggers 3-way handshake) */
    if (connect(sock_fd, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
        perror("connect() failed — is the Master running?");
        exit(EXIT_FAILURE);
    }

    printf("[WORKER] Connected to Master at %s:%d\n", master_ip, PORT);
    return sock_fd;
}

/* ── Function: tokenize_command() ──────────────────────────────────────── */
/*
 * Splits a command string into an argv-style array for execvp().
 *
 * Example: "ls -la /tmp" → argv = {"ls", "-la", "/tmp", NULL}
 *
 * execvp() requires:
 *   - argv[0] = the program name (e.g., "ls")
 *   - argv[1..n] = arguments
 *   - argv[n+1] = NULL (sentinel — marks the end of the array)
 *
 * Interview Q: "Why does execvp need a NULL-terminated argv?"
 * Answer: "C doesn't track array lengths. The NULL sentinel tells execvp()
 *          where the argument list ends. Without it, execvp() would read
 *          garbage memory past the last argument."
 *
 * We use strtok() to split on whitespace. This is a simplified tokenizer
 * that doesn't handle quotes or escapes (that's what a real shell does).
 */
#define MAX_ARGS 64

void tokenize_command(char *command, char **argv) {
    int i = 0;
    char *token = strtok(command, " \t");

    while (token != NULL && i < MAX_ARGS - 1) {
        argv[i++] = token;
        token = strtok(NULL, " \t");
    }
    argv[i] = NULL;   /* NULL sentinel for execvp() */
}

/* ── Function: execute_command() ───────────────────────────────────────── */
/*
 * THE CORE FUNCTION — Executes a shell command and captures its output.
 *
 * This function demonstrates 5 critical UNIX system calls:
 *
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │  1. pipe(pipe_fd)                                                   │
 * │     Creates a unidirectional data channel:                          │
 * │       pipe_fd[0] = READ end  (parent reads from here)               │
 * │       pipe_fd[1] = WRITE end (child writes here — via dup2)         │
 * │                                                                     │
 * │     Think of it as a one-way tube connecting two processes.          │
 * │                                                                     │
 * │  2. fork()                                                          │
 * │     Creates an EXACT COPY of the current process.                   │
 * │     Returns:                                                        │
 * │       0       → in the child process                                │
 * │       > 0     → in the parent process (value = child's PID)         │
 * │       -1      → error                                               │
 * │                                                                     │
 * │     Interview Q: "Does fork() copy all memory?"                     │
 * │     Answer: "Initially, no. Linux uses Copy-On-Write (COW). Both    │
 * │              processes share the same physical memory pages. A copy  │
 * │              is made only when one of them tries to WRITE to a page."│
 * │                                                                     │
 * │  3. dup2(pipe_fd[1], STDOUT_FILENO)  [in child]                     │
 * │     Redirects stdout to the WRITE end of the pipe.                  │
 * │     After this, anything the child prints goes into the pipe,       │
 * │     NOT to the terminal.                                            │
 * │                                                                     │
 * │     Interview Q: "How does dup2() work internally?"                 │
 * │     Answer: "Every process has a file descriptor table. dup2(old,   │
 * │              new) closes 'new' if it's open, then makes 'new'       │
 * │              point to the same kernel file object as 'old'.         │
 * │              So fd 1 (stdout) now points to the pipe."              │
 * │                                                                     │
 * │  4. execvp(argv[0], argv)  [in child]                               │
 * │     Replaces the child process's memory with the command binary.    │
 * │     The child process IS NOW the command (e.g., 'ls' or 'uptime').  │
 * │     execvp() NEVER RETURNS on success. If it returns, it failed.    │
 * │                                                                     │
 * │     The 'v' in execvp = takes an argv array.                        │
 * │     The 'p' in execvp = searches PATH for the program.              │
 * │                                                                     │
 * │     Interview Q: "Difference between exec() variants?"              │
 * │     Answer:                                                         │
 * │       execl()  — args as a list: execl("/bin/ls", "ls", "-l", NULL) │
 * │       execv()  — args as an array: execv("/bin/ls", argv)           │
 * │       execlp() — like execl but searches PATH                       │
 * │       execvp() — like execv but searches PATH  ← WE USE THIS       │
 * │       execve() — like execv but you also pass the environment       │
 * │                                                                     │
 * │  5. waitpid(pid, &status, 0)  [in parent]                           │
 * │     Blocks until the child process exits.                           │
 * │     The 'status' integer encodes the exit status.                   │
 * │                                                                     │
 * │     WIFEXITED(status)   — did the child exit normally?              │
 * │     WEXITSTATUS(status) — what was the exit code (0 = success)?     │
 * │                                                                     │
 * │     Interview Q: "What is a zombie process?"                        │
 * │     Answer: "A child process that has terminated but whose parent   │
 * │              hasn't called waitpid() yet. It still occupies a slot  │
 * │              in the process table. waitpid() 'reaps' the zombie."   │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * Parameters:
 *   command     — the shell command to execute (e.g., "uptime")
 *   result      — buffer to store the captured output
 *   result_size — size of the result buffer
 */
void execute_command(const char *command, char *result, int result_size) {
    int pipe_fd[2];   /* pipe_fd[0]=read, pipe_fd[1]=write */

    /* ── Step 1: Create a pipe ── */
    if (pipe(pipe_fd) < 0) {
        perror("pipe() failed");
        snprintf(result, result_size, "[ERROR] pipe() failed: %s", strerror(errno));
        return;
    }

    /* ── Step 2: Fork a child process ── */
    pid_t pid = fork();

    if (pid < 0) {
        /* fork() failed */
        perror("fork() failed");
        snprintf(result, result_size, "[ERROR] fork() failed: %s", strerror(errno));
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }

    if (pid == 0) {
        /* ═══════════════════════ CHILD PROCESS ═══════════════════════ */

        /* Step 3a: Redirect stdout to the pipe's write end */
        dup2(pipe_fd[1], STDOUT_FILENO);

        /* Step 3b: Redirect stderr to the pipe's write end too */
        dup2(pipe_fd[1], STDERR_FILENO);

        /* Step 3c: Close unused file descriptors */
        close(pipe_fd[0]);   /* Child doesn't read from pipe */
        close(pipe_fd[1]);   /* Already duplicated into stdout/stderr */

        /* Step 4: Tokenize and execute the command */
        char cmd_copy[BUFFER_SIZE];
        strncpy(cmd_copy, command, BUFFER_SIZE - 1);
        cmd_copy[BUFFER_SIZE - 1] = '\0';

        char *argv[MAX_ARGS];
        tokenize_command(cmd_copy, argv);

        /* execvp() replaces this process with the command.
         * If it returns, something went wrong. */
        execvp(argv[0], argv);

        /* If we reach here, execvp failed */
        fprintf(stderr, "execvp() failed for '%s': %s\n", argv[0], strerror(errno));
        _exit(127);   /* Use _exit(), not exit() — avoids flushing parent's buffers */
    }

    /* ═══════════════════════ PARENT PROCESS ═══════════════════════ */

    /* Close the write end — parent only reads */
    close(pipe_fd[1]);

    /* Step 5a: Read the child's output from the pipe */
    int total = 0;
    ssize_t n;
    while ((n = read(pipe_fd[0], result + total, result_size - total - 1)) > 0) {
        total += n;
        if (total >= result_size - 1) break;
    }
    result[total] = '\0';
    close(pipe_fd[0]);

    /* Step 5b: Wait for the child to finish (reap the zombie) */
    int status;
    waitpid(pid, &status, 0);

    /* Append exit status information */
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        char status_line[128];
        snprintf(status_line, sizeof(status_line),
                 "\n[Exit Code: %d]", exit_code);
        strncat(result, status_line, result_size - strlen(result) - 1);
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        char status_line[128];
        snprintf(status_line, sizeof(status_line),
                 "\n[Killed by signal: %d]", sig);
        strncat(result, status_line, result_size - strlen(result) - 1);
    }
}

/* ── Function: worker_loop() ───────────────────────────────────────────── */
/*
 * The main worker loop:
 *   1. Wait for a task from the Master (recv_message)
 *   2. Execute the task (execute_command)
 *   3. Send the result back to the Master (send_message)
 *   4. Repeat until disconnected
 *
 * Interview Q: "What happens if the Master crashes mid-task?"
 * Answer: "recv_message() will return 0 or -1 (the TCP connection is broken).
 *          The worker detects this and exits gracefully."
 */
void worker_loop(int master_fd) {
    char command[BUFFER_SIZE];
    char result[BUFFER_SIZE];

    printf("[WORKER] Ready. Waiting for tasks...\n\n");

    while (1) {
        /* Receive a task from the Master */
        int n = recv_message(master_fd, command, BUFFER_SIZE);

        if (n <= 0) {
            printf("[WORKER] Master disconnected. Shutting down.\n");
            break;
        }

        printf("[WORKER] Received task: %s\n", command);

        /* Execute the command and capture output */
        memset(result, 0, sizeof(result));
        execute_command(command, result, BUFFER_SIZE);

        printf("[WORKER] Task completed. Sending result...\n");

        /* Send the result back to the Master */
        if (send_message(master_fd, result) < 0) {
            printf("[WORKER] Failed to send result. Master may have disconnected.\n");
            break;
        }
    }
}

/* ── main() ────────────────────────────────────────────────────────────── */
/*
 * Usage: ./worker [master_ip]
 *   - If no IP is given, defaults to 127.0.0.1 (localhost)
 *   - This allows running workers on different machines in a real network
 */
int main(int argc, char *argv[]) {
    const char *master_ip = "127.0.0.1";   /* Default: localhost */

    if (argc >= 2) {
        master_ip = argv[1];
    }

    printf("===========================================\n");
    printf("  DISTRIBUTED TASK SCHEDULER — WORKER\n");
    printf("===========================================\n");

    int master_fd = connect_to_master(master_ip);
    worker_loop(master_fd);

    close(master_fd);
    printf("[WORKER] Shutdown complete.\n");
    return 0;
}
