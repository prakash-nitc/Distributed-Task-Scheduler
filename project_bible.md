# 📘 Distributed Task Scheduler — Project Bible (Interview Preparation)

> **Purpose**: A complete interview-preparation reference covering every concept, system call, C++ feature, Python component, and design decision in this project.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architecture & Data Flow](#2-architecture--data-flow)
3. [The Wire Protocol (Length-Prefix Framing)](#3-the-wire-protocol-length-prefix-framing)
4. [The Networking Layer — Socket Programming](#4-the-networking-layer--socket-programming)
5. [I/O Multiplexing with select()](#5-io-multiplexing-with-select)
6. [Process Creation — fork()](#6-process-creation--fork)
7. [Process Execution — The exec() Family](#7-process-execution--the-exec-family)
8. [File Descriptor Redirection — dup2()](#8-file-descriptor-redirection--dup2)
9. [Inter-Process Communication — pipe()](#9-inter-process-communication--pipe)
10. [Waiting for Children — waitpid()](#10-waiting-for-children--waitpid)
11. [Load Balancing & Task Queue](#11-load-balancing--task-queue)
12. [C++ Concepts Used](#12-c-concepts-used)
13. [Python Client — Cross-Language Interoperability](#13-python-client--cross-language-interoperability)
14. [Code Walkthrough](#14-code-walkthrough)
15. [Error Handling & Edge Cases](#15-error-handling--edge-cases)
16. [How to Build and Run](#16-how-to-build-and-run)
17. [Interview Questions — Sockets & Networking (Q1–Q10)](#17-interview-questions--sockets--networking)
18. [Interview Questions — Process Management (Q11–Q22)](#18-interview-questions--process-management)
19. [Interview Questions — I/O and Pipes (Q23–Q28)](#19-interview-questions--io-and-pipes)
20. [Interview Questions — C++ Concepts (Q29–Q38)](#20-interview-questions--c-concepts)
21. [Interview Questions — Python & Cross-Language (Q39–Q43)](#21-interview-questions--python--cross-language)
22. [Interview Questions — Architecture & Design (Q44–Q50)](#22-interview-questions--architecture--design)
23. [How to Extend This Project](#23-how-to-extend-this-project)
24. [Glossary](#24-glossary)
25. [Structured Logging — logger.hpp](#25-structured-logging--loggerhpp)
26. [Docker & Containerization — Complete Beginner Guide](#26-docker--containerization--complete-beginner-guide)
27. [CMake Build System](#27-cmake-build-system)
28. [Interview Questions — Logging, Docker & CMake (Q51–Q75)](#28-interview-questions--logging-docker--cmake-q51q75)

---

## 1. Project Overview

### What is this project?

A **Distributed Task Scheduler** built with **C++** and **Python** using the **Master-Worker model**. The system distributes shell commands across multiple Worker processes for parallel execution.

### Tech Stack

| Component | Language | Why |
|---|---|---|
| **Master** | C++ | Classes, STL containers, RAII for resource management |
| **Worker** | C++ | POSIX process APIs (fork/exec/dup2/pipe) + C++ structure |
| **Client** | Python | Rapid prototyping, demonstrate cross-language interop |
| **Protocol** | Shared | Length-prefix framing works across both languages |
| **Logger** | C++ (logger.hpp) | Structured timestamps, log levels, ANSI colors, thread-safe |
| **Docker** | Dockerfile + Compose | Containerized deployment, one-command cluster startup |
| **CMake** | CMakeLists.txt | Industry-standard cross-platform build system |

### What it demonstrates

| Concept | Where |
|---|---|
| TCP Socket Programming | Master (server) + Worker/Client (clients) |
| Process Creation (fork) | Worker creates child processes |
| Process Execution (exec) | Child runs the shell command |
| I/O Redirection (dup2) | Captures stdout to a pipe |
| IPC (pipe) | Output flows from child to parent |
| Load Balancing | Master picks first free worker |
| Task Queue | `std::queue` buffers tasks when all workers busy |
| RAII | Socket class auto-closes fds |
| Cross-Language Protocol | Same wire format in C++ and Python |
| Structured Logging | Timestamps, levels, colors, mutex, isatty |
| Containerization | Multi-stage Docker build, docker-compose orchestration |
| DNS in Docker | getaddrinfo() resolves container service names |

---

## 2. Architecture & Data Flow

```
┌──────────────────────────────────────────────────┐
│                  MASTER NODE (C++)                │
│                                                   │
│  ┌─────────────┐  ┌───────────────┐  ┌──────────┐│
│  │ TCP Server   │  │ Worker        │  │ Load     ││
│  │ Socket       │  │ Registry      │  │ Balancer ││
│  │              │  │ std::vector   │  │          ││
│  └─────────────┘  └───────────────┘  └──────────┘│
│                                                   │
│  ┌────────────────────────────────────────────┐   │
│  │ Task Queue: std::queue<string>              │   │
│  │ Auto-dispatches when workers become free    │   │
│  └────────────────────────────────────────────┘   │
│                                                   │
│  ┌────────────────────────────────────────────┐   │
│  │ select() Event Loop                         │   │
│  │ Monitors: stdin + server fd + worker fds    │   │
│  └────────────────────────────────────────────┘   │
└───────────┬────────────────────────┬──────────────┘
            │ TCP                    │ TCP
      ┌─────▼──────┐          ┌─────▼──────┐
      │ WORKER (C++)│          │ CLIENT (Py)│
      │             │          │            │
      │ fork()      │          │ socket lib │
      │ dup2()      │          │ struct lib │
      │ execvp()    │          │ interactive│
      │ waitpid()   │          │ CLI        │
      └─────────────┘          └────────────┘
```

### End-to-End Flow

```
1. Operator types "uptime" (via Master stdin or Python client)
2. Master's select() detects the input
3. Load Balancer finds first worker where is_busy == false
4. Master sends: [4 bytes: length=6] + [6 bytes: "uptime"]
5. Worker receives the message
6. Worker: pipe() → fork()
7.   CHILD: dup2(pipe→stdout) → execvp("uptime")
8.   PARENT: read(pipe) → waitpid() → capture output + exit code
9. Worker sends result back via TCP
10. Master's select() detects worker fd is readable
11. Master prints result, marks worker as free
12. If tasks are queued: auto-dispatch next task
```

---

## 3. The Wire Protocol (Length-Prefix Framing)

### The Problem

TCP is a **byte-stream protocol** — no message boundaries. `send("Hello")` + `send("World")` might arrive as `"HelloWorld"` or `"He"` + `"lloWorld"`.

### The Solution

Before every message, send a **4-byte big-endian integer** containing the message length:

```
┌──────────────────┬─────────────────────────────┐
│ 4 bytes (uint32)  │  N bytes (payload)          │
│ Message Length    │  Message Data               │
│ (big-endian)     │  (UTF-8 string)             │
└──────────────────┴─────────────────────────────┘
```

### In C++

```cpp
uint32_t net_len = htonl(len);
send(fd, &net_len, 4, 0);
send(fd, msg.c_str(), len, 0);
```

### In Python (same protocol!)

```python
header = struct.pack("!I", len(data))   # "!I" = big-endian unsigned 32-bit
sock.sendall(header + data)
```

Both use **network byte order (big-endian)** — this is why the Python client can talk to the C++ Master seamlessly.

### Why htonl() / ntohl()?

x86 CPUs are **little-endian** (least significant byte first). Network protocols use **big-endian** (most significant byte first). `htonl()` converts host→network and `ntohl()` converts network→host.

---

## 4. The Networking Layer — Socket Programming

### Socket Lifecycle

**Server (Master):**
```
socket() → bind() → listen() → accept() → recv()/send() → close()
```

**Client (Worker / Python Client):**
```
socket() → connect() → recv()/send() → close()
```

### Key System Calls

| Call | Purpose |
|---|---|
| `socket(AF_INET, SOCK_STREAM, 0)` | Create a TCP socket (returns fd) |
| `bind(fd, addr, len)` | Attach socket to IP+port |
| `listen(fd, backlog)` | Mark as passive (accept connections) |
| `accept(fd, ...)` | Block until a client connects; returns a new fd |
| `connect(fd, addr, len)` | Client: connect to server (3-way handshake) |
| `setsockopt(SO_REUSEADDR)` | Allow port reuse (avoid "Address in use") |
| `getaddrinfo(host, port, ...)` | Resolve hostname or IP to a sockaddr struct |

### TCP 3-Way Handshake

```
Client              Server
  │── SYN ────────────►│
  │◄── SYN-ACK ───────│
  │── ACK ────────────►│
  → Connection established
```

### TCP vs UDP

| Feature | TCP | UDP |
|---|---|---|
| Connection | Connection-oriented | Connectionless |
| Reliability | Guaranteed (ACKs) | No guarantees |
| Ordering | In-order | No ordering |
| Message boundaries | NO (stream) | YES (datagrams) |
| Use in this project | ✅ Can't lose tasks | ✗ |

---

## 5. I/O Multiplexing with select()

The Master monitors multiple fds simultaneously without threads:

```cpp
fd_set read_fds;
FD_ZERO(&read_fds);
FD_SET(STDIN_FILENO, &read_fds);    // Operator input
FD_SET(server_fd, &read_fds);       // New worker connections
for (auto& w : workers_)
    FD_SET(w.fd, &read_fds);        // Worker results

select(max_fd + 1, &read_fds, NULL, NULL, NULL);  // BLOCK

if (FD_ISSET(server_fd, &read_fds))    // → accept_worker()
if (FD_ISSET(STDIN_FILENO, &read_fds)) // → assign_task()
for (auto& w : workers_)
    if (FD_ISSET(w.fd, &read_fds))     // → handle_result()
```

### select() vs alternatives

| Method | Pros | Cons |
|---|---|---|
| **select()** | Simple, portable | O(n) scan, FD_SETSIZE=1024 |
| **poll()** | No fd limit | Still O(n) |
| **epoll()** | O(1), scalable | Linux-only |
| **Threads** | True parallelism | Race conditions, mutexes |

For our small number of connections, select() is ideal.

---

## 6. Process Creation — fork()

`fork()` creates a new process by duplicating the caller.

```cpp
pid_t pid = fork();
// pid > 0 → parent (value is child's PID)
// pid == 0 → child
// pid < 0 → error
```

### Copy-On-Write (COW)

After fork(), parent and child share the same physical memory pages (marked read-only). A page is copied only when one process writes to it. This makes fork() fast, especially when the child immediately calls exec().

```
After fork() (COW):
  Parent: [Page A] [Page B] [Page C]  ← shared, read-only
  Child:      ↑        ↑        ↑

After child writes to Page B:
  Parent: [Page A] [Page B ] [Page C]
  Child:      ↑    [Page B']     ↑     ← only this page copied
```

### Why fork() in this project?

`execvp()` **replaces** the current process. Without fork(), the Worker itself would become the command and stop being a Worker. Fork creates a disposable child for exec.

---

## 7. Process Execution — The exec() Family

`exec()` replaces the current process's memory with a new program. **Never returns on success.**

### Variants

| Function | Args | PATH search? |
|---|---|---|
| `execl()` | list | No |
| `execlp()` | list | Yes |
| `execv()` | array | No |
| **`execvp()`** | **array** | **Yes** ← we use this |
| `execve()` | array+env | No |

Memory aid: **v** = vector (array), **p** = PATH, **e** = environment, **l** = list.

### Why execvp()?

- **v**: We tokenize the command into a `std::vector`, so we need the array variant.
- **p**: Users type `"uptime"` not `"/usr/bin/uptime"`, so we need PATH searching.

### _exit() vs exit() in child

After a failed execvp(), the child calls `_exit(127)` — not `exit()`. `exit()` flushes stdio buffers shared with the parent (causing double output). `_exit()` terminates immediately.

---

## 8. File Descriptor Redirection — dup2()

`dup2(old_fd, new_fd)` makes new_fd point to the same file as old_fd.

```cpp
dup2(pipe_fd[1], STDOUT_FILENO);
// Now fd 1 (stdout) → pipe write end
// Anything printed goes into the pipe, not the terminal
```

### FD Table Before and After

```
Before:  fd 1 → terminal
After:   fd 1 → pipe write end

So when execvp("uptime") prints output → it goes into the pipe
→ the parent reads it from the pipe's read end
```

This is the same mechanism shells use for `ls | grep`.

---

## 9. Inter-Process Communication — pipe()

```cpp
int pipe_fd[2];
pipe(pipe_fd);
// pipe_fd[0] = READ end
// pipe_fd[1] = WRITE end
```

After fork(), close unused ends:
- **Parent**: close write end, read from read end
- **Child**: close read end, dup2 write end to stdout, close write end

### Why close unused ends?

1. `read()` returns EOF only when ALL write ends are closed
2. File descriptors are a limited resource

---

## 10. Waiting for Children — waitpid()

```cpp
int status;
waitpid(pid, &status, 0);

if (WIFEXITED(status))
    int code = WEXITSTATUS(status);   // 0 = success
if (WIFSIGNALED(status))
    int sig = WTERMSIG(status);       // killed by signal
```

### Zombie processes

A terminated child not yet reaped by waitpid(). Takes a process table slot. We always call waitpid() to prevent zombies.

### Orphan processes

A child whose parent died. Adopted by init (PID 1). Not harmful.

---

## 11. Load Balancing & Task Queue

### First-Available Load Balancing

```cpp
bool assign_task(const std::string& command) {
    for (auto& w : workers_) {
        if (!w.is_busy) {
            protocol::send_message(w.fd, command);
            w.is_busy = true;
            return true;
        }
    }
    return false;   // All busy
}
```

### Task Queue (std::queue)

When all workers are busy, tasks are buffered:

```cpp
if (!assign_task(command)) {
    task_queue_.push(command);   // Buffer it
}
```

When a worker finishes, the next queued task is auto-dispatched:

```cpp
void handle_result(size_t index) {
    // ... receive result, mark worker free ...
    
    if (!task_queue_.empty()) {
        std::string next = task_queue_.front();
        task_queue_.pop();
        assign_task(next);
    }
}
```

This is a **producer-consumer pattern** — stdin produces tasks, workers consume them, and `std::queue` is the buffer.

### Load Balancing Comparison

| Strategy | How | Complexity |
|---|---|---|
| **First-Available** (ours) | Pick first free worker | O(n) |
| Round-Robin | Cycle through workers | O(1) |
| Least-Connections | Fewest active tasks | O(n) |
| Weighted | Based on capacity | O(n) |

---

## 12. C++ Concepts Used

### RAII (Resource Acquisition Is Initialization)

The `Socket` class wraps a file descriptor and closes it in the destructor:

```cpp
class Socket {
    int fd_;
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { if (fd_ >= 0) ::close(fd_); }
    
    // Move semantics — transfer ownership
    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    
    // No copying (unique ownership)
    Socket(const Socket&) = delete;
};
```

**Why RAII?** If an exception is thrown or a function returns early, the destructor still runs, preventing fd leaks. This is the C++ alternative to Go's `defer` or Python's `with`.

### std::vector — Dynamic Worker Registry

```cpp
std::vector<Worker> workers_;
workers_.emplace_back(client_fd, ip_str);   // Add worker
workers_.erase(workers_.begin() + i);        // Remove disconnected worker
```

vs C: No manual `realloc()`, no buffer overflow bugs, automatic memory management.

### std::queue — Task Buffer

```cpp
std::queue<std::string> task_queue_;
task_queue_.push(command);                  // Enqueue
std::string next = task_queue_.front();     // Peek
task_queue_.pop();                          // Dequeue
```

Internally, `std::queue` uses `std::deque` as its underlying container.

### std::string — Safe String Handling

No `char buf[4096]`, no `strcpy`/`strcat` buffer overflows, no manual null-termination. `std::string` manages memory automatically.

### Move Semantics

The Socket class is **movable but not copyable** — like `std::unique_ptr`. This prevents two objects from closing the same fd:

```cpp
Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;   // Source gives up ownership
}
```

### std::mutex and std::lock_guard — Thread Safety

Used in `logger.hpp` to protect log output from concurrent threads:

```cpp
std::mutex mutex_;

void log(...) {
    std::lock_guard<std::mutex> lock(mutex_); // Acquired here
    std::cout << message;
    // lock goes out of scope → mutex released automatically (RAII)
}
```

`std::lock_guard` is an RAII mutex wrapper — it acquires the mutex on construction and releases it on destruction, even if an exception is thrown.

### std::chrono — Timestamps and Durations

Used in `logger.hpp` for timestamps and in `worker.cpp` for task timing:

```cpp
// Timestamp: wall clock
auto now = std::chrono::system_clock::now();

// Task timing: monotonic clock (never goes backward)
auto t0 = std::chrono::steady_clock::now();
auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - t0).count();
```

### Namespaces

All code is under `namespace scheduler` to avoid name collisions. The protocol helpers are in `scheduler::protocol`.

---

## 13. Python Client — Cross-Language Interoperability

### Why Python?

The Python client demonstrates that our wire protocol is **language-agnostic**. Both C++ and Python implement the same 4-byte length-prefix format.

### The struct Module

Python's `struct` module packs/unpacks binary data — the Python equivalent of `htonl()`:

```python
# Pack: integer → 4 bytes (big-endian)
header = struct.pack("!I", length)
# "!" = network byte order (big-endian)
# "I" = unsigned 32-bit integer

# Unpack: 4 bytes → integer
length = struct.unpack("!I", header)[0]
```

### Python Socket vs C++ Socket

| | C++ | Python |
|---|---|---|
| Create | `socket(AF_INET, SOCK_STREAM, 0)` | `socket.socket(socket.AF_INET, socket.SOCK_STREAM)` |
| Connect | `connect(fd, addr, len)` | `sock.connect((host, port))` |
| Send all | Manual loop | `sock.sendall(data)` |
| Receive | `recv(fd, buf, len, 0)` | `sock.recv(n)` |
| Close | `close(fd)` | `sock.close()` |

### Read-Exactly Pattern

Both C++ and Python implement a loop to read exactly N bytes:

```python
def _recv_exact(self, n):
    buf = b""
    while len(buf) < n:
        chunk = self.sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf
```

This handles TCP fragmentation — you might not get all bytes in a single recv().

---

## 14. Code Walkthrough

### File Structure

```
├── protocol.hpp       # Shared: RAII Socket, Worker struct, wire protocol
├── logger.hpp         # Structured logger: levels, timestamps, colors, mutex
├── master.cpp         # Master class: server, select loop, queue, LB
├── worker.cpp         # TaskExecutor + WorkerNode classes
├── client.py          # Python client: TaskClient class
├── Dockerfile         # Multi-stage Docker build (builder + slim runtime)
├── docker-compose.yml # Orchestrates master + N workers in containers
├── .dockerignore      # Excludes binaries/build artifacts from Docker context
├── CMakeLists.txt     # Modern CMake build with Threads, install targets
├── Makefile           # Simple make build (alternative to CMake)
└── project_bible.md   # This file
```

### protocol.hpp — Key Design

- `namespace scheduler` wraps everything
- `Socket` class: RAII, movable, not copyable
- `Worker` struct: fd, ip (std::string), is_busy
- `protocol::send_message()` / `protocol::recv_message()` — return std::string

### logger.hpp — Key Design

```cpp
// Singleton logger — one global instance
Logger::get().log(level, component, message);

// Macro API — stream-based, no overhead if level filtered
LOG_INFO("MASTER") << "Worker #" << i << " connected from " << ip;
LOG_WARN("MASTER") << "All workers busy (" << task_queue_.size() << " queued)";
LOG_ERROR("WORKER") << "connect() failed: " << strerror(errno);
```

### master.cpp — Master Class

```cpp
class Master {
    Socket server_fd_;                    // RAII — auto-closes
    std::vector<Worker> workers_;         // Dynamic registry
    std::queue<std::string> task_queue_;  // Pending tasks buffer
    bool interactive_;                    // isatty(STDIN_FILENO) — false in Docker

    void run();              // select() event loop
    void accept_worker();    // Register new worker
    bool assign_task();      // First-available load balancing
    void handle_result();    // Receive result, dispatch queued tasks
};
```

### worker.cpp — Two Classes

```cpp
class TaskExecutor {
    static std::string execute(const std::string& cmd);
    //  pipe() → fork() → child: dup2()+execvp()
    //                  → parent: read(pipe)+waitpid()
    //  Returns: output + "[Exit Code: N]" + timing
};

class WorkerNode {
    Socket master_fd_;      // RAII connection to master
    void run();             // recv → execute → send loop
    // connect_to_master(): uses getaddrinfo() for hostname + IP support
    //   retries 10 times with 2s delay (Docker startup robustness)
};
```

### client.py — TaskClient Class

```python
class TaskClient:
    def connect(self):       # TCP connection
    def send_message(self):  # Length-prefix send (struct.pack)
    def recv_message(self):  # Length-prefix recv (struct.unpack)
```

---

## 15. Error Handling & Edge Cases

| Scenario | What Happens |
|---|---|
| Worker crashes mid-task | Master detects broken TCP (recv returns empty), removes worker, task result lost |
| Master crashes | Workers detect disconnect (recv returns empty), exit gracefully |
| execvp fails (bad command) | Error written to stderr (redirected to pipe), exit code 127 |
| All workers busy | Task queued in `std::queue`, auto-dispatched when worker frees up |
| Partial recv() | Read-exactly loops in both C++ and Python handle TCP fragmentation |
| EINTR (signal interrupt) | select() is retried on EINTR |
| Master stdin closes (Docker) | `interactive_` flag set to false, master continues as TCP-only server |
| Worker can't connect to master | Retries 10 times with 2s delay; logs each attempt with reason |
| Hostname resolution fails | `getaddrinfo()` fails, worker logs error and exits cleanly |
| Max workers reached | Connection rejected immediately with a WARN log |

---

## 16. How to Build and Run

### Method 1: Makefile (Quickest)

```bash
# Build (in WSL or Linux)
cd '/mnt/c/project/Distributed Task Scheduler'
make

# Terminal 1: Master
./master

# Terminal 2: Worker (IP or hostname)
./worker              # default: 127.0.0.1
./worker 192.168.1.100

# Terminal 3: Python Client
python3 client.py
python3 client.py 192.168.1.100 8080

# Commands:
uptime    ls -la    date    whoami    status    quit
```

### Method 2: CMake (Recommended for Development)

```bash
# Configure (one time)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# CMake prints:
# -- Distributed Task Scheduler v1.0.0
# -- Build type : Release
# -- C++ standard: C++17
# -- Compiler   : GNU 12.3.0

# Build
make -j$(nproc)

# Run (same as Makefile, binaries are in build/)
./master
./worker 127.0.0.1

# Rebuild after code changes (only recompiles what changed):
cmake --build .

# Install to /usr/local/bin (optional)
cmake --install .
```

### Method 3: Docker — One-Command Cluster (Recommended for Demo)

```bash
# Build the Docker image
docker-compose build

# Launch master + 3 workers (everything in containers)
docker-compose up --scale worker=3

# You'll see structured logs from all containers:
# scheduler-master   | [2026-05-17 14:32:01.123] [INFO ] [MASTER] Listening on port 8080
# scheduler-master   | [2026-05-17 14:32:03.456] [INFO ] [MASTER] Worker #0 connected from 172.18.0.3
# scheduler-worker_1 | [2026-05-17 14:32:03.451] [INFO ] [WORKER] Connected to Master at master:8080

# In another terminal — submit tasks from your laptop:
python3 client.py 127.0.0.1 8080
>>> uptime
>>> ls -la /

# OR submit from inside the container:
docker exec -it scheduler-master python3 client.py master 8080

# Scale up to 5 workers without restart:
docker-compose up --scale worker=5

# Stop everything:
docker-compose down

# Follow logs (useful for demos):
docker-compose logs -f
docker-compose logs -f master   # master only
docker-compose logs -f worker   # all workers
```

---

## 17. Interview Questions — Sockets & Networking

**Q1: Explain the socket lifecycle for a TCP server.**
> `socket()` creates an endpoint → `bind()` assigns an address → `listen()` marks as passive → `accept()` blocks until a client connects and returns a new fd → `recv()`/`send()` for communication → `close()`.

**Q2: What is the difference between TCP and UDP?**
> TCP: connection-oriented, reliable (ACKs + retransmission), ordered, byte-stream. UDP: connectionless, unreliable, unordered, message-oriented. We use TCP because we can't lose tasks.

**Q3: What is the TCP 3-way handshake?**
> Client sends SYN → Server responds SYN-ACK → Client sends ACK. Establishes reliable connection with sequence numbers.

**Q4: What is SO_REUSEADDR?**
> Allows binding to a port in TIME_WAIT state. Without it, restarting the server within ~60s fails with "Address already in use."

**Q5: Why htonl()/ntohl()?**
> Converts between host byte order (little-endian on x86) and network byte order (big-endian). Without it, a length of 6 could be misread as 100663296 on another architecture.

**Q6: What is the `accept()` return value?**
> A NEW file descriptor for the specific client connection. The listening socket continues accepting others. Think: receptionist (listening fd) vs dedicated phone lines (accepted fds).

**Q7: Why can't a single recv() get all the data?**
> TCP is a stream protocol. The kernel delivers whatever's available, which may be a partial message. You need a read-exactly loop.

**Q8: What is TIME_WAIT?**
> A TCP state lasting ~60s after closing, ensuring all in-flight packets reach their destination. Prevents port reuse without SO_REUSEADDR.

**Q9: What does INADDR_ANY mean?**
> Bind to all available network interfaces. The server accepts connections on any of its IP addresses (localhost, LAN, etc).

**Q10: What's the `listen()` backlog?**
> The max pending connections queued before accept() is called. NOT the max total connections.

---

## 18. Interview Questions — Process Management

**Q11: What does fork() return?**
> Two values to two processes: >0 (child's PID) to parent, 0 to child, -1 on error.

**Q12: What is Copy-On-Write?**
> After fork(), parent and child share physical memory pages marked read-only. A page is physically copied only when written to. Makes fork+exec nearly free since the child immediately replaces its memory.

**Q13: fork() vs vfork()?**
> vfork() shares the parent's address space entirely (parent suspended until child calls exec/exit). Mostly obsolete because COW makes fork() nearly as fast.

**Q14: What happens if you call exec() without fork()?**
> The current process is permanently replaced. The Worker would cease to exist.

**Q15: Difference between execvp() and execve()?**
> execvp: takes argv array, searches PATH. execve: takes argv + custom environment, needs full path. execve is the only true syscall — all others are wrappers.

**Q16: Why _exit() instead of exit() in the child?**
> exit() flushes stdio buffers and runs atexit handlers — shared with parent via fork. This causes double-flushing. _exit() terminates immediately without cleanup.

**Q17: What is a zombie process?**
> A terminated child whose exit status hasn't been read by waitpid(). Occupies a process table slot. Fixed by calling waitpid().

**Q18: What is an orphan process?**
> A child whose parent died. Adopted by init (PID 1), which reaps it. Not harmful.

**Q19: waitpid() vs wait()?**
> wait() waits for ANY child. waitpid() waits for a SPECIFIC child and supports WNOHANG (non-blocking).

**Q20: What is WNOHANG?**
> Makes waitpid() non-blocking — returns immediately if no child has exited. Useful in event loops.

**Q21: How do you get the exit code after waitpid()?**
> Use macros: WIFEXITED(status) checks normal exit, WEXITSTATUS(status) gets the code.

**Q22: What is exit code 127?**
> Convention for "command not found" (same as bash). We use it when execvp() fails.

---

## 19. Interview Questions — I/O and Pipes

**Q23: How does dup2() work?**
> `dup2(old, new)` makes fd `new` point to the same file/pipe as `old`. After `dup2(pipe, STDOUT)`, writes to stdout go into the pipe.

**Q24: What is a pipe vs a socket?**
> Pipe: unidirectional, between related processes (parent-child). Socket: bidirectional, between any processes, even across networks.

**Q25: Why close unused pipe ends?**
> (1) read() gets EOF only when ALL write ends are closed. If parent keeps write end open, read never returns EOF. (2) Conserve file descriptors.

**Q26: What is the pipe buffer size?**
> Typically 64KB on Linux. If full, write() blocks. If empty, read() blocks.

**Q27: How does the shell implement `ls | grep "txt"`?**
> Creates a pipe, forks twice. ls child: `dup2(pipe_write, stdout)` + `exec("ls")`. grep child: `dup2(pipe_read, stdin)` + `exec("grep")`. Same technique we use.

**Q28: What is SIGPIPE?**
> Signal sent when writing to a pipe/socket where the reader has closed. Default action: terminate. Fix: ignore SIGPIPE or use MSG_NOSIGNAL.

---

## 20. Interview Questions — C++ Concepts

**Q29: What is RAII?**
> Resource Acquisition Is Initialization. Resources (fds, memory, locks) are acquired in constructors and released in destructors. Guarantees cleanup even during exceptions. Our Socket class is an example.

**Q30: Why is your Socket class movable but not copyable?**
> Two Socket objects with the same fd would double-close it. Like `std::unique_ptr`, only one object can own the resource. Move semantics transfer ownership without copying.

**Q31: Explain move semantics with your Socket.**
> `Socket(Socket&& other)` transfers the fd and sets `other.fd_ = -1`. The source gives up ownership. No fd duplication occurs. This is efficient for returning Socket from functions.

**Q32: What's the Rule of Five?**
> If a class defines any of: destructor, copy constructor, copy assignment, move constructor, move assignment — it should define all five. Our Socket defines destructor + move pair and deletes the copy pair.

**Q33: Why std::vector over a raw array for workers?**
> Dynamic sizing (workers join/leave at runtime), bounds checking, automatic memory management, works with range-based for loops, has erase() for removal.

**Q34: How does std::queue work internally?**
> It's a container adapter wrapping `std::deque` by default. Provides FIFO operations: push() (back), front(), pop() (front). O(1) amortized for all operations.

**Q35: Why namespace rather than a class with static methods?**
> Namespaces are for organizing code without instantiation overhead. `protocol::send_message()` doesn't need object state. Static class methods would also work but are less idiomatic in C++.

**Q36: What does `explicit` on the Socket constructor do?**
> Prevents implicit conversion: `Socket s = 5;` is rejected. Forces `Socket s(5);` or `Socket s{5}`. Prevents bugs from accidental type conversions.

**Q37: Why emplace_back() instead of push_back()?**
> `emplace_back(fd, ip)` constructs the Worker in-place inside the vector. `push_back()` first creates a temporary Worker, then copies/moves it. emplace_back is more efficient.

**Q38: What's the difference between std::string and C strings?**
> std::string: automatic memory, knows its length, no buffer overflow, supports concatenation with `+`. C strings: null-terminated char arrays, manual memory, vulnerable to overflow. We use std::string everywhere for safety.

---

## 21. Interview Questions — Python & Cross-Language

**Q39: Why did you write the client in Python?**
> "To demonstrate cross-language interoperability. The Python client implements the exact same wire protocol as C++, proving the protocol design is language-agnostic. Python's rapid prototyping also made it ideal for building the interactive CLI."

**Q40: How does struct.pack("!I", len) work?**
> `"!"` = network byte order (big-endian), `"I"` = unsigned 32-bit integer. This is the Python equivalent of `htonl()`. It converts an integer to 4 big-endian bytes.

**Q41: Python's sendall() vs C++'s send() loop?**
> `sendall()` automatically loops until all bytes are sent, handling partial sends internally. In C++, we write the loop manually. Same result, Python just wraps it.

**Q42: How did you ensure protocol compatibility?**
> Both languages use: (1) 4-byte big-endian length header, (2) UTF-8 payload encoding, (3) read-exactly loops for receiving. The `struct` module in Python matches `htonl`/`ntohl` in C++.

**Q43: What is the GIL and does it matter here?**
> Python's Global Interpreter Lock prevents true multithreading. But our client is single-threaded and I/O-bound (waiting on sockets), so the GIL has no impact. For CPU-bound parallel work, you'd use multiprocessing.

---

## 22. Interview Questions — Architecture & Design

**Q44: Why the Master-Worker model?**
> Separation of concerns: Master schedules, Workers execute. Scalable (add workers), and partially fault-tolerant (one worker fails, others continue).

**Q45: How does your load balancer work?**
> First-Available: linear scan, pick first worker where `is_busy == false`. O(n). Simple and sufficient for our scale.

**Q46: What happens when all workers are busy?**
> Tasks are queued in `std::queue`. When a worker sends back a result, the next queued task is auto-dispatched. This is the producer-consumer pattern.

**Q47: What if a worker crashes?**
> Master detects broken TCP (recv returns empty), removes the worker, task result is lost. To fix: task acknowledgment + retry logic + pending task list.

**Q48: Why select() instead of threads?**
> Avoids thread synchronization complexity (mutexes, race conditions, deadlocks). For our small number of connections, select() is simpler and correct.

**Q49: How would you make this production-ready?**
> (1) Thread pool or epoll for scalability. (2) Task persistence (database). (3) Worker heartbeats. (4) TLS encryption. (5) Task timeout + retry. (6) Web dashboard.

**Q50: What's the most challenging part of this project?**
> "The fork/dup2/execvp pipeline in the Worker. Getting the pipe ends, file descriptor redirections, and child/parent responsibilities right requires careful ordering. A single missed close() or wrong dup2() target causes subtle bugs — hung reads, missing output, or zombie processes."

---

## 23. How to Extend This Project

| Extension | Difficulty | Description |
|---|---|---|
| Task Priority Queue | Easy | `std::priority_queue` instead of `std::queue` |
| Worker Heartbeat | Medium | Periodic ping-pong to detect dead workers |
| Task Retry | Medium | Re-queue task if worker crashes |
| Task Timeout | Medium | `alarm()` + signal handler to kill hanging tasks |
| epoll() I/O | Medium | Replace select() with epoll for 10k+ connections |
| Thread Pool | Hard | `std::thread` pool in worker for parallel task execution |
| TLS Encryption | Hard | OpenSSL for encrypted communication |
| SQLite Persistence | Medium | Store tasks in DB — survive master restarts |
| REST Status API | Hard | HTTP `/status` endpoint returning JSON metrics |
| Prometheus Metrics | Hard | Expose task count, latency, worker stats |
| Python Worker | Easy | Implement worker in Python too (subprocess module) |
| Kubernetes Deployment | Hard | k8s Deployment + Service YAML for cloud deployment |

---

## 24. Glossary

| Term | Definition |
|---|---|
| **RAII** | Resource Acquisition Is Initialization — C++ pattern for automatic cleanup |
| **Socket** | An endpoint for network communication (represented as fd) |
| **TCP** | Reliable, ordered, connection-oriented byte-stream protocol |
| **fork()** | System call to create a new process (copy of parent) |
| **exec()** | System call to replace current process with a new program |
| **COW** | Copy-On-Write — memory optimization for fork() |
| **dup2()** | Duplicate a file descriptor (redirect I/O) |
| **pipe()** | Create a unidirectional data channel between processes |
| **waitpid()** | Wait for child process and retrieve exit status |
| **select()** | I/O multiplexing — monitor multiple fds for activity |
| **htonl()/ntohl()** | Host↔Network byte order conversion |
| **getaddrinfo()** | Resolves hostnames and IP strings to socket addresses (DNS-aware) |
| **std::vector** | Dynamic array with automatic memory management |
| **std::queue** | FIFO container adapter (wraps std::deque) |
| **Move semantics** | Transfer resource ownership without copying |
| **Zombie** | Terminated child not yet reaped by waitpid() |
| **struct.pack()** | Python binary packing — equivalent of htonl() |
| **GIL** | Python's Global Interpreter Lock |
| **LogLevel** | Severity classification: DEBUG < INFO < WARN < ERROR |
| **Singleton** | Design pattern ensuring exactly one instance of a class exists |
| **std::mutex** | Mutual exclusion primitive — only one thread holds it at a time |
| **std::lock_guard** | RAII wrapper for mutex — auto-releases on scope exit |
| **std::chrono** | C++ time library for timestamps and duration measurement |
| **isatty()** | Returns true if a file descriptor is connected to a terminal |
| **ANSI escape codes** | Terminal control sequences for colors (`\033[32m` = green) |
| **Container** | Isolated process with its own filesystem, network, and PID namespace |
| **Docker Image** | Read-only filesystem snapshot — blueprint for containers |
| **Dockerfile** | Instructions for building a Docker image layer by layer |
| **Multi-stage build** | Using multiple FROM stages to separate build and runtime environments |
| **docker-compose** | Tool for defining and running multi-container Docker applications |
| **Healthcheck** | Command Docker runs to verify a container is actually functional (not just running) |
| **Bridge network** | Docker virtual network allowing containers to communicate by service name |
| **CMake** | Cross-platform build system generator for C++ projects |
| **find_package** | CMake command to locate external libraries (e.g., Threads, OpenSSL) |
| **target_link_libraries** | CMake command to link a library against an executable or library target |

---

## 25. Structured Logging — logger.hpp

### Why Structured Logging?

The original code used raw print statements:
```cpp
std::cout << "[MASTER] Worker connected\n";
perror("connect failed");
```

Problems with this approach:
- **No timestamp** — you don't know WHEN something happened
- **No severity** — is this routine info or a critical error?
- **No consistent format** — hard to grep, parse, or feed into log aggregators
- **Not thread-safe** — two threads printing simultaneously produce garbled output
- **No filtering** — can't suppress DEBUG messages in production

Structured logging fixes all of this with zero external dependencies.

### What Was Added

A new `logger.hpp` header-only library providing:

| Feature | Implementation |
|---|---|
| 4 log levels | `enum class LogLevel { DEBUG, INFO, WARN, ERROR }` |
| Millisecond timestamps | `std::chrono::system_clock` + `localtime_r` |
| Component labels | `[MASTER]`, `[WORKER]` |
| ANSI colors | Terminal only (auto-detected via `isatty`) |
| Thread safety | `std::mutex` + `std::lock_guard` |
| Stream API | `LOG_INFO("MASTER") << "msg " << value` |

### What the Output Looks Like

```
[2026-05-17 14:32:01.123] [INFO ] [MASTER] Listening on port 8080
[2026-05-17 14:32:03.456] [INFO ] [MASTER] Worker #0 connected from 172.18.0.3 (pool size: 1)
[2026-05-17 14:32:08.789] [INFO ] [MASTER] Dispatching to Worker #0 [172.18.0.3]: "uptime"
[2026-05-17 14:32:09.012] [INFO ] [WORKER] Task completed in 223ms — sending result
[2026-05-17 14:32:09.015] [WARN ] [MASTER] All workers busy — task queued (2 pending)
[2026-05-17 14:32:12.234] [ERROR] [WORKER] Failed to send result — connection lost
```

In a terminal, INFO lines are green, WARN is yellow, ERROR is red, DEBUG is cyan. In Docker logs, all output is plain text (no escape codes).

### Design #1: The Singleton Pattern

```cpp
class Logger {
public:
    static Logger& get() {
        static Logger instance;  // Created once, on first call
        return instance;         // All callers get the same object
    }
private:
    Logger() = default;  // Private — only get() can create it
};
```

**Why Singleton?** A logger is a global resource. All parts of the program — master, workers, different classes — should write to the same output with consistent formatting. The Singleton ensures exactly one Logger exists without the problems of a global variable (it's lazy-initialized and encapsulated).

**Thread safety of static locals**: In C++11 and later, the initialization of a function-local `static` variable is guaranteed by the standard to be thread-safe. Even if two threads call `Logger::get()` simultaneously for the first time, only one initialization runs. No manual locking required here.

### Design #2: The LogStream Pattern

This is the clever part. The macro:
```cpp
LOG_INFO("MASTER") << "Worker #" << i << " connected";
```

expands to:
```cpp
LogStream(LogLevel::INFO, "MASTER") << "Worker #" << i << " connected";
```

Here is how it works step by step:

**Step 1** — The macro creates a temporary `LogStream` object on the stack:
```cpp
LogStream temp(LogLevel::INFO, "MASTER");
// temp holds: level, component, empty ostringstream
```

**Step 2** — Each `<<` operator appends to an internal `ostringstream`:
```cpp
LogStream& operator<<(const T& val) {
    ss_ << val;   // Append to internal buffer
    return *this; // Return self so chaining works
}
// After all <<: ss_ contains "Worker #2 connected"
```

**Step 3** — At the semicolon, the temporary is destroyed. The **destructor** fires the actual log call:
```cpp
~LogStream() {
    Logger::get().log(level_, component_, ss_.str());
    // This is where the message is actually written
}
```

**Why this design works**: C++ guarantees that temporary objects are destroyed at the end of the full-expression (the semicolon). So the message is completely assembled before it's sent to the logger. This is the same technique used by Google's `glog` library.

### Design #3: Thread Safety with std::mutex

```cpp
class Logger {
    std::mutex mutex_;

    void log(LogLevel level, const std::string& component, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        // ↑ Acquires mutex here. Other threads block if they try to log.
        
        std::cout << timestamp() << " " << level_tag(level)
                  << " [" << component << "] " << msg << "\n";
        
        // lock goes out of scope here → destructor releases the mutex
    }
};
```

**Why this matters**: Without the mutex, consider two threads logging at the same time:

```
Thread A writing: "[2026-05-17..."
Thread B writing: "[2026-05-17..."
Result: "[2026-05-17[2026-05-17..." ← interleaved garbage
```

`std::lock_guard` is RAII for mutexes — it acquires the mutex in its constructor and releases in its destructor. Even if an exception is thrown inside `log()`, the mutex is always released.

### Design #4: Timestamps with std::chrono

```cpp
static std::string timestamp() {
    using namespace std::chrono;

    // Get current wall clock time
    auto now = system_clock::now();

    // Extract millisecond sub-second part (0–999)
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    // Convert to broken-down time (year, month, day, hour, min, sec)
    std::time_t t = system_clock::to_time_t(now);
    struct tm tm_info{};
    localtime_r(&t, &tm_info);  // Thread-safe version of localtime()

    // Format: "2026-05-17 14:32:01"
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    // Append milliseconds: "2026-05-17 14:32:01.234"
    char result[32];
    std::snprintf(result, sizeof(result), "[%s.%03lld]", buf, (long long)ms.count());
    return result;
}
```

**system_clock**: Real wall-clock time. Can jump backward for DST/NTP. Good for human-readable timestamps.

**steady_clock**: Monotonic — always increases, never jumps. Used in `worker.cpp` for measuring task execution duration. Always use `steady_clock` for measuring elapsed time; never `system_clock`.

**localtime_r()**: Thread-safe version of `localtime()`. The non-`_r` version writes to a shared static buffer, causing data races in multi-threaded code. The `_r` version writes to your provided `struct tm`.

### Design #5: ANSI Color Codes and TTY Detection

ANSI escape sequences control terminal colors:

```
\033[32m  → Switch text color to green
\033[33m  → Switch text color to yellow
\033[31m  → Switch text color to red
\033[36m  → Switch text color to cyan
\033[0m   → Reset all attributes to default
```

`\033` is the ESC character (ASCII 27). When a terminal sees this sequence, it changes the rendering color. When a file or pipe sees it, it appears as literal characters like `^[[32m` — ugly and confusing.

**isatty() detection**:
```cpp
bool colors_ = isatty(STDOUT_FILENO);
// STDOUT_FILENO == 1 (standard output)
// Returns: 1 if connected to a terminal (TTY)
//          0 if connected to a file, pipe, or Docker log driver
```

In Docker, stdout is captured by the logging driver (not a terminal), so `isatty()` returns 0 and the logger uses plain text automatically. You get readable logs in `docker-compose logs` without garbage escape codes.

### The isatty() Fix for Docker Compatibility

The original `master.cpp` had a critical bug for Docker:

```cpp
// ORIGINAL: always reads stdin, exits on EOF
while (true) {
    ...
    if (!std::getline(std::cin, command)) break;  // ← EXITS on EOF!
}
```

In Docker, stdin is `/dev/null` (a file that immediately returns EOF). `std::getline` returns false on the first call, breaking the loop. **The master exits instantly before any worker can connect.**

**The fix in our master.cpp**:
```cpp
// Detect at startup whether we're in a terminal
bool interactive_ = isatty(STDIN_FILENO);

// In run(): only monitor stdin if interactive
if (interactive_) {
    FD_SET(STDIN_FILENO, &read_fds);
}

// In the stdin handler: on EOF, switch mode instead of exiting
if (!std::getline(std::cin, command)) {
    LOG_INFO("MASTER") << "stdin closed — switching to server-only mode";
    interactive_ = false;  // Continue running, just stop reading stdin
    continue;              // ← Don't break! Keep the server alive.
}
```

Now in Docker: the master ignores stdin entirely, listens on TCP port 8080, and accepts tasks from `client.py`. It runs indefinitely until explicitly stopped.

### Task Timing in worker.cpp

We added execution timing using `std::chrono::steady_clock`:

```cpp
auto t0 = std::chrono::steady_clock::now();
std::string result = TaskExecutor::execute(command);
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0).count();

LOG_INFO("WORKER") << "Task completed in " << ms << "ms — sending result";
```

This tells you exactly how long each command took to execute, which is invaluable for debugging slow tasks or bottlenecks.

---

## 26. Docker & Containerization — Complete Beginner Guide

### What Problem Does Docker Solve?

Every developer has hit this wall:

```
You:        "It works on my machine!"
Teammate:   "Well it crashes on mine."
CI server:  "Tests pass. Deploy fails."
Production: "503 Service Unavailable"
```

The root cause is always the same: **environment differences**. Your machine has Ubuntu 22.04, theirs has 20.04. You have gcc 12, they have gcc 9. A library you installed three months ago is missing on the server.

Docker solves this by packaging your application **together with its entire environment** into one portable unit called a **container**. The container runs identically everywhere Docker is installed — your laptop, a teammate's Windows machine (via WSL), a cloud server.

### What is a Container?

A container is an **isolated process** that:
- Has its own **filesystem** — sees only what you put in the image
- Has its own **network** — its own IP address, port namespace
- Has its own **process tree** — can't see or kill host processes
- **Shares the host OS kernel** — not a separate OS

Think of it like a "jail" or "sandbox" for your program. The process runs at near-native speed (no emulation) but is isolated from the rest of the system.

### Containers vs Virtual Machines — Explained Simply

A virtual machine (VM) runs a complete operating system (kernel + userspace) inside your OS. It virtualizes the hardware itself.

A container skips the kernel — it uses the host's kernel directly and only isolates the userspace (filesystem, network, processes).

```
Virtual Machine:
┌─────────────────────┐
│ Your App            │
│ Ubuntu 22.04        │ ← Full OS inside
│ Virtual Hardware    │
└─────────────────────┘
     ↕ Hypervisor
┌─────────────────────┐
│ Host OS             │
│ Physical Hardware   │
└─────────────────────┘

Container:
┌─────────────────────┐
│ Your App            │
│ Ubuntu filesystem   │ ← Just the userspace
└─────────────────────┘
     ↕ Host Kernel (shared)
┌─────────────────────┐
│ Host OS             │
│ Physical Hardware   │
└─────────────────────┘
```

| Aspect | Virtual Machine | Container |
|---|---|---|
| Includes | Full OS kernel | Shares host kernel |
| Boot time | 30 seconds to 5 minutes | Less than 1 second |
| Size | 1–50 GB | 10–500 MB |
| Performance | 70–90% native | 98–99% native |
| Isolation level | Complete (hardware-level) | Process-level |
| Use case | Full OS isolation | App packaging and deployment |

For our project: containers give us isolation and portability at near-native speed. Our C++ binary runs just as fast in a container as on bare metal.

### What is a Docker Image?

An image is the **blueprint** (template) for a container. It is a read-only, layered filesystem snapshot.

Think of it in OOP terms:
```
Image (class definition)  →  Container (object/instance)
Dockerfile (source code)  →  Image (compiled result)
```

Multiple containers can run from the same image — all three worker containers in our `docker-compose` share one image.

**Images are layered**: Each instruction in the Dockerfile adds a new layer on top of the previous one. Layers are cached and reused across builds. If you change only your source code (`COPY` layer), Docker reuses all the earlier layers (apt install layer, etc.) from cache — making rebuilds very fast.

```
Layer 5: COPY binaries from builder     ← your app (changes often)
Layer 4: RUN apt-get install python3    ← system packages (rarely changes)
Layer 3: FROM ubuntu:22.04              ← base OS (never changes)
```

### What is a Dockerfile?

A text file with step-by-step instructions for building a Docker image. Docker reads it top to bottom, executes each instruction, and produces an image. Each instruction creates a cached layer.

### Our Dockerfile — Every Line Explained

```dockerfile
# ── Stage 1: Build ────────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder
```

`FROM` is always first. It specifies the **base image** to start from. We use Ubuntu 22.04 because it has `apt` for package management and provides a stable Linux environment.

`AS builder` names this build stage "builder". We are using a **multi-stage build** (explained below), and we need this name so Stage 2 can reference it.

---

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ make \
    && rm -rf /var/lib/apt/lists/*
```

`RUN` executes a shell command **inside the container being built**. This creates a new image layer.

Why is everything in ONE `RUN` command with `&&`? Because each `RUN` creates one layer. If you did:
```dockerfile
RUN apt-get update              # Layer A: stores the package list cache (~20MB)
RUN apt-get install g++         # Layer B: installs compiler
RUN rm -rf /var/lib/apt/lists/* # Layer C: tries to delete the cache
```
Layer A still permanently stores the cache in the image — deleting it in a later layer doesn't remove it from earlier layers. The one-liner approach means the cache is never stored permanently.

`-y` means "answer yes to all prompts" — required because Docker builds are non-interactive (no one is at the keyboard).

`--no-install-recommends` skips optional "recommended" packages, keeping the image lean.

`rm -rf /var/lib/apt/lists/*` deletes the package index downloaded by `apt-get update` — we don't need it after installation. Saves ~20MB in the image.

---

```dockerfile
WORKDIR /build
```

Creates the `/build` directory if it doesn't exist and sets it as the working directory for all subsequent commands. Equivalent to `mkdir -p /build && cd /build`. Everything after this executes relative to `/build`.

---

```dockerfile
COPY protocol.hpp logger.hpp master.cpp worker.cpp ./
```

`COPY` copies files from your local machine (the **build context** — the directory where you run `docker build` or `docker-compose build`) into the image's `/build/` directory.

`./` means "the current WORKDIR", which is `/build`.

We explicitly list only the source files we need (not `COPY . .` which would copy everything including `.git/`, compiled binaries, etc.). This:
1. Keeps the context small and the build fast
2. Maximizes cache utilization — if only `master.cpp` changes, only that COPY layer is invalidated

---

```dockerfile
RUN g++ -std=c++17 -Wall -Wextra -O2 -pthread -o master master.cpp \
 && g++ -std=c++17 -Wall -Wextra -O2 -pthread -o worker worker.cpp
```

Compiles both binaries **inside the container**. The compilation environment is guaranteed — exact same g++ version, same Ubuntu 22.04, same libraries — regardless of the host machine.

`-O2` produces optimized code (not `-g` debug mode). `-pthread` links the POSIX threading library needed by `std::mutex` in logger.hpp.

---

```dockerfile
# ── Stage 2: Runtime ──────────────────────────────────────────────────────────
FROM ubuntu:22.04
```

**This is the key moment of a multi-stage build.** We start a brand-new, completely empty Ubuntu 22.04 image. Everything from Stage 1 — g++, make, source files, the entire `/build` directory — is gone. It never makes it into the final image.

### Why Multi-Stage Builds Matter

Without multi-stage (single-stage):
```
Your final image contains:
  g++ compiler (~300MB)
  make, headers
  Source code (.cpp, .hpp files)
  + compiled binaries
Total: ~800MB+
```

With multi-stage (what we do):
```
Stage 1 (builder): exists only during build, never shipped
Stage 2 (runtime): only contains what's needed to RUN
  Ubuntu 22.04 base
  python3
  master binary
  worker binary
  client.py
Total: ~180MB
```

The runtime image has **zero build tools**. This is also a security win — an attacker who compromises the running container cannot use your compiler to recompile malicious code.

---

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    && rm -rf /var/lib/apt/lists/*
```

Install Python3 in the runtime image so `client.py` can run inside the container. No g++ — the runtime only runs code, never compiles it.

---

```dockerfile
WORKDIR /app
COPY --from=builder /build/master /build/worker ./
COPY client.py ./
```

`COPY --from=builder` is the cross-stage copy — it reaches into Stage 1's filesystem and grabs specific files. We only take the compiled binaries. Source code stays in Stage 1 forever.

`client.py` is copied directly from our local machine (it's a Python script, no compilation needed).

After this, `/app` contains only: `master`, `worker`, `client.py`.

---

```dockerfile
EXPOSE 8080
```

This is **documentation only** — it does not actually open the port. It tells Docker and tools like docker-compose that this container intends to listen on port 8080. Think of it as a comment that tooling can read.

The actual port mapping (making port 8080 reachable from your laptop) is done in docker-compose via `ports: - "8080:8080"`.

---

```dockerfile
CMD ["./master"]
```

The default command to run when a container starts from this image. The array form (`["./master"]`) is preferred over the string form (`"./master"`) because:
- Array form runs the binary directly (no shell overhead)
- Signals like SIGTERM go to `./master` directly (not via a shell)
- No shell quoting issues

This can be overridden: `docker run <image> ./worker master` overrides CMD. That's how our worker containers use the same image but run a different command.

---

### Our docker-compose.yml — Every Line Explained

docker-compose lets you define a multi-container application in one YAML file and manage it with simple commands.

```yaml
version: '3.8'
```

The Compose file format version. Version 3.8 (requires Docker Engine 19.03+) supports the features we use: healthchecks with `condition`, `deploy.replicas`, etc.

---

```yaml
services:
  master:
    build: .
```

`services` is a dictionary of named container configurations. Each service defines how to build, configure, and run a container.

`build: .` means: build the image using the `Dockerfile` in the current directory (`.`). Alternative: `image: ubuntu:22.04` to use a pre-existing image.

---

```yaml
    container_name: scheduler-master
```

Assigns a fixed, predictable name to the container. Without this, Compose generates `<project>_master_1`. With a fixed name, you can always run `docker logs scheduler-master` or `docker exec -it scheduler-master bash` without guessing the name.

---

```yaml
    ports:
      - "8080:8080"
```

Port mapping in the format `"host_port:container_port"`. This makes port 8080 inside the container reachable as port 8080 on your laptop (`localhost:8080`).

Without `ports`, the master's port 8080 exists only inside the Docker network — unreachable from your laptop's `python3 client.py` command. Worker containers on the same network can still reach it (they use the internal network), but your local Python client cannot.

---

```yaml
    healthcheck:
      test: ["CMD-SHELL", "bash -c 'echo > /dev/tcp/localhost/8080' 2>/dev/null"]
      interval: 2s
      timeout: 2s
      retries: 15
      start_period: 3s
```

**What is a healthcheck?**

Docker tracks container state as: `starting` → `running` → `healthy` (or `unhealthy`). By default, a container moves to "running" as soon as the process starts — but the process might not be ready yet. Our master takes a moment to bind to port 8080 after startup.

A healthcheck is a command Docker runs periodically to check if the service is truly ready.

**The test command**: `bash -c 'echo > /dev/tcp/localhost/8080'`

Bash has a built-in `/dev/tcp` pseudo-filesystem. When bash opens `/dev/tcp/host/port`, it attempts a real TCP connection. If port 8080 is listening (our master socket is bound and accepting), the command succeeds (exit 0 = healthy). If not, it fails (exit 1 = unhealthy).

`2>/dev/null` suppresses error output so Docker logs stay clean.

**Why each parameter matters**:
- `interval: 2s` — check every 2 seconds
- `timeout: 2s` — if the check takes > 2s, count it as failed
- `retries: 15` — after 15 consecutive failures → mark as unhealthy (15 × 2s = 30s window)
- `start_period: 3s` — ignore failures in the first 3 seconds (startup grace)

**Why this is critical for us**:

Workers use `depends_on: master: condition: service_healthy`. Without the healthcheck, `depends_on` only waits for the master container to **start** — not for the master process to **bind the socket**. Workers would try to connect before port 8080 exists and crash.

Timeline with healthcheck:
```
t=0s:  master container starts → ./master begins executing
t=1s:  master calls socket() → bind() → listen() → port 8080 is ready
t=2s:  Docker runs healthcheck → port 8080 open → SUCCESS → master is HEALTHY
t=2s:  worker containers start (depends_on condition met)
t=4s:  workers run getaddrinfo("master") → connect(172.18.0.2:8080) → SUCCESS
```

---

```yaml
  worker:
    build: .
    command: ["./worker", "master"]
```

The worker service uses the **same image** as the master (same Dockerfile, same build). The `command` overrides the Dockerfile's `CMD ["./master"]`.

`"master"` is a **Docker hostname** — not an IP address. Inside the `scheduler-net` network, Docker's embedded DNS resolves `"master"` to the master container's IP (e.g., `172.18.0.2`). Workers find the master by name, not by hardcoded IP.

---

```yaml
    depends_on:
      master:
        condition: service_healthy
```

Workers won't start until the master passes its healthcheck. This requires a `healthcheck:` block on master to be defined. Without `condition: service_healthy`, it would only wait for the container to start (not for the port to be ready).

---

```yaml
    restart: on-failure
```

If a worker exits with a non-zero exit code (e.g., crashes, gives up on connecting), Docker automatically restarts it. Combined with our 10-retry connection logic in `worker.cpp`, this provides belt-and-suspenders reliability during startup.

Restart policy options:
- `no` (default): never restart
- `always`: restart no matter what
- `on-failure`: restart only on non-zero exit code
- `unless-stopped`: always restart except after manual `docker stop`

---

```yaml
networks:
  scheduler-net:
    driver: bridge
```

Creates a custom private network named `scheduler-net`. The `bridge` driver creates a software-defined Ethernet switch inside the Docker host.

**What the network does for us**:
1. Containers on the same network get private IPs (e.g., `172.18.0.x`)
2. Docker runs an embedded DNS server — containers can reach each other by **service name** (`master`, `worker`) instead of IP address
3. Traffic stays on the host — doesn't go to your physical network
4. Custom network isolates our containers from containers of other projects

Without a custom network, Compose uses a default network, which works but doesn't provide clean isolation.

### How Docker DNS Works — Hostname to IP

When our worker calls `connect("master", 8080)`, here is exactly what happens:

```
worker calls: getaddrinfo("master", "8080", ...)
                        ↓
OS sends DNS query to 127.0.0.11 (Docker's embedded DNS resolver, inside every container)
                        ↓
Docker DNS looks up "master" in scheduler-net service registry
                        ↓
Docker DNS responds: "master" → 172.18.0.2 (the master container's IP)
                        ↓
getaddrinfo() returns struct addrinfo with ai_addr = 172.18.0.2:8080
                        ↓
connect(fd, 172.18.0.2:8080) → TCP connection established
```

This is exactly how microservices work in production (Docker, Kubernetes) — services discover each other by name, not hardcoded IPs.

### getaddrinfo() vs inet_pton() — An Important Fix

The original worker code used `inet_pton()`:
```cpp
inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
```

`inet_pton` (IP text to network) **only converts IP address strings** like `"192.168.1.1"` or `"127.0.0.1"`. It does **not** do DNS resolution. Passing `"master"` to it returns an error and the worker exits.

We updated to `getaddrinfo()`:
```cpp
struct addrinfo hints{}, *res;
hints.ai_family   = AF_INET;        // We want IPv4
hints.ai_socktype = SOCK_STREAM;    // We want TCP
getaddrinfo("master", "8080", &hints, &res);
// Now works for BOTH:
//   Hostnames: "master", "localhost", "my-server.example.com"
//   IP strings: "192.168.1.1", "127.0.0.1"
```

`getaddrinfo()` is the modern, POSIX-standard, thread-safe way to resolve hosts. It handles:
- IPv4 addresses directly (`"192.168.1.1"`)
- Hostnames via DNS (`"master"`, `"google.com"`)
- Service names (`"http"` → port 80)
- IPv6 if requested

Always use `getaddrinfo()` over the deprecated `gethostbyname()` (not thread-safe) or `inet_pton()` (no DNS).

`freeaddrinfo(res)` must be called when done — `getaddrinfo` allocates a linked list of results.

### Essential Docker Commands Reference

```bash
# ─ Images ────────────────────────────────────────────────────
docker build -t scheduler .              # Build image tagged "scheduler"
docker build --no-cache -t scheduler .  # Force rebuild, ignore layer cache
docker images                           # List all images on your machine
docker rmi scheduler                    # Delete an image

# ─ Containers ────────────────────────────────────────────────
docker run -it scheduler ./master       # Run master interactively (stdin attached)
docker run -d scheduler ./worker master # Run worker in background (detached)
docker ps                               # List running containers
docker ps -a                            # List all containers (including stopped)
docker stop scheduler-master            # Gracefully stop a container (SIGTERM)
docker rm scheduler-master              # Remove a stopped container

# ─ Docker Compose ────────────────────────────────────────────
docker-compose build                    # Build all service images
docker-compose up                       # Start all services (foreground)
docker-compose up -d                    # Start all services (background/detached)
docker-compose up --scale worker=5      # Start master + 5 workers
docker-compose down                     # Stop and remove all containers
docker-compose restart worker           # Restart all worker containers

# ─ Logs ──────────────────────────────────────────────────────
docker-compose logs                     # Print all logs
docker-compose logs -f                  # Follow logs in real time (Ctrl+C to stop)
docker-compose logs -f master           # Follow master logs only
docker-compose logs --tail=50 worker    # Last 50 lines of worker logs

# ─ Interacting with running containers ───────────────────────
docker exec -it scheduler-master bash           # Get a shell inside master container
docker exec scheduler-master ./master           # Run a command in the container
docker exec -it scheduler-master python3 client.py master 8080

# ─ Inspection ────────────────────────────────────────────────
docker stats                            # Live resource usage (CPU, memory, network)
docker inspect scheduler-master         # Full JSON metadata for a container
docker network ls                       # List all Docker networks
docker network inspect scheduler_scheduler-net  # Inspect our network (IPs, etc.)

# ─ Cleanup ────────────────────────────────────────────────────
docker-compose down --rmi all           # Remove containers AND their images
docker system prune                     # Remove all stopped containers, unused images
docker system prune -a                  # Remove everything not currently running
```

### Step-by-Step: Running the Project with Docker

**Step 1 — Build the image** (only needed once, or after code changes):
```bash
docker-compose build
```

**Step 2 — Start master + 3 workers**:
```bash
docker-compose up --scale worker=3
```

The terminal shows structured logs from all containers:
```
scheduler-master   | [2026-05-17 14:32:01.123] [INFO ] [MASTER] Listening on port 8080
scheduler-master   | [2026-05-17 14:32:01.130] [INFO ] [MASTER] Waiting for workers to connect...
scheduler-master   | [2026-05-17 14:32:03.456] [INFO ] [MASTER] Worker #0 connected from 172.18.0.3 (pool size: 1)
scheduler-worker_1 | [2026-05-17 14:32:03.451] [INFO ] [WORKER] Connected to Master at master:8080
scheduler-worker_2 | [2026-05-17 14:32:03.489] [INFO ] [WORKER] Connected to Master at master:8080
scheduler-worker_3 | [2026-05-17 14:32:03.501] [INFO ] [WORKER] Connected to Master at master:8080
```

**Step 3 — Submit tasks** (from your laptop, in a new terminal):
```bash
python3 client.py 127.0.0.1 8080
>>> uptime
>>> ls -la /
>>> date
>>> quit
```

The task goes: your terminal → master container (port 8080) → worker container → command executes → result returns.

**Step 4 — Scale workers dynamically** (no restart needed):
```bash
docker-compose up --scale worker=6     # Add 3 more workers
docker-compose up --scale worker=2     # Scale back down
```

**Step 5 — Stop everything**:
```bash
docker-compose down
```

---

## 27. CMake Build System

### What is CMake?

CMake is a **build system generator** for C++ projects. It doesn't compile code directly — it generates build files suited for your platform:
- Linux/macOS: `Makefile` (then use `make`)
- Windows: Visual Studio `.sln` project
- Any platform: `build.ninja` (Ninja build system)

```
CMakeLists.txt  →  cmake ..  →  Makefile (Linux)
                             →  .sln (Windows)
                             →  build.ninja (Ninja)
                             →  then run: make / msbuild / ninja
```

### Why CMake over a Plain Makefile?

| Feature | Makefile | CMake |
|---|---|---|
| Cross-platform | Linux/macOS only | Windows, macOS, Linux |
| IDE integration | None | VS Code, CLion, Xcode, Visual Studio |
| Library detection | Manual flags | `find_package(OpenSSL)` |
| Out-of-source builds | Awkward | First-class (`mkdir build && cd build`) |
| C++ standard | Manual flag | `CMAKE_CXX_STANDARD 17` |
| Industry adoption | Legacy projects | Modern C++ standard |

For a resume project targeting software engineering roles, CMake signals that you write production-level C++ — not just homework code.

### Our CMakeLists.txt — Every Line Explained

```cmake
cmake_minimum_required(VERSION 3.16)
```

The minimum CMake version required. If an older CMake is installed, it prints an error and stops. 3.16 (December 2019) is widely available and supports all features we use. This prevents silent incompatibilities with very old CMake installations.

---

```cmake
project(DistributedTaskScheduler
    VERSION 1.0.0
    DESCRIPTION "Distributed task scheduler — master/worker architecture over raw TCP"
    LANGUAGES CXX
)
```

Declares the project. Sets useful variables:
- `PROJECT_NAME` = `DistributedTaskScheduler`
- `PROJECT_VERSION` = `1.0.0`
- `PROJECT_DESCRIPTION` = the description string

`LANGUAGES CXX` tells CMake we only use C++. It skips looking for a C compiler (faster configure step).

---

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

- `CMAKE_CXX_STANDARD 17`: Use C++17 features.
- `CMAKE_CXX_STANDARD_REQUIRED ON`: If the compiler doesn't support C++17, error out — don't silently fall back to C++14.
- `CMAKE_CXX_EXTENSIONS OFF`: Use strict ISO standard (`-std=c++17`), not compiler extensions (`-std=gnu++17`). GNU extensions add non-portable features. Disabling them ensures code works with Clang, MSVC, and future compilers.

---

```cmake
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
```

If no build type is specified on the command line, default to `Release`.

Build types and their compiler flags:
| Type | Flags | Use for |
|---|---|---|
| `Debug` | `-g` (debug symbols, no optimization) | Local debugging with gdb |
| `Release` | `-O2 -DNDEBUG` (optimized, no debug) | Production / demo |
| `RelWithDebInfo` | `-O2 -g` | Profiling (optimization + symbols) |
| `MinSizeRel` | `-Os` | Embedded systems / space-constrained |

`CACHE STRING ... FORCE` stores the value in CMake's `CMakeCache.txt` so it persists across reconfiguration.

---

```cmake
add_compile_options(-Wall -Wextra -Wpedantic)
```

Add warning flags to ALL targets in the project.
- `-Wall`: Enable most common warnings (unused variables, implicit fallthrough, etc.)
- `-Wextra`: Additional warnings beyond `-Wall`
- `-Wpedantic`: Enforce strict ISO C++ — warns on any non-standard extensions

These warnings catch bugs before they become runtime errors.

---

```cmake
find_package(Threads REQUIRED)
```

CMake searches for the system's threading library.
- Linux: finds `-lpthread` (POSIX threads)
- macOS: `-lpthread` or built-in
- Windows: Win32 threads

`REQUIRED` means: if threads aren't found, stop with an error. (They're always found on any real OS, so this is defensive.)

**Why do we need this?** Our `logger.hpp` uses `std::mutex`. On Linux, `std::mutex` is implemented using pthreads. Without linking `-lpthread`, you get a linker error:
```
undefined reference to `pthread_mutex_lock'
```

The `find_package(Threads)` creates a CMake imported target `Threads::Threads` that abstracts the platform-specific linking.

---

```cmake
set(HEADERS protocol.hpp logger.hpp)

add_executable(master master.cpp ${HEADERS})
target_link_libraries(master PRIVATE Threads::Threads)

add_executable(worker worker.cpp ${HEADERS})
target_link_libraries(worker PRIVATE Threads::Threads)
```

`add_executable(name source1 source2 ...)`: Define a build target — an executable that CMake will compile and link.

Including `${HEADERS}` in sources: Headers aren't compiled directly, but listing them makes them visible in IDEs (they appear in the project tree) and CMake tracks them for rebuild purposes.

`target_link_libraries(master PRIVATE Threads::Threads)`:
- Link the `master` executable against the threading library
- `Threads::Threads` is CMake's imported target — it automatically adds `-lpthread` (or the equivalent) on the right platform
- `PRIVATE` means this dependency is internal to `master` — it doesn't propagate to things that link against `master`

**PRIVATE vs PUBLIC vs INTERFACE**:
- `PRIVATE`: Link only for this target (internal use)
- `PUBLIC`: Link for this target AND propagate to anything that links against this target
- `INTERFACE`: Don't link for this target, but propagate to dependents (for header-only libraries)

---

```cmake
install(TARGETS master worker RUNTIME DESTINATION bin)
install(FILES client.py DESTINATION bin)
```

Defines installation rules for `cmake --install`. When run:
- Copies `master` and `worker` binaries to `${CMAKE_INSTALL_PREFIX}/bin` (default: `/usr/local/bin`)
- Copies `client.py` to the same location

This makes the project properly installable on a system. Run with:
```bash
cmake --install build/                      # Install to /usr/local/bin
cmake --install build/ --prefix ~/myapp    # Custom install prefix
```

---

```cmake
message(STATUS "---------------------------------------")
message(STATUS " Distributed Task Scheduler v${PROJECT_VERSION}")
message(STATUS " Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS " Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "---------------------------------------")
```

`message(STATUS ...)` prints during the cmake configure step (not during compilation). Shows build summary so you immediately know what you're building.

`${VARIABLE}`: CMake variable substitution, like `${PROJECT_VERSION}` → `1.0.0`.

### How to Build with CMake — Step by Step

```bash
# Step 1: Create a build directory (out-of-source build)
mkdir build
cd build

# Step 2: Configure — CMake reads CMakeLists.txt and generates Makefile
cmake ..
# Output:
# -- ─────────────────────────────────────
# --  Distributed Task Scheduler v1.0.0
# --  Build type : Release
# --  C++ standard: C++17
# --  Compiler   : GNU 12.3.0
# -- ─────────────────────────────────────
# -- Configuring done
# -- Build files have been written to: /build

# Step 3: Compile (parallel — uses all CPU cores)
make -j$(nproc)
# OR (platform-independent):
cmake --build . -j4

# Binaries are now in build/: ./master and ./worker

# Step 4: Optional — install to system
cmake --install .

# Rebuild after code changes (only recompiles changed files):
cmake --build .
```

### Out-of-Source Builds — Why They Matter

A plain Makefile puts all generated files (object files, binaries) in the same directory as your source code:
```
source/
├── master.cpp
├── master          ← binary mixed with source!
├── worker.cpp
├── worker
└── Makefile
```

CMake builds out-of-source by default:
```
source/
├── master.cpp      ← clean source tree
├── worker.cpp
├── CMakeLists.txt
└── build/          ← all generated files here
    ├── master
    ├── worker
    ├── Makefile
    └── CMakeFiles/
```

Benefits:
- `git status` only shows actual source changes (not build artifacts)
- Run `rm -rf build/` to completely clean — no risk of touching source
- Keep multiple build configurations simultaneously: `build-debug/` and `build-release/`
- `git clean -fd` is safe (build artifacts are in `build/`, not the root)

---

## 28. Interview Questions — Logging, Docker & CMake (Q51–Q75)

### Logging (Q51–Q57)

**Q51: Why use structured logging instead of printf/cout?**
> "Structured logging adds three things raw prints lack: timestamps (when did it happen?), severity levels (how serious?), and consistent format (grep-able, parseable by Splunk/ELK). It's also thread-safe — multiple components can log simultaneously without garbled output. A recruiter looking at `docker-compose logs -f` immediately sees timestamped, color-coded, labeled output instead of a wall of unlabeled text."

**Q52: Explain the LogStream destructor pattern in logger.hpp.**
> "The macro `LOG_INFO('MASTER') << 'msg'` creates a temporary `LogStream` on the stack. Each `operator<<` appends to an internal `ostringstream`. At the semicolon, the temporary is destroyed. The destructor calls `Logger::get().log(level, component, ss_.str())` — this is where the actual write happens. C++ guarantees temporaries are destroyed at the end of the full-expression. The same pattern is used by Google's glog."

**Q53: Why does the Logger use a Singleton, and is it thread-safe?**
> "A logger is a global shared resource. The Singleton ensures exactly one instance exists, initialized lazily. We use a static local variable (Meyers' Singleton): `static Logger instance;`. In C++11 and later, this initialization is guaranteed thread-safe by the standard — multiple threads calling `get()` simultaneously will only initialize it once, with no race condition."

**Q54: What is std::lock_guard and why does logger.hpp use it?**
> "`std::lock_guard<std::mutex>` is an RAII mutex wrapper. It acquires the mutex in its constructor and releases it when it goes out of scope (end of the `log()` function), even if an exception is thrown. Without it, two threads logging simultaneously could interleave their characters in the output. With it, each complete log line is written atomically."

**Q55: What is the difference between system_clock and steady_clock in std::chrono?**
> "`system_clock` is wall-clock time — what you'd read on a watch. It can jump backward for DST adjustments or NTP corrections. `steady_clock` is monotonic — it always increases, never jumps. We use `system_clock` for log timestamps (human-readable wall time) and `steady_clock` for measuring task execution duration. Never use `system_clock` to measure elapsed time — use `steady_clock`."

**Q56: How does isatty() make the logger smarter?**
> "ANSI color escape codes look beautiful in a terminal but appear as garbage characters like `^[[32m` in log files, Docker logs, and CI pipelines. `isatty(STDOUT_FILENO)` returns true only when stdout is connected to a terminal. The logger checks this once at startup and automatically enables colors for terminals and plain text for everything else — zero configuration needed."

**Q57: How did you fix the master for running inside Docker?**
> "In Docker, stdin is `/dev/null` — getline returns EOF immediately and the original master exited. Fix: use `isatty(STDIN_FILENO)` to detect if we're in a terminal. If true (interactive terminal), add stdin to select() and read commands. If false (Docker/pipe), skip stdin entirely and run as a pure TCP server — workers connect and clients use `client.py`. On EOF, set `interactive_ = false` and continue running instead of breaking the loop."

---

### Docker (Q58–Q68)

**Q58: What is Docker and why did you add it to this project?**
> "Docker packages an application plus its entire environment — OS libraries, compiler output, Python interpreter — into a portable container. Without Docker, running this project requires: compiling C++17 on a Linux system, having the right g++ version, having Python3, managing paths. With Docker: `docker-compose up --scale worker=3` starts the full cluster on any machine. That's the production mindset recruiters look for."

**Q59: What is the difference between a Docker image and a container?**
> "An image is a read-only filesystem snapshot — like a class definition. A container is a running instance of an image — like an object. Multiple containers run from the same image (our worker service scales to 5 containers from one image). Images are built from Dockerfiles; containers are created from images and run until stopped."

**Q60: Explain multi-stage Docker builds and why they matter.**
> "Multi-stage builds use multiple `FROM` instructions. Stage 1 (builder) installs g++, compiles the binaries — these are large build-time dependencies. Stage 2 (runtime) starts fresh from a clean Ubuntu image and only copies the compiled binaries using `COPY --from=builder`. Result: the final image has zero compiler tools. Size drops from ~800MB to ~180MB. This is also a security improvement — no compiler means an attacker can't recompile malicious code in the running container."

**Q61: What does EXPOSE do in a Dockerfile, and what doesn't it do?**
> "EXPOSE is documentation — it records that the container intends to use that port. It does NOT publish the port or create any firewall rule. Actual port exposure requires `ports: - '8080:8080'` in docker-compose or `-p 8080:8080` in `docker run`. Think of EXPOSE as a structured comment that tools can read; it's not an operative instruction."

**Q62: What is the difference between CMD and ENTRYPOINT?**
> "ENTRYPOINT sets a fixed command that always runs. CMD provides default arguments that can be overridden. In our Dockerfile, `CMD ['./master']` means: run master by default. `docker run <img> ./worker master` overrides CMD entirely. If we used ENTRYPOINT, the override would be appended as arguments to ENTRYPOINT. We use CMD for flexibility — the same image can run as master or worker."

**Q63: What is docker-compose and what problem does it solve here?**
> "docker-compose orchestrates multi-container applications. Without it, starting this project requires 4+ manual `docker run` commands, manual network creation, and careful startup ordering. With docker-compose, the entire cluster — master, 3 workers, networking, healthchecks, restart policies — starts with one command: `docker-compose up --scale worker=3`. It's the difference between operating a system and configuring one."

**Q64: What is a Docker healthcheck and why is it critical in this project?**
> "A healthcheck is a command Docker periodically runs inside the container to verify the service is truly functional (not just started). We use it on master: `bash -c 'echo > /dev/tcp/localhost/8080'` — this succeeds only if port 8080 is listening. Workers use `depends_on: condition: service_healthy`, so they wait until master's socket is ready before attempting to connect. Without this, workers would start before `./master` finishes `bind()` and fail to connect."

**Q65: How does Docker networking enable container-to-container communication?**
> "We define a custom bridge network `scheduler-net`. Docker runs an embedded DNS server at `127.0.0.11` inside every container. Containers on the same network can reach each other by service name — `connect('master', 8080)` resolves 'master' to the master container's IP (e.g., 172.18.0.2) via Docker DNS. The host machine's public network is never involved. Ports are only exposed to the host via the explicit `ports:` mapping."

**Q66: Why did you replace inet_pton() with getaddrinfo() in the worker?**
> "`inet_pton()` only converts IP address strings — it doesn't do DNS. When the worker runs in Docker and connects to hostname 'master', `inet_pton('master', ...)` fails immediately. `getaddrinfo()` performs actual DNS resolution, handling both IP strings and hostnames. It's the modern POSIX-standard API, thread-safe, and supports IPv4, IPv6, and service name resolution. We also `freeaddrinfo(res)` to avoid a memory leak."

**Q67: What does restart: on-failure do and why do workers use it?**
> "`restart: on-failure` tells Docker to automatically restart the container if it exits with a non-zero exit code. Workers use it as a safety net: even with a healthcheck, edge cases exist where a worker might start before the master socket is fully ready. The worker exits (non-zero), Docker restarts it, it retries — combined with the 10-attempt retry loop in worker.cpp, it will eventually connect."

**Q68: How does --scale worker=5 work and what changes in your system?**
> "`docker-compose up --scale worker=5` launches 5 instances of the worker service, each in its own container with a unique IP address. They all use the same image, connect to the same master service name ('master'), and register themselves with the master. The master's `workers_` vector grows to 5 entries. Load balancing now has 5 options. Workers are named `worker_1` through `worker_5` automatically."

---

### CMake (Q69–Q75)

**Q69: What is CMake and why is it preferred over a plain Makefile?**
> "CMake is a build system generator — it produces platform-appropriate build files from a single, platform-neutral `CMakeLists.txt`. A Makefile only works on Linux/macOS. CMake generates Makefiles on Linux, Visual Studio projects on Windows, Xcode projects on macOS. It integrates with every major C++ IDE, handles `find_package` for third-party libraries automatically, and is the standard for modern C++ projects in industry."

**Q70: What does find_package(Threads REQUIRED) do and why do we need it?**
> "`find_package(Threads)` locates the platform's thread library. On Linux it's `-lpthread`. Without it, `std::mutex` in `logger.hpp` causes a linker error: `undefined reference to 'pthread_mutex_lock'`. The `Threads::Threads` imported target automatically adds the correct linker flags for the current platform — making the build cross-platform without manual flag management."

**Q71: Explain PRIVATE vs PUBLIC vs INTERFACE in target_link_libraries.**
> "These control dependency propagation. `PRIVATE`: the library is used internally by this target only — not propagated to targets that link against ours. `PUBLIC`: used internally AND propagated. `INTERFACE`: not used by this target but propagated to dependents (for header-only libraries). We use `PRIVATE Threads::Threads` because pthread is an internal implementation detail of master and worker — nothing needs to know about it from outside."

**Q72: What are CMake build types and which do you use?**
> "Build type controls compiler flags. `Debug`: `-g` debug symbols, no optimization — for gdb. `Release`: `-O2 -DNDEBUG` optimized, no debug symbols — for deployment. `RelWithDebInfo`: `-O2 -g` — optimization plus symbols for profiling. `MinSizeRel`: minimize binary size. We default to `Release` in CMakeLists.txt for optimized binaries. Override: `cmake .. -DCMAKE_BUILD_TYPE=Debug`."

**Q73: What does CMAKE_CXX_EXTENSIONS OFF do?**
> "Without it, GCC compiles with `-std=gnu++17` which enables GNU-specific extensions beyond ISO C++17. With `OFF`, it uses strict `-std=c++17`. Disabling extensions ensures code is portable to Clang, MSVC, and future compilers that may not support those GNU-specific behaviors. It's a correctness and portability guarantee."

**Q74: What is an out-of-source build and why is it better?**
> "A Makefile build pollutes the source tree with object files, binaries, and generated files — these appear in `git status` and must be manually gitignored. An out-of-source CMake build puts everything in a separate directory (`build/`). `git status` only shows real source changes. `rm -rf build/` performs a complete clean. You can have `build-debug/` and `build-release/` simultaneously. The source tree stays clean."

**Q75: How would you add unit tests to this CMake project?**
> "Use CMake's built-in testing support: `enable_testing()`, then `find_package(GTest)` or `FetchContent_Declare(googletest ...)`. Create a test executable: `add_executable(test_protocol test_protocol.cpp)`, link with `GTest::GTest GTest::Main` and `Threads::Threads`. Register: `add_test(NAME test_protocol COMMAND test_protocol)`. Run with `ctest -V` after building. CMake integrates this with CI — `cmake --build . && ctest` is the standard CI build+test command."

---

*Last Updated: May 2026*
*Project: Distributed Task Scheduler — C++ & Python — Interview Preparation*
*New sections: Structured Logging (25), Docker (26), CMake (27), Interview Q51–Q75 (28)*
