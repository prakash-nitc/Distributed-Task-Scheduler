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
├── protocol.hpp      # Shared: RAII Socket, Worker struct, wire protocol
├── master.cpp        # Master class: server, select loop, queue, LB
├── worker.cpp        # TaskExecutor + WorkerNode classes
├── client.py         # Python client: TaskClient class
├── Makefile          # g++ -std=c++17 -Wall -Wextra
└── project_bible.md  # This file
```

### protocol.hpp — Key Design

- `namespace scheduler` wraps everything
- `Socket` class: RAII, movable, not copyable
- `Worker` struct: fd, ip (std::string), is_busy
- `protocol::send_message()` / `protocol::recv_message()` — return std::string

### master.cpp — Master Class

```cpp
class Master {
    Socket server_fd_;                    // RAII — auto-closes
    std::vector<Worker> workers_;         // Dynamic registry
    std::queue<std::string> task_queue_;  // Pending tasks buffer

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
};

class WorkerNode {
    Socket master_fd_;      // RAII connection to master
    void run();             // recv → execute → send loop
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
| Worker crashes mid-task | Master detects broken TCP, removes worker, task result lost |
| Master crashes | Workers detect disconnect (recv returns empty), exit gracefully |
| execvp fails (bad command) | Error written to stderr (redirected to pipe), exit code 127 |
| All workers busy | Task queued in `std::queue`, auto-dispatched when worker frees up |
| Partial recv() | Read-exactly loops in both C++ and Python handle TCP fragmentation |
| EINTR (signal interrupt) | select() is retried on EINTR |

---

## 16. How to Build and Run

```bash
# Build (in WSL)
cd '/mnt/c/project/Distributed Task Scheduler'
make

# Terminal 1: Master
./master

# Terminal 2: C++ Worker
./worker                    # default: 127.0.0.1
./worker 192.168.1.100      # remote Master IP

# Terminal 3: Python Client
python3 client.py                      # default: 127.0.0.1:8080
python3 client.py 192.168.1.100 8080   # remote Master

# Commands to try:
uptime       ls -la       date       whoami
echo "hello"    cat /etc/hostname
status       quit
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
| Thread Pool Master | Hard | `std::thread` or `pthread` instead of select() |
| TLS Encryption | Hard | OpenSSL for encrypted communication |
| Web Dashboard | Hard | HTTP endpoint for monitoring |
| Python Worker | Easy | Implement worker in Python too (subprocess module) |

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
| **std::vector** | Dynamic array with automatic memory management |
| **std::queue** | FIFO container adapter (wraps std::deque) |
| **Move semantics** | Transfer resource ownership without copying |
| **Zombie** | Terminated child not yet reaped by waitpid() |
| **struct.pack()** | Python binary packing — equivalent of htonl() |
| **GIL** | Python's Global Interpreter Lock |

---

*Last Updated: April 2026*
*Project: Distributed Task Scheduler — C++ & Python — Interview Preparation*
