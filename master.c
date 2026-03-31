/*
 * ============================================================================
 *  master.c — The Master Node (Control Center)
 * ============================================================================
 *
 *  ROLE: The Master is the central coordinator of the distributed system.
 *
 *  RESPONSIBILITIES:
 *    1. Listen for incoming Worker connections (TCP server)
 *    2. Accept and register Workers in an array
 *    3. Read task commands from stdin (operator input)
 *    4. Load-balance: assign each task to the first free Worker
 *    5. Receive results from Workers and display them
 *
 *  KEY SYSTEM CALLS USED:
 *    socket(), bind(), listen(), accept(), select(), send(), recv(), close()
 *
 *  SOCKET LIFECYCLE (Server-side):
 *    socket()  → Create a TCP socket (file descriptor)
 *    bind()    → Attach the socket to an IP address and port
 *    listen()  → Mark the socket as passive (ready to accept connections)
 *    accept()  → Block until a client connects; returns a NEW fd for that client
 *
 *  I/O MULTIPLEXING with select():
 *    Instead of using threads, we use select() to monitor MULTIPLE file
 *    descriptors simultaneously: stdin (for new tasks), the listening socket
 *    (for new workers), and all worker sockets (for results).
 *
 *    Interview Q: "Why select() instead of threads?"
 *    Answer: "select() avoids the complexity of thread synchronization
 *             (mutexes, race conditions). It's event-driven — the kernel
 *             tells us which fd is ready, and we handle it sequentially."
 * ============================================================================
 */

#include "protocol.h"
#include <sys/select.h>

/* ── Global State ──────────────────────────────────────────────────────── */

Worker workers[MAX_WORKERS];    /* Array of connected workers              */
int    worker_count = 0;        /* Number of currently connected workers   */

/* ── Function: setup_server() ──────────────────────────────────────────── */
/*
 * Creates and configures the TCP listening socket.
 *
 * Detailed steps:
 *   1. socket(AF_INET, SOCK_STREAM, 0)
 *      - AF_INET     = IPv4
 *      - SOCK_STREAM = TCP (reliable, ordered, connection-oriented)
 *
 *   2. setsockopt(SO_REUSEADDR)
 *      - Allows restarting the server immediately after a crash.
 *      - Without this, bind() would fail with "Address already in use"
 *        because the OS keeps the port in TIME_WAIT state for ~60 seconds.
 *      - Interview Q: "What is TIME_WAIT?" → It's a TCP state that ensures
 *        all in-flight packets are delivered before releasing the port.
 *
 *   3. bind() — Associate the socket with port 8080 on all interfaces.
 *      - INADDR_ANY means "listen on all available network interfaces."
 *
 *   4. listen(fd, backlog=5)
 *      - The backlog (5) is the max number of pending connections in the
 *        kernel's queue before accept() is called.
 *
 * Returns: the listening socket file descriptor.
 */
int setup_server() {
    int server_fd;
    struct sockaddr_in addr;
    int opt = 1;

    /* Step 1: Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    /* Step 2: Allow port reuse (avoid "Address already in use") */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt() failed");
        exit(EXIT_FAILURE);
    }

    /* Step 3: Bind to port on all interfaces */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;           /* IPv4                     */
    addr.sin_addr.s_addr = INADDR_ANY;        /* All network interfaces   */
    addr.sin_port        = htons(PORT);        /* Convert port to network byte order */

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

    /* Step 4: Start listening (backlog = 5) */
    if (listen(server_fd, 5) < 0) {
        perror("listen() failed");
        exit(EXIT_FAILURE);
    }

    printf("===========================================\n");
    printf("  DISTRIBUTED TASK SCHEDULER — MASTER\n");
    printf("===========================================\n");
    printf("[MASTER] Listening on port %d...\n", PORT);
    printf("[MASTER] Waiting for workers to connect...\n\n");

    return server_fd;
}

/* ── Function: accept_worker() ─────────────────────────────────────────── */
/*
 * Accepts a new worker connection and adds it to the worker registry.
 *
 *   accept() blocks until a client (Worker) calls connect().
 *   It returns a NEW socket fd dedicated to that specific worker.
 *   The original listening socket continues to accept more connections.
 *
 * Interview Q: "What's the difference between the listening fd and the
 *               fd returned by accept()?"
 * Answer: "The listening fd is like a receptionist — it only accepts
 *          new connections. accept() returns a NEW fd for the established
 *          connection. You communicate with the client on this new fd."
 */
void accept_worker(int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
        perror("accept() failed");
        return;
    }

    if (worker_count >= MAX_WORKERS) {
        printf("[MASTER] Max workers reached. Rejecting connection.\n");
        close(client_fd);
        return;
    }

    /* Register the new worker */
    workers[worker_count].fd     = client_fd;
    workers[worker_count].isBusy = 0;
    inet_ntop(AF_INET, &client_addr.sin_addr,
              workers[worker_count].ip, INET_ADDRSTRLEN);

    printf("[MASTER] Worker #%d connected from %s\n",
           worker_count, workers[worker_count].ip);
    worker_count++;
}

/* ── Function: assign_task() ───────────────────────────────────────────── */
/*
 * LOAD BALANCER: Assigns a task to the first non-busy worker.
 *
 * Algorithm: Linear scan of the worker array.
 *   - Find first worker where isBusy == 0
 *   - Send the command string using our length-prefix protocol
 *   - Mark worker as busy (isBusy = 1)
 *
 * Interview Q: "What load balancing algorithm did you use?"
 * Answer: "First-Available selection — O(n) scan of the worker array.
 *          For a production system, I'd consider round-robin, least-
 *          connections, or weighted load balancing."
 *
 * Interview Q: "What happens if all workers are busy?"
 * Answer: "The task is not assigned, and the user is informed. In a
 *          production system, I'd implement a task queue for pending tasks."
 */
void assign_task(const char *command) {
    for (int i = 0; i < worker_count; i++) {
        if (!workers[i].isBusy) {
            printf("[MASTER] Assigning task to Worker #%d (%s): %s\n",
                   i, workers[i].ip, command);

            if (send_message(workers[i].fd, command) == 0) {
                workers[i].isBusy = 1;   /* Mark as busy */
            } else {
                printf("[MASTER] Failed to send task to Worker #%d\n", i);
            }
            return;
        }
    }

    printf("[MASTER] All workers are busy! Try again later.\n");
}

/* ── Function: handle_worker_result() ──────────────────────────────────── */
/*
 * Called when a worker's fd becomes readable (select() signals it).
 * Reads the result, prints it, and marks the worker as free.
 *
 * If recv_message returns 0 or -1, the worker has disconnected or errored.
 * We handle this by removing the worker from the registry.
 */
void handle_worker_result(int worker_index) {
    char buf[BUFFER_SIZE];
    int n = recv_message(workers[worker_index].fd, buf, BUFFER_SIZE);

    if (n <= 0) {
        /* Worker disconnected */
        printf("[MASTER] Worker #%d (%s) disconnected.\n",
               worker_index, workers[worker_index].ip);
        close(workers[worker_index].fd);

        /* Remove worker by shifting the array */
        for (int j = worker_index; j < worker_count - 1; j++) {
            workers[j] = workers[j + 1];
        }
        worker_count--;
        return;
    }

    /* Print the result */
    printf("\n───────────────────────────────────────────\n");
    printf("  Result from Worker #%d (%s):\n", worker_index, workers[worker_index].ip);
    printf("───────────────────────────────────────────\n");
    printf("%s\n", buf);
    printf("───────────────────────────────────────────\n\n");

    /* Mark worker as free */
    workers[worker_index].isBusy = 0;
}

/* ── Function: main() — Event Loop using select() ─────────────────────── */
/*
 * THE MAIN EVENT LOOP:
 *
 * select() monitors multiple file descriptors simultaneously:
 *   - stdin (fd 0):      "Did the operator type a new command?"
 *   - server_fd:         "Is a new worker trying to connect?"
 *   - worker fds:        "Did any worker send back a result?"
 *
 * How select() works:
 *   1. Build an fd_set (a bitmask of file descriptors to watch).
 *   2. Call select() — it BLOCKS until at least one fd is "ready."
 *   3. Check which fds are set using FD_ISSET().
 *   4. Handle the ready fds.
 *   5. Repeat.
 *
 * Interview Q: "What are the alternatives to select()?"
 * Answer: "poll() (no fd limit), epoll() (Linux, O(1) scalable),
 *          kqueue (BSD/macOS). For a small number of fds, select() is fine."
 */
int main() {
    int server_fd = setup_server();

    printf("[MASTER] Type a shell command to distribute (e.g., 'uptime', 'ls -la'):\n\n");

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        /* Add stdin to the watch set */
        FD_SET(STDIN_FILENO, &read_fds);
        int max_fd = STDIN_FILENO;

        /* Add the listening socket */
        FD_SET(server_fd, &read_fds);
        if (server_fd > max_fd) max_fd = server_fd;

        /* Add all worker sockets */
        for (int i = 0; i < worker_count; i++) {
            FD_SET(workers[i].fd, &read_fds);
            if (workers[i].fd > max_fd) max_fd = workers[i].fd;
        }

        /* Block until something is ready */
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) continue;   /* Interrupted by signal, retry */
            perror("select() failed");
            break;
        }

        /* ── Check: New worker connection? ── */
        if (FD_ISSET(server_fd, &read_fds)) {
            accept_worker(server_fd);
        }

        /* ── Check: New command from stdin? ── */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char command[BUFFER_SIZE];
            if (fgets(command, sizeof(command), stdin) == NULL) {
                break;   /* EOF → operator closed stdin */
            }

            /* Remove trailing newline */
            command[strcspn(command, "\n")] = '\0';

            if (strlen(command) == 0) continue;

            /* Special commands */
            if (strcmp(command, "quit") == 0) {
                printf("[MASTER] Shutting down...\n");
                break;
            }
            if (strcmp(command, "status") == 0) {
                printf("\n[MASTER] === Worker Status ===\n");
                for (int i = 0; i < worker_count; i++) {
                    printf("  Worker #%d | IP: %s | %s\n",
                           i, workers[i].ip,
                           workers[i].isBusy ? "BUSY" : "FREE");
                }
                printf("  Total workers: %d\n\n", worker_count);
                continue;
            }

            if (worker_count == 0) {
                printf("[MASTER] No workers connected yet!\n");
                continue;
            }

            assign_task(command);
        }

        /* ── Check: Result from any worker? ── */
        for (int i = 0; i < worker_count; i++) {
            if (FD_ISSET(workers[i].fd, &read_fds)) {
                handle_worker_result(i);
                /* After removal, recheck from same index */
                i--;
            }
        }
    }

    /* Cleanup: close all connections */
    for (int i = 0; i < worker_count; i++) {
        close(workers[i].fd);
    }
    close(server_fd);

    printf("[MASTER] Shutdown complete.\n");
    return 0;
}
